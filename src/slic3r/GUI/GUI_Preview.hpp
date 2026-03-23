#ifndef slic3r_GUI_Preview_hpp_
#define slic3r_GUI_Preview_hpp_

#include <wx/panel.h>

#include "libslic3r/Point.hpp"
#include "libslic3r/CustomGCode.hpp"

// BBS: add print base
#include "libslic3r/PrintBase.hpp"

#include <string>
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include <slic3r/GUI/GCodeViewer.hpp>

class wxGLCanvas;
class wxBoxSizer;
class wxStaticText;
class wxComboBox;
class wxComboCtrl;
class wxCheckBox;

namespace Slic3r {

class DynamicPrintConfig;
class Print;
class BackgroundSlicingProcess;
class Model;

namespace GUI {

// [INTENT][UNITY] Exposes the 3D preview components (View3D, Preview, AssembleView) that bridge wxWidgets GLCanvas samples and the slicer
// state; plan to rehost these in Unity as RenderTexture cameras + UI Toolkit controls tracking the same options and background slicing state.

class GLCanvas3D;
class GLToolbar;
class Bed3D;
struct Camera;
class Plater;
#ifdef _WIN32
// [PORTING_HAZARD:P3] Windows-only BitmapComboBox is not available in Unity; replace with a skinned UI Toolkit dropdown or custom IMGUI container.
class BitmapComboBox;
#endif

// [INTENT][UNITY] Wraps the GLCanvas3D viewport so the Preview panel exposes selection helpers and view navigation; Unity should translate
// this into a RenderTexture camera plus a selection controller MonoBehaviour.
class View3D : public wxPanel
{
    // [STATE][OPENGL] Owns the wxGLCanvas widget that backs the OpenGL context.
    wxGLCanvas* m_canvas_widget;
    // [STATE][OPENGL] Stores the renderer instance used for draw calls and gizmo interactions.
    GLCanvas3D* m_canvas;

public:
    View3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
    virtual ~View3D();

    wxGLCanvas* get_wxglcanvas() { return m_canvas_widget; }
    GLCanvas3D* get_canvas3d() { return m_canvas; }

    // [STATE][OPENGL] Flags the canvas so GPU redraws happen after scene updates.
    void set_as_dirty();
    // [EVENT][STATE] Tracks bed changes that influence the 3D preview mesh.
    void bed_shape_changed();
    // [EVENT][UNITY] Ties plate-count updates to Unity UI counters and camera resets.
    void plates_count_changed();

    // [EVENT][UNITY] Rotates to a preset direction; Unity ports can hook this into Camera rigs or dropdowns.
    void select_view(const std::string& direction);

    // BBS
    // [EVENT][STATE] Commands wired to toolbar buttons to grow/shrink selection; Unity should update its selection manager when these run.
    void select_curr_plate_all();
    void select_object_from_idx(std::vector<int>& object_idxs);
    void remove_curr_plate_all();

    // [EVENT] Convenience focus and cleanup helpers driven by keyboard shortcuts.
    void select_all();
    void deselect_all();
    void exit_gizmo();
    void delete_selected();
    void center_selected();
    void drop_selected();
    void center_selected_plate(const int plate_idx);
    void mirror_selection(Axis axis);

    // [STATE] Reflects whether layer editing mode is active and allowed for the current selection.
    bool is_layers_editing_enabled() const;
    bool is_layers_editing_allowed() const;
    void enable_layers_editing(bool enable);

    // [STATE] Queries the GLCanvas for drag operations.
    bool is_dragging() const;
    // [STATE] Detects if reload requests are being throttled.
    bool is_reload_delayed() const;

    // [THREAD][OPENGL] Forces the view to rebuild, coordinating worker results with the UI thread.
    void reload_scene(bool refresh_immediately, bool force_full_scene_refresh = false);
    // [OPENGL][UNITY] Schedules a GL draw; Unity ports will hook RenderTexture updates instead.
    void render();

private:
    bool init(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
};

class Preview : public wxPanel
{
    // [STATE][OPENGL] Mirrors the GLCanvas and renderer owned by the preview pane.
    wxGLCanvas* m_canvas_widget{nullptr};
    GLCanvas3D* m_canvas{nullptr};
    // [STATE][UNITY] Shared print configuration that Unity ports should access via ScriptableObject-backed models.
    DynamicPrintConfig* m_config;
    // [THREAD] Reference to the background slicing process whose results drive reloads.
    BackgroundSlicingProcess* m_process;
    // [STATE][THREAD] Keeps the latest G-code processor output so the GL canvas can read mesh layers.
    GCodeProcessorResult* m_gcode_result;

    // Calling this function object forces Plater::schedule_background_process.
    // [EVENT][THREAD] Callback that schedules the Plater background slicing process; must be invoked on the UI thread.
    std::function<void()> m_schedule_background_process;

    // [STATE] Tracks how many extruders are available for colour/filament overlays.
    unsigned int m_number_extruders{1};
    // [STATE] Hints whether the preview should keep its current mode when reloading.
    bool m_keep_current_preview_type{false};

    // bool m_loaded { false };
    // BBS: add logic for preview print
    // [STATE] Points to the PrintBase currently previewed (if any) so the UI knows whether loading happened.
    const Slic3r::PrintBase* m_loaded_print{nullptr};
    // BBS: add only gcode mode
    // [STATE][PORTING_HAZARD:P3] Indicates whether only G-code preview is allowed, affecting how Unity replays this fallback mode.
    bool m_only_gcode{false};
    // [STATE][THREAD] Indicates whether we should repaint after the slicing worker finishes.
    bool m_reload_paint_after_background_process_apply{false};

public:
    // [STATE][INTENT][UNITY] Describes each preview toggle group (travel, wipe, retraction, etc.) so Unity can mirror them with
    // ToggleGroups or ToolButtons.
    enum class OptionType : unsigned int {
        Travel,
        Wipe,
        Retractions,
        Unretractions,
        Seams,
        ToolChanges,
        ColorChanges,
        PausePrints,
        CustomGCodes,
        Shells,
        ToolMarker,
        Legend
    };

    // [INTENT][THREAD] Builds the preview panel and wires the GL canvas with the slicing process scheduling helper.
    Preview(
        wxWindow*                 parent,
        Bed3D&                    bed,
        Model*                    model,
        DynamicPrintConfig*       config,
        BackgroundSlicingProcess* process,
        GCodeProcessorResult*     gcode_result,
        std::function<void()>     schedule_background_process = []() {});
    virtual ~Preview();

    // BBS: update gcode_result
    // [THREAD][STATE] Called from the background worker to swap in the latest processor result and refresh sliders.
    void update_gcode_result(GCodeProcessorResult* gcode_result);

    wxGLCanvas* get_wxglcanvas() { return m_canvas_widget; }
    GLCanvas3D* get_canvas3d() { return m_canvas; }

    // [EVENT][OPENGL] Flags redraw after model or config changes.
    void set_as_dirty();

    // [EVENT][STATE] Reacts to bed changes published by other UI modules.
    void bed_shape_changed();
    // [EVENT][UNITY] Mirrors view selection commands, which Unity should map to camera routines.
    void select_view(const std::string& direction);
    // [EVENT][THREAD] Updates the drop target used for drag/drop; Unity should expose the same capability through InputSystem drop hooks.
    void set_drop_target(wxDropTarget* target);

    // BBS: add only gcode mode
    // [INTENT][STATE] Loads the specified print or G-code-only payload, optionally preserving the previous Z-range.
    void load_print(bool keep_z_range = false, bool only_gcode = false);
    // [EVENT] Refreshes the current preview when toggling modes.
    void reload_print(bool only_gcode = false);
    // BBS: always load shell at preview
    // [STATE][OPENGL] Switches meshes to the provided shells for rendering.
    void load_shells(const Print& print, bool force_previewing = false);
    // [STATE] Clears transient shell overlays before reloading.
    void reset_shells();

    // [EVENT] Recomputes layout when DPI/resolution changes under MSW.
    void msw_rescale();
    // [EVENT][STATE] Reacts to system color changes (themes, high contrast).
    void sys_color_changed();

    // BBS: add m_loaded_print logic
    // [STATE] Indicates whether a PrintBase asset is active.
    bool is_loaded() const { return (m_loaded_print != nullptr); }
    // BBS
    // [EVENT] Model tick updates from the render loop.
    void on_tick_changed(Type type);

    // [STATE][UNITY] Exposes slider visibility toggles for the Unity toolbar/VisualElement tree.
    void show_sliders(bool show = true);
    void show_moves_sliders(bool show = true);
    void show_layers_sliders(bool show = true);
    void set_reload_paint_after_background_process_apply(bool flag) { m_reload_paint_after_background_process_apply = flag; }
    bool get_reload_paint_after_background_process_apply() { return m_reload_paint_after_background_process_apply; }

private:
    // [INTENT][THREAD] Shared initializer that sets up the GL context and event bindings.
    bool init(wxWindow* parent, Bed3D& bed, Model* model);

    // [EVENT] Binds the wx size/interactions and tears them down when the panel dies.
    void bind_event_handlers();
    void unbind_event_handlers();
    void on_size(wxSizeEvent& evt);
    // Create/Update/Reset double slider on 3dPreview
    // [STATE][UNITY] Maintains CustomGCode ticks plus slider ranges; Unity should keep the slider values synchronized through binding.
    void check_layers_slider_values(std::vector<CustomGCode::Item>& ticks_from_model, const std::vector<double>& layers_z);

    // [STATE] Layer slider helpers invoked after interactive or programmatic camera changes.
    void update_layers_slider(const std::vector<double>& layers_z, bool keep_z_range = false);
    void update_layers_slider_mode();
    void update_layers_slider_from_canvas(wxKeyEvent& event);
    // BBS: add only gcode mode
    // [EVENT][STATE] Forces loading the print via the FFF path for unsupported G-code mode toggles.
    void load_print_as_fff(bool keep_z_range = false, bool only_gcode = false);
};

// [INTENT][UNITY] Provides the assemble-mode view used after slicing, which Unity can mirror with another RenderTexture + camera pair.
class AssembleView : public wxPanel
{
    // [STATE][OPENGL] Owns a separate GL canvas for assemble preview rendering.
    wxGLCanvas* m_canvas_widget{nullptr};
    GLCanvas3D* m_canvas{nullptr};

public:
    // [INTENT][THREAD] Constructs a secondary view that shows assembly-specific layers; Unity should share the same data pipeline for the
    // additional camera.
    AssembleView(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
    ~AssembleView();

    wxGLCanvas* get_wxglcanvas() { return m_canvas_widget; }
    GLCanvas3D* get_canvas3d() { return m_canvas; }

    // [STATE] Marks this canvas dirty so the renderer refreshes the assemble view.
    void set_as_dirty();
    // [OPENGL] Triggers GPU draws for this pane.
    void render();

    // [STATE] Queries the reload throttle for this view.
    bool is_reload_delayed() const;
    // [THREAD][OPENGL] Forces an immediate or deferred scene rebuild for the assemble canvas.
    void reload_scene(bool refresh_immediately, bool force_full_scene_refresh = false);
    void select_view(const std::string& direction);

private:
    bool init(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Preview_hpp_
