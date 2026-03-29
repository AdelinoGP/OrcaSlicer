#ifndef slic3r_GUI_HyperLink_hpp_
#define slic3r_GUI_HyperLink_hpp_

#include <wx/wx.h>
#include <wx/window.h>

namespace Slic3r { namespace GUI {

// [INTENT] Tiny hyperlink wrapper around `wxStaticText`: keep a label clickable, underlined, and styled like a link.
// [STATE] `m_url` stores the click target/tooltip text; the cached colors preserve normal vs hover affordance.
// [UNITY] Map to a retained text/button hybrid with explicit hover state and a browser-launch bridge.
// [PORTING_HAZARD:P3] `wxStaticText` has no native link behavior, so the Unity port must own click/hover semantics explicitly.
class HyperLink : public wxStaticText
{
public:
    // [INTENT] Construct the link label, seed its URL, and wire the hover/click behavior.
    HyperLink(wxWindow* parent, const wxString& label = wxEmptyString, const wxString& url = wxEmptyString, const long style = 0);

    // [STATE] Keep the tooltip aligned with the active click target.
    void SetURL(const wxString& url);
    // [STATE] Expose the current navigation target to callers.
    wxString GetURL() const;

    // [STATE] Force underline preservation even when callers replace the font.
    bool SetFont(const wxFont& font);

private:
    // [STATE] The navigation target is retained as widget state instead of being inferred from the label.
    wxString m_url;
    // [STATE] Cached colors preserve the normal/hover visual distinction.
    wxColour m_normalColor;
    wxColour m_hoverColor;
};

}} // namespace Slic3r::GUI
#endif // !slic3r_GUI_HyperLink_hpp_
