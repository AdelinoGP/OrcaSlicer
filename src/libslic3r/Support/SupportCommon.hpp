#ifndef slic3r_SupportCommon_hpp_
#define slic3r_SupportCommon_hpp_

// [INTENT] Public API for the shared support-structure generation pipeline.
// Both the classic FDM support generator (PrintObjectSupportMaterial) and
// the tree/organic support generator share the routines declared here.
// Callers feed sorted SupportGeneratorLayer* vectors (contacts, interface,
// intermediate, raft) and receive filled-in SupportLayer toolpaths via
// generate_support_toolpaths().
//
// [COUPLING] Depends heavily on SupportLayer.hpp (layer category enums and
// SupportGeneratorLayerStorage), SupportParameters.hpp (all flow/density/style
// settings), and Print.hpp (PrintObject, SlicingParameters).  Any port must
// replicate all three dependency chains.
//
// [MEMORY] SupportGeneratorLayerStorage is the bump allocator for raw
// SupportGeneratorLayer* pointers.  Raw-pointer ownership is managed by that
// storage object; the Ptr vectors do NOT own their elements.
//
// [CONCURRENCY] generate_interface_layers() and generate_support_toolpaths()
// use TBB parallel_for internally.  Callers must not pass aliased (shared-
// writable) vectors from different threads.

#include "../Layer.hpp"
#include "../Polygon.hpp"
#include "../Print.hpp"
#include "SupportLayer.hpp"
#include "SupportParameters.hpp"

namespace Slic3r {

class PrintObject;
class SupportLayer;

// Turn some of the base layers into base interface layers.
// For soluble interfaces with non-soluble bases, print maximum two first interface layers with the base
// extruder to improve adhesion of the soluble filament to the base.
// For Organic supports, merge top_interface_layers & top_base_interface_layers with the interfaces
// produced by this function.
// [INTENT] Separates the topmost intermediate layers into interface-quality
// regions.  Returns (interface_layers, base_interface_layers).  For soluble
// filament interfaces, the bottom 1-2 layers of the interface are generated
// with the base extruder material to anchor the soluble filament.
// [STATE] intermediate_layers is modified in place (top slice trimmed away).
// top_interface_layers and top_base_interface_layers are merged in-place with
// newly created layers when organic supports are active.
// [HAZARD] The two output vectors share SupportGeneratorLayer* memory with the
// storage allocator.  The caller must keep layer_storage alive until both
// output vectors are consumed.  No RAII wrapper enforces this lifetime.
std::pair<SupportGeneratorLayersPtr, SupportGeneratorLayersPtr> generate_interface_layers(
    const PrintObjectConfig&         config,
    const SupportParameters&         support_params,
    const SupportGeneratorLayersPtr& bottom_contacts,
    const SupportGeneratorLayersPtr& top_contacts,
    // Input / output, will be merged with output
    SupportGeneratorLayersPtr& top_interface_layers,
    SupportGeneratorLayersPtr& top_base_interface_layers,
    // Input, will be trimmed with the newly created interface layers.
    SupportGeneratorLayersPtr&    intermediate_layers,
    SupportGeneratorLayerStorage& layer_storage);

// Generate raft layers, also expand the 1st support layer
// in case there is no raft layer to improve support adhesion.
// [INTENT] Builds raft column + transition layers below support_z[0].
// Applies brim avoidance: expands the first raft layer outward, then clips
// against the object brim exclusion polygon.  Transition layers use a
// multi-step inflate (nsteps = max(5, ...)) to prevent raft walls leaking
// through thin walls.
// [HAZARD] If top_contacts, interface_layers, and base_layers are all empty,
// returns an empty vector without error.  Callers that assume at least one raft
// layer is always returned will index out-of-bounds.
SupportGeneratorLayersPtr generate_raft_base(const PrintObject&               object,
                                             const SupportParameters&         support_params,
                                             const SlicingParameters&         slicing_params,
                                             const SupportGeneratorLayersPtr& top_contacts,
                                             const SupportGeneratorLayersPtr& interface_layers,
                                             const SupportGeneratorLayersPtr& base_interface_layers,
                                             const SupportGeneratorLayersPtr& base_layers,
                                             SupportGeneratorLayerStorage&    layer_storage);

// [INTENT] Generates extrusion paths for tree-support branch cross-sections.
// Uses ClipperLib_Z to annotate contour indices for seam placement.
// For large branch areas (> tree_branch_diameter_double_wall_area_scaled) a
// double perimeter wall is generated; small branches get a single outline.
// [COUPLING] dst is ExtrusionEntitiesPtr — raw pointer collection owned by the
// caller; append semantics (does not clear dst first).
void tree_supports_generate_paths(ExtrusionEntitiesPtr&    dst,
                                  const Polygons&          polygons,
                                  const Flow&              flow,
                                  const SupportParameters& support_params);

// [INTENT] Fills support polygons with a fill pattern and optionally wraps the
// perimeter with a sheath loop (for better wall adhesion).
// [STATE] dst is appended to (not cleared). filler internal state is mutated
// (angle, spacing, link_max_length) before each Fill::fill_surface call.
// [HAZARD] InfillFailedException thrown by fill_surface is caught silently —
// failed infill regions are silently dropped (same as fill_expolygons_generate_paths).
void fill_expolygons_with_sheath_generate_paths(ExtrusionEntitiesPtr&    dst,
                                                const Polygons&          polygons,
                                                Fill*                    filler,
                                                float                    density,
                                                ExtrusionRole            role,
                                                const Flow&              flow,
                                                const SupportParameters& support_params,
                                                bool                     with_sheath,
                                                bool                     no_sort);

// returns sorted layers
// [INTENT] Merges all five layer category vectors (raft, bottom_contacts,
// top_contacts, intermediate, interface, base_interface) into a single sorted
// vector of SupportLayer* objects attached to the PrintObject.  Deduplicates
// layers that share the same print_z.  Assigns interface_id counters for
// raft-interface angle alternation.
// [STATE] Mutates object by appending to object.m_support_layers.
// [COUPLING] Caller owns the returned SupportGeneratorLayersPtr only as a view;
// the PrintObject owns the SupportLayer heap objects.
SupportGeneratorLayersPtr generate_support_layers(PrintObject&                     object,
                                                  const SupportGeneratorLayersPtr& raft_layers,
                                                  const SupportGeneratorLayersPtr& bottom_contacts,
                                                  const SupportGeneratorLayersPtr& top_contacts,
                                                  const SupportGeneratorLayersPtr& intermediate_layers,
                                                  const SupportGeneratorLayersPtr& interface_layers,
                                                  const SupportGeneratorLayersPtr& base_interface_layers);

// Produce the support G-code.
// Used by both classic and tree supports.
// [INTENT] Main toolpath generation entry point: runs two TBB parallel_for
// passes over all support layers.  Pass 1 fills each layer's contacts,
// interface, base, and raft regions into LayerCache per-layer.  Pass 2
// modulates extrusion height by overlapping layers and appends to
// support_layer.support_fills.
// [CONCURRENCY] Both passes use TBB parallel_for.  The fill pattern objects
// (filler_interface, filler_support, etc.) are allocated per TBB worker thread
// to avoid aliasing.  loop_interface_processor is shared read-only across
// threads (its generate() method must be thread-safe).
// [STATE] support_layers[*]->support_fills.entities is appended to inside the
// parallel region; no external locking because each layer_id maps to exactly
// one SupportLayer*.
// [HAZARD] link_max_length_factor is hardcoded to 0.0 (a commented-out value
// of 3.0 exists).  Zero disables link-max-length infill clipping entirely.
// [HAZARD] The debug-only Test::verify_nonempty() struct at line ~1870 uses
// dynamic_cast on every extrusion entity — O(N) RTTI in debug builds only.
// Release builds have no such guard.
void generate_support_toolpaths(SupportLayerPtrs&                support_layers,
                                const PrintObjectConfig&         config,
                                const SupportParameters&         support_params,
                                const SlicingParameters&         slicing_params,
                                const SupportGeneratorLayersPtr& raft_layers,
                                const SupportGeneratorLayersPtr& bottom_contacts,
                                const SupportGeneratorLayersPtr& top_contacts,
                                const SupportGeneratorLayersPtr& intermediate_layers,
                                const SupportGeneratorLayersPtr& interface_layers,
                                const SupportGeneratorLayersPtr& base_interface_layers);

// FN_HIGHER_EQUAL: the provided object pointer has a Z value >= of an internal threshold.
// Find the first item with Z value >= of an internal threshold of fn_higher_equal.
// If no vec item with Z value >= of an internal threshold of fn_higher_equal is found, return vec.size()
// If the initial idx is size_t(-1), then use binary search.
// Otherwise search linearly upwards.
// [INTENT] Hybrid binary/linear search used inside TBB parallel loops.
// On first call per TBB worker thread (idx == IndexType(-1)) uses binary search
// O(log N).  On subsequent calls in the same batch, linear scan O(1) amortized
// because layers are visited in sorted order.
// [HAZARD] idx == IndexType(-1) sentinel is cast from -1 to size_t(-1) (= SIZE_MAX).
// For signed IndexType (int) this works as expected, but callers must use the
// correct IndexType to avoid sentinel aliasing with a valid index at position
// SIZE_MAX-1 (impossible in practice for layer vectors, but fragile).
template<typename IteratorType, typename IndexType, typename FN_HIGHER_EQUAL>
IndexType idx_higher_or_equal(IteratorType begin, IteratorType end, IndexType idx, FN_HIGHER_EQUAL fn_higher_equal)
{
    auto size = int(end - begin);
    if (size == 0) {
        idx = 0;
    } else if (idx == IndexType(-1)) {
        // First of the batch of layers per thread pool invocation. Use binary search.
        int idx_low  = 0;
        int idx_high = std::max(0, size - 1);
        while (idx_low + 1 < idx_high) {
            int idx_mid = (idx_low + idx_high) / 2;
            if (fn_higher_equal(begin[idx_mid]))
                idx_high = idx_mid;
            else
                idx_low = idx_mid;
        }
        idx = fn_higher_equal(begin[idx_low]) ? idx_low : (fn_higher_equal(begin[idx_high]) ? idx_high : size);
    } else {
        // For the other layers of this batch of layers, search incrementally, which is cheaper than the binary search.
        while (int(idx) < size && !fn_higher_equal(begin[idx]))
            ++idx;
    }
    return idx;
}
// [INTENT] Convenience overload accepting std::vector<T> directly.
template<typename T, typename IndexType, typename FN_HIGHER_EQUAL>
IndexType idx_higher_or_equal(const std::vector<T>& vec, IndexType idx, FN_HIGHER_EQUAL fn_higher_equal)
{
    return idx_higher_or_equal(vec.begin(), vec.end(), idx, fn_higher_equal);
}

// FN_LOWER_EQUAL: the provided object pointer has a Z value <= of an internal threshold.
// Find the first item with Z value <= of an internal threshold of fn_lower_equal.
// If no vec item with Z value <= of an internal threshold of fn_lower_equal is found, return -1.
// If the initial idx is < -1, then use binary search.
// Otherwise search linearly downwards.
// [INTENT] Downward-scanning companion to idx_higher_or_equal.
// idx < -1 triggers binary search on first call; idx >= 0 linear scan downward.
// [HAZARD] The sentinel convention is inverted vs. idx_higher_or_equal:
// "first call" is signalled by idx < -1 (i.e. INT_MIN or similar), NOT idx == -1.
// idx == -1 means "already past the beginning — stop searching immediately".
// Callers that initialise idx to -1 (the natural "not found" sentinel) will
// trigger linear scan mode rather than binary search on first call.
template<typename IT, typename FN_LOWER_EQUAL> int idx_lower_or_equal(IT begin, IT end, int idx, FN_LOWER_EQUAL fn_lower_equal)
{
    auto size = int(end - begin);
    if (size == 0) {
        idx = -1;
    } else if (idx < -1) {
        // First of the batch of layers per thread pool invocation. Use binary search.
        int idx_low  = 0;
        int idx_high = std::max(0, size - 1);
        while (idx_low + 1 < idx_high) {
            int idx_mid = (idx_low + idx_high) / 2;
            if (fn_lower_equal(begin[idx_mid]))
                idx_low = idx_mid;
            else
                idx_high = idx_mid;
        }
        idx = fn_lower_equal(begin[idx_high]) ? idx_high : (fn_lower_equal(begin[idx_low]) ? idx_low : -1);
    } else {
        // For the other layers of this batch of layers, search incrementally, which is cheaper than the binary search.
        while (idx >= 0 && !fn_lower_equal(begin[idx]))
            --idx;
    }
    return idx;
}
// [INTENT] Convenience overload accepting std::vector<T*> directly.
template<typename T, typename FN_LOWER_EQUAL> int idx_lower_or_equal(const std::vector<T*>& vec, int idx, FN_LOWER_EQUAL fn_lower_equal)
{
    return idx_lower_or_equal(vec.begin(), vec.end(), idx, fn_lower_equal);
}

} // namespace Slic3r

#endif /* slic3r_SupportCommon_hpp_ */
