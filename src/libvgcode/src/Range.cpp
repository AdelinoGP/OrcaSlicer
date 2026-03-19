///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#include "Range.hpp"

#include <algorithm>

namespace libvgcode {

// [INTENT] Set the range bounds, ensuring min <= max
void Range::set(Interval::value_type min, Interval::value_type max)
{
    // [STATE] Input validation: swap if max < min to maintain invariant
    if (max < min)
        std::swap(min, max);
    // [STATE] Store ordered bounds
    m_range[0] = min;
    m_range[1] = max;
    // [PORTING_HAZARD] Relies on std::swap and array indexing - ensure consistent types
}

// [INTENT] Clamp another range to fit within this range's bounds
void Range::clamp(Range& other)
{
    // [STATE] Clamp lower bound
    other.m_range[0] = std::clamp(other.m_range[0], m_range[0], m_range[1]);
    // [STATE] Clamp upper bound
    other.m_range[1] = std::clamp(other.m_range[1], m_range[0], m_range[1]);
    // [PORTING_HAZARD] Uses std::clamp (C++17) - verify target platforms support C++17
}

} // namespace libvgcode
