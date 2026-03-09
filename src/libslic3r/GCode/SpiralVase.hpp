// [INTENT] SpiralVase.hpp — G-code post-processor that converts a standard
// single-wall vase layer into a continuous spiral ramp. Instead of a flat
// perimeter at a fixed Z, the nozzle rises smoothly from the bottom to the
// top of the layer height across one full revolution.
//
// [STATE] Per-instance state:
//   m_reader       — GCodeReader tracking current X/Y/Z/E positions
//   m_enabled      — whether spiral mode is active for the current layer
//   m_transition_layer — first spiral layer; Z is ramped from 0 to target height
//   m_smooth_spiral — if true, XY is also interpolated with the previous layer
//                     to eliminate the visible seam at the layer change point
//   m_previous_layer — raw-pointer to heap-allocated vector of previous-layer
//                      XY points; used for smooth spiral interpolation [HAZARD H847]
//   m_max_xy_smoothing — max allowed XY displacement for smooth blending
//
// [HAZARD H847] m_previous_layer is a raw `new`/`delete` pointer. Ownership:
//   - process_layer() deletes it at the end and assigns a new `current_layer`
//   - If process_layer() throws, m_previous_layer leaks (no RAII guard)
//   - Should be std::unique_ptr<std::vector<SpiralPoint>>
//
// [CONCURRENCY] Not thread-safe. GCodeReader m_reader is mutated on every call.
// SpiralVase is owned by GCode.cpp and called single-threaded.
//
// [COUPLING] process_layer() must receive a complete single-layer G-code string
// (per the comment in .cpp: single Z move at the start, single closed loop).
// Calling with multi-layer or partial-layer G-code produces wrong Z ramping.

#ifndef slic3r_SpiralVase_hpp_
#define slic3r_SpiralVase_hpp_

#include "../libslic3r.h"
#include "../GCodeReader.hpp"

namespace Slic3r {

class SpiralVase
{
public:
    class SpiralPoint
    {
    public:
        SpiralPoint(float paramx, float paramy) : x(paramx), y(paramy) {}

    public:
        float x, y;
    };
    SpiralVase(const PrintConfig& config) : m_config(config)
    {
        m_reader.z() = (float) m_config.z_offset;
        m_reader.apply_config(m_config);
        m_previous_layer = NULL;
        m_smooth_spiral  = config.spiral_mode_smooth;
    };

    void enable(bool en)
    {
        m_transition_layer = en && !m_enabled;
        m_enabled          = en;
    }

    std::string process_layer(const std::string& gcode, bool last_layer);
    void        set_max_xy_smoothing(float max) { m_max_xy_smoothing = max; }

private:
    const PrintConfig& m_config;
    GCodeReader        m_reader;
    float              m_max_xy_smoothing = 0.f;

    bool m_enabled = false;
    // First spiral vase layer. Layer height has to be ramped up from zero to the target layer height.
    bool m_transition_layer = false;
    // Whether to interpolate XY coordinates with the previous layer. Results in no seam at layer changes
    bool                      m_smooth_spiral = false;
    std::vector<SpiralPoint>* m_previous_layer;
};
} // namespace Slic3r

#endif // slic3r_SpiralVase_hpp_
