#ifndef _ImageDPIFrame_H_
#define _ImageDPIFrame_H_

#include "GUI_App.hpp"
#include "GUI_Utils.hpp"

class wxStaticBitmap;
namespace Slic3r { namespace GUI {
// [INTENT] Floating DPI-aware helper frame that surfaces image previews and keeps sync with the main ImageGrid scale.
// [UNITY] Translate to an UI Toolkit Panel + ImageElement backed by a Texture2D that reimports the bitmap and updates layout whenever the
// shared settings ScriptableObject's DPI value changes. [PORTING_HAZARD:P2] wxWidgets auto DPI rect hints and wxRect rounding will need
// manual scaling computation per display in Unity because `wxRect` suggestions don't exist there.
class ImageDPIFrame : public Slic3r::GUI::DPIFrame
{
public:
    ImageDPIFrame();
    // [THREAD] Tear-down runs on the GUI thread so timers and wxStaticBitmap cleanup happen before destruction;
    // Unity would mirror this in MonoBehaviour.OnDestroy by stopping coroutines and releasing Texture2D references.
    ~ImageDPIFrame() override;
    // [EVENT] Respond to dpi events dispatched from the shared DPI frame when monitor scale shifts.
    void on_dpi_changed(const wxRect& suggested_rect) override;
    // [STATE] Color changes propagate via wxWidgets color change broadcast; tracked to update the static bitmap border/text.
    void sys_color_changed();
    // [EVENT] Called when the frame is stamped visible to keep Unity's equivalent in-sync with runtime layout.
    void on_show();
    // [EVENT] Hides also reset running timers so Unity can disable its coroutine refresh helpers.
    void on_hide();
    // [STATE] Overrides wxFrame Show to gate timer lifetime around actual visibility.
    bool Show(bool show = true) override;

    // [INTENT] Replace the painted bitmap while ensuring Unity's Texture2D cache updates on the main thread.
    void set_bitmap(const wxBitmap& bit_map);
    // [STATE] Image size cache that determines metric and text overlays plus Unity scaling calculations.
    int get_image_px() { return m_image_px; }
    // [INTENT] Brick title string so Unity's window/toolbar label can mirror the same context (Texture name / DPI label).
    void set_title(const wxString& title);

private:
    // [THREAD] wxTimer hooks run on the GUI thread; Unity will need a main-thread coroutine to poll for refresh state.
    void init_timer();
    // [EVENT] Timer fires to throttle repaint; equivalent to Unity's `InvokeRepeating` or coroutine tick with `Time.deltaTime` sampling.
    void on_timer(wxTimerEvent& event);

private:
    // [STATE] Bitmap container mirroring the latest preview texture; Unity should replace with `Texture2D` and `UI.Image`/`RawImage` element.
    wxStaticBitmap* m_bitmap = nullptr;
    // [STATE] Layout sizer maintaining bitmap + title stacking; Unity counterpart is a VerticalLayoutGroup or UI Toolkit `VisualElement` stack.
    wxBoxSizer* m_sizer_main{nullptr};
    // [STATE] Pixel width of the currently attached image; key for text overlays and metric computations.
    int m_image_px;
    // [STATE] Cached header text so set_title can avoid recomputing strings; Unity should mirror this via ScriptableObject metadata.
    wxString m_title_str;
    // [STATE] Static text element showing title; Unity analog is a TextMeshProUGUI or UI Toolkit Label.
    wxStaticText* m_title;
    // [THREAD] wxTimer is tied to the main GUI thread; Unity needs its own scheduler around `Coroutine`/`InvokeRepeating` so texture
    // uploads don't race.
    // [PORTING_HAZARD:P3] wxTimer keeps firing until the event binding is removed; Unity must cancel its coroutine when the frame hides or
    // destroys to avoid writing to dead textures or leaking UI references.
    wxTimer* m_refresh_timer{nullptr};
    // [STATE] Incremental counter used to debounce refreshes; in Unity, store as float delta until the next `Update` cycle.
    float m_timer_count = 0;
};
}} // namespace Slic3r::GUI
#endif // _STEP_MESH_DIALOG_H_
