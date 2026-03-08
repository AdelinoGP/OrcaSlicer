// [INTENT] Implements the Extruder extrusion state machine.
// All math is in mm (physical filament length/volume), not scaled coordinates.
// [COUPLING] Reads configuration exclusively via GCodeConfig, accessed through m_config pointer.
// [CONCURRENCY] Not thread-safe; all calls are serialized by the GCode generation loop.

#include "Extruder.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

// [INTENT] Define the static class-level shared E and retraction vectors for BBS
// single-extruder multi-material machines. These are initialized to MAXIMUM_EXTRUDER_NUMBER
// zeroes at startup and reset() re-initializes them each print.
// [HAZARD] H646: Static initialization occurs at program startup, before any Print object
// is created. If MAXIMUM_EXTRUDER_NUMBER changes between config reload and these static vectors,
// a resize would be needed. Currently reset() recreates the vectors on each reset() call,
// which is the only mechanism to resize them.
std::vector<double> Extruder::m_share_E         = std::vector<double>(MAXIMUM_EXTRUDER_NUMBER, 0);
std::vector<double> Extruder::m_share_retracted = std::vector<double>(MAXIMUM_EXTRUDER_NUMBER, 0);

// [INTENT] Constructor: stores config reference, calls reset(), then caches m_e_per_mm3.
// m_e_per_mm3 = flow_ratio / crossection_area. This means for every mm^3 of geometric
// extrusion volume, m_e_per_mm3 mm of filament is consumed.
// [HAZARD] H651: m_e_per_mm3 is computed once at construction. If filament_flow_ratio or
// filament_diameter change after construction (which doesn't happen in normal use but could
// in PlaceholderParser scripts), the cached value is stale. The e_per_mm3() getter returns
// the stale cached value, not a live computed value.
Extruder::Extruder(unsigned int id, GCodeConfig* config, bool share_extruder) : m_id(id), m_config(config), m_share_extruder(share_extruder)
{
    reset();

    // cache values that are going to be called often
    m_e_per_mm3 = this->filament_flow_ratio();
    m_e_per_mm3 /= this->filament_crossection();
}

// [INTENT] Maps the logical extruder id (m_id) to the physical filament slot index via
// config's filament_map. The -1 compensates for the 1-based filament slot numbers in the map.
// Returns 0 (first physical slot) as fallback for out-of-range m_id.
// [HAZARD] H649 (see .hpp): filament_map lookup uses m_id, whereas other getters also use m_id
// directly. Only toolchange retraction getters use extruder_id().
unsigned int Extruder::extruder_id() const
{
    assert(m_config);
    if (m_id < m_config->filament_map.size()) {
        return m_config->filament_map.get_at(m_id) - 1;
    }
    return 0;
}

// [INTENT] Advance E axis by dE. In relative mode, m_E is first zeroed so subsequent G-code
// output represents just the delta for this move. m_absolute_E accumulates the total print-wide
// extrusion for statistics (never reset by relative mode reset).
// [HAZARD] H647 (see .hpp): negative dE is allowed — it increases m_retracted. Prefer retract()
// for explicit retraction to preserve idempotency.
double Extruder::extrude(double dE)
{
    // BBS
    if (m_share_extruder) {
        if (m_config->use_relative_e_distances)
            m_share_E[extruder_id()] = 0.;
        m_share_E[extruder_id()] += dE;
        m_absolute_E += dE;
        if (dE < 0.)
            m_share_retracted[extruder_id()] -= dE;
    } else {
        // in case of relative E distances we always reset to 0 before any output
        if (m_config->use_relative_e_distances)
            m_E = 0.;
        m_E += dE;
        m_absolute_E += dE;
        if (dE < 0.)
            m_retracted -= dE;
    }
    return dE;
}

/* This method makes sure the extruder is retracted by the specified amount
   of filament and returns the amount of filament retracted.
   If the extruder is already retracted by the same or a greater amount,
   this method is a no-op.
   The restart_extra argument sets the extra length to be used for
   unretraction. If we're actually performing a retraction, any restart_extra
   value supplied will overwrite the previous one if any. */
// [INTENT] Idempotent retraction: ensures m_retracted >= length.
// Only retracts the delta (length - m_retracted) if needed.
// m_restart_extra is ALWAYS updated regardless of whether new retraction occurred.
// [HAZARD] H652: m_restart_extra is updated even when to_retract == 0. This means calling
// retract(same_length, new_restart_extra) mid-print will silently change the priming
// amount for the next unretract, even though no new retraction occurred. This can cause
// over- or under-priming at the next unretract.
double Extruder::retract(double length, double restart_extra)
{
    // BBS
    if (m_share_extruder) {
        if (m_config->use_relative_e_distances)
            m_share_E[extruder_id()] = 0.;
        double to_retract = std::max(0., length - m_share_retracted[extruder_id()]);
        m_restart_extra   = restart_extra;
        if (to_retract > 0.) {
            m_share_E[extruder_id()] -= to_retract;
            m_absolute_E -= to_retract;
            m_share_retracted[extruder_id()] += to_retract;
        }
        return to_retract;
    } else {
        // in case of relative E distances we always reset to 0 before any output
        if (m_config->use_relative_e_distances)
            m_E = 0.;
        double to_retract = std::max(0., length - m_retracted);
        m_restart_extra   = restart_extra;
        if (to_retract > 0.) {
            m_E -= to_retract;
            m_absolute_E -= to_retract;
            m_retracted += to_retract;
        }
        return to_retract;
    }
}

// [INTENT] Unretract (prime) the nozzle. Extrudes m_retracted + m_restart_extra mm of filament,
// then clears both fields. After unretract(), the extruder is in an unretracted state.
// [HAZARD] H653: In share_extruder mode, dE = m_share_retracted[extruder_id()] + m_restart_extra.
// After this->extrude(dE), the extrude() call will ALSO increment m_share_retracted (because
// dE > 0 is not < 0, so the `if (dE < 0.)` branch is not taken). The explicit
// `m_share_retracted[extruder_id()] = 0.` immediately after the extrude() call overrides this.
// In non-share mode, extrude() increments m_absolute_E and m_E (not m_retracted for positive dE),
// then m_retracted = 0 resets it. Both are correct but the ordering matters.
double Extruder::unretract()
{
    // BBS
    if (m_share_extruder) {
        double dE = m_share_retracted[extruder_id()] + m_restart_extra;
        this->extrude(dE);
        m_share_retracted[extruder_id()] = 0.;
        m_restart_extra                  = 0.;
        return dE;
    } else {
        double dE = m_retracted + m_restart_extra;
        this->extrude(dE);
        m_retracted     = 0.;
        m_restart_extra = 0.;
        return dE;
    }
}

// Setting the retract state from the script.
// Sets current retraction value & restart extra filament amount if retracted > 0.
// [INTENT] Called by PlaceholderParser scripts to externally set retraction state.
// Validates non-negative inputs (throws RuntimeError on negative values, which is caught
// and injected into the G-code as an error comment by GCodeWriter).
// [HAZARD] H654: set_retracted() only updates m_retracted and m_restart_extra, not the
// shared equivalents (m_share_retracted, m_share_E). In share_extruder mode, the script
// would set m_retracted (which is not read in share_extruder mode), leaving the actual
// retraction state unchanged.
void Extruder::set_retracted(double retracted, double restart_extra)
{
    if (retracted < -EPSILON)
        throw Slic3r::RuntimeError("Custom G-code reports negative z_retracted.");
    if (restart_extra < -EPSILON)
        throw Slic3r::RuntimeError("Custom G-code reports negative z_restart_extra.");

    if (retracted > EPSILON) {
        m_retracted     = retracted;
        m_restart_extra = restart_extra < EPSILON ? 0 : restart_extra;
    } else {
        m_retracted     = 0;
        m_restart_extra = 0;
    }
}

// Used filament volume in mm^3.
// [INTENT] Volume = used_filament (length in mm) × cross-sectional area.
// [HAZARD] H655: In both share_extruder and non-share paths, extruded_volume() calls
// used_filament() * filament_crossection(). The two paths are currently identical.
// The FIXME comment indicates m_retracted accounting is not included for share_extruder.
// This means the statistics slightly undercount volume for share-extruder machines.
double Extruder::extruded_volume() const
{
    // BBS
    if (m_share_extruder) {
        // FIXME: need to count m_retracted for share extruder machine
        return this->used_filament() * this->filament_crossection();
    } else {
        return this->used_filament() * this->filament_crossection();
    }
}

// Used filament length in mm.
// [INTENT] For non-share mode: m_absolute_E + m_retracted, because m_absolute_E tracks
// net extrusion (decremented by retraction), so adding m_retracted gives total consumed length.
// For share mode: only m_absolute_E (FIXME: retracted not counted — see H655).
double Extruder::used_filament() const
{
    // BBS
    if (m_share_extruder) {
        // FIXME: need to count retracted length for share-extruder machine
        return m_absolute_E;
    } else {
        return m_absolute_E + m_retracted;
    }
}

// [INTENT] All property getters below delegate to GCodeConfig via the get_at() pattern.
// filament_diameter() and filament_flow_ratio() use m_id (logical extruder index);
// retract_length_toolchange() and retract_restart_extra_toolchange() use extruder_id()
// (physical slot index) — see H649.
double Extruder::filament_diameter() const { return m_config->filament_diameter.get_at(m_id); }

double Extruder::filament_density() const { return m_config->filament_density.get_at(m_id); }

double Extruder::filament_cost() const { return m_config->filament_cost.get_at(m_id); }

double Extruder::filament_flow_ratio() const { return m_config->filament_flow_ratio.get_at(m_id); }

// Return a "retract_before_wipe" percentage as a factor clamped to <0, 1>
double Extruder::retract_before_wipe() const { return std::min(1., std::max(0., m_config->retract_before_wipe.get_at(m_id) * 0.01)); }

double Extruder::retraction_length() const { return m_config->retraction_length.get_at(m_id); }

double Extruder::retract_lift() const { return m_config->z_hop.get_at(m_id); }

// [INTENT] Retract speed in mm/s, rounded to nearest integer.
int Extruder::retract_speed() const { return int(floor(m_config->retraction_speed.get_at(m_id) + 0.5)); }

bool Extruder::use_firmware_retraction() const { return m_config->use_firmware_retraction; }

// [INTENT] Deretraction speed in mm/s. If config value is 0, falls back to retract_speed().
// This allows "same speed for retract and deretract" to be configured as deretract_speed = 0.
int Extruder::deretract_speed() const
{
    int speed = int(floor(m_config->deretraction_speed.get_at(m_id) + 0.5));
    return (speed > 0) ? speed : this->retract_speed();
}

double Extruder::retract_restart_extra() const { return m_config->retract_restart_extra.get_at(m_id); }

// [HAZARD] H649: Uses extruder_id() (physical slot) not m_id (logical extruder).
double Extruder::retract_length_toolchange() const { return m_config->retract_length_toolchange.get_at(extruder_id()); }

// [HAZARD] H649: Uses extruder_id() (physical slot) not m_id (logical extruder).
double Extruder::retract_restart_extra_toolchange() const { return m_config->retract_restart_extra_toolchange.get_at(extruder_id()); }

// [INTENT] Travel slope (z-hop ramp angle) converted from degrees to radians.
// [HAZARD] H649: Uses extruder_id() (physical slot) not m_id (logical extruder).
double Extruder::travel_slope() const { return m_config->travel_slope.get_at(extruder_id()) * PI / 180; }

} // namespace Slic3r
