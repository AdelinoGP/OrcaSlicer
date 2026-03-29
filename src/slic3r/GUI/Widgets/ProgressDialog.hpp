#ifndef slic3r_GUI_PROGRESSDIALOG_hpp_
#define slic3r_GUI_PROGRESSDIALOG_hpp_

#include "wx/dialog.h"
#include "wx/progdlg.h"
#include "wx/weakref.h"
#include "wx/simplebook.h"
#include "Button.hpp"
#include "../wxExtensions.hpp"

class WXDLLIMPEXP_FWD_CORE wxButton;
class WXDLLIMPEXP_FWD_CORE wxEventLoop;
class WXDLLIMPEXP_FWD_CORE wxGauge;
class WXDLLIMPEXP_FWD_CORE wxStaticText;
class WXDLLIMPEXP_FWD_CORE wxWindowDisabler;

#define PROGRESSDIALOG_SIMPLEBOOK_SIZE wxSize(FromDIP(320), FromDIP(38))
#define PROGRESSDIALOG_GAUGE_SIZE wxSize(FromDIP(320), FromDIP(6))
#define PROGRESSDIALOG_CANCEL_BUTTON_SIZE wxSize(FromDIP(60), FromDIP(24))
#define PROGRESSDIALOG_DEF_BK wxColour(255, 255, 255)
#define PROGRESSDIALOG_GREY_700 wxColour(107, 107, 107)

#define wxPD_NO_PROGRESS 0x0100

namespace Slic3r { namespace GUI {

// [INTENT] Modal progress dialog that owns the user-facing progress workflow: title/message formatting,
//          elapsed/estimated/remaining time updates, and the optional cancel/skip controls.
// [STATE] The dialog keeps separate layout modes, a cached progress range/value pair, adaptive sizing,
//         and a top-level parent reference so the window can be disabled/re-enabled while shown.
// [EVENT] Update(), Pulse(), OnCancel(), OnSkip(), and OnClose() form the public progress/cancel surface;
//         the private helpers coordinate button state, message updates, and the nested modal lifecycle.
// [THREAD] This API assumes UI-thread ownership. The internal wxEventLoop / wxWindowDisabler pair makes
//          the dialog re-entrant-sensitive and difficult to mirror with a purely passive Unity panel.
// [UNITY] Port as a modal overlay controller with a retained progress view, a separate async task/service
//         for progress reporting, and explicit main-thread marshaling for cancel/skip completion.
// [PORTING_HAZARD:P1] wxEventLoop, wxWindowDisabler, and direct parent disabling encode native modal-loop
//                    behavior that has no 1:1 Unity equivalent and must be flattened into host-driven state.
class WXDLLIMPEXP_CORE ProgressDialog : public wxDialog
{
public:
    ProgressDialog();
    ProgressDialog(const wxString& title,
                   const wxString& message,
                   int             maximum  = 100,
                   wxWindow*       parent   = NULL,
                   int             style    = wxPD_APP_MODAL | wxPD_AUTO_HIDE,
                   bool            adaptive = false);

    void OnPaint(wxPaintEvent& evt);
    virtual ~ProgressDialog();

    virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);
    bool         Create(const wxString& title,
                        const wxString& message,
                        int             maximum = 100,
                        wxWindow*       parent  = NULL,
                        int             style   = wxPD_APP_MODAL | wxPD_AUTO_HIDE);

    virtual bool Update(int value, const wxString& newmsg = wxEmptyString, bool* skip = NULL);
    virtual bool Pulse(const wxString& newmsg = wxEmptyString, bool* skip = NULL);
    bool         WasCanceled() const;

    virtual void Resume();

    virtual int      GetValue() const;
    virtual int      GetRange() const;
    virtual wxString GetMessage() const;

    virtual void SetRange(int maximum);
    // Return whether "Cancel" or "Skip" button was pressed, always return
    // false if the corresponding button is not shown.
    virtual bool WasCancelled() const;
    virtual bool WasSkipped() const;

    virtual void OnCancel() {};

    // Must provide overload to avoid hiding it (and warnings about it)
    virtual void Update() wxOVERRIDE { wxDialog::Update(); }

    virtual bool Show(bool show = true) wxOVERRIDE;

    // This enum is an implementation detail and should not be used
    // by user code.
    enum State {
        Uncancelable = -1, // dialog can't be canceled
        Canceled,          // can be cancelled and, in fact, was
        Continue,          // can be cancelled but wasn't
        Finished,          // finished, waiting to be removed from screen
        Dismissed          // was closed by user after finishing
    };

    // [STATE] Layout mode switch: single-line mode uses the simplebook/page split, while two-line mode
    //         exposes the larger message area and time estimate labels.
    int m_mode = 0; // 0 is 1line mode 1 is 2line mode

    // [STATE] Adaptive sizing toggles whether the dialog resizes itself around message/content changes.
    bool m_adaptive = {false};
    // [STATE] Root layout and retained subpanels for the two presentation modes.
    wxSizer*      m_sizer_main  = {nullptr};
    wxPanel*      m_top_line    = {nullptr};
    wxSimplebook* m_simplebook  = {nullptr};
    wxPanel*      m_panel_2line = {nullptr};
    wxPanel*      m_panel_1line = {nullptr};
    // [STATE] Optional cancel affordance exposed by the dialog configuration.
    Button* m_button_cancel = {nullptr};
    // wxWindow *          m_block_left = {nullptr};
    // wxWindow *          m_block_right = {nullptr};
    //  [STATE] Scrollable message surface used by the wrapped progress text path.
    wxScrolledWindow* m_msg_scrolledWindow = {nullptr};

protected:
    // Update just the m_maximum field, this is used by public SetRange() but,
    // unlike it, doesn't update the controls state. This makes it useful for
    // both this class and its derived classes that don't use m_gauge to
    // display progress.
    void SetMaximum(int maximum);
    // Return the labels to use for showing the elapsed/estimated/remaining
    // times respectively.
    static wxString GetElapsedLabel() { return wxGetTranslation("Elapsed time:"); }
    static wxString GetEstimatedLabel() { return wxGetTranslation("Estimated time:"); }
    static wxString GetRemainingLabel() { return wxGetTranslation("Remaining time:"); }

    // Similar to wxWindow::HasFlag() but tests for a presence of a wxPD_XXX
    // flag in our (separate) flags instead of using m_windowStyle.
    bool HasPDFlag(int flag) const { return (m_pdStyle & flag) != 0; }

    // Return the progress dialog style. Prefer to use HasPDFlag() if possible.
    int  GetPDStyle() const { return m_pdStyle; }
    void SetPDStyle(int pdStyle) { m_pdStyle = pdStyle; }
    void set_panel_height(int height);

    // Updates estimated times from a given progress bar value and stores the
    // results in provided arguments.
    void UpdateTimeEstimates(int value, unsigned long& elapsedTime, unsigned long& estimatedTime, unsigned long& remainingTime);

    // Converts seconds to HH:mm:ss format.
    static wxString GetFormattedTime(unsigned long timeInSec);

    // Create a new event loop if there is no currently running one.
    void EnsureActiveEventLoopExists();

    // callback for optional abort button
    void OnCancel(wxCommandEvent&);

    // callback for optional skip button
    void OnSkip(wxCommandEvent&);

    // callback to disable "hard" window closing
    void OnClose(wxCloseEvent&);

    // called to disable the other windows while this dialog is shown
    void DisableOtherWindows();

    // must be called to re-enable the other windows temporarily disabled while
    // the dialog was shown
    void ReenableOtherWindows();

    // Store the parent window as wxWindow::m_parent and also set the top level
    // parent reference we store in this class itself.
    void SetTopParent(wxWindow* parent);

    wxString FormatString(wxString title);
    // return the top level parent window of this dialog (may be NULL)
    wxWindow* GetTopParent() const { return m_parentTop; }

    // [STATE] Continue/cancel lifecycle state returned by Update()/Pulse() and the close handlers.
    State m_state;

    // [STATE] Progress maximum and, on Windows, the reduced-factor bridge to the native gauge range.
    int m_maximum;

#if defined(__WXMSW__)
    // the factor we use to always keep the value in 16 bit range as the native
    // control only supports ranges from 0 to 65,535
    size_t m_factor;
#endif // __WXMSW__

    // [STATE] Timing snapshot used for elapsed/estimated/remaining labels and update throttling.
    unsigned long m_timeStart;
    unsigned long m_timeStop;
    unsigned long m_break;

private:
    // update the label to show the given time (in seconds)
    static void SetTimeLabel(unsigned long val, wxStaticText* label);

    // common part of all ctors
    void Init();

    // create the label with given text and another one to show the time nearby
    // as the next windows in the sizer, returns the created control
    wxStaticText* CreateLabel(const wxString& text, wxSizer* sizer);

    // updates the label message
    void UpdateMessage(const wxString& newmsg);

    // common part of Update() and Pulse(), returns true if not cancelled
    bool DoBeforeUpdate(bool* skip);

    // common part of Update() and Pulse()
    void DoAfterUpdate();

    // shortcuts for enabling buttons
    void EnableClose();
    void EnableSkip(bool enable = true);
    void EnableAbort(bool enable = true);
    void DisableSkip() { EnableSkip(false); }
    void DisableAbort() { EnableAbort(false); }

    // [STATE] Retained progress widgets and labels; they are updated in place instead of recreated.
    wxGauge*      m_gauge;
    wxStaticText* m_msg;
    wxStaticText* m_msg_2line;
    wxStaticText *m_elapsed, *m_estimated, *m_remaining;

    // [STATE] Non-owning top-level parent reference; auto-null behavior avoids dangling disable/reenable calls.
    wxWindowRef m_parentTop;

    // [STATE] ProgressDialog-specific style bits are stored separately from wxWindow styles to avoid flag collisions.
    int m_pdStyle;

    // [STATE] Skip flag propagated through Update()/Pulse() when the optional skip button is used.
    bool m_skip;

    // [STATE] Optional command buttons and throttled time-estimation caches.
    wxButton* m_btnAbort;
    wxButton* m_btnSkip;

    // [STATE] Time-of-last-update and smoothing parameters used to avoid noisy time estimates.
    unsigned long m_last_timeupdate;
    int           m_delay;
    int           m_ctdelay;
    unsigned long m_display_estimated;

    // [STATE] Native modal-loop helpers: window disabler plus temporary event loop when the caller lacks one.
    wxWindowDisabler* m_winDisabler;
    wxEventLoop*      m_tempEventLoop;

    wxDECLARE_NO_COPY_CLASS(ProgressDialog);
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_PROGRESSDIALOG_hpp_
