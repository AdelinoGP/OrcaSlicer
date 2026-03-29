#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/utils.h"
#include "wx/frame.h"
#include "wx/button.h"
#include "wx/stattext.h"
#include "wx/sizer.h"
#include "wx/event.h"
#include "wx/gauge.h"
#include "wx/intl.h"
#include "wx/dcclient.h"
#include "wx/timer.h"
#include "wx/settings.h"
#include "wx/app.h"
#endif

#include "../GUI.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "ProgressDialog.hpp"
#include "wx/evtloop.h"
#include "Label.hpp"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

#define LAYOUT_MARGIN 12

static const int wxID_SKIP = 32000;

namespace Slic3r { namespace GUI {
// [INTENT] This dialog is a stateful progress controller, not a passive indicator: it owns the modal lifecycle, cancel/skip affordances,
// time estimates, and the fallback text layout mode. [STATE] The instance caches the current progress range, estimated time smoothing,
// modal state, and optional adaptive-layout widgets so updates can re-render without rebuilding the window. [UNITY] Port this as a modal
// progress overlay/controller with a retained value model, explicit cancel/skip buttons, and a separate text-layout service.
// [PORTING_HAZARD:P1] The implementation depends on nested wx event loops, window disabling, and re-entrant YieldFor() calls; Unity cannot
// mirror that directly and needs an explicit async/state-machine design.
void ProgressDialog::Init()
{
    // we may disappear at any moment, let the others know about it
    SetExtraStyle(GetExtraStyle() | wxWS_EX_TRANSIENT);

    // Initialize all our members that we always use (even when we don't
    // create a valid window in this class).

    m_pdStyle   = 0;
    m_parentTop = NULL;

    m_gauge   = NULL;
    m_msg     = NULL;
    m_elapsed = m_estimated = m_remaining = NULL;

    m_state   = Uncancelable;
    m_maximum = 0;

    m_timeStart = wxGetCurrentTime();
    m_timeStop  = (unsigned long) -1;
    m_break     = 0;

    m_skip = false;

    m_btnAbort = NULL;
    m_btnSkip  = NULL;

    m_display_estimated = m_last_timeupdate = m_ctdelay = 0;

    m_delay = 3;

    m_winDisabler   = NULL;
    m_tempEventLoop = NULL;

    SetWindowStyle(wxDEFAULT_DIALOG_STYLE);
}

ProgressDialog::ProgressDialog() : wxDialog() { Init(); }

ProgressDialog::ProgressDialog(const wxString& title, const wxString& message, int maximum, wxWindow* parent, int style, bool adaptive)
    : wxDialog()
{
    m_adaptive = adaptive;
    Init();
    Create(title, message, maximum, parent, style);
    Bind(wxEVT_PAINT, &ProgressDialog::OnPaint, this);
    Bind(wxEVT_CLOSE_WINDOW, &ProgressDialog::OnClose, this);
}

void ProgressDialog::OnPaint(wxPaintEvent& evt) {}

void ProgressDialog::SetTopParent(wxWindow* parent)
{
    m_parent    = parent;
    m_parentTop = parent;
}

wxString ProgressDialog::FormatString(wxString title)
{
    // [INTENT] Select between a one-line label, a two-line simplebook page, or the adaptive scrolled layout based on the measured title
    // width. [STATE] The chosen branch mutates the active message widget tree and, in adaptive mode, resizes the scroll container to match
    // wrapped text. [UNITY] Use a content-size aware text component or a layout controller that swaps between compact and wrapped
    // presentation instead of switching native child pages. [PORTING_HAZARD:P2] The current width check uses paint-time text metrics and
    // manually resizes child panels, so Unity should treat wrapping as layout state rather than a side effect of label assignment.
    if (!m_adaptive) {
        auto current_width = 0;
        m_mode             = 0;
        for (int i = 0; i < title.length(); i++) {
            current_width = m_msg->GetTextExtent(title.SubString(0, i)).x;
            if (current_width > PROGRESSDIALOG_GAUGE_SIZE.x) {
                m_mode = 1;
                // title.insert(i - 1, "\n");
                break;
            }
        }
        // set mode
        if (m_mode == 0) {
            m_simplebook->SetSelection(0);
            m_msg->SetLabel(title);
        } else {
            wxSize content_size   = m_msg->GetTextExtent(title);
            int    resized_height = (int(content_size.x / PROGRESSDIALOG_GAUGE_SIZE.x) + 1) * content_size.y;
            set_panel_height(resized_height);
            m_simplebook->SetSelection(1);
            m_msg_2line->SetLabel(title);
        }
    } else {
        m_msg->SetLabel(title);
        m_msg->SetMaxSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x + 5, -1));
        m_msg->SetMinSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x + 5, -1));
        m_msg->Wrap(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x + 5);
        m_msg->Layout();
        m_msg->Fit();

        m_msg_scrolledWindow->SetSize(wxSize(FromDIP(m_msg->GetSize().x + 5), FromDIP(150)));
        m_msg_scrolledWindow->SetMinSize(wxSize(FromDIP(m_msg->GetSize().x + 5), FromDIP(150)));
        m_msg_scrolledWindow->SetMaxSize(wxSize(FromDIP(m_msg->GetSize().x + 5), FromDIP(150)));
        // m_msg_scrolledWindow->Layout();
        m_msg_scrolledWindow->Fit();
    }

    // Fit();
    return title;
}

bool ProgressDialog::Create(const wxString& title, const wxString& message, int maximum, wxWindow* parent, int style)
{
    // [INTENT] Build the dialog chrome, progress gauge, message area, and cancel/skip affordances, then immediately show it in a
    // disabled-other-windows modal flow. [EVENT] Button clicks, close events, and later update calls all route through the dialog's own
    // state machine; the window also installs a temporary active event loop if none exists. [THREAD] This path assumes UI-thread ownership
    // and then yields back into the event loop during setup and updates, so it is tightly coupled to wx's modal dispatch model. [UNITY]
    // Port as a controller that constructs retained subviews once, then drives them from explicit state transitions without creating a
    // nested message loop. [PORTING_HAZARD:P1] The modal creation path combines dialog construction with event-loop bootstrap and global
    // window disabling, which is a major architectural mismatch for Unity.
    SetFont(wxGetApp().normal_font());
    SetTopParent(parent);

    m_pdStyle = style;

    if (!wxDialog::Create(m_parentTop, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, GetWindowStyle()))
        return false;
    SetBackgroundColour(PROGRESSDIALOG_DEF_BK);

    /* SetSize(DESIGN_RESOUTION_PROGRESS_SIZE);
     SetMinSize(DESIGN_RESOUTION_PROGRESS_SIZE);
     SetMaxSize(DESIGN_RESOUTION_PROGRESS_SIZE);*/

    SetMaximum(maximum);
    EnsureActiveEventLoopExists();

#if defined(__WXMSW__) && !defined(__WXUNIVERSAL__)
    if (!HasPDFlag(wxPD_CAN_ABORT)) {
        EnableCloseButton(false);
    }
#endif // wxMSW

    m_state = HasPDFlag(wxPD_CAN_ABORT) ? Continue : Uncancelable;

    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_top_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    m_top_line->SetBackgroundColour(wxColour(166, 169, 170));

    m_sizer_main->Add(m_top_line, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(24));

    if (!m_adaptive) {
        m_simplebook = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, PROGRESSDIALOG_SIMPLEBOOK_SIZE, 0);

        m_panel_1line = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, PROGRESSDIALOG_SIMPLEBOOK_SIZE, 0);
        m_panel_1line->SetMaxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE);
        m_panel_1line->SetBackgroundColour(PROGRESSDIALOG_DEF_BK);
        m_simplebook->AddPage(m_panel_1line, wxEmptyString, false);

        m_panel_2line = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, PROGRESSDIALOG_SIMPLEBOOK_SIZE, 0);
        m_panel_2line->SetMaxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE);
        m_panel_2line->SetBackgroundColour(PROGRESSDIALOG_DEF_BK);
        m_simplebook->AddPage(m_panel_2line, wxEmptyString, false);

        wxBoxSizer* sizer_1line = new wxBoxSizer(wxHORIZONTAL);
        m_msg = new wxStaticText(m_panel_1line, wxID_ANY, wxEmptyString, wxDefaultPosition, PROGRESSDIALOG_SIMPLEBOOK_SIZE, 0);
        m_msg->Wrap(-1);
        m_msg->SetFont(::Label::Body_13);
        m_msg->SetForegroundColour(PROGRESSDIALOG_GREY_700);
        sizer_1line->Add(m_msg, 0, wxALIGN_CENTER, 0);
        m_panel_1line->SetSizer(sizer_1line);
        m_panel_1line->Layout();
        sizer_1line->Fit(m_panel_1line);

        wxBoxSizer* sizer_2line = new wxBoxSizer(wxVERTICAL);
        m_msg_2line = new wxStaticText(m_panel_2line, wxID_ANY, wxEmptyString, wxDefaultPosition, PROGRESSDIALOG_SIMPLEBOOK_SIZE, 0);
        m_msg_2line->Wrap(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x);
        m_msg_2line->SetFont(::Label::Body_13);
        m_msg_2line->SetForegroundColour(PROGRESSDIALOG_GREY_700);
        m_msg_2line->SetMaxSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, -1));
        sizer_2line->Add(m_msg_2line, 1, wxALL, 0);
        m_panel_2line->SetSizer(sizer_2line);
        m_panel_2line->Layout();
        sizer_2line->Fit(m_panel_2line);

        m_sizer_main->Add(m_simplebook, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(28));
    } else {
        m_msg_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        m_msg_scrolledWindow->SetScrollRate(0, 5);
        wxBoxSizer* m_msg_sizer = new wxBoxSizer(wxVERTICAL);

        m_msg = new wxStaticText(m_msg_scrolledWindow, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                 wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, -1), 0);
        m_msg->Wrap(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x);
        m_msg->SetFont(::Label::Body_13);
        m_msg->SetForegroundColour(PROGRESSDIALOG_GREY_700);

        m_msg_sizer->Add(m_msg, 0, wxEXPAND | wxALL, 0);
        m_msg_scrolledWindow->SetSizer(m_msg_sizer);
        m_msg_scrolledWindow->Layout();
        m_msg_sizer->Fit(m_msg_scrolledWindow);
        m_sizer_main->Add(m_msg_scrolledWindow, 0, wxEXPAND | wxALL, FromDIP(28));
    }

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 0);

    int gauge_style = wxGA_HORIZONTAL;
    if (style & wxPD_SMOOTH)
        gauge_style |= wxGA_SMOOTH;
    gauge_style |= wxGA_PROGRESS;

#ifdef __WXMSW__
    maximum /= m_factor;
#endif

    if (!HasPDFlag(wxPD_NO_PROGRESS)) {
        m_gauge = new wxGauge(this, wxID_ANY, maximum, wxDefaultPosition, PROGRESSDIALOG_GAUGE_SIZE, gauge_style);
        m_gauge->SetValue(0);
        m_sizer_main->Add(m_gauge, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(28));
    }

#ifdef __WXMSW__
    // m_block_left = new wxWindow(m_gauge, wxID_ANY, wxPoint(0, 0), wxSize(FromDIP(2), PROGRESSDIALOG_GAUGE_SIZE.y * 2));
    // m_block_left->SetBackgroundColour(PROGRESSDIALOG_DEF_BK);
    // m_block_right = new wxWindow(m_gauge, wxID_ANY, wxPoint(PROGRESSDIALOG_GAUGE_SIZE.x - 2, 0), wxSize(FromDIP(2),
    // PROGRESSDIALOG_GAUGE_SIZE.y * 2)); m_block_right->SetBackgroundColour(PROGRESSDIALOG_DEF_BK);
#endif
    wxBoxSizer* m_sizer_bottom = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_bottom->Add(0, 0, 1, wxEXPAND, 0);

    if (HasPDFlag(wxPD_CAN_ABORT)) {
        m_button_cancel = new Button(this, _L("Cancel"));
        m_button_cancel->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
        m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
            if (m_state == Finished) {
                event.Skip();
            } else {
                m_state = Canceled;
                DisableAbort();
                m_button_cancel->Enable(false);
                DisableSkip();
                OnCancel();
                m_timeStop = wxGetCurrentTime();
            }
        });
        m_sizer_bottom->Add(m_button_cancel, 0, wxALL, 0);
    }

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(16));
    m_sizer_main->Add(m_sizer_bottom, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(28));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(10));

    wxGetApp().UpdateDlgDarkUI(this);

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Centre(wxCENTER_FRAME | wxBOTH);
    DisableOtherWindows();
    FormatString(message);

    Show();
    Enable();
    if (m_elapsed) {
        SetTimeLabel(0, m_elapsed);
    }

    Update();
    return true;

    //
    //    sizerTop = new wxBoxSizer(wxVERTICAL);
    //    auto def_size_width = wxMin(wxGetClientDisplayRect().width / 3, 305);
    //
    //    wxStaticLine *m_staticline2 = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 1), wxLI_HORIZONTAL);
    //    sizerTop->Add(m_staticline2, 0, wxEXPAND, 0);
    //    auto m_block_top = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 20));
    //    m_block_top->SetBackgroundColour(wxColor(DESIGN_RESOUTION_DEF_BK_COLOR));
    //    sizerTop->Add(m_block_top, 0, wxEXPAND);
    //
    //    m_message_area = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 38));
    //    m_message_area->SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    //    sizerTop->Add(m_message_area, 0, wxLEFT | wxRIGHT | wxBottom | wxEXPAND, 2 * LAYOUT_MARGIN);
    //
    //    m_msg = new wxStaticText(m_message_area, wxID_ANY, wxString(""), wxPoint(0, 10), wxSize(def_size_width, 20), wxST_ELLIPSIZE_END);
    //    m_msg->SetForegroundColour(wxColour(107, 107, 107));
    //    m_msg->SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    //    m_msg->SetMinSize(wxSize(def_size_width, 20));
    //    m_msg->SetMaxSize(wxSize(def_size_width, 20));
    //
    //    m_msg_2line = new wxStaticText(m_message_area, wxID_ANY, wxString(""), wxPoint(0, 0), wxSize(def_size_width, 40),
    //    wxST_ELLIPSIZE_END); m_msg_2line->SetForegroundColour(wxColour(107, 107, 107));
    //    m_msg_2line->SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    //    m_msg_2line->SetMinSize(wxSize(def_size_width, 40));
    //    m_msg_2line->SetMaxSize(wxSize(def_size_width, 40));
    //
    //    auto block_left = new wxWindow(m_gauge, -1, wxPoint(0, 0), wxSize(2, m_gauge->GetSize().GetHeight()));
    //    block_left->SetBackgroundColour(wxColor(255, 255, 255));
    //
    //    auto block_right = new wxWindow(m_gauge, -1, wxPoint(m_gauge->GetSize().GetWidth() - 2, 0), wxSize(2,
    //    m_gauge->GetSize().GetHeight())); block_right->SetBackgroundColour(wxColor(255, 255, 255));
    //
    //    sizerTop->Add(m_gauge, 0, wxLEFT | wxRIGHT | wxBottom, 2 * LAYOUT_MARGIN);
    //    m_gauge->SetValue(0);
    //
    //    m_elapsed = m_estimated = m_remaining = NULL;
    //    size_t nTimeLabels                    = 0;
    //
    //    wxSizer *const sizerLabels = new wxFlexGridSizer(2);
    //
    //    if (style & wxPD_ELAPSED_TIME) {
    //        nTimeLabels++;
    //
    //        m_elapsed = CreateLabel(GetElapsedLabel(), sizerLabels);
    //    }
    //
    //    if (style & wxPD_ESTIMATED_TIME) {
    //        nTimeLabels++;
    //
    //        m_estimated = CreateLabel(GetEstimatedLabel(), sizerLabels);
    //    }
    //
    //    if (style & wxPD_REMAINING_TIME) {
    //        nTimeLabels++;
    //
    //        m_remaining = CreateLabel(GetRemainingLabel(), sizerLabels);
    //    }
    //    sizerTop->Add(sizerLabels, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBottom, LAYOUT_MARGIN);
    //
    //    wxStdDialogButtonSizer *buttonSizer = wxDialog::CreateStdDialogButtonSizer(0);
    //
    //    const int borderFlags = wxALL;
    //
    //    std::string icon_path = (boost::format("%1%/images/common_dialog_confirm.png") % resources_dir()).str();
    //    /*m_btnAbort            = new wxBitmapButton(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(52, 24), wxBU_AUTODRAW |
    //    wxBORDER_NONE); m_btnAbort->SetBitmap(wxBitmap(icon_path, wxBITMAP_TYPE_ANY)); m_btnAbort->SetBitmapDisabled(wxBitmap(icon_path,
    //    wxBITMAP_TYPE_ANY)); m_btnAbort->SetBitmapPressed(wxBitmap(icon_path, wxBITMAP_TYPE_ANY));
    //    m_btnAbort->SetBitmapFocus(wxBitmap(icon_path, wxBITMAP_TYPE_ANY)); m_btnAbort->SetBitmapCurrent(wxBitmap(icon_path,
    //    wxBITMAP_TYPE_ANY));*/
    //
    //     m_btnAbort = new wxButton(this, wxID_CANCEL, wxString(""), wxDefaultPosition, wxSize(52,24));
    //
    //     wxStaticBitmap *m_bitmatAbort = new wxStaticBitmap(m_btnAbort, wxID_ANY, wxBitmap(icon_path, wxBITMAP_TYPE_ANY),
    //     wxDefaultPosition, wxSize(52, 24), 0); wxStaticText *textAbort = new wxStaticText(m_btnAbort, wxID_ANY, _T("Cancel"), wxPoint(5,
    //     3), wxSize(42, 19)); textAbort->SetBa ckgroundColour(wxColor(0, 150, 136));
    //     textAbort->SetForegroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    //
    //     textAbort->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
    //        if (m_state == Finished) {
    //            event.Skip();
    //        } else {
    //            m_state = Canceled;
    //            //DisableAbort();
    //            DisableSkip();
    //            m_timeStop = wxGetCurrentTime();
    //        }
    //    });
    //
    //     wxSizerFlags sizerFlags = wxSizerFlags().Border(borderFlags, LAYOUT_MARGIN);
    //    wxWindow *m_button_sizer = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 26));
    //    m_button_sizer->SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    //
    //    if (HasPDFlag(wxPD_CAN_SKIP)) {
    //        m_btnSkip = new wxButton(this, wxID_SKIP, wxGetTranslation("&Skip"));
    //        m_btnSkip->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &ProgressDialog::OnSkip, this);
    //        buttonSizer->SetNegativeButton(m_btnSkip);
    //    }
    //
    //    if (HasPDFlag(wxPD_CAN_ABORT)) {
    //        /*m_btnAbort = new wxButton(this, wxID_CANCEL);
    //        buttonSizer->SetCancelButton(m_btnAbort);
    //        m_btnAbort->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &ProgressDialog::OnCancel, this);*/
    //
    //        m_button_calcel = new Button(m_button_sizer, _T("Cancel"));
    //        m_button_calcel->SetTextColor(wxColour(107, 107, 107));
    //        m_button_calcel->SetSize(60, 24);
    //        m_button_calcel->SetPosition(wxPoint(m_button_sizer->GetSize().GetWidth() - m_button_calcel->GetSize().GetWidth(), 0));
    //
    //        m_button_calcel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
    //            if (m_state == Finished) {
    //                event.Skip();
    //            } else {
    //                m_state = Canceled;
    //                 DisableAbort();
    //                m_button_calcel->Enable(false);
    //                DisableSkip();
    //                m_timeStop = wxGetCurrentTime();
    //            }
    //        });
    //    }
    //
    //    if (!HasPDFlag(wxPD_CAN_SKIP | wxPD_CAN_ABORT)) buttonSizer->AddSpacer(LAYOUT_MARGIN);
    //
    //    sizerTop->Add(m_button_sizer, 1, wxEXPAND, 0);
    //
    //    auto m_block_bottom = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 15));
    //    m_block_bottom->SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    //    sizerTop->Add(m_block_bottom, 0, wxEXPAND);
    //
    //    SetSizerAndFit(sizerTop);
    //    FormatString(message);
    //
    //    Centre(wxCENTER_FRAME | wxBOTH);
    //
    //    DisableOtherWindows();
    //
    //    Show();
    //    Enable();
    //    if (m_elapsed) { SetTimeLabel(0, m_elapsed); }
    //
    //    Update();
    //    return true;
}

void ProgressDialog::UpdateTimeEstimates(int value, unsigned long& elapsedTime, unsigned long& estimatedTime, unsigned long& remainingTime)
{
    // [STATE] Exponential-smoothing style delay counters damp oscillation in the ETA label so the dialog does not flicker between adjacent
    // values. [UNITY] Keep this as pure model logic behind the view; the UI should consume a debounced ETA/remaining-time value rather than
    // recomputing it from widget state.
    unsigned long elapsed = wxGetCurrentTime() - m_timeStart;
    if (value != 0 && (m_last_timeupdate < elapsed || value == m_maximum)) {
        m_last_timeupdate       = elapsed;
        unsigned long estimated = m_break + (unsigned long) (((double) (elapsed - m_break) * m_maximum) / ((double) value));
        if (estimated > m_display_estimated && m_ctdelay >= 0) {
            ++m_ctdelay;
        } else if (estimated < m_display_estimated && m_ctdelay <= 0) {
            --m_ctdelay;
        } else {
            m_ctdelay = 0;
        }
        if (m_ctdelay >= m_delay             // enough confirmations for a higher value
            || m_ctdelay <= (m_delay * -1)   // enough confirmations for a lower value
            || value == m_maximum            // to stay consistent
            || elapsed > m_display_estimated // to stay consistent
            || (elapsed > 0 && elapsed < 4)  // additional updates in the beginning
        ) {
            m_display_estimated = estimated;
            m_ctdelay           = 0;
        }
    }

    if (value != 0) {
        long display_remaining = m_display_estimated - elapsed;
        if (display_remaining < 0) {
            display_remaining = 0;
        }

        estimatedTime = m_display_estimated;
        remainingTime = display_remaining;
    }

    elapsedTime = elapsed;
}

// static
wxString ProgressDialog::GetFormattedTime(unsigned long timeInSec)
{
    wxString timeAsHMS;

    if (timeInSec == (unsigned long) -1) {
        timeAsHMS = wxGetTranslation("Unknown");
    } else {
        unsigned hours   = timeInSec / 3600;
        unsigned minutes = (timeInSec % 3600) / 60;
        unsigned seconds = timeInSec % 60;
        timeAsHMS.Printf("%u:%02u:%02u", hours, minutes, seconds);
    }

    return timeAsHMS;
}

void ProgressDialog::EnsureActiveEventLoopExists()
{
    // [THREAD] The dialog creates and installs a temporary wx event loop if the app has none, which is a process-level UI-thread bootstrap
    // concern. [PORTING_HAZARD:P1] Unity has no equivalent to swapping the active message loop at dialog creation time; this must become an
    // explicit host-driven lifecycle contract.
    if (!wxEventLoopBase::GetActive()) {
        m_tempEventLoop = new wxEventLoop;
        wxEventLoop::SetActive(m_tempEventLoop);
    }
}

wxStaticText* ProgressDialog::CreateLabel(const wxString& text, wxSizer* sizer)
{
    // [INTENT] Create a paired label/value row whose placement is chosen per-platform so the time estimates look native.
    // [UNITY] Replace with a small reusable row prefab and let the layout system decide alignment; do not recreate the platform-specific
    // placement branches.
    wxStaticText* label = new wxStaticText(this, wxID_ANY, text);
    wxStaticText* value = new wxStaticText(this, wxID_ANY, wxGetTranslation("unknown"));

    // select placement most native or nice on target GUI
#if defined(__WXMSW__) || defined(__WXMAC__) || defined(__WXGTK20__)
    // value and time centered in one row
    sizer->Add(label, 1, wxALIGN_RIGHT | wxTOP | wxRIGHT, LAYOUT_MARGIN);
    sizer->Add(value, 1, wxALIGN_LEFT | wxTOP, LAYOUT_MARGIN);
#else
    // value and time to the right in one row
    sizer->Add(label);
    sizer->Add(value, 0, wxLEFT, LAYOUT_MARGIN);
#endif

    return value;
}

// ----------------------------------------------------------------------------
// ProgressDialog operations
// ----------------------------------------------------------------------------

bool ProgressDialog::Update(int value, const wxString& newmsg, bool* skip)
{
    // [THREAD] Every update yields to UI/user-input events before touching the gauge so cancel/skip clicks can land while long work is
    // running. [STATE] Reaching the maximum value flips the dialog into a finished state, which in turn decides whether the window
    // auto-hides or blocks in ShowModal(). [UNITY] Model this as a progress-state tick on the main thread with explicit completion handling
    // and no synchronous YieldFor equivalent.
    if (!DoBeforeUpdate(skip))
        return false;

    wxCHECK_MSG(m_msg || m_gauge, false, "dialog should be fully created");

#ifdef __WXMSW__
    value /= m_factor;
#endif // __WXMSW__

    wxASSERT_MSG(value <= m_maximum, wxT("invalid progress value"));
    if (m_gauge)
        m_gauge->SetValue(value);

    UpdateMessage(newmsg);

    if ((m_elapsed || m_remaining || m_estimated) && (value != 0)) {
        unsigned long elapsed;
        unsigned long display_remaining;

        UpdateTimeEstimates(value, elapsed, m_display_estimated, display_remaining);

        SetTimeLabel(elapsed, m_elapsed);
        SetTimeLabel(m_display_estimated, m_estimated);
        SetTimeLabel(display_remaining, m_remaining);
    }

    if (value == m_maximum) {
        if (m_state == Finished) {
            // ignore multiple calls to Update(m_maximum): it may sometimes be
            // troublesome to ensure that Update() is not called twice with the
            // same value (e.g. because of the rounding errors) and if we don't
            // return now we're going to generate asserts below
            return true;
        }

        // so that we return true below and that out [Cancel] handler knew what
        // to do
        m_state = Finished;
        if (!HasPDFlag(wxPD_AUTO_HIDE)) {
            EnableClose();
            DisableSkip();
#if defined(__WXMSW__) && !defined(__WXUNIVERSAL__)
            EnableCloseButton();
#endif // __WXMSW__

            if (newmsg.empty()) {
                // also provide the finishing message if the application didn't
                m_msg->SetLabel(wxGetTranslation("Done."));
            }

            // allow the window to repaint:
            // NOTE: since we yield only for UI events with this call, there
            //       should be no side-effects
            wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);

            // NOTE: this call results in a new event loop being created
            //       and to a call to ProcessPendingEvents() (which may generate
            //       unwanted re-entrancies).
            (void) ShowModal();
        } else // auto hide
        {
            // reenable other windows before hiding this one because otherwise
            // Windows wouldn't give the focus back to the window which had
            // been previously focused because it would still be disabled
            ReenableOtherWindows();

            Hide();
        }
    } else // not at maximum yet
    {
        DoAfterUpdate();
    }

    // update the display in case yielding above didn't do it
    Update();

    return m_state != Canceled;
}

bool ProgressDialog::Pulse(const wxString& newmsg, bool* skip)
{
    // [STATE] Indeterminate progress still updates the message and time labels, but it only pulses the gauge instead of setting a numeric
    // value. [UNITY] Keep indeterminate mode as a separate visual state on the retained progress bar rather than overloading the same
    // update path.
    if (!DoBeforeUpdate(skip))
        return false;

    wxCHECK_MSG(m_msg || m_gauge, false, "dialog should be fully created");

    // show a bit of progress
    if (m_gauge)
        m_gauge->Pulse();

    UpdateMessage(newmsg);

    if (m_elapsed || m_remaining || m_estimated) {
        unsigned long elapsed = wxGetCurrentTime() - m_timeStart;

        SetTimeLabel(elapsed, m_elapsed);
        SetTimeLabel((unsigned long) -1, m_estimated);
        SetTimeLabel((unsigned long) -1, m_remaining);
    }

    DoAfterUpdate();

    return m_state != Canceled;
}

bool ProgressDialog::WasCanceled() const { return m_state == Canceled; }

bool ProgressDialog::DoBeforeUpdate(bool* skip)
{
    // [THREAD] This pre-update gate pumps UI/user-input events so the dialog stays responsive while the caller performs background work on
    // the same thread. [PORTING_HAZARD:P2] The logic depends on synchronous re-entrancy to observe skip/cancel clicks; Unity should replace
    // it with explicit frame-driven polling or async callbacks.

    // we have to yield because not only we want to update the display but also to
    // process the clicks on the cancel and skip buttons NOTE: using YieldFor() this call shouldn't give re-entrancy problems
    //       for event handlers not interested to UI/user-input events.
    wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);

    Update();

    if (m_skip && skip && !*skip) {
        *skip  = true;
        m_skip = false;
        EnableSkip();
    }

    return m_state != Canceled;
}

void ProgressDialog::DoAfterUpdate()
{
    // [THREAD] A second yield after the state mutation lets the window repaint before the caller resumes its workload.
    // allow the window to repaint:
    // NOTE: since we yield only for UI events with this call, there
    //       should be no side-effects
    wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
}

void ProgressDialog::Resume()
{
    // [STATE] Resuming clears the canceled state, re-bases the elapsed timer, and re-enables the action buttons.
    m_state   = Continue;
    m_ctdelay = m_delay; // force an update of the elapsed/estimated/remaining time
    m_break += wxGetCurrentTime() - m_timeStop;

    EnableAbort();
    m_button_cancel->Enable(true);
    EnableSkip();
    m_skip = false;
}

bool ProgressDialog::Show(bool show)
{
    // [STATE] Hiding the dialog must re-enable the windows it disabled during modal entry so focus returns to the previous app window.
    // reenable other windows before hiding this one because otherwise
    // Windows wouldn't give the focus back to the window which had
    // been previously focused because it would still be disabled
    if (!show)
        ReenableOtherWindows();
    return wxDialog::Show(show);
}

int ProgressDialog::GetValue() const
{
    wxCHECK_MSG(m_gauge, -1, "dialog should be fully created");

    return m_gauge->GetValue();
}

int ProgressDialog::GetRange() const { return m_maximum; }

wxString ProgressDialog::GetMessage() const { return m_msg->GetLabel(); }

void ProgressDialog::SetRange(int maximum)
{
    wxCHECK_RET(m_gauge, "dialog should be fully created");

    wxCHECK_RET(maximum > 0, "Invalid range");

    m_gauge->SetRange(maximum);

    SetMaximum(maximum);
}

void ProgressDialog::set_panel_height(int height)
{
    // [STATE] The adaptive two-line layout resizes the simplebook, page, and label together so the wrapped title stays aligned.
    m_simplebook->SetSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
    m_simplebook->SetMinSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
    m_panel_2line->SetSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
    m_panel_2line->SetMinSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
    m_msg_2line->SetSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
    m_msg_2line->SetMinSize(wxSize(PROGRESSDIALOG_SIMPLEBOOK_SIZE.x, height));
}

void ProgressDialog::SetMaximum(int maximum)
{
    // [STATE] Windows-specific scaling clamps the logical maximum to the gauge control's range, so the stored maximum and widget range can diverge.
    m_maximum = maximum;

#if defined(__WXMSW__)
    // we can't have values > 65,536 in the progress control under Windows, so
    // scale everything down
    m_factor = m_maximum / 65536 + 1;
    m_maximum /= m_factor;
#endif // __WXMSW__
}

bool ProgressDialog::WasCancelled() const { return HasPDFlag(wxPD_CAN_ABORT) && m_state == Canceled; }

bool ProgressDialog::WasSkipped() const { return HasPDFlag(wxPD_CAN_SKIP) && m_skip; }

// static
void ProgressDialog::SetTimeLabel(unsigned long val, wxStaticText* label)
{
    // [STATE] Time labels are only touched when the formatted string changes, reducing churn during frequent UI updates.
    if (label) {
        wxString s;

        if (val != (unsigned long) -1) {
            s = GetFormattedTime(val);
        } else {
            s = wxGetTranslation("Unknown");
        }

        if (s != label->GetLabel())
            label->SetLabel(s);
    }
}

// ----------------------------------------------------------------------------
// event handlers
// ----------------------------------------------------------------------------

void ProgressDialog::OnCancel(wxCommandEvent& event)
{
    // [EVENT] Cancel transitions the dialog into the canceled state, disables both action buttons, and defers the actual abort handling to
    // the next update cycle.
    if (m_state == Finished) {
        // this means that the count down is already finished and we're being
        // shown as a modal dialog - so just let the default handler do the job
        event.Skip();
    } else {
        // request to cancel was received, the next time Update() is called we
        // will handle it
        m_state = Canceled;

        // update the buttons state immediately so that the user knows that the
        // request has been noticed
        DisableAbort();
        DisableSkip();

        OnCancel();

        // save the time when the dialog was stopped
        m_timeStop = wxGetCurrentTime();
    }
}

void ProgressDialog::OnSkip(wxCommandEvent& WXUNUSED(event))
{
    // [EVENT] Skip is latched until the next update cycle so the caller can observe the request without re-entering the control flow immediately.
    DisableSkip();
    m_skip = true;
}

void ProgressDialog::OnClose(wxCloseEvent& event)
{
    // [EVENT] Close mirrors cancel semantics unless the dialog is still uncancelable or has already finished.
    if (m_state == Uncancelable) {
        // can't close this dialog
        event.Veto();
    } else if (m_state == Finished) {
        // let the default handler close the window as we already terminated
        event.Skip();
    } else {
        // next Update() will notice it
        m_state = Canceled;
        DisableAbort();
        DisableSkip();
        OnCancel();

        m_timeStop = wxGetCurrentTime();
    }
}

// ----------------------------------------------------------------------------
// destruction
// ----------------------------------------------------------------------------

ProgressDialog::~ProgressDialog()
{
    // [THREAD] Destructor cleanup must restore the disabled windows and unwind any temporary event loop without invalidating a newer active
    // loop. [PORTING_HAZARD:P1] The temporary loop ownership and active-loop swap are global-process concerns that need a different
    // lifetime model in Unity.

    // normally this should have been already done, but just in case
    ReenableOtherWindows();

    if (m_tempEventLoop) {
        // If another event loop has been installed as active during the life
        // time of this object, we shouldn't deactivate it, but we also can't
        // delete our m_tempEventLoop in this case because it risks leaving the
        // new event loop with a dangling pointer, which it will set back as
        // the active loop when it exits, resulting in a crash. So we have no
        // choice but to just leak this pointer then, which is, of course, bad
        // and usually easily avoidable by just destroying the progress dialog
        // sooner, so warn the programmer about it.
        wxCHECK_RET(wxEventLoopBase::GetActive() == m_tempEventLoop, "current event loop must not be changed during "
                                                                     "ProgressDialog lifetime");

        wxEventLoopBase::SetActive(NULL);
        delete m_tempEventLoop;
    }
}

void ProgressDialog::DoSetSize(int x, int y, int width, int height, int sizeFlags /*= wxSIZE_AUTO*/)
{
    // [STATE] Resizing also reapplies the cancel button minimum size so the custom footer remains consistent after DPI/layout changes.
    if (m_button_cancel != nullptr) {
        m_button_cancel->SetMinSize(PROGRESSDIALOG_CANCEL_BUTTON_SIZE);
    }

#ifdef __WXMSW__
    // if (m_block_left != nullptr && m_block_right != nullptr) {
    //     m_block_left->SetPosition(wxPoint(0, 0));
    //     m_block_right->SetPosition(wxPoint(PROGRESSDIALOG_GAUGE_SIZE.x - 2, 0));
    // }
#endif
    wxDialog::DoSetSize(x, y, width, height, sizeFlags);
}

void ProgressDialog::DisableOtherWindows()
{
    // [STATE] Modal entry disables either the parent top-level window or the full app, depending on the wxPD_APP_MODAL flag.
    // [UNITY] Port this as an overlay modality mask on the host UI rather than trying to disable OS windows.
    if (HasPDFlag(wxPD_APP_MODAL)) {
#if defined(__WXOSX__)
        if (m_parentTop)
            m_parentTop->Disable();
        m_winDisabler = NULL;
#else
        m_winDisabler = new wxWindowDisabler(this);
#endif
    } else {
        if (m_parentTop)
            m_parentTop->Disable();
        m_winDisabler = NULL;
    }
}

void ProgressDialog::ReenableOtherWindows()
{
    // [STATE] Modal exit restores the previously disabled parent/app window relationship.
    if (HasPDFlag(wxPD_APP_MODAL)) {
#if defined(__WXOSX__)
        if (m_parentTop)
            m_parentTop->Enable();
#else
        wxDELETE(m_winDisabler);
#endif
    } else {
        if (m_parentTop)
            m_parentTop->Enable();
    }
}

// ----------------------------------------------------------------------------
// private functions
// ----------------------------------------------------------------------------

void ProgressDialog::EnableSkip(bool enable)
{
    // [EVENT] Button enablement is toggled separately from the skip latch so the caller can observe the request before the control becomes
    // active again.
    if (HasPDFlag(wxPD_CAN_SKIP)) {
        if (m_btnSkip)
            m_btnSkip->Enable(enable);
    }
}

void ProgressDialog::EnableAbort(bool enable)
{
    // [EVENT] Abort enablement follows the same state gate as skip and only exists when abort was part of the original dialog style.
    if (HasPDFlag(wxPD_CAN_ABORT)) {
        if (m_btnAbort)
            m_btnAbort->Enable(enable);
    }
}

void ProgressDialog::EnableClose()
{
    // [STATE] The abort button becomes a Close button when work is finished, preserving the same control slot with a different meaning.
    if (HasPDFlag(wxPD_CAN_ABORT)) {
        if (m_btnAbort) {
            m_btnAbort->Enable();
            m_btnAbort->SetLabel(wxGetTranslation("Close"));
        }
    }
}

void ProgressDialog::UpdateMessage(const wxString& newmsg)
{
    // [STATE] Message updates can trigger a layout resize when the wrapped title grows wider than the current dialog, so the label tree and
    // window size move together. [UNITY] Keep message content in a retained text view with a layout pass, instead of mutating child sizes
    // from inside the update method.
    if (!newmsg.empty() && newmsg != m_msg->GetLabel()) {
        const wxSize sizeOld = m_msg->GetSize();

        FormatString(newmsg);
        Fit();
        // m_msg->SetLabel(newmsg);

        if (m_msg->GetSize().x > sizeOld.x) {
            // Resize the dialog to fit its new, longer contents instead of
            // just truncating it.
            Fit();
        }

        // allow the window to repaint:
        // NOTE: since we yield only for UI events with this call, there
        //       should be no side-effects
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
    }
}
}} // namespace Slic3r::GUI
