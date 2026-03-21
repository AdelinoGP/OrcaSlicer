#ifndef slic3r_GLGizmoBase_hpp_
#define slic3r_GLGizmoBase_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/Color.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "slic3r/GUI/SceneRaycaster.hpp"
#include "slic3r/GUI/3DScene.hpp"

#include <cereal/archives/binary.hpp>

#include <wx/event.h>

#define ENABLE_FIXED_GRABBER 1

class wxWindow;

namespace Slic3r {

class BoundingBoxf3;
class Linef3;
class ModelObject;

namespace GUI {

class ImGuiWrapper;
class GLCanvas3D;
enum class CommonGizmosDataID;
class CommonGizmosDataPool;
class Selection;

class GLGizmoBase
{
public:
    // [INTENT] Base controller for every 3D gizmo, owning pickable handles, cached colors, and ImGui overlays shared across the GLCanvas3D
    // session. [UNITY] Map this to a MonoBehaviour base that wires MeshColliders/ProximitySensors for picking plus a
    // GraphicRaycaster-driven UI overlay that reuses a ScriptableObject color palette and pointer events. Starting value for ids to avoid
    // clashing with ids used by GLVolumes (254 is choosen to leave some space for forward compatibility)
    static const unsigned int BASE_ID                    = 255 * 255 * 254;
    static const unsigned int GRABBER_ELEMENTS_MAX_COUNT = 7;

    static float INV_ZOOM;

    // BBS colors
    static ColorRGBA                DEFAULT_BASE_COLOR;
    static ColorRGBA                DEFAULT_DRAG_COLOR;
    static ColorRGBA                DEFAULT_HIGHLIGHT_COLOR;
    static std::array<ColorRGBA, 3> AXES_COLOR;
    static std::array<ColorRGBA, 3> AXES_HOVER_COLOR;
    static ColorRGBA                CONSTRAINED_COLOR;
    static ColorRGBA                FLATTEN_COLOR;
    static ColorRGBA                FLATTEN_HOVER_COLOR;
    static ColorRGBA                GRABBER_NORMAL_COL;
    static ColorRGBA                GRABBER_HOVER_COL;
    static ColorRGBA                GRABBER_UNIFORM_COL;
    static ColorRGBA                GRABBER_UNIFORM_HOVER_COL;

    // [STATE] Shared palettes describe per-plane/drag colors that get recalculated when dark mode or theme swaps to keep Derived gizmos
    // consistent. [UNITY] Treat these as data inside a `GizmoColorPalette` ScriptableObject bound to each `GizmoHandle` prefab, ensuring
    // the same hover/active tints across handles.
    static void update_render_colors();
    static void load_render_colors();

    enum class EGrabberExtension {
        None = 0,
        PosX = 1 << 0,
        NegX = 1 << 1,
        PosY = 1 << 2,
        NegY = 1 << 3,
        PosZ = 1 << 4,
        NegZ = 1 << 5,
    };

    // Represents NO key(button on keyboard) value
    static const int NO_SHORTCUT_KEY_VALUE = 0;

protected:
    // [INTENT] Encapsulates one manipulator handle's transform, colors, and picking state so the GLCanvas renders plus raycasts keep the
    // same hover/drags. [STATE] Flags like `enabled`/`dragging` plus `picking_id`/`raycasters` capture whether the handle is active and
    // registered with the scene raycaster. [OPENGL] `render()` and `get_cube()` produce the mesh used for each cube/cone handle before
    // rasterization. [UNITY] Map each Grabber to a child GameObject with a MeshCollider + EventTrigger-driven MonoBehaviour that toggles
    // hover/drag colors from a shared ScriptableObject.
    struct Grabber
    {
        static const float SizeFactor;
        static const float MinHalfSize;
        static const float DraggingScaleFactor;
        static const float FixedGrabberSize;
        static const float FixedRadiusSize;

        bool              enabled{true};
        bool              dragging{false};
        Vec3d             center{Vec3d::Zero()};
        Vec3d             angles{Vec3d::Zero()};
        Transform3d       matrix{Transform3d::Identity()};
        ColorRGBA         color{GRABBER_NORMAL_COL};
        ColorRGBA         hover_color{GRABBER_HOVER_COL};
        EGrabberExtension extensions{EGrabberExtension::None};
        // the picking id shared by all the elements
        int                                                                         picking_id{-1};
        std::array<std::shared_ptr<SceneRaycasterItem>, GRABBER_ELEMENTS_MAX_COUNT> raycasters = {nullptr};

        Grabber() = default;
        ~Grabber();

        void render(bool hover, float size) { render(size, hover ? hover_color : color); }

        float         get_half_size(float size) const;
        float         get_dragging_half_size(float size) const;
        PickingModel& get_cube();

        // [EVENT][THREAD] Register/unregister ensures each handle keeps its SceneRaycasterItem live while the GL thread owns pick IDs.
        void register_raycasters_for_picking(int id);
        void unregister_raycasters_for_picking();

    private:
        void render(float size, const ColorRGBA& render_color);

        static PickingModel s_cube;
        static PickingModel s_cone;
    };

public:
    // [STATE] Basic lifecycle switch used by the UI to track whether the gizmo is active or dormant.
    // [EVENT] Transitions fire UI updates and can spawn tooltips/help text when moving from Off→On.
    enum EState { Off, On, Num_States };

    // [INTENT] Bundles the current picking ray + mouse pixel for derived gizmos to interpret drags or highlight logic.
    // [THREAD] Captured on the GL/Qt input thread before invoking `on_dragging` so derived classes can stay UI-thread safe.
    struct UpdateData
    {
        const Linef3& mouse_ray;
        const Point&  mouse_pos;

        UpdateData(const Linef3& mouse_ray, const Point& mouse_pos) : mouse_ray(mouse_ray), mouse_pos(mouse_pos) {}
    };

protected:
    // [STATE] Pointers and flags backing the gizmo lifecycle: parent canvas, shortcut keys, hover identifiers, grabbers, and ImGui
    // overlays. [THREAD] All members above are touched on the UI/GL worker thread and must not be mutated from background jobs.
    GLCanvas3D& m_parent;

    int                          m_group_id; // TODO: remove only for rotate
    EState                       m_state;
    int                          m_shortcut_key;
    std::string                  m_icon_filename;
    unsigned int                 m_sprite_id;
    int                          m_hover_id{-1};
    bool                         m_dragging{false};
    mutable std::vector<Grabber> m_grabbers;
    ImGuiWrapper*                m_imgui;
    bool                         m_first_input_window_render{true};
    // [STATE] Optional shared pool for common gizmo data so color/matrix lookups reuse cached meshes.
    CommonGizmosDataPool* m_c{nullptr};

    bool m_is_dark_mode = false;

    // [INTENT] Helper that folds ImGui combo boxes into the gizmo UI so scriptable states can be selected from a dropdown.
    // [UNITY] Replace with a UI Toolkit `PopupWindow` bound to VisualElement `ListView` backed by the same selection model.
    bool render_combo(
        const std::string& label, const std::vector<std::string>& lines, int& selection_idx, float label_width, float item_width);
    void render_cross_mark(const Vec3f& target, bool is_single = false);

public:
    GLGizmoBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    virtual ~GLGizmoBase() = default;

    bool init() { return on_init(); }

    // [EVENT] `load`/`save` participate in undo/redo snapshots; derived gizmos must remain consistent once serialization writes/reads
    // state. [THREAD] These run on the UI thread so callbacks like `on_load` never race with the GL render loop.
    void load(cereal::BinaryInputArchive& ar)
    {
        m_state = On;
        on_load(ar);
    }
    void save(cereal::BinaryOutputArchive& ar) const { on_save(ar); }

    std::string get_name(bool include_shortcut = true) const;

    EState get_state() const { return m_state; }
    // [EVENT] State flips trigger derived callbacks so the UI can update cursor text, tooltips, and selection overlays atomically.
    void set_state(EState state)
    {
        m_state = state;
        on_set_state();
    }

    int get_shortcut_key() const { return m_shortcut_key; }

    const std::string& get_icon_filename() const { return m_icon_filename; }

    void set_icon_filename(const std::string& filename);

    bool                is_activable() const { return on_is_activable(); }
    bool                is_selectable() const { return on_is_selectable(); }
    CommonGizmosDataID  get_requirements() const { return on_get_requirements(); }
    virtual bool        wants_enter_leave_snapshots() const { return false; }
    virtual std::string get_gizmo_entering_text() const
    {
        assert(false);
        return "";
    }
    virtual std::string get_gizmo_leaving_text() const
    {
        assert(false);
        return "";
    }
    virtual std::string get_action_snapshot_name() const;
    void                set_common_data_pool(CommonGizmosDataPool* ptr) { m_c = ptr; }

    virtual bool apply_clipping_plane() { return true; }

    /// <summary>
    /// Implement when want to process mouse events in gizmo
    /// Click, Right click, move, drag, ...
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information and don't want to propagate it otherwise False.</returns>
    // [EVENT] Derived gizmos override this to interpret clicks/drags, and returning true stops wxWidgets propagation back to the GLCanvas.
    // [PORTING_HAZARD:P2] Unity port will need to route `PointerEventData` into this path instead of wxMouseEvent.
    virtual bool on_mouse(const wxMouseEvent& mouse_event) { return false; }
    unsigned int get_sprite_id() const { return m_sprite_id; }

    int get_hover_id() const { return m_hover_id; }
    // [STATE][EVENT] Tracks the currently hovered grabber so UI hints, tooltip text, and cursor updates stay in sync with raycast hits.
    void set_hover_id(int id);

    bool is_dragging() const { return m_dragging; }

    // returns True when Gizmo changed its state
    // [STATE][EVENT] Run every input cycle to refresh hover/drag flags and emit UI notifications when derived gizmos mutate their state.
    bool update_items_state();

    void render() { on_render(); }
    // [OPENGL][EVENT] Invoked from the GLCanvas idle loop to draw helper widgets via ImGui while respecting the gizmo's screen position.
    // [UNITY] Replace with a UI Toolkit panel driven by a WorldSpace camera plus `ScreenPointToWorldPointInRectangle` so Unity can overlay
    // input fields over the gizmo.
    void         render_input_window(float x, float y, float bottom_limit);
    virtual void on_change_color_mode(bool is_dark) { m_is_dark_mode = is_dark; }

    /// <summary>
    /// Mouse tooltip text
    /// </summary>
    /// <returns>Text to be visible in mouse tooltip</returns>
    virtual std::string get_tooltip() const { return ""; }

    int         get_count() { return ++count; }
    std::string get_gizmo_name() { return on_get_name(); }

    /// <summary>
    /// Is called when data (Selection) is changed
    /// </summary>
    virtual void data_changed(bool is_serializing) {};

    // [EVENT][THREAD] Called when Gizmo becomes active/inactive so all grabbers get live SceneRaycaster items on the GL thread.
    void register_raycasters_for_picking()
    {
        register_grabbers_for_picking();
        on_register_raycasters_for_picking();
    }
    void unregister_raycasters_for_picking()
    {
        unregister_grabbers_for_picking();
        on_unregister_raycasters_for_picking();
    }

    virtual bool is_in_editing_mode() const { return false; }
    virtual bool is_selection_rectangle_dragging() const { return false; }

protected:
    float                      last_input_window_width = 0;
    virtual bool               on_init()               = 0;
    virtual void               on_load(cereal::BinaryInputArchive& ar) {}
    virtual void               on_save(cereal::BinaryOutputArchive& ar) const {}
    virtual std::string        on_get_name() const = 0;
    virtual void               on_set_state() {}
    virtual void               on_set_hover_id() {}
    virtual bool               on_is_activable() const { return true; }
    virtual bool               on_is_selectable() const { return true; }
    virtual CommonGizmosDataID on_get_requirements() const { return CommonGizmosDataID(0); }
    virtual void               on_enable_grabber(unsigned int id) {}
    virtual void               on_disable_grabber(unsigned int id) {}

    // called inside use_grabbers
    virtual void on_start_dragging() {}
    virtual void on_stop_dragging() {}
    virtual void on_dragging(const UpdateData& data) {}

    virtual void on_render() = 0;
    virtual void on_render_input_window(float x, float y, float bottom_limit) {}

    // [INTENT][EVENT] Helpers to open/position ImGui windows anchored to the gizmo; wrappers keep wxWidgets events synchronized with ImGui
    // draws. [PORTING_HAZARD:P2] Unity lacks native ImGui, so porters must replicate these flows via UI Toolkit overlays or a custom editor
    // window.
    bool GizmoImguiBegin(const std::string& name, int flags);
    void GizmoImguiEnd();
    void GizmoImguiSetNextWIndowPos(float& x, float y, int flag, float pivot_x = 0.0f, float pivot_y = 0.0f);
    void GizmoImguiSetNextWIndowPos(float& x, float y, float w, float h, int flag, float pivot_x = 0.0f, float pivot_y = 0.0f);

    void         register_grabbers_for_picking();
    void         unregister_grabbers_for_picking();
    virtual void on_register_raycasters_for_picking() {}
    virtual void on_unregister_raycasters_for_picking() {}

    // [OPENGL] Shared helpers that draw grabber cubes/cones inside the current bounding box before the main GL pass completes.
    void render_grabbers(const BoundingBoxf3& box) const;
    void render_grabbers(float size) const;
    void render_grabbers(size_t first, size_t last, float size, bool force_hover) const;

    std::string format(float value, unsigned int decimals) const;

    // Mark gizmo as dirty to Re-Render when idle()
    // [THREAD][PORTING_HAZARD:P3] `set_dirty` flips `m_dirty` to schedule rerendering via the GLCanvas idle loop, which Unity needs to
    // mimic with explicit repaint requests (e.g., `Camera.Render`).
    void set_dirty();

    /// <summary>
    /// function which
    /// Set up m_dragging and call functions
    /// on_start_dragging / on_dragging / on_stop_dragging
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>same as on_mouse</returns>
    // [EVENT][THREAD] This helper sequences mouse drags, calling derived `on_dragging` between `on_start_dragging`/`on_stop_dragging` on
    // the UI thread. [UNITY] The port should mirror it with `IPointerDownHandler`/`IDragHandler` callbacks into the MonoBehaviour that
    // wraps this base logic.
    bool use_grabbers(const wxMouseEvent& mouse_event);

    void do_stop_dragging(bool perform_mouse_cleanup);

private:
    // Flag for dirty visible state of Gizmo
    // When True then need new rendering
    bool m_dirty{false};
    int  count = 0;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoBase_hpp_
