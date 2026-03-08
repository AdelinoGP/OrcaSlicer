// [INTENT] Public API header for the 2D bin-packing / auto-arrange subsystem.
// Exposes ArrangePolygon (input/output item descriptor), ArrangeParams
// (algorithm configuration), and the template arrange() family that dispatches
// to libnest2d for actual NFP-based placement.
//
// [STATE] Arrange is stateless from the caller's perspective: callers populate
// ArrangePolygons, call arrange(), and read back translation/rotation/bed_idx.
// Internal state (spatial indices, pile bounding boxes) lives only inside
// AutoArranger<TBin> for the duration of a single arrange() call.
//
// [COUPLING] Depends on ExPolygon, PrintConfig (DynamicPrintConfig, bed shape
// helpers), and Print (is_filaments_compatible).  libnest2d is the heavy
// backend dependency — it is vendored in deps/ and must not be replaced without
// updating all template specialisations in Arrange.cpp.
//
// [CONCURRENCY] ArrangeParams::parallel enables TBB-based parallelism inside
// the NFP placer. The progress callback (progressind) and stop predicate
// (stopcondition) are called from worker threads — callers must ensure these
// lambdas are thread-safe.

#ifndef ARRANGE_HPP
#define ARRANGE_HPP

#include "ExPolygon.hpp"
#include "PrintConfig.hpp"
#include "Print.hpp"

// [INTENT] Constant: bed shrink amount (mm) applied on each side for sequential
// print mode to ensure the print head rod clearance.  Tight coupling: any
// change to rod clearance geometry must revisit this constant.
#define BED_SHRINK_SEQ_PRINT 5

namespace Slic3r {

class BoundingBox;

namespace arrangement {

// [INTENT] Lightweight value type representing a circular print bed.
// Used by call_with_bed() when the bed polygon closely approximates a circle
// (all vertices within 10*SCALED_EPSILON of average radius).
// [STATE] Immutable after construction; center and radius are plain value fields.
// [COUPLING] Consumed exclusively by arrange<CircleBed>() template specialisation.
/// A geometry abstraction for a circular print bed. Similarly to BoundingBox.
class CircleBed
{
    Point  center_;
    double radius_;

public:
    inline CircleBed() : center_(0, 0), radius_(std::nan("")) {}
    explicit inline CircleBed(const Point& c, double r) : center_(c), radius_(r) {}

    inline double       radius() const { return radius_; }
    inline const Point& center() const { return center_; }
};

// [INTENT] Represents an unbounded print area — items are arranged relative to
// a center point with no collision boundary. Used as a fallback when the bed
// shape cannot be determined (0 or 1 bed point).
// [COUPLING] Used as a template argument for arrange<InfiniteBed>().
/// Representing an unbounded bed.
struct InfiniteBed
{
    Point center;
    explicit InfiniteBed(const Point& p = {0, 0}) : center{p} {}
};

// [INTENT] Sentinel: indicates this ArrangePolygon has not yet been placed
// by a successful arrange() call, or was rejected because it does not fit the bed.
// [COUPLING] Checked by is_arranged() and used by callers to detect unplaced items.
/// A logical bed representing an object not being arranged. Either the arrange
/// has not yet successfully run on this ArrangePolygon or it could not fit the
/// object due to overly large size or invalid geometry.
static const constexpr int UNARRANGED = -1;

// [INTENT] Combined input/output descriptor for one printable object silhouette.
// Before arrange(): caller fills poly, translation (initial offset), rotation
// (initial angle), inflation (brim/clearance radius in scaled coords), and all
// material/temperature metadata.  After arrange(): translation, rotation, and
// bed_idx are updated with the placed position.
//
// [STATE] is_applied guards against applying the setter callback twice.
//         Multiple fields (bed_temp, print_temp, first_bed_temp, vitrify_temp)
//         are only used by the objective function inside AutoArranger — they are
//         NOT transformed by arrange() itself.
//
// [HAZARD] allowed_rotations is present but the comment says "currently
//         unsupported". The field is silently ignored; only params.allow_rotations
//         controls whether rotations are tried, and only the four 45° steps in
//         fill_config() are used.  A port must not trust allowed_rotations to
//         constrain rotation angles per-item.
//
// [COUPLING] setter is a type-erased callback; callers store back-references
//         to their own data structures inside this closure.  The lifetime of
//         captured objects must exceed the arrange() call.
/// Input/Output structure for the arrange() function. The poly field will not
/// be modified during arrangement. Instead, the translation and rotation fields
/// will mark the needed transformation for the polygon to be in the arranged
/// position. These can also be set to an initial offset and rotation.
///
/// The bed_idx field will indicate the logical bed into which the
/// polygon belongs: UNARRANGED means no place for the polygon
/// (also the initial state before arrange), 0..N means the index of the bed.
/// Zero is the physical bed, larger than zero means a virtual bed.
struct ArrangePolygon
{
    ExPolygon poly;                /// The 2D silhouette to be arranged
    Vec2crd   translation{0, 0};   /// The translation of the poly
    double    rotation{0.0};       /// The rotation of the poly in radians
    coord_t   inflation = 0;       /// Arrange with inflated polygon
    int       bed_idx{UNARRANGED}; /// To which logical bed does poly belong...
    int       priority{0};
    // BBS: add locked_plate to indicate whether it is in the locked plate
    int  locked_plate{-1};
    bool is_virt_object{false};
    bool is_extrusion_cali_object{false};
    bool is_wipe_tower{false};
    bool has_tree_support{false};
    // BBS: add row/col for sudoku-style layout
    int              row{0};
    int              col{0};
    std::vector<int> extrude_ids{}; /// extruder_id for least extruder switch
    int              filament_temp_type{-1};
    int              bed_temp{0};         /// bed temperature for different material judge
    int              print_temp{0};       /// print temperature for different material judge
    int              first_bed_temp{0};   /// first layer bed temperature for different material judge
    int              first_print_temp{0}; /// first layer print temperature for different material judge
    int              vitrify_temp{0}; // max bed temperature for material compatibility, which is usually the filament vitrification temp
    int              itemid{0};       // item id in the vector, used for accessing all possible params like extrude_id
    int              is_applied{0};   // transform has been applied
    double           height{0};       // item height
    double           brim_width{0};   // brim width
    std::string      name;

    // If empty, any rotation is allowed (currently unsupported)
    // If only a zero is there, no rotation is allowed
    std::vector<double> allowed_rotations = {0.};

    /// Optional setter function which can store arbitrary data in its closure
    std::function<void(const ArrangePolygon&)> setter = nullptr;

    /// Helper function to call the setter with the arrange data arguments
    void apply()
    {
        if (setter && !is_applied) {
            setter(*this);
            is_applied = 1;
        }
    }

    /// Test if arrange() was called previously and gave a successful result.
    bool is_arranged() const { return bed_idx != UNARRANGED; }

    inline ExPolygon transformed_poly() const
    {
        ExPolygon ret = poly;
        ret.rotate(rotation);
        ret.translate(translation.x(), translation.y());

        return ret;
    }
};

using ArrangePolygons = std::vector<ArrangePolygon>;

// [INTENT] Algorithm configuration bundle passed to arrange(). All fields are
// value-copied into AutoArranger at construction time — mutations after the
// arrange() call starts have no effect.
//
// [STATE] excluded_regions and nonprefered_regions are ArrangePolygon vectors
// that represent plate zones the packer must avoid (hard exclusion) or
// deprioritise (soft penalty). They are converted to libnest2d Items inside
// fill_config() and inflated by -2*EPSILON to prevent false adjacency hits.
//
// [HAZARD] bed_shrink_x/y starts at 1 mm and is further mutated by
// update_arrange_params() which adds brim_skirt_distance then subtracts
// clearance_radius/2 for seq_print. Call order matters: update_arrange_params
// must be called before get_shrink_bedpts, or the bed shrink will be wrong.
//
// [COUPLING] progressind default implementation writes to std::cout — this
// fires on every packed item during background arrange threads.  In GUI mode
// this lambda is replaced with a wxWidgets progress update call.
//
// [CONCURRENCY] progressind and stopcondition are called from inside the
// libnest2d packing loop, which may run on TBB worker threads when
// params.parallel=true. Both lambdas must be thread-safe.
struct ArrangeParams
{
    /// The minimum distance which is allowed for any
    /// pair of items on the print bed in any direction.
    coord_t min_obj_distance = 0;

    /// The accuracy of optimization.
    /// Goes from 0.0 to 1.0 and scales performance as well
    float accuracy = 1.f;

    /// Allow parallel execution.
    bool parallel = true;

    bool allow_rotations = false;

    bool do_final_align = true;

    // BBS: add specific arrange params
    bool  allow_multi_materials_on_same_plate = true;
    bool  avoid_extrusion_cali_region         = true;
    bool  is_seq_print                        = false;
    bool  align_to_y_axis                     = false;
    float bed_shrink_x                        = 1;
    float bed_shrink_y                        = 1;
    float brim_skirt_distance                 = 0;
    float clearance_height_to_rod             = 0;
    float clearance_height_to_lid             = 0;
    float clearance_radius                    = 0;
    float object_skirt_offset                 = 0;
    float nozzle_height                       = 0;
    float printable_height                    = 256.0;
    Vec2d align_center{0.5, 0.5};

    ArrangePolygons excluded_regions;    // regions cant't be used
    ArrangePolygons nonprefered_regions; // regions can be used but not prefered

    /// Progress indicator callback called when an object gets packed.
    /// The unsigned argument is the number of items remaining to pack.
    std::function<void(unsigned, std::string)> progressind = [](unsigned st, std::string str = "") {
        std::cout << "st=" << st << ", " << str << std::endl;
    };

    std::function<void(const ArrangePolygon&)> on_packed;

    /// A predicate returning true if abort is needed.
    std::function<bool(void)> stopcondition;

    ArrangeParams() = default;
    explicit ArrangeParams(coord_t md) : min_obj_distance(md) {}
    // to json format
    std::string to_json() const
    {
        std::string ret = "{";
        ret += "\"min_obj_distance\":" + std::to_string(min_obj_distance) + ",";
        ret += "\"accuracy\":" + std::to_string(accuracy) + ",";
        ret += "\"parallel\":" + std::to_string(parallel) + ",";
        ret += "\"allow_rotations\":" + std::to_string(allow_rotations) + ",";
        ret += "\"do_final_align\":" + std::to_string(do_final_align) + ",";
        ret += "\"allow_multi_materials_on_same_plate\":" + std::to_string(allow_multi_materials_on_same_plate) + ",";
        ret += "\"avoid_extrusion_cali_region\":" + std::to_string(avoid_extrusion_cali_region) + ",";
        ret += "\"is_seq_print\":" + std::to_string(is_seq_print) + ",";
        ret += "\"bed_shrink_x\":" + std::to_string(bed_shrink_x) + ",";
        ret += "\"bed_shrink_y\":" + std::to_string(bed_shrink_y) + ",";
        ret += "\"brim_skirt_distance\":" + std::to_string(brim_skirt_distance) + ",";
        ret += "\"clearance_height_to_rod\":" + std::to_string(clearance_height_to_rod) + ",";
        ret += "\"clearance_height_to_lid\":" + std::to_string(clearance_height_to_lid) + ",";
        ret += "\"clearance_radius\":" + std::to_string(clearance_radius) + ",";
        ret += "\"printable_height\":" + std::to_string(printable_height) + ",";
        return ret;
    }
};

// [INTENT] Pre-arrange helpers — must be called in this order by callers before
// invoking arrange():
//   1. update_arrange_params()         — computes bed_shrink from skirt/clearance
//   2. update_selected_items_inflation — computes per-item inflation from brim/clearance
//   3. update_unselected_items_inflation — inflates fixed (excluded/locked) items
//   4. update_selected_items_axis_align — optionally pre-rotates to align to Y axis
//   5. get_shrink_bedpts               — returns the shrunken bed polygon for arrange()
// [HAZARD] Calling these out of order, or skipping any step, produces silently
//         wrong inflation values or incorrect bed shrink. No assertion enforces order.
void update_arrange_params(ArrangeParams& params, const DynamicPrintConfig* print_cfg, const ArrangePolygons& selected);

void update_selected_items_inflation(ArrangePolygons& selected, const DynamicPrintConfig* print_cfg, ArrangeParams& params);

void update_unselected_items_inflation(ArrangePolygons& unselected, const DynamicPrintConfig* print_cfg, const ArrangeParams& params);

void update_selected_items_axis_align(ArrangePolygons& selected, const DynamicPrintConfig* print_cfg, const ArrangeParams& params);

Points get_shrink_bedpts(const DynamicPrintConfig* print_cfg, const ArrangeParams& params);

/**
 * \brief Arranges the input polygons.
 *
 * WARNING: Currently, only convex polygons are supported by the libnest2d
 * library which is used to do the arrangement. This might change in the future
 * this is why the interface contains a general polygon capable to have holes.
 *
 * \param items Input vector of ArrangePolygons. The transformation, rotation
 * and bin_idx fields will be changed after the call finished and can be used
 * to apply the result on the input polygon.
 */
template<class TBed>
void arrange(ArrangePolygons& items, const ArrangePolygons& excludes, const TBed& bed, const ArrangeParams& params = {});

// A dispatch function that determines the bed shape from a set of points.
template<> void arrange(ArrangePolygons& items, const ArrangePolygons& excludes, const Points& bed, const ArrangeParams& params);

extern template void arrange(ArrangePolygons& items, const ArrangePolygons& excludes, const BoundingBox& bed, const ArrangeParams& params);
extern template void arrange(ArrangePolygons& items, const ArrangePolygons& excludes, const CircleBed& bed, const ArrangeParams& params);
extern template void arrange(ArrangePolygons& items, const ArrangePolygons& excludes, const Polygon& bed, const ArrangeParams& params);
extern template void arrange(ArrangePolygons& items, const ArrangePolygons& excludes, const InfiniteBed& bed, const ArrangeParams& params);

inline void arrange(ArrangePolygons& items, const Points& bed, const ArrangeParams& params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons& items, const BoundingBox& bed, const ArrangeParams& params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons& items, const CircleBed& bed, const ArrangeParams& params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons& items, const Polygon& bed, const ArrangeParams& params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons& items, const InfiniteBed& bed, const ArrangeParams& params = {}) { arrange(items, {}, bed, params); }

} // namespace arrangement
} // namespace Slic3r

#endif // MODELARRANGE_HPP
