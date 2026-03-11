// [INTENT] TimelapsePosPicker chooses a safe parking point for "take photo now" moves. The goal
// is to move the nozzle somewhere camera-visible while avoiding collisions with printed geometry,
// the gantry rod envelope, and per-extruder printable-area restrictions.
//
// [STATE] init() snapshots printer geometry and config-derived safety envelopes; pick_pos() then
// answers per-layer queries using the cached bed polygon, extruder printable areas, and optional
// all-layer memoized answer.
//
// [MEMORY] The picker owns only cached polygons and a BoundingBox cache keyed by PrintInstance*.
// Print / Layer / PrintObject pointers passed in are borrowed for the lifetime of one export.
//
// [COUPLING] Pulls data from Print, PrintObject, Layer, and Clipper-based polygon ops. It also
// knows Bambu-specific notions like camera keep-out regions and rod collision limits.
//
// [HAZARD] Coordinates intentionally mix scaled polygon space and unscaled config space; helpers
// must preserve the documented convention or the chosen parking point will drift.
#ifndef TIMELAPSE_POS_PICKER_HPP
#define TIMELAPSE_POS_PICKER_HPP

#include <vector>
#include "libslic3r/Point.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

    const Point DefaultTimelapsePos = Point(0, 0);
    const Point DefaultCameraPos = Point(0, 0);

    class Layer;
    class Print;

    struct PosPickCtx
    {
        Point curr_pos;
        const Layer* curr_layer;
        int picture_extruder_id; // the extruder id to take picture
        int curr_extruder_id;
        // [STATE] Only populated in sequential "by object" printing, where already-finished
        // objects change both collision and camera-visibility constraints.
        std::optional<std::vector<const PrintObject*>> printed_objects; // printed objects, only have value in by object mode
    };

    // [STATE] Cached geometry is stored without plate offset unless a field comment says scaled.
    class TimelapsePosPicker
    {
    public:
        TimelapsePosPicker() = default;
        ~TimelapsePosPicker() = default;

        Point pick_pos(const PosPickCtx& ctx);
        void init(const Print* print, const Point& plate_offset);
        void reset();
    private:
        void construct_printable_area_by_printer();

        Point pick_pos_for_curr_layer(const PosPickCtx& ctx);
        Point pick_pos_for_all_layer(const PosPickCtx& ctx);

        ExPolygons collect_object_slices_data(const Layer* curr_layer, float height_range, const std::vector<const PrintObject*>& object_list,bool by_object);
        Polygons collect_limit_areas_for_camera(const std::vector<const PrintObject*>& object_list);

        Polygons collect_limit_areas_for_rod(const std::vector<const PrintObject*>& object_list, const PosPickCtx& ctx);

        Polygon       expand_object_projection(const Polygon &poly, bool by_object, bool higher_than_curr = true);
        BoundingBoxf3 expand_object_bbox(const BoundingBoxf3& bbox, bool by_object);

        Point pick_nearest_object_center(const Point& curr_pos, const std::vector<const PrintObject*>& object_list);
        Point get_objects_center(const std::vector<const PrintObject*>& object_list);

        Polygon get_limit_area_for_camera(const PrintObject* obj);
        std::vector<const PrintObject*> get_object_list(const std::optional<std::vector<const PrintObject*>>& printed_objects);

        double get_raft_height(const PrintObject* obj);
        BoundingBoxf3 get_real_instance_bbox(const PrintInstance& instance);
        Point get_object_center(const PrintObject* obj);
    private:
        const Print* print{ nullptr };
        std::vector<ExPolygons> m_extruder_printable_area; //scaled data
        Polygon m_bed_polygon; //scaled_data
        Point m_plate_offset; // unscaled data
        int m_plate_height; // unscaled data
        int m_plate_width; // unscaled data

        PrintSequence m_print_seq;
        bool m_based_on_all_layer;
        int m_nozzle_height_to_rod;
        int m_nozzle_clearance_radius;
        std::optional<int> m_liftable_extruder_id;
        std::optional<int> m_extruder_height_gap;

        // [MEMORY] bbox_cache avoids recomputing transformed instance bounds on every layer query.
        std::unordered_map<const PrintInstance*, BoundingBoxf3> bbox_cache;

        // [STATE] Smooth timelapse mode reuses one globally safe point for the whole print.
        std::optional<Point> m_all_layer_pos;
    };
}

#endif