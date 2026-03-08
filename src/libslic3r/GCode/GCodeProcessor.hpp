#ifndef slic3r_GCodeProcessor_hpp_
#define slic3r_GCodeProcessor_hpp_

// [INTENT] GCodeProcessor.hpp — the G-code post-processing and analysis pass.
//
// This header declares the data structures and public/private interface of
// GCodeProcessor: the component that reads a completed G-code file (or a live
// G-code buffer from the slicer pipeline) and produces:
//   • GCodeProcessorResult::moves — a per-move vertex array consumed by the
//     G-code viewer (GLCanvas3D) for visualization and time estimation.
//   • PrintEstimatedStatistics — Normal/Stealth time estimates, filament volumes
//     per role/extruder/color-change, flush amounts.
//   • Slice warnings and printability checks (bed area, nozzle HRC, timelapse).
//
// Processing pipeline (two modes):
//   1. Stand-alone file mode: process_file() → reads from disk via GCodeReader.
//   2. Streaming/pipelined mode: initialize() → process_buffer()* → finalize().
//      Used by GCode.cpp immediately after writing the G-code to avoid a second
//      file read.  finalize(post_process=true) calls run_post_process() which
//      rewrites the file inserting M73 remaining-time placeholders.
//
// IMPORTANT ARCHITECTURAL CONSTRAINT:
//   GCodeProcessor does NOT have access to the original Print/PrintObject during
//   stand-alone file load.  It reconstructs all state (layer, role, extruder, etc.)
//   entirely from G-code comments/tags embedded by GCode.cpp.  Any semantic
//   information not tagged in the G-code is unavailable to the viewer.
//
// [COUPLING] Key external couplings:
//   • GCodeReader (m_parser) — tokenises lines; calls process_gcode_line() callback.
//   • CommandProcessor (m_command_processor) — trie-based dispatch for G/M codes.
//   • TimeMachine (inside TimeProcessor) — trapezoidal motion planner simulation,
//     runs in TWO modes (Normal, Stealth) simultaneously.
//   • ETags enum — tag strings embedded as G-code comments by GCode.cpp; must be
//     kept in sync between GCode.cpp and GCodeProcessor.cpp.
//   • s_IsBBLPrinter static flag — selects between BBL tag set and compatible tag set.

#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/CustomGCode.hpp"

#include <cstdint>
#include <array>
#include <vector>
#include <mutex>
#include <string>
#include <string_view>
#include <optional>

namespace Slic3r {

class Print;

// [INTENT] Slice-warning sentinel strings embedded as G-code comments.
// These string literals are used as keys in SliceWarning::msg.  Any code
// that parses slice warnings (UI or scripting) must use these exact values.
// [HAZARD] H214 — these are bare #defines, not typed enums or string_view constants.
// A refactor must replicate them exactly (including underscores and casing).
#define NOZZLE_HRC_CHECKER "the_actual_nozzle_hrc_smaller_than_the_required_nozzle_hrc"
#define BED_TEMP_TOO_HIGH_THAN_FILAMENT "bed_temperature_too_high_than_filament"
#define NOT_SUPPORT_TRADITIONAL_TIMELAPSE "not_support_traditional_timelapse"
#define NOT_GENERATE_TIMELAPSE "not_generate_timelapse"
#define SMOOTH_TIMELAPSE_WITHOUT_PRIME_TOWER "smooth_timelapse_without_prime_tower"
#define LONG_RETRACTION_WHEN_CUT "activate_long_retraction_when_cut"

// [INTENT] Classifies the semantic type of each G-code move for the viewer.
// Noop is the dummy sentinel for the mandatory first entry in moves[].
// Count is used as array size; do NOT add enum values without updating
// all arrays/switches that are indexed by EMoveType.
enum class EMoveType : unsigned char {
    Noop,
    Retract,
    Unretract,
    Seam,
    Tool_change,
    Color_change,
    Pause_Print,
    Custom_GCode,
    Travel,
    Wipe,
    Extrude,
    Count
};

// [INTENT] Aggregates time estimates and filament volumes for the print summary.
// Two time modes are maintained: Normal (user-facing) and Stealth (quiet mode,
// typically 50 % slower).  Both are estimated simultaneously during G-code processing.
struct PrintEstimatedStatistics
{
    // [STATE] ETimeMode::Count == 2 — used as compile-time array size.
    // Do NOT insert modes without updating all std::array<..., Count> usages.
    enum class ETimeMode : unsigned char { Normal, Stealth, Count };

    // [STATE] Per-mode timing data.  custom_gcode_times stores (type, elapsed, remaining)
    // pairs for display in the time breakdown tooltip.
    struct Mode
    {
        float                                                              time;
        float                                                              prepare_time;
        std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> custom_gcode_times;

        void reset()
        {
            time         = 0.0f;
            prepare_time = 0.0f;
            custom_gcode_times.clear();
            custom_gcode_times.shrink_to_fit();
        }
    };

    std::vector<double>      volumes_per_color_change;
    std::map<size_t, double> model_volumes_per_extruder;
    std::map<size_t, double> wipe_tower_volumes_per_extruder;
    std::map<size_t, double> support_volumes_per_extruder;
    std::map<size_t, double> total_volumes_per_extruder;
    // BBS: the flush amount of every filament
    std::map<size_t, double>                           flush_per_filament;
    std::map<ExtrusionRole, std::pair<double, double>> used_filaments_per_role;

    std::array<Mode, static_cast<size_t>(ETimeMode::Count)> modes;
    unsigned int                                            total_filament_changes;
    unsigned int                                            total_extruder_changes;

    PrintEstimatedStatistics() { reset(); }

    // [HAZARD] H215 — reset() iterates modes by value (copies), so m.reset() has
    // no effect on the actual array elements. This is a pre-existing upstream bug;
    // time/prepare_time/custom_gcode_times in modes[] are NOT cleared on reset().
    // Only the vectors below are actually cleared.
    void reset()
    {
        for (auto m : modes) {
            m.reset();
        }
        volumes_per_color_change.clear();
        volumes_per_color_change.shrink_to_fit();
        wipe_tower_volumes_per_extruder.clear();
        model_volumes_per_extruder.clear();
        support_volumes_per_extruder.clear();
        total_volumes_per_extruder.clear();
        flush_per_filament.clear();
        used_filaments_per_role.clear();
        total_filament_changes = 0;
        total_extruder_changes = 0;
    }
};

// [INTENT] Represents one detected toolpath-collision conflict (two objects whose
// paths intersect at the same Z height).  _obj1 == nullptr means wipe tower.
// [HAZARD] H216 — _obj1/_obj2 are raw void pointers into PrintObject instances.
// They are valid only within the slicer session that produced them; serialising
// ConflictResult across sessions or file loads produces dangling pointers.
struct ConflictResult
{
    std::string _objName1;
    std::string _objName2;
    double      _height;
    const void* _obj1; // nullptr means wipe tower
    const void* _obj2;
    int         layer = -1;
    ConflictResult(const std::string& objName1, const std::string& objName2, double height, const void* obj1, const void* obj2)
        : _objName1(objName1), _objName2(objName2), _height(height), _obj1(obj1), _obj2(obj2)
    {}
    ConflictResult() = default;
};

using ConflictResultOpt = std::optional<ConflictResult>;

// [INTENT] Bitfield result of multi-extruder printable-area / printable-height checks.
// error_code bit layout (from comment):
//   bit 0 (0x01): multi-extruder printable area error
//   bit 1 (0x02): multi-extruder printable height error
//   bit 2 (0x04): plate printable area error
//   bit 3 (0x08): plate printable height error
//   bit 4 (0x10): wrapping detection area error
// [HAZARD] H217 — error_code is an int, not a strongly-typed enum or bitset.
// Consumers must know the bit layout from comments; adding new check types requires
// coordinated changes across all consumers.
struct GCodeCheckResult
{
    int error_code = 0; // 0 means succeed, 0b 0001 multi extruder printable area error, 0b 0010 multi extruder printable height error,
    // 0b 0100 plate printable area error, 0b 1000 plate printable height error, 0b 10000 wrapping detection area error
    std::map<int, std::vector<std::pair<int, int>>>
        print_area_error_infos; // printable_area  extruder_id to <filament_id - object_label_id> which cannot printed in this extruder
    std::map<int, std::vector<std::pair<int, int>>>
         print_height_error_infos; // printable_height extruder_id to <filament_id - object_label_id> which cannot printed in this extruder
    void reset()
    {
        error_code = 0;
        print_area_error_infos.clear();
        print_height_error_infos.clear();
    }
};

// [INTENT] Identifies which filaments cannot be printed on a given plate due to
// type incompatibility.  Used to generate a user-facing warning.
struct FilamentPrintableResult
{
    std::vector<int> conflict_filament;
    std::string      plate_name;
    FilamentPrintableResult() {};
    FilamentPrintableResult(std::vector<int>& conflict_filament, std::string plate_name)
        : conflict_filament(conflict_filament), plate_name(plate_name)
    {}
    bool has_value() { return !conflict_filament.empty(); };
};

// [INTENT] The primary output of GCodeProcessor — consumed by the G-code viewer,
// time estimate display, and slice-validation UI.
// [MEMORY] moves[] can be very large (millions of entries for complex prints).
// The viewer loads this entirely into GPU vertex buffers; peak RSS can spike to
// several hundred MB during loading.  Any refactor must preserve the flat array
// layout — random access by index is required for viewport picking.
struct GCodeProcessorResult
{
    // [INTENT] Hash function for layer_filaments map key (vector of filament IDs).
    // Uses a bit-set representation: bit N is set if filament N is used.
    // [HAZARD] H218 — hash collides whenever more than 64 filaments are used
    // (bit shift wraps on 64-bit uint64_t).  Printers with > 64 filament slots
    // will silently produce incorrect hash values and map collisions.
    struct FilamentSequenceHash
    {
        uint64_t operator()(const std::vector<unsigned int>& layer_filament) const
        {
            uint64_t key = 0;
            for (auto& f : layer_filament)
                key |= (uint64_t(1) << f);
            return key;
        }
    };
    ConflictResultOpt conflict_result;
    GCodeCheckResult  gcode_check_result;
    // [HAZARD] H219 — typo: "filament_printable_reuslt" (missing 'e' in "result").
    // Pre-existing upstream typo.  The copy-assignment operator below copies this
    // field; any serialisation layer that uses the field name by string will carry
    // the typo.
    FilamentPrintableResult filament_printable_reuslt;
    float                   initial_layer_time;

    // [STATE] IDs from the print settings that generated this G-code, embedded
    // in the file header and parsed back on load for configuration reconciliation.
    struct SettingsIds
    {
        std::string              print;
        std::vector<std::string> filament;
        std::string              printer;

        void reset()
        {
            print.clear();
            filament.clear();
            printer.clear();
        }
    };

    // [INTENT] Per-move record for the G-code viewer.  One entry per G1/G2/G3
    // command plus synthetic entries for tool-changes, color-changes, pauses.
    // [STATE] moves[0] is always a dummy Noop vertex (initialized by
    // initialize_result_moves()).  Viewer code must skip index 0.
    // [MEMORY] At ~80 bytes per vertex, a 1M-move print uses ~80 MB for moves[].
    // [CONCURRENCY] result_mutex protects the whole GCodeProcessorResult from
    // concurrent reads (viewer) and writes (background slice).
    struct MoveVertex
    {
        unsigned int  gcode_id{0};
        EMoveType     type{EMoveType::Noop};
        ExtrusionRole extrusion_role{erNone};
        unsigned char extruder_id{0};
        unsigned char cp_color_id{0};
        Vec3f         position{Vec3f::Zero()}; // mm
        float         delta_extruder{0.0f};    // mm
        float         feedrate{0.0f};          // mm/s
        float         actual_feedrate{0.0f};   // mm/s
        float         width{0.0f};             // mm
        float         height{0.0f};            // mm
        float         mm3_per_mm{0.0f};
        float         travel_dist{0.0f}; // mm
        float         fan_speed{0.0f};   // percentage
        float         temperature{0.0f}; // Celsius degrees
                                         // ORCA: Add Pressure Advance visualization support
        float                                                                              pressure_advance{0.0f};
        std::array<float, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> time{0.0f, 0.0f};     // s
        float                                                                              layer_duration{0.0f}; // s
        unsigned int                                                                       layer_id{0};
        bool                                                                               internal_only{false};

        // BBS
        int   object_label_id{-1};
        float print_z{0.0f};

        float volumetric_rate() const { return feedrate * mm3_per_mm; }
        float actual_volumetric_rate() const { return actual_feedrate * mm3_per_mm; }
    };

    // [INTENT] User-visible warning entry produced during G-code analysis.
    // level: 0 = tip, 1 = warning, 2 = error.
    // msg: one of the #define sentinel strings above.
    // params: optional extra info (e.g. filament name, layer number).
    struct SliceWarning
    {
        int                      level;      // 0: normal tips, 1: warning; 2: error
        std::string              msg;        // enum string
        std::string              error_code; // error code for studio
        std::vector<std::string> params;     // extra msg info
    };

    std::string             filename;
    unsigned int            id;
    std::vector<MoveVertex> moves;
    // Positions of ends of lines of the final G-code this->filename after TimeProcessor::post_process() finalizes the G-code.
    std::vector<size_t> lines_ends;
    Pointfs             printable_area;
    // BBS: add bed exclude area
    Pointfs              bed_exclude_area;
    Pointfs              wrapping_exclude_area;
    std::vector<Pointfs> extruder_areas;
    std::vector<double>  extruder_heights;
    // BBS: add toolpath_outside
    bool toolpath_outside;
    // BBS: add object_label_enabled
    bool label_object_enabled;
    // BBS : extra retraction when change filament,experiment func
    bool                           long_retraction_when_cut{0};
    int                            timelapse_warning_code{0};
    bool                           support_traditional_timelapse{true};
    float                          printable_height;
    float                          z_offset;
    SettingsIds                    settings_ids;
    size_t                         filaments_count;
    bool                           backtrace_enabled;
    std::vector<std::string>       extruder_colors;
    std::vector<float>             filament_diameters;
    std::vector<int>               required_nozzle_HRC;
    std::vector<float>             filament_densities;
    std::vector<float>             filament_costs;
    std::vector<int>               filament_vitrification_temperature;
    std::vector<int>               filament_maps;
    std::vector<int>               limit_filament_maps;
    PrintEstimatedStatistics       print_statistics;
    std::vector<CustomGCode::Item> custom_gcode_per_print_z;
    bool                           spiral_vase_mode;
    // BBS
    std::vector<SliceWarning> warnings;
    int                       nozzle_hrc;
    std::vector<NozzleType>   nozzle_type;
    // first key stores filaments, second keys stores the layer ranges(enclosed) that use the filaments
    std::unordered_map<std::vector<unsigned int>, std::vector<std::pair<int, int>>, FilamentSequenceHash> layer_filaments;
    // first key stores `from` filament, second keys stores the `to` filament
    std::map<std::pair<int, int>, int> filament_change_count_map;

    BedType bed_type = BedType::btCount;
    void    reset();

    // [CONCURRENCY] result_mutex protects concurrent read (G-code viewer thread)
    // and write (slicer/post-process thread).  The copy-assignment operator does
    // NOT lock — callers must hold the mutex before copying.
    // [HAZARD] H220 — result_mutex is mutable and lock()/unlock() are const — so
    // any const accessor on GCodeProcessorResult is silently un-threadsafe.
    // The viewer calls get_result() which returns a const ref and then reads
    // unlocked.  A refactor should use std::shared_mutex + RAII guards throughout.
    mutable std::mutex result_mutex;
    // [HAZARD] H221 — custom copy-assignment does NOT copy: backtrace_enabled,
    // extruder_areas, extruder_heights, nozzle_hrc, nozzle_type, required_nozzle_HRC,
    // filament_vitrification_temperature, z_offset, support_traditional_timelapse,
    // wrapping_exclude_area, filament_maps, id, lines_ends, settings_ids.filament.
    // These fields will be stale/zero after any copy-assignment.  Use of this
    // operator must be audited before adding new fields to GCodeProcessorResult.
    GCodeProcessorResult& operator=(const GCodeProcessorResult& other)
    {
        filename                  = other.filename;
        id                        = other.id;
        moves                     = other.moves;
        lines_ends                = other.lines_ends;
        printable_area            = other.printable_area;
        bed_exclude_area          = other.bed_exclude_area;
        wrapping_exclude_area     = other.wrapping_exclude_area;
        toolpath_outside          = other.toolpath_outside;
        label_object_enabled      = other.label_object_enabled;
        long_retraction_when_cut  = other.long_retraction_when_cut;
        timelapse_warning_code    = other.timelapse_warning_code;
        printable_height          = other.printable_height;
        settings_ids              = other.settings_ids;
        filaments_count           = other.filaments_count;
        extruder_colors           = other.extruder_colors;
        filament_diameters        = other.filament_diameters;
        filament_densities        = other.filament_densities;
        filament_costs            = other.filament_costs;
        print_statistics          = other.print_statistics;
        custom_gcode_per_print_z  = other.custom_gcode_per_print_z;
        spiral_vase_mode          = other.spiral_vase_mode;
        warnings                  = other.warnings;
        bed_type                  = other.bed_type;
        gcode_check_result        = other.gcode_check_result;
        limit_filament_maps       = other.limit_filament_maps;
        filament_printable_reuslt = other.filament_printable_reuslt;
        layer_filaments           = other.layer_filaments;
        filament_change_count_map = other.filament_change_count_map;
        initial_layer_time        = other.initial_layer_time;
#if ENABLE_GCODE_VIEWER_STATISTICS
        time = other.time;
#endif
        return *this;
    }
    void lock() const { result_mutex.lock(); }
    void unlock() const { result_mutex.unlock(); }
};

// [INTENT] Trie-based G-code command dispatcher.
// register_command() inserts a string into the trie with an associated handler
// function.  process_command() walks the trie character-by-character and invokes
// the handler on the first match.
// [STATE] early_quit=true on a node means the handler fires as soon as the prefix
// is matched, without consuming the rest of the command string — used for
// single-letter commands like T (tool change).
// [COUPLING] All G-code handlers (process_G1, process_M106, etc.) are registered
// in GCodeProcessor::register_commands() which is called from the constructor.
class CommandProcessor
{
public:
    using command_handler_t = std::function<void(const GCodeReader::GCodeLine& line)>;

private:
    struct TrieNode
    {
        command_handler_t                                   handler{nullptr};
        std::unordered_map<char, std::unique_ptr<TrieNode>> children;
        bool                                                early_quit{false}; // stop matching, trigger handle imediately
    };

public:
    CommandProcessor();
    void register_command(const std::string& str, command_handler_t handler, bool early_quit = false);
    bool process_comand(std::string_view cmd, const GCodeReader::GCodeLine& line);

private:
    std::unique_ptr<TrieNode> root;
};

// [INTENT] The core G-code analysis engine.  Parses G-code line by line,
// simulates printer kinematics (trapezoidal motion), accumulates per-move
// metadata, and produces GCodeProcessorResult.
//
// [STATE] All private member fields represent the "current printer state" as
// understood from the G-code parsed so far:
//   m_start_position / m_end_position — XYZЕ coordinates (absolute mm or relative)
//   m_units — G20 (inches) or G21 (mm)
//   m_global_positioning_type / m_e_local_positioning_type — absolute/relative
//   m_extruder_id — currently active extruder (0-based)
//   m_extrusion_role — role of current extrusion move (set from comment tags)
//   m_feedrate — current programmed feedrate in mm/s
//   m_width / m_height — current extrusion width/height in mm (from tags)
//   m_wiping / m_flushing / m_virtual_flushing / m_wipe_tower — section flags
//
// [CONCURRENCY] GCodeProcessor is NOT thread-safe in its internal state.
// Only the final GCodeProcessorResult (behind result_mutex) is shared across
// threads.  The processor itself must be used from a single thread.
class GCodeProcessor
{
    static const std::vector<std::string> Reserved_Tags;
    static const std::vector<std::string> Reserved_Tags_compatible;
    static const std::string              Flush_Start_Tag;
    static const std::string              Flush_End_Tag;
    static const std::string              VFlush_Start_Tag;
    static const std::string              VFlush_End_Tag;
    static const std::string              External_Purge_Tag;

public:
    // [INTENT] Embedded comment tags written by GCode.cpp and parsed by
    // GCodeProcessor.  The integer values are indices into Reserved_Tags[] and
    // Reserved_Tags_compatible[].  The two arrays MUST have the same length and
    // same semantic ordering.
    // [HAZARD] H222 — ETags values are used as raw array indices via
    // static_cast<unsigned char>(tag).  Adding, removing, or reordering enum
    // values without updating BOTH tag arrays simultaneously will silently read
    // the wrong tag string.
    enum class ETags : unsigned char {
        Role,
        Wipe_Start,
        Wipe_End,
        Height,
        Width,
        Layer_Change,
        Color_Change,
        Pause_Print,
        Custom_Code,
        First_Line_M73_Placeholder,
        Last_Line_M73_Placeholder,
        Estimated_Printing_Time_Placeholder,
        Total_Layer_Number_Placeholder,
        Manual_Tool_Change,
        During_Print_Exhaust_Fan,
        Wipe_Tower_Start,
        Wipe_Tower_End,
        PA_Change,
        Print_Time_Sec_Placeholder,
        Used_Filament_Length_Placeholder,
    };

    // [STATE] s_IsBBLPrinter selects between BBL-native tag strings and the
    // PrusaSlicer-compatible tag strings.  Set at startup based on printer config.
    // [HAZARD] H223 — s_IsBBLPrinter is a mutable static — shared state across
    // all GCodeProcessor instances and across threads.  Not protected by a mutex.
    static const std::string& reserved_tag(ETags tag)
    {
        return s_IsBBLPrinter ? Reserved_Tags[static_cast<unsigned char>(tag)] : Reserved_Tags_compatible[static_cast<unsigned char>(tag)];
    }
    // checks the given gcode for reserved tags and returns true when finding the 1st (which is returned into found_tag)
    static bool contains_reserved_tag(const std::string& gcode, std::string& found_tag);
    // checks the given gcode for reserved tags and returns true when finding any
    // (the first max_count found tags are returned into found_tag)
    static bool contains_reserved_tags(const std::string& gcode, unsigned int max_count, std::vector<std::string>& found_tag);

    static int  get_gcode_last_filament(const std::string& gcode_str);
    static bool get_last_z_from_gcode(const std::string& gcode_str, double& z);
    static bool get_last_position_from_gcode(const std::string& gcode_str, Vec3f& pos);

    static const float Wipe_Width;
    static const float Wipe_Height;

    static bool s_IsBBLPrinter;

private:
    // [STATE] XYZE axis coordinate array (indices 0=X, 1=Y, 2=Z, 3=E).
    // Stored as double to accumulate small increments without float precision loss.
    using AxisCoords     = std::array<double, 4>;
    using ExtruderColors = std::vector<unsigned char>;
    using ExtruderTemps  = std::vector<float>;

    enum class EUnits : unsigned char { Millimeters, Inches };

    enum class EPositioningType : unsigned char { Absolute, Relative };

    // [STATE] Saved position for G-codes that need to restore position (M401/M402).
    struct CachedPosition
    {
        AxisCoords position; // mm
        float      feedrate; // mm/s

        void reset();
    };

    // [STATE] Color-change index tracking.  counter counts all color changes;
    // current is the currently displayed color segment index.
    struct CpColor
    {
        unsigned char counter;
        unsigned char current;

        void reset();
    };

public:
    // [INTENT] Trapezoidal motion profile for one move segment.
    // Stores the entry/cruise/exit feedrates after junction-velocity smoothing.
    struct FeedrateProfile
    {
        float entry{0.0f};  // mm/s
        float cruise{0.0f}; // mm/s
        float exit{0.0f};   // mm/s
    };

    // [INTENT] Trapezoidal velocity profile geometry for one move.
    // accelerate_until / decelerate_after are distances from move start (mm).
    // cruise_feedrate is the plateau speed after full acceleration.
    // [COUPLING] calculate_trapezoid() uses MachineEnvelopeConfig acceleration
    // limits from TimeMachine; the result drives the time() calculation.
    struct Trapezoid
    {
        float accelerate_until{0.0f}; // mm
        float decelerate_after{0.0f}; // mm
        float cruise_feedrate{0.0f};  // mm/sec

        float acceleration_time(float entry_feedrate, float acceleration) const;
        float cruise_time() const { return (cruise_feedrate != 0.0f) ? cruise_distance() / cruise_feedrate : 0.0f; }
        float deceleration_time(float distance, float acceleration) const;
        float acceleration_distance() const { return accelerate_until; }
        float cruise_distance() const { return decelerate_after - accelerate_until; }
        float deceleration_distance(float distance) const { return distance - decelerate_after; }
        bool  is_cruise_only(float distance) const { return std::abs(cruise_distance() - distance) < EPSILON; }
    };

    // [INTENT] A single unit of motion planning work — one G1/G2/G3 move plus
    // its computed trapezoidal velocity profile.  Blocks are accumulated in a
    // queue and re-planned (backward-pass junction smoothing) when the queue
    // reaches Planner::refresh_threshold.
    // [STATE] flags.recalculate — this block needs re-planning on next flush.
    //        flags.nominal_length — block is long enough that entry speed = exit speed.
    //        flags.prepare_stage — block belongs to start-gcode prepare phase.
    struct TimeBlock
    {
        struct Flags
        {
            bool recalculate{false};
            bool nominal_length{false};
            bool prepare_stage{false};
        };

        EMoveType       move_type{EMoveType::Noop};
        ExtrusionRole   role{erNone};
        unsigned int    move_id{0};
        unsigned int    g1_line_id{0};
        unsigned int    remaining_internal_g1_lines{0};
        unsigned int    layer_id{0};
        float           distance{0.0f};        // mm
        float           acceleration{0.0f};    // mm/s^2
        float           max_entry_speed{0.0f}; // mm/s
        float           safe_feedrate{0.0f};   // mm/s
        Flags           flags;
        FeedrateProfile feedrate_profile;
        Trapezoid       trapezoid;

        // Calculates this block's trapezoid
        void calculate_trapezoid();

        float time() const
        {
            return trapezoid.acceleration_time(feedrate_profile.entry, acceleration) + trapezoid.cruise_time() +
                   trapezoid.deceleration_time(distance, acceleration);
        }
    };

private:
    friend class ExportLines;
    // [INTENT] Simulates firmware motion planner for one time mode (Normal or Stealth).
    // Maintains a queue of TimeBlocks and re-plans junction velocities in batches.
    // calculate_time() flushes the queue into the GCodeProcessorResult moves[].
    //
    // [STATE] Two TimeMachine instances run simultaneously in TimeProcessor::machines[].
    // They are driven by the same sequence of G-code moves but with different
    // machine limits (Normal vs Stealth).
    //
    // [COUPLING] actual_speed_moves: a parallel list of actual (post-planning)
    // feedrate overrides that is later applied to the moves[] array.  This is an
    // Orca extension for accurate visualization of actual print speed.
    struct TimeMachine
    {
        // [STATE] Per-move kinematic state (feedrates, directions).
        // For arc moves (G2/G3), enter_direction != exit_direction.
        struct State
        {
            float feedrate;      // mm/s
            float safe_feedrate; // mm/s
            // BBS: feedrate of X-Y-Z-E axis. But when the move is G2 and G3, X-Y will be
            // same value which means feedrate in X-Y plane.
            AxisCoords axis_feedrate;     // mm/s
            AxisCoords abs_axis_feedrate; // mm/s

            // BBS: unit vector of enter speed and exit speed in x-y-z space.
            // For line move, there are same. For arc move, there are different.
            Vec3f enter_direction;
            Vec3f exit_direction;

            void reset();
        };

        // [STATE] Accumulates elapsed time at custom G-code events
        // (color-change, pause, etc.) for the time breakdown display.
        struct CustomGCodeTime
        {
            bool                                             needed;
            float                                            cache;
            std::vector<std::pair<CustomGCode::Type, float>> times;

            void reset();
        };

        // [STATE] Cache of (g1_line_id, elapsed_time) for M73 remaining-time
        // injection during post-processing (run_post_process()).
        struct G1LinesCacheItem
        {
            unsigned int id;
            unsigned int remaining_internal_g1_lines{0};
            float        elapsed_time;
        };

        // [STATE] Records the actual (after junction re-planning) feedrate for
        // each move, along with optional overrides for other fields.  Used to
        // back-fill actual_feedrate into MoveVertex after all blocks are planned.
        struct ActualSpeedMove
        {
            unsigned int         move_id{0};
            std::optional<Vec3f> position;
            float                actual_feedrate{0.0f};
            std::optional<float> delta_extruder;
            std::optional<float> feedrate;
            std::optional<float> width;
            std::optional<float> height;
            std::optional<float> mm3_per_mm;
            std::optional<float> fan_speed;
            std::optional<float> temperature;
        };

        bool  enabled;
        float acceleration; // mm/s^2
        // hard limit for the acceleration, to which the firmware will clamp.
        float max_acceleration;     // mm/s^2
        float retract_acceleration; // mm/s^2
        // hard limit for the acceleration, to which the firmware will clamp.
        float max_retract_acceleration; // mm/s^2
        float travel_acceleration;      // mm/s^2
        // hard limit for the travel acceleration, to which the firmware will clamp.
        float max_travel_acceleration; // mm/s^2
        float extrude_factor_override_percentage;
        // We accumulate total print time in doubles to reduce the loss of precision
        // while adding big floating numbers with small float numbers.
        double time; // s
        struct StopTime
        {
            unsigned int g1_line_id;
            float        elapsed_time;
        };
        std::vector<StopTime>         stop_times;
        std::string                   line_m73_main_mask;
        std::string                   line_m73_stop_mask;
        State                         curr;
        State                         prev;
        CustomGCodeTime               gcode_time;
        std::vector<TimeBlock>        blocks;
        std::vector<G1LinesCacheItem> g1_times_cache;
        float                         first_layer_time;
        std::vector<ActualSpeedMove>  actual_speed_moves;
        // BBS: prepare stage time before print model, including start gcode time and mostly same with start gcode time
        float prepare_time;

        void reset();

        void calculate_time(GCodeProcessorResult&               result,
                            PrintEstimatedStatistics::ETimeMode mode,
                            size_t                              keep_last_n_blocks = 0,
                            float                               additional_time    = 0.0f);
    };

    // [INTENT] Tracks extruded filament volumes broken down by role, extruder,
    // color-change segment, and flush type.  Uses "cache + flush" pattern:
    // each role/type accumulates into a cache scalar; process_*_cache() flushes
    // the cache into the per-extruder/per-role maps when a segment boundary is crossed.
    // [COUPLING] process_caches() is called from GCodeProcessor after each
    // tool-change, color-change, or end-of-file.
    struct UsedFilaments // filaments per ColorChange
    {
        double              color_change_cache;
        std::vector<double> volumes_per_color_change;

        double                   model_extrude_cache;
        std::map<size_t, double> model_volumes_per_filament;

        double                   wipe_tower_cache;
        std::map<size_t, double> wipe_tower_volumes_per_filament;

        double                   support_volume_cache;
        std::map<size_t, double> support_volumes_per_filament;

        // BBS: the flush amount of every filament
        std::map<size_t, double> flush_per_filament;

        double                   total_volume_cache;
        std::map<size_t, double> total_volumes_per_filament;

        double                                             role_cache;
        std::map<ExtrusionRole, std::pair<double, double>> filaments_per_role;

        void reset();

        void increase_support_caches(double extruded_volume);
        void increase_model_caches(double extruded_volume);
        void increase_wipe_tower_caches(double extruded_volume);

        void process_color_change_cache();
        void process_model_cache(GCodeProcessor* processor);
        void process_wipe_tower_cache(GCodeProcessor* processor);
        void process_support_cache(GCodeProcessor* processor);
        void process_total_volume_cache(GCodeProcessor* processor);

        void update_flush_per_filament(size_t extrude_id, float flush_length);
        void process_role_cache(GCodeProcessor* processor);
        void process_caches(GCodeProcessor* processor);

        friend class GCodeProcessor;
    };

    // [INTENT] Wraps the two TimeMachine instances and common planner parameters.
    // Planner::queue_size (64) and Planner::refresh_threshold (256) mirror the
    // firmware's planning queue — larger queue = more lookahead = better junction
    // velocity optimization.
    // [HAZARD] H224 — queue_size=64 is hardcoded.  Modern 32-bit Klipper firmware
    // uses much larger look-ahead queues (sometimes 512+ moves).  For accurate
    // time estimation on Klipper printers the queue_size should be configurable.
    struct TimeProcessor
    {
        struct Planner
        {
            // Size of the firmware planner queue. The old 8-bit Marlins usually just managed 16 trapezoidal blocks.
            // Let's be conservative and plan for newer boards with more memory.
            static constexpr size_t queue_size = 64;
            // The firmware recalculates last planner_queue_size trapezoidal blocks each time a new block is added.
            // We are not simulating the firmware exactly, we calculate a sequence of blocks once a reasonable number of blocks accumulate.
            static constexpr size_t refresh_threshold = queue_size * 4;
        };

        // extruder_id is currently used to correctly calculate filament load / unload times into the total print time.
        // This is currently only really used by the MK3 MMU2:
        // extruder_unloaded = true means no filament is loaded yet, all the filaments are parked in the MK3 MMU2 unit.
        bool extruder_unloaded;
        // allow to skip the lines M201/M203/M204/M205 generated by GCode::print_machine_envelope() for non-Normal time estimate mode
        bool                  machine_envelope_processing_enabled;
        MachineEnvelopeConfig machine_limits;
        // Additional load / unload times for a filament exchange sequence.
        float filament_load_times;
        float filament_unload_times;
        // Orca:  time for tool change
        float machine_tool_change_time;

        std::array<TimeMachine, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> machines;

        void reset();
    };

public:
    // [INTENT] Detects the current seam vertex position during wipe moves.
    // Activated when a Wipe_Start tag is encountered; deactivated on Wipe_End.
    // The first vertex during an active wipe is recorded as the seam position.
    class SeamsDetector
    {
        bool                 m_active{false};
        std::optional<Vec3f> m_first_vertex;

    public:
        void activate(bool active)
        {
            if (m_active != active) {
                m_active = active;
                if (m_active)
                    m_first_vertex.reset();
            }
        }

        std::optional<Vec3f> get_first_vertex() const { return m_first_vertex; }
        void                 set_first_vertex(const Vec3f& vertex) { m_first_vertex = vertex; }

        bool is_active() const { return m_active; }
        bool has_first_vertex() const { return m_first_vertex.has_value(); }
    };

    // [INTENT] Fixes the Z coordinate of Color_Change / Pause_Print / Custom_GCode
    // move vertices.  These events are recorded at the Z of the preceding move, but
    // the actual layer height is only known when the NEXT Height tag is parsed.
    // update(height) back-patches the stored move vertex with the correct height.
    // [HAZARD] H225 — OptionsZCorrector::update() uses moves.emplace_back() then
    // erase(begin+id) — O(N) on the moves vector.  For very large prints this
    // produces repeated linear scans.  The pattern relies on index stability of
    // std::vector which is guaranteed, but the O(N) cost is a latent perf hazard.
    class OptionsZCorrector
    {
        GCodeProcessorResult& m_result;
        std::optional<size_t> m_move_id;
        std::optional<size_t> m_custom_gcode_per_print_z_id;

    public:
        explicit OptionsZCorrector(GCodeProcessorResult& result) : m_result(result) {}

        void set()
        {
            m_move_id                     = m_result.moves.size() - 1;
            m_custom_gcode_per_print_z_id = m_result.custom_gcode_per_print_z.size() - 1;
        }

        void update(float height)
        {
            if (!m_move_id.has_value() || !m_custom_gcode_per_print_z_id.has_value())
                return;

            const Vec3f position = m_result.moves.back().position;

            GCodeProcessorResult::MoveVertex& move = m_result.moves.emplace_back(m_result.moves[*m_move_id]);
            move.position                          = position;
            move.height                            = height;
            m_result.moves.erase(m_result.moves.begin() + *m_move_id);
            m_result.custom_gcode_per_print_z[*m_custom_gcode_per_print_z_id].print_z = position.z();
            reset();
        }

        void reset()
        {
            m_move_id.reset();
            m_custom_gcode_per_print_z_id.reset();
        }
    };

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    // [INTENT] Debug-only: validates that the actual extrusion width/height/mm3_per_mm
    // values computed during simulation are within threshold% of the tagged values
    // embedded by GCode.cpp.  Accumulates errors for offline analysis.
    // Not compiled in release builds.
    struct DataChecker
    {
        struct Error
        {
            float         value;
            float         tag_value;
            ExtrusionRole role;
        };

        std::string        type;
        float              threshold{0.01f};
        float              last_tag_value{0.0f};
        unsigned int       count{0};
        std::vector<Error> errors;

        DataChecker(const std::string& type, float threshold) : type(type), threshold(threshold) {}

        void update(float value, ExtrusionRole role)
        {
            if (role != erCustom) {
                ++count;
                if (last_tag_value != 0.0f) {
                    if (std::abs(value - last_tag_value) / last_tag_value > threshold)
                        errors.push_back({value, last_tag_value, role});
                }
            }
        }

        void reset()
        {
            last_tag_value = 0.0f;
            errors.clear();
            count = 0;
        }

        std::pair<float, float> get_min() const
        {
            float delta_min = FLT_MAX;
            float perc_min  = 0.0f;
            for (const Error& e : errors) {
                if (delta_min > e.value - e.tag_value) {
                    delta_min = e.value - e.tag_value;
                    perc_min  = 100.0f * delta_min / e.tag_value;
                }
            }
            return {delta_min, perc_min};
        }

        std::pair<float, float> get_max() const
        {
            float delta_max = -FLT_MAX;
            float perc_max  = 0.0f;
            for (const Error& e : errors) {
                if (delta_max < e.value - e.tag_value) {
                    delta_max = e.value - e.tag_value;
                    perc_max  = 100.0f * delta_max / e.tag_value;
                }
            }
            return {delta_max, perc_max};
        }

        void output() const
        {
            if (!errors.empty()) {
                std::cout << type << ":\n";
                std::cout << "Errors: " << errors.size() << " (" << 100.0f * float(errors.size()) / float(count) << "%)\n";
                auto [min, perc_min] = get_min();
                auto [max, perc_max] = get_max();
                std::cout << "min: " << min << "(" << perc_min << "%) - max: " << max << "(" << perc_max << "%)\n";
            }
        }
    };
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

private:
    // [STATE] Active processor state fields — represent the "current printer state"
    // reconstructed purely from G-code comments and motion commands.
    CommandProcessor   m_command_processor;
    GCodeReader        m_parser;
    EUnits             m_units;
    EPositioningType   m_global_positioning_type;
    EPositioningType   m_e_local_positioning_type;
    std::vector<Vec3f> m_extruder_offsets;
    GCodeFlavor        m_flavor;
    std::vector<float> m_nozzle_volume;
    AxisCoords         m_start_position; // mm
    AxisCoords         m_end_position;   // mm
    AxisCoords         m_origin;         // mm
    CachedPosition     m_cached_position;
    bool               m_wiping;
    bool               m_flushing;         // mark a section with real flush
    bool               m_virtual_flushing; // mark a section with virtual flush, only for statistics
    bool               m_wipe_tower;
    int                m_object_label_id{-1};
    float              m_print_z{0.0f};
    std::vector<float> m_remaining_volume;
    ExtruderTemps      m_filament_nozzle_temp;
    ExtruderTemps      m_filament_nozzle_temp_first_layer;
    std::vector<int>   m_physical_extruder_map;
    bool               m_manual_filament_change;

    // BBS: x, y offset for gcode generated
    double m_x_offset{0};
    double m_y_offset{0};

    unsigned int m_line_id;
    unsigned int m_last_line_id;
    float        m_feedrate;      // mm/s
    float        m_width;         // mm
    float        m_height;        // mm
    float        m_forced_width;  // mm
    float        m_forced_height; // mm
    float        m_mm3_per_mm;
    float        m_travel_dist; // mm
    float        m_fan_speed;   // percentage
    float        m_z_offset;    // mm
                                // ORCA: Add Pressure Advance visualization support
    float                      m_pressure_advance;
    ExtrusionRole              m_extrusion_role;
    std::vector<int>           m_filament_maps;
    std::vector<unsigned char> m_last_filament_id;
    std::vector<unsigned char> m_filament_id;
    unsigned char              m_extruder_id;
    ExtruderColors             m_extruder_colors;
    ExtruderTemps              m_extruder_temps;
    bool                       m_is_XL_printer = false;
    int                        m_highest_bed_temp;
    float                      m_extruded_last_z;
    float                      m_first_layer_height; // mm
    float                      m_zero_layer_height;  // mm
    bool                       m_processing_start_custom_gcode;
    unsigned int               m_g1_line_id;
    unsigned int               m_layer_id;
    CpColor                    m_cp_color;
    SeamsDetector              m_seams_detector;
    OptionsZCorrector          m_options_z_corrector;
    size_t                     m_last_default_color_id;
    bool                       m_detect_layer_based_on_tag{false};
    int                        m_seams_count;
    bool                       m_measure_g29_time{false};
    bool                       m_single_extruder_multi_material;
    float                      m_preheat_time;
    int                        m_preheat_steps;
    bool                       m_disable_m73;

    // [INTENT] Identifies the slicer that produced the G-code being processed.
    // Used to select the correct tag-parsing path (process_bambuslicer_tags,
    // process_cura_tags, etc.).
    // [STATE] Detected during the first pass through comments via detect_producer().
    enum class EProducer { Unknown, OrcaSlicer, Slic3rPE, Slic3r, SuperSlicer, Cura, Simplify3D, CraftWare, ideaMaker, KissSlicer };

    static const std::vector<std::pair<GCodeProcessor::EProducer, std::string>> Producers;
    EProducer                                                                   m_producer;

    TimeProcessor m_time_processor;
    UsedFilaments m_used_filaments;

    // [STATE] Raw pointer to the owning Print — only valid during pipelined
    // (non-file-load) processing.  Null during stand-alone G-code file load.
    // [HAZARD] H226 — m_print is a raw non-owning pointer.  If the Print object
    // is destroyed before GCodeProcessor finishes (e.g. cancel race), accessing
    // m_print is a use-after-free.
    Print* m_print{nullptr};

    GCodeProcessorResult m_result;
    // [STATE] Global monotonically increasing result ID.  Incremented on each
    // reset()/initialize() call so the viewer can detect stale results.
    static unsigned int s_result_id;

public:
    GCodeProcessor();
    void init_filament_maps_and_nozzle_type_when_import_only_gcode();
    // check whether the gcode path meets the filament_map grouping requirements
    bool check_multi_extruder_gcode_valid(const int                         extruder_size,
                                          const Pointfs                     plate_printable_area,
                                          const double                      plate_printable_height,
                                          const Pointfs                     wrapping_exclude_area,
                                          const std::vector<Polygons>&      unprintable_areas,
                                          const std::vector<double>&        printable_heights,
                                          const std::vector<int>&           filament_map,
                                          const std::vector<std::set<int>>& unprintable_filament_types);
    void apply_config(const PrintConfig& config);
    void set_print(Print* print) { m_print = print; }

    DynamicConfig export_config_for_render() const;

    void enable_stealth_time_estimator(bool enabled);
    bool is_stealth_time_estimator_enabled() const
    {
        return m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].enabled;
    }
    void enable_machine_envelope_processing(bool enabled) { m_time_processor.machine_envelope_processing_enabled = enabled; }
    void reset();

    const GCodeProcessorResult& get_result() const { return m_result; }
    GCodeProcessorResult&       result() { return m_result; }
    GCodeProcessorResult&&      extract_result() { return std::move(m_result); }

    // Load a G-code into a stand-alone G-code viewer.
    // throws CanceledException through print->throw_if_canceled() (sent by the caller as callback).
    void process_file(const std::string& filename, std::function<void()> cancel_callback = nullptr);

    // Streaming interface, for processing G-codes just generated by PrusaSlicer in a pipelined fashion.
    void initialize(const std::string& filename);
    // [STATE] Must be called exactly once before any moves are stored.
    // Inserts the mandatory Noop sentinel at moves[0].
    void initialize_result_moves()
    {
        // 1st move must be a dummy move
        assert(m_result.moves.empty());
        m_result.moves.emplace_back(GCodeProcessorResult::MoveVertex());
    }
    void process_buffer(const std::string& buffer);
    void finalize(bool post_process);

    float                                                              get_time(PrintEstimatedStatistics::ETimeMode mode) const;
    float                                                              get_prepare_time(PrintEstimatedStatistics::ETimeMode mode) const;
    std::string                                                        get_time_dhm(PrintEstimatedStatistics::ETimeMode mode) const;
    std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> get_custom_gcode_times(PrintEstimatedStatistics::ETimeMode mode,
                                                                                              bool include_remaining) const;

    float get_first_layer_time(PrintEstimatedStatistics::ETimeMode mode) const;

    // BBS: set offset for gcode writer
    void set_xy_offset(double x, double y)
    {
        m_x_offset = x;
        m_y_offset = y;
    }

    // Orca: if true, only change new layer if ETags::Layer_Change occurs
    // otherwise when we got a lift of z during extrusion, a new layer will be added
    void detect_layer_based_on_tag(bool enabled) { m_detect_layer_based_on_tag = enabled; }

private:
    void register_commands();
    void apply_config(const DynamicPrintConfig& config);
    void apply_config_simplify3d(const std::string& filename);
    void apply_config_superslicer(const std::string& filename);
    void process_gcode_line(const GCodeReader::GCodeLine& line, bool producers_enabled);

    // [INTENT] process_tags() dispatches comment lines to the appropriate
    // producer-specific tag parser.  Each parser returns true on match so
    // process_tags() can short-circuit.  Order of parser calls is fixed:
    // producers → bambuslicer → cura → simplify3d → craftware → ideamaker → kissslicer.
    // [HAZARD] H227 — tag dispatch is linear search through comment strings.
    // For large G-code files with many comment lines this can be a hot path.
    void process_tags(const std::string_view comment, bool producers_enabled);
    bool process_producers_tags(const std::string_view comment);
    bool process_bambuslicer_tags(const std::string_view comment);
    bool process_cura_tags(const std::string_view comment);
    bool process_simplify3d_tags(const std::string_view comment);
    bool process_craftware_tags(const std::string_view comment);
    bool process_ideamaker_tags(const std::string_view comment);
    bool process_kissslicer_tags(const std::string_view comment);

    bool detect_producer(const std::string_view comment);

    // Move
    void process_G0(const GCodeReader::GCodeLine& line);
    // [INTENT] process_G1 is the most-called handler — every extrusion move.
    // The overload with axes/feedrate arrays is used by process_G2_G3 which
    // discretizes arcs into line segments and calls process_G1 for each.
    void process_G1(const GCodeReader::GCodeLine& line, const std::optional<unsigned int>& remaining_internal_g1_lines = std::nullopt);
    enum class G1DiscretizationOrigin {
        G1,
        G2G3,
    };
    void process_G1(const std::array<std::optional<double>, 4>& axes     = {std::nullopt, std::nullopt, std::nullopt, std::nullopt},
                    const std::optional<double>&                feedrate = std::nullopt,
                    G1DiscretizationOrigin                      origin   = G1DiscretizationOrigin::G1,
                    const std::optional<unsigned int>&          remaining_internal_g1_lines = std::nullopt);

    // Arc Move
    void process_G2_G3(const GCodeReader::GCodeLine& line, bool clockwise);

    // [INTENT] VG1 = "virtual G1": a synthetic move injected by BBS firmware tags
    // to represent moves that don't appear as explicit G1 lines in the G-code.
    void process_VG1(const GCodeReader::GCodeLine& line);

    // BBS: handle delay command
    void process_G4(const GCodeReader::GCodeLine& line);

    // Retract
    void process_G10(const GCodeReader::GCodeLine& line);

    // Unretract
    void process_G11(const GCodeReader::GCodeLine& line);

    // Set Units to Inches
    void process_G20(const GCodeReader::GCodeLine& line);

    // Set Units to Millimeters
    void process_G21(const GCodeReader::GCodeLine& line);

    // Firmware controlled Retract
    void process_G22(const GCodeReader::GCodeLine& line);

    // Firmware controlled Unretract
    void process_G23(const GCodeReader::GCodeLine& line);

    // Move to origin
    void process_G28(const GCodeReader::GCodeLine& line);

    // BBS
    void process_G29(const GCodeReader::GCodeLine& line);

    // Set to Absolute Positioning
    void process_G90(const GCodeReader::GCodeLine& line);

    // Set to Relative Positioning
    void process_G91(const GCodeReader::GCodeLine& line);

    // Set Position
    void process_G92(const GCodeReader::GCodeLine& line);

    // Sleep or Conditional stop
    void process_M1(const GCodeReader::GCodeLine& line);

    // Set extruder to absolute mode
    void process_M82(const GCodeReader::GCodeLine& line);

    // Set extruder to relative mode
    void process_M83(const GCodeReader::GCodeLine& line);

    // Set extruder temperature
    void process_M104(const GCodeReader::GCodeLine& line);

    // Process virtual command of M104, in order to help gcodeviewer work
    void process_VM104(const GCodeReader::GCodeLine& line);

    // Process virtual command of M109, in order to help gcodeviewer work
    void process_VM109(const GCodeReader::GCodeLine& line);

    // Set fan speed
    void process_M106(const GCodeReader::GCodeLine& line);

    // Disable fan
    void process_M107(const GCodeReader::GCodeLine& line);

    // ORCA: Add Pressure Advance visualization support
    // Set pressure advance
    void process_M900(const GCodeReader::GCodeLine& line);
    void process_M572(const GCodeReader::GCodeLine& line);
    void process_SET_PRESSURE_ADVANCE(const GCodeReader::GCodeLine& line);

    // Set tool (Sailfish)
    void process_M108(const GCodeReader::GCodeLine& line);

    // Set extruder temperature and wait
    void process_M109(const GCodeReader::GCodeLine& line);

    // Recall stored home offsets
    void process_M132(const GCodeReader::GCodeLine& line);

    // Set tool (MakerWare)
    void process_M135(const GCodeReader::GCodeLine& line);

    // BBS: Set bed temperature
    void process_M140(const GCodeReader::GCodeLine& line);

    // BBS: wait bed temperature
    void process_M190(const GCodeReader::GCodeLine& line);

    // BBS: wait chamber temperature
    void process_M191(const GCodeReader::GCodeLine& line);

    // Set max printing acceleration
    void process_M201(const GCodeReader::GCodeLine& line);

    // Set maximum feedrate
    void process_M203(const GCodeReader::GCodeLine& line);

    // Set default acceleration
    void process_M204(const GCodeReader::GCodeLine& line);

    // Advanced settings
    void process_M205(const GCodeReader::GCodeLine& line);

    // Klipper SET_VELOCITY_LIMIT
    void process_SET_VELOCITY_LIMIT(const GCodeReader::GCodeLine& line);

    // Set extrude factor override percentage
    void process_M221(const GCodeReader::GCodeLine& line);

    // BBS: handle delay command. M400 is defined by BBL only
    void process_M400(const GCodeReader::GCodeLine& line);

    // Repetier: Store x, y and z position
    void process_M401(const GCodeReader::GCodeLine& line);

    // Repetier: Go to stored position
    void process_M402(const GCodeReader::GCodeLine& line);

    // Set allowable instantaneous speed change
    void process_M566(const GCodeReader::GCodeLine& line);

    // Unload the current filament into the MK3 MMU2 unit at the end of print.
    void process_M702(const GCodeReader::GCodeLine& line);

    void process_SYNC(const GCodeReader::GCodeLine& line);

    // Processes T line (Select Tool)
    void process_T(const GCodeReader::GCodeLine& line);
    void process_T(const std::string_view command);
    void process_M1020(const GCodeReader::GCodeLine& line);

    void process_M622(const GCodeReader::GCodeLine& line);
    void process_M623(const GCodeReader::GCodeLine& line);

    void process_filament_change(int id);

    // post process the file with the given filename to:
    // 1) add remaining time lines M73 and update moves' gcode ids accordingly
    // 2) update used filament data
    void run_post_process();

    // BBS: different path_type is only used for arc move
    void store_move_vertex(EMoveType type, EMovePathType path_type = EMovePathType::Noop_move, bool internal_only = false);

    void set_extrusion_role(ExtrusionRole role);

    float minimum_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const;
    float minimum_travel_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const;
    // Machine limit arrays are indexed by time mode only: [0]=Normal, [1]=Stealth.
    // Do NOT add an extruder_id parameter — OrcaSlicer does not use BambuStudio's
    // per-nozzle machine limits (filament_map_2 / get_config_idx_for_filament).
    float get_axis_max_feedrate(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
    float get_axis_max_acceleration(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
    float get_axis_max_jerk_with_jd(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
    float get_axis_max_jerk(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
    Vec3f get_xyz_max_jerk(PrintEstimatedStatistics::ETimeMode mode) const;
    float get_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
    void  set_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
    float get_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
    void  set_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
    float get_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
    void  set_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
    float get_filament_load_time(size_t extruder_id);
    float get_filament_unload_time(size_t extruder_id);
    float get_extruder_change_time(size_t extruder_id);
    int   get_filament_vitrification_temperature(size_t extrude_id);
    void  process_custom_gcode_time(CustomGCode::Type code);
    void  process_filaments(CustomGCode::Type code);

    void calculate_time(GCodeProcessorResult& result, size_t keep_last_n_blocks = 0, float additional_time = 0.0f);

    // Simulates firmware st_synchronize() call
    void simulate_st_synchronize(float additional_time = 0.0f);

    void update_estimated_times_stats();

    double extract_absolute_position_on_axis(Axis axis, const GCodeReader::GCodeLine& line, double area_filament_cross_section);

    // BBS:
    void update_slice_warnings();

    // get current used filament
    int get_filament_id(bool force_initialize = true) const;
    // get last used filament in the same extruder with current filament
    int get_last_filament_id(bool force_initialize = true) const;
    // get current used extruder
    int get_extruder_id(bool force_initialize = true) const;
};

} /* namespace Slic3r */

#endif /* slic3r_GCodeProcessor_hpp_ */
