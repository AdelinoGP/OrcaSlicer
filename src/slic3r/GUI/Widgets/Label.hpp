#ifndef slic3r_GUI_Label_hpp_
#define slic3r_GUI_Label_hpp_

#include <wx/stattext.h>

#define LB_HYPERLINK 0x0020
#define LB_PROPAGATE_MOUSE_EVENT 0x0040
#define LB_AUTO_WRAP 0x0080
// [INTENT][PORTING_HAZARD:P3] Flag bits the shared label relies on to toggle hyperlink styling, mouse bubbling, and auto-wrap; Unity will
// need an explicit `Button`/`TextMeshProUGUI` combo to replicate these bits while preserving cursor event routing.

// [INTENT][STATE] Consolidates wxStaticText into a consistent font/cursor helper so the GUI always draws with the shared font palette and
// hyperlink semantics. [UNITY] Map to a `TextMeshProUGUI` housed under a `GraphicRaycaster` canvas with a `PointerEnter/PointerClick`
// handler to reproduce hyperlink hover/click behavior.
class Label : public wxStaticText
{
public:
    Label(wxWindow* parent, wxString const& text = {}, long style = 0, wxSize size = wxDefaultSize);

    Label(wxWindow* parent, wxFont const& font, wxString const& text = {}, long style = 0, wxSize size = wxDefaultSize);

    // [STATE][EVENT] Keeps the cached string/color in sync whenever external callers change the label text; hooks into hyperlink markup to
    // keep the cursor state consistent.
    void SetLabel(const wxString& label) override;

    // [STATE][EVENT] Re-evaluates the `LB_*` flags to rewrap, recolor, and propagate mouse events when styles mutate during layout changes.
    void SetWindowStyleFlag(long style) override;

    // [INTENT][EVENT] Triggered during layout to split the label text when `LB_AUTO_WRAP` is set; uses the helper below to recompute
    // multi-line content.
    void Wrap(int width);

private:
    // [EVENT][THREAD] wxSizeEvent happens on the UI thread; we guard rewraps with `m_skip_size_evt` so Unity ports can avoid recursive
    // layout updates.
    void OnSize(wxSizeEvent& evt);

private:
    // [STATE][UNITY] Cache the latest font/color pair so layout refreshes replay the same style; in Unity this would be a `TMP_FontAsset`
    // plus `Color` stored on the `TextMeshProUGUI` component.
    wxFont   m_font;
    wxColour m_color;
    wxString m_text;
    // [STATE] Stores whether the size handler is already re-entered to prevent infinite wrap-invalidation loops.
    bool m_skip_size_evt = false;

public:
    static wxFont Head_48;
    static wxFont Head_32;
    static wxFont Head_24;
    static wxFont Head_20;
    static wxFont Head_18;
    static wxFont Head_16;
    static wxFont Head_15;
    static wxFont Head_14;
    static wxFont Head_13;
    static wxFont Head_12;
    static wxFont Head_11;
    static wxFont Head_10;

    static wxFont Body_16;
    static wxFont Body_15;
    static wxFont Body_14;
    static wxFont Body_13;
    static wxFont Body_12;
    static wxFont Body_10;
    static wxFont Body_11;
    static wxFont Body_9;
    static wxFont Body_8;
    // [STATE][UNITY] Shared font cache used during startup so every label instantly reuses the system font metrics; Unity equivalent is a
    // `FontLibrary` ScriptableObject that caches `TMP_FontAsset` references keyed by weight/size.

    static void initSysFont();
    // [STATE][THREAD] Call once on the UI thread before accessing `Head_*`/`Body_*`; Unity must eagerly construct TMP font assets on the
    // main thread to avoid runtime exceptions.

    static wxFont sysFont(int size, bool bold = false);
    // [INTENT][UNITY] Helper that picks the matching preset font; map to a `FontResolver` in Unity that grabs the `TMP_FontAsset` for the
    // requested size/bold pairing.

    static wxSize split_lines(wxDC& dc, int width, const wxString& text, wxString& multiline_text, int max_count = 0);
    // [INTENT][UNITY][PORTING_HAZARD:P3] Manual word-wrapping helper that measures text width; Unity's TextMeshPro handles wrapping
    // automatically, but replicating exact `wxDC` metrics may require a custom `TMP_Text` measurement pass.
};

#endif // !slic3r_GUI_Label_hpp_
