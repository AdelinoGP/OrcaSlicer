#ifndef slic3r_GUI_Colors_hpp_
#define slic3r_GUI_Colors_hpp_

#include "imgui/imgui.h"
#include "libslic3r/Color.hpp"
#include <array>

// [INTENT]/[STATE]/[OPENGL]/[UNITY]/[PORTING_HAZARD:P3] Logical palette indices consumed by every viewport renderer and GUI overlay so
// OpenGL draw calls can read the same color slot; Unity should mirror this enum inside a ScriptableObject-backed Color[] and keep the order
// synchronized with the ImGui/GL pipeline because shared indices determine which color is bound to which mesh or gizmo.
enum RenderCol_ {
    RenderCol_3D_Background = 0,
    RenderCol_Plate_Unselected,
    RenderCol_Plate_Selected,
    RenderCol_Plate_Default,
    RenderCol_Plate_Line_Top,
    RenderCol_Plate_Line_Bottom,
    RenderCol_Model_Disable,
    RenderCol_Model_Unprintable,
    RenderCol_Model_Neutral,
    RenderCol_Part,
    RenderCol_Modifier,
    RenderCol_Negtive_Volume,
    RenderCol_Support_Enforcer,
    RenderCol_Support_Blocker,
    RenderCol_Axis_X,
    RenderCol_Axis_Y,
    RenderCol_Axis_Z,
    RenderCol_Grabber_X,
    RenderCol_Grabber_Y,
    RenderCol_Grabber_Z,
    RenderCol_Flatten_Plane,
    RenderCol_Flatten_Plane_Hover,
    RenderCol_Count,
};

typedef int RenderCol;

namespace Slic3r {

class RenderColor {
public:
    // [STATE]/[UNITY] Cached ImGui-compatible RGBA colors that match the enum order above so render code can index into one static array.
    // Unity should provide an equivalent cached Color[] and expose it to shaders and the UI controller for palette editing.
    static ImVec4 colors[RenderCol_Count];
};
// [INTENT]/[UNITY] Provides a human-readable label (used for diagnostics and palette panels).
// Unity port tooling should expose the same names through `EditorGUI` or UI Toolkit helpers.
const char* GetRenderColName(RenderCol idx);

} // namespace Slic3r

#endif
