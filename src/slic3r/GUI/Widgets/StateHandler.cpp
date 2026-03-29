#include "StateHandler.hpp"
#include <wx/window.h>

// [INTENT] Global custom event used to recompute a widget's state palette when its enabled flag changes.
// [UNITY] Map this to a local state-change callback on a retained control controller rather than a wxCommandEvent broadcast.
wxDEFINE_EVENT(EVT_ENABLE_CHANGED, wxCommandEvent);

// [INTENT] Attach this event handler to a wxWindow so the widget and its descendants can derive one merged visual-state model.
// [STATE] The owner starts with enabled/focused bits mirrored from the live wxWindow and keeps later hover/press/check state in `states_`.
// [PORTING_HAZARD:P2] The handler mutates the window's event chain at runtime; Unity will need explicit controller ownership instead of
// handler insertion.
StateHandler::StateHandler(wxWindow* owner) : owner_(owner)
{
    owner_->PushEventHandler(this);
    if (owner->IsEnabled())
        states_ |= Enabled;
    if (owner->HasFocus())
        states_ |= Focused;
}

// [INTENT] Detach the handler from the owner window before destruction so the wx event chain does not retain a dangling callback.
StateHandler::~StateHandler() { owner_->RemoveEventHandler(this); }

// [STATE] StateColor pointers are non-owning palette inputs; this object only aggregates them to decide which state events to bind.
void StateHandler::attach(StateColor const& color) { colors_.push_back(&color); }

// [STATE] Bulk-attach lets callers share the same palette/state-source set across related child controls.
void StateHandler::attach(std::vector<StateColor const*> const& colors) { colors_.insert(colors_.end(), colors.begin(), colors.end()); }

// [INTENT] Register a child window with its own nested StateHandler and fold the child's derived state into the parent refresh model.
// [UNITY] This becomes a retained parent/child controller tree where child controllers report state upward, instead of nested wxEvtHandler objects.
void StateHandler::attach_child(wxWindow* child)
{
    auto ch = new StateHandler(this, child);
    children_.emplace_back(ch);
    ch->update_binds();
    states2_ |= ch->states();
}

// [INTENT] Remove a child handler and rebuild the cached descendant-state mask so the parent can recompute bindings and refresh behavior.
void StateHandler::remove_child(wxWindow* child)
{
    children_.erase(std::remove_if(children_.begin(), children_.end(), [child](auto& c) { return c->owner_ == child; }), children_.end());
    states2_ = 0;
    for (auto& c : children_)
        states2_ |= c->states();
}

// [EVENT] Rebind only the wx events that matter for the currently requested palette states, avoiding unnecessary focus/hover/press
// listeners. [STATE] `bind_states_` mirrors the active palette requirements, while `states2_` tracks descendant influence so a child can
// still force a redraw. [UNITY] In Unity this should be an explicit subscription set on the controller, not reflective Bind/Unbind calls.
void StateHandler::update_binds()
{
    int bind_states = parent_ ? (parent_->bind_states_ & ~Enabled) : 0;
    for (auto c : colors_) {
        bind_states |= c->states();
    }
    bind_states           = bind_states | (bind_states >> 16);
    int         diff      = bind_states ^ bind_states_;
    State       states[]  = {Enabled, Checked, Focused, Hovered, Pressed};
    wxEventType events[]  = {EVT_ENABLE_CHANGED, wxEVT_CHECKBOX, wxEVT_SET_FOCUS, wxEVT_ENTER_WINDOW, wxEVT_LEFT_DOWN};
    wxEventType events2[] = {0, 0, wxEVT_KILL_FOCUS, wxEVT_LEAVE_WINDOW, wxEVT_LEFT_UP};
    for (int i = 0; i < 5; ++i) {
        int s = states[i];
        if (diff & s) {
            if (bind_states & s) {
                Bind(events[i], &StateHandler::changed, this);
                if (events2[i])
                    Bind(events2[i], &StateHandler::changed, this);
            } else {
                Unbind(events[i], &StateHandler::changed, this);
                if (events2[i])
                    owner_->Unbind(events2[i], &StateHandler::changed, this);
            }
        }
    }
    bind_states_ = bind_states;
    for (auto& c : children_)
        c->update_binds();
}

// [EVENT] Update the local state bitmask for a direct owner event such as focus, hover, press, or checkbox toggle.
// [PORTING_HAZARD:P3] The refresh decision depends on comparing old vs. combined child state, which is easy to miss when porting to retained UI.
void StateHandler::set_state(int state, int mask)
{
    if ((states_ & mask) == (state & mask))
        return;
    int old = states_;
    states_ = (states_ & ~mask) | (state & mask);
    if (old != states_ && (old | states2_) != (states_ | states2_)) {
        if (parent_)
            parent_->changed(states_ | states2_);
        else
            owner_->Refresh();
    }
}

// [INTENT] Child handlers reuse the same owner window but suppress Enabled in the child-local state so the parent can manage enabled
// visuals separately.
StateHandler::StateHandler(StateHandler* parent, wxWindow* owner) : StateHandler(owner)
{
    states_ &= ~Enabled;
    parent_ = parent;
}

// [EVENT] Translate wx event types into the local state bits, including paired enter/leave and focus gain/loss transitions.
// [THREAD] This must stay on the UI thread because it touches wx event routing and triggers owner Refresh calls.
void StateHandler::changed(wxEvent& event)
{
    event.Skip();
    wxEventType events[]  = {EVT_ENABLE_CHANGED, wxEVT_CHECKBOX, wxEVT_SET_FOCUS, wxEVT_ENTER_WINDOW, wxEVT_LEFT_DOWN};
    wxEventType events2[] = {0, 0, wxEVT_KILL_FOCUS, wxEVT_LEAVE_WINDOW, wxEVT_LEFT_UP};
    int         old       = states_;
    // some events are from another window (ex: text_ctrl of TextInput), save state in states2_ to avoid conflicts
    for (int i = 0; i < 5; ++i) {
        if (events2[i]) {
            if (event.GetEventType() == events[i]) {
                states_ |= 1 << i;
                break;
            } else if (event.GetEventType() == events2[i]) {
                states_ &= ~(1 << i);
                break;
            }
        } else {
            if (event.GetEventType() == events[i]) {
                states_ ^= (1 << i);
                break;
            }
        }
    }
    if (old != states_ && (old | states2_) != (states_ | states2_)) {
        if (parent_)
            parent_->changed(states_ | states2_);
        else
            owner_->Refresh();
    }
}

// [INTENT] Rebuild the descendant-state aggregate after a child reports a state change, then bubble the combined state upward or refresh
// the owner. [UNITY] A Unity port would replace this recursive aggregation with a retained hierarchy that exposes a computed visual-state
// property.
void StateHandler::changed(int)
{
    int old  = states2_;
    states2_ = 0;
    for (auto& c : children_)
        states2_ |= c->states();
    if (old != states2_ && (old | states_) != (states_ | states2_)) {
        if (parent_)
            parent_->changed(states_ | states2_);
        else
            owner_->Refresh();
    }
}
