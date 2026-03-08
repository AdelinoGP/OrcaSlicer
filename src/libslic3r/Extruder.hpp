// [INTENT] Declares the Extruder class, which models the per-extruder state machine for
// extrusion, retraction, and deretraction during G-code generation. One Extruder instance
// is created per physical extruder by GCodeWriter.
// [COUPLING] Tightly coupled to GCodeConfig (runtime reference, not a copy). All filament
// properties are read live from config on each call, so a config change mid-print would
// take effect immediately (though in practice config is frozen before slicing).

#ifndef slic3r_Extruder_hpp_
#define slic3r_Extruder_hpp_

#include "libslic3r.h"
#include "Point.hpp"

namespace Slic3r {

class GCodeConfig;

// [INTENT] Tracks extrusion state for a single extruder during G-code generation.
// Manages the E-axis position, retraction state, and statistics (filament used/volume).
// [STATE]
//   m_E          — current E-axis value emitted to G-code (reset to 0 each move if relative E)
//   m_absolute_E — monotonically increasing total extrusion (never decremented by reset)
//   m_retracted  — current retraction amount; always >= 0
//   m_restart_extra — extra prime distance planned for the next unretract
//   m_e_per_mm3  — cached conversion factor (flow_ratio / crosssection_area); computed at construction
// [CONCURRENCY] Not thread-safe. GCodeWriter serializes all calls from a single thread.
// [COUPLING] BBS "share extruder" mode: single-extruder machines with multi-material use
// static class-level m_share_E and m_share_retracted vectors shared across all instances.
// [HAZARD] H646: m_share_E and m_share_retracted are static class members, meaning they are
// global mutable state shared across all Extruder instances in the same process. If multiple
// Print jobs run concurrently (e.g., batch CLI), their extruder states will corrupt each other.
class Extruder
{
public:
    Extruder(unsigned int id, GCodeConfig* config, bool share_extruder);
    virtual ~Extruder() {}

    // [INTENT] Resets E-axis and retraction state to zero. Called at the start of each layer
    // in relative-E mode. `m_absolute_E` is NOT reset — it accumulates across the whole print.
    void reset()
    {
        // BBS
        if (m_share_extruder) {
            m_share_E         = std::vector<double>(MAXIMUM_EXTRUDER_NUMBER, 0);
            m_share_retracted = std::vector<double>(MAXIMUM_EXTRUDER_NUMBER, 0);
        } else {
            m_E         = 0;
            m_retracted = 0;
        }
        m_restart_extra = 0;
        m_absolute_E    = 0;
    }

    // [INTENT] Print-wide global ID (0-based). Used for config lookups via get_at(m_id).
    unsigned int id() const { return m_id; }

    // [INTENT] Maps m_id to the actual physical filament slot via filament_map config.
    // Returns 0 if m_id is out of range. The -1 in the implementation compensates for
    // 1-based filament_map values.
    unsigned int extruder_id() const;

    // [INTENT] Advances the E axis by dE (positive = extrude, negative = retract).
    // In relative mode, m_E is reset to 0 before accumulating (so G-code always emits relative delta).
    // m_retracted is incremented (note: decremented in the implementation — negative dE decrements
    // m_retracted by subtracting a negative, i.e., increases retraction counter).
    // [HAZARD] H647: When dE < 0 (retracting via extrude()), m_retracted is decremented (double-negative):
    // `m_retracted -= dE` with dE < 0 means m_retracted increases. This is a retraction tracked
    // via extrude(), which bypasses the idempotency guard in retract(). Use retract() for
    // controlled retraction to avoid over-retracting.
    double extrude(double dE);

    // [INTENT] Retracts by `length` mm, but only if not already retracted by that much.
    // Idempotent: calling retract(3.0) twice leaves m_retracted at 3.0, not 6.0.
    // Sets m_restart_extra regardless of whether retraction was needed.
    double retract(double length, double restart_extra);

    // [INTENT] Unretracts: extrudes m_retracted + m_restart_extra to prime the nozzle.
    // Resets m_retracted and m_restart_extra to zero.
    double unretract();

    // [INTENT] Returns current E-axis position. For shared-extruder machines, returns
    // the shared slot's E value, NOT m_E (which may be stale).
    double E() const { return m_share_extruder ? m_share_E[extruder_id()] : m_E; }
    // [HAZARD] H648: reset_E() always resets BOTH m_E and m_share_E[extruder_id()], even in
    // non-share-extruder mode. For non-shared extruders, m_share_E is not used, so the write
    // is harmless but misleading. Could cause confusion in share-extruder mode if extruder_id()
    // returns wrong index.
    void reset_E()
    {
        m_E                      = 0.;
        m_share_E[extruder_id()] = 0.;
    }
    // e_per_mm is extrusion_per_mm = geometric volume * (filament flow ratio / cross-sectional area)  [Doesn't account for
    // print_flow_ratio, or modifiers like bridge flow ratio etc.]
    double e_per_mm(double mm3_per_mm) const { return mm3_per_mm * m_e_per_mm3; }
    // e_per_mm3 is extrusion_per_mm3 = filament flow ratio / cross-sectional area    [Doesn't account for print_flow_ratio, or modifiers
    // like bridge flow ratio etc.]
    double e_per_mm3() const { return m_e_per_mm3; }
    // Used filament volume in mm^3.
    double extruded_volume() const;
    // Used filament length in mm.
    double used_filament() const;

    // Getters for the PlaceholderParser.
    // Get current extruder position. Only applicable with absolute extruder addressing.
    double position() const { return m_E; }
    // Get current retraction value. Only non-negative values.
    double retracted() const { return m_retracted; }
    // Get extra retraction planned after
    double restart_extra() const { return m_restart_extra; }
    // Setters for the PlaceholderParser.
    // Set current extruder position. Only applicable with absolute extruder addressing.
    void set_position(double e) { m_E = e; }
    // Sets current retraction value & restart extra filament amount if retracted > 0.
    void set_retracted(double retracted, double restart_extra);

    // [INTENT] All property getters below forward to GCodeConfig via get_at(m_id).
    // [HAZARD] H649: filament_diameter() and filament_flow_ratio() use m_id for lookup,
    // but retract_length_toolchange() and retract_restart_extra_toolchange() use extruder_id()
    // (the mapped physical slot index). This inconsistency means toolchange retraction values
    // are indexed by the physical slot, while regular retraction values are indexed by the
    // logical extruder number. A port must replicate this asymmetry exactly.
    double filament_diameter() const;
    double filament_crossection() const { return this->filament_diameter() * this->filament_diameter() * 0.25 * PI; }
    double filament_density() const;
    double filament_cost() const;
    double filament_flow_ratio() const;
    double retract_before_wipe() const;
    double retraction_length() const;
    double retract_lift() const;
    int    retract_speed() const;
    int    deretract_speed() const;
    double retract_restart_extra() const;
    double retract_length_toolchange() const;
    double retract_restart_extra_toolchange() const;
    double travel_slope() const;

    bool use_firmware_retraction() const;

private:
    // Private constructor to create a key for a search in std::set.
    Extruder(unsigned int id) : m_id(id) {}

    // Reference to GCodeWriter instance owned by GCodeWriter.
    // [HAZARD] H650: This is a raw non-owning pointer. If the owning GCodeWriter (or GCodeConfig)
    // is destroyed while an Extruder outlives it, any property getter call dereferences a dangling
    // pointer. Lifetime management is entirely by convention, not enforced by type.
    GCodeConfig* m_config;
    // Print-wide global ID of this extruder.
    unsigned int m_id;
    // Current state of the extruder axis, may be resetted if use_relative_e_distances.
    double m_E;
    // Current state of the extruder tachometer, used to output the extruded_volume() and used_filament() statistics.
    double m_absolute_E;
    // Current positive amount of retraction.
    double m_retracted;
    // When retracted, this value stores the extra amount of priming on deretraction.
    double m_restart_extra;
    // [INTENT] Cached value of filament_flow_ratio() / filament_crossection().
    // Computed once at construction; does NOT update if config changes during a print.
    double m_e_per_mm3;

    // BBS.
    // Create shared E and retraction data for single extruder multi-material machine
    // [HAZARD] H646: Static class members — global state shared across all Extruder instances.
    bool                       m_share_extruder;
    static std::vector<double> m_share_E;
    static std::vector<double> m_share_retracted;
};

// Sort Extruder objects by the extruder id by default.
inline bool operator==(const Extruder& e1, const Extruder& e2) { return e1.id() == e2.id(); }
inline bool operator!=(const Extruder& e1, const Extruder& e2) { return e1.id() != e2.id(); }
inline bool operator<(const Extruder& e1, const Extruder& e2) { return e1.id() < e2.id(); }
inline bool operator>(const Extruder& e1, const Extruder& e2) { return e1.id() > e2.id(); }

} // namespace Slic3r

#endif
