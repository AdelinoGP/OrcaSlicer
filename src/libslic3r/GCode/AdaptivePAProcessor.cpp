// AdaptivePAProcessor.cpp
// OrcaSlicer
//
// Implementation of the AdaptivePAProcessor class, responsible for processing G-code layers with adaptive pressure advance.
//
// [INTENT] Adaptive Pressure Advance (APA) is an OrcaSlicer extension that dynamically adjusts the Klipper/Marlin
// pressure advance (PA) coefficient per-feature per-layer, based on the volumetric flow rate and acceleration at
// that feature. A per-tool PCHIP (Piecewise Cubic Hermite Interpolating Polynomial) model maps
// (flow_rate_mm3s, acceleration_mm_s2) → PA_coefficient. The model is calibrated from user-collected
// PA/flow/accel CSV data and stored in the `adaptive_pressure_advance_model` config key.
//
// [STATE] The processor is stateful across layers:
//   - m_last_predicted_pa: last emitted PA value (enables dedup suppression)
//   - m_current_feedrate: tracks most-recent G1 F feedrate seen in layer G-code
//   - m_last_extruder_id: tracks current extruder to detect toolchanges
// The processor holds a map of per-tool interpolator objects (m_AdaptivePAInterpolators).
//
// [COUPLING] Tightly coupled to GCode.hpp (must call gcodegen.config() and gcodegen.writer()).
// The processor assumes that GCode.cpp has injected "; PA_CHANGE:T<n> MM3MM:... ACCEL:... BR:... RC:... OV:..."
// comment tags into the G-code stream at every feature boundary where PA should be re-evaluated.
//
// [MEMORY] m_AdaptivePAInterpolators uses std::unique_ptr<AdaptivePAInterpolator> — clean RAII ownership.
// The m_config reference is non-owning; the PrintConfig object must outlive AdaptivePAProcessor.
// The m_gcodegen reference is non-owning (same lifetime constraint).

#include "../GCode.hpp"
#include "AdaptivePAProcessor.hpp"
#include <sstream>
#include <iostream>
#include <cmath>

namespace Slic3r {

/**
 * @brief Constructor for AdaptivePAProcessor.
 *
 * This constructor initializes the AdaptivePAProcessor with a reference to a GCode object.
 * It also initializes the configuration reference, pressure advance interpolation object,
 * and regular expression patterns used for processing the G-code.
 *
 * @param gcodegen A reference to the GCode object that generates the G-code.
 */
// [INTENT] Constructor builds per-tool interpolator models. Only tools with BOTH
// adaptive_pressure_advance AND enable_pressure_advance enabled get an interpolator.
// Tools without adaptive PA pass through to the static PA value from config.
//
// [HAZARD] m_pa_change_pattern and m_g1_f_pattern are compiled regex objects initialized
// in the constructor member initializer list. Construction cost is paid once per layer
// export (one AdaptivePAProcessor is created per GCode::do_export() call).
// If OrcaSlicer ever creates AdaptivePAProcessor objects per-layer, this becomes expensive.
//
// [HAZARD] If adaptive_pressure_advance_model contains malformed CSV data,
// AdaptivePAInterpolator::parseAndSetData() catches the exception and sets
// m_isInitialised = false. The constructor silently continues without the model.
// No error is surfaced to the user unless gcode_comments is enabled.
AdaptivePAProcessor::AdaptivePAProcessor(GCode& gcodegen, const std::vector<unsigned int>& tools_used)
    : m_gcodegen(gcodegen)
    , m_config(gcodegen.config())
    , m_last_predicted_pa(0.0)
    , m_max_next_feedrate(0.0)
    , m_next_feedrate(0.0)
    , m_current_feedrate(0.0)
    , m_last_extruder_id(-1)
    , m_pa_change_pattern(R"(; PA_CHANGE:T(\d+) MM3MM:([0-9]*\.[0-9]+) ACCEL:(\d+) BR:(\d+) RC:(\d+) OV:(\d+))")
    , m_g1_f_pattern(R"(G1 F([0-9]+))")
{
    // Constructor body can be used for further initialization if necessary
    for (unsigned int tool : tools_used) {
        // Only enable model for the tool if both PA and adaptive PA options are enabled
        if (m_config.adaptive_pressure_advance.get_at(tool) && m_config.enable_pressure_advance.get_at(tool)) {
            auto interpolator = std::make_unique<AdaptivePAInterpolator>();
            // Get calibration values from extruder
            std::string pa_calibration_values = m_config.adaptive_pressure_advance_model.get_at(tool);
            // Setup the model and store it in the tool-interpolation model map
            interpolator->parseAndSetData(pa_calibration_values);
            m_AdaptivePAInterpolators[tool] = std::move(interpolator);
        }
    }
}

// [INTENT] Returns the AdaptivePAInterpolator for the given tool ID, or nullptr if the tool
// does not have adaptive PA enabled (was not inserted during construction).
// Callers must null-check the return value before calling the interpolator.
//
// [COUPLING] The tool_id must match the 0-based extruder IDs used throughout GCode.cpp.
// If GCode.cpp ever uses 1-based IDs in PA_CHANGE tags, the lookup will always return nullptr.
AdaptivePAInterpolator* AdaptivePAProcessor::getInterpolator(unsigned int tool_id)
{
    auto it = m_AdaptivePAInterpolators.find(tool_id);
    if (it != m_AdaptivePAInterpolators.end()) {
        return it->second.get();
    }
    return nullptr; // Handle the case where the tool_id was not found
}

/**
 * @brief Processes a layer of G-code and applies adaptive pressure advance.
 *
 * This method processes the G-code for a single layer, identifying the appropriate
 * pressure advance settings and applying them based on the current state and configurations.
 *
 * @param gcode A string containing the G-code for the layer.
 * @return A string containing the processed G-code with adaptive pressure advance applied.
 */
// [INTENT] Main per-layer processing loop. Scans lines sequentially, tracking:
//   1. WIPE_START/WIPE_END to suppress feedrate updates during wipe moves.
//   2. "G1 F..." lines (feedrate-only moves) to maintain m_current_feedrate.
//   3. "; PA_CHANGE:..." tag lines injected by GCode.cpp at each feature boundary.
//
// On each PA_CHANGE tag found:
//   - Parses T(extruder), MM3MM (linear flow density), ACCEL (acceleration), BR (bridge flag),
//     RC (role change flag), OV (overhang flag).
//   - Performs a LOOKAHEAD scan forward in the stream to find the max print feedrate for
//     the upcoming feature island.
//   - Computes predicted_pa = interpolator(flow_rate_mm3s, accel).
//   - If predicted_pa differs from m_last_predicted_pa by more than EPSILON (or extruder changed),
//     emits a set_pressure_advance() G-code command.
//   - Lines that are not PA_CHANGE tags are passed through unchanged.
//
// [STATE] Mutates m_current_feedrate, m_max_next_feedrate, m_next_feedrate, m_last_extruder_id,
// m_last_predicted_pa across the loop iteration.
//
// [HAZARD] The lookahead scan advances stream.tellg() and then seeks back via stream.seekg().
// If the underlying string stream is very large and the platform's seekg() is slow
// (rare but possible on some STL implementations), this seek-per-PA_CHANGE-tag becomes
// O(n²) in the number of PA_CHANGE tags. For prints with many small features, this could
// be slow.
//
// [HAZARD] m_next_feedrate is reset to 0 on EVERY line inside the outer while loop (line 99),
// not just on PA_CHANGE lines. This means if two consecutive non-PA_CHANGE lines are processed,
// m_next_feedrate is cleared. This is intentional but fragile: any future refactor that adds
// a "continue" path before the reset could break the feedrate tracking logic.
//
// [HAZARD] The lookahead's "first command after PA_CHANGE" heuristic (line_counter==1) resets
// m_current_feedrate to the first G1 F found. This is correct when the PA_CHANGE tag is
// immediately followed by a G1 F (speed preamble before extrusion). If GCode.cpp ever emits
// a comment or non-G1-F line between the tag and the speed preamble, this reset fires on
// the SECOND G1 F instead of the first, silently using the wrong current speed.
//
// [HAZARD] `accel_value` is declared `unsigned int` but populated with std::stod() (line 112):
//   accel_value = std::stod(m_match[3].str());
// The double-to-unsigned int conversion truncates any fractional part silently. The PA_CHANGE
// pattern captures `\d+` (integers only) so this is harmless in current practice, but the
// type mismatch is a maintenance hazard.
//
// [HAZARD] For overhang regions, the speed selection is:
//   adaptive_PA_speed = min(m_current_feedrate, m_next_feedrate) IF both non-zero
//                     = max(m_current_feedrate, m_next_feedrate) IF either is zero
// This guards against using 0 as the speed (which would compute zero flow and give wrong PA),
// but the fallback to max() could give an overly high PA if m_next_feedrate is never found.
std::string AdaptivePAProcessor::process_layer(std::string&& gcode)
{
    std::istringstream stream(gcode);
    std::string        line;
    std::ostringstream output;
    double             mm3mm_value = 0.0;
    unsigned int       accel_value = 0;
    std::string        pa_change_line;
    bool               wipe_command = false;

    // Iterate through each line of the layer G-code
    while (std::getline(stream, line)) {
        // [INTENT] Gate feedrate tracking during wipe moves. Wipe moves typically use higher
        // feedrates than the print move they follow, so including them would bias PA high.
        // If a wipe start command is found, ignore all speed changes till the wipe end part is found
        if (line.find("WIPE_START") != std::string::npos) {
            wipe_command = true;
        }

        // [INTENT] Track the most-recent standalone feedrate command.
        // "G1 F..." (feedrate-only move) precedes extrude moves and establishes their speed.
        // Travel feedrate is output as part of a G1 X Y (Z) F command, not a standalone G1 F.
        // Update current feed rate (this is preceding an extrude or wipe command only). Ignore any speed changes that are emitted during a
        // wipe move. Travel feedrate is output as part of a G1 X Y (Z) F command
        if ((line.find("G1 F") == 0) && (!wipe_command)) { // prune lines quickly before running pattern matching
            std::size_t pos = line.find('F');
            if (pos != std::string::npos) {
                m_current_feedrate = std::stod(line.substr(pos + 1)) / 60.0; // Convert from mm/min to mm/s
            }
        }

        // Wipe end found, continue searching for current feed rate.
        if (line.find("WIPE_END") != std::string::npos) {
            wipe_command = false;
        }

        // [HAZARD] m_next_feedrate reset occurs on every line, not just on PA_CHANGE lines.
        // This means the "first feedrate after PA_CHANGE" tracking restarts each outer loop iteration.
        // Reset next feedrate to zero enable searching for the first encountered
        // feedrate change command after the PA change tag.
        m_next_feedrate = 0;

        // Check for PA_CHANGE pattern in the line
        // We will only find this pattern for extruders where adaptive PA is enabled.
        // If there is mixed extruders in the layer (i.e. with adaptive PA on and off
        // this will only update the extruders where the adaptive PA is enabled
        // as these are the only ones where the PA pattern is output
        // For a mixed extruder layer with both adaptive PA enabled and disabled when the new tool is selected
        // the PA for that material is set. As no tag below will be found for this extruder, the original PA is retained.
        if (line.find("; PA_CHANGE") == 0) { // prune lines quickly before running regex check as regex is more expensive to run
            if (std::regex_search(line, m_match, m_pa_change_pattern)) {
                int extruder_id = std::stoi(m_match[1].str());
                mm3mm_value     = std::stod(m_match[2].str());
                // [HAZARD] accel_value is unsigned int but populated via std::stod() — double-to-uint truncation.
                accel_value    = std::stod(m_match[3].str());
                int isBridge   = std::stoi(m_match[4].str());
                int roleChange = std::stoi(m_match[5].str());
                int isOverhang = std::stoi(m_match[6].str());

                // Check if the extruder ID has changed
                bool extruder_changed = (extruder_id != m_last_extruder_id);
                m_last_extruder_id    = extruder_id;

                // Save the PA_CHANGE line to output later after finding feedrate
                pa_change_line = line;

                // [INTENT] Lookahead scan: walk forward in the stream to find the max print speed
                // for the upcoming feature island without consuming lines permanently.
                // The scan stops at: (a) a travel move after an extrude move, (b) a WIPE command,
                // (c) a PA_CHANGE with RC=1 (role change), or (d) end-of-buffer.
                // After the scan, stream.seekg() restores the original position so the outer loop
                // continues from just after the PA_CHANGE line.
                // Look ahead for feedrate before any line containing both G and E commands
                std::streampos current_pos = stream.tellg();
                std::string    next_line;
                double         temp_feed_rate     = 0;
                bool           extrude_move_found = false;
                int            line_counter       = 0;

                // Carry on searching on the layer gcode lines to find the print speed
                // If a G1 Fxxxx pattern is found, the new speed is identified
                // Carry on searching for feedrates to find the maximum print speed
                // until a feature change pattern or a wipe command is detected
                while (std::getline(stream, next_line)) {
                    line_counter++;
                    // Found an extrude move, set extrude move found flag and move to the next line
                    if ((!extrude_move_found) && next_line.find("G1 ") == 0 && next_line.find('X') != std::string::npos &&
                        next_line.find('Y') != std::string::npos && next_line.find('E') != std::string::npos) {
                        // Pattern matched, break the loop
                        extrude_move_found = true;
                        continue;
                    }

                    // Found a travel move after we've found at least one extrude move
                    // We now need to stop searching for speeds as we're done printing this island
                    if (next_line.find("G1 ") == 0 && next_line.find('X') != std::string::npos && // X is present
                        next_line.find('Y') != std::string::npos &&                               // Y is present
                        next_line.find('E') == std::string::npos &&                               // no "E" present
                        extrude_move_found) {                                                     // An extrude move has happened already
                        // First travel move after extrude move found. Stop searching
                        break;
                    }

                    // Found a WIPE command
                    // If we have a wipe command, usually the wipe speed is different (larger) than the max print speed
                    // for that feature. So stop searching if a wipe command is found because we do not want to overwrite the
                    // speed used for PA calculation by the Wipe speed.
                    if (next_line.find("WIPE") != std::string::npos) {
                        break; // Stop searching if wipe command is found
                    }

                    // Found another PA_CHANGE pattern
                    // If RC = 1, it means we have a role change, so stop trying to find the max speed for the feature.
                    // This is possibly redundant as a new feature would always have a travel move preceding it
                    // but check anyway. However check last so to not invoke it without reason...
                    if (next_line.find("; PA_CHANGE") == 0) { // prune lines quickly before running pattern matching
                        std::size_t rc_pos = next_line.rfind("RC:");
                        if (rc_pos != std::string::npos) {
                            int rc_value = std::stoi(next_line.substr(rc_pos + 3));
                            if (rc_value == 1) {
                                break; // Role change found, stop searching
                            }
                        }
                    }

                    // Found a Feedrate change command
                    // If the new feedrate is greater than any feedrate encountered so far after the PA change command, use that to
                    // calculate the PA value Also if this is the first feedrate we encounter, store it as the next feedrate.
                    if (next_line.find("G1 F") == 0) { // prune lines quickly before running pattern matching
                        std::size_t pos = next_line.find('F');
                        if (pos != std::string::npos) {
                            double feedrate = std::stod(next_line.substr(pos + 1)) / 60.0; // Convert from mm/min to mm/s
                            if (line_counter == 1) { // this is the first command after the PA change pattern, and hence before any
                                                     // extrusion has happened. Reset the current speed to this one
                                m_current_feedrate = feedrate;
                            }
                            if (temp_feed_rate < feedrate) {
                                temp_feed_rate = feedrate;
                            }
                            if (m_next_feedrate < EPSILON) { // This the first feedrate found after the PA Change command
                                m_next_feedrate = feedrate;
                            }
                        }
                        continue;
                    }
                }

                // If we found a new maximum feedrate after the PA change command, use it
                if (temp_feed_rate > 0) {
                    m_max_next_feedrate = temp_feed_rate;
                } else // If we didnt find a new feedrate at all after the PA change command, use the current feedrate.
                    m_max_next_feedrate = m_current_feedrate;

                // [INTENT] Restore stream position to immediately after the PA_CHANGE line.
                // The lookahead scan consumed lines from the stream; seekg() rewinds them so
                // the outer while loop processes them normally.
                // Restore stream position
                stream.clear();
                stream.seekg(current_pos);

                // Calculate the predicted PA using the upcomming feature maximum feedrate
                // Get the interpolator for the active tool
                AdaptivePAInterpolator* interpolator = getInterpolator(m_last_extruder_id);

                double predicted_pa      = 0;
                double adaptive_PA_speed = 0;

                if (!interpolator) { // Tool not found in the interpolator map
                    // Tool not found in the PA interpolator to tool map
                    // [INTENT] Fallback to static PA value from config when no interpolator is available.
                    // This handles: tools with adaptive PA disabled, tools not in tools_used list.
                    predicted_pa = m_config.enable_pressure_advance.get_at(m_last_extruder_id) ?
                                       m_config.pressure_advance.get_at(m_last_extruder_id) :
                                       0;
                    if (m_config.gcode_comments)
                        output << "; APA: Tool doesnt have APA enabled\n";
                } else if (!interpolator->isInitialised() || (!m_config.adaptive_pressure_advance.get_at(m_last_extruder_id)))
                // Check if the model is not initialised by the constructor for the active extruder
                // Also check that adaptive PA is enabled for that extruder. This should not be needed
                // as the PA change flag should not be set upstream (in the GCode.cpp file) if adaptive PA is disabled
                // however check for robustness sake.
                {
                    // Model failed or adaptive pressure advance not enabled - use default value from m_config
                    predicted_pa = m_config.enable_pressure_advance.get_at(m_last_extruder_id) ?
                                       m_config.pressure_advance.get_at(m_last_extruder_id) :
                                       0;
                    if (m_config.gcode_comments)
                        output << "; APA: Interpolator setup failed, using default pressure advance\n";
                } else { // Model setup succeeded
                    // [INTENT] Speed selection for PA calculation:
                    // - Overhang (isOverhang > 0): use min of current/next speed, because overhangs
                    //   are typically cooled down and may print slower than the nominal feature speed.
                    //   The min() guards against layer cooling reducing the effective speed below m_next_feedrate.
                    // - Non-overhang: use max of current/next speed. The "island" may start with a
                    //   slow perimeter speed and ramp up to infill speed; we use the max to predict PA
                    //   at peak flow which is the most demanding condition.
                    // Proceed to identify the print speed to use to calculate the adaptive PA value
                    if (isOverhang > 0) { // If we are in an overhang area, use the minimum between current print speed
                                          // and any speed immediately after
                                          // In most cases the current speed is the minimum one;
                                          // however if slowdown for layer cooling is enabled, the overhang
                                          // may be slowed down more than the current speed.
                        adaptive_PA_speed = (m_current_feedrate == 0 || m_next_feedrate == 0) ?
                                                std::max(m_current_feedrate, m_next_feedrate) :
                                                std::min(m_current_feedrate, m_next_feedrate);
                    } else { // If this is not an overhang area, use the maximum speed from the current and
                             // upcomming speeds for the island.
                        adaptive_PA_speed = std::max(m_max_next_feedrate, m_current_feedrate);
                    }

                    // [INTENT] Flow rate in mm³/s = (mm³/mm linear flow density) × (mm/s print speed).
                    // mm3mm_value from the PA_CHANGE tag is the volumetric flow per unit distance (mm³/mm).
                    // Calculate the adaptive PA value
                    predicted_pa = (*interpolator)(mm3mm_value * adaptive_PA_speed, accel_value);

                    // [INTENT] Bridge override: if a dedicated bridge PA value is configured and non-zero,
                    // use it unconditionally regardless of the interpolated value.
                    // This is a hard override, not a blend. Users who want speed-adaptive bridge PA must
                    // leave adaptive_pressure_advance_bridges at 0.
                    // This is a bridge, use the dedicated PA setting.
                    if (isBridge && m_config.adaptive_pressure_advance_bridges.get_at(m_last_extruder_id) > EPSILON)
                        predicted_pa = m_config.adaptive_pressure_advance_bridges.get_at(m_last_extruder_id);

                    if (predicted_pa < 0) { // If extrapolation fails, fall back to the default PA for the extruder.
                        predicted_pa = m_config.enable_pressure_advance.get_at(m_last_extruder_id) ?
                                           m_config.pressure_advance.get_at(m_last_extruder_id) :
                                           0;
                        if (m_config.gcode_comments)
                            output << "; APA: Interpolation failed, using fallback pressure advance value\n";
                    }
                }
                if (m_config.gcode_comments) {
                    // Output debug GCode comments
                    output << pa_change_line << '\n'; // Output PA change command tag
                    if (isBridge && m_config.adaptive_pressure_advance_bridges.get_at(m_last_extruder_id) > EPSILON)
                        output << "; APA Model Override (bridge)\n";
                    output << "; APA Current Speed: " << std::to_string(m_current_feedrate) << "\n";
                    output << "; APA Next Speed: " << std::to_string(m_next_feedrate) << "\n";
                    output << "; APA Max Next Speed: " << std::to_string(m_max_next_feedrate) << "\n";
                    output << "; APA Speed Used: " << std::to_string(adaptive_PA_speed) << "\n";
                    // [HAZARD] This debug comment uses m_max_next_feedrate for flow but the actual
                    // PA calculation uses adaptive_PA_speed (which differs for overhang vs non-overhang).
                    // The "APA Flow rate" debug line may show a value different from what was actually used.
                    output << "; APA Flow rate: " << std::to_string(mm3mm_value * m_max_next_feedrate) << "\n";
                    output << "; APA Prev PA: " << std::to_string(m_last_predicted_pa) << " New PA: " << std::to_string(predicted_pa)
                           << "\n";
                }
                // [INTENT] Suppress redundant PA commands. A PA command is only emitted when:
                // (a) the extruder changed (toolchange forces re-emission even if PA is the same), or
                // (b) the new predicted PA differs from the last by more than floating-point epsilon.
                // This avoids cluttering G-code with M900/SET_PRESSURE_ADVANCE on every feature
                // boundary when the PA value hasn't changed meaningfully.
                if (extruder_changed || std::fabs(predicted_pa - m_last_predicted_pa) > EPSILON) {
                    output << m_gcodegen.writer().set_pressure_advance(predicted_pa); // Use m_writer to set pressure advance
                    m_last_predicted_pa = predicted_pa;                               // Update the last predicted PA value
                }
            }
        } else {
            // Output the current line as this isn't a PA change tag
            output << line << '\n';
        }
    }

    return output.str();
}

} // namespace Slic3r
