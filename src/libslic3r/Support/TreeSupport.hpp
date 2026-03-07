// [INTENT] Declares the BBS-era tree support generator for OrcaSlicer.
// Tree support generates organic, branching support structures instead of
// traditional rectangular pillars.  The core classes are:
//   SupportNode    — a single node in the branch graph (one per XY position per layer)
//   TreeSupportData — per-object caches for collision/avoidance polygons at each (radius, layer) pair
//   TreeSupport     — orchestrates the full pipeline: detect → contact → drop → smooth → draw → toolpaths
//
// LINEAGE: This file descends from the Cura tree-support algorithm, with heavy BBS / OrcaSlicer additions
// (sharp-tail detection, cantilever detection, OverhangCluster, polygon-node type, 'slim' variant, etc.)
//
// ORGANIC MODE: When support_style == smsTreeOrganic, generate() immediately delegates to
// generate_tree_support_3D() (TreeSupport3D.cpp) and returns.  Everything in this file handles
// the NON-organic tree-support variants (slim, strong, hybrid).
//
// [CONCURRENCY] TreeSupportData caches use tbb::concurrent_unordered_map for thread-safe access.
// SupportNode allocation is serialized by a tbb::spin_mutex inside create_node().
// drop_nodes() pre-computes avoidance areas in a tbb::parallel_for then processes layers serially.
// draw_circles() uses tbb::parallel_for over layers.

#ifndef TREESUPPORT_H
#define TREESUPPORT_H

#include <forward_list>
#include <unordered_set>
#include "ExPolygon.hpp"
#include "Point.hpp"
#include "Slicing.hpp"
#include "MinimumSpanningTree.hpp"
#include "tbb/concurrent_unordered_map.h"
#include "Flow.hpp"
#include "PrintConfig.hpp"
#include "Fill/Lightning/Generator.hpp"
#include "TreeModelVolumes.hpp"
#include "TreeSupport3D.hpp"

#ifndef SQ
#define SQ(x) ((x)*(x))
#endif

namespace Slic3r
{
class PrintObject;
class TreeSupport;
class SupportLayer;

// [INTENT] Maps a single support-tree layer to its z-coordinate, thickness,
// and the corresponding object-layer index.  Used by plan_layer_heights() to
// allow support layers to be taller than object layers (adaptive heights).
// [COUPLING] Stored in TreeSupportData::layer_heights and accessed by every
// pipeline stage that iterates over support layers.
struct LayerHeightData
{
    coordf_t print_z       = 0;  // [STATE] Top surface of this support layer (mm)
    coordf_t height        = 0;  // [STATE] Thickness of this layer (mm); 0 = skipped/empty layer
    size_t   obj_layer_nr  = 0;  // [STATE] Index into PrintObject::layers() that overlaps this support layer
    LayerHeightData()      = default;
    LayerHeightData(coordf_t z, coordf_t h, size_t obj_layer) : print_z(z), height(h), obj_layer_nr(obj_layer) {}
    coordf_t bottom_z() {
        return print_z - height;
    }
};

// [INTENT] Discriminates the shape used to render a tree-support node in draw_circles().
// eCircle  → default: 100-vertex polygon approximating a circle; scales with node radius.
// eSquare  → used when avg_node_per_layer > 200 for performance (4-vertex polygon); faster Clipper ops.
// ePolygon → used in hybrid mode for large overhangs: the node carries an explicit ExPolygon overhang.
// [COUPLING] Checked in drop_nodes() (merge rules differ per type), draw_circles() (shape generation),
//            generate_contact_points() (assign ePolygon for big hybrid overhangs).
enum TreeNodeType {
    eCircle,
    eSquare,
    ePolygon
};


/*!
 * \brief Represents the metadata of a node in the tree.
 *
 * [INTENT] One SupportNode per XY position per support layer.  Nodes form a singly-linked
 * chain from leaf (contact with overhang) down to root (build plate or model surface):
 *   leaf → parent → parent → … → root
 * Merging two branches makes both point to the same child; the merged list is tracked in
 * `merged_neighbours`.  All nodes are owned by TreeSupportData::contact_nodes (unique_ptr
 * vector); raw pointers elsewhere are borrowing references.
 *
 * [MEMORY] Lifetime is tied to TreeSupportData which lives for the duration of the
 * generate() call.  Dangling pointer risk: `parent` / `child` / `merged_neighbours` entries
 * are raw pointers into the same arena.  Nodes marked `valid = false` are logically deleted
 * but remain in memory.
 *
 * [CONCURRENCY] `radius` and `max_move_dist` are declared `mutable` because they are lazily
 * computed from `const` accessor contexts (get_radius()).  `merged_neighbours` mutations in
 * drop_nodes() are guarded by TreeSupportData::m_mutex.
 *
 * [HAZARD] `diameter_angle_scale_factor` is a static class member, meaning it is shared
 * across all SupportNode instances and all objects in the same process.  If two PrintObjects
 * are sliced concurrently with different branch angle settings, the last writer wins.
 */
struct SupportNode
{
    static constexpr SupportNode* NO_PARENT = nullptr;

    SupportNode()
        : distance_to_top(0)
        , position(Point(0, 0))
        , obj_layer_nr(0)
        , support_roof_layers_below(0)
        , to_buildplate(true)
        , parent(nullptr)
        , print_z(0.0)
        , height(0.0)
    {}

    // when dist_mm_to_top_==0, new node's dist_mm_to_top=parent->dist_mm_to_top + parent->height;
    SupportNode(const Point position, const int distance_to_top, const int obj_layer_nr, const int support_roof_layers_below, const bool to_buildplate, SupportNode* parent,
        coordf_t     print_z_, coordf_t height_, coordf_t dist_mm_to_top_ = 0, coordf_t radius_ = 0)
        : distance_to_top(distance_to_top)
        , position(position)
        , obj_layer_nr(obj_layer_nr)
        , support_roof_layers_below(support_roof_layers_below)
        , to_buildplate(to_buildplate)
        , parent(parent)
        , print_z(print_z_)
        , height(height_)
        , dist_mm_to_top(dist_mm_to_top_)
        , radius(radius_)
    {
        if (parent) {
            parents.push_back(parent);
            type = parent->type;
            overhang = parent->overhang;
            if (dist_mm_to_top == 0)
                dist_mm_to_top = parent->dist_mm_to_top + parent->height;
            if (radius == 0 && parent->radius>0)
                radius = parent->radius + (dist_mm_to_top - parent->dist_mm_to_top) * diameter_angle_scale_factor;
            parent->child = this;
            for (auto& neighbor : parent->merged_neighbours) {
                neighbor->child = this;
                parents.push_back(neighbor);
            }
            is_sharp_tail = parent->is_sharp_tail;
            skin_direction = parent->skin_direction;
        }
    }

#ifdef DEBUG // Clear the delete node's data so if there's invalid access after, we may get a clue by inspecting that node.
    ~SupportNode()
    {
        parent = nullptr;
        merged_neighbours.clear();
    }
#endif // DEBUG

    /*!
     * \brief The number of layers to go to the top of this branch.
     * Negative value means it's a virtual node between support and overhang, which doesn't need to be extruded.
     * [STATE] Decremented by 1 at each layer as a node is propagated downward in drop_nodes().
     * [HAZARD] plan_layer_heights() may set this to a negative value (-num_layers) to encode
     * multi-layer gap height; consumers must handle the negative sentinel.
     */
    int distance_to_top;
    // [STATE] Physical distance (mm) from this node's print_z down to the contact (overhang) surface.
    // Initialized to 0 at contact; accumulated in the SupportNode constructor as nodes propagate downward.
    // Used by calc_branch_radius() to determine how wide the branch should be at this height.
    coordf_t dist_mm_to_top = 0;  // dist to bottom contact in mm

    // [HAZARD] Class-level (static) variable — shared across all SupportNode instances and all
    // PrintObjects in a single process.  Set once in drop_nodes() from TreeSupport::diameter_angle_scale_factor.
    // NOT thread-safe for concurrent multi-object slicing.
    // all nodes will have same diameter_angle_scale_factor because it's defined by user
    static double diameter_angle_scale_factor;

    // [STATE] Scaled integer XY position of this node on its layer (Clipper units, 1 = 1e-6 mm).
    Point          position;
    // [STATE] Displacement vector applied by smooth_nodes(); used in draw_circles() to elongate the
    // circle into an ellipse along the movement direction for smoother visual transitions.
    Point          movement; // movement towards neighbor center or outline
    // [STATE] Branch radius at this node (mm, unscaled).  mutable because lazily computed by
    // get_radius() in const contexts.  0 = not yet computed.
    mutable double radius          = 0.0;
    // [STATE] Maximum movement distance per layer for this node (mm).  mutable because lazily
    // computed from node->height and tan(branch_angle).
    mutable double max_move_dist   = 0.0;
    // [STATE] Shape to use when rendering this node as a polygon in draw_circles().
    TreeNodeType   type            = eCircle;
    // [STATE] True if this contact point was placed at a sharp corner of an overhang contour.
    // Corner nodes are never merged with nearby nodes in generate_contact_points().
    bool           is_corner       = false;
    // [STATE] Bookkeeping flag for smooth_nodes(): marks nodes that have been smoothed in the
    // current pass so that branch chains are not re-processed.
    bool           is_processed    = false;
    // [STATE] Set by smooth_nodes() when a branch has multiple parents, large movement, or is a
    // tall branch near its tip.  Causes draw_circles() to add an extra wall loop.
    bool           need_extra_wall = false;
    // [STATE] Propagated from parent; marks nodes that belong to a sharp-tail overhang region.
    // Affects collision trimming (uses top_z_distance instead of xy_distance) and interface generation.
    bool           is_sharp_tail   = false;
    // [STATE] Logical-deletion flag.  Nodes merged into neighbours are marked valid=false but
    // remain in memory.  drop_nodes() skips invalid nodes.
    bool           valid = true;
    // [STATE] For ePolygon nodes, holds the original overhang ExPolygon so draw_circles() can
    // reproduce it verbatim rather than approximating with a circle.
    ExPolygon      overhang; // when type==ePolygon, set this value to get original overhang area

    /*!
     * \brief The direction of the skin lines above the tip of the branch.
     *
     * This determines in which direction we should reduce the width of the
     * branch.
     */
    Point skin_direction;

    /*!
     * \brief The number of support roof layers below this one.
     *
     * When a contact point is created, it is determined whether the mesh
     * needs to be supported with support roof or not, since that is a
     * per-mesh setting. This is stored in this variable in order to track
     * how far we need to extend that support roof downwards.
     */
    int support_roof_layers_below;
    int obj_layer_nr;

    /*!
     * \brief Whether to try to go towards the build plate.
     *
     * If the node is inside the collision areas, it has no choice but to go
     * towards the model. If it is not inside the collision areas, it must
     * go towards the build plate to prevent a scar on the surface.
     */
    bool to_buildplate;

    /*!
     * \brief The originating node for this one, one layer higher.
     *
     * In order to prune branches that can't have any support (because they
     * can't be on the model and the path to the buildplate isn't clear),
     * the entire branch needs to be known.
     */
    // [STATE] Parent node (one layer below this one).  nullptr for root nodes that have reached the build plate.
    // [MEMORY] Raw borrowing pointer — owned by TreeSupportData::contact_nodes.
    SupportNode* parent;
    // [STATE] All parents (i.e. this node + merged_neighbours' parents) collected when the node is created.
    // Used to propagate is_sharp_tail and other attributes from all contributing branches.
    std::vector<SupportNode*> parents;
    // [STATE] Child node (one layer above this one).  Nullptr for leaf (contact) nodes.
    // Set in the SupportNode constructor when a parent-child relationship is established.
    SupportNode* child = nullptr;

    /*!
    * \brief All neighbours (on the same layer) that where merged into this node.
    *
    * In order to prune branches that can't have any support (because they
    * can't be on the model and the path to the buildplate isn't clear),
    * the entire branch needs to be known.
    * [STATE] Populated in drop_nodes() when two nearby nodes are merged.
    * [MEMORY] Raw borrowing pointers.  Merging invalidates the neighbour (valid=false) but
    * the raw pointer remains — do not dereference after merging without checking valid.
    */
    std::list<SupportNode*> merged_neighbours;

    // [STATE] Z height of the top surface of this layer (mm, unscaled).
    coordf_t print_z;
    // [STATE] Layer thickness (mm). 0 for gap/virtual layers.
    coordf_t height;

    bool operator==(const SupportNode& other) const
    {
        return position == other.position;
    }
};

/*!
 * \brief Lazily generates tree guidance volumes.
 *
 * [INTENT] Per-object cache for collision and avoidance polygons at (radius, layer_nr) pairs.
 * "Collision" = expanded model outline that a branch of given radius must not enter.
 * "Avoidance" = union of collision regions propagated upward from the build plate, representing
 *               all positions a branch cannot occupy if it is to reach the build plate without
 *               embedding in the model.
 *
 * [MEMORY] Owns all SupportNode instances via contact_nodes (unique_ptr).  Raw SupportNode*
 * pointers held elsewhere are borrowing references into this arena.  clear_nodes() invalidates
 * all of them.
 *
 * [CONCURRENCY] m_collision_cache and m_avoidance_cache use tbb::concurrent_unordered_map
 * so they are safe to read from multiple TBB threads simultaneously.  create_node() and
 * clear_nodes() are guarded by m_mutex (tbb::spin_mutex).
 * calculate_avoidance() is RECURSIVE and caps depth at 100 layers (max_recursion_depth).
 *
 * \warning This class is not currently thread-safe and should not be accessed in OpenMP blocks
 */
class TreeSupportData
{
public:
    TreeSupportData() = default;
    /*!
     * \brief Construct the TreeSupportData object
     *
     * \param xy_distance The required clearance between the model and the
     * tree branches.
     * \param radius_sample_resolution Sample size used to round requested node radii.
     */
    TreeSupportData(const PrintObject& object, coordf_t xy_distance, coordf_t radius_sample_resolution);
    ~TreeSupportData() {
        clear_nodes();
    }

    TreeSupportData(TreeSupportData&&) = default;
    TreeSupportData& operator=(TreeSupportData&&) = default;

    TreeSupportData(const TreeSupportData&) = delete;
    TreeSupportData& operator=(const TreeSupportData&) = delete;

    /*!
     * \brief Creates the areas that have to be avoided by the tree's branches.
     *
     * The result is a 2D area that would cause nodes of radius \p radius to
     * collide with the model.
     *
     * \param radius The radius of the node of interest
     * \param layer The layer of interest
     * \return Polygons object
     */
    const ExPolygons& get_collision(coordf_t radius, size_t layer_idx) const;

    /*!
     * \brief Creates the areas that have to be avoided by the tree's branches
     * in order to reach the build plate.
     *
     * The result is a 2D area that would cause nodes of radius \p radius to
     * collide with the model or be unable to reach the build platform.
     *
     * The input collision areas are inset by the maximum move distance and
     * propagated upwards.
     *
     * \param radius The radius of the node of interest
     * \param layer The layer of interest
     * \return Polygons object
     */
    const ExPolygons& get_avoidance(coordf_t radius, size_t layer_idx, int recursions=0) const;

    Polygons get_contours(size_t layer_nr) const;
    Polygons get_contours_with_holes(size_t layer_nr) const;

    SupportNode* create_node(const Point position, const int distance_to_top, const int obj_layer_nr, const int support_roof_layers_below, const bool to_buildplate, SupportNode* parent,
        coordf_t     print_z_, coordf_t height_, coordf_t dist_mm_to_top_ = 0, coordf_t radius_ = 0);
    void clear_nodes();

    // [STATE] One LayerHeightData entry per support layer, produced by plan_layer_heights().
    // Indexed by "support layer index" (which may differ from object layer index when adaptive heights are used).
    // Shared across all pipeline stages; never modified after plan_layer_heights() returns.
    std::vector<LayerHeightData> layer_heights;

    // [STATE] Flat arena of all SupportNode instances, owned via unique_ptr.
    // Raw SupportNode* pointers held in contact_nodes (TreeSupport private field) and
    // in SupportNode::parent/child/merged_neighbours are borrowing references into this arena.
    // [MEMORY] Nodes are only logically deleted (valid=false); memory is reclaimed when
    // TreeSupportData is destroyed or clear_nodes() is called.
    std::vector<std::unique_ptr<SupportNode>> contact_nodes;
    // ExPolygon                  m_machine_border;

private:
    /*!
     * \brief Convenience key type for the collision/avoidance caches.
     *
     * [INTENT] Bundles (radius, layer_nr, recursions) into a single hashable struct.
     * radius is pre-rounded via ceil_radius() so cache keys align to m_radius_sample_resolution
     * multiples, reducing the number of distinct entries.
     * [HAZARD] RadiusLayerPairEquality intentionally ignores the `recursions` field in its
     * equality check — two keys with the same (radius, layer_nr) but different recursion depths
     * are treated as equal.  This is intentional (recursions is only used to cap stack depth,
     * not to produce different avoidance values), but is subtle and fragile.
     */
    struct RadiusLayerPair {
        coordf_t radius;
        size_t layer_nr;
        int recursions;  // [STATE] Recursion depth counter used only to prevent stack overflow in calculate_avoidance(); ignored by equality.
    };
    // [INTENT] Equality functor: two keys are equal iff radius AND layer_nr match.
    // Intentionally ignores `recursions` — see RadiusLayerPair note above.
    struct RadiusLayerPairEquality {
        constexpr bool operator()(const RadiusLayerPair& _Left, const RadiusLayerPair& _Right) const {
            return _Left.radius == _Right.radius && _Left.layer_nr == _Right.layer_nr;
        }
    };
    // [INTENT] Hash functor for RadiusLayerPair.  Mixes radius and layer_nr using a
    // prime multiplier (7919) on layer_nr.  Does NOT include recursions (consistent with equality).
    struct RadiusLayerPairHash {
        size_t operator()(const RadiusLayerPair& elem) const {
            return std::hash<coord_t>()(elem.radius) ^ std::hash<coord_t>()(elem.layer_nr * 7919);
        }
    };

    /*!
     * \brief Round \p radius upwards to a multiple of m_radius_sample_resolution
     *
     * \param radius The radius of the node of interest
     */
    coordf_t ceil_radius(coordf_t radius) const;

    /*!
     * \brief Calculate the collision areas at the radius and layer indicated
     * by \p key.
     *
     * \param key The radius and layer of the node of interest
     */
    const ExPolygons& calculate_collision(const RadiusLayerPair& key) const;

    /*!
     * \brief Calculate the avoidance areas at the radius and layer indicated
     * by \p key.
     *
     * \param key The radius and layer of the node of interest
     */
    const ExPolygons& calculate_avoidance(const RadiusLayerPair& key) const;

    // [STATE] Mutex protecting create_node() and clear_nodes() from concurrent writers.
    // [CONCURRENCY] tbb::spin_mutex — appropriate for very short critical sections.
    // The caches (m_collision_cache, m_avoidance_cache) use concurrent_unordered_map
    // so they do NOT need this lock for reads or lazy inserts.
    tbb::spin_mutex  m_mutex;

public:
    // [STATE] True when the current support style is "slim" (smsTreeSlim or smsTreeHybrid slim path).
    // Affects branch radius tip taper length (doubled for slim).
    bool is_slim = false;
    /*!
     * \brief The required clearance between the model and the tree branches
     * [STATE] Set from PrintObjectConfig::support_xy_distance at construction.
     */
    coordf_t m_xy_distance;

    // [STATE] tan(tree_support_branch_angle). Used to compute max horizontal move per layer.
    double branch_scale_factor = 1.0; // tan(45 degrees)

    /*!
     * \brief Sample resolution for radius values.
     *
     * The radius will be rounded (upwards) to multiples of this value before
     * calculations are done when collision, avoidance and internal model
     * Polygons are requested.
     * [INTENT] Reduces the number of distinct cache keys, trading small accuracy losses
     * for much better cache hit rates.  Typically set to g_config_tree_support_collision_resolution.
     */
    coordf_t m_radius_sample_resolution;

    /*!
     * \brief Storage for layer outlines of the meshes.
     * [STATE] m_layer_outlines[layer_nr] = simplified union of all ExPolygons on that layer.
     * Populated once in TreeSupportData constructor; never modified afterwards.
     * [COUPLING] Used by calculate_collision() (offset by radius + xy_distance) and by
     * draw_circles() (lazy collision closure).
     */
    std::vector<ExPolygons> m_layer_outlines;

    // [STATE] m_layer_outlines_below[layer_nr] = cumulative union of all layer outlines from 0..layer_nr.
    // Used in drop_nodes() to determine which "part" (ExPolygon island) a node belongs to.
    // [HAZARD] This is an O(N) cumulative union computed serially in the constructor for each layer —
    // a FIXME in SupportMaterial notes this should be a parallel prefix sum.
    // union contours of all layers below
    std::vector<ExPolygons> m_layer_outlines_below;

    // [STATE] Per-layer maximum horizontal move distance (mm) = layer_height * tan(branch_angle).
    // Used in calculate_avoidance() to inset avoidance areas upward layer by layer.
    std::vector<double> m_max_move_distances;

    /*!
     * \brief Caches for the collision, avoidance and internal model polygons
     * at given radius and layer indices.
     *
     * These are mutable to allow modification from const function. This is
     * generally considered OK as the functions are still logically const
     * (ie there is no difference in behaviour for the user betweeen
     * calculating the values each time vs caching the results).
     *
     * coconut: previously stl::unordered_map is used which seems problematic with tbb::parallel_for.
     * So we change to tbb::concurrent_unordered_map
     *
     * [CONCURRENCY] tbb::concurrent_unordered_map allows concurrent reads and concurrent inserts
     * from multiple TBB worker threads without external locking.
     * [HAZARD] calculate_avoidance() is recursive with depth proportional to layer_nr; pre-computes
     * layer (layer_nr - max_recursion_depth) first to cap call-stack depth at ~100 frames.
     */
    mutable tbb::concurrent_unordered_map<RadiusLayerPair, ExPolygons, RadiusLayerPairHash, RadiusLayerPairEquality> m_collision_cache;
    mutable tbb::concurrent_unordered_map<RadiusLayerPair, ExPolygons, RadiusLayerPairHash, RadiusLayerPairEquality> m_avoidance_cache;

    friend TreeSupport;
};

// [INTENT] Hash functor for Line objects, used to key MST line→layer-contour intersection caches
// (TreeSupport::m_mst_line_x_layer_contour_caches).  Combines endpoint coordinates using XOR and
// asymmetric prime multipliers so that swapping a/b of the line gives a different hash.
// [HAZARD] XOR-based hashing of two pairs of coordinates can produce many collisions for symmetric
// inputs (e.g., horizontal/vertical lines); acceptable in practice because cache sizes are small.
struct LineHash {
    size_t operator()(const Line& line) const {
        return (std::hash<coord_t>()(line.a(0)) ^ std::hash<coord_t>()(line.b(1))) * 102 +
            (std::hash<coord_t>()(line.a(1)) ^ std::hash<coord_t>()(line.b(0))) * 10222;
    }
};

/*!
 * \brief Generates a tree structure to support your models.
 *
 * [INTENT] Top-level orchestrator for BBS non-organic tree support.  Owns the full pipeline:
 *   detect_overhangs() → generate_contact_points() → plan_layer_heights() →
 *   drop_nodes() → smooth_nodes() → draw_circles() → generate_toolpaths()
 *
 * [COUPLING] Reads PrintObject (mesh, config, object layers) and writes SupportLayer expolygons.
 * Delegates to TreeSupportData for collision/avoidance caching and node arena allocation.
 * For smsTreeOrganic, generate() immediately calls generate_tree_support_3D() (TreeSupport3D.cpp)
 * and returns without executing any of the methods in this class.
 *
 * [CONCURRENCY] Most internal methods are serial.  generate_contact_points() and draw_circles()
 * use tbb::parallel_for internally.  drop_nodes() pre-computes avoidance in parallel but
 * processes layers serially.
 */
class TreeSupport
{
public:
    /*!
     * \brief Creates an instance of the tree support generator.
     *
     * \param storage The data storage to get global settings from.
     */
    TreeSupport(PrintObject& object, const SlicingParameters &slicing_params);

    // [INTENT] Bridge method used when organic-mode (TreeSupport3D) produces SupportElements;
    // converts them into SupportNode objects compatible with this class's contact_nodes layout
    // so that generate_toolpaths() can render them.
    void move_bounds_to_contact_nodes(std::vector<TreeSupport3D::SupportElements> &move_bounds,
                                      PrintObject                                 &print_object,
                                      const TreeSupport3D::TreeSupportSettings    &config);

    /*!
     * \brief Create the areas that need support.
     *
     * These areas are stored inside the given SliceDataStorage object.
     * \param storage The data storage where the mesh data is gotten from and
     * where the resulting support areas are stored.
     */
    void generate();

    // [INTENT] Public so that Print.cpp can call it for "check_support_necessity" mode
    // (dry-run detect to decide if any support is needed at all, without full generation).
    void detect_overhangs(bool check_support_necessity = false);

    // [INTENT] Thin wrapper that delegates to m_ts_data->create_node() so callers inside
    // TreeSupport.cpp don't need to hold a direct reference to TreeSupportData.
    SupportNode* create_node(const Point  position,
        const int    distance_to_top,
        const int    obj_layer_nr,
        const int    support_roof_layers_below,
        const bool   to_buildplate,
        SupportNode* parent,
        coordf_t     print_z_,
        coordf_t     height_,
        coordf_t     dist_mm_to_top_ = 0,
        coordf_t     radius_ = 0)
    {
        return m_ts_data->create_node(position, distance_to_top, obj_layer_nr, support_roof_layers_below, to_buildplate, parent, print_z_, height_, dist_mm_to_top_, radius_);
    }

    // [STATE] Rolling average of nodes per layer, computed in generate_contact_points().
    // Used in draw_circles() to switch from 100-vertex circles to 4-vertex squares when
    // avg_node_per_layer > 200, for performance.
    int  avg_node_per_layer = 0;
    // [STATE] Dominant angle of skin/perimeter lines on overhang surfaces (radians).
    // Used in generate_contact_points() to orient support-roof interface infill.
    float nodes_angle = 0;
    // [STATE] Set to true by detect_overhangs() if any sharp-tail overhangs were found.
    // Checked by generate() to decide whether sharp-tail-specific code paths run.
    bool  has_sharp_tails = false;
    // [STATE] Set to true by detect_overhangs() if any cantilever overhangs were found.
    bool  has_cantilever = false;
    // [STATE] Maximum horizontal cantilever distance found during detect_overhangs() (mm).
    double max_cantilever_dist = 0;
    // [STATE] Support type (normal/tree/hybrid) read from config; used in generate() to
    // decide which code path to follow.
    SupportType support_type;

    // [STATE] Lightning infill generator, created lazily in generate_toolpaths() when
    // lightning infill is requested for support base layers.
    std::unique_ptr<FillLightning::Generator> generator;
    // [STATE] Maps print_z (mm) → lightning layer index for fast lookup in generate_toolpaths().
    std::unordered_map<double, size_t> printZ_to_lightninglayer;

    // [COUPLING] Cancellation callback injected by Print.cpp; called periodically in long loops.
    // Throws if user cancelled the slice.
    std::function<void()> throw_on_cancel;
    // [COUPLING] Global PrintConfig pointer; used for printer-level settings (e.g., nozzle diameter).
    const PrintConfig* m_print_config;
    /*!
     * \brief Polygons representing the limits of the printable area of the
     * machine
     */
    ExPolygon m_machine_border;

    // [INTENT] Classifies each detected overhang ExPolygon by the reason it was flagged:
    //   Detected   — normal overhang angle threshold
    //   Enforced   — user-placed support enforcer modifier
    //   SharpTail  — small footprint detected as sharp-tail
    // Used in generate_contact_points() and draw_circles() to apply different rules per overhang type.
    enum OverhangType { Detected = 0, Enforced, SharpTail };
    std::map<const ExPolygon*, OverhangType> overhang_types;
    // [STATE] Vertical enforcer volumes expressed as (bottom_point, top_point) pairs.
    // Populated from support-enforcer mesh volumes; used in detect_overhangs() to add
    // enforced contacts even in areas below the normal overhang threshold.
    std::vector<std::pair<Vec3f, Vec3f>>      m_vertical_enforcer_points;

private:
    /*!
     * \brief Generator for model collision, avoidance and internal guide volumes
     *
     * Lazily computes volumes as needed.
     *  \warning This class is NOT currently thread-safe and should not be accessed in OpenMP blocks
     */
    // [STATE] Per-layer vectors of raw SupportNode* — one inner vector per support layer.
    // Nodes are borrowed from m_ts_data->contact_nodes (unique_ptr arena).
    // Populated by generate_contact_points(), modified by drop_nodes(), consumed by draw_circles().
    std::vector<std::vector<SupportNode*>> contact_nodes;
    // [STATE] Shared ownership of the TreeSupportData cache/arena.
    // Shared with external callers (e.g. move_bounds_to_contact_nodes) via shared_ptr.
    std::shared_ptr<TreeSupportData> m_ts_data;
    // [STATE] Organic-mode model volume cache (used only when generate() delegates to TreeSupport3D).
    std::unique_ptr<TreeSupport3D::TreeModelVolumes> m_model_volumes;
    // [COUPLING] Raw pointer to the PrintObject being processed — not owned.
    PrintObject    *m_object;
    // [COUPLING] Raw pointer to the per-object config — not owned; valid for the duration of generate().
    const PrintObjectConfig* m_object_config;
    // [STATE] Slicing parameters (layer heights, raft, etc.) copied at construction.
    SlicingParameters        m_slicing_params;
    // [STATE] Precomputed support flow/width/spacing parameters shared across pipeline stages.
    SupportParameters   m_support_params;
    // [STATE] Number of raft layers (base + interface + gap); used as an offset when indexing
    // support layers in draw_circles() and generate_toolpaths().
    size_t          m_raft_layers = 0;  // number of raft layers, including raft base, raft interface, raft gap
    // [STATE] Highest object layer index that has at least one overhang; used to skip
    // layers above the topmost overhang in drop_nodes().
    size_t          m_highest_overhang_layer = 0;
    // [STATE] Per-layer MSTs used in drop_nodes() to find optimal merge candidates.
    // One MST per "part" (ExPolygon island) per layer.
    std::vector<std::vector<MinimumSpanningTree>> m_spanning_trees;
    // [STATE] Per-layer cache: for each MST edge (Line), whether it crosses the layer's
    // object contour.  Used in drop_nodes() to avoid merging across part boundaries.
    std::vector< std::unordered_map<Line, bool, LineHash>> m_mst_line_x_layer_contour_caches;
    // [STATE] Minimum Z height (mm) below which nodes are not allowed to move horizontally.
    // Set in generate() from config; prevents bottom-most branch segments from moving.
    float    DO_NOT_MOVER_UNDER_MM = 0.0;
    // [STATE] Base radius of a branch at its tip (mm). Derived from nozzle size / line width.
    coordf_t base_radius                        = 0.0;
    // [STATE] Radius clamps (mm) applied in calc_branch_radius() to prevent unreasonably
    // thin or thick branches.
    const coordf_t MAX_BRANCH_RADIUS = 10.0;
    const coordf_t MIN_BRANCH_RADIUS = 0.4;
    const coordf_t MAX_BRANCH_RADIUS_FIRST_LAYER = 12.0;
    const coordf_t MIN_BRANCH_RADIUS_FIRST_LAYER = 2.0;
    // [STATE] Instance copy of the diameter angle scale factor = tan(branch_angle_degrees * π/180).
    // Copied to SupportNode::diameter_angle_scale_factor (static) in drop_nodes().
    // [HAZARD] The static copy on SupportNode is shared across all objects — last writer wins
    // in concurrent multi-object slicing.
    double diameter_angle_scale_factor = tan(5.0*M_PI/180.0);
    // [STATE] Minimum roof area threshold (scaled mm^2).  Interface layers are not generated
    // for contact regions smaller than this.
    const double minimum_roof_area{SQ(scaled<double>(1.))};
    // [STATE] Z distance (mm) between the top of the support and the model surface.
    // Derived from support_top_z_distance config.  Used when trimming support near sharp tails.
    float        top_z_distance = 0.0;

    // [STATE] True when support_style is smsTreeStrong; enables thicker branches and
    // disables some slim-mode taper optimizations.
    bool  is_strong = false;
    // [STATE] True when support_style is smsTreeSlim or smsTreeHybrid slim path.
    // Enables longer tip taper, thinner base, and different merge distances.
    bool  is_slim                            = false;
    // [STATE] True when support infill pattern is not "None"; enables infill generation
    // inside support base layers in generate_toolpaths().
    bool  with_infill                        = false;



    /*!
     * \brief Draws circles around each node of the tree into the final support.
     *
     * This also handles the areas that have to become support roof, support
     * bottom, the Z distances, etc.
     *
     * \param storage[in, out] The settings storage to get settings from and to
     * save the resulting support polygons to.
     * \param contact_nodes The nodes to draw as support.
     * [CONCURRENCY] Uses tbb::parallel_for over layers.  Each layer is independent.
     * [INTENT] Converts the abstract SupportNode graph into concrete ExPolygon areas
     * stored in SupportLayer::support_fills / support_interface / support_roof.
     */
    void draw_circles();

    /*!
     * \brief Drops down the nodes of the tree support towards the build plate.
     *
     * This is where the cleverness of tree support comes in: The nodes stay on
     * their 2D layers but on the next layer they are slightly shifted. This
     * causes them to move towards each other as they are copied to lower layers
     * which ultimately results in a 3D tree.
     *
     * \param contact_nodes[in, out] The nodes in the space that need to be
     * dropped down. The nodes are dropped to lower layers inside the same
     * vector of layers.
     * [INTENT] Fully serial per layer (top→bottom).  Pre-computes avoidance in parallel first.
     * [HAZARD] insert_dropped_node() uses std::find linear scan — O(N) per insertion on dense layers.
     */
    void drop_nodes();

    // [INTENT] Smooths branch paths with 100 iterations of Laplacian averaging.
    // Sets need_extra_wall on nodes where the smoothed path changes significantly.
    void smooth_nodes();

    /*! BBS: MusangKing: maximum layer height
     * \brief Optimize the generation of tree support by pre-planning the layer_heights
     *
     * [INTENT] Pre-plans support layer heights, allowing support layers to be thicker than
     * object layers.  Stores results in m_ts_data->layer_heights and re-distributes contact_nodes.
    */

    std::vector<LayerHeightData> plan_layer_heights();
    /*!
     * \brief Creates points where support contacts the model.
     *
     * A set of points is created for each layer.
     * \param mesh The mesh to get the overhang areas to support of.
     * \param contact_nodes[out] A vector of mappings from contact points to
     * their tree nodes.
     * \param collision_areas For every layer, the areas where a generated
     * contact point would immediately collide with the model due to the X/Y
     * distance.
     * \return For each layer, a list of points where the tree should connect
     * with the model.
     * [CONCURRENCY] Uses tbb::parallel_for; results accumulated with tbb::spin_mutex.
     * [INTENT] Generates a rotated 22° grid of candidate contact points over each
     * overhang bounding box, then clips to the overhang polygon.
     */
    void generate_contact_points();

    /*!
     * \brief Add a node to the next layer.
     *
     * If a node is already at that position in the layer, the nodes are merged.
     * [HAZARD] Uses std::find linear scan — O(N) per call.  On layers with many nodes
     * (e.g. large flat overhangs) this becomes O(N²) total for a single layer pass.
     */
    void insert_dropped_node(std::vector<SupportNode*>& nodes_layer, SupportNode* node);
    // [INTENT] Creates the SupportLayer objects in PrintObject with correct print_z and height.
    // Must be called before draw_circles() writes into those layers.
    void create_tree_support_layers();
    // [INTENT] Converts contact_nodes into final SupportLayer expolygons (walls, infill, interface).
    // Handles raft, roof/floor interface, base, and lightning infill paths.
    void generate_toolpaths();
    // [INTENT] Computes branch radius at a given distance-to-top, in layer units.
    // Clamps to [MIN_BRANCH_RADIUS, MAX_BRANCH_RADIUS].
    coordf_t calc_branch_radius(coordf_t base_radius, size_t layers_to_top, size_t tip_layers, double diameter_angle_scale_factor);
    // [INTENT] Computes branch radius at a given distance-to-top, in mm.
    // use_min_distance = false skips the floor clamp (used for tip taper in slim mode).
    coordf_t calc_branch_radius(coordf_t base_radius, coordf_t mm_to_top, double diameter_angle_scale_factor, bool use_min_distance=true);
    // [INTENT] Convenience wrapper: computes radius from mm_to_top using class-level diameter_angle_scale_factor.
    coordf_t   calc_radius(coordf_t mm_to_top);
    // [INTENT] Returns the effective radius of a SupportNode, computing and caching it in node->radius
    // if not already set.
    coordf_t get_radius(const SupportNode* node);
    // [INTENT] Returns avoidance ExPolygons for a given (radius, obj_layer_nr) pair.
    // Avoidance = areas a branch CANNOT occupy if it must reach the build plate.
    ExPolygons get_avoidance(coordf_t radius, size_t obj_layer_nr);
    // [INTENT] Returns collision ExPolygons (model outline expanded by radius + xy_distance)
    // for a given (radius, layer_nr) pair.
    ExPolygons get_collision(coordf_t radius, size_t layer_nr);
    // [INTENT] Same as get_collision() but returns Polygons (flattened, no holes).
    Polygons get_collision_polys(coordf_t radius, size_t layer_nr);

    // similar to SupportMaterial::trim_support_layers_by_object
    // [INTENT] Clips support regions against the object to enforce top/bottom/xy gaps.
    // Returns the trimmed polygon area that must be subtracted from the support layer.
    Polygons get_trim_support_regions(
        const PrintObject& object,
        SupportLayer* support_layer_ptr,
        const coordf_t       gap_extra_above,
        const coordf_t       gap_extra_below,
        const coordf_t       gap_xy);
};

}

#endif /* TREESUPPORT_H */
