///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#include "ExtrusionRoles.hpp"

namespace libvgcode {

// [INTENT] Accumulate time budgets for each extrusion role so higher layers can report or
//           visualize how long actions such as travel, extrusion, or waiting consume per slice.
// [STATE] `m_items` keeps a `role -> Item` map where `Item::times` is a fixed-size `std::array`
//         indexed by `ETimeMode` categories; this routine ensures the entry exists before
//         adding the provided per-mode deltas.
// [UNITY] Mirror this with a `Dictionary<EGCodeExtrusionRole, RoleTiming>` (RoleTiming wrapping
//         a `float[TimeMode.Count]`) hosted on a `ScriptableObject` that the UI can query safely.
void ExtrusionRoles::add(EGCodeExtrusionRole role, const std::array<float, TIME_MODES_COUNT>& times)
{
    auto role_it = m_items.find(role);
    if (role_it == m_items.end())
        role_it = m_items.insert(std::make_pair(role, Item())).first;

    for (std::size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        role_it->second.times[i] += times[i];
    }
}

// [INTENT] Provide a stable list of currently tracked roles so the UI or schedulers can iterate
//         over them when building legends, toggles, or runtime summaries.
// [UNITY] Expose this as a `List<EGCodeExtrusionRole>` from the ScriptableObject model to feed
//         a UI Toolkit `ListView` or `Dropdown` with explicit role ordering.
std::vector<EGCodeExtrusionRole> ExtrusionRoles::get_roles() const
{
    std::vector<EGCodeExtrusionRole> ret;
    ret.reserve(m_items.size());
    for (const auto& [role, item] : m_items) {
        ret.emplace_back(role);
    }
    return ret;
}

// [INTENT] Query the cumulative time spent for the given role/mode pair; returns 0 if the
//         role is unknown or the mode enum is out-of-bounds.
// [PORTING_HAZARD:P3] Unity enums must keep the same ordering/COUNT sentinel to avoid
//                  indexing mismatches when bridged to the native timing cache.
float ExtrusionRoles::get_time(EGCodeExtrusionRole role, ETimeMode mode) const
{
    const auto role_it = m_items.find(role);
    if (role_it == m_items.end())
        return 0.0f;

    return (mode < ETimeMode::COUNT) ? role_it->second.times[static_cast<std::size_t>(mode)] : 0.0f;
}

} // namespace libvgcode
