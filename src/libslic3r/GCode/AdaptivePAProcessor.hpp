// AdaptivePAProcessor.hpp
// OrcaSlicer
//
// Header file for the AdaptivePAProcessor class, responsible for processing G-code layers for the purposes of applying adaptive pressure
// advance.
//
// [INTENT] Adaptive Pressure Advance (APA) adjusts Klipper/Marlin pressure advance (PA) per-feature
// using a user-calibrated 2D interpolation model: PA = f(volumetric_flow_mm3s, acceleration_mm_s2).
// This post-processor scans each layer's G-code for "; PA_CHANGE:..." comment tags injected by
// GCode.cpp, performs a lookahead to find the feature's print speed, then emits an M900 (Klipper)
// or SET_PRESSURE_ADVANCE command with the interpolated PA value.
//
// [COUPLING] Depends on GCode.hpp (for GCode&, writer(), and config() access).
// Depends on AdaptivePAInterpolator.hpp for the 2D PCHIP interpolation model.
// Depends on PrintConfig for config keys: adaptive_pressure_advance, enable_pressure_advance,
//   adaptive_pressure_advance_model, adaptive_pressure_advance_bridges, pressure_advance.
//
// [STATE] This processor is stateful across layer calls. The following fields are updated
// during each process_layer() invocation and carry over to the next layer:
//   - m_last_predicted_pa: dedup gate to suppress redundant PA commands
//   - m_current_feedrate: most-recently observed G1 F feedrate (mm/s)
//   - m_last_extruder_id: active extruder (for toolchange detection)
// m_max_next_feedrate and m_next_feedrate are scratch variables reset inside each PA_CHANGE
// handling block (not persistent across layers).
//
// [MEMORY] m_AdaptivePAInterpolators holds std::unique_ptr<AdaptivePAInterpolator> — RAII,
// safe ownership. The m_gcodegen and m_config members are non-owning references;
// the GCode object and its PrintConfig must outlive this processor.

#ifndef ADAPTIVEPAPROCESSOR_H
#define ADAPTIVEPAPROCESSOR_H

#include <string>
#include <sstream>
#include <regex>
#include <memory>
#include <map>
#include <vector>
#include "AdaptivePAInterpolator.hpp"

namespace Slic3r {

// Forward declaration of GCode class
class GCode;

/**
 * @brief Class for processing G-code layers with adaptive pressure advance.
 */
class AdaptivePAProcessor
{
public:
    /**
     * @brief Constructor for AdaptivePAProcessor.
     *
     * This constructor initializes the AdaptivePAProcessor with a reference to a GCode object.
     * It also initializes the configuration reference, pressure advance interpolation object,
     * and regular expression patterns used for processing the G-code.
     *
     * @param gcodegen A reference to the GCode object that generates the G-code.
     */
    AdaptivePAProcessor(GCode& gcodegen, const std::vector<unsigned int>& tools_used);

    /**
     * @brief Processes a layer of G-code and applies adaptive pressure advance.
     *
     * This method processes the G-code for a single layer, identifying the appropriate
     * pressure advance settings and applying them based on the current state and configurations.
     *
     * @param gcode A string containing the G-code for the layer.
     * @return A string containing the processed G-code with adaptive pressure advance applied.
     */
    std::string process_layer(std::string&& gcode);

    /**
     * @brief Manually sets adaptive PA internal value.
     *
     * This method manually sets the adaptive PA internally held value.
     * Call this when changing tools or in any other case where the internally assumed last PA value may be incorrect
     */
    // [INTENT] resetPreviousPA() is called from GCode.cpp's set_extruder() path when a toolchange
    // occurs. Since the new extruder may have a different PA calibration model, the last-PA dedup
    // gate must be invalidated to force emission of the new extruder's PA value even if numerically
    // equal to the old extruder's PA.
    // [HAZARD] This is a public setter exposing internal state. If it is not called on every toolchange,
    // the PA command for the new tool may be suppressed (silent wrong-PA bug). The caller in GCode.cpp
    // must ensure it is called before the new layer's process_layer() is invoked.
    void resetPreviousPA(double PA) { m_last_predicted_pa = PA; };

private:
    GCode& m_gcodegen; ///< Reference to the GCode object.
    // [STATE] Per-tool interpolator map. Only tools with adaptive PA + PA enabled have entries.
    // Tools not in this map fall back to the static pressure_advance config value.
    std::unordered_map<unsigned int, std::unique_ptr<AdaptivePAInterpolator>>
                       m_AdaptivePAInterpolators; ///< Map between Interpolator objects and tool ID's
    const PrintConfig& m_config;                  ///< Reference to the print configuration.
    double             m_last_predicted_pa;       ///< Last predicted pressure advance value.
    // [STATE] m_max_next_feedrate: maximum feedrate found in the lookahead window after the PA_CHANGE tag.
    // m_next_feedrate: FIRST feedrate found after the PA_CHANGE tag (before any extrusion).
    // Both are reset per-PA_CHANGE; not persistent between PA_CHANGE tags within the same layer.
    double
        m_max_next_feedrate; ///< Maximum feed rate (speed) for the upcomming island. If no speed is found, the previous island speed is used.
    double m_next_feedrate;    ///< First feed rate (speed) for the upcomming island.
    double m_current_feedrate; ///< Current, latest feedrate.
    int    m_last_extruder_id; ///< Last used extruder ID.

    // [HAZARD] m_pa_change_pattern and m_g1_f_pattern are std::regex objects compiled once
    // in the constructor. Regex compilation is expensive; keeping them as class members avoids
    // per-line compilation. However, std::regex objects are NOT thread-safe for concurrent
    // use of the same instance. AdaptivePAProcessor is single-threaded (called from GCode.cpp's
    // serial TBB pipeline stage), so this is safe in current usage.
    std::regex  m_pa_change_pattern; ///< Regular expression to detect PA_CHANGE pattern.
    std::regex  m_g1_f_pattern;      ///< Regular expression to detect G1 F pattern.
    std::smatch m_match;             ///< Match results for regular expressions.

    /**
     * @brief Get the PA interpolator attached to the specified tool ID.
     *
     * This method manually sets the adaptive PA internally held value.
     * Call this when changing tools or in any other case where the internally assumed last PA value may be incorrect
     *
     * @param An integer with the tool ID for which the PA interpolation model is to be returned.
     * @return The Adaptive PA Interpolator object corresponding to that tool.
     */
    // [HAZARD] Returns raw pointer into the unique_ptr-owned interpolator. The caller must not
    // store this pointer beyond the scope of the current process_layer() call — if the
    // AdaptivePAProcessor is destroyed, the raw pointer dangles.
    AdaptivePAInterpolator* getInterpolator(unsigned int tool_id);
};

} // namespace Slic3r

#endif // ADAPTIVEPAPROCESSOR_H
