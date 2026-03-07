// Tree supports by Thomas Rahm, losely based on Tree Supports by CuraEngine.
// Original source of Thomas Rahm's tree supports:
// https://github.com/ThomasRahm/CuraEngine
//
// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Public header for TreeSupport3D — the organic-mode tree support subsystem.
// This file defines data types shared between TreeSupport3D.cpp and its callers.
// The only external entry point is generate_tree_support_3D() (bottom of file).
//
// [COUPLING] TreeSupport3D is invoked exclusively from TreeSupport.cpp when
// config.support_style == smsTreeOrganic. All other styles (slim/strong/hybrid)
// are handled inside TreeSupport.cpp itself — this file is organic-only.
//
// [STATE] Key types defined here:
//   AreaIncreaseSettings — packed strategy descriptor for one "attempt" at growing
//                          an influence area downward one layer.
//   SupportElementStateBits — bit-field of boolean flags; C++17 forbids in-place
//                             bit initializers, so a ctor is provided (see comment).
//   SupportElementState — full node state, inherits the bit fields.
//   SupportElement — node in the branch graph: state + parent list + influence area.
//   SupportElements — std::deque<SupportElement> (deque preserves pointer stability
//                     during append, important since we store raw pointers).

#ifndef slic3r_TreeSupport_hpp
#define slic3r_TreeSupport_hpp

#include "SupportLayer.hpp"
#include "TreeModelVolumes.hpp"
#include "TreeSupportCommon.hpp"

#include "../BoundingBox.hpp"
#include "../Point.hpp"
#include "../Utils.hpp"

#include <boost/container/small_vector.hpp>


// #define TREE_SUPPORT_SHOW_ERRORS

#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    // The various stages of the process can be weighted differently in the progress bar.
    // These weights are obtained experimentally using a small sample size. Sensible weights can differ drastically based on the assumed default settings and model.
    #define TREE_PROGRESS_TOTAL 10000
    #define TREE_PROGRESS_PRECALC_COLL TREE_PROGRESS_TOTAL * 0.1
    #define TREE_PROGRESS_PRECALC_AVO TREE_PROGRESS_TOTAL * 0.4
    #define TREE_PROGRESS_GENERATE_NODES TREE_PROGRESS_TOTAL * 0.1
    #define TREE_PROGRESS_AREA_CALC TREE_PROGRESS_TOTAL * 0.3
    #define TREE_PROGRESS_DRAW_AREAS TREE_PROGRESS_TOTAL * 0.1
    #define TREE_PROGRESS_GENERATE_BRANCH_AREAS TREE_PROGRESS_DRAW_AREAS / 3
    #define TREE_PROGRESS_SMOOTH_BRANCH_AREAS TREE_PROGRESS_DRAW_AREAS / 3
    #define TREE_PROGRESS_FINALIZE_BRANCH_AREAS TREE_PROGRESS_DRAW_AREAS / 3
#endif // SLIC3R_TREESUPPORTS_PROGRESS

namespace Slic3r
{

// Forward declarations
class Print;
class PrintObject;
struct SlicingParameters;

namespace TreeSupport3D
{


// [INTENT] Strategy descriptor for a single downward-propagation attempt in
// increase_areas_one_layer(). For each node, a priority-ordered vector of these
// is tried in sequence until one succeeds (see increase_single_area()). This
// lets the propagator try "ideal" movements first (fast avoidance, no radius
// change) and fall back to slower/wider settings if the ideal path is blocked.
//
// [MEMORY] Bit-packed to reduce the footprint of SupportElementState and
// SupportElementMerging, which each store one AreaIncreaseSettings inline.
struct AreaIncreaseSettings
{
    AreaIncreaseSettings(
        TreeModelVolumes::AvoidanceType type = TreeModelVolumes::AvoidanceType::Fast, coord_t increase_speed = 0, 
        bool increase_radius = false, bool no_error = false, bool use_min_distance = false, bool move = false) :
        increase_speed{ increase_speed }, type{ type }, increase_radius{ increase_radius }, no_error{ no_error }, use_min_distance{ use_min_distance }, move{ move } {}

    coord_t         increase_speed;
    // Packing for smaller memory footprint of SupportElementState && SupportElementMerging
    TreeModelVolumes::AvoidanceType type;
    bool            increase_radius  : 1;
    bool            no_error         : 1;
    bool            use_min_distance : 1;
    bool            move             : 1;
    bool operator==(const AreaIncreaseSettings& other) const
    {
        return type             == other.type               &&
               increase_speed   == other.increase_speed     &&
               increase_radius  == other.increase_radius    &&
               no_error         == other.no_error           &&
               use_min_distance == other.use_min_distance   &&
               move             == other.move;
    }
};

#define TREE_SUPPORTS_TRACK_LOST

// [INTENT] Bit-field of boolean state flags for a tree support node.
// Separated from SupportElementState so C++17 can zero all bits via the ctor
// (C++17 does not allow in-place default initializers for bit-field members).
//
// [STATE] Each flag documents a different constraint or debug annotation:
//   to_buildplate       — this branch is trying to reach the build plate
//   to_model_gracious   — branch can rest on a flat model/buildplate surface
//   use_min_xy_dist     — may use minimum rather than preferred xy distance
//   supports_roof       — this element or an ancestor carries a roof layer
//   can_use_safe_radius — path is hole-free; can use holefree avoidance
//   skip_ovalisation    — do not ovalise this element when drawing circles
//   lost / verylost     — debug-only flags for lost-branch diagnostic (gated on TREE_SUPPORTS_TRACK_LOST)
//   deleted             — soft-delete marker; compacted by remove_deleted_elements()
//   marked              — general-purpose visited marker (reused across passes)
// C++17 does not support in place initializers of bit values, thus a constructor zeroing the bits is provided.
struct SupportElementStateBits {
    SupportElementStateBits() :
        to_buildplate(false),
        to_model_gracious(false),
        use_min_xy_dist(false),
        supports_roof(false),
        can_use_safe_radius(false),
        skip_ovalisation(false),
#ifdef TREE_SUPPORTS_TRACK_LOST
        lost(false),
        verylost(false),
#endif // TREE_SUPPORTS_TRACK_LOST
        deleted(false),
        marked(false)
        {}

    /*!
     * \brief The element trys to reach the buildplate
     */
    bool to_buildplate : 1;

    /*!
     * \brief Will the branch be able to rest completely on a flat surface, be it buildplate or model ?
     */
    bool to_model_gracious : 1;

    /*!
     * \brief Whether the min_xy_distance can be used to get avoidance or similar. Will only be true if support_xy_overrides_z=Z overrides X/Y.
     */
    bool use_min_xy_dist : 1;

    /*!
     * \brief True if this Element or any parent (element above) provides support to a support roof.
     */
    bool supports_roof : 1;

    /*!
     * \brief An influence area is considered safe when it can use the holefree avoidance <=> It will not have to encounter holes on its way downward.
     */
    bool can_use_safe_radius : 1;

    /*!
     * \brief Skip the ovalisation to parent and children when generating the final circles.
     */
    bool skip_ovalisation : 1;

#ifdef TREE_SUPPORTS_TRACK_LOST
    // Likely a lost branch, debugging information.
    bool lost : 1;
    bool verylost : 1;
#endif // TREE_SUPPORTS_TRACK_LOST

    // Not valid anymore, to be deleted.
    bool deleted : 1;

    // General purpose flag marking a visited element.
    bool marked : 1;
};

// [INTENT] Full state of a single support-tree node at one layer.
// Inherits the bit flags from SupportElementStateBits.
//
// [STATE] Key fields:
//   target_height / target_position — the overhang point this branch is anchoring
//   layer_idx                       — current layer (decrements each step down)
//   effective_radius_height         — "age" used to compute the actual branch radius
//                                     (radius grows linearly with distance from tip)
//   distance_to_top                 — how many layers below the tip this node is
//   result_on_layer                 — the final 2D center point placed by
//                                     create_nodes_from_area() / set_points_on_areas()
//                                     Sentinel: (INT_MAX, INT_MAX) means not yet set.
//   increased_to_model_radius       — extra radius gained by merging with a
//                                     to_model-only branch; used in merge accounting
//   elephant_foot_increases         — float counter of how many times the base
//                                     elephant-foot radius has been widened
//   dont_move_until                 — distance_to_top below which the node is "locked"
//                                     and should not move laterally (use locked() to query)
//   last_area_increase              — the AreaIncreaseSettings that produced the
//                                     current influence area; used for merging logic
//   missing_roof_layers             — roof layers still owed (branch had to move)
//
// [CONCURRENCY] result_on_layer is written by create_nodes_from_area() in a
// bottom-up serial pass. Prior to that it must not be read by other threads.
struct SupportElementState : public SupportElementStateBits
{
    int type = 0;
    coordf_t radius = 0;
    float print_z = 0;

    /*!
     * \brief The layer this support elements wants reach
     */
    LayerIndex  target_height;

    /*!
     * \brief The position this support elements wants to support on layer=target_height
     */
    Point       target_position;

    /*!
     * \brief The next position this support elements wants to reach. NOTE: This is mainly a suggestion regarding direction inside the influence area.
     */
    Point       next_position;

    /*!
     * \brief The next height this support elements wants to reach
     */
    LayerIndex  layer_idx;

    /*!
     * \brief The Effective distance to top of this element regarding radius increases and collision calculations.
     */
    uint32_t    effective_radius_height;

    /*!
     * \brief The amount of layers this element is below the topmost layer of this branch.
     */
    uint32_t    distance_to_top;

    /*!
     * \brief The resulting center point around which a circle will be drawn later.
     * Will be set by setPointsOnAreas
     */
    Point result_on_layer { std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max() };
    bool  result_on_layer_is_set() const { return this->result_on_layer != Point{ std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max() }; }
    void  result_on_layer_reset() { this->result_on_layer = Point{ std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max() }; }
    /*!
     * \brief The amount of extra radius we got from merging branches that could have reached the buildplate, but merged with ones that can not.
     */
    coord_t     increased_to_model_radius; // how much to model we increased only relevant for merging

    /*!
     * \brief Counter about the times the elephant foot was increased. Can be fractions for merge reasons.
     */
    double      elephant_foot_increases;

    /*!
     * \brief The element tries to not move until this dtt is reached, is set to 0 if the element had to move.
     */
    uint32_t    dont_move_until;

    /*!
     * \brief Settings used to increase the influence area to its current state.
     */
    AreaIncreaseSettings last_area_increase;

    /*!
     * \brief Amount of roof layers that were not yet added, because the branch needed to move.
     */
    uint32_t    missing_roof_layers;

    // called by increase_single_area() and increaseAreas()
    [[nodiscard]] static SupportElementState propagate_down(const SupportElementState &src)
    {
        SupportElementState dst{ src };
        ++ dst.distance_to_top;
        -- dst.layer_idx;
        // set to invalid as we are a new node on a new layer
        dst.result_on_layer_reset();
        dst.skip_ovalisation = false;
        return dst;
    }

    [[nodiscard]] bool locked() const { return this->distance_to_top < this->dont_move_until; }
};

/*!
 * \brief Get the Distance to top regarding the real radius this part will have. This is different from distance_to_top, which is can be used to calculate the top most layer of the branch.
 * \param elem[in] The SupportElement one wants to know the effectiveDTT
 * \return The Effective DTT.
 */
[[nodiscard]] inline size_t getEffectiveDTT(const TreeSupportSettings &settings, const SupportElementState &elem)
{
    return elem.effective_radius_height < settings.increase_radius_until_layer ? 
        (elem.distance_to_top < settings.increase_radius_until_layer ? elem.distance_to_top : settings.increase_radius_until_layer) : 
        elem.effective_radius_height;
}

/*!
 * \brief Get the Radius, that this element will have.
 * \param elem[in] The Element.
 * \return The radius the element has.
 */
[[nodiscard]] inline coord_t support_element_radius(const TreeSupportSettings &settings, const SupportElementState &elem)
{ 
    return settings.getRadius(getEffectiveDTT(settings, elem), elem.elephant_foot_increases);
}

/*!
 * \brief Get the collision Radius of this Element. This can be smaller then the actual radius, as the drawAreas will cut off areas that may collide with the model.
 * \param elem[in] The Element.
 * \return The collision radius the element has.
 */
[[nodiscard]] inline coord_t support_element_collision_radius(const TreeSupportSettings &settings, const SupportElementState &elem)
{
    return settings.getRadius(elem.effective_radius_height, elem.elephant_foot_increases);
}

// [INTENT] The fundamental node in the tree support branch graph.
// One SupportElement exists per node per layer; the full graph is stored as
// std::vector<SupportElements> move_bounds[layer_idx].
//
// [STATE]
//   state          — all node properties (position, radius, flags etc.)
//   parents        — indices into move_bounds[layer_idx + 1]; the nodes above
//                    this one that this element is "growing toward"
//   influence_area — the 2D polygon region inside which this node is allowed to
//                    place its result_on_layer center point. Set by
//                    create_layer_pathing(); consumed by create_nodes_from_area().
//
// [MEMORY] In Release mode, ParentIndices is boost::container::small_vector<int32_t, 4>
// to avoid heap allocation for the common case of ≤4 parents. In Debug mode it
// falls back to std::vector for easier inspection.
//
// [HAZARD] influence_area is only valid after create_layer_pathing(); it is empty
// inside the createLayerPathing recursion itself. Do not read prematurely.
struct SupportElement
{
    using ParentIndices =
#ifdef NDEBUG
        // To reduce memory allocation in release mode.
        boost::container::small_vector<int32_t, 4>;
#else // NDEBUG
        // To ease debugging.
        std::vector<int32_t>;
#endif // NDEBUG

//    SupportElement(const SupportElementState &state) : SupportElementState(state) {}
    SupportElement(const SupportElementState &state, Polygons &&influence_area) : state(state), influence_area(std::move(influence_area)) {}
    SupportElement(const SupportElementState &state, ParentIndices &&parents, Polygons &&influence_area) :
        state(state), parents(std::move(parents)), influence_area(std::move(influence_area)) {}

    SupportElementState         state;

    /*!
     * \brief All elements in the layer above the current one that are supported by this element
     */
    ParentIndices               parents;

    /*!
     * \brief The resulting influence area.
     * Will only be set in the results of createLayerPathing, and will be nullptr inside!
     */
    Polygons                    influence_area;
};

// [INTENT] std::deque chosen over std::vector so that appending new elements
// does not invalidate pointers to existing elements. The branch-traversal code
// in organic_draw_branches() stores raw const SupportElement* pointers into
// Branch::path, so pointer stability is required.
using SupportElements = std::deque<SupportElement>;

[[nodiscard]] inline coord_t support_element_radius(const TreeSupportSettings &settings, const SupportElement &elem)
{
    return support_element_radius(settings, elem.state);
}

[[nodiscard]] inline coord_t support_element_collision_radius(const TreeSupportSettings &settings, const SupportElement &elem)
{
    return support_element_collision_radius(settings, elem.state);
}

// [INTENT] Organic-mode final drawing pass.
// After create_nodes_from_area() has placed result_on_layer points, this function:
//  1. Flattens the per-layer deque into a linear list (elements_with_link_down)
//     with explicit child indices, enabling parallel tree traversal.
//  2. Calls organic_smooth_branches_avoid_collisions() — 100 iterations of
//     collision nudge + Laplacian smoothing on all branch node positions.
//  3. Traverses the tree graph, collecting Branch sequences (runs of nodes
//     between bifurcations). Uses TreeVisitor::visit_recursive() with a
//     state.marked flag to avoid double-visiting.
//  4. TBB parallel_for over all trees: calls extrude_branch() to triangulate
//     each Branch into a 3D mesh tube, then slices it with slice_mesh().
//     Clips each slice against collision(0) and bed_area. Propagates "non-
//     gracious" branch roots downward layer by layer until area < threshold.
//  5. Second TBB pass: unions polygons within each Tree slice.
//  6. Merges all Tree slices into a single flat slices[] vector.
//  7. Third TBB pass: smooth_outward + simplify each layer's merged polygon,
//     subtract top contacts, allocate bottom_contact and intermediate layers.
//
// [CONCURRENCY] Steps 4–7 use TBB parallel_for with simple_partitioner (grain=1).
// intermediate_layers[] is written at indices [range.begin, range.end) with no
// overlap — safe without a mutex.
//
// [HAZARD] The `#if 0` block inside step 4 (interface tip extraction via
// branch.has_tip) is disabled and marked FIXME — top contact layers from the
// organic pass are NOT populated here; they come from generate_initial_areas()
// via interface_placer instead.
// Organic specific: Smooth branches and produce one cummulative mesh to be sliced.
void organic_draw_branches(
    PrintObject                     &print_object,
    TreeModelVolumes                &volumes, 
    const TreeSupportSettings       &config,
    std::vector<SupportElements>    &move_bounds,

    // I/O:
    SupportGeneratorLayersPtr       &bottom_contacts,
    SupportGeneratorLayersPtr       &top_contacts,
    InterfacePlacer                 &interface_placer,

    // Output:
    SupportGeneratorLayersPtr       &intermediate_layers,
    SupportGeneratorLayerStorage    &layer_storage,

    std::function<void()> throw_on_cancel);

} // namespace TreeSupport3D

// [INTENT] Public entry point — the only function called from outside this subsystem.
// Called by TreeSupport::generate() when config.support_style == smsTreeOrganic.
//
// [COUPLING] Delegates to TreeSupport3D::generate_support_areas() after:
//  - Finding the index of print_object in print->objects()
//  - Converting machine border Points → Pointfs for BuildVolume
//
// [STATE] All support output (layers, contacts, toolpaths) is stored on
// print_object.support_layers() via generate_support_toolpaths().
void generate_tree_support_3D(PrintObject &print_object, TreeSupport* tree_support, std::function<void()> throw_on_cancel = []{});

} // namespace Slic3r

#endif /* slic3r_TreeSupport_hpp */
