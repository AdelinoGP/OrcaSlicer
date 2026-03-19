///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#ifndef VGCODE_COLORPRINT_HPP
#define VGCODE_COLORPRINT_HPP

#include "../include/Types.hpp"

namespace libvgcode {

// [INTENT] Defines the data structure for a color change event in the G-code path visualization.
// [UNITY] Map to a standard C# struct or simple class (e.g., ColorPrintEvent) for pure data representation.
struct ColorPrint
{
    // [STATE] The ID of the extruder to switch to.
    uint8_t extruder_id{0};
    // [STATE] The ID of the color to apply.
    uint8_t color_id{0};
    // [STATE] The index of the layer where the color change occurs.
    uint32_t layer_id{0};
    // [STATE] Estimated times associated with the color print change.
    std::array<float, TIME_MODES_COUNT> times{0.0f, 0.0f};
};

} // namespace libvgcode

#endif // VGCODE_COLORPRINT_HPP