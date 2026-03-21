#ifndef slic3r_GLGizmoFdmSupports_hpp_
#define slic3r_GLGizmoFdmSupports_hpp_

#include "GLGizmoPainterBase.hpp"
// BBS
#include "libslic3r/Print.hpp"
#include "libslic3r/ObjectID.hpp"
#include "slic3r/GUI/3DScene.hpp"

#include <boost/thread.hpp>

namespace Slic3r::GUI {

// [INTENT] Paint-on support surface editor layered over the 3D canvas so users can sculpt support scaffolds before slicing.
class GLGizmoFdmSupports : public GLGizmoPainterBase
{
public:
    GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    // [OPENGL][INTENT] Issue the painter gizmo draw calls, blending the support preview volume with the existing scene.
    // [UNITY] Replace with a RenderTexture-backed preview camera + CommandBuffer that overlays mesh highlights.
    void render_painter_gizmo() override;

    // BBS: add edit state
    //  [STATE] Tracks whether the support painter is idle, generating previews, or showing ready results to gate input.
    enum EditState { state_idle = 0, state_generating = 1, state_ready };

    // BBS
    // [EVENT] Keyboard shortcuts that cycle between support tools; Unity should wire InputSystem key events to the same toggles.
    bool on_key_down_select_tool_type(int keyCode);

protected:
    // [EVENT][OPENGL] Render the in-canvas UI controls (angle slider, hints) so painters can adjust thresholds; Unity can layer a Canvas
    // over the RenderTexture instead.
    void on_render_input_window(float x, float y, float bottom_limit) override;
    // [INTENT][UNITY] Provides the localized display name used for toolbar/context menus; Unity can mirror this via its
    // LocalizationSettings.StringDatabase entries.
    std::string on_get_name() const override;

    // [EVENT] Called when the gizmo becomes active to refresh caches before opening the painter session.
    void on_set_state() override;
    // [EVENT][UNITY] Compose the tooltip text for the input window; triggered during mouse hover in the painter overlay, and used by UI
    // Toolkit tooltip bindings in Unity.
    void show_tooltip_information(float caption_max, float x, float y);
    // [EVENT][STATE] Provides snapshot label for the undo stack, including Shift modifiers; Unity should sync to its own SnapshotManager.
    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    // [STATE][UNITY] Entry/exit/action strings quoted here drive the UI banners and undo captions; Unity can reuse them through localized
    // ScriptableObjects.
    std::string get_gizmo_entering_text() const override { return "Entering Paint-on supports"; }
    std::string get_gizmo_leaving_text() const override { return "Leaving Paint-on supports"; }
    std::string get_action_snapshot_name() const override { return "Paint-on supports editing"; }

    // BBS
    // [STATE] Tracks the current mouse-tool symbol so the UI state syncs with keyboard toggles and brushes.
    wchar_t m_current_tool = 0;

private:
    // [INTENT][OPENGL] Prepare painter-specific resources before the gizmo is activated so the GL overlay has everything it needs; Unity
    // would mirror this in `OnEnable`.
    bool on_init() override;

    // BBS: remove const
    // [STATE][UNITY] Re-syncs support settings when the underlying object changes to keep the preview consistent with the project; Unity
    // should refresh its painter cache the same way.
    void update_model_object() override;
    // BBS: add logic to distinguish the first_time_update and later_update
    // [THREAD] Distinguishes the first snapshot from incremental updates so worker generation can throttle duplicates.
    void update_from_model_object(bool first_update) override;
    // [EVENT] Called when a different paint tool is selected; Unity should expose the same toggle event from its toolbar.
    void tool_changed(wchar_t old_tool, wchar_t new_tool);

    // [STATE][EVENT] Handles transitions to/from the painter mode so resources (like preview volumes) are reset correctly.
    void on_opening() override;
    void on_shutdown() override;
    // [INTENT] Identifies this gizmo as the support painter, used by the base class to choose icons/menus.
    PainterGizmoType get_painter_type() const override;

    // [STATE][INTENT] Highlights mesh facets whose normals exceed the given threshold, enabling context-aware selection in the painter.
    void select_facets_by_angle(float threshold, bool block);
    // BBS
    // [STATE][UNITY] Returns the slider value driving selection angle; Unity should expose this as a FloatField on the painter panel.
    int get_selection_support_threshold_angle();

    // [STATE][UNITY] Cached slider value for the facet angle threshold; drives how aggressive support selection is and ties to the Unity
    // FloatField slider.
    int m_support_threshold_angle = -1;

    // BBS: add support preview logic
    // [INTENT][STATE][THREAD] Captures a frozen set of print settings so preview generation never races with live edits; Unity can freeze
    // the relevant ScriptableObject snapshot before queuing a Job/Task.
    void init_print_instance();
    // [THREAD][OPENGL] Refreshes the cached GLVolume from the print data; invoked on the UI thread after the worker completes.
    void update_support_volumes();
    // [STATE][EVENT] Triggered whenever the painter config changes so cached meshes are invalidated before the next render pass; Unity
    // should invalidate its RenderTexture the same way.
    void invalid_support_volumes(bool invalid_step = false);
    // [STATE][EVENT][UNITY] Answers whether user actions require a new generation cycle (slider moved, tool change); Unity should hook this
    // into its UI-triggered Task scheduling.
    bool need_regenerate_support_volumes();
    // [THREAD][PORTING_HAZARD:P2] Recomputes support volume geometry on the dedicated thread before the UI renders it; Unity needs a Job
    // System worker + main-thread marshaling to keep the shared Mesh/Volume safe.
    void generate_support_volume();
    // [THREAD][PORTING_HAZARD:P2] Background worker that races against the GL render thread; Unity must replace this with Task/Job +
    // main-thread dispatch boundaries.
    void run_thread();
    // [STATE][UNITY] Angle threshold that drives facet selection heuristics; Unity should keep the painter panel slider synchronized with
    // this value.
    float m_angle_threshold_deg = 40.f;
    // [STATE] Indicates whether the last generation completed successfully so rendering can trust cached data.
    bool m_volume_valid = false;

    // [OPENGL][UNITY] Generated preview mesh consumed in `render_painter_gizmo` and updated only when `m_volume_ready` is true; Unity
    // should convert this into a MeshFilter/MeshRenderer pair.
    GLVolume* m_support_volume = NULL;
    // [STATE] Guards access to `m_support_volume` so the render path knows if generation is complete.
    mutable bool m_volume_ready = false;
    // [STATE] Switch between block/tree support modes; the UI toggles this value while rendering.
    bool m_is_tree_support = false;
    // [STATE][THREAD] Cancellation flag observed by the worker thread and UI; Unity's CancellationToken should guard this shared signal.
    bool m_cancel = false;
    // [STATE] Tracks the object whose supports are being painted.
    size_t m_object_id;
    // [STATE][THREAD] Timestamps for incremental generation; updated on the UI thread after worker completes.
    std::vector<ObjectBase::Timestamp> m_volume_timestamps;
    // [STATE][UNITY] Snapshotted print instance for deterministic preview generation independent of live edit state; Unity can feed this
    // snapshot into its Job System via a serializable asset.
    PrintInstance m_print_instance;
    // [STATE] Indicates the current edit lifecycle (idle, generating, ready) so UI and generator stay in sync.
    mutable EditState m_edit_state;
    // thread
    // [THREAD][PORTING_HAZARD:P2] boost::thread drives generation; Unity must swap for Task/Job System and respect main-thread GLVolume mutation.
    boost::thread m_thread;
    // [THREAD] Synchronizes the worker with the main thread when swapping preview volumes.
    std::mutex m_mutex;
    // [STATE][PORTING_HAZARD:P2] Counts pending generate requests to avoid redundant worker restarts; Unity's Task scheduler must respect
    // this counter to avoid duplicate Jobs.
    int m_generate_count;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    // [STATE][UNITY] Localized string cache; Unity should hook this into its localization table rather than re-rendering per frame.
    std::map<std::string, wxString> m_desc;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoFdmSupports_hpp_
