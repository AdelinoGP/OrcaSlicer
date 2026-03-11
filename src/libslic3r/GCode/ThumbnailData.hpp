#ifndef slic3r_ThumbnailData_hpp_
#define slic3r_ThumbnailData_hpp_

#include <vector>
#include "libslic3r/Point.hpp"
#include "nlohmann/json.hpp"

namespace Slic3r {

// [INTENT] ThumbnailData.hpp defines the shared payload handed from the 3D preview renderer to
// the G-code/plate metadata exporters. The same RGBA buffer is reused by PNG/JPG/QOI/firmware-
// specific encoders, so this header is the contract that keeps render-time image generation
// decoupled from output-time compression.
//
// [STATE] The mutable fields here are serialization-facing state, not rendering policy. Width,
// height, and pixels are filled by the thumbnail callback; THUMBNAIL_SIZE / ThumbnailsParams carry
// the caller's requested sizes and scene-filtering flags into that callback.
//
// [MEMORY] ThumbnailData owns its pixel vector by value. Downstream compressors in
// GCode/Thumbnails.cpp borrow this storage transiently and allocate their own encoded buffers, so
// no encoded image keeps a pointer into pixels after compression finishes.
//
// [COUPLING] This header couples G-code export, preview rendering, and plate-metadata JSON:
//   - Preview/UI code produces ThumbnailsList via ThumbnailsGeneratorCallback.
//   - GCode/Thumbnails.cpp consumes ThumbnailData for comment-block embedding.
//   - bbs_3mf / plate metadata serialization reuse BBoxData and PlateBBoxData.
//
// [HAZARD] THUMBNAIL_SIZE is a mutable namespace-scope vector, not a constexpr default. Any code
// that mutates it changes the process-wide default thumbnail request for later exports.

// BBS: thumbnail_size in gcode file
static std::vector<Vec2d> THUMBNAIL_SIZE = {Vec2d(50, 50)};

struct ThumbnailData
{
    unsigned int               width;
    unsigned int               height;
    std::vector<unsigned char> pixels;

    ThumbnailData() { reset(); }
    void set(unsigned int w, unsigned int h);
    void reset();

    bool is_valid() const;
    void load_from(ThumbnailData& data)
    {
        this->set(data.width, data.height);
        pixels = data.pixels;
    }
};

// BBS: add plate id into thumbnail render logic
using ThumbnailsList = std::vector<ThumbnailData>;

struct ThumbnailsParams
{
    const Vec2ds sizes;
    bool         printable_only;
    bool         parts_only;
    bool         show_bed;
    bool         transparent_background;
    int          plate_id;
    bool         use_plate_box{true};
};

typedef std::function<ThumbnailsList(const ThumbnailsParams&)> ThumbnailsGeneratorCallback;

struct BBoxData
{
    // [STATE] id/name link the first-layer bounding box back to a logical object in the plate UI,
    // while bbox/area/layer_height snapshot the geometry metrics later embedded into metadata.
    int                   id;   // object id
    std::vector<coordf_t> bbox; // first layer bounding box: min.{x,y}, max.{x,y}
    float                 area; // first layer area
    float                 layer_height;
    std::string           name;
    void                  to_json(nlohmann::json& j) const
    {
        j = nlohmann::json{{"id", id}, {"bbox", bbox}, {"area", area}, {"layer_height", layer_height}, {"name", name}};
    }
    void from_json(const nlohmann::json& j)
    {
        j.at("id").get_to(id);
        j.at("bbox").get_to(bbox);
        j.at("area").get_to(area);
        j.at("layer_height").get_to(layer_height);
        j.at("name").get_to(name);
    }
};

struct PlateBBoxData
{
    // [INTENT] PlateBBoxData serializes the first-layer spatial summary that Bambu/Orca viewers
    // consume without reparsing the full 3D model: whole-plate bounds, per-object bounds, and
    // filament/bed context needed for preview overlays.
    std::vector<coordf_t>    bbox_all;     // total bounding box of all objects including brim
    std::vector<BBoxData>    bbox_objs;    // BBoxData of seperate object
    std::vector<int>         filament_ids; // filament id used in curr plate
    std::vector<std::string> filament_colors;
    bool                     is_seq_print    = false;
    int                      first_extruder  = 0;
    float                    nozzle_diameter = 0.4;
    std::string              bed_type;
    float                    first_layer_time;
    // version 1: use view type ColorPrint (filament color)
    // version 2: use view type FilamentId (filament id)
    int version = 2;

    void to_json(nlohmann::json& j) const
    {
        j                     = nlohmann::json{{"bbox_all", bbox_all}};
        j["filament_ids"]     = filament_ids;
        j["filament_colors"]  = filament_colors;
        j["is_seq_print"]     = is_seq_print;
        j["first_extruder"]   = first_extruder;
        j["nozzle_diameter"]  = nozzle_diameter;
        j["version"]          = version;
        j["bed_type"]         = bed_type;
        j["first_layer_time"] = first_layer_time;
        for (const auto& bbox : bbox_objs) {
            nlohmann::json j_bbox;
            bbox.to_json(j_bbox);
            j["bbox_objects"].push_back(j_bbox);
        }
    }
    void from_json(const nlohmann::json& j)
    {
        j.at("bbox_all").get_to(bbox_all);
        j.at("filament_ids").get_to(filament_ids);
        j.at("filament_colors").get_to(filament_colors);
        j.at("is_seq_print").get_to(is_seq_print);
        j.at("first_extruder").get_to(first_extruder);
        j.at("nozzle_diameter").get_to(nozzle_diameter);
        j.at("version").get_to(version);
        j.at("bed_type").get_to(bed_type);
        for (auto& bbox_j : j.at("bbox_objects")) {
            BBoxData bbox_data;
            bbox_data.from_json(bbox_j);
            bbox_objs.push_back(bbox_data);
        }
    }
    bool is_valid() const
    {
        // [HAZARD] Validity currently means "has per-object boxes" only. Callers must not assume
        // optional fields such as filament_colors or first_layer_time were populated unless the
        // producer guarantees the matching schema version.
        return !bbox_objs.empty();
    }
};

} // namespace Slic3r

#endif // slic3r_ThumbnailData_hpp_
