#ifndef slic3r_GUI_StaticLine_hpp_
#define slic3r_GUI_StaticLine_hpp_

#include "../wxExtensions.hpp"
#include "wx/window.h"

// [INTENT] Draws the separator line used between option rows so layout adheres to a shared thickness/icon scheme.
// [UNITY] Replace with a custom VisualElement that uses a `VisualElement` background and optional `Image` icon, keeping one shared
// `StyleSheet` for spacing. [PORTING_HAZARD:P3] Unity lacks `wxWindow::PaintEvent`, so the line must own a `MeshRenderer` or `UI`
// `VisualElement` that redraws on style or size changes.
class StaticLine : public wxWindow
{
public:
    // [INTENT] Construct the line in either vertical or horizontal orientation, optionally decorating it with a label/icon.
    // [STATE] Tracks orientation and label resources so `Rescale` & `paintEvent` can respond to DPI changes.
    StaticLine(wxWindow* parent, bool vertical = false, const wxString& label = {}, const wxString& icon = {});

public:
    // [STATE] Stores the label text used only on platforms that render text beside the separator; Unity would skip this when using
    // Image-only separators.
    void SetLabel(const wxString& label) override;

    // [STATE] Caches the icon path so the helper can reload textures when the DPI or theme changes.
    void SetIcon(const wxString& icon);

    // [STATE] Updates the RGB state for the line; in Unity map this to a `Color` property on a shared UI `StyleSheet`.
    void SetLineColour(wxColour color);

    // [THREAD] Called when the parent DPI/style changes; Unity analog is responding to `Canvas` scale or `ResolvedStyle` updates.
    void Rescale();

private:
    // [STATE] Keeps the inline painting details so paint events don't recompute color/icon assets each frame.
    wxColour       lineColor;
    bool           vertical;
    ScalableBitmap icon;

private:
    // [EVENT] Handles wxPaintEvent which is always fired on the UI thread; Unity skew replicates this via `OnGUI`/`Repaint` callbacks.
    void paintEvent(wxPaintEvent& evt);

    // [STATE] Recomputes the cached best-fit size after icon/label changes.
    void messureSize();

    // [OPENGL] Renders the line using the provided device context; Unity would draw using the UI Toolkit render chain instead.
    void render(wxDC& dc);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_StaticLine_hpp_
