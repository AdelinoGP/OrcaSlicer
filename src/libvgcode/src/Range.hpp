///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#ifndef VGCODE_RANGE_HPP
#define VGCODE_RANGE_HPP

#include "../include/Types.hpp"

namespace libvgcode {

class Range
{
public:
    // [INTENT] Get the current interval range as a const reference
    // [UNITY] Maps to (float min, float max) tuple or a custom Range struct in C#
    const Interval& get() const { return m_range; }

    // [INTENT] Copy assignment from another Range object
    // [UNITY] Simple value copy: Range other = this;
    void set(const Range& other) { m_range = other.m_range; }

    // [INTENT] Set range from interval (min, max) array
    // [UNITY] Use tuple deconstruction: (min, max) = interval;
    void set(const Interval& range) { set(range[0], range[1]); }

    // [INTENT] Set range with explicit min/max values
    // [UNITY] Use standard assignment or constructor
    void set(Interval::value_type min, Interval::value_type max);

    // [INTENT] Get minimum value of range
    // [UNITY] Public property: float Min => range[0];
    Interval::value_type get_min() const { return m_range[0]; }

    // [INTENT] Set minimum value, preserve maximum
    // [UNITY] Property setter: Min = value;
    void set_min(Interval::value_type min) { set(min, m_range[1]); }

    // [INTENT] Get maximum value of range
    // [UNITY] Public property: float Max => range[1];
    Interval::value_type get_max() const { return m_range[1]; }

    // [INTENT] Set maximum value, preserve minimum
    // [UNITY] Property setter: Max = value;
    void set_max(Interval::value_type max) { set(m_range[0], max); }

    // [INTENT] Clamp the given range to stay inside this range (mutates other)
    // [STATE] Modifies the 'other' Range parameter
    // [PORTING_HAZARD] In-place mutation pattern - consider returning new Range instead
    void clamp(Range& other);

    // [INTENT] Reset range to zero (0, 0)
    // [UNITY] Range = default; or Range = new Range();
    void reset() { m_range = {0, 0}; }

    // [INTENT] Equality comparison
    // [UNITY] Standard equality operator works with value tuples
    bool operator==(const Range& other) const { return m_range == other.m_range; }

    // [INTENT] Inequality comparison
    // [UNITY] Standard inequality operator works
    bool operator!=(const Range& other) const { return m_range != other.m_range; }

private:
    // [STATE] Stored interval [min, max]
    // [UNITY] Use (float,float) tuple or C# value tuple: (float min, float max)
    Interval m_range{0, 0};
};

} // namespace libvgcode

#endif // VGCODE_RANGE_HPP