///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#ifndef VGCODE_SETTINGS_HPP
#define VGCODE_SETTINGS_HPP

#include "../include/Types.hpp"

#include <map>

namespace libvgcode {

// [INTENT] Configuration container for visualization behavior
// [PORTING_HAZARD:Px] Struct layout must be mirrored in C# for Unity interop
// [UNITY] Map to Settings struct in C# with identical field order and types
struct Settings
{
    // [INTENT][STATE] Current visualization mode selection
    // [STATE] Initialized to FeatureType - default view shows print features
    // [PORTING_HAZARD:Px] EViewType enum has 16+ values - ensure all mapped in C#
    EViewType view_type{EViewType::FeatureType};

    // [INTENT][STATE] Print time calculation mode
    // [STATE] Default Normal mode - Stealth available for alternative timing
    ETimeMode time_mode{ETimeMode::Normal};

    // [INTENT][STATE] Render scope limitation flag
    // [STATE] When false (default), render all layers; true restricts to top layer only
    // [EVENT] Toggles layer range filtering in visualization pipeline
    bool top_layer_only_view_range{false};

    // [INTENT][STATE] Special print mode flag
    // [STATE] Indicates presence of spiral vase mode in gcode
    // [EVENT] Affects layer visualization due to continuous spiral nature
    bool spiral_vase_mode{false};

    // [INTENT][STATE] Flags indicating which visualization components need updates
    // [STATE] All three default to true to force initial render
    // [EVENT] These trigger re-computation of layer ranges, entity visibility, and color mapping
    bool update_view_full_range{true};
    bool update_enabled_entities{true};
    bool update_colors{true};

    // [INTENT][STATE] Bitmask-like array for filtering move types in visualization
    // [STATE] Persistent visibility state for user-controlled UI filtering
    // [UNITY] Map to bool array in C# - index must match EOptionType enum order
    // [PORTING_HAZARD:Px] Conditional compilation (COG_AND_TOOL_MARKERS) affects array size
    // [PORTING_HAZARD:Px] Ensure C# has same VGCODE_ENABLE_COG_AND_TOOL_MARKERS condition
    std::array<bool, std::size_t(EOptionType::COUNT)> options_visibility{
        false, // Travels - invisible by default (reduce visual clutter)
        false, // Wipes - invisible by default
        false, // Retractions - invisible by default
        false, // Unretractions - invisible by default
        true,  // Seams - visible by default (important for print quality)
        false, // ToolChanges - invisible by default
        false, // ColorChanges - invisible by default
        false, // PausePrints - invisible by default
        false, // CustomGCodes - invisible by default
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        false, // CenterOfGravity - invisible by default
        true   // ToolMarker - visible by default
#endif         // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    };

    // [INTENT][STATE] Bitmask-like array for filtering extrusion roles in visualization
    // [STATE] All roles visible by default - user can disable specific print features
    // [EVENT] Used to color-code and filter toolpaths by extrusion type
    // [PORTING_HAZARD:Px] ORCA additions add 5 new items - must extend C# enum and array
    // [UNCLEAR] Inconsistent formatting (spaces vs tabs) indicates merge artifacts
    // [UNCLEAR] Right-aligned "ORCA" and "true" comments suggest different contributors
    std::array<bool, std::size_t(EGCodeExtrusionRole::COUNT)> extrusion_roles_visibility{
        true, // None
        true, // Perimeter
        true, // ExternalPerimeter
        true, // OverhangPerimeter
        true, // InternalInfill
        true, // SolidInfill (NOTE: inconsistent spacing here)
        true, // TopSolidInfill
        true, // Ironing
        true, // BridgeInfill
        true, // GapFill
        true, // Skirt
        true, // SupportMaterial
        true, // SupportMaterialInterface
        true, // WipeTower
        true, // Custom
        // ORCA
        true, // BottomSurface
        true, // InternalBridgeInfill
        true, // Brim
        true, // SupportTransition
        true, // Mixed
    };
};

} // namespace libvgcode

#endif // VGCODE_SETTINGS_HPP
