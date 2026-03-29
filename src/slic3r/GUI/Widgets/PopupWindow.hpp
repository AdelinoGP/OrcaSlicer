#ifndef slic3r_GUI_PopupWindow_hpp_
#define slic3r_GUI_PopupWindow_hpp_

#include <wx/popupwin.h>
#include <wx/event.h>

// [INTENT] Lightweight transient popup shell that centralizes platform-specific activation and pointer-loss behavior for
// wxPopupTransientWindow. [STATE] The class itself is almost stateless; the only retained per-instance bridge is the macOS hover/event
// target used to replay mouse hits. [EVENT] Create() installs the platform listeners that dismiss or re-route the popup when the top window
// deactivates, iconizes, or changes visibility. [UNITY] Port this as a floating popup controller anchored to a host panel, with explicit
// outside-click and focus-loss dismissal instead of native transient-window behavior. [PORTING_HAZARD:P2] wxPopupTransientWindow relies on
// OS-managed focus and activation semantics; Unity needs its own lifecycle and pointer-over routing to avoid stuck popups.
class PopupWindow : public wxPopupTransientWindow
{
public:
    PopupWindow() {}

    ~PopupWindow();

    PopupWindow(wxWindow* parent, int style = wxBORDER_NONE) { Create(parent, style); }

    // [INTENT] Create the popup and bind the platform-specific focus/activation hooks needed to keep it transient.
    // [EVENT] On Windows this can also bind an explicit unfocus listener because deactivation alone does not cover every dismissal path.
    bool Create(wxWindow* parent, int flags = wxBORDER_NONE);
#ifdef __WXMSW__
    void BindUnfocusEvent();
#endif
private:
#ifdef __WXOSX__
    // [EVENT] Replays mouse input through the hovered handler so the popup can decide whether to stay open or dismiss.
    // [STATE] `hovered` is a non-owning handler pointer that defaults to the popup itself until hit-testing redirects it.
    void          OnMouseEvent2(wxMouseEvent& evt);
    wxEvtHandler* hovered{this};
#endif

#ifdef __WXGTK__
    // [UNCLEAR] The method name is misspelled in the source; the intent appears to be top-window activation handling for GTK dismissal.
    void topWindowActiavate(wxActivateEvent& event);
#endif

#ifdef __WXMSW__
    // [EVENT] Windows listens to activation, iconize, and show transitions to keep the transient popup synchronized with its owner window.
    void topWindowActivate(wxActivateEvent& event);
    void topWindowIconize(wxIconizeEvent& event);
    void topWindowShow(wxShowEvent& event);
#endif
};

#endif // !slic3r_GUI_PopupWindow_hpp_
