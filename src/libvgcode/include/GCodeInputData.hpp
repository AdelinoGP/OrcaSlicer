///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#ifndef VGCODE_GCODEINPUTDATA_HPP
#define VGCODE_GCODEINPUTDATA_HPP

#include "PathVertex.hpp"

namespace libvgcode {

// [INTENT] Container for input data required by the libvgcode viewer.
// [UNITY] If libvgcode is ported to C#, this becomes a pure data class/struct.
// If kept native, this structure must be mirrored in C# for P/Invoke (StructLayout).
struct GCodeInputData
{
    //
    // Whether or not the gcode was generated with spiral vase mode enabled.
    // Required to properly detect fictitious layer changes when spiral vase mode is enabled.
    //
    // [STATE] Indicates if spiral vase mode is active.
    bool spiral_vase_mode{false};
    //
    // List of path vertices (gcode moves)
    // See: PathVertex
    //
    // [STATE] The complete set of vertices defining the G-code toolpath.
    // [UNITY] Maps to `List<PathVertex>` or native array/NativeArray for performance.
    std::vector<PathVertex> vertices;
    //
    // Palette for extruders colors
    //
    // [STATE] Color palette assigned to individual extruders/tools.
    // [UNITY] Maps to array/list of Unity Colors (`Color[]`).
    Palette tools_colors;
    //
    // Palette for color print colors
    //
    // [STATE] Color palette assigned for color changes at specific layers.
    // [UNITY] Maps to array/list of Unity Colors (`Color[]`).
    Palette color_print_colors;
};

} // namespace libvgcode

#endif // VGCODE_BITSET_HPP
