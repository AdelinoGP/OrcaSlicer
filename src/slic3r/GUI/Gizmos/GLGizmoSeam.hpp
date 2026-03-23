#ifndef slic3r_GLGizmoSeam_hpp_
#define slic3r_GLGizmoSeam_hpp_

#include "GLGizmoPainterBase.hpp"

namespace Slic3r::GUI {

// [INTENT] Host the seam-editing painter, wiring wxWidgets + OpenGL controls to the seam brush state machine inherited from
// GLGizmoPainterBase. [UNITY] In Unity this becomes a SeamPainterController MonoBehaviour on a dedicated Canvas/GraphicRaycaster, updating
// a RenderTexture overlay and using the Input System (pointer/toggle actions) to drive seam edits.
class GLGizmoSeam : public GLGizmoPainterBase
{
public:
    GLGizmoSeam(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    // [OPENGL] Called once per frame by the GLCanvas3D renderer to draw the seam brush overlay and HUD markers.
    void render_painter_gizmo() override;

    // BBS
    //  [EVENT] Keyboard shortcuts here flip between seam tool variants before mouse input reroutes to GLGizmoPainterBase.
    bool on_key_down_select_tool_type(int keyCode);

protected:
    // BBS
    // [STATE] Invoked whenever the seam tool becomes active/inactive so cached cursor info and UI state can be reset.
    // [EVENT] This mirrors GLGizmoBase::set_state events that drive UI widget enabling.
    void on_set_state() override;

    // [STATE] Tracks the current wchar_t table entry for seam brush type so input dialogs stay in sync with selection.
    wchar_t m_current_tool = 0;
    // [OPENGL][UNITY] Positions the seam input widget using GL coordinates; Unity would map this to overlay VisualElements driven by the
    // same state.
    void             on_render_input_window(float x, float y, float bottom_limit) override;
    std::string      on_get_name() const override;
    PainterGizmoType get_painter_type() const override;

    // [OPENGL][UNITY] Renders tooltip text near the cursor; mapped to a Unity TextMeshPro overlay or UI Toolkit VisualElement if porting.
    void show_tooltip_information(float caption_max, float x, float y);

    // [EVENT] Called when the active seam tool char changes so helper text and cursor radius can refresh.
    void tool_changed(wchar_t old_tool, wchar_t new_tool);

    // [EVENT] Snapshot naming hooks aggregate shift/button state and read `m_desc` so undo history shows human-friendly action text.
    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string                  get_gizmo_entering_text() const override { return _u8L("Entering Seam painting"); }
    std::string                  get_gizmo_leaving_text() const override { return _u8L("Leaving Seam painting"); }
    std::string                  get_action_snapshot_name() const override { return _u8L("Paint-on seam editing"); }
    static const constexpr float CursorRadiusMin = 0.05f; // cannot be zero

    const float get_cursor_radius_min() const override { return CursorRadiusMin; }

private:
    // [EVENT] GLGizmoController calls this during gizmo registration to build tooltips and restore brush cache.
    bool on_init() override;

    // BBS:remove const
    //  [STATE] Synchronize painter limits with the underlying platform model so brush settings persist across mode switches.
    void update_model_object() override;
    // BBS: add logic to distinguish the first_time_update and later_update
    //  [STATE] first_update flag toggles the initial localization/layout work from incremental state refreshes.
    void update_from_model_object(bool first_update = false) override;

    // [THREAD][EVENT] on_opening runs on the UI thread when the gizmo becomes visible; only stubs needed because GLGizmoPainterBase handles
    // most work.
    void on_opening() override {}
    // [THREAD] on_shutdown tears down any stored GL resources or translation state when this gizmo is retired.
    void on_shutdown() override;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    // [STATE][PORTING_HAZARD:P2] Unity translations must also refresh on locale change; mirror this map with a ScriptableObject-backed
    // localization cache or Reactively update the VisualElement tree.
    std::map<std::string, wxString> m_desc;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoSeam_hpp_
