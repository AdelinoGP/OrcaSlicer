// [INTENT] Header for the "normal" (non-tree) support material generator.
// This class is responsible for generating raft layers, top contact layers
// (touching the object from below), bottom contact layers (above object top
// surfaces), intermediate base layers, and all interface layers between them.
// It delegates toolpath / fill generation to generate_support_toolpaths() in
// SupportCommon.cpp, and layer assembly to generate_support_layers().
//
// [COUPLING] PrintObject is not owned; all three config pointers are raw
// observing pointers into the owning Print hierarchy.
//
// [CONCURRENCY] generate() orchestrates several TBB parallel_for passes.
// Shared mutable state between tasks is protected by tbb::spin_mutex inside
// the thread-safe layer_allocate() overload.

#ifndef slic3r_SupportMaterial_hpp_
#define slic3r_SupportMaterial_hpp_

#include "Flow.hpp"
#include "PrintConfig.hpp"
#include "Slicing.hpp"
#include "Fill/FillBase.hpp"
#include "SupportLayer.hpp"
#include "SupportParameters.hpp"
namespace Slic3r {

class PrintObject;
class PrintConfig;
class PrintObjectConfig;

// This class manages raft and supports for a single PrintObject.
// Instantiated by Slic3r::Print::Object->_support_material()
// This class is instantiated before the slicing starts as Object.pm will query
// the parameters of the raft to determine the 1st layer height and thickness.
class PrintObjectSupportMaterial
{
public:
	// [INTENT] Constructor caches config pointers and precomputes SlicingParameters /
	// SupportParameters that are reused throughout generate().
	PrintObjectSupportMaterial(const PrintObject *object, const SlicingParameters &slicing_params);

	// Is raft enabled?
	bool 		has_raft() 					const { return m_slicing_params.has_raft(); }
	// Has any support?
	bool 		has_support()				const { return m_object_config->enable_support.value || m_object_config->enforce_support_layers; }
	bool 		build_plate_only() 			const { return this->has_support() && m_object_config->support_on_build_plate_only.value; }
	// BBS: synchronize_layers() is true when independent_support_layer_height is OFF,
	// meaning support layers must coincide with object layer print_z values.
	// [INTENT] This trades support quality for fewer tool changes on single-nozzle MMU printers.
	bool 		synchronize_layers()		const { return /*m_slicing_params.soluble_interface && */!m_print_config->independent_support_layer_height.value; }
	bool 		has_contact_loops() 		const { return m_object_config->support_interface_loop_pattern.value; }

	// [INTENT] Main entry point. Orchestrates the full support generation pipeline:
	//   1. buildplate_covered()                       - cumulative object projection downward
	//   2. top_contact_layers()                       - detect overhangs, create top contacts
	//   3. bottom_contact_layers_and_layer_support_areas() - bottom contacts + projected areas
	//   4. raft_and_intermediate_support_layers()     - allocate empty intermediate layers
	//   5. trim_support_layers_by_object()            - XY clearance from top contacts
	//   6. generate_base_layers()                     - fill intermediate layer polygons
	//   7. trim_top_contacts_by_bottom_contacts()     - prevent vertical overlap
	//   8. generate_interface_layers()                - build dense interface slabs
	//   9. generate_raft_base()                       - raft geometry
	//  10. generate_support_layers()                  - assemble SupportLayer objects
	//  11. generate_support_toolpaths()               - fill extrusion paths
	// New support layers will be added to the object,
	// with extrusion paths and islands filled in for each support layer.
	void 		generate(PrintObject &object);

private:
	// [INTENT] Returns, for each object layer index, the union of all object slices
	// below that layer. Used to prevent support above already-printed material when
	// "build plate only" mode is active.  Serial (inherently non-parallelizable).
	std::vector<Polygons> buildplate_covered(const PrintObject &object) const;

	// Generate top contact layers supporting overhangs.
	// For a soluble interface material synchronize the layer heights with the object, otherwise leave the layer height undefined.
	// If supports over bed surface only are requested, don't generate contact layers over an object.
	// [CONCURRENCY] Overhang detection runs in tbb::parallel_for; sharp-tail
	// propagation and small-overhang cluster filtering are serial post-passes.
	// [COUPLING] Reads layer->sharp_tails / sharp_tails_height (BBS additions),
	// cantilevers, and per-layer enforcer / blocker polygon slices.
	SupportGeneratorLayersPtr top_contact_layers(const PrintObject &object, const std::vector<Polygons> &buildplate_covered, SupportGeneratorLayerStorage &layer_storage) const;

	// Generate bottom contact layers supporting the top contact layers.
	// For a soluble interface material synchronize the layer heights with the object, 
	// otherwise set the layer height to a bridging flow of a support interface nozzle.
	// [INTENT] Walks layers top-to-bottom accumulating overhangs_projection.
	// For each layer, two tbb tasks run concurrently:
	//   (a) detect_bottom_contacts() - places a bottom contact slab on object top surfaces
	//   (b) project_support_to_grid() - propagates the contact projection one layer down
	// [HAZARD] bottom_contacts is appended from a lambda; the vector is later
	// std::reverse()-d because insertion is in descending layer order.
	SupportGeneratorLayersPtr bottom_contact_layers_and_layer_support_areas(
		const PrintObject &object, const SupportGeneratorLayersPtr &top_contacts, std::vector<Polygons> &buildplate_covered, 
		SupportGeneratorLayerStorage &layer_storage, std::vector<Polygons> &layer_support_areas) const;

	// Trim the top_contacts layers with the bottom_contacts layers if they overlap, so there would not be enough vertical space for both of them.
	// [CONCURRENCY] Runs in tbb::parallel_for over top_contacts; read-only access
	// to bottom_contacts via binary search (idx_lower_or_equal cache).
	void trim_top_contacts_by_bottom_contacts(const PrintObject &object, const SupportGeneratorLayersPtr &bottom_contacts, SupportGeneratorLayersPtr &top_contacts) const;

	// Generate raft layers and the intermediate support layers between the bottom contact and top contact surfaces.
	// [INTENT] Collects all "extremes" (bottom-z of top contacts + print-z of bottom
	// contacts), sorts them, then fills the Z gaps with intermediate layers.
	// If synchronize_layers() is true the intermediate layers align with object layers;
	// otherwise they are evenly spaced up to max_suport_layer_height.
	SupportGeneratorLayersPtr raft_and_intermediate_support_layers(
	    const PrintObject   &object,
	    const SupportGeneratorLayersPtr   &bottom_contacts,
	    const SupportGeneratorLayersPtr   &top_contacts,
	    SupportGeneratorLayerStorage	  &layer_storage) const;

	// Fill in the base layers with polygons.
	// [INTENT] For each intermediate layer: look up the pre-projected support area
	// from layer_support_areas[], trim it by overlapping top- and bottom-contact
	// polygons, then store the result as the layer's polygon set.
	// [CONCURRENCY] tbb::parallel_for; each thread maintains idx_top_contact_above,
	// idx_bottom_contact_overlapping, idx_object_layer_above caches to avoid
	// repeated binary search. Thread-safe because each intermediate layer is
	// written by exactly one thread (range partition).
	void generate_base_layers(
	    const PrintObject   &object,
	    const SupportGeneratorLayersPtr   &bottom_contacts,
	    const SupportGeneratorLayersPtr   &top_contacts,
	    SupportGeneratorLayersPtr         &intermediate_layers,
	    const std::vector<Polygons> &layer_support_areas) const;



	// Trim support layers by an object to leave a defined gap between
	// the support volume and the object.
	// [INTENT] Expands object slices by gap_xy, inflated in Z by gap_extra_above /
	// gap_extra_below, then subtracts them from each support layer's polygons.
	// Handles thick-bridge regions specially (extra trimming for bridging fill areas).
	// [CONCURRENCY] tbb::parallel_for over nonempty support layers.
	void trim_support_layers_by_object(
	    const PrintObject   &object,
	    SupportGeneratorLayersPtr         &support_layers,
	    const coordf_t       gap_extra_above,
	    const coordf_t       gap_extra_below,
	    const coordf_t       gap_xy) const;

/*
	void generate_pillars_shape();
	void clip_with_shape();
*/

	// [STATE] None of these pointers are owned by this class; they are
	// observing raw pointers into the PrintObject / Print hierarchy.
	// They remain valid for the lifetime of a single generate() call.
	const PrintObject 		*m_object;
	const PrintConfig 		*m_print_config;
	const PrintObjectConfig *m_object_config;
	// Pre-calculated parameters shared between the object slicer and the support generator,
	// carrying information on a raft, 1st layer height, 1st object layer height, gap between the raft and object etc.
	SlicingParameters	     m_slicing_params;
	// Various precomputed support parameters to be shared with external functions.
	SupportParameters   	 m_support_params;
};

} // namespace Slic3r

#endif /* slic3r_SupportMaterial_hpp_ */
