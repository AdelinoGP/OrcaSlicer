// [INTENT] PressureEqualizer is a G-code post-processor that enforces a maximum
// volumetric extrusion rate slope (mm³/s²) by adjusting feed rates on individual
// G1 extrusion lines. It prevents the extruder pressure from changing too abruptly,
// which would otherwise cause under/over-extrusion at role transitions (e.g. when
// switching from a slow external perimeter to a fast infill line or vice-versa).
//
// The algorithm operates on a per-layer buffer of parsed GCodeLine structs:
//   1. parse: process_layer(string) → parse all lines into m_gcode_lines
//   2. segment: identify contiguous extrusion segments (ignoring small travel gaps)
//   3. smooth: for each segment, call adjust_volumetric_rate(start, end) which
//              runs BACKWARD (decel) then FORWARD (accel) to clamp rate slopes
//   4. emit: output_gcode_line() re-serializes modified lines, splitting long
//            lines into sub-segments to achieve a smooth speed ramp
//
// [STATE] Stateful across layers: m_current_pos[5] (XYZEF), m_current_extruder,
//   m_retracted, m_current_extrusion_role all persist across process_layer() calls.
//   m_gcode_lines also persists (lines from layer N are still in the buffer when
//   layer N+1 is processed; they are emitted and erased at the next call).
//
// [COUPLING] PressureEqualizer sits in the TBB pipeline in GCode.cpp between the
//   per-layer G-code generator and the CoolingBuffer. It receives one LayerResult
//   at a time. The 1-layer lookahead buffer (m_layer_results queue + NOP injection)
//   means the caller must inject a NOP layer at end-of-print to flush the last
//   real layer.
//
// [MEMORY] output_buffer is a single char vector that only grows (never shrinks).
//   The power-of-2 resize strategy amortises allocation cost. m_gcode_lines is
//   a persistent vector; front entries are erased after emission.
//
// [HAZARD] `goto single_slope_fallback` on line ~549: the accel-peak-decel
//   solver uses a goto to fall back to the simpler single-slope mode when the
//   computed peak rate is degenerate. This is the only goto in the G-code pipeline.
//   A port should replace this with early-return or a helper function.
//
// [HAZARD] m_layer_results is a public member (raw pointer queue). Callers can
//   directly access or modify the queue, bypassing encapsulation. Public access
//   is needed by the TBB pipeline in GCode.cpp; it creates a coupling hazard.

#include <iostream>
#include <memory.h>
#include <cstring>
#include <cfloat>
#include <algorithm>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../LocalesUtils.hpp"
#include "../GCode.hpp"

#include "PressureEqualizer.hpp"
#include "fast_float/fast_float.h"
#include "GCodeWriter.hpp"

namespace Slic3r {

// [INTENT] In-band comment tags injected by GCode.cpp to communicate extrusion role
// transitions to PressureEqualizer. These are not valid G-code; they are stripped
// from output. EXTRUDE_SET_SPEED opens a "speed-adjustable block" and EXTRUDE_END
// closes it — only lines inside such blocks have their feed rate modified.
// EXTERNAL_PERIMETER_TAG tags external perimeter moves for Klipper PA tagging.
static const std::string EXTRUSION_ROLE_TAG = ";_EXTRUSION_ROLE:";
static const std::string EXTRUDE_END_TAG = ";_EXTRUDE_END";
static const std::string EXTRUDE_SET_SPEED_TAG = ";_EXTRUDE_SET_SPEED";
static const std::string EXTERNAL_PERIMETER_TAG = ";_EXTERNAL_PERIMETER";

// [INTENT] Sliding-window back-look limit for adjust_volumetric_rate(). 
// The backward pass corrects deceleration needs: it looks back up to 128 G-code
// lines to propagate a lower rate-start constraint from a future slow segment.
// A larger limit means smoother deceleration over a longer distance but costs
// O(N × max_look_back_limit) time per layer.
// [HAZARD] 128 is not derived from any physical model — it is a fixed heuristic.
// For very fine-resolution G-code (many short lines), 128 lines may cover only
// a fraction of a millimetre; for coarse G-code it may span many centimetres.
static constexpr int max_look_back_limit = 128;

// [INTENT] Maximum XY travel distance (mm) between two extrusion segments that
// is treated as "within the same extrusion" for pressure continuity purposes.
// Rationale: at short travel gaps (e.g. between adjacent infill lines) the nozzle
// retains some melt pressure, so pre-decelerating before the gap wastes print speed.
// Above 3 mm, pressure is assumed to have equilibrated to zero and a fresh start
// is modelled.
// [HAZARD] Hardcoded constant — not exposed as a config option. Different nozzle
// geometries / materials have different pressure decay rates; 3 mm is a heuristic.
static constexpr long max_ignored_gap_between_extruding_segments = 3;

PressureEqualizer::PressureEqualizer(const Slic3r::GCodeConfig &config) : m_use_relative_e_distances(config.use_relative_e_distances.value)
{
    // Preallocate some data, so that output_buffer.data() will return an empty string.
    output_buffer.assign(32, 0);
    output_buffer_length      = 0;
    output_buffer_prev_length = 0;

    m_current_extruder = 0;
    // Zero the position of the XYZE axes + the current feed
    memset(m_current_pos, 0, sizeof(float) * 5);
    m_current_extrusion_role = ExtrusionRole::erNone;
    // Expect the first command to fill the nozzle (deretract).
    m_retracted = true;
    
    m_max_segment_length = 2.f;

    // Calculate filamet crossections for the multiple extruders.
    m_filament_crossections.clear();
    for (double r : config.filament_diameter.values) {
        double a = 0.25f * M_PI * r * r;
        m_filament_crossections.push_back(float(a));
    }

    // Volumetric rate of a 0.45mm x 0.2mm extrusion at 60mm/s XY movement: 0.45*0.2*60*60=5.4*60 = 324 mm^3/min
    // Volumetric rate of a 0.45mm x 0.2mm extrusion at 20mm/s XY movement: 0.45*0.2*20*60=1.8*60 = 108 mm^3/min
    // Slope of the volumetric rate, changing from 20mm/s to 60mm/s over 2 seconds: (5.4-1.8)*60*60/2=60*60*1.8 = 6480 mm^3/min^2 = 1.8 mm^3/s^2
    
    if(config.max_volumetric_extrusion_rate_slope.value > 0){
		m_max_volumetric_extrusion_rate_slope_positive = float(config.max_volumetric_extrusion_rate_slope.value) * 60.f * 60.f;
    	m_max_volumetric_extrusion_rate_slope_negative = float(config.max_volumetric_extrusion_rate_slope.value) * 60.f * 60.f;
    	m_max_segment_length = float(config.max_volumetric_extrusion_rate_slope_segment_length.value);
        m_extrusion_rate_smoothing_external_perimeter_only = bool(config.extrusion_rate_smoothing_external_perimeter_only.value);
    }

    for (ExtrusionRateSlope &extrusion_rate_slope : m_max_volumetric_extrusion_rate_slopes) {
        extrusion_rate_slope.negative = m_max_volumetric_extrusion_rate_slope_negative;
        extrusion_rate_slope.positive = m_max_volumetric_extrusion_rate_slope_positive;
    }
    
	// Don't regulate the pressure before and after ironing.
    for (const ExtrusionRole er : {ExtrusionRole::erIroning}) {
        m_max_volumetric_extrusion_rate_slopes[size_t(er)].negative = 0;
        m_max_volumetric_extrusion_rate_slopes[size_t(er)].positive = 0;
    }

    opened_extrude_set_speed_block = false;

#ifdef PRESSURE_EQUALIZER_STATISTIC
    m_stat.reset();
#endif

#ifdef PRESSURE_EQUALIZER_DEBUG
    line_idx = 0;
#endif
}

// [INTENT] process_layer(string) — Phase 1: parse a layer's raw G-code string into
// m_gcode_lines, then identify contiguous extrusion segments and run the sliding-window
// pressure-equalizer over each one.
//
// Flow:
//   a) Tokenise the string line-by-line; call process_line() on each. Lines that are
//      in-band tags (EXTRUSION_ROLE_TAG etc.) return false and are discarded from the
//      vector — they have already updated class state.
//   b) Walk m_gcode_lines to find "extrusion segments": runs of extruding lines that
//      may be bridged across small travel gaps (≤3 mm) by advance_segment_beyond_small_gap().
//   c) For each segment, apply adjust_volumetric_rate() in a sliding window of up to
//      max_look_back_limit lines centred at the current line. This is the O(N * W) pass.
//
// [STATE] m_gcode_lines grows here; it is NOT cleared — lines from layer N survive until
// they are emitted by the overloaded process_layer(LayerResult&&) caller.
// opened_extrude_set_speed_block must be false at the end of each layer string (asserted).
void PressureEqualizer::process_layer(const std::string &gcode)
{
    if (!gcode.empty()) {
        const char *gcode_begin = gcode.c_str();
        while (*gcode_begin != 0) {
            // Find end of the line.
            const char *gcode_end = gcode_begin;
            // Slic3r always generates end of lines in a Unix style.
            for (; *gcode_end != 0 && *gcode_end != '\n'; ++gcode_end);

            m_gcode_lines.emplace_back();
            if (!this->process_line(gcode_begin, gcode_end, m_gcode_lines.back())) {
                // The line has to be forgotten. It contains comment marks, which shall be filtered out of the target g-code.
                m_gcode_lines.pop_back();
            }
            gcode_begin = gcode_end;
            if (*gcode_begin == '\n')
                ++gcode_begin;
        }
        assert(!this->opened_extrude_set_speed_block);
    }
    
    // at this point, we have an entire layer of gcode lines loaded into m_gcode_lines
    // now we will split the mix of travels and extrudes into segments of continous extrusion and process those
    // We skip over large travels, and pretend small ones are part of a continous extrusion segment
    long idx_end_current_extrusion = 0;
    while (idx_end_current_extrusion < m_gcode_lines.size()) {
        // find beginning of next extrusion segment from current pos
        const long idx_begin_current_extrusion   = find_if(m_gcode_lines.begin() + idx_end_current_extrusion, m_gcode_lines.end(),
                                                          [](GCodeLine line) { return line.extruding(); }) - m_gcode_lines.begin();
        // (extrusion begin idx = extrusion end idx) here because we start with extrusion length of zero
        idx_end_current_extrusion = idx_begin_current_extrusion;

        // inner loop extends the extrusion segment over small travel moves
        while (idx_end_current_extrusion < m_gcode_lines.size()) {
            // find end of the current extrusion segment
            const auto just_after_end_extrusion = find_if(m_gcode_lines.begin() + idx_end_current_extrusion, m_gcode_lines.end(),
                                                          [](GCodeLine line) { return !line.extruding(); });
            idx_end_current_extrusion = std::max<long>(0,(just_after_end_extrusion - m_gcode_lines.begin()) - 1);
            const long idx_begin_segment_continuation = advance_segment_beyond_small_gap(idx_end_current_extrusion);
            if (idx_begin_segment_continuation > idx_end_current_extrusion) {
                // extend the continous line over the small gap
                idx_end_current_extrusion = idx_begin_segment_continuation;
                continue; // keep going, loop again to find new end of extrusion segment
            } else {
                // gap to next extrude is too big, stop looking forward. We've found end of this segment
                break;
            }
        }

        // now run the pressure equalizer across the segment like a streamroller
        // it operates on a sliding window that moves forward across gcode line by line
        for (int i = idx_begin_current_extrusion; i < idx_end_current_extrusion; ++i) {
            // feed pressure equalizer past lines, going back to max_look_back_limit (or start of segment)
            const auto start_idx = std::max<long>(idx_begin_current_extrusion, i - max_look_back_limit);
            adjust_volumetric_rate(start_idx, i);
        }
        // current extrusion is all done processing so advance beyond it for next loop
        idx_end_current_extrusion++;
    }
}

// [INTENT] advance_segment_beyond_small_gap — looks forward from the end of an extrusion
// segment to see if the next extruding line is reachable via a travel move shorter than
// max_ignored_gap_between_extruding_segments (3 mm). If yes, returns the index of that
// next extruding line so the caller can extend the current segment across the gap.
// If the gap is too large (or the end of the layer is reached), returns idx_orig unchanged.
//
// [STATE] Pure read on m_gcode_lines; does not modify any state.
// [HAZARD] Uses dist_xy() which only sums XY distance; Z hops are ignored. A large Z-hop
//          followed by an immediate XY move could be mis-classified as a small gap.
long PressureEqualizer::advance_segment_beyond_small_gap(const long idx_orig)
{
    // this should only be run on the last extruding line before a gap
    assert(m_gcode_lines[idx_orig].extruding());
    double distance_traveled = 0.0;
    // start at beginning of gap, advance till extrusion found or gap too big
    for (auto idx_cur_pos = idx_orig + 1; idx_cur_pos < m_gcode_lines.size(); idx_cur_pos++) {
        // started extruding again! return segment extension
        if (m_gcode_lines[idx_cur_pos].extruding()) {
            return idx_cur_pos;
        }

        distance_traveled += m_gcode_lines[idx_cur_pos].dist_xy();
        // gap too big, dont extend segment
        if (distance_traveled > max_ignored_gap_between_extruding_segments) {
            return idx_orig;
        }
    }
    // looped until end of layer and couldn't extend extrusion
     return idx_orig;
}

// [INTENT] process_layer(LayerResult&&) — Phase 2: the TBB-pipeline entry point.
// Implements a 1-layer lookahead buffer using m_layer_results (a raw-pointer queue):
//   - On the FIRST call: parse the layer into m_gcode_lines, buffer the LayerResult,
//     return a NOP to signal "not ready yet".
//   - On subsequent calls: parse the new layer (look-ahead), then EMIT the previously
//     buffered layer by serialising m_gcode_lines[0..next_layer_first_idx) into
//     output_buffer, then erase those lines from m_gcode_lines.
//   - When a NOP LayerResult is injected by the caller (end-of-print flush): skip the
//     parse phase, emit the last real layer, return it.
//
// [COUPLING] m_layer_results is a public std::queue<LayerResult*> (raw pointer ownership).
//   The caller (GCode.cpp TBB pipeline) is responsible for injecting the end-of-print NOP.
//   Failing to inject the NOP leaks the last layer in the buffer.
//
// [MEMORY] prev_layer_result is raw-`delete`d after copy-out. The returned LayerResult
//   is a value copy — safe for TBB pipeline ownership transfer.
//
// [HAZARD] output_buffer is populated by output_gcode_line() in a tight loop; the result
//   is then assigned as a string via output_buffer.data() which relies on a NUL byte
//   inserted at output_buffer_length. The buffer must remain valid until the string copy.
LayerResult PressureEqualizer::process_layer(LayerResult &&input)
{
    const bool   is_first_layer       = m_layer_results.empty();
    const size_t next_layer_first_idx = m_gcode_lines.size();

    if (!input.nop_layer_result) {
        this->process_layer(input.gcode);
        input.gcode.clear(); // GCode is already processed, so it isn't needed to store it.
        m_layer_results.emplace(new LayerResult(input));
    }

    if (is_first_layer) // Buffer previous input result and output NOP.
        return LayerResult::make_nop_layer_result();

    // Export previous layer.
    LayerResult *prev_layer_result = m_layer_results.front();
    m_layer_results.pop();

    output_buffer_length      = 0;
    output_buffer_prev_length = 0;
    for (size_t line_idx = 0; line_idx < next_layer_first_idx; ++line_idx)
        output_gcode_line(line_idx);
    m_gcode_lines.erase(m_gcode_lines.begin(), m_gcode_lines.begin() + int(next_layer_first_idx));

    if (output_buffer_length > 0)
        prev_layer_result->gcode = std::string(output_buffer.data());

    assert(!input.nop_layer_result || m_layer_results.empty());
    LayerResult out = *prev_layer_result;
    delete prev_layer_result;
    return out;
}

// Is a white space?
static inline bool is_ws(const char c) { return c == ' ' || c == '\t'; }
// Is it an end of line? Consider a comment to be an end of line as well.
static inline bool is_eol(const char c) { return c == 0 || c == '\r' || c == '\n' || c == ';'; }
// Is it a white space or end of line?
static inline bool is_ws_or_eol(const char c) { return is_ws(c) || is_eol(c); }

// Eat whitespaces.
static void eatws(const char *&line)
{
    while (is_ws(*line)) 
        ++ line;
}

// Parse an int starting at the current position of a line.
// If succeeded, the line pointer is advanced.
static inline int parse_int(const char *&line)
{
    char *endptr = nullptr;
    long result = strtol(line, &endptr, 10);
    if (endptr == nullptr || !is_ws_or_eol(*endptr))
        throw Slic3r::InvalidArgument("PressureEqualizer: Error parsing an int");
    line = endptr;
    return int(result);
}

float string_to_float_decimal_point(const char *line, const size_t str_len, size_t* pos)
{
    float out;
    size_t p = fast_float::from_chars(line, line + str_len, out).ptr - line;
    if (pos)
        *pos = p;
    return out;
}

// Parse an int starting at the current position of a line.
// If succeeded, the line pointer is advanced.
static inline float parse_float(const char *&line, const size_t line_length)
{
    size_t endptr = 0;
    auto   result = string_to_float_decimal_point(line, line_length, &endptr);
    if (endptr == 0 || !is_ws_or_eol(*(line + endptr)))
        throw Slic3r::RuntimeError("PressureEqualizer: Error parsing a float");
    line = line + endptr;
    return result;
}

// [INTENT] process_line — parse a single G-code line into a GCodeLine struct.
// Returns false for lines that are in-band tags (EXTRUSION_ROLE_TAG) that must be
// consumed to update state but should NOT appear in output. Returns true for all
// other lines (G/M/T codes, comments, blanks).
//
// Side effects on class state (persist across calls):
//   m_current_pos[5]  — updated for G0/G1/G92
//   m_current_extruder — updated for T-codes
//   m_current_extrusion_role — updated when EXTRUSION_ROLE_TAG is seen
//   m_retracted — updated on retract/unretract/tool-change
//   opened_extrude_set_speed_block — toggled by EXTRUDE_SET_SPEED_TAG / EXTRUDE_END_TAG
//
// [HAZARD] volumetric rate calculation (line ~438):
//   rate = A_filament * F_xyz * sqrt(dE²/dXYZ²)
//   This uses the feedrate from the CURRENT move (new_pos[4]), not the previous one.
//   If a G1 line omits F, new_pos[4] is copied from m_current_pos[4] (last seen F).
//   If no F has been seen at all, the rate is 0, which will never trigger smoothing.
//
// [COUPLING] adjustable_flow is set true only when opened_extrude_set_speed_block is
//   active. That flag is set by EXTRUDE_SET_SPEED_TAG lines injected by GCode.cpp.
//   This creates a tight coupling: the smoothing only works for lines that GCode.cpp
//   has explicitly tagged as smoothable.
bool PressureEqualizer::process_line(const char *line, const char *line_end, GCodeLine &buf)
{
    const size_t len = line_end - line;
    if (strncmp(line, EXTRUSION_ROLE_TAG.data(), EXTRUSION_ROLE_TAG.length()) == 0) {
        line += EXTRUSION_ROLE_TAG.length();
        int role = atoi(line);
        m_current_extrusion_role = ExtrusionRole(role);
#ifdef PRESSURE_EQUALIZER_DEBUG
        ++line_idx;
#endif
        return false;
    }

    // Set the type, copy the line to the buffer.
    buf.type = GCODELINETYPE_OTHER;
    buf.modified = false;
    if (buf.raw.size() < len + 1)
        buf.raw.assign(line, line + len + 1);
    else
        memcpy(buf.raw.data(), line, len);
    buf.raw[len] = 0;
    buf.raw_length = len;

    memcpy(buf.pos_start, m_current_pos, sizeof(float)*5);
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);
    memset(buf.pos_provided, 0, 5);

    buf.volumetric_extrusion_rate = 0.f;
    buf.volumetric_extrusion_rate_start = 0.f;
    buf.volumetric_extrusion_rate_end = 0.f;
    buf.max_volumetric_extrusion_rate_slope_positive = 0.f;
    buf.max_volumetric_extrusion_rate_slope_negative = 0.f;
	buf.extrusion_role = m_current_extrusion_role;

    std::string str_line(line, line_end);
    const bool found_extrude_set_speed_tag = boost::contains(str_line, EXTRUDE_SET_SPEED_TAG);
    const bool found_extrude_end_tag = boost::contains(str_line, EXTRUDE_END_TAG);
    assert(!found_extrude_set_speed_tag || !found_extrude_end_tag);

    if (found_extrude_set_speed_tag)
        this->opened_extrude_set_speed_block = true;
    else if (found_extrude_end_tag)
        this->opened_extrude_set_speed_block = false;

    // Parse the G-code line, store the result into the buf.
    switch (toupper(*line ++)) {
    case 'G': {
        int gcode = -1;
        try {
            gcode = parse_int(line);
        } catch (Slic3r::InvalidArgument &) {
            // Ignore invalid GCodes.
            eatws(line);
            break;
        }

        assert(gcode != -1);
        eatws(line);
        switch (gcode) {
        case 0:
        case 1:
        {
            // G0, G1: A FFF 3D printer does not make a difference between the two.
            buf.adjustable_flow = this->opened_extrude_set_speed_block;
            buf.extrude_set_speed_tag = found_extrude_set_speed_tag;
            buf.extrude_end_tag = found_extrude_end_tag;
            float new_pos[5];
            memcpy(new_pos, m_current_pos, sizeof(float)*5);
            bool  changed[5] = { false, false, false, false, false };
            while (!is_eol(*line)) {
                const char axis = toupper(*line++);
                int  i = -1;
                switch (axis) {
                case 'X':
                case 'Y':
                case 'Z':
                    i = axis - 'X';
                    break;
                case 'E':
                    i = 3;
                    break;
                case 'F':
                    i = 4;
                    break;
                default:
                    break;
                }
                if (i != -1) {
                    buf.pos_provided[i] = true;
                    new_pos[i] = parse_float(line, line_end - line);
                    if (i == 3 && m_use_relative_e_distances)
                        new_pos[i] += m_current_pos[i];
                    changed[i] = new_pos[i] != m_current_pos[i];
                    eatws(line);
                }
            }
            if (changed[3]) {
                // Extrusion, retract or unretract.
                float diff = new_pos[3] - m_current_pos[3];
                if (diff < 0) {
                    buf.type = GCODELINETYPE_RETRACT;
                    m_retracted = true;
                } else if (! changed[0] && ! changed[1] && ! changed[2]) {
                    // assert(m_retracted);
                    buf.type = GCODELINETYPE_UNRETRACT;
                    m_retracted = false;
                } else {
                    assert(changed[0] || changed[1]);
                    // Moving in XY plane.
                    buf.type = GCODELINETYPE_EXTRUDE;
                    // Calculate the volumetric extrusion rate.
                    float diff[4];
                    for (size_t i = 0; i < 4; ++ i)
                        diff[i] = new_pos[i] - m_current_pos[i];
                    // volumetric extrusion rate = A_filament * F_xyz * L_e / L_xyz [mm^3/min]
                    float len2 = diff[0]*diff[0]+diff[1]*diff[1]+diff[2]*diff[2];
                    float rate = m_filament_crossections[m_current_extruder] * new_pos[4] * sqrt((diff[3]*diff[3])/len2);
                    buf.volumetric_extrusion_rate       = rate;
                    buf.volumetric_extrusion_rate_start = rate;
                    buf.volumetric_extrusion_rate_end   = rate;

#ifdef PRESSURE_EQUALIZER_STATISTIC
                    m_stat.update(rate, sqrt(len2));
#endif
#ifdef PRESSURE_EQUALIZER_DEBUG
                    if (rate < 40.f) {
                        printf("Extremely low flow rate: %f. Line %d, Length: %f, extrusion: %f Old position: (%f, %f, %f), new position: (%f, %f, %f)\n",
                               rate, int(line_idx), sqrt(len2), sqrt((diff[3] * diff[3]) / len2), m_current_pos[0], m_current_pos[1], m_current_pos[2],
                               new_pos[0], new_pos[1], new_pos[2]);
                    }
#endif
                }
            } else if (changed[0] || changed[1] || changed[2]) {
                // Moving without extrusion.
                buf.type = GCODELINETYPE_MOVE;
            }
            memcpy(m_current_pos, new_pos, sizeof(float) * 5);
            break;
        }
        case 92: 
        {
            // G92 : Set Position
            // Set a logical coordinate position to a new value without actually moving the machine motors.
            // Which axes to set?
            while (!is_eol(*line)) {
                const char axis = toupper(*line++);
                switch (axis) {
                case 'X':
                case 'Y':
                case 'Z':
                    m_current_pos[axis - 'X'] = (!is_ws_or_eol(*line)) ? parse_float(line, line_end - line) : 0.f;
                    break;
                case 'E':
                    m_current_pos[3] = (!is_ws_or_eol(*line)) ? parse_float(line, line_end - line) : 0.f;
                    break;
                default:
                    break;
                }
                eatws(line);
            }
            break;
        }
        case 10:
        case 22:
            // Firmware retract.
            buf.type = GCODELINETYPE_RETRACT;
            m_retracted = true;
            break;
        case 11:
        case 23:
            // Firmware unretract.
            buf.type = GCODELINETYPE_UNRETRACT;
            m_retracted = false;
            break;
        default:
            // Ignore the rest.
        break;
        }
        break;
    }
    case 'M': {
        eatws(line);
        // Ignore the rest of the M-codes.
        break;
    }
    case 'T':
    {
        // Activate an extruder head.
        int new_extruder = -1;
        try {
            new_extruder = parse_int(line);
        } catch (Slic3r::InvalidArgument &) {
            // Ignore invalid GCodes starting with T.
            eatws(line);
            break;
        }
        assert(new_extruder != -1);

        if (new_extruder != int(m_current_extruder)) {
            m_current_extruder = new_extruder;
            m_retracted = true;
            buf.type = GCODELINETYPE_TOOL_CHANGE;
        } else {
            buf.type = GCODELINETYPE_NOOP;
        }
        break;
    }
    }

    buf.extruder_id = m_current_extruder;
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);
#ifdef PRESSURE_EQUALIZER_DEBUG
    ++line_idx;
#endif
    return true;
}

// [INTENT] output_gcode_line — serialise a single GCodeLine back to text in output_buffer.
// If the line was NOT modified by adjust_volumetric_rate(), it is emitted verbatim.
// If modified, the line is split into sub-segments to achieve a smooth speed ramp:
//
//   Case A — trivial rate change (delta < 10 mm³/min) or line too short for splitting:
//     Emit a single line with feedrate = original_feedrate * volumetric_correction_avg().
//
//   Case B — accel-peak-decel (both start and end rates < peak rate):
//     Compute the achievable peak rate using the quadratic formula:
//       target_max = sqrt((2*max_sloped*sp*sn + sn*e0² + sp*e1²) / (sp+sn))
//     If target_max is degenerate (≤ start or end), fall through to Case C (goto).
//     Otherwise split into: [accel slope segments] + [steady segment] + [decel slope segments].
//
//   Case C (single_slope_fallback) — monotone ramp (accel-only or decel-only):
//     Optionally emit a steady-feed prefix/suffix, then split the ramp into sub-segments
//     interpolating feedrate at the centre of each.
//
// [HAZARD] goto single_slope_fallback (line ~618): the only goto in the G-code pipeline.
//   It is a fallback from Case B when the peak rate is degenerate, jumping into Case C.
//   A refactor should replace this with an early-return into a helper function.
//
// [HAZARD] pos_start/pos_end are mutated in-place during segment emission (memcpy chains).
//   After output_gcode_line() returns, the GCodeLine struct is in an undefined intermediate
//   state. This is safe only because the function is the last consumer of the struct.
//
// [HAZARD] NON_TRIVIAL_RATE_DELTA = 10 mm³/min is a hardcoded heuristic threshold.
//   Below this threshold the line is emitted unsplit regardless of segment length.
void PressureEqualizer::output_gcode_line(const size_t line_idx)
{
    GCodeLine &line = m_gcode_lines[line_idx];
    if (!line.modified) {
        push_to_output(line.raw.data(), line.raw_length, true);
        return;
    }

    // The line was modified.
    // Find the comment.
    const char *comment = line.raw.data();
    while (*comment != ';' && *comment != 0) ++comment;
    if (*comment != ';')
        comment = nullptr;

    // get the gcode line length
    float l = line.dist_xyz();
    // number of segments this line can be broken down to
    auto nSegments = size_t(ceil(l / m_max_segment_length));
    
    // Orca:
    // Calculate the absolute difference in volumetric extrusion rate between the start and end point of the line.
    // Quantize it to 1mm3/min (0.016mm3/sec).
    int delta_volumetric_rate = std::round(std::max({
        fabs(line.volumetric_extrusion_rate_end - line.volumetric_extrusion_rate_start),
        // For line with accel-then-decel, we also calc the max difference to the peak
        fabs(line.volumetric_extrusion_rate - line.volumetric_extrusion_rate_start),
        fabs(line.volumetric_extrusion_rate - line.volumetric_extrusion_rate_end),
    }));
    
    // Emit the line with lowered extrusion rates.
    // Orca:
    // First, check if the change in volumetric extrusion rate is trivial (less than 10mm3/min -> 0.16mm3/sec (5mm/sec speed for a 0.25 mm nozzle).
    // Or if the line size is equal in length with the smallest segment.
    // If so, then emit the line as a single extrusion, i.e. dont split into segments.
    constexpr int NON_TRIVIAL_RATE_DELTA = 10;
    if (nSegments == 1 || delta_volumetric_rate < NON_TRIVIAL_RATE_DELTA) {
        push_line_to_output(line_idx, line.feedrate() * line.volumetric_correction_avg(), comment);
    } else // The line needs to be split the line into segments and apply extrusion rate smoothing
    {
        const float original_feedrate = line.feedrate();
        // Update the initial and final feed rate values.
        line.pos_start[4] = line.volumetric_extrusion_rate_start * line.pos_end[4] / line.volumetric_extrusion_rate;
        line.pos_end  [4] = line.volumetric_extrusion_rate_end   * line.pos_end[4] / line.volumetric_extrusion_rate;

        // Handle special case where both start & end extrusion rates are smaller than the original extrusion rate,
        // which means we need to do an accel-then-decel movements to achieve potentially max print speed
        if (line.volumetric_extrusion_rate > line.volumetric_extrusion_rate_start &&
            line.volumetric_extrusion_rate > line.volumetric_extrusion_rate_end) {
            // total extrusion amount of the original line
            const double original_extrusion = (double) l * line.volumetric_extrusion_rate / original_feedrate;

            // make sure there is a steady segment that's no shorter than `m_max_segment_length`
            const double min_steady_extrusion = original_extrusion * m_max_segment_length / l;
            const double max_sloped_extrusion = original_extrusion - min_steady_extrusion;
            assert(max_sloped_extrusion > 0);

            // Calculate the maximum possible peak extrusion rate
            // amount of extrusion if accelerate from volumetric_extrusion_rate_start to volumetric_extrusion_rate then
            // decelerate from volumetric_extrusion_rate to volumetric_extrusion_rate_end with max slope rate
            const auto   pow2              = [](const double x) { return x * x; };
            const double e_2               = pow2(line.volumetric_extrusion_rate);
            const double e0_2              = pow2(line.volumetric_extrusion_rate_start);
            const double e1_2              = pow2(line.volumetric_extrusion_rate_end);
            const double sp                = line.max_volumetric_extrusion_rate_slope_positive;
            const double sn                = line.max_volumetric_extrusion_rate_slope_negative;
            const double sloped_extrusion  = (e_2 - e0_2) / 2 / sp + (e_2 - e1_2) / 2 / sn;

            double target_max_extrusion_rate = line.volumetric_extrusion_rate;
            if (sloped_extrusion > max_sloped_extrusion) {
                // We don't have enough time to accel to max possible extrusion rate
                // now we calculate the actual possible value
                target_max_extrusion_rate = std::sqrt((2 * max_sloped_extrusion * sp * sn + sn * e0_2 + sp * e1_2) / (sp + sn));

                // Worst case: we don't have enough time to do an accl-steady-decel movement at all, fallback to the old fashion
                // single slope mode
                if (target_max_extrusion_rate <= line.volumetric_extrusion_rate_start ||
                    target_max_extrusion_rate <= line.volumetric_extrusion_rate_end) {
                    goto single_slope_fallback;  // TODO: FIXIT: better way than a goto?
                }
            }
            assert(target_max_extrusion_rate > line.volumetric_extrusion_rate_start);
            assert(target_max_extrusion_rate > line.volumetric_extrusion_rate_end);
            assert(target_max_extrusion_rate <= line.volumetric_extrusion_rate);

            // if the extrusion rate change is trivial, then ignore this algorithm and use the single sloped version instead
            delta_volumetric_rate = std::round(std::min({ // important! it's MIN here not max!
                fabs(target_max_extrusion_rate - line.volumetric_extrusion_rate_start),
                fabs(target_max_extrusion_rate - line.volumetric_extrusion_rate_end),
            }));

            if (delta_volumetric_rate >= NON_TRIVIAL_RATE_DELTA) {
                // we then have the target max feedrate when we reach target_max_extrusion_rate
                const double target_max_feedrate = original_feedrate * target_max_extrusion_rate / line.volumetric_extrusion_rate;
                assert(target_max_feedrate <= original_feedrate);

                // calculate the accel and deccel time & length
                const double t_acc = (target_max_extrusion_rate - line.volumetric_extrusion_rate_start) / sp;
                const double l_acc = t_acc * (target_max_feedrate + line.pos_start[4]) / 2;
                const double t_dec = (target_max_extrusion_rate - line.volumetric_extrusion_rate_end) / sn;
                const double l_dec = t_dec * (target_max_feedrate + line.pos_end[4]) / 2;

                float pos_end_bak[5];
                memcpy(pos_end_bak, line.pos_end, sizeof(float) * 5); // backup the final pos
                float pos_start[5];
                float pos_end[5];
                memcpy(pos_start, line.pos_start, sizeof(float) * 5);
                memcpy(pos_end, line.pos_end, sizeof(float) * 5);

                // calculate the end pos of the accel slope
                float t = l_acc / l;
                for (int i = 0; i < 4; ++i) {
                    pos_end[i]           = pos_start[i] + (pos_end_bak[i] - pos_start[i]) * t;
                    line.pos_provided[i] = true;
                }
                // emit accel slope in nSegments
                nSegments = size_t(ceil(l_acc / m_max_segment_length));
                assert(nSegments > 0);
                for (size_t i = 1; i <= nSegments; ++i) {
                    t = float(i) / float(nSegments);
                    for (size_t j = 0; j < 4; ++j) {
                        line.pos_end[j]      = pos_start[j] + (pos_end[j] - pos_start[j]) * t;
                        line.pos_provided[j] = true;
                    }
                    // Interpolate the feed rate at the center of the segment.
                    push_line_to_output(line_idx, pos_start[4] + (target_max_feedrate - pos_start[4]) * (float(i) - 0.5f) / float(nSegments), comment);
                    comment = nullptr;
                    memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
                }

                // calculate the end pos of the steady segment
                t = (l - l_dec) / l;
                for (int i = 0; i < 4; ++i) {
                    line.pos_end[i] = pos_start[i] + (pos_end_bak[i] - pos_start[i]) * t;
                }
                // emit the steady feed rate segment
                push_line_to_output(line_idx, target_max_feedrate, nullptr);
                memcpy(line.pos_start, line.pos_end, sizeof(float) * 5);

                // calculate the start pos of the decl slope
                memcpy(pos_start, line.pos_end, sizeof(float) * 5);
                line.pos_start[4] = target_max_feedrate;
                pos_start[4]      = target_max_feedrate;
                // emit deccel slope in nSegments
                nSegments = size_t(ceil(l_dec / m_max_segment_length));
                assert(nSegments > 0);
                for (size_t i = 1; i <= nSegments; ++ i) {
                    t = float(i) / float(nSegments);
                    for (size_t j = 0; j < 4; ++ j) {
                        line.pos_end[j] = pos_start[j] + (pos_end_bak[j] - pos_start[j]) * t;
                    } 
                    // Interpolate the feed rate at the center of the segment.
                    push_line_to_output(line_idx, pos_start[4] + (pos_end_bak[4] - pos_start[4]) * (float(i) - 0.5f) / float(nSegments), nullptr);
                    memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
                }

                // finish the movement by moving to end pos
                for (int i = 0; i < 4; ++i) {
                    line.pos_end[i] = pos_end_bak[i];
                }
                push_line_to_output(line_idx, pos_end_bak[4], nullptr);

                return;
            }
        }
single_slope_fallback:
        bool accelerating = line.volumetric_extrusion_rate_start < line.volumetric_extrusion_rate_end;
        float feed_avg = 0.5f * (line.pos_start[4] + line.pos_end[4]);
        // Limiting volumetric extrusion rate slope for this segment.
        float max_volumetric_extrusion_rate_slope = accelerating ? line.max_volumetric_extrusion_rate_slope_positive :
                                                                   line.max_volumetric_extrusion_rate_slope_negative;
        // Total time for the segment, corrected for the possibly lowered volumetric feed rate,
        // if accelerating / decelerating over the complete segment.
        float t_total = line.dist_xyz() / feed_avg;
        // Time of the acceleration / deceleration part of the segment, if accelerating / decelerating
        // with the maximum volumetric extrusion rate slope.
        float t_acc    = std::fabs(line.volumetric_extrusion_rate_start - line.volumetric_extrusion_rate_end) / max_volumetric_extrusion_rate_slope;
        float l_acc    = l;
        float l_steady = 0.f;
        if (t_acc < t_total) {
            // One may achieve higher print speeds if part of the segment is not speed limited.
            l_acc    = t_acc * feed_avg;
            l_steady = l - l_acc;
            if (l_steady < 0.5f * m_max_segment_length) {
                l_acc    = l;
                l_steady = 0.f;
            } else
                nSegments = size_t(ceil(l_acc / m_max_segment_length));
        }
        float pos_start[5];
        float pos_end[5];
        float pos_end2[4];
        memcpy(pos_start, line.pos_start, sizeof(float) * 5);
        memcpy(pos_end, line.pos_end, sizeof(float) * 5);
        if (l_steady > 0.f) {
            // There will be a steady feed segment emitted.
            if (accelerating) {
                // Prepare the final steady feed rate segment.
                memcpy(pos_end2, pos_end, sizeof(float)*4);
                float t = l_acc / l;
                for (int i = 0; i < 4; ++ i) {
                    pos_end[i] = pos_start[i] + (pos_end[i] - pos_start[i]) * t;
                    line.pos_provided[i] = true;
                }
            } else {
                // Emit the steady feed rate segment.
                float t = l_steady / l;
                for (int i = 0; i < 4; ++ i) {
                    line.pos_end[i] = pos_start[i] + (pos_end[i] - pos_start[i]) * t;
                    line.pos_provided[i] = true;
                }
                push_line_to_output(line_idx, pos_start[4], comment);
                comment = nullptr;

                float new_pos_start_feedrate = pos_start[4];

                memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
                memcpy(pos_start, line.pos_end, sizeof(float)*5);

                line.pos_start[4] = new_pos_start_feedrate;
                pos_start[4] = new_pos_start_feedrate;
            }
        }
        // Split the segment into pieces.
        for (size_t i = 1; i < nSegments; ++ i) {
            float t = float(i) / float(nSegments);
            for (size_t j = 0; j < 4; ++ j) {
                line.pos_end[j] = pos_start[j] + (pos_end[j] - pos_start[j]) * t;
                line.pos_provided[j] = true;
            } 
            // Interpolate the feed rate at the center of the segment.
            push_line_to_output(line_idx, pos_start[4] + (pos_end[4] - pos_start[4]) * (float(i) - 0.5f) / float(nSegments), comment);
            comment = nullptr;
            memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
        }
		if (l_steady > 0.f && accelerating) {
            for (int i = 0; i < 4; ++ i) {
                line.pos_end[i] = pos_end2[i];
                line.pos_provided[i] = true;
            }
            push_line_to_output(line_idx, pos_end[4], comment);
        } else {
            for (int i = 0; i < 4; ++ i) {
                line.pos_end[i] = pos_end[i];
                line.pos_provided[i] = true;
            }
            push_line_to_output(line_idx, pos_end[4], comment);
        }
    }
}

// [INTENT] adjust_volumetric_rate — the core pressure-smoothing algorithm.
// Operates on m_gcode_lines[first_line_idx..last_line_idx] in two passes:
//
//   BACKWARD PASS (deceleration constraint, lines ~800-845):
//     Walks from last_line_idx down to first_line_idx.
//     For each extrusion line, checks: given the rate at the START of the NEXT line
//     and the negative slope limit, what is the maximum allowable rate at the END of
//     this line? Clamps volumetric_extrusion_rate_end and propagates the constraint
//     backward via the kinematic formula:
//       rate_start = sqrt(rate_end² + 2 * rate * dist_xyz * slope / feedrate)
//     This ensures no abrupt speed increase seen by the extruder as it approaches
//     a slower segment.
//
//   FORWARD PASS (acceleration constraint, lines ~848-905):
//     Walks from first_line_idx up to last_line_idx.
//     Applies the positive slope limit symmetrically:
//       rate_end = sqrt(rate_start² + 2 * rate * dist_xyz * slope / feedrate)
//     Ensures the extruder cannot accelerate faster than the slope limit coming out
//     of a slow segment.
//
//   Per-role slope limits: m_max_volumetric_extrusion_rate_slopes[iRole].{positive,negative}
//   Each role can have independent slopes (e.g. external perimeter vs infill).
//   feedrate_per_extrusion_role[] tracks the most recently seen rate for each role to
//   allow cross-role slope constraints (deceleration before an upcoming slow role).
//
// [HAZARD] Bridge infill and ironing are explicitly skipped in both passes — their
//   flow rates are never modified. Ironing is also excluded from feedrate_per_extrusion_role
//   to prevent it from influencing adjacent segments.
//
// [CONCURRENCY] This function is called from the single-threaded process_layer() loop.
//   No TBB parallelism inside; the whole G-code pipeline step is single-threaded per plate.
//
// [UNCLEAR → RESOLVED] The cross-role propagator replaced the per-role-specific clamping
//   as an intentional improvement. The commented-out code stored `rate_start` (a role-specific
//   computed value) for non-matching roles and `line.volumetric_extrusion_rate_start` for the
//   matching role — this meant each role tracked its own "last seen rate" independently.
//   The active code unconditionally stores `line.volumetric_extrusion_rate_start` (the actual
//   constrained line rate) for ALL roles. This prevents under-deceleration at cross-role
//   transitions: when switching from a fast role to a slow role that hasn't been seen recently,
//   the old per-role tracker would use a stale (too-high) rate; the new code uses the current
//   line's actual rate, giving a more conservative and accurate deceleration constraint.
//   for cross-role propagation. The current strategy always uses
//   line.volumetric_extrusion_rate_start regardless of iRole match — the original
//   per-role tracking is preserved only in the active code path. Unclear if the
//   commented version was discarded intentionally or by accident.
void PressureEqualizer::adjust_volumetric_rate(const size_t first_line_idx, const size_t last_line_idx)
{
    // don't bother adjusting volumetric rate if there's no gcode to adjust
    if (last_line_idx - first_line_idx < 2)
        return;

    size_t line_idx = last_line_idx;
    if (line_idx == first_line_idx || !m_gcode_lines[line_idx].extruding())
        // Nothing to do, the last move is not extruding.
        return;

    std::array<float, size_t(ExtrusionRole::erCount)> feedrate_per_extrusion_role{};
    feedrate_per_extrusion_role.fill(std::numeric_limits<float>::max());
    feedrate_per_extrusion_role[int(m_gcode_lines[line_idx].extrusion_role)] = m_gcode_lines[line_idx].volumetric_extrusion_rate_start;

    while (line_idx != first_line_idx) {
        size_t idx_prev = line_idx - 1;
        for (; !m_gcode_lines[idx_prev].extruding() && idx_prev != first_line_idx; --idx_prev);
        if (!m_gcode_lines[idx_prev].extruding())
            break;
        // Don't decelerate before ironing.
        if (m_gcode_lines[line_idx].extrusion_role == ExtrusionRole::erIroning) {
            line_idx = idx_prev;
            continue;
        }
        // Volumetric extrusion rate at the start of the succeeding segment.
        float rate_succ = m_gcode_lines[line_idx].volumetric_extrusion_rate_start;
        // What is the gradient of the extrusion rate between idx_prev and idx?
        line_idx        = idx_prev;
        GCodeLine &line = m_gcode_lines[line_idx];

        for (size_t iRole = 1; iRole < size_t(ExtrusionRole::erCount); ++ iRole) {
            const float &rate_slope = m_max_volumetric_extrusion_rate_slopes[iRole].negative;
            if (rate_slope == 0 || feedrate_per_extrusion_role[iRole] == std::numeric_limits<float>::max())
                continue; // The negative rate is unlimited or the rate for ExtrusionRole iRole is unlimited.

            float rate_end = feedrate_per_extrusion_role[iRole];
            if (iRole == size_t(line.extrusion_role) && rate_succ < rate_end)
                // Limit by the succeeding volumetric flow rate.
                rate_end = rate_succ;

            // don't alter the flow rate for these extrusion types
            if (!line.adjustable_flow || line.extrusion_role == ExtrusionRole::erBridgeInfill || line.extrusion_role == ExtrusionRole::erIroning ||
                // Orca: Limit ERS to external perimeters and overhangs if option selected by user
                (m_extrusion_rate_smoothing_external_perimeter_only && line.extrusion_role != ExtrusionRole::erOverhangPerimeter && line.extrusion_role != ExtrusionRole::erExternalPerimeter)) {
                rate_end = line.volumetric_extrusion_rate_end;
            } else if (line.volumetric_extrusion_rate_end > rate_end) {
                line.volumetric_extrusion_rate_end = rate_end;
                line.max_volumetric_extrusion_rate_slope_negative = rate_slope;
                line.modified = true;
            } else if (iRole == size_t(line.extrusion_role)) {
                rate_end = line.volumetric_extrusion_rate_end;
            } else {
                // Use the original, 'floating' extrusion rate as a starting point for the limiter.
            }

            if (line.adjustable_flow) {
                float rate_start = sqrt(rate_end * rate_end + 2 * line.volumetric_extrusion_rate * line.dist_xyz() * rate_slope / line.feedrate());
                if (rate_start < line.volumetric_extrusion_rate_start) {
                    // Limit the volumetric extrusion rate at the start of this segment due to a segment
                    // of ExtrusionType iRole, which will be extruded in the future.
                    line.volumetric_extrusion_rate_start = rate_start;
                    line.max_volumetric_extrusion_rate_slope_negative = rate_slope;
                    line.modified = true;
                }
            }
//            feedrate_per_extrusion_role[iRole] = (iRole == line.extrusion_role) ? line.volumetric_extrusion_rate_start : rate_start;
            // Don't store feed rate for ironing
            if (line.extrusion_role != ExtrusionRole::erIroning)
                feedrate_per_extrusion_role[iRole] = line.volumetric_extrusion_rate_start;
        }
    }

    feedrate_per_extrusion_role.fill(std::numeric_limits<float>::max());
    feedrate_per_extrusion_role[size_t(m_gcode_lines[line_idx].extrusion_role)] = m_gcode_lines[line_idx].volumetric_extrusion_rate_end;

    assert(m_gcode_lines[line_idx].extruding());
    while (line_idx != last_line_idx) {
        size_t idx_next = line_idx + 1;
        for (; !m_gcode_lines[idx_next].extruding() && idx_next != last_line_idx; ++idx_next);
        if (!m_gcode_lines[idx_next].extruding())
            break;
        // Don't accelerate after ironing.
        if (m_gcode_lines[line_idx].extrusion_role == ExtrusionRole::erIroning) {
            line_idx = idx_next;
            continue;
        }
        float rate_prec = m_gcode_lines[line_idx].volumetric_extrusion_rate_end;
        // What is the gradient of the extrusion rate between idx_prev and idx?
        line_idx = idx_next;
        GCodeLine &line = m_gcode_lines[line_idx];

        for (size_t iRole = 1; iRole < size_t(ExtrusionRole::erCount); ++ iRole) {
            const float &rate_slope = m_max_volumetric_extrusion_rate_slopes[iRole].positive;
            if (rate_slope == 0 || feedrate_per_extrusion_role[iRole] == std::numeric_limits<float>::max())
                continue; // The positive rate is unlimited or the rate for ExtrusionRole iRole is unlimited.

            float rate_start = feedrate_per_extrusion_role[iRole];
            // don't alter the flow rate for these extrusion types
            if (!line.adjustable_flow || line.extrusion_role == ExtrusionRole::erBridgeInfill || line.extrusion_role == ExtrusionRole::erIroning ||
                // Orca: Limit ERS to external perimeters and overhangs if option selected by user
                (m_extrusion_rate_smoothing_external_perimeter_only && line.extrusion_role != ExtrusionRole::erOverhangPerimeter && line.extrusion_role != ExtrusionRole::erExternalPerimeter)) {
                rate_start = line.volumetric_extrusion_rate_start;
            } else if (iRole == size_t(line.extrusion_role) && rate_prec < rate_start)
                rate_start = rate_prec;

            if (line.volumetric_extrusion_rate_start > rate_start) {
                line.volumetric_extrusion_rate_start = rate_start;
                line.max_volumetric_extrusion_rate_slope_positive = rate_slope;
                line.modified = true;
            } else if (iRole == size_t(line.extrusion_role)) {
                rate_start = line.volumetric_extrusion_rate_start;
            } else {
                // Use the original, 'floating' extrusion rate as a starting point for the limiter.
            }

            if (line.adjustable_flow) {
                float rate_end = sqrt(rate_start * rate_start + 2 * line.volumetric_extrusion_rate * line.dist_xyz() * rate_slope / line.feedrate());
                if (rate_end < line.volumetric_extrusion_rate_end) {
                    // Limit the volumetric extrusion rate at the start of this segment due to a segment
                    // of ExtrusionType iRole, which was extruded before.
                    line.volumetric_extrusion_rate_end                = rate_end;
                    line.max_volumetric_extrusion_rate_slope_positive = rate_slope;
                    line.modified                                     = true;
                }
            }
//            feedrate_per_extrusion_role[iRole] = (iRole == line.extrusion_role) ? line.volumetric_extrusion_rate_end : rate_end;
            // Don't store feed rate for ironing
            if (line.extrusion_role != ExtrusionRole::erIroning)
                feedrate_per_extrusion_role[iRole] = line.volumetric_extrusion_rate_end;
        }
    }
}

// [INTENT] push_to_output — low-level append-to-output_buffer helpers.
// Three overloads: GCodeG1Formatter, std::string, and raw char* + length.
// All three ultimately call the char* overload which:
//   1. Ensures output_buffer is large enough using next-power-of-2 growth.
//   2. memcpy's the text at output_buffer_length.
//   3. Optionally appends '\n' (add_eol).
//   4. Always writes a NUL byte at output_buffer_length to maintain C-string invariant.
//
// [MEMORY] output_buffer only grows, never shrinks. After a large layer it retains
//   peak capacity for the rest of the print. This is intentional for performance but
//   means peak memory usage is proportional to the largest single layer.
//
// [STATE] output_buffer_prev_length tracks the start of the LAST appended chunk to
//   allow push_line_to_output() to erase a useless speed-only line (see below).
inline void PressureEqualizer::push_to_output(GCodeG1Formatter &formatter)
{
    return this->push_to_output(formatter.string(), false);
}

inline void PressureEqualizer::push_to_output(const std::string &text, bool add_eol)
{
    return this->push_to_output(text.data(), text.size(), add_eol);
}

inline void PressureEqualizer::push_to_output(const char *text, const size_t len, bool add_eol)
{
    // New length of the output buffer content.
    size_t len_new = output_buffer_length + len + 1;
    if (add_eol)
        ++len_new;

    // Resize the output buffer to a power of 2 higher than the required memory.
    if (output_buffer.size() < len_new) {
        size_t v = len_new;
        // Compute the next highest power of 2 of 32-bit v
        // http://graphics.stanford.edu/~seander/bithacks.html
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        output_buffer.resize(v);
    }

    // Copy the text to the output.
    if (len != 0) {
        memcpy(output_buffer.data() + output_buffer_length, text, len);
        this->output_buffer_prev_length = this->output_buffer_length;
        output_buffer_length += len;
    }
    if (add_eol)
        output_buffer[output_buffer_length++] = '\n';
    output_buffer[output_buffer_length] = 0;
}

inline bool is_just_line_with_extrude_set_speed_tag(const std::string &line)
{
    if (line.empty() && !boost::starts_with(line, "G1 ") && !boost::ends_with(line, EXTRUDE_SET_SPEED_TAG))
        return false;

    const char       *p_line   = line.data() + 3;
    const char *const line_end = line.data() + line.length() - 1;
    while (!is_eol(*p_line)) {
        if (toupper(*p_line++) == 'F')
            break;
        else
            return false;
    }
    parse_float(p_line, line_end - p_line);
    eatws(p_line);
    p_line += EXTRUDE_SET_SPEED_TAG.length();
    return p_line <= line_end && is_eol(*p_line);
}

// [INTENT] push_line_to_output — emit one G1 sub-segment with a new feedrate.
// Protocol for in-band tags:
//   1. Check if the previous output line was a "useless speed setter" (a G1 F... with
//      only EXTRUDE_SET_SPEED_TAG and no XY/E move). If so, erase it by rewinding
//      output_buffer_length to output_buffer_prev_length (O(1) rewind).
//      Otherwise emit EXTRUDE_END_TAG to close the previous speed block.
//   2. Emit a new "G1 F<new_feedrate> ;_EXTRUDE_SET_SPEED" line to open a new block,
//      also appending EXTERNAL_PERIMETER_TAG if the role is external perimeter.
//   3. Emit the actual G1 X Y Z E line for this sub-segment.
//
// [HAZARD] new_feedrate is quantized to 1 mm/s steps (rounded to nearest 60 mm/min)
//   and floored at 60 mm/min (1 mm/s). This reduces G-code volume but means that
//   very fine rate adjustments below 1 mm/s are silently discarded.
//
// [HAZARD] is_just_line_with_extrude_set_speed_tag() has a logic bug on its first
//   check: `line.empty() && !boost::starts_with(...)` — the empty() check should be
//   `!line.empty()`. In practice this bug is hidden because if the line is truly empty
//   the function returns false (correct), but the intent reads as "non-empty AND starts
//   with G1 AND ends with tag". A future maintainer may be confused.
void PressureEqualizer::push_line_to_output(const size_t line_idx, float new_feedrate, const char *comment)
{
    // Orca: sanity check, 1 mm/s is the minimum feedrate.
    if (new_feedrate < 60)
        new_feedrate = 60;
    // Quantize speed changes to a minimum of 1mm/sec, to reduce gcode volume for trivial speed changes.
    new_feedrate = std::round(new_feedrate / 60.0) * 60.0;
    const GCodeLine &line = m_gcode_lines[line_idx];
    if (line_idx > 0 && output_buffer_length > 0) {
        const std::string prev_line_str = std::string(output_buffer.begin() + int(this->output_buffer_prev_length),
                                                      output_buffer.begin() + int(this->output_buffer_length) + 1);
        if (is_just_line_with_extrude_set_speed_tag(prev_line_str))
            this->output_buffer_length = this->output_buffer_prev_length; // Remove the last line because it only sets the speed for an empty block of g-code lines, so it is useless.
        else
            push_to_output(EXTRUDE_END_TAG.data(), EXTRUDE_END_TAG.length(), true);
    } else
        push_to_output(EXTRUDE_END_TAG.data(), EXTRUDE_END_TAG.length(), true);

    GCodeG1Formatter feedrate_formatter;
    feedrate_formatter.emit_f(new_feedrate);
    feedrate_formatter.emit_string(std::string(EXTRUDE_SET_SPEED_TAG.data(), EXTRUDE_SET_SPEED_TAG.length()));
    if (line.extrusion_role == ExtrusionRole::erExternalPerimeter)
        feedrate_formatter.emit_string(std::string(EXTERNAL_PERIMETER_TAG.data(), EXTERNAL_PERIMETER_TAG.length()));
    push_to_output(feedrate_formatter);

    GCodeG1Formatter extrusion_formatter;
    for (size_t axis_idx = 0; axis_idx < 3; ++axis_idx)
        if (line.pos_provided[axis_idx])
            extrusion_formatter.emit_axis(char('X' + axis_idx), line.pos_end[axis_idx], GCodeFormatter::XYZF_EXPORT_DIGITS);
    extrusion_formatter.emit_axis('E', m_use_relative_e_distances ? (line.pos_end[3] - line.pos_start[3]) : line.pos_end[3], GCodeFormatter::E_EXPORT_DIGITS);

    if (comment != nullptr)
        extrusion_formatter.emit_string(std::string(comment));

    push_to_output(extrusion_formatter);
}

} // namespace Slic3r
