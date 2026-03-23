#include "BBLStatusBar.hpp"

#include <wx/timer.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/statusbr.h>
#include <wx/frame.h>
#include <wx/statline.h>

#include "GUI_App.hpp"

#include "I18N.hpp"

#include <iostream>

namespace Slic3r {

// [INTENT] Expose BBL-specific status, progress, and cancel affordances inside the main frame so print workflow updates stay visible
// without hijacking the primary preview space. [STATE] `m_self` owns the sizer chain (object info, slice info, progress+cancel) that is
// toggled to keep the bar compact when nothing is active. [THREAD] Construction happens on the main wxWidgets UI thread; future updates
// must marshal through the same thread (Unity port: enqueue to `MainThreadDispatcher`). [UNITY] Map this to a UI Toolkit `VisualElement`
// row with `Label` fields, a `ProgressBar`, and a `Button` that drive a shared `ScriptableObject` status model. [PORTING_HAZARD:P3]
// wxSizer::Show/Hide combos trigger manual Layout calls; Unity will need to mirror the visibility state machine explicitly rather than
// relying on automatic layout invalidation.
BBLStatusBar::BBLStatusBar(wxWindow* parent, int id)
    : m_self{new wxPanel(parent, id == -1 ? wxID_ANY : id)}
    , m_prog{new wxGauge(m_self, wxGA_HORIZONTAL, 100, wxDefaultPosition, wxSize(120, -1))}
    , m_cancelbutton{new wxButton(m_self, -1, _(L("Cancel")), wxDefaultPosition, wxDefaultSize)}
    , m_sizer(new wxBoxSizer(wxHORIZONTAL))
    , m_slice_info_sizer(new wxBoxSizer(wxHORIZONTAL))
    , m_object_info_sizer(new wxBoxSizer(wxHORIZONTAL))
{
    m_status_text = new wxStaticText(m_self, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_status_text->SetForegroundColour(*wxBLACK);

    m_object_info = new wxStaticText(m_self, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_object_info->SetForegroundColour(*wxBLACK);

    m_slice_info = new wxStaticText(m_self, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_slice_info->SetForegroundColour(*wxBLACK);

    wxStaticLine* seperator_1 = new wxStaticLine(m_self, wxID_ANY, wxDefaultPosition, wxSize(3, -1), wxLI_VERTICAL);
    wxStaticLine* seperator_2 = new wxStaticLine(m_self, wxID_ANY, wxDefaultPosition, wxSize(3, -1), wxLI_VERTICAL);

    m_object_info_sizer->Add(m_object_info, 1, wxEXPAND | wxALL, 0);
    m_object_info_sizer->Add(seperator_1, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

    m_slice_info_sizer->Add(m_slice_info, 1, wxEXPAND | wxALL, 0);
    m_slice_info_sizer->Add(seperator_2, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

    // [EVENT] Cancel button dispatches the optional cancel callback and hides itself; Unity will replace this with a
    // `Button.onClick`+`CancellationToken` combo. [STATE] The button is hidden once tapped so repeated cancellation events aren't reissued
    // until the job re-enters busy mode.
    m_cancelbutton->Bind(wxEVT_BUTTON, [this](const wxCommandEvent&) {
        if (m_cancel_cb)
            m_cancel_cb();
        m_cancelbutton->Hide();
    });

    m_sizer->Add(m_object_info_sizer, 1, wxEXPAND | wxALL | wxALIGN_LEFT, 5);
    m_sizer->Add(m_slice_info_sizer, 1, wxEXPAND | wxALL | wxALIGN_LEFT, 5);
    m_sizer->Add(m_status_text, 1, wxEXPAND | wxALL | wxALIGN_LEFT, 5);
    m_sizer->Add(m_prog, 0, wxEXPAND | wxLEFT | wxALL, 5);
    m_sizer->Add(m_cancelbutton, 0, wxEXPAND | wxALL, 5);
    m_sizer->SetSizeHints(m_self);
    m_self->SetSizer(m_sizer);

    m_sizer->Hide(m_object_info_sizer);
    m_sizer->Hide(m_slice_info_sizer);
    m_sizer->Hide(m_prog);
    m_sizer->Hide(m_cancelbutton);
    m_sizer->Layout();
}

// [STATE] Gauge value is read only on the UI thread; background jobs should marshal through the UI dispatcher before querying progress.
int BBLStatusBar::get_progress() const { return m_prog->GetValue(); }

void BBLStatusBar::set_progress(int val)
{
    if (val < 0)
        return;

    bool need_layout = false;
    // [STATE] Showing/hiding object/slice info vs. the gauge keeps available space bounded; we then call Layout explicitly to avoid stale
    // measurements. add the logic for arrange/orient jobs, which don't call stop_busy
    if (val == m_prog->GetRange()) {
        m_prog->SetValue(0);
        m_sizer->Hide(m_prog);
        need_layout = true;
    } else {
        if (m_sizer->IsShown(m_object_info_sizer)) {
            m_sizer->Hide(m_object_info_sizer);
            need_layout = true;
        }

        if (m_sizer->IsShown(m_slice_info_sizer)) {
            m_sizer->Hide(m_slice_info_sizer);
            need_layout = true;
        }

        if (!m_sizer->IsShown(m_prog)) {
            m_sizer->Show(m_prog);
            m_sizer->Show(m_cancelbutton);
            need_layout = true;
        }
        m_prog->SetValue(val);
    }

    if (need_layout) {
        m_sizer->Layout();
    }
}

int BBLStatusBar::get_range() const { return m_prog->GetRange(); }

void BBLStatusBar::set_range(int val)
{
    if (val != m_prog->GetRange()) {
        m_prog->SetRange(val);
    }
}

void BBLStatusBar::clear_percent() {}

void BBLStatusBar::show_error_info(wxString msg, int code, wxString description, wxString extra) {}

void BBLStatusBar::show_progress(bool show)
{
    // [STATE] Toggles the busy progress indicator versus passive info text so the user sees either active slicing or contextual labels.
    // [UNITY] Equivalent to switching `ProgressBar` visibility on a VisualElement row while hiding `Label` elements when busy.
    if (show) {
        m_sizer->Hide(m_object_info);
        m_sizer->Hide(m_slice_info);

        m_sizer->Show(m_prog);
        m_sizer->Layout();
    } else {
        m_sizer->Hide(m_prog);
        m_sizer->Layout();
    }
}

void BBLStatusBar::start_busy(int rate)
{
    // [STATE] Busy flag indicates a background BBL job; showing the progress gauge and cancel button keeps users aware that the workflow
    // can still be interrupted.
    m_busy = true;
    show_progress(true);
    show_cancel_button();
}

void BBLStatusBar::stop_busy()
{
    // [STATE] Reset the gage/cancel UI, reset progress to 0, and restore slice info so the next job can reuse existing text.
    show_progress(false);
    hide_cancel_button();
    m_prog->SetValue(0);
    m_sizer->Show(m_slice_info_sizer);
    m_sizer->Layout();
    m_busy = false;
}

void BBLStatusBar::set_cancel_callback(BBLStatusBar::CancelFn ccb)
{
    // [EVENT] Swap the callback so the cancel button reuses the latest job-level cancellation; Unity will expose this via a serialized
    // `UnityEvent` wired to `CancellationTokenSource.Cancel`.
    m_cancel_cb = ccb;
    if (ccb) {
        m_sizer->Show(m_cancelbutton);
    } else {
        m_sizer->Hide(m_cancelbutton);
    }
}

wxPanel* BBLStatusBar::get_panel() { return m_self; }

void BBLStatusBar::set_status_text(const wxString& txt) { m_status_text->SetLabelText(txt); }

void BBLStatusBar::set_status_text(const std::string& txt) { this->set_status_text(txt.c_str()); }

void BBLStatusBar::set_status_text(const char* txt) { this->set_status_text(wxString::FromUTF8(txt)); }

wxString BBLStatusBar::get_status_text() const { return m_status_text->GetLabelText(); }

// [STATE] Object info text is only shown when non-empty so the status bar stays compact and doesn't display stale data.
void BBLStatusBar::set_object_info(const wxString& txt)
{
    if (txt == "") {
        m_object_info->SetLabelText("");
        m_sizer->Hide(m_object_info_sizer);
    } else {
        if (!m_sizer->IsShown(m_object_info_sizer)) {
            m_sizer->Show(m_object_info_sizer);
        }
        m_object_info->SetLabelText(txt);
    }
    m_sizer->Layout();
}

// [STATE] Slice info text only shows when there is meaningful progress text; Unity replicates by binding a `Label` to
// `string.IsNullOrEmpty` and toggling visibility.
void BBLStatusBar::set_slice_info(const wxString& txt)
{
    if (!txt.empty()) {
        if (!m_sizer->IsShown(m_slice_info_sizer)) {
            m_sizer->Show(m_slice_info_sizer);
        }
        m_slice_info->SetLabelText(txt);
        m_sizer->Layout();
    }
}

// [STATE] Programmatically toggling the slice info sizer keeps the UI predictable when the workflow transitions between slicing and idle states.
void BBLStatusBar::show_slice_info(bool show)
{
    if (show) {
        m_sizer->Show(m_slice_info_sizer);
        m_sizer->Layout();
    } else {
        m_sizer->Hide(m_slice_info_sizer);
        m_sizer->Layout();
    }
}

bool BBLStatusBar::is_slice_info_shown() { return m_sizer->IsShown(m_slice_info_sizer); }

void BBLStatusBar::set_font(const wxFont& font) { m_self->SetFont(font); }

// [STATE] Cancel button show/hide is separate because some flows disable cancel without ending busy mode; manual layout keeps spacing consistent.
void BBLStatusBar::show_cancel_button()
{
    m_sizer->Show(m_cancelbutton);
    m_sizer->Layout();
}

void BBLStatusBar::hide_cancel_button()
{
    m_sizer->Hide(m_cancelbutton);
    m_sizer->Layout();
}

} // namespace Slic3r
