#ifndef slic3r_GUI_StaticGroup_hpp_
#define slic3r_GUI_StaticGroup_hpp_

#include "../wxExtensions.hpp"

#include "LabeledStaticBox.hpp"

// [INTENT] Thin header boundary for the badge-capable labeled group box used in settings panes.
// [STATE] The only extra retained state beyond LabeledStaticBox is the optional badge overlay bitmap.
// [UNITY] Port as a retained panel with a header row and an optional top-right overlay sprite backed by a shared asset cache.
// [PORTING_HAZARD:P3] The badge is injected during custom border/label painting, so Unity should re-express it as layout-driven chrome.
class StaticGroup : public LabeledStaticBox
{
public:
    // [EVENT] Toggle the badge overlay on or off; the cpp mutates cached bitmap state and repaints only on visible changes.
    StaticGroup(wxWindow* parent, wxWindowID id, const wxString& label);
    // [STATE] Public visibility switch for the optional badge overlay.
    void ShowBadge(bool show);

private:
    // [INTENT] Extend the inherited border/label draw pass with badge placement at the far-right header edge.
    void DrawBorderAndLabel(wxDC& dc) override;
    // [STATE] Lazily populated badge sprite; empty when the overlay is hidden.
    ScalableBitmap badge;
};

#endif // !slic3r_GUI_StaticGroup_hpp_
