#ifndef slic3r_GUI_GLGizmosManager_hpp_
#define slic3r_GUI_GLGizmosManager_hpp_

#include "slic3r/GUI/GLTexture.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"
// BBS: GUI refactor: add object manipulation
#include "slic3r/GUI/Gizmos/GizmoObjectManipulation.hpp"

#include "libslic3r/ObjectID.hpp"

#include <wx/timer.h>
#include <map>

// BBS: GUI refactor: to support top layout
#define BBS_TOOLBAR_ON_TOP 1

namespace Slic3r {

namespace UndoRedo {
struct Snapshot;
}

namespace GUI {

class GLCanvas3D;
class ClippingPlane;
enum class SLAGizmoEventType : unsigned char;
class CommonGizmosDataPool;
// BBS: GUI refactor: add object manipulation
class GizmoObjectManipulation;
// [STATE] Utility rect used during layout calculations; mirror with Unity RectInt.
class Rect
{
    float m_left{0.0f};
    float m_top{0.0f};
    float m_right{0.0f};
    float m_bottom{0.0f};

public:
    Rect() = default;
    Rect(float left, float top, float right, float bottom) : m_left(left), m_top(top), m_right(right), m_bottom(bottom) {}

    bool operator==(const Rect& other) const
    {
        if (std::abs(m_left - other.m_left) > EPSILON)
            return false;
        if (std::abs(m_top - other.m_top) > EPSILON)
            return false;
        if (std::abs(m_right - other.m_right) > EPSILON)
            return false;
        if (std::abs(m_bottom - other.m_bottom) > EPSILON)
            return false;
        return true;
    }
    bool operator!=(const Rect& other) const { return !operator==(other); }

    float get_left() const { return m_left; }
    void  set_left(float left) { m_left = left; }

    float get_top() const { return m_top; }
    void  set_top(float top) { m_top = top; }

    float get_right() const { return m_right; }
    void  set_right(float right) { m_right = right; }

    float get_bottom() const { return m_bottom; }
    void  set_bottom(float bottom) { m_bottom = bottom; }

    float get_width() const { return m_right - m_left; }
    float get_height() const { return m_top - m_bottom; }
};

class GLGizmosManager : public Slic3r::ObjectBase
{
public:
    static const float Default_Icons_Size;

    // [INTENT] Each value ties to toolbar icons and the `m_gizmos` index.
    // [STATE] The enum order must remain stable because serialization/load replays the exact index.
    // [PORTING_HAZARD:P2] Reordering entries without migrating saved `m_current` values will desync the active gizmo on load.
    enum EType : unsigned char {
        // Order must match index in m_gizmos!
        Move,
        Rotate,
        Scale,
        Flatten,
        Cut,
        MeshBoolean,
        FdmSupports,
        Seam,
        FuzzySkin,
        MmSegmentation,
        Emboss,
        Svg,
        Measure,
        Assembly,
        Simplify,
        BrimEars,
        // SlaSupports,
        //  BBS
        // FaceRecognition,
        // Hollow,
        Undefined,
    };

private:
    struct Layout
    {
        // [STATE] Caches the icon spacing metrics so overlay layout can reuse the same math each frame.
        // [UNITY] Mirror these values in UI Toolkit VisualElements to reproduce the spacing.
        float scale{1.0f};
        float icons_size{Default_Icons_Size};
        float border{4.0f};
        float gap_y{4.0f};
        // BBS: GUI refactor: to support top layout
        float gap_x{4.0f};
        float stride_x() const { return icons_size + gap_x; }
        float scaled_gap_x() const { return scale * gap_x; }
        float scaled_stride_x() const { return scale * stride_x(); }

        float stride_y() const { return icons_size + gap_y; }

        float scaled_icons_size() const { return scale * icons_size; }
        float scaled_border() const { return scale * border; }
        float scaled_gap_y() const { return scale * gap_y; }
        float scaled_stride_y() const { return scale * stride_y(); }
    };

    // [INTENT] Owns the parent canvas for rendering, input capture, and dimension queries needed by the toolbar.
    // [UNITY] Toggle the overlay GameObject rather than manipulating raw GL state.
    GLCanvas3D& m_parent;
    // [STATE] Toggles toolbar visibility/picking; Unity replacement can toggle the overlay GameObject.
    bool m_enabled;
    // [STATE] Persistent instances for each gizmo so we can update selection without re-allocating.
    // [STATE] Persistent instances for each gizmo so we can update selection without re-allocating.
    std::vector<std::unique_ptr<GLGizmoBase>> m_gizmos;
    // [OPENGL] Atlas texture containing all toolbar icons rendered once per theme change.
    GLTexture m_icons_texture;
    // [STATE] Flag forcing icon rebuild when theme or scale changes.
    bool m_icons_texture_dirty;
    // [OPENGL] Background gradient texture for the toolbar; draw order handled by `render_overlay`.
    BackgroundTexture m_background_texture;
    // [OPENGL] Pointer arrow icons used during gizmo targeting.
    GLTexture m_arrow_texture;
    // [STATE] Layout metrics for spacing, scaling, and border computation; reused across frames.
    Layout m_layout;
    // [STATE] Currently active gizmo index; synchronized with serialization and UI buttons.
    EType m_current;
    // [STATE] Gizmo type currently under pointer but not yet activated.
    EType m_hover;
    // [STATE] Highlight info: `second` signals whether the overlay should show or hide emphasis.
    std::pair<EType, bool> m_highlight; // bool true = higlightedShown, false = highlightedHidden

    // BBS: GUI refactor: add object manipulation
    // [STATE][UNITY] Tracks drag/selection helpers that can move to a Unity ObjectManipulation MonoBehaviour.
    // [STATE][UNITY] Tracks drag/selection helpers that can move to a Unity ObjectManipulation MonoBehaviour.
    GizmoObjectManipulation m_object_manipulation;

    std::vector<size_t> get_selectable_idxs() const;
    EType               get_gizmo_from_mouse(const Vec2d& mouse_pos) const;

    bool activate_gizmo(EType type);

    // [STATE] Tooltip text for the toolbar; updated when hover state changes.
    std::string m_tooltip;
    // [THREAD] Guard that prevents re-entrant serialization while load runs on the main thread.
    bool m_serializing;
    // [STATE][UNITY] Shared caches reused by individual gizmos so they do not each recreate textures/buffers.
    // [STATE][UNITY] Shared caches reused by individual gizmos so they do not each recreate textures/buffers.
    std::unique_ptr<CommonGizmosDataPool> m_common_gizmos_data;

    // When there are more than 9 colors, shortcut key coloring
    // [THREAD] wx timers tick on the main loop so color mode adjustments stay on the UI thread.
    // [UNITY] Replace with `InvokeRepeating` or a coroutine to periodically refresh palette overlays.
    // [THREAD] wx timers tick on the main loop so color mode adjustments stay on the UI thread.
    // [UNITY] Replace with `InvokeRepeating` or a coroutine to periodically refresh palette overlays.
    wxTimer m_timer_set_color;
    // [EVENT] Handles timer ticks to update shortcut-key colors, keeping the toolbar palette responsive.
    void on_set_color_timer(wxTimerEvent& evt);

    // key MENU_ICON_NAME, value = ImtextureID
    // [STATE] Tracks the ImGui texture IDs for each toolbar icon so we can reuse them without reloading.
    // [PORTING_HAZARD:P3] Unity has no ImtextureID so store equivalent `Texture2D` references.
    // [STATE] Tracks the ImGui texture IDs for each toolbar icon so we can reuse them without reloading.
    // [PORTING_HAZARD:P3] Unity has no ImtextureID so store equivalent `Texture2D` references.
    static std::map<int, void*> icon_list;

    // [STATE] Tracks whether the UI is in dark mode so icons/text adapt to theme changes.
    // [UNITY] Mirror this with a ScriptableObject theme asset that exposes `bool IsDarkMode`.
    // [STATE] Tracks whether the UI is in dark mode so icons/text adapt to theme changes.
    // [UNITY] Mirror this with a ScriptableObject theme asset that exposes `bool IsDarkMode`.
    bool m_is_dark = false;

    /// <summary>
    /// Process mouse event on gizmo toolbar
    /// </summary>
    /// <param name="mouse_event">Event descriptor</param>
    /// <returns>TRUE when take responsibility for event otherwise FALSE.
    /// On true, event should not be process by others.
    /// On false, event should be process by others.</returns>
    bool gizmos_toolbar_on_mouse(const wxMouseEvent& mouse_event);

public:
    // [STATE] Shares assemble-view buffers with other gizmos so they can draw overlays without recomputing data.
    // [STATE] Shares assemble-view buffers with other gizmos so they can draw overlays without recomputing data.
    std::unique_ptr<AssembleViewDataPool> m_assemble_view_data;
    // [INTENT] Provides a stable keyset for toolbar/submenu icons so texture lookups stay predictable.
    // [INTENT] Provides a stable keyset for toolbar/submenu icons so texture lookups stay predictable.
    enum MENU_ICON_NAME {
        IC_TOOLBAR_RESET = 0,
        IC_TOOLBAR_RESET_HOVER,
        IC_TOOLBAR_RESET_ZERO,
        IC_TOOLBAR_RESET_ZERO_HOVER,
        IC_TOOLBAR_TOOLTIP,
        IC_TOOLBAR_TOOLTIP_HOVER,
        IC_NAME_COUNT,
        IC_CANVAS_MENU,
        IC_CANVAS_MENU_HOVER,
        IC_CANVAS_MENU_DARK,
        IC_CANVAS_MENU_DARK_HOVER,
        IC_CANVAS_ZOOM,
        IC_CANVAS_ZOOM_HOVER,
        IC_CANVAS_ZOOM_DARK,
        IC_CANVAS_ZOOM_DARK_HOVER,
    };

    // [INTENT] Builds the toolbar, ties into the canvas, and primes icon/layout state before first render.
    // [UNITY] Port this to a MonoBehaviour that creates child buttons for each gizmo with shared data references.
    // [INTENT] Builds the toolbar, ties into the canvas, and primes icon/layout state before first render.
    // [UNITY] Port this to a MonoBehaviour that creates child buttons for each gizmo with shared data references.
    explicit GLGizmosManager(GLCanvas3D& parent);

    // [INTENT] Points the icon loader at the correct path when DPI or theme changes.
    // [UNITY] Swap Texture2D references on toolbar buttons instead of regenerating IDs.
    // [PORTING_HAZARD:P3] Reloading textures mid-frame can flicker unless double-buffered.
    // [INTENT] Points the icon loader at the correct path when DPI or theme changes.
    // [UNITY] Swap Texture2D references on toolbar buttons instead of regenerating IDs.
    // [PORTING_HAZARD:P3] Reloading textures mid-frame can flicker unless double-buffered.
    void switch_gizmos_icon_filename();

    // [INTENT] Allocates gizmos, icon textures, and layout data; must succeed before rendering begins.
    // [STATE] Called once at startup; fails if resources are missing.
    // [INTENT] Allocates gizmos, icon textures, and layout data; must succeed before rendering begins.
    // [STATE] Called once at startup; fails if resources are missing.
    bool init();

    // [OPENGL] Uploads toolbar icon bitmaps (dark/light) into GPU textures shared by ImGui buttons.
    // [OPENGL] Uploads toolbar icon bitmaps (dark/light) into GPU textures shared by ImGui buttons.
    bool init_icon_textures();

    // [STATE] Returns the cached scale multiplier so other systems can align overlays with the toolbar.
    // [STATE] Returns the cached scale multiplier so other systems can align overlays with the toolbar.
    float get_layout_scale();

    // [OPENGL] Loads arrow glyphs so the active gizmo can highlight handles in the current theme.
    // [OPENGL] Loads arrow glyphs so the active gizmo can highlight handles in the current theme.
    bool init_arrow(const std::string& filename);

    // [THREAD] Load runs on the UI thread during project restoration so we do not race with worker gizmo updates.
    // [STATE] The temporary swap of `m_current` ensures we only mutate state after the gizmo is ready.
    // [THREAD] Load runs on the UI thread during project restoration so we do not race with worker gizmo updates.
    // [STATE] The temporary swap of `m_current` ensures we only mutate state after the gizmo is ready.
    template<class Archive> void load(Archive& ar)
    {
        if (!m_enabled)
            return;

        m_serializing = true;

        // Following is needed to know which to be turn on, but not actually modify
        // m_current prematurely, so activate_gizmo is not confused.
        EType old_current = m_current;
        ar(m_current);
        EType new_current = m_current;
        m_current         = old_current;

        // activate_gizmo call sets m_current and calls set_state for the gizmo
        // it does nothing in case the gizmo is already activated
        // it can safely be called for Undefined gizmo
        activate_gizmo(new_current);
        if (m_current != Undefined)
            m_gizmos[m_current]->load(ar);
    }

    // [STATE] Persists the active gizmo so the UI can restore it without asking the user to re-open the tool.
    // [THREAD] Called while serialization runs on the GUI thread to avoid racing with input handlers.
    // [STATE] Persists the active gizmo so the UI can restore it without asking the user to re-open the tool.
    // [THREAD] Called while serialization runs on the GUI thread to avoid racing with input handlers.
    template<class Archive> void save(Archive& ar) const
    {
        if (!m_enabled)
            return;

        ar(m_current);

        if (m_current != Undefined && !m_gizmos.empty())
            m_gizmos[m_current]->save(ar);
    }

    bool is_enabled() const { return m_enabled; }
    void set_enabled(bool enable) { m_enabled = enable; }

    void set_icon_dirty() { m_icons_texture_dirty = true; }
    // [STATE] Adjusts toolbar icon size for DPI scaling or explicit user override.
    // [STATE] Adjusts toolbar icon size for DPI scaling or explicit user override.
    void set_overlay_icon_size(float size);
    // [STATE] Tweaks the overall UI scale so the gizmo layout matches canvas zoom levels.
    // [STATE] Tweaks the overall UI scale so the gizmo layout matches canvas zoom levels.
    void set_overlay_scale(float scale);

    // [STATE] Re-syncs which gizmos should be enabled/disabled based on selection context.
    // [STATE] Re-syncs which gizmos should be enabled/disabled based on selection context.
    void refresh_on_off_state();
    // [STATE] Forces every gizmo into its default state (undoing previews/selections).
    // [STATE] Forces every gizmo into its default state (undoing previews/selections).
    void reset_all_states();
    // [EVENT] Activates a gizmo, updating `m_current` and triggering render/data refresh hooks.
    // [EVENT] Activates a gizmo, updating `m_current` and triggering render/data refresh hooks.
    bool open_gizmo(EType type);
    // [STATE] Helper used by the toolbar to ensure only one gizmo is running at a time.
    // [STATE] Helper used by the toolbar to ensure only one gizmo is running at a time.
    bool check_gizmos_closed_except(EType) const;

    // [EVENT] Signals which toolbar icon is under the pointer so tooltip/highlight text can refresh.
    // [EVENT] Signals which toolbar icon is under the pointer so tooltip/highlight text can refresh.
    void set_hover_id(int id);

    /// <summary>
    /// Distribute information about different data into active gizmo
    /// Should be called when selection changed
    /// </summary>
    // [EVENT] Called whenever selection or document changes to inform the active gizmo of new context.
    // [EVENT] Called whenever selection or document changes to inform the active gizmo of new context.
    void update_data();
    // [STATE] Pushes additional assemble view data to painter helpers to keep previews in sync.
    // [STATE] Pushes additional assemble view data to painter helpers to keep previews in sync.
    void update_assemble_view_data();

    EType get_current_type() const { return m_current; }
    // [STATE] Returns the active gizmo instance, or nullptr when none is set.
    // [STATE] Returns the active gizmo instance, or nullptr when none is set.
    GLGizmoBase* get_current() const;
    // [STATE] Helper to map an enum back to the stored gizmo instance for rendering or saving.
    // [STATE] Helper to map an enum back to the stored gizmo instance for rendering or saving.
    GLGizmoBase* get_gizmo(GLGizmosManager::EType type) const;
    EType        get_gizmo_from_name(const std::string& gizmo_name) const;

    // [STATE] True when any gizmo sub-tool or painter is streaming updates.
    // [STATE] True when any gizmo sub-tool or painter is streaming updates.
    bool is_running() const;
    // [EVENT] Checks shortcut keys (e.g., `G` for Grab) and updates `m_current` accordingly.
    // [EVENT] Checks shortcut keys (e.g., `G` for Grab) and updates `m_current` accordingly.
    bool handle_shortcut(int key);

    // [STATE] Reports whether the current gizmo is actively dragging, so the UI knows to block other inputs.
    // [STATE] Reports whether the current gizmo is actively dragging, so the UI knows to block other inputs.
    bool is_dragging() const;

    // BBS
    // [OPENGL] Provides the cached ImGui texture ID for the requested toolbar icon.
    // [PORTING_HAZARD:P3] Unity lacks ImtextureID so porters must stash equivalent Texture2D handles.
    // [OPENGL] Provides the cached ImGui texture ID for the requested toolbar icon.
    // [PORTING_HAZARD:P3] Unity lacks ImtextureID so porters must stash equivalent Texture2D handles.
    void* get_icon_texture_id(MENU_ICON_NAME icon)
    {
        if (icon_list.find((int) icon) != icon_list.end())
            return icon_list[icon];
        else
            return nullptr;
    }
    void* get_icon_texture_id(MENU_ICON_NAME icon) const
    {
        if (icon_list.find((int) icon) != icon_list.end())
            return icon_list.at(icon);
        else
            return nullptr;
    }

    // [STATE] True when the painter mode is active so the toolbar can swap to painter-specific overlays.
    bool is_paint_gizmo();
    // [STATE] Indicates whether user input should select all objects rather than individual ones.
    bool is_allow_select_all();
    // [STATE] Cached clipping plane used by GLSL to trim gizmo draws to the build volume.
    // [STATE] Cached clipping plane used by GLSL to trim gizmo draws to the build volume.
    ClippingPlane get_clipping_plane() const;
    // [STATE] Separate clipping plane for the assemble view preview.
    // [STATE] Separate clipping plane for the assemble view preview.
    ClippingPlane get_assemble_view_clipping_plane() const;
    // [STATE] Decides whether support resin operations should re-trigger after undo/redo.
    bool wants_reslice_supports_on_undo() const;

    // [STATE] Returns whether the current document is editable; can optionally emit errors for locked states.
    // [STATE] Returns whether the current document is editable; can optionally emit errors for locked states.
    bool is_in_editing_mode(bool error_notification = false) const;
    // [STATE] Signals whether instance hiding is enabled so gizmo highlights skip hidden items.
    // [STATE] Signals whether instance hiding is enabled so gizmo highlights skip hidden items.
    bool is_hiding_instances() const;

    // [STATE] Switches icon shading + background textures based on the requested theme.
    // [STATE] Switches icon shading + background textures based on the requested theme.
    void on_change_color_mode(bool is_dark);
    // [OPENGL] Renders the currently active gizmo primitives (e.g., handles, axis lines).
    // [OPENGL] Renders the currently active gizmo primitives (e.g., handles, axis lines).
    void render_current_gizmo() const;
    // [OPENGL][STATE] Renders painter-mode helpers that are not tied to a single gizmo.
    // [OPENGL][STATE] Renders painter-mode helpers that are not tied to a single gizmo.
    void render_painter_gizmo();
    // [OPENGL] Draws supplemental assemble-view overlays using cached painter data.
    // [OPENGL] Draws supplemental assemble-view overlays using cached painter data.
    void render_painter_assemble_view() const;

    // [OPENGL] Entry point for drawing the toolbar overlay background/icons every frame.
    // [OPENGL] Entry point for drawing the toolbar overlay background/icons every frame.
    void render_overlay();

    // [OPENGL][STATE] Draws the arrow indicator above the toolbar and highlights the provided gizmo.
    // [OPENGL][STATE] Draws the arrow indicator above the toolbar and highlights the provided gizmo.
    void render_arrow(const GLCanvas3D& parent, EType highlighted_type) const;

    // [STATE] Returns the tooltip text for whichever icon is currently highlighted.
    // [STATE] Returns the tooltip text for whichever icon is currently highlighted.
    std::string get_tooltip() const;

    bool on_mouse(const wxMouseEvent& mouse_event);
    bool on_mouse_wheel(const wxMouseEvent& evt);
    // [EVENT] Handles character input for keyboard shortcuts (e.g., `1`-`9`).
    // [EVENT] Handles character input for keyboard shortcuts (e.g., `1`-`9`).
    bool on_char(wxKeyEvent& evt);
    // [EVENT] Handles non-character keys such as Escape, Enter, or modifiers.
    // [EVENT] Handles non-character keys such as Escape, Enter, or modifiers.
    bool on_key(wxKeyEvent& evt);

    // [EVENT] Runs after undo/redo so the toolbar repaints or reactivates gizmos to match restored selection.
    // [EVENT] Runs after undo/redo so the toolbar repaints or reactivates gizmos to match restored selection.
    void update_after_undo_redo(const UndoRedo::Snapshot& snapshot);

    // [STATE] Convenience for ImGui to know how many icons are active.
    // [STATE] Convenience for ImGui to know how many icons are active.
    int get_selectable_icons_cnt() const { return get_selectable_idxs().size(); }

    // To end highlight set gizmo = undefined
    // [STATE] Toggles the highlight overlay that visually emphasizes a toolbar button.
    // [STATE] Toggles the highlight overlay that visually emphasizes a toolbar button.
    void set_highlight(EType gizmo, bool highlight_shown) { m_highlight = std::pair<EType, bool>(gizmo, highlight_shown); }
    // [STATE] The bool portion of `m_highlight` indicating whether highlight should be shown.
    // [STATE] The bool portion of `m_highlight` indicating whether highlight should be shown.
    bool get_highlight_state() const { return m_highlight.second; }

    // BBS: GUI refactor: GLToolbar adjust
    //  [STATE] Exposed scaled bounding box for toolbar so parent layout can adapt to zoom changes.
    float get_scaled_total_height() const;
    float get_scaled_total_width() const;
    // [STATE] Provides the helper that composes axis transforms and drag hints for manipulations.
    // [STATE] Provides the helper that composes axis transforms and drag hints for manipulations.
    GizmoObjectManipulation& get_object_manipulation() { return m_object_manipulation; }
    // [STATE] Mirror of the manipulation helper's uniform scaling toggle.
    bool get_uniform_scaling() const { return m_object_manipulation.get_uniform_scaling(); }

private:
    // [EVENT] Central dispatcher that forwards low-level SLAGizmo events to the active gizmo while honoring modifiers.
    // [EVENT] Central dispatcher that forwards low-level SLAGizmo events to the active gizmo while honoring modifiers.
    bool gizmo_event(SLAGizmoEventType action,
                     const Vec2d&      mouse_position = Vec2d::Zero(),
                     bool              shift_down     = false,
                     bool              alt_down       = false,
                     bool              control_down   = false);

    // [OPENGL] Draws the toolbar background quad with the provided extents/borders.
    // [OPENGL] Draws the toolbar background quad with the provided extents/borders.
    void render_background(float left, float top, float right, float bottom, float border_w, float border_h) const;

    // [OPENGL] Internal helper that sequences overlay drawing after state prep.
    // [OPENGL] Internal helper that sequences overlay drawing after state prep.
    void do_render_overlay() const;

    // [OPENGL] Rebuilds the icon atlas when the theme/scale changes; guard with `m_icons_texture_dirty`.
    // [OPENGL] Rebuilds the icon atlas when the theme/scale changes; guard with `m_icons_texture_dirty`.
    bool generate_icons_texture();

    // [STATE] Updates `m_hover` and tooltip text when the cursor moves over a new icon.
    // [STATE] Updates `m_hover` and tooltip text when the cursor moves over a new icon.
    void update_hover_state(const EType& type);
    // [STATE] Returns whether any toolbar grabber currently contains the cursor to block other inputs.
    // [STATE] Returns whether any toolbar grabber currently contains the cursor to block other inputs.
    bool grabber_contains_mouse() const;
};

std::string get_name_from_gizmo_etype(GLGizmosManager::EType type);

} // namespace GUI
} // namespace Slic3r

namespace cereal {
template<class Archive> struct specialize<Archive, Slic3r::GUI::GLGizmosManager, cereal::specialization::member_load_save>
{};
} // namespace cereal

#endif // slic3r_GUI_GLGizmosManager_hpp_
