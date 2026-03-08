// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Declares the Generator class — the top-level orchestrator for Lightning Infill.
// A Generator is constructed once per PrintObject (at prepare_infill time) and stores:
//   - m_overhang_per_layer: per-layer internal overhang polygons (which areas need support)
//   - m_lightning_layers:   per-layer tree structures (the actual lightning paths to print)
//
// The Generator lifetime spans the entire slicing/G-code generation phase for one PrintObject.
// It is accessed read-only (via getTreesForLayer) by Filler::_fill_surface_single().
//
// [COUPLING] Depends on:
//   - Layer.hpp     (Layer type, NodeSPtr)
//   - PrintObject   (layer geometry, region configs, nozzle diameters)
//   - ClipperUtils  (polygon operations in generateInitialInternalOverhangs)
//   - EdgeGrid      (outline locator for snap-to-boundary in generateTrees)
//
// [STATE] All mutable state is private. Once constructed, the Generator is immutable from
// the perspective of callers (only getTreesForLayer const, Overhangs() is non-const but
// used only by the support-mode constructor, and infilll_extrusion_width() is read-only).
//
// [MEMORY] Generator is heap-allocated by build_generator() and owned by GeneratorPtr
// (unique_ptr with GeneratorDeleter). All per-layer Polygons and Layers are stored by value
// in m_overhang_per_layer and m_lightning_layers vectors respectively.
//
// [CONCURRENCY] The Generator constructor (generateInitialInternalOverhangs + generateTrees)
// runs single-threaded. After construction, getTreesForLayer() is read-only and is safe to
// call from multiple TBB threads simultaneously. generateTreesforSupport() is only called
// from the secondary constructor (tree-support use case), also single-threaded.
//
// [HAZARD H270] Two public constructors exist:
//   1. Generator(const PrintObject&, ...) — infill use case (called by build_generator)
//   2. Generator(PrintObject*, std::vector<Polygons>&, ...) — support use case (takes pre-
//      computed overhangs). The second constructor BYPASSES generateInitialInternalOverhangs().
// Both constructors share most parameters but have DIFFERENT m_supporting_radius formulas:
//   - Infill:  m_supporting_radius = extrusion_width * 100 * n_multiline / density
//   - Support: m_supporting_radius = extrusion_width / density (density clamped to 0.15)
// A port that accidentally uses the infill formula for support (or vice versa) will produce
// wildly wrong branch densities. The two constructors must be ported separately.

#ifndef LIGHTNING_GENERATOR_H
#define LIGHTNING_GENERATOR_H

#include "Layer.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace Slic3r {
class PrintObject;

namespace FillLightning {

/*!
 * Generates the Lightning Infill pattern.
 *
 * The lightning infill pattern is designed to use a minimal amount of material
 * to support the top skin of the print, while still printing with reasonably
 * consistently flowing lines. It sacrifices strength completely in favour of
 * top surface quality and reduced print time / material usage.
 *
 * Lightning Infill is so named because the patterns it creates resemble a
 * forked path with one main path and many small lines on the side. These paths
 * grow out from the sides of the model just below where the top surface needs
 * to be supported from the inside, so that minimal material is needed.
 *
 * This pattern is based on a paper called "Ribbed Support Vaults for 3D
 * Printing of Hollowed Objects" by Tricard, Claux and Lefebvre:
 * https://www.researchgate.net/publication/333808588_Ribbed_Support_Vaults_for_3D_Printing_of_Hollowed_Objects
 */
// [INTENT] The "Just like Nicola used to make!" comment is a developer joke referencing
// Nicola Tesla (lightning). Not algorithmically relevant.
class Generator // "Just like Nicola used to make!"
{
public:
    /*!
     * Create a generator to fill a certain mesh with infill.
     *
     * This generator will pre-compute things in preparation of generating
     * Lightning Infill for the infill areas in that mesh. The infill areas must
     * already be calculated at this point.
     */
    // [INTENT] Primary constructor: infill use case. Called by build_generator().
    // Reads all layer geometry from print_object, computes internal overhangs top-to-bottom,
    // then builds lightning trees bottom-from-top.
    // [COUPLING] Requires print_object.fill_surfaces to be populated (after PrintObject::slice).
    // [STATE] After construction, all data is in m_overhang_per_layer and m_lightning_layers.
    // [HAZARD H271] throw_on_cancel_callback is called periodically inside long loops. If it
    // throws a non-std exception (e.g., Slic3r::SlicingCancelledException), the Generator
    // is partially constructed and must be discarded. The GeneratorPtr owner (PrintObject)
    // must not call getTreesForLayer() on a partially-constructed Generator.
    explicit Generator(const PrintObject& print_object, const std::function<void()>& throw_on_cancel_callback);

    /*!
     * Get a tree of paths generated for a certain layer of the mesh.
     *
     * This tree represents the paths that must be traced to print the infill.
     * \param layer_id The layer number to get the path tree for. This is within
     * the range of layers of the mesh (not the global layer numbers).
     * \return A tree structure representing paths to print to create the
     * Lightning Infill pattern.
     */
    // [INTENT] Read-only accessor for per-layer tree data. Called by Filler::_fill_surface_single.
    // [HAZARD H268] assert(layer_id < m_lightning_layers.size()) in debug only.
    // In release, out-of-range layer_id → operator[] UB.
    const Layer& getTreesForLayer(const size_t& layer_id) const;

    // [INTENT] Exposes m_overhang_per_layer for the support-mode constructor to pre-populate
    // overhangs externally (bypassing generateInitialInternalOverhangs).
    // [STATE] Non-const: only used during construction by the support-mode caller.
    std::vector<Polygons>& Overhangs() { return m_overhang_per_layer; }

    // [INTENT] Returns the computed infill extrusion width (in scaled coords).
    // Used by tree-support code to correctly size the lightning support density.
    // [HAZARD] Typo: "infilll_extrusion_width" has three 'l's. Left as-is to match compiled API.
    float infilll_extrusion_width() const { return m_infill_extrusion_width; }

    // [INTENT] Secondary constructor: tree-support use case. Receives pre-computed contours
    // (infill region outlines per layer) and overhangs (what must be supported per layer).
    // Calls generateTreesforSupport() instead of generateInitialInternalOverhangs() + generateTrees().
    // [HAZARD H270] Different m_supporting_radius formula than the primary constructor.
    // density is clamped to min 0.15 to prevent near-zero divison producing enormous radius.
    // [UNCLEAR] density parameter default 0.15 — why this specific value? No comment in original.
    // Corresponds to 15% infill density as the minimum practical lightning infill density.
    Generator(PrintObject*                 m_object,
              std::vector<Polygons>&       contours,
              std::vector<Polygons>&       overhangs,
              const std::function<void()>& throw_on_cancel_callback,
              float                        density = 0.15);

protected:
    /*!
     * Calculate the overhangs above the infill areas that need to be supported
     * by infill.
     *
     * Normally, overhangs are only generated for the outside of the model and
     * only when support is generated. For this pattern, we also need to
     * generate overhang areas for the inside of the model.
     */
    // [INTENT] Iterates layers top-to-bottom. For each layer, the "internal overhang" is:
    //   diff(offset(infill_area_here, -m_wall_supporting_radius), infill_area_above)
    // i.e.: parts of this layer's infill that are NOT directly below the layer above's infill,
    // eroded by the distance a wall can support. These areas require lightning tree support.
    // [STATE] Populates m_overhang_per_layer[0..N-1]. Consumes stInternal + stInternalVoid surfaces.
    // [COUPLING] Reads LayerRegion::fill_surfaces from every layer of print_object.
    void generateInitialInternalOverhangs(const PrintObject& print_object, const std::function<void()>& throw_on_cancel_callback);

    /*!
     * Calculate the tree structure of all layers.
     */
    // [INTENT] Two-pass loop (top-to-bottom):
    //   Pass 1 (collect): gather infill_outlines per layer.
    //   Pass 2 (build): for each layer top-to-bottom:
    //     1. generateNewTrees() — add new branches for unsupported overhang points
    //     2. reconnectRoots()   — reattach roots propagated from layer above to new boundary
    //     3. propagateToNextLayer() — copy/prune/straighten trees for the layer below
    // Uses a single EdgeGrid::Grid (outlines_locator) that is updated per layer.
    // [STATE] Populates m_lightning_layers[0..N-1] and bboxs[0..N-1].
    // [MEMORY] infill_outlines is a temporary N-element vector of Polygons; freed on return.
    // [COUPLING] Calls Layer::generateNewTrees, Layer::reconnectRoots, Node::propagateToNextLayer.
    void generateTrees(const PrintObject& print_object, const std::function<void()>& throw_on_cancel_callback);

    // [INTENT] Identical algorithm to generateTrees() but takes pre-built contours vector
    // instead of extracting from PrintObject. Used by the support-mode constructor.
    // [COUPLING] m_overhang_per_layer must be pre-populated before this call (done by the
    // secondary constructor which assigns overhangs directly from the caller).
    void generateTreesforSupport(std::vector<Polygons>& contours, const std::function<void()>& throw_on_cancel_callback);

    // [STATE] Infill extrusion width in scaled coord_t (result of scaled<float>(line_width_mm)).
    // Used to compute m_supporting_radius and returned via infilll_extrusion_width().
    float m_infill_extrusion_width;

    /*!
     * How far each piece of infill can support skin in the layer above.
     */
    // [INTENT] Primary radius of influence: an infill line can "support" overhang points
    // within m_supporting_radius of it. Controls how dense the tree branches are.
    // Formula (infill): extrusion_width * 100 * n_multiline / sparse_infill_density
    // Formula (support): extrusion_width / density (density clamped >= 0.15)
    // Units: scaled coord_t (integer micrometers × 10^6 scale factor)
    coord_t m_supporting_radius;

    /*!
     * How far a wall can support the wall above it. If a wall completely
     * supports the wall above it, no infill needs to support that.
     *
     * This is similar to the overhang distance calculated for support. It is
     * determined by the lightning_infill_overhang_angle setting.
     */
    // [INTENT] Controls overhang erosion in generateInitialInternalOverhangs:
    //   offset(infill_area, -m_wall_supporting_radius) removes areas close to walls.
    // Fixed at 45° overhang angle: m_wall_supporting_radius = layer_thickness * tan(45°) = layer_thickness.
    // Units: scaled coord_t.
    coord_t m_wall_supporting_radius;

    /*!
     * How far each piece of infill can support other infill in the layer above.
     *
     * This may be different than \ref supporting_radius, because the infill is
     * printed with one end floating in mid-air. This endpoint will sag more, so
     * an infill line may need to be supported more than a skin line.
     */
    // [INTENT] Controls Node::prune() — how far leaf endpoints are retracted when
    // propagating trees to the next layer below. Prevents overhanging leaf endpoints.
    // Fixed at 45°: m_prune_length = layer_thickness * tan(45°) = layer_thickness.
    // [UNCLEAR] Why is m_prune_length the same as m_wall_supporting_radius? Both use 45°.
    // In Cura, prune_angle and overhang_angle are separate configurable values.
    coord_t m_prune_length;

    /*!
     * How far a line may be shifted in order to straighten the line out.
     *
     * Straightening the line reduces material and time usage and reduces
     * accelerations needed to print the pattern. However it makes the infill
     * weak if lines are partially suspended next to the line on the previous
     * layer.
     */
    // [INTENT] Controls Node::straighten() — maximum node displacement toward the
    // junction-to-junction axis per layer. Fixed at 45°: same as prune_length.
    // [HAZARD H272] All three angle-derived parameters (wall_supporting_radius,
    // prune_length, straightening_max_distance) are hardcoded at M_PI/4 (45°).
    // In CuraEngine, these are separate user-configurable settings. A port must
    // expose these as separate parameters if users are to have CuraEngine-equivalent
    // control. Current OrcaSlicer code has no UI sliders for lightning infill angles.
    coord_t m_straightening_max_distance;

    /*!
     * For each layer, the overhang that needs to be supported by the pattern.
     *
     * This is generated by \ref generateInitialInternalOverhangs.
     */
    // [STATE] m_overhang_per_layer[i] = Polygons to be supported on layer i.
    // Populated by generateInitialInternalOverhangs (infill mode) or assigned directly
    // from caller (support mode via Overhangs() accessor).
    // Size = print_object.layers().size() or contours.size().
    std::vector<Polygons> m_overhang_per_layer;

    /*!
     * For each layer, the generated lightning paths.
     *
     * This is generated by \ref generateTrees.
     */
    // [STATE] m_lightning_layers[i] = the Layer holding tree_roots for layer i.
    // Populated by generateTrees/generateTreesforSupport. Read-only after construction.
    // [MEMORY] Layer contains std::vector<NodeSPtr> (shared_ptr tree nodes). All nodes
    // are heap-allocated via Node::create (make_shared). Destroyed when Layer is destroyed
    // (all shared_ptr ref counts reach 0 — but see weak_ptr parent references in nodes).
    std::vector<Layer> m_lightning_layers;

    // [STATE] Per-layer bounding boxes of infill outlines. Populated alongside m_lightning_layers.
    // Used in getBestGroundingLocation to convert world coordinates to grid addresses.
    // [COUPLING] Consumed by Layer methods that take current_outlines_bbox.
    // [UNCLEAR] bboxs is not documented and its name is a typo (should be "bboxes").
    std::vector<BoundingBox> bboxs;
};

} // namespace FillLightning
} // namespace Slic3r

#endif // LIGHTNING_GENERATOR_H
