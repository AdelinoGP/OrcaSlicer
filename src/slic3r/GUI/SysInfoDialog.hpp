#ifndef slic3r_GUI_SysInfoDialog_hpp_
#define slic3r_GUI_SysInfoDialog_hpp_

#include <wx/wx.h>
#include <wx/html/htmlwin.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Modal system-information dialog with branded summary content, process/OpenGL details, and clipboard export.
// [STATE] Owns the rescaled logo bitmap, the summary/report panes, and the copy button for the modal lifetime.
// [UNITY] Map this to a modal overlay/popup with a retained report-text model, two scrollable text panes, and a command button.
// [PORTING_HAZARD:P2] The dialog mixes presentation with live process inspection and clipboard writes; Unity should move data collection
// and clipboard access behind services.
class SysInfoDialog : public DPIDialog
{
    // [STATE] DPI-scaled branding bitmap that is resampled when the window moves between monitors.
    ScalableBitmap m_logo_bmp;
    // [STATE] Static image control that displays the app logo in the dialog chrome.
    wxStaticBitmap* m_logo;
    // [STATE] Scrollable report pane for process, memory, and OpenGL capability details.
    wxHtmlWindow* m_opengl_info_html;
    // [STATE] Scrollable summary pane for version, build, OS, and hardware summary content.
    wxHtmlWindow* m_html;

    // [EVENT] Button bound to the clipboard export handler.
    wxButton* m_btn_copy_to_clipboard;

public:
    SysInfoDialog();

protected:
    // [EVENT] DPI refresh path that rescales the logo and reapplies font/layout metrics.
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    // [EVENT] Copy the same report text used by the dialog body into the system clipboard.
    void onCopyToClipboard(wxEvent&);
    // [EVENT] Close the modal dialog with an explicit result.
    void onCloseDialog(wxEvent&);
};
}} // namespace Slic3r::GUI

#endif
