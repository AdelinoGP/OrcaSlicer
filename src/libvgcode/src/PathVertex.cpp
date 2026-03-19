///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#include "../include/PathVertex.hpp"

namespace libvgcode {

// [INTENT][STATE] Default constructor creates a dummy/invalid vertex used as sentinel value
// [PORTING_HAZARD] This static dummy is used throughout the codebase - ensure initialization order is preserved
const PathVertex PathVertex::DUMMY_PATH_VERTEX = PathVertex();

// [INTENT][STATE] Query method to determine if vertex represents an extrusion move
// [STATE] Checks internal type field against EMoveType::Extrude enumeration
// [EVENT] Used during visualization rendering to differentiate extrusion segments
bool PathVertex::is_extrusion() const { return type == EMoveType::Extrude; }

// [INTENT][STATE] Query method to determine if vertex represents a travel move
// [STATE] Checks internal type field against EMoveType::Travel enumeration
// [EVENT] Used during visualization rendering to differentiate travel segments
bool PathVertex::is_travel() const { return type == EMoveType::Travel; }

// [INTENT][STATE] Query method to determine if vertex represents a wipe move
// [STATE] Checks internal type field against EMoveType::Wipe enumeration
// [EVENT] Used during visualization rendering to differentiate wipe segments
bool PathVertex::is_wipe() const { return type == EMoveType::Wipe; }

// [INTENT][STATE] Query method to determine if vertex represents an "option" move type
// [STATE] Uses switch statement to check type against multiple EMoveType enumerations
// [EVENT] Classifies special move types (retracts, seams, tool changes, pauses) for UI filtering
// [UNCLEAR] The term "option" is vague - these are special event markers in the print path
bool PathVertex::is_option() const
{
    switch (type) {
    case EMoveType::Retract:
    case EMoveType::Unretract:
    case EMoveType::Seam:
    case EMoveType::ToolChange:
    case EMoveType::ColorChange:
    case EMoveType::PausePrint:
    case EMoveType::CustomGCode: {
        return true;
    }
    default: {
        return false;
    }
    }
}

// [INTENT][STATE] Query method to determine if vertex represents custom G-code
// [STATE] Checks both type (Extrude) and role (Custom) fields
// [EVENT] Used to identify custom g-code segments in the visualization
bool PathVertex::is_custom_gcode() const { return type == EMoveType::Extrude && role == EGCodeExtrusionRole::Custom; }

} // namespace libvgcode
