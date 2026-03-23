#ifndef slic3r_GUI_AuxiliaryDialog_hpp_
#define slic3r_GUI_AuxiliaryDialog_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

class AuxiliaryList;

namespace Slic3r { 
namespace GUI {

// [INTENT] DPI-aware overlay that owns the auxiliary list and keeps the right panel scaled/layout-sync'd with the current display metrics.
// [UNITY] Map to a UI Toolkit `VisualElement` panel whose `ListView` is driven by a ScriptableObject-backed list model and resizes via
// `RectTransform` updates.
// [PORTING_HAZARD:P3] Unity uses device-independent units, so convert wx DPI events into `Display.main.renderingWidth/Height` refreshes.
class AuxiliaryDialog : public DPIDialog
{
public:
    AuxiliaryDialog(wxWindow * parent);

    // [STATE] Persistent pointer to the AuxiliaryList widget; owns the list data, selection, and command routing for the dialog.
    // [UNITY] Replace with a `ListView` bound to the same `AuxiliaryItem` collection exposed by a MonoBehaviour controller.
    AuxiliaryList * aux_list() { return m_aux_list; }

protected:
    // [EVENT][THREAD] Fires when `DPIDialog` re-requests layout after a monitor DPI change; runs on the UI thread, so Unity must marshal
    // updates through the main-thread dispatcher before touching VisualElements.
    // [PORTING_HAZARD:P2] DPI values can arrive mid-layout, so Unity must sample `Display.displays`/`CanvasScaler` on the main thread
    // before resizing child elements.
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    // [STATE] Owns the list reference for layout/selection cache reuse while the dialog persists on the UI thread.
    AuxiliaryList * m_aux_list;
};

} // namespace GUI
} // namespace Slic3r

#endif
