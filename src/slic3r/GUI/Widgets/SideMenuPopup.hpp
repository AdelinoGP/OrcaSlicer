#ifndef slic3r_GUI_SideMenuPopup_hpp_
#define slic3r_GUI_SideMenuPopup_hpp_

#include <wx/stattext.h>
#include <wx/vlbox.h>
#include <wx/combo.h>
#include <wx/htmllbox.h>
#include <wx/frame.h>
#include <vector>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"
#include "SideButton.hpp"
#include "PopupWindow.hpp"

// [INTENT] Transient popup shell for the side-menu button stack.
// [STATE] Stores a caller-populated raw pointer list of buttons and relies on PopupWindow for the actual dismissal/focus lifecycle.
// [EVENT] Exposes popup/show/dismiss hooks plus a paint handler for the popup chrome.
// [UNITY] Map this to a floating panel/overlay controller with a vertical layout and explicit open/close state.
// [PORTING_HAZARD:P2] Layout width is recomputed from child min sizes and the popup is positioned against the focus widget and screen bounds.
class SidePopup : public PopupWindow
{
private:
    std::vector<SideButton*> btn_list;

public:
    SidePopup(wxWindow* parent);
    ~SidePopup();

    void Create();

    virtual void Popup(wxWindow* focus = NULL) wxOVERRIDE;
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent& event) wxOVERRIDE;
    virtual bool Show(bool show = true) wxOVERRIDE;

    void append_button(SideButton* btn);

    void paintEvent(wxPaintEvent& evt);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_Button_hpp_
