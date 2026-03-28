#include "ErrorMsgStaticText.hpp"
#include <wx/dcclient.h>

// [INTENT] Custom-painted error label that wraps its message to the available panel width and then resizes itself to the number of rendered
// lines. [UNITY] Port as a text component with automatic wrapping and layout-driven height rather than manual DC text measurement.
ErrorMsgStaticText::ErrorMsgStaticText() {}

ErrorMsgStaticText::ErrorMsgStaticText(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
{
    // [EVENT] The panel owns only a paint binding; it does not schedule redraws itself, so all sizing updates happen from the UI paint path.
    Create(parent, id, pos, size);
    Bind(wxEVT_PAINT, &ErrorMsgStaticText::paintEvent, this);
}

// [STATE] m_msg is the caller-supplied label text; this handler rewrites a local copy to inject wrap breaks and then forces the widget
// height to match. [PORTING_HAZARD:P2] The direct m_msg[0]/m_msg[1] access assumes at least two characters, and the ad-hoc multibyte
// heuristic is fragile outside wxWidgets text rendering.
void ErrorMsgStaticText::paintEvent(wxPaintEvent& evt)
{
    auto      size = GetSize();
    wxPaintDC dc(this);
    auto      text_height  = dc.GetCharHeight();
    wxString  out_txt      = m_msg;
    wxString  count_txt    = "";
    int       line_count   = 1;
    int       new_line_pos = 0;
    bool      is_ch        = false;

    if (m_msg[0] > 0x80 && m_msg[1] > 0x80)
        is_ch = true;

    // [INTENT] Greedy wrap: accumulate characters until the current text extent exceeds the panel width, then backtrack to the last break
    // opportunity or insert a hard split for presumed CJK text.
    for (int i = 0; i < m_msg.length(); i++) {
        auto text_size = dc.GetTextExtent(count_txt);
        if (text_size.x < (size.x)) {
            count_txt += m_msg[i];
            if (m_msg[i] == ' ' || m_msg[i] == ',' || m_msg[i] == '.' || m_msg[i] == '\n') {
                new_line_pos = i;
            }
        } else {
            if (!is_ch) {
                out_txt[new_line_pos] = '\n';
                i                     = new_line_pos;
            } else {
                out_txt.insert(i - 1, '\n');
            }
            count_txt = "";
            line_count++;
        }
    }
    // [STATE] The control mutates its own min/max/actual height on every paint so parent sizers can reflow around the new wrapped line count.
    SetSize(wxSize(-1, line_count * text_height));
    SetMinSize(wxSize(-1, line_count * text_height));
    SetMaxSize(wxSize(-1, line_count * text_height));
    // [UNITY] Replace this immediate-mode paint with a retained text element whose preferred height is computed from the wrapped line count.
    dc.DrawText(out_txt, 0, 0);
}
