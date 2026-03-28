
#ifndef _WX_ERRORMSGSTATTEXT_H_
#define _WX_ERRORMSGSTATTEXT_H_

#include <wx/panel.h>
#include "wx/stattext.h"

// [INTENT] Small error-banner panel that draws wrapped message text itself instead of relying on wxStaticText.
// [STATE] `m_msg` holds the transient error string that the paint path measures, wraps, and sizes against the available width.
// [EVENT] `paintEvent` is the custom draw/update hook; the implementation may also adjust panel height from within the paint flow.
// [UNITY] Map this to a TextMeshProUGUI label inside a layout-driven container, with preferred-height measurement outside the draw pass.
// [PORTING_HAZARD:P3] Any paint-time resize or text-measurement side effect will be fragile in retained-mode UI and should move into layout.
class WXDLLIMPEXP_CORE ErrorMsgStaticText : public wxPanel
{
public:
    wxString m_msg;
    ErrorMsgStaticText();
    ErrorMsgStaticText(wxWindow*      parent,
                       wxWindowID     id   = wxID_ANY,
                       const wxPoint& pos  = wxDefaultPosition,
                       const wxSize&  size = wxSize(0, 0));

    void paintEvent(wxPaintEvent& evt);

    void SetLabel(wxString msg) { m_msg = msg; };
};
#endif
