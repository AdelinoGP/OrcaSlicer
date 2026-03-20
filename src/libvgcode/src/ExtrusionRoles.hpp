///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#ifndef VGCODE_EXTRUSION_ROLES_HPP
#define VGCODE_EXTRUSION_ROLES_HPP

#include "../include/Types.hpp"

#include <map>

namespace libvgcode {

// [INTENT] Aggregate the duration signatures of every extrusion role so the UI/metrics layers
// can interpret gcode commands and schedule animations consistently.
// [UNITY] Port as a ScriptableObject-backed `Dictionary<EGCodeExtrusionRole, FixedTimeArray>` so
// C# widgets can bind to the role list and shared timing data.
class ExtrusionRoles
{
public:
    struct Item
    {
        // [STATE] Cache the duration for each TIME_MODE to avoid recomputing when rendering multi-mode stats.
        std::array<float, TIME_MODES_COUNT> times;
    };

    // [EVENT] Invoked when parsing gcode roles so the UI/metrics layers can observe new timing profiles.
    // [THREAD] Runs on the slicing thread before the UI reads the map, so the port must marshal any cross-thread updates.
    // [PORTING_HAZARD:P3] Unity will need explicit locking or copy semantics if this data ever crosses to multiple threads.
    void add(EGCodeExtrusionRole role, const std::array<float, TIME_MODES_COUNT>& times);

    // [STATE] Basic helpers used by UI panels to size lists consistently across callbacks.
    std::size_t get_roles_count() const { return m_items.size(); }

    // [UNITY] Surface as a `Dictionary<EGCodeExtrusionRole, Item>` bound to a ListView/Inspector tree for display.
    std::vector<EGCodeExtrusionRole> get_roles() const;

    // [STATE] Read access for metrics and dashboards to surface each role/mode duration.
    float get_time(EGCodeExtrusionRole role, ETimeMode mode) const;

    // [EVENT] Reset when the slicer reloads a profile so stale durations disappear before the next run.
    void reset() { m_items.clear(); }

private:
    // [STATE] Aggregates the cached per-role durations; callers must duplicate this map before crossing threads.
    // [PORTING_HAZARD:P3] The current implementation lacks synchronization, so add explicit marshaling in the Unity port.
    std::map<EGCodeExtrusionRole, Item> m_items;
};

} // namespace libvgcode

#endif // VGCODE_EXTRUSION_ROLES_HPP
