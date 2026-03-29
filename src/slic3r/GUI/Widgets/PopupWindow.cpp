#include "PopupWindow.hpp"

// [INTENT] Shared transient-popup shell for GUI widgets that need native popup lifetime rules plus custom dismissal behavior.
// [EVENT] Platform-specific activation and visibility changes are rebound so the popup closes when its host loses focus.
// [UNITY] Port this as a retained popup controller with explicit focus-loss and outside-click dismissal instead of a native popup window.
static wxWindow* GetTopParent(wxWindow* pWindow)
{
    // [INTENT] Walk up to the outermost host window so platform bindings attach to the real top-level parent.
    // [PORTING_HAZARD:P2] The helper depends on a live wx parent chain and a wxNonOwnedWindow dynamic_cast; Unity should resolve the
    // host from retained UI hierarchy state instead of traversing native parents.
    wxWindow* pWin = pWindow;
    while (pWin->GetParent()) {
        pWin = pWin->GetParent();
        if (auto top = dynamic_cast<wxNonOwnedWindow*>(pWin))
            return top;
    }
    return pWin;
}

bool PopupWindow::Create(wxWindow* parent, int style)
{
    if (!wxPopupTransientWindow::Create(parent, style))
        return false;
#ifdef __WXGTK__
    // [EVENT] GTK dismisses the popup when the top-level window deactivates, so the binding is attached to the outer host window.
    // [UNITY] Keep this as an explicit host-focus listener on the retained popup root.
    GetTopParent(parent)->Bind(wxEVT_ACTIVATE, &PopupWindow::topWindowActiavate, this);
#endif
#ifdef __WXOSX__
    // [EVENT] macOS popup controls need manual mouse-event replay to reach nested children under the transient window.
    // [UNITY] Model this as retained hit testing plus explicit pointer enter/leave dispatch.
    if (style & wxPU_CONTAINS_CONTROLS)
        for (auto evt : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_MOTION, wxEVT_MOUSEWHEEL})
            Bind(evt, &PopupWindow::OnMouseEvent2, this);
#endif
    return true;
}

PopupWindow::~PopupWindow()
{
    // [EVENT] Teardown removes the host-window bindings so the popup cannot receive stale activation callbacks after destruction.
#ifdef __WXGTK__
    GetTopParent(this)->Unbind(wxEVT_ACTIVATE, &PopupWindow::topWindowActiavate, this);
#endif
#ifdef __WXMSW__
    // [EVENT] Windows splits unfocus handling across activate, iconize, and show notifications; all three are released here.
    GetTopParent(this)->Unbind(wxEVT_ACTIVATE, &PopupWindow::topWindowActivate, this);
    GetTopParent(this)->Unbind(wxEVT_ICONIZE, &PopupWindow::topWindowIconize, this);
    GetTopParent(this)->Unbind(wxEVT_SHOW, &PopupWindow::topWindowShow, this);
#endif
}

#ifdef __WXOSX__

static wxEvtHandler* HitTest(wxWindow* parent, wxMouseEvent& evt)
{
    // [INTENT] Depth-first hit test the popup subtree so the deepest child under the cursor receives the mouse event.
    // [PORTING_HAZARD:P2] The event position is rewritten in place during recursion, so a Unity port should keep coordinate conversion
    // separate from child lookup.
    auto                pt       = evt.GetPosition();
    const wxWindowList& children = parent->GetChildren();
    for (auto w : children) {
        wxRect rc{w->GetPosition(), w->GetSize()};
        if (rc.Contains(pt)) {
            evt.SetPosition(pt - rc.GetTopLeft());
            if (auto child = HitTest(w, evt))
                return child;
            return w;
        }
    }
    return nullptr;
}

void PopupWindow::OnMouseEvent2(wxMouseEvent& evt)
{
    // [EVENT] Motion updates synthetic enter/leave state before the original event is forwarded to the hovered child.
    // [UNITY] Recreate this with retained pointer-over state and explicit enter/exit callbacks on the popup tree.
    auto child = ::HitTest(this, evt);
    if (evt.GetEventType() == wxEVT_MOTION) {
        auto h = child ? child : this;
        if (hovered != h) {
            wxMouseEvent leave(wxEVT_LEAVE_WINDOW);
            leave.SetEventObject(hovered);
            leave.SetId(static_cast<wxWindow*>(hovered)->GetId());
            hovered->ProcessEventLocally(leave);
            hovered = h;
            wxMouseEvent enter(wxEVT_ENTER_WINDOW);
            enter.SetEventObject(hovered);
            enter.SetId(static_cast<wxWindow*>(hovered)->GetId());
            hovered->ProcessEventLocally(enter);
        }
    }
    if (child) {
        child->ProcessEventLocally(evt);
    } else {
        evt.Skip();
    }
}

#endif

#ifdef __WXGTK__
void PopupWindow::topWindowActiavate(wxActivateEvent& event)
{
    event.Skip();
    // [EVENT] Losing activation dismisses the popup immediately; the event itself is still propagated.
    if (!event.GetActive() && IsShown())
        DismissAndNotify();
}
#endif

#ifdef __WXMSW__
void PopupWindow::BindUnfocusEvent()
{
    // [EVENT] Windows needs separate listeners for activation loss, iconization, and show/hide changes to mirror popup dismissal rules.
    // [UNITY] Collapse this into a single retained focus/visibility watcher instead of three native callbacks.
    GetTopParent(this)->Bind(wxEVT_ACTIVATE, &PopupWindow::topWindowActivate, this);
    GetTopParent(this)->Bind(wxEVT_ICONIZE, &PopupWindow::topWindowIconize, this);
    GetTopParent(this)->Bind(wxEVT_SHOW, &PopupWindow::topWindowShow, this);
}

void PopupWindow::topWindowActivate(wxActivateEvent& event)
{
    // [EVENT] A deactivated host window closes the popup without consuming the activation event.
    if (!event.GetActive())
        Dismiss();
}

void PopupWindow::topWindowIconize(wxIconizeEvent& event)
{
    event.Skip();
    // [EVENT] Minimizing the host should close the transient popup as soon as the iconize event arrives.
    if (event.IsIconized())
        Dismiss();
}

void PopupWindow::topWindowShow(wxShowEvent& event)
{
    event.Skip();
    // [EVENT] Hiding the host window dismisses the popup so it cannot outlive the visible application shell.
    if (!event.IsShown())
        Dismiss();
}
#endif
