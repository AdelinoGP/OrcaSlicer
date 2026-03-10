// [INTENT] FanMover.hpp declares the `BufferData` struct and `FanMover` class used to
// delay fan speed-up commands backward in the G-code time stream. The public API is a
// single `process_gcode()` entry point; all state is encapsulated in the class.
//
// [STATE] `FanMover` is a stateful, single-use post-processor. It is constructed once
// per print with fixed configuration (delay, kickstart, etc.) and then called repeatedly
// with successive G-code chunks. State accumulates across calls via `m_buffer`.
//
// [MEMORY] The buffer (`m_buffer`) is a `std::list<BufferData>`. std::list was chosen
// for O(1) insert/erase at arbitrary positions (needed by `_put_in_middle_G1`), but
// at the cost of poor cache locality. For prints with a large delay window this list
// can grow to thousands of nodes.
//
// [COUPLING] Holds a const reference to `GCodeWriter` for flavour-specific fan command
// generation. The writer must outlive the `FanMover` instance. If the writer is
// destroyed first, UB results (dangling reference).

#ifndef slic3r_GCode_FanMover_hpp_
#define slic3r_GCode_FanMover_hpp_

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../ExtrusionEntity.hpp"

#include "../Point.hpp"
#include "../GCodeReader.hpp"
#include "../GCodeWriter.hpp"
#include <regex>

namespace Slic3r {

// [INTENT] Value type representing a single G-code line plus its motion metadata.
// Stored in `FanMover::m_buffer` to track buffered-but-not-yet-emitted lines.
//
// [STATE] Fields:
//   `raw`         — the original G-code text (may be mutated by `_put_in_middle_G1` /
//                   `_print_in_middle_G1` when a move is split)
//   `time`        — estimated seconds this move takes (0 for non-motion lines)
//   `fan_speed`   — -1 if not a fan command; 0-100 if a fan command (percentage)
//   `is_kickstart`— true if this is an M106 S255 kickstart pulse (not a final target)
//   `x,y,z,e`     — absolute position BEFORE this move (start position)
//   `dx,dy,dz,de` — position deltas for this move (used to rewrite split moves)
//
// [HAZARD] The constructor pops the trailing '\n' from `line` only if it was passed
// in as part of the string — but the pop operates on the local parameter `line`, NOT
// on `raw` (which was already stored via the initializer list). This means `raw` always
// retains the '\n' if the caller passed one. The pop is effectively a dead operation.
// When `raw` is emitted, callers add "\n" unconditionally — leading to double newlines
// if the original line had a trailing '\n'. This is a latent bug.
//
// [HAZARD] `float is_kickstart` in the constructor signature — the parameter type is
// `float` but `is_kickstart` member is `bool`. This is a type mismatch that compiles
// without error (implicit conversion). A caller accidentally passing 0.5f would set
// `is_kickstart = true` (non-zero float → true), which is likely correct, but the
// signature is misleading and should be `bool`.
class BufferData
{
public:
    std::string raw;
    float       time;
    int16_t     fan_speed;
    bool        is_kickstart;
    float       x = 0, y = 0, z = 0, e = 0;
    float       dx = 0, dy = 0, dz = 0, de = 0;
    BufferData(std::string line, float time = 0, int16_t fan_speed = 0, float is_kickstart = false)
        : raw(line), time(time), fan_speed(fan_speed), is_kickstart(is_kickstart)
    {
        // avoid double \n
        if (!line.empty() && line.back() == '\n')
            line.pop_back();
    }
};

// [INTENT] Post-processor that rewrites G-code to shift fan speed-up commands backward
// in time. Constructed once per print; `process_gcode()` is called chunk-by-chunk.
// The final call must set `flush=true` to drain any remaining buffered lines.
//
// [STATE] Configuration fields are all `const` after construction — the post-processor
// is not reconfigurable mid-print. This simplifies reasoning about state transitions.
//
// [COUPLING] `m_writer` (const reference to GCodeWriter) provides:
//   - `config.gcode_flavor` — for fan command format selection
//   - `GCodeWriter::set_fan()` — for generating M106/M107 strings
// If GCodeWriter's config API changes, FanMover must be updated.
//
// [HAZARD] `regex_fan_speed` is constructed but never used in FanMover.cpp — the actual
// fan speed parsing is done by `get_fan_speed()` using `get_axis_value()`. The regex
// member is dead code and wastes ~40 bytes per instance plus regex compilation time.
class FanMover
{
private:
    // [STATE] Dead member: `regex_fan_speed` was likely used in an earlier implementation
    // but `get_fan_speed()` + `get_axis_value()` replaced it. Never referenced in .cpp.
    const std::regex regex_fan_speed;

    // [STATE] Configuration (all const after construction):
    //   `nb_seconds_delay` — how far back in time to push fan speed-up commands.
    //                        Clamped to max(0.01, requested) to avoid division issues.
    //   `with_D_option`    — [UNCLEAR → RESOLVED] constructor flag stored but never read; `GCode` passes it in, yet `FanMover` treats it as a current no-op.
    //   `relative_e`       — whether the G-code uses relative extrusion (affects E-split)
    //   `only_overhangs`   — if true, delays only applied during overhang perimeter extrusion
    //   `kickstart`        — duration (seconds per 100% speed) for M106 S255 pulse before
    //                        stepping down to target speed. 0 = no kickstart.
    const float nb_seconds_delay;
    const bool  with_D_option;
    const bool  relative_e;
    const bool  only_overhangs;
    const float kickstart;

    // [STATE] GCodeReader used to parse incoming G-code. Maintains parser state (current
    // position, last seen F, mode flags) across calls. Configured from writer.config in ctor.
    GCodeReader m_parser{};

    // [COUPLING] Const reference — FanMover does not own GCodeWriter; caller must ensure
    // the writer outlives this FanMover instance.
    const GCodeWriter& m_writer;

    // [STATE] "Back of buffer" state — values at the newest (most recently parsed) end:
    //   `current_role`       — extrusion type from last `;TYPE:` comment
    //   `m_current_speed`    — last feedrate seen (mm/s, converted from mm/min)
    //   `m_is_custom_gcode`  — whether parser is inside a `;custom gcode` block
    //   `m_currrent_extruder`— active extruder index (note: typo "currrent" in original)
    //
    // [HAZARD] `m_current_speed` initializes to 1000/60 ≈ 16.67 mm/s. If the first
    // G1 move in the file has no F parameter, this default is used. 1000mm/min is a
    // reasonable travel speed but may significantly underestimate or overestimate time
    // for the first move depending on the actual printer speed.
    ExtrusionRole current_role = ExtrusionRole::erCustom;
    // in unit/second
    double   m_current_speed     = 1000 / 60.0;
    bool     m_is_custom_gcode   = false;
    uint16_t m_currrent_extruder = 0;

    // [STATE] "Front of buffer" tracking — values at the oldest (about-to-be-emitted) end:
    //   `m_front_buffer_fan_speed` — the fan speed that has already been emitted to output.
    //                                Used to suppress re-emitting unchanged fan commands.
    //   `m_back_buffer_fan_speed`  — the fan speed reflected by the most recently parsed M106.
    //                                Used to detect speed increase vs decrease.
    //
    // [HAZARD] Both start at 0 (fan off). If the print starts with the fan already running
    // (from a prior print or manual command), these will be out of sync with reality until
    // the first M106 is encountered. No sync mechanism exists.
    // variable for when you add a line (front of the buffer)
    int m_front_buffer_fan_speed = 0;
    int m_back_buffer_fan_speed  = 0;

    // [STATE] `m_current_kickstart` tracks a pending kickstart event that hasn't yet been
    // placed in the buffer (because there wasn't enough buffer depth at the time).
    //   `.time` — remaining countdown seconds; -1 = no active kickstart
    //   `.fan_speed` — target speed to emit when countdown expires
    //   `.raw` — the M106 line to emit at that time
    //
    // [HAZARD] Initialized with time=-1 (inactive). The -1 sentinel is a float, so
    // comparing `m_current_kickstart.time > 0` to check activity is correct, but
    // floating point equality checks on the sentinel could be fragile if time is
    // decremented to exactly 0.0 rather than going negative.
    BufferData m_current_kickstart{"", -1, 0};

    // buffer
    //  [STATE] Main delay buffer. Entries are in chronological order: front = oldest
    //  (next to be emitted), back = newest (just parsed). The total time represented
    //  is tracked in `m_buffer_time_size` for O(1) queries.
    std::list<BufferData> m_buffer;
    double                m_buffer_time_size = 0;

    // The output of process_layer()
    std::string m_process_output;

public:
    // [INTENT] Constructor. All configuration is immutable after construction.
    // `nb_seconds_delay` is clamped to max(0.01, requested) to avoid zero-delay edge
    // cases in the buffer drain condition.
    //
    // [HAZARD] `with_D_option` is stored but never read in the .cpp implementation.
    // It may be a planned feature (D parameter on M106 for some firmware?) that was
    // never implemented. Passing any value has no effect.
    FanMover(const GCodeWriter& writer,
             const float        nb_seconds_delay,
             const bool         with_D_option,
             const bool         relative_e,
             const bool         only_overhangs,
             const float        kickstart)
        : regex_fan_speed("S[0-9]+")
        , nb_seconds_delay(nb_seconds_delay > 0 ? std::max(0.01f, nb_seconds_delay) : 0)
        , with_D_option(with_D_option)
        , relative_e(relative_e)
        , only_overhangs(only_overhangs)
        , kickstart(kickstart)
        , m_writer(writer)
    {
        m_parser.apply_config(writer.config);
    }

    // Adds the gcode contained in the given string to the analysis and returns it after removing the workcodes
    const std::string& process_gcode(const std::string& gcode, bool flush);

private:
    // [INTENT] Append a new entry to the back of the buffer and update `m_buffer_time_size`.
    // Returns a reference to the newly inserted element for immediate field population.
    //
    // [HAZARD] `data` is taken by rvalue reference but passed as lvalue to `emplace_back`.
    // The `data` object is copied into the list, not moved. This is a missed optimization
    // — `emplace_back(std::move(data))` would avoid a string copy.
    BufferData& put_in_buffer(BufferData&& data)
    {
        m_buffer_time_size += data.time;
        m_buffer.emplace_back(data);
        return m_buffer.back();
    }

    // [INTENT] Erase an entry from the buffer and update `m_buffer_time_size`.
    // Returns the iterator to the next element (standard erase behaviour).
    std::list<BufferData>::iterator remove_from_buffer(std::list<BufferData>::iterator data)
    {
        m_buffer_time_size -= data->time;
        return m_buffer.erase(data);
    }
    // Processes the given gcode line
    void        _process_gcode_line(GCodeReader& reader, const GCodeReader::GCodeLine& line);
    void        _process_T(const std::string_view command);
    void        _put_in_middle_G1(std::list<BufferData>::iterator item_to_split, float nb_sec, BufferData&& line_to_write);
    void        _print_in_middle_G1(BufferData& line_to_split, float nb_sec, const std::string& line_to_write);
    void        _remove_slow_fan(int16_t min_speed, float past_sec);
    std::string _set_fan(int16_t speed);
};

} // namespace Slic3r

#endif /* slic3r_GCode_FanMover_hpp_ */
