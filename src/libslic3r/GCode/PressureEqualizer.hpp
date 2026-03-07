#ifndef slic3r_GCode_PressureEqualizer_hpp_
#define slic3r_GCode_PressureEqualizer_hpp_

// [INTENT] PressureEqualizer header — defines the G-code post-processor that enforces
// a maximum volumetric extrusion rate slope (mm³/s²) to prevent abrupt pressure changes
// in the hot end. It is inserted in the TBB pipeline in GCode.cpp between the per-layer
// G-code generator and the CoolingBuffer.
//
// [STATE] The class is stateful across process_layer() calls:
//   - m_current_pos[5]  (XYZEF) persists across layers
//   - m_gcode_lines     persists across layers (1-layer lookahead buffer)
//   - m_layer_results   public raw-pointer queue — see [COUPLING] below
//
// [COUPLING] m_layer_results and m_gcode_lines are declared public to allow direct access
//   by the TBB pipeline in GCode.cpp. This bypasses encapsulation and creates a hard
//   coupling between PressureEqualizer and the pipeline runner.
//
// [HAZARD] m_layer_results is a std::queue<LayerResult*> — raw owning pointers.
//   The caller (GCode.cpp) must inject a NOP LayerResult at end-of-print to flush the
//   last real layer. Failure to do so causes a memory leak and a missing last layer.
//
// [HAZARD] GCodeLine::volumetric_correction_avg() is capped to [0.05, 1.00000001].
//   The upper bound > 1.0 suggests the intent is to allow tiny floating-point rounding
//   above 1.0 without asserting. Any correction above 1.0 would increase the feedrate,
//   which contradicts the purpose of the smoother (rates should only be lowered, not raised).
//
// Debug compile flags (both off by default):
//   PRESSURE_EQUALIZER_STATISTIC — tracks min/max/avg volumetric rate per layer
//   PRESSURE_EQUALIZER_DEBUG     — prints anomalously low flow rates to stdout

#include "../libslic3r.h"
#include "../PrintConfig.hpp"

#include <queue>

namespace Slic3r {

struct LayerResult;

class GCodeG1Formatter;

//#define PRESSURE_EQUALIZER_STATISTIC
//#define PRESSURE_EQUALIZER_DEBUG

// Processes a G-code. Finds changes in the volumetric extrusion speed and adjusts the transitions
// between these paths to limit fast changes in the volumetric extrusion speed.
class PressureEqualizer
{
public:
    PressureEqualizer() = delete;
    explicit PressureEqualizer(const Slic3r::GCodeConfig &config);
    ~PressureEqualizer() = default;

    // Process a next batch of G-code lines.
    // The last LayerResult must be LayerResult::make_nop_layer_result() because it always returns GCode for the previous layer.
    // When process_layer is called for the first layer, then LayerResult::make_nop_layer_result() is returned.
    LayerResult process_layer(LayerResult &&input);
private:

    void process_layer(const std::string &gcode);

#ifdef PRESSURE_EQUALIZER_STATISTIC
    struct Statistics
    {
        void reset()
        {
            volumetric_extrusion_rate_min = std::numeric_limits<float>::max();
            volumetric_extrusion_rate_max = 0.f;
            volumetric_extrusion_rate_avg = 0.f;
            extrusion_length              = 0.f;
        }
        void update(float volumetric_extrusion_rate, float length)
        {
            volumetric_extrusion_rate_min  = std::min(volumetric_extrusion_rate_min, volumetric_extrusion_rate);
            volumetric_extrusion_rate_max  = std::max(volumetric_extrusion_rate_max, volumetric_extrusion_rate);
            volumetric_extrusion_rate_avg += volumetric_extrusion_rate * length;
            extrusion_length              += length;
        }
        float volumetric_extrusion_rate_min;
        float volumetric_extrusion_rate_max;
        float volumetric_extrusion_rate_avg;
        float extrusion_length;
    };

    struct Statistics m_stat;
#endif

    // [INTENT] Per-role slope limits (mm³/min² in stored form; converted from mm³/s² in ctor).
    // Each ExtrusionRole can have independent acceleration and deceleration limits.
    // Roles not explicitly configured keep 0.0 (unlimited).
    // m_max_volumetric_extrusion_rate_slope_{positive,negative} are the global fallback limits
    // derived from config.max_volumetric_extrusion_rate_slope.
    struct ExtrusionRateSlope {
        float positive;
        float negative;
    };
    ExtrusionRateSlope              m_max_volumetric_extrusion_rate_slopes[size_t(ExtrusionRole::erCount)];
    float                           m_max_volumetric_extrusion_rate_slope_positive;
    float                           m_max_volumetric_extrusion_rate_slope_negative;

    // Configuration extracted from config.
    // Area of the crossestion of each filament. Necessary to calculate the volumetric flow rate.
    std::vector<float>              m_filament_crossections;

    // [STATE] Persistent G-code machine state. Updated by process_line() for every G0/G1/G92/T.
    // X,Y,Z,E,F
    float                           m_current_pos[5];
    size_t                          m_current_extruder;
    ExtrusionRole     m_current_extrusion_role;
    bool                            m_retracted;
    bool                            m_use_relative_e_distances;

	// Maximum segment length to split a long segment if the initial and the final flow rate differ.
	// Smaller value means a smoother transition between two different flow rates.
    float                           m_max_segment_length;
    
    // Apply ERS only on external perimeters and overhangs
    bool                           m_extrusion_rate_smoothing_external_perimeter_only;

    // Indicate if extrude set speed block was opened using the tag ";_EXTRUDE_SET_SPEED"
    // or not (not opened, or it was closed using the tag ";_EXTRUDE_END").
    bool                            opened_extrude_set_speed_block = false;

    enum GCodeLineType {
        GCODELINETYPE_INVALID,
        GCODELINETYPE_NOOP,
        GCODELINETYPE_OTHER,
        GCODELINETYPE_RETRACT,
        GCODELINETYPE_UNRETRACT,
        GCODELINETYPE_TOOL_CHANGE,
        GCODELINETYPE_MOVE,
        GCODELINETYPE_EXTRUDE,
    };

    // [INTENT] GCodeLine — parsed representation of a single G-code line, enriched with
    // volumetric rate bookkeeping for the pressure-smoothing algorithm.
    //
    // Key fields for smoothing:
    //   volumetric_extrusion_rate        — nominal rate for the whole line (mm³/min)
    //   volumetric_extrusion_rate_start  — rate at the start of this segment (may be lowered)
    //   volumetric_extrusion_rate_end    — rate at the end of this segment (may be lowered)
    //   max_volumetric_extrusion_rate_slope_{positive,negative} — which slope limit was binding
    //   adjustable_flow — true only if inside a EXTRUDE_SET_SPEED block; only these lines
    //                     get their feedrate modified during output.
    //   modified — set true by adjust_volumetric_rate() when start/end rates were clamped.
    //
    // [MEMORY] raw is a vector<char> kept persistent to avoid repeated allocation;
    //   raw_length is the valid content length (raw.size() may be larger).
    //
    // [HAZARD] volumetric_correction_avg() upper-bound assert is 1.00000001 to tolerate
    //   floating-point rounding. Any value above 1.0 would increase feedrate, which is
    //   never the intent of the smoother.
    struct GCodeLine
    {
        GCodeLine() : 
            type(GCODELINETYPE_INVALID),
            raw_length(0),
            modified(false),
            extruder_id(0), 
            volumetric_extrusion_rate(0.f), 
            volumetric_extrusion_rate_start(0.f), 
            volumetric_extrusion_rate_end(0.f) 
            {}

        bool        moving_xy()     const { return fabs(pos_end[0] - pos_start[0]) > 0.f || fabs(pos_end[1] - pos_start[1]) > 0.f; }
        bool        moving_z ()     const { return fabs(pos_end[2] - pos_start[2]) > 0.f; }
        bool        extruding()     const { return moving_xy() && pos_end[3] > pos_start[3]; }
        bool        retracting()    const { return pos_end[3] < pos_start[3]; }
        bool        deretracting()  const { return ! moving_xy() && pos_end[3] > pos_start[3]; }

        float       dist_xy2()      const { return (pos_end[0] - pos_start[0]) * (pos_end[0] - pos_start[0]) + (pos_end[1] - pos_start[1]) * (pos_end[1] - pos_start[1]); }
        float       dist_xyz2()     const { return (pos_end[0] - pos_start[0]) * (pos_end[0] - pos_start[0]) + (pos_end[1] - pos_start[1]) * (pos_end[1] - pos_start[1]) + (pos_end[2] - pos_start[2]) * (pos_end[2] - pos_start[2]); }
        float       dist_xy()       const { return sqrt(dist_xy2()); }
        float       dist_xyz()      const { return sqrt(dist_xyz2()); }
        float       dist_e()        const { return fabs(pos_end[3] - pos_start[3]); }

        float       feedrate()      const { return pos_end[4]; }
        float       time()          const { return dist_xyz() / feedrate(); }
        float       time_inv()      const { return feedrate() / dist_xyz(); }
        float       volumetric_correction_avg() const { 
        // Orca: cap the correction to 0.05 - 1.00000001 to avoid zero feedrate
            float avg_correction = std::max(0.05f,0.5f * (volumetric_extrusion_rate_start + volumetric_extrusion_rate_end) / volumetric_extrusion_rate); 
            assert(avg_correction > 0.f);
            assert(avg_correction <= 1.00000001f);
            return avg_correction;
        }

        GCodeLineType type;

        // We try to keep the string buffer once it has been allocated, so it will not be reallocated over and over.
        std::vector<char>   raw;
        size_t              raw_length;
        // If modified, the raw text has to be adapted by the new extrusion rate,
        // or maybe the line needs to be split into multiple lines.
        bool                modified;

        // X,Y,Z,E,F. Storing the state of the currently active extruder only.
        float       pos_start[5];
        float       pos_end[5];
        // Was the axis found on the G-code line? X,Y,Z,E,F
        bool        pos_provided[5];

        // Index of the active extruder.
        size_t      extruder_id;
        // Extrusion role of this segment.
        ExtrusionRole extrusion_role;

        // Current volumetric extrusion rate.
        float       volumetric_extrusion_rate;
        // Volumetric extrusion rate at the start of this segment.
        float       volumetric_extrusion_rate_start;
        // Volumetric extrusion rate at the end of this segment.
        float       volumetric_extrusion_rate_end;

        // Volumetric extrusion rate slope limiting this segment.
        // If set to zero, the slope is unlimited.
        float       max_volumetric_extrusion_rate_slope_positive;
        float       max_volumetric_extrusion_rate_slope_negative;

        bool        adjustable_flow       = false;

        bool        extrude_set_speed_tag = false;
        bool        extrude_end_tag       = false;
    };

    // Output buffer will only grow. It will not be reallocated over and over.
    std::vector<char>               output_buffer;
    size_t                          output_buffer_length;
    size_t                          output_buffer_prev_length;

#ifdef PRESSURE_EQUALIZER_DEBUG
    // For debugging purposes. Index of the G-code line processed.
    size_t                          line_idx;
#endif

    bool process_line(const char *line, const char *line_end, GCodeLine &buf);
    long advance_segment_beyond_small_gap(long idx_cur_pos);
    void output_gcode_line(size_t line_idx);

    // Go back from the current circular_buffer_pos and lower the feedtrate to decrease the slope of the extrusion rate changes.
    // Then go forward and adjust the feedrate to decrease the slope of the extrusion rate changes.
    void adjust_volumetric_rate(size_t first_line_idx, size_t last_line_idx);

    // Push the text to the end of the output_buffer.
    inline void push_to_output(GCodeG1Formatter &formatter);
    inline void push_to_output(const std::string &text, bool add_eol);
    inline void push_to_output(const char *text, size_t len, bool add_eol = true);
    // Push a G-code line to the output.
    void push_line_to_output(size_t line_idx, float new_feedrate, const char *comment);

public:
    // [COUPLING] Public raw-pointer queue. Ownership of LayerResult* is held here;
    // caller is responsible for injecting a NOP at end-of-print to flush the last layer.
    // Making this public is necessary for the TBB pipeline in GCode.cpp.
    std::queue<LayerResult*> m_layer_results;

    // [COUPLING] Public parsed-line buffer. Exposed for the TBB pipeline to inspect
    // pending lines. Grows during process_layer(string) and is pruned after emission.
    std::vector<GCodeLine> m_gcode_lines;
};

} // namespace Slic3r

#endif /* slic3r_GCode_PressureEqualizer_hpp_ */
