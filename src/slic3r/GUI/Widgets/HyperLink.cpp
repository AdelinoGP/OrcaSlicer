#include "HyperLink.hpp"
#include "Label.hpp"

namespace Slic3r { namespace GUI {

HyperLink::HyperLink(wxWindow* parent, const wxString& label, const wxString& url, long style)
    : wxStaticText(parent, wxID_ANY, label)
    , m_url(url)
    , m_normalColor(wxColour("#009687")) // used slightly different color otherwise automatically uses ColorForDark that not visible enough
    , m_hoverColor(wxColour("#26A69A"))
{
    // [INTENT] Lightweight hyperlink label wrapper: keep the text underlined, tint it like a link, and open the browser on click.
    // [STATE] `m_url` is the only retained payload; the color pair is cached because wxStaticText has no native link hover state.
    // [EVENT] Pointer enter/leave swaps the foreground color and a left-click launches the stored URL.
    // [UNITY] UI Toolkit `TextElement`/`Button` with pointer-enter/exit handlers and `Application.OpenURL` for navigation.
    // [PORTING_HAZARD:P3] The visual affordance is encoded in ad hoc color/underline mutations instead of a dedicated link widget.
    SetForegroundColour(m_normalColor);
    HyperLink::SetFont(Label::Head_14);
    SetCursor(wxCursor(wxCURSOR_HAND));

    if (!m_url.IsEmpty())
        SetToolTip(m_url);

    Bind(wxEVT_LEFT_DOWN, ([this](wxMouseEvent& e) {
             if (!m_url.IsEmpty())
                 wxLaunchDefaultBrowser(m_url);
         }));

    Bind(wxEVT_ENTER_WINDOW, ([this](wxMouseEvent& e) {
             SetForegroundColour(m_hoverColor);
             Refresh();
         }));
    Bind(wxEVT_LEAVE_WINDOW, ([this](wxMouseEvent& e) {
             SetForegroundColour(m_normalColor);
             Refresh();
         }));
}

bool HyperLink::SetFont(const wxFont& font)
{ // ensure it stays underlined
    // [INTENT] Preserve the hyperlink affordance even when callers replace the font.
    // [STATE] Underline is forced on every assignment so the control keeps link semantics across theme/style changes.
    wxFont f = font;
    f.SetUnderlined(true);
    return wxStaticText::SetFont(f);
}

void HyperLink::SetURL(const wxString& url)
{
    // [STATE] URL updates are immediately reflected in the tooltip so hover text always matches the click target.
    m_url = url;
    SetToolTip(m_url);
}

wxString HyperLink::GetURL() const { return m_url; }

}} // namespace Slic3r::GUI
