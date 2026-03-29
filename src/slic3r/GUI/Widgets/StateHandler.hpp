#ifndef slic3r_GUI_StateHandler_hpp_
#define slic3r_GUI_StateHandler_hpp_

#include <memory>
#include <wx/event.h>

#include "StateColor.hpp"

// [INTENT] StateHandler aggregates a widget's derived visual state (enabled/hovered/focused/pressed/checked) and keeps the owner and any
// nested child handlers in sync so custom controls can repaint from one normalized state model.
// [STATE] owner_ is a non-owning wxWindow back-pointer; colors_ stores non-owning palette references; children_ owns nested handlers;
// states_, states2_, and bind_states_ cache local, child-derived, and currently-bound state masks.
// [EVENT] EVT_ENABLE_CHANGED is the custom notification surface; update_binds() rewires wx event handlers when the effective state mask
// changes, and changed(...) funnels both wx events and child-state updates back into the aggregate model. [UNITY] Port as a retained
// controller tree with explicit C# event subscriptions and a derived visual-state resolver, not as reflective Bind/ Unbind calls on the
// view hierarchy. [PORTING_HAZARD:P2] The parent/child propagation order and owner refresh behavior are implicit in the current wx handler
// graph, so a Unity port must preserve the same update sequencing to avoid stale hover/press/checked visuals.
wxDECLARE_EVENT(EVT_ENABLE_CHANGED, wxCommandEvent);

class StateHandler : public wxEvtHandler
{
public:
    enum State {
        Enabled    = 1,
        Checked    = 2,
        Focused    = 4,
        Hovered    = 8,
        Pressed    = 16,
        Disabled   = 1 << 16,
        NotChecked = 2 << 16,
        NotFocused = 4 << 16,
        NotHovered = 8 << 16,
        NotPressed = 16 << 16,
    };

public:
    StateHandler(wxWindow* owner);

    ~StateHandler();

public:
    void attach(StateColor const& color);

    void attach(std::vector<StateColor const*> const& colors);

    void attach_child(wxWindow* child);

    void remove_child(wxWindow* child);

    void update_binds();

    int states() const { return states_ | states2_; }

    void set_state(int state, int mask);

private:
    StateHandler(StateHandler* parent, wxWindow* owner);

    void changed(wxEvent& event);

    void changed(int state2);

private:
    wxWindow*                                  owner_;
    std::vector<StateColor const*>             colors_;
    int                                        bind_states_ = 0;
    int                                        states_      = 0;
    int                                        states2_     = 0; // from children
    std::vector<std::unique_ptr<StateHandler>> children_;
    StateHandler*                              parent_ = nullptr;
};

#endif // !slic3r_GUI_StateHandler_hpp_
