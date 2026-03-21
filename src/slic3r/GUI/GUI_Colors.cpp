#include "GUI_Colors.hpp"
#include "imgui/imgui.h"

namespace Slic3r {

// [INTENT] Own the canonical palette driving every render layer so GL shaders can sample consistent values.
// [STATE] `colors` caches a shared `ImVec4` table indexed by `RenderCol` to avoid recomputing hues on each draw.
// [OPENGL] This table feeds uniform/color-buffer updates for each GL pass, so the values must stay in sync with the renderer.
// [THREAD] Expect all writes to happen on the main thread while the GL context is active; background threads must not mutate this table.
// [UNITY] Replace with a ScriptableObject `RenderPalette` coupled to a MaterialPropertyBlock per renderer, keeping palette changes in sync
// via Unity events. [PORTING_HAZARD:P3] The `RenderCol` ordering is implicit in GL draw calls—reordering or adding entries here without
// updating renderers will color the wrong primitives.
ImVec4 RenderColor::colors[RenderCol_Count] = {};

// [INTENT] Provide the legend label used by UI controls when a user inspects or edits a render layer color.
// [UNITY] Port this to Unity as the VisualElement list bound to the ScriptableObject palette so the legend stays one-to-one with the colors.
// [PORTING_HAZARD:P3] These strings must mirror the `RenderCol` enum order; any mismatch leaks into the chooser and mislabels the color swatches.
const char* GetRenderColName(RenderCol idx)
{
    switch (idx) {
    case RenderCol_3D_Background: return "3D Background";
    case RenderCol_Plate_Unselected: return "Plate Unselected";
    case RenderCol_Plate_Selected: return "Plate Selected";
    case RenderCol_Plate_Default: return "Plate Default";
    case RenderCol_Plate_Line_Top: return "Plate Line Top";
    case RenderCol_Plate_Line_Bottom: return "Plate Line Bottom";
    case RenderCol_Model_Disable: return "Model Disable";
    case RenderCol_Model_Unprintable: return "Model Unprintable";
    case RenderCol_Model_Neutral: return "Model Neutral";
    case RenderCol_Part: return "Part";
    case RenderCol_Modifier: return "Modifier";
    case RenderCol_Negtive_Volume: return "Negtive Volume";
    case RenderCol_Support_Enforcer: return "Support Enforcer";
    case RenderCol_Support_Blocker: return "Support Blocker";
    case RenderCol_Axis_X: return "Axis X";
    case RenderCol_Axis_Y: return "Axis Y";
    case RenderCol_Axis_Z: return "Axis Z";
    case RenderCol_Grabber_X: return "Grabber X";
    case RenderCol_Grabber_Y: return "Grabber Y";
    case RenderCol_Grabber_Z: return "Grabber Z";
    case RenderCol_Flatten_Plane: return "Flatten Plane";
    case RenderCol_Flatten_Plane_Hover: return "Flatten Plane Hover";
    }
    return "Unknown";
}

} // namespace Slic3r
