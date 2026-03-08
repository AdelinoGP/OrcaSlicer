// [INTENT] FanMover is a G-code post-processor that delays fan speed-up commands backward
// in time by `nb_seconds_delay` seconds. This ensures that the fan is running at full
// speed when the printer physically reaches the cooling-critical region (e.g., overhang),
// compensating for fan spin-up latency.
//
// [STATE] Maintains a sliding time-window buffer (`m_buffer`, a std::list<BufferData>)
// of not-yet-emitted G-code lines. Lines are held back until the cumulative motion time
// in the buffer exceeds `nb_seconds_delay`, at which point they are flushed to output.
//
// [MEMORY] Two separate fan-speed tracking variables exist:
//   - `m_front_buffer_fan_speed`: fan speed at the output (front/oldest) end of the buffer
//   - `m_back_buffer_fan_speed`: fan speed at the input (back/newest) end of the buffer
// Both must stay consistent with the buffer contents or fan commands will duplicate/vanish.
//
// [CONCURRENCY] FanMover is not thread-safe. It is called sequentially per-layer during
// post-processing. No shared state is guarded by mutexes.
//
// [COUPLING] Delegates G-code flavour logic to GCodeWriter::set_fan() and GCodeReader
// for parsing. Role detection depends on `;TYPE:` comment injection by the upstream
// G-code generator — if that contract changes, only_overhangs mode breaks silently.
//
// [HAZARD] The entire post-processor operates on raw G-code strings. Any upstream change
// to G-code formatting (e.g. removing the leading space before axis letters) breaks
// `get_axis_value()` and `change_axis_value()` silently.

#include "FanMover.hpp"

#include "GCodeReader.hpp"

#include <iomanip>
/*
#include <memory.h>
#include <string.h>
#include <float.h>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../Utils.hpp"
#include "Print.hpp"

#include <boost/log/trivial.hpp>
*/

namespace Slic3r {

// [INTENT] Entry point: parses a chunk of G-code text and returns the processed output.
// If `flush` is true, drains the entire buffer unconditionally (used at end-of-print).
//
// [STATE] `m_process_output` is cleared at the start of every call — callers must
// consume the returned reference before calling again or the previous content is lost.
//
// [MEMORY] `m_buffer_time_size` is recomputed from scratch at the start of each call
// (lines 27-28) to recover from accumulated floating-point rounding drift. This is O(n)
// in buffer size on every call — a small but non-trivial overhead for long prints.
//
// [HAZARD] The returned `const std::string&` is a reference to the member
// `m_process_output`. It is invalidated by the next call to `process_gcode()`.
// Callers that store the reference rather than copying the string will see corruption.
const std::string& FanMover::process_gcode(const std::string& gcode, bool flush)
{
    m_process_output = "";

    // recompute buffer time to recover from rounding
    m_buffer_time_size = 0;
    for (auto& data : m_buffer)
        m_buffer_time_size += data.time;

    if (!gcode.empty())
        m_parser.parse_buffer(gcode,
                              [this](GCodeReader& reader, const GCodeReader::GCodeLine& line) { /*m_process_output += line.raw() + "\n";*/
                                                                                                this->_process_gcode_line(reader, line);
                              });

    if (flush) {
        while (!m_buffer.empty()) {
            m_process_output += m_buffer.front().raw + "\n";
            remove_from_buffer(m_buffer.begin());
        }
    }

    return m_process_output;
}

// [INTENT] Helper: returns true if `c` is a whitespace or null character — used to
// detect the end of a G-code token when scanning axis values.
bool is_end_of_word(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0; }

// [INTENT] Parses the numeric value of a given axis letter (e.g. 'X', 'E') from a
// raw G-code line string. Returns NAN if the axis is not present.
//
// [HAZARD] The search pattern is `" X"` (space + letter). If the axis parameter appears
// at the very start of the string (no leading space), the find() call returns npos and
// NAN is returned, silently ignoring the value. This is unlikely in practice but fragile.
//
// [HAZARD] Uses `strtod` with a raw `c_str()` pointer and manual offset arithmetic.
// If `pos` arithmetic overflows or the string is shorter than expected, undefined behaviour
// results (though the errno/pend checks provide some guard).
float get_axis_value(const std::string& line, char axis)
{
    char match[3] = " X";
    match[1]      = axis;

    size_t pos = line.find(match);
    if (pos == std::string::npos) {
        return NAN;
    }
    pos += 2;
    // size_t end = std::min(line.find(' ', pos + 1), line.find(';', pos + 1));
    //  Try to parse the numeric value.
    const char* c    = line.c_str();
    char*       pend = nullptr;
    errno            = 0;
    double v         = strtod(c + pos, &pend);
    if (pend != nullptr && errno == 0 && pend != c) {
        // The axis value has been parsed correctly.
        return float(v);
    }
    return NAN;
}

// [INTENT] Rewrites the numeric value of a given axis letter in a raw G-code string
// in-place, formatting to `decimal_digits` decimal places.
//
// [HAZARD] CRITICAL: If the axis letter is not found, `line.find(match)` returns npos
// (SIZE_MAX). Adding 2 to npos wraps to 1 on 64-bit (SIZE_MAX + 2 = 1 mod 2^64),
// so `pos` becomes 1. `line.replace(1, end - 1, ...)` then silently overwrites the
// beginning of the string with the numeric value, corrupting G-code. No assertion or
// error is raised. Callers must guarantee the axis is present before calling this.
//
// [HAZARD] `end` is computed as min(find(' '), find(';')), both starting from pos+1.
// If neither space nor semicolon follows the value (e.g. value is at end of string),
// both finds return npos and `std::min(npos, npos)` = npos, making `end - pos` an
// enormous number, causing `replace` to delete most of the string.
void change_axis_value(std::string& line, char axis, const float new_value, const int decimal_digits)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimal_digits) << new_value;

    char match[3] = " X";
    match[1]      = axis;

    size_t pos = line.find(match) + 2;
    size_t end = std::min(line.find(' ', pos + 1), line.find(';', pos + 1));
    line       = line.replace(pos, end - pos, ss.str());
}

// [INTENT] Parses the fan speed value from a raw G-code line. Returns the S-parameter
// value (0-255) as int16_t, 0 for M107/M127 (fan off), or -1 for lines that are not
// fan commands.
//
// [STATE] Handles multi-firmware fan command semantics:
//   - Mach3/Machinekit: M106 P-parameter is the speed
//   - Bambu/standard: M106 S-parameter; M106 P0 is AUX fan (ignored), M106 P1 is
//     part-cooling fan (accepted), M106 without P is accepted for non-Bambu machines
//   - MakerWare/Sailfish: M126 T-parameter
//
// [COUPLING] The -1 return value is overloaded: it means "not a fan command" but is
// also the sentinel used in `BufferData::fan_speed` to indicate "no fan command on
// this line". Callers must treat -1 as "not applicable", not as a valid speed.
int16_t get_fan_speed(const std::string& line, GCodeFlavor flavor)
{
    if (line.compare(0, 4, "M106") == 0) {
        if (flavor == (gcfMach3) || flavor == (gcfMachinekit)) {
            return (int16_t) get_axis_value(line, 'P');
        } else {
            // Bambu machines use both M106 P1(not P0!) and M106 for part cooling fan.
            // Non-bambu machines usually use M106 (without P parameter) for part cooling fan.
            // P2 is reserved for auxiliary fan regardless of bambu or not.
            // To keep compatibility with Bambu machines, we accept M106 and M106 P1 as the only two valid form
            // of gcode that control the part cooling fan. Any other command will be ignored!
            const auto idx = get_axis_value(line, 'P');
            if (!isnan(idx) && idx != 1.0f) {
                return -1;
            }
            return (int16_t) get_axis_value(line, 'S');
        }
    } else if (line.compare(0, 4, "M127") == 0 || line.compare(0, 4, "M107") == 0) {
        return 0;
    } else if ((flavor == (gcfMakerWare) || flavor == (gcfSailfish)) && line.compare(0, 4, "M126") == 0) {
        return (int16_t) get_axis_value(line, 'T');
    } else {
        return -1;
    }
}

// [INTENT] Inserts `line_to_write` into `m_buffer` at the correct position so that it
// appears exactly `nb_sec_since_itemtosplit_start` seconds after the start of
// `item_to_split`. If the target time is near the boundary (within 10% of the item's
// duration), the command is inserted before or after without splitting. Otherwise the
// G1 move is physically split into two shorter moves (before and after the injection
// point), preserving total motion.
//
// [STATE] When splitting, both the `before` copy and the modified `item_to_split` have
// their time, dx, dy, dz, de values adjusted proportionally. The raw G-code strings are
// also rewritten via `change_axis_value()`. The buffer list gains two elements (before
// and line_to_write) in front of the modified item_to_split.
//
// [MEMORY] Creates a copy (`before = *item_to_split`) on the stack; this is fine for
// the current 80-byte BufferData struct but would be costly if the struct grew large.
//
// [HAZARD] Only handles G1 moves (checked via raw string prefix "G1 "). Non-G1 commands
// (arcs, etc.) are inserted before without splitting — the timing will be slightly off
// for arc moves. This is acceptable only because the slicer rarely uses arc moves.
//
// [HAZARD] Relative-E mode (line 145-149) correctly uses `de` for both halves. Absolute-E
// mode only updates `item_to_split->e` (absolute position) and `before.de` but does NOT
// update `item_to_split->de` with the residual. This is consistent with the intent
// (de is used only for split math) but could confuse a reader expecting symmetric handling.
void FanMover::_put_in_middle_G1(std::list<BufferData>::iterator item_to_split,
                                 float                           nb_sec_since_itemtosplit_start,
                                 BufferData&&                    line_to_write)
{
    assert(item_to_split != m_buffer.end());
    if (nb_sec_since_itemtosplit_start > item_to_split->time * 0.9) {
        // doesn't really need to be split, print it after
        m_buffer.insert(next(item_to_split), line_to_write);
    } else if (nb_sec_since_itemtosplit_start < item_to_split->time * 0.1) {
        // doesn't really need to be split, print it before
        // will also print before if line_to_split.time == 0
        m_buffer.insert(item_to_split, line_to_write);
    } else if (item_to_split->raw.size() > 2 && item_to_split->raw[0] == 'G' && item_to_split->raw[1] == '1' &&
               item_to_split->raw[2] == ' ') {
        float      percent = nb_sec_since_itemtosplit_start / item_to_split->time;
        BufferData before  = *item_to_split;
        before.time *= percent;
        item_to_split->time *= (1 - percent);
        if (item_to_split->dx != 0) {
            before.dx = item_to_split->dx * percent;
            item_to_split->x += before.dx;
            item_to_split->dx = item_to_split->dx * (1 - percent);
            change_axis_value(before.raw, 'X', before.x + before.dx, 3);
        }
        if (item_to_split->dy != 0) {
            before.dy = item_to_split->dy * percent;
            item_to_split->y += before.dy;
            item_to_split->dy = item_to_split->dy * (1 - percent);
            change_axis_value(before.raw, 'Y', before.y + before.dy, 3);
        }
        if (item_to_split->dz != 0) {
            before.dz = item_to_split->dz * percent;
            item_to_split->z += before.dz;
            item_to_split->dz = item_to_split->dz * (1 - percent);
            change_axis_value(before.raw, 'Z', before.z + before.dz, 3);
        }
        if (item_to_split->de != 0) {
            if (relative_e) {
                before.de = item_to_split->de * percent;
                change_axis_value(before.raw, 'E', before.de, 5);
                item_to_split->de = item_to_split->de * (1 - percent);
                change_axis_value(item_to_split->raw, 'E', item_to_split->de, 5);
            } else {
                before.de = item_to_split->de * percent;
                item_to_split->e += before.de;
                item_to_split->de = item_to_split->de * (1 - percent);
                change_axis_value(before.raw, 'E', before.e + before.de, 5);
            }
        }
        // add before then line_to_write, then there is the modified data.
        m_buffer.insert(item_to_split, before);
        m_buffer.insert(item_to_split, line_to_write);

    } else {
        // not a G1, print it before
        m_buffer.insert(item_to_split, line_to_write);
    }
}

// [INTENT] Flushes `line_to_split` to `m_process_output`, injecting `line_to_write`
// at the correct time offset within it. Similar to `_put_in_middle_G1` but writes
// directly to the output string rather than re-buffering.
//
// [STATE] Mutates `line_to_split.raw` in-place when splitting (the "after" part) via
// `change_axis_value`. The caller must not reuse `line_to_split.raw` after this call
// as it has been partially overwritten with the residual move parameters.
//
// [HAZARD] Note the asymmetry vs `_put_in_middle_G1`: boundary thresholds are swapped —
// if `nb_sec < 10%`, the split line is printed first (output order: line, fan); if
// `nb_sec > 90%`, the fan is printed first. This is the reverse of the buffer version,
// reflecting the difference between "flush to output now" vs "re-buffer for later".
//
// [HAZARD] In absolute-E mode, only `before`'s E value is updated; the `after` part
// retains the original E endpoint (correct behaviour since it moves to the original target),
// but `de` for the after part is not updated, meaning if something later reads `de` from
// `line_to_split` after this call, it is stale.
void FanMover::_print_in_middle_G1(BufferData& line_to_split, float nb_sec, const std::string& line_to_write)
{
    if (nb_sec < line_to_split.time * 0.1) {
        // doesn't really need to be split, print it after
        m_process_output += line_to_split.raw + "\n";
        m_process_output += line_to_write + (line_to_write.back() == '\n' ? "" : "\n");
    } else if (nb_sec > line_to_split.time * 0.9) {
        // doesn't really need to be split, print it before
        // will also print before if line_to_split.time == 0
        m_process_output += line_to_write + (line_to_write.back() == '\n' ? "" : "\n");
        m_process_output += line_to_split.raw + "\n";
    } else if (line_to_split.raw.size() > 2 && line_to_split.raw[0] == 'G' && line_to_split.raw[1] == '1' && line_to_split.raw[2] == ' ') {
        float        percent = nb_sec / line_to_split.time;
        std::string  before  = line_to_split.raw;
        std::string& after   = line_to_split.raw;
        if (line_to_split.dx != 0) {
            change_axis_value(before, 'X', line_to_split.x + line_to_split.dx * percent, 3);
        }
        if (line_to_split.dy != 0) {
            change_axis_value(before, 'Y', line_to_split.y + line_to_split.dy * percent, 3);
        }
        if (line_to_split.dz != 0) {
            change_axis_value(before, 'Z', line_to_split.z + line_to_split.dz * percent, 3);
        }
        if (line_to_split.de != 0) {
            if (relative_e) {
                change_axis_value(before, 'E', line_to_split.de * percent, 5);
                change_axis_value(after, 'E', line_to_split.de * (1 - percent), 5);
            } else {
                change_axis_value(before, 'E', line_to_split.e + line_to_split.de * percent, 5);
            }
        }
        m_process_output += before + "\n";
        m_process_output += line_to_write + (line_to_write.back() == '\n' ? "" : "\n");
        m_process_output += line_to_split.raw + "\n";

    } else {
        // not a G1, print it before
        m_process_output += line_to_write + (line_to_write.back() == '\n' ? "" : "\n");
        m_process_output += line_to_split.raw + "\n";
    }
}

// [INTENT] Scans forward from the front of the buffer (oldest entries) up to `past_sec`
// seconds of elapsed time and removes any fan commands with speed below `min_speed`.
// Used to purge intermediate slowdown commands that would interfere with the kickstart
// pulse — we don't want a step-down to cancel the upcoming spin-up.
//
// [STATE] Traversal is from front (oldest, output side) to back (newest, input side),
// consuming time budget as each item is visited. Removal via `remove_from_buffer()`
// keeps `m_buffer_time_size` consistent.
//
// [HAZARD] Only fan commands (`fan_speed >= 0`) below `min_speed` are removed; G-code
// motion lines are skipped. If a motion line's time depletes `past_sec` to <= 0, the
// scan stops, potentially leaving slow fan commands further into the buffer un-purged.
// This is intentional (only clean "recent" history) but means kickstart suppression
// only reaches back as far as the motion lines allow.
void FanMover::_remove_slow_fan(int16_t min_speed, float past_sec)
{
    // erase fan in the buffer -> don't slowdown if you are in the process of step-up.
    // we began at the "recent" side , and remove as long as we don't push past_sec to 0
    auto it = m_buffer.begin();
    while (it != m_buffer.end() && past_sec > 0) {
        past_sec -= it->time;
        if (it->fan_speed >= 0 && it->fan_speed < min_speed) {
            // found something that is lower than us
            it = remove_from_buffer(it);

        } else {
            ++it;
        }
    }
}

// [INTENT] Thin wrapper around `GCodeWriter::set_fan()` that delegates to the writer's
// configured G-code flavour. Returns the complete M106/M107 command string.
//
// [COUPLING] The commented-out alternative (`m_writer.get_tool(...)`) indicates a
// planned but not-yet-implemented multi-extruder fan assignment. Currently always
// uses extruder 0's fan command format. In multi-extruder setups this is wrong.
std::string FanMover::_set_fan(int16_t speed)
{
    // const Tool* tool = m_writer.get_tool(m_currrent_extruder < 20 ? m_currrent_extruder : 0);
    return GCodeWriter::set_fan(m_writer.config.gcode_flavor.value, speed);
}

// [INTENT] Converts a `string_view` sub-string to int. Used by `_process_T()` to
// parse the tool index from a `Tnn` command.
//
// [MEMORY] Explicitly noted in the comment as "costly" — constructs a full `std::string`
// copy from the `string_view` before calling `std::stoi`. This was a placeholder
// conversion; the comment suggests a faster from_chars() approach was intended.
//
// [HAZARD] `std::stoi` can throw `std::invalid_argument` and `std::out_of_range`, both
// caught by the blanket `catch(...)`. Silent failure (returns false) is acceptable here
// since the callers handle the fallback case.
bool parse_number(const std::string_view sv, int& out)
{
    {
        // Legacy conversion, which is costly due to having to make a copy of the string before conversion.
        try {
            assert(sv.size() < 1024);
            assert(sv.data() != nullptr);
            std::string str{sv};
            size_t      read = 0;
            out              = std::stoi(str, &read);
            return str.size() == read;
        } catch (...) {
            return false;
        }
    }
}

// [INTENT] Parses a tool-change command (`Tnn`) and updates `m_currrent_extruder`.
// Handles special firmware-specific tool commands (Tx, Tc, T?, T-1) without changing
// the current extruder state.
//
// [STATE] `m_currrent_extruder` is updated here. If the parse fails and the firmware
// is not RepRap, the extruder is reset to 0 as a safe fallback. This means malformed
// tool commands silently select extruder 0 on non-RepRap firmware.
//
// [COUPLING] The TODO comment "add other firmware / create that damn new gcode writer arch"
// signals that this function is a known maintenance burden — firmware-specific branching
// is scattered here rather than in a polymorphic writer.
// FIXME: add other firmware
// or just create that damn new gcode writer arch
void FanMover::_process_T(const std::string_view command)
{
    if (command.length() > 1) {
        int eid = 0;
        if (!parse_number(command.substr(1), eid) || eid < 0 || eid > 255) {
            GCodeFlavor flavor = m_writer.config.gcode_flavor;
            // Specific to the MMU2 V2 (see https://www.help.prusa3d.com/en/article/prusa-specific-g-codes_112173):
            if ((flavor == gcfMarlinLegacy || flavor == gcfMarlinFirmware) && (command == "Tx" || command == "Tc" || command == "T?"))
                return;

            // T-1 is a valid gcode line for RepRap Firmwares (used to deselects all tools) see https://github.com/prusa3d/PrusaSlicer/issues/5677
            if ((flavor != gcfRepRapFirmware && flavor != gcfRepRapSprinter) || eid != -1)
                m_currrent_extruder = static_cast<uint16_t>(0);
        } else {
            m_currrent_extruder = static_cast<uint16_t>(eid);
        }
    }
}

// [INTENT] Core per-line dispatcher for the FanMover post-processor. Called by the
// GCodeReader callback for every line in the input. Decides whether to buffer the line,
// emit it immediately, inject kickstart commands, or split G1 moves to place fan
// commands at precise time offsets.
//
// [STATE] Key state machine transitions:
//   - Fan speed increase + delay mode + (not custom gcode) + (not only_overhangs or on overhang):
//       → fan command goes into the PAST (delayed back in the buffer)
//   - Fan speed decrease: → trigger flush so no slow-down command is swallowed
//   - Custom gcode section: → fan commands pass through without delay
//   - `;TYPE:` comment: → updates `current_role` for only_overhangs mode
//   - `;custom gcode` / `;custom gcode end`: → toggles `m_is_custom_gcode`
//
// [MEMORY] Each G-code line is stored in a `BufferData` on the heap (via `std::list`
// emplace_back). For very long prints with large `nb_seconds_delay`, the buffer can
// accumulate thousands of entries. Memory is bounded by the delay window length.
//
// [CONCURRENCY] Runs inside a GCodeReader callback — single-threaded, no locking needed.
//
// [HAZARD] Speed estimation (line 296): `time = dist / m_current_speed` uses a constant
// speed (the last seen F parameter). It ignores acceleration/deceleration, so time
// estimates for short moves are systematically low. This causes fan commands to arrive
// slightly early (a few milliseconds), which is acceptable for fan control but is a
// known limitation.
//
// [HAZARD] `fan_speed` is normalised from 0-255 to 0-100 on line 306 using integer
// division: `100 * fan_speed / 255`. Values are truncated, not rounded. S127 maps to
// 49, not 50. This is consistent with existing behaviour but a refactoring must not
// change the divisor from 255 to 100 or all thresholds break.
//
// [HAZARD] The `need_flush` path (fan speed decrease) bypasses the delay logic and
// flushes everything accumulated so far. This is correct but means a slowdown command
// always arrives on time — there is no symmetrical "delay slowdowns" mode.
//
// [HAZARD] The `m_current_kickstart` countdown (line 456-461) decrements by the current
// G1 move's time. If the move time was underestimated (see speed hazard above), the
// kickstart ends later than intended — the printer may overshoot the kickstart window
// slightly. This results in a longer full-speed pulse than configured, which is safe but
// slightly wasteful.
//
// [HAZARD] `assert(abs(m_buffer_time_size - sum) < 0.01)` at line 510 fires in debug
// builds if rounding drift exceeds 10ms. The recompute at the top of `process_gcode()`
// is the corrective measure; if calls are infrequent (flush=true) the drift can
// accumulate within a single call.
void FanMover::_process_gcode_line(GCodeReader& reader, const GCodeReader::GCodeLine& line)
{
    // processes 'normal' gcode lines
    bool        need_flush = false;
    std::string cmd(line.cmd());
    double      time      = 0;
    int16_t     fan_speed = -1;
    if (cmd.length() > 1) {
        if (line.has_f())
            m_current_speed = line.f() / 60.0f;
        switch (::toupper(cmd[0])) {
        case 'T':
        case 't': _process_T(cmd); break;
        case 'G': {
            // [INTENT] Compute motion time for G0/G1 moves using Euclidean XYZ distance
            // and current feedrate. E-only moves (retracts) are excluded from time
            // computation because they don't affect the physical head position.
            if (::atoi(&cmd[1]) == 1 || ::atoi(&cmd[1]) == 0) {
                double distx = line.dist_X(reader);
                double disty = line.dist_Y(reader);
                double distz = line.dist_Z(reader);
                double dist  = distx * distx + disty * disty + distz * distz;
                if (dist > 0) {
                    dist = std::sqrt(dist);
                    time = dist / m_current_speed;
                }
            }
            break;
        }
        case 'M': {
            // [INTENT] Detect and handle fan commands. The fan_speed result is:
            //   -1  → not a fan command (pass through normally)
            //    0  → M107/fan off
            //  1..100 → normalised fan speed percent
            //
            // [STATE] `time = -1` signals to the post-buffer section below that this
            // line should NOT be added to the buffer (it has been handled inline).
            fan_speed = get_fan_speed(line.raw(), m_writer.config.gcode_flavor);
            if (fan_speed >= 0) {
                const auto fan_baseline = 255.0;
                fan_speed               = 100 * fan_speed / fan_baseline;
                // speed change: stop kickstart reverting if any
                m_current_kickstart.time = -1;
                if (!m_is_custom_gcode) {
                    // if slow down => put in the queue. if not =>
                    if (m_back_buffer_fan_speed < fan_speed) {
                        if (nb_seconds_delay > 0 && (!only_overhangs || current_role == ExtrusionRole::erOverhangPerimeter)) {
                            // don't put this command in the queue
                            time = -1;
                            // this M106 need to go in the past
                            // check if we have ( kickstart and not in slowdown )
                            if (kickstart > 0 && fan_speed > m_front_buffer_fan_speed) {
                                // stop current kickstart , it's not relevant anymore
                                if (m_current_kickstart.time > 0) {
                                    m_current_kickstart.time = (-1);
                                }

                                // [INTENT] Kickstart sequence: briefly emit M106 S255 (full speed)
                                // to overcome static friction / back-EMF on the fan motor, then
                                // after `kickstart` seconds, step down to the actual target speed.
                                //
                                // [STATE] Two-phase insertion:
                                // 1) Emit M106 S255 immediately (into buffer front / output)
                                // 2) Schedule the actual target M106 in the buffer at
                                //    `kickstart_duration` seconds from now
                                //
                                // [HAZARD] `kickstart_duration` scales with the fan delta:
                                // `kickstart * (fan_speed - m_front_buffer_fan_speed) / 100`.
                                // If fan_speed == m_front_buffer_fan_speed + 1 (1% increase),
                                // the kickstart is 1/100th of `kickstart` seconds — potentially
                                // sub-millisecond, effectively no kickstart pulse at all.

                                // first erase everything lower that that value
                                _remove_slow_fan(fan_speed, m_buffer_time_size + 1);
                                // then erase everything lower that kickstart
                                _remove_slow_fan(fan_baseline, kickstart);
                                // print me
                                if (!m_buffer.empty() && (m_buffer_time_size - m_buffer.front().time * 0.1) > nb_seconds_delay) {
                                    _print_in_middle_G1(m_buffer.front(), m_buffer_time_size - nb_seconds_delay,
                                                        _set_fan(100)); // m_writer.set_fan(100, true)); //FIXME extruder id (or use the
                                                                        // gcode writer, but then you have to disable the multi-thread thing
                                    remove_from_buffer(m_buffer.begin());
                                } else {
                                    m_process_output += _set_fan(100); // m_writer.set_fan(100, true)); //FIXME extruder id (or use the
                                                                       // gcode writer, but then you have to disable the multi-thread thing
                                }
                                // write it in the queue if possible
                                const float kickstart_duration = kickstart * float(fan_speed - m_front_buffer_fan_speed) / 100.f;
                                float       time_count         = kickstart_duration;
                                auto        it                 = m_buffer.begin();
                                while (it != m_buffer.end() && time_count > 0) {
                                    time_count -= it->time;
                                    if (time_count < 0) {
                                        // found something that is lower than us
                                        _put_in_middle_G1(it, it->time + time_count,
                                                          BufferData(std::string(line.raw()), 0, fan_speed, true));
                                        // found, stop
                                        break;
                                    }
                                    ++it;
                                }
                                if (time_count > 0) {
                                    // can't place it in the buffer, use m_current_kickstart
                                    m_current_kickstart.fan_speed = fan_speed;
                                    m_current_kickstart.time      = time_count;
                                    m_current_kickstart.raw       = line.raw();
                                }
                                m_front_buffer_fan_speed = fan_speed;
                            } else {
                                // [INTENT] No kickstart: erase any lower fan commands in the
                                // buffer that this command supersedes, then inject the fan command
                                // at the correct position relative to the delay window.
                                // first erase everything lower that that value
                                _remove_slow_fan(fan_speed, m_buffer_time_size + 1);
                                // then write the fan command
                                if (!m_buffer.empty() && (m_buffer_time_size - m_buffer.front().time * 0.1) > nb_seconds_delay) {
                                    _print_in_middle_G1(m_buffer.front(), m_buffer_time_size - nb_seconds_delay, line.raw());
                                    remove_from_buffer(m_buffer.begin());
                                } else {
                                    m_process_output += line.raw() + "\n";
                                }
                                m_front_buffer_fan_speed = fan_speed;
                            }
                        } else {
                            // [INTENT] Delay is disabled (nb_seconds_delay == 0) OR we are not
                            // on an overhang in only_overhangs mode. Fan command passes through
                            // normally, but kickstart logic may still apply.
                            if (kickstart <= 0) {
                                // nothing to do
                                // we don't put time = -1; so it will printed in the buffer as other line are done
                            } else if (m_current_kickstart.time > 0) {
                                // cherry-pick this one
                                if (m_back_buffer_fan_speed >= fan_speed) {
                                    // stop kickstart
                                    m_current_kickstart.time = -1;
                                    // this will print me just after as time >=0
                                } else {
                                    // add some duration to the kickstart and use it for me.
                                    float kickstart_duration      = kickstart * float(fan_speed - m_back_buffer_fan_speed) / 100.f;
                                    m_current_kickstart.fan_speed = fan_speed;
                                    m_current_kickstart.time += kickstart_duration;
                                    m_current_kickstart.raw = line.raw();
                                    // i'm printed by the m_current_kickstart
                                    time = -1;
                                }
                            } else if (m_back_buffer_fan_speed < fan_speed - 10) { // only kickstart if more than 10% change
                                // don't write this line, as it will need to be delayed
                                time = -1;
                                // get the duration of kickstart
                                float kickstart_duration = kickstart * float(fan_speed - m_back_buffer_fan_speed) / 100.f;
                                // if kickstart, write the M106 S[fan_baseline] first
                                // set the target speed and set the kickstart flag
                                put_in_buffer(BufferData(_set_fan(100) // m_writer.set_fan(100, true)); //FIXME extruder id (or use the
                                                                       // gcode writer, but then you have to disable the multi-thread thing
                                                         ,
                                                         0, fan_speed, true));
                                // kickstart!
                                // m_process_output += m_writer.set_fan(100, true);
                                // add the normal speed line for the future
                                m_current_kickstart.fan_speed = fan_speed;
                                m_current_kickstart.time      = kickstart_duration;
                                m_current_kickstart.raw       = line.raw();
                            }
                        }
                    }
                    // update back buffer fan speed
                    m_back_buffer_fan_speed = fan_speed;
                } else {
                    // have to flush the buffer to avoid erasing a fan command.
                    need_flush = true;
                }
            }
            break;
        }
        }
    } else {
        // [INTENT] Handle single-character lines and comments:
        //   - `;TYPE:<role>` → update `current_role` for only_overhangs mode tracking
        //   - `; custom gcode` → enter custom gcode section (bypass delay logic)
        //   - `; custom gcode end` → exit custom gcode section
        //
        // [COUPLING] Depends on the exact comment strings `;TYPE:` and `; custom gcode`
        // being injected by GCode.cpp. If those strings change, this detection breaks
        // silently — no compile-time contract enforces them.
        if (!line.raw().empty() && line.raw().front() == ';') {
            if (line.raw().size() > 10 && line.raw().rfind(";TYPE:", 0) == 0) {
                // get the type of the next extrusions
                std::string extrusion_string = line.raw().substr(6, line.raw().size() - 6);
                current_role                 = ExtrusionEntity::string_to_role(extrusion_string);
            }
            if (line.raw().size() > 16) {
                if (line.raw().rfind("; custom gcode", 0) != std::string::npos) {
                    if (line.raw().rfind("; custom gcode end", 0) != std::string::npos)
                        m_is_custom_gcode = false;
                    else
                        m_is_custom_gcode = true;
                }
            }
        }
    }

    // [INTENT] Buffer the current line if time >= 0 (i.e. it was not consumed inline).
    // Fan commands with time == -1 were handled above and should not re-enter the buffer.
    // After buffering, drain lines from the front when the buffer window exceeds the delay.
    //
    // [STATE] `new_data` is a reference into `m_buffer.back()`. Setting x/y/z/e on it
    // after `put_in_buffer()` populates the position fields needed by `_put_in_middle_G1`
    // for correct proportional splitting later.
    //
    // [HAZARD] After `put_in_buffer()`, `m_current_kickstart.time` is decremented by the
    // new move's time. If it crosses zero, `_put_in_middle_G1` is called on `prev(m_buffer.end())`,
    // i.e. the entry we just inserted. This is safe because `std::list` iterators are
    // stable, but it does cause the just-inserted entry to be split immediately —
    // the kickstart target fan command is injected into the middle of this same move.
    if (time >= 0) {
        BufferData& new_data = put_in_buffer(BufferData(line.raw(), time, fan_speed));
        if (line.has(Axis::X)) {
            new_data.x  = reader.x();
            new_data.dx = line.dist_X(reader);
        }
        if (line.has(Axis::Y)) {
            new_data.y  = reader.y();
            new_data.dy = line.dist_Y(reader);
        }
        if (line.has(Axis::Z)) {
            new_data.z  = reader.z();
            new_data.dz = line.dist_Z(reader);
        }
        if (line.has(Axis::E)) {
            new_data.e = reader.e();
            if (relative_e)
                new_data.de = line.e();
            else
                new_data.de = line.dist_E(reader);
        }

        if (m_current_kickstart.time > 0 && time > 0) {
            m_current_kickstart.time -= time;
            if (m_current_kickstart.time < 0) {
                // prev is possible because we just do a emplace_back.
                _put_in_middle_G1(prev(m_buffer.end()), time + m_current_kickstart.time,
                                  BufferData{m_current_kickstart.raw, 0, m_current_kickstart.fan_speed, true});
            }
        }
    } /* else {
         BufferData& new_data = put_in_buffer(BufferData("; del? "+line.raw(), 0, fan_speed));
         if (line.has(Axis::X)) {
             new_data.x = reader.x();
             new_data.dx = line.dist_X(reader);
         }
         if (line.has(Axis::Y)) {
             new_data.y = reader.y();
             new_data.dy = line.dist_Y(reader);
         }
         if (line.has(Axis::Z)) {
             new_data.z = reader.z();
             new_data.dz = line.dist_Z(reader);
         }
         if (line.has(Axis::E)) {
             new_data.e = reader.e();
             if (relative_e)
                 new_data.de = line.e();
             else
                 new_data.de = line.dist_E(reader);
         }
     }*/
    // puts the line back into the gcode
    // if buffer too big, flush it.
    // [INTENT] Drain loop: emit lines from the front of the buffer when the buffer
    // holds more time than `nb_seconds_delay`. Lines with fan_speed == -1 (non-fan
    // lines) are always emitted as-is. Fan lines with matching front-speed are
    // suppressed (they were already handled above). Kickstart lines that turn out to
    // be a slowdown are rewritten with the correct lower speed via `_set_fan()`.
    //
    // [HAZARD] The suppression condition `frontdata.fan_speed != m_front_buffer_fan_speed`
    // means duplicate fan commands (same speed, re-emitted) are silently dropped. This
    // is correct behaviour but means if upstream emits redundant fan commands as a
    // safety measure, FanMover removes them, potentially leaving the fan at the wrong
    // speed if a prior command was delayed or dropped.
    if (time >= 0) {
        while (!m_buffer.empty() && (need_flush || m_buffer_time_size - m_buffer.front().time > nb_seconds_delay - EPSILON)) {
            BufferData& frontdata = m_buffer.front();
            if (frontdata.fan_speed < 0 || frontdata.fan_speed != m_front_buffer_fan_speed || frontdata.is_kickstart) {
                if (frontdata.is_kickstart && frontdata.fan_speed < m_front_buffer_fan_speed) {
                    // you have to slow down! not kickstart! rewrite the fan speed.
                    m_process_output += _set_fan(
                        frontdata.fan_speed); // m_writer.set_fan(frontdata.fan_speed,true); //FIXME extruder id (or use the gcode writer,
                                              // but then you have to disable the multi-thread thing

                    m_front_buffer_fan_speed = frontdata.fan_speed;
                } else {
                    m_process_output += frontdata.raw + "\n";
                    if (frontdata.fan_speed >= 0) {
                        // note that this is the only place where the fan_speed is set and we print from the buffer, as if the fan_speed >=
                        // 0 => time == 0 and as this flush all time == 0 lines from the back of the queue...
                        m_front_buffer_fan_speed = frontdata.fan_speed;
                    }
                }
            }
            remove_from_buffer(m_buffer.begin());
        }
    }
    // [INTENT] Debug invariant: verify that `m_buffer_time_size` is consistent with
    // the actual sum of all buffer entry times. A discrepancy > 10ms indicates a bug
    // in put_in_buffer/remove_from_buffer accounting. Only fires in debug builds.
    double sum = 0;
    for (auto& data : m_buffer)
        sum += data.time;
    assert(std::abs(m_buffer_time_size - sum) < 0.01);
}

} // namespace Slic3r
