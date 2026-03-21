#ifndef slic3r_GLGizmoFuzzySkin_hpp_
#define slic3r_GLGizmoFuzzySkin_hpp_

#include "GLGizmoPainterBase.hpp"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r::GUI {

class GLGizmoFuzzySkin : public GLGizmoPainterBase
{
public:
    // [INTENT] Specialized painter gizmo for the fuzzy skin tool that reuses GLGizmoPainterBase
    //           but injects fuzzy-skin specific texts, enforcer states, and tooltip hints so
    //           downstream Unity work can graft this into a MonoBehaviour-driven brush overlay.
    GLGizmoFuzzySkin(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    // [OPENGL][EVENT] Called inside the shared painter draw loop to emit fuzzy-skin handles and color bands.
    void render_painter_gizmo() override;

protected:
    // [OPENGL][UNITY] Renders the input window overlay tied to the mouse pointer; Unity should map this to
    //                  RenderTexture + dedicated UI Canvas + GraphicRaycaster input handling.
    void on_render_input_window(float x, float y, float bottom_limit) override;
    // [STATE] Tooltip label depends on localization state captured in this header, so keep the string helper near the class.
    std::string on_get_name() const override;

    // [STATE][EVENT] Presents the tooltip string near the fuzzy skin cursor when the pointer hovers over the mesh.
    void show_tooltip_information(float caption_max, float x, float y);

    // [EVENT] Snapshot actions rely on wxWidgets Button enum, so Unity must translate to InputSystem button IDs.
    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    // [STATE] Localized status text reused while tool is active; keep all text constant so Unity can reuse TextMeshPro prefab.
    std::string get_gizmo_entering_text() const override { return _u8L("Entering Paint-on fuzzy skin"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Paint-on fuzzy skin"); }
    std::string get_action_snapshot_name() const override { return _u8L("Paint-on fuzzy skin editing"); }

    // [STATE] EnforcerBlockerType defines how input blocking cascades; fuzzy skin claims the left button click chain.
    EnforcerBlockerType get_left_button_state_type() const override { return EnforcerBlockerType::FUZZY_SKIN; }
    EnforcerBlockerType get_right_button_state_type() const override { return EnforcerBlockerType::NONE; }

    // BBS
    // [STATE] Tracks the locally active brush/tool variant so continuous input (hover/drag) keeps the same painting mode.
    wchar_t m_current_tool = 0;

private:
    // [EVENT][THREAD] Initialization happens on the GUI thread after parent setup; Unity should replicate in Awake/Start.
    bool on_init() override;

    // [THREAD][EVENT] Mirrors model mutations by syncing the Gizmo state back to the transient mesh description on the UI thread.
    void update_model_object() override;
    void update_from_model_object(bool first_update) override;

    // [EVENT] No extra work is needed on opening beyond base class behavior.
    void on_opening() override {}
    // [THREAD] Shutdown tears down tooltip state and any cached translations on the UI thread before the gizmo is destroyed.
    void on_shutdown() override;
    // [STATE] Identifies this gizmo variant when the painter base switches between brush types.
    PainterGizmoType get_painter_type() const override;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations.
    // [STATE][PORTING_HAZARD:P3] Unity will need to recreate or refresh this map when localization changes instead of relying on wxWidgets
    // recreating the whole window.
    std::map<std::string, wxString> m_desc;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoFuzzySkin_hpp_
