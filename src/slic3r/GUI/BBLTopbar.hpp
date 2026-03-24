#pragma once

#include "wx/wxprec.h"
#include "wx/aui/auibar.h"

#include "SelectMachine.hpp"
#include "DeviceManager.hpp"

using namespace Slic3r::GUI;

// [INTENT] Top-of-window toolbar that owns menu dropdowns, window controls, publish/undo/redo, and the model store button so the frame
// behaves like the legacy main UI bar. [UNITY] Map this to a UI Toolkit `Toolbar` VisualElement hierarchy driven by a MonoBehaviour that
// keeps dropdown menus, icons, and window-state toggles synchronized with a shared settings ScriptableObject. [PORTING_HAZARD:P2] Unity
// lacks `wxAuiToolBar` layout math; its button ordering and resizing need manual layout logic in C# while preserving focus callbacks.
class BBLTopbar : public wxAuiToolBar
{
public:
    BBLTopbar(wxWindow* pwin, wxFrame* parent);
    BBLTopbar(wxFrame* parent);
    void Init(wxFrame* parent);
    ~BBLTopbar();
    void UpdateToolbarWidth(int width);
    void Rescale();
    // [EVENT][THREAD] Called by wxWidgets on the UI thread whenever a toolbar button changes frame state.
    void OnIconize(wxAuiToolBarEvent& event);
    void OnFullScreen(wxAuiToolBarEvent& event);
    void OnCloseFrame(wxAuiToolBarEvent& event);
    // [EVENT] File/calibration/menu dropdown presses are translated into frame actions synchronously.
    void OnFileToolItem(wxAuiToolBarEvent& evt);
    void OnDropdownToolItem(wxAuiToolBarEvent& evt);
    void OnCalibToolItem(wxAuiToolBarEvent& evt);
    // [EVENT] Mouse capture dance interprets double-clicks on the toolbar background for maximize behavior.
    void OnMouseLeftDClock(wxMouseEvent& mouse);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);
    void OnMenuClose(wxMenuEvent& event);
    void OnOpenProject(wxAuiToolBarEvent& event);
    void show_publish_button(bool show);
    void OnSaveProject(wxAuiToolBarEvent& event);
    void OnUndo(wxAuiToolBarEvent& event);
    void OnRedo(wxAuiToolBarEvent& event);
    void OnModelStoreClicked(wxAuiToolBarEvent& event);
    void OnPublishClicked(wxAuiToolBarEvent& event);

    // [STATE] Helpers managing dropdown ownership, text, and frame size metadata that live alongside the toolbar items.
    wxAuiToolBarItem* FindToolByCurrentPosition();

    void    SetFileMenu(wxMenu* file_menu);
    void    AddDropDownSubMenu(wxMenu* sub_menu, const wxString& title);
    void    AddDropDownMenuItem(wxMenuItem* menu_item);
    wxMenu* GetTopMenu();
    wxMenu* GetCalibMenu();
    void    SetTitle(wxString title);
    void    SetMaximizedSize();
    void    SetWindowSize();

    void EnableUndoRedoItems();
    void DisableUndoRedoItems();

    void SaveNormalRect();

    void ShowCalibrationButton(bool show = true);

protected:
#ifdef __WIN32__
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    // [STATE] Keeps a non-owning pointer to the parent frame so the toolbar can reach back for sizing and menu actions.
    wxFrame* m_frame;
    // [STATE] Menu anchor items used to drive dropdown placement without re-scanning the toolbar.
    wxAuiToolBarItem* m_file_menu_item;
    wxAuiToolBarItem* m_dropdown_menu_item;
    // [STATE] Window restore geometry for toggling maximize/minimize plus the accumulated drag delta.
    wxRect  m_normalRect;
    wxPoint m_delta;
    // [STATE] Menu containers that live for the toolbar lifetime and manage dropdown entries.
    wxMenu  m_top_menu;
    wxMenu* m_file_menu;
    wxMenu  m_calib_menu;
    // [STATE] Title/account/model store buttons that display user info and route selection actions.
    wxAuiToolBarItem* m_title_item;
    wxAuiToolBarItem* m_account_item;
    wxAuiToolBarItem* m_model_store_item;

    // [STATE] Publish/undo/redo/calc items rely on the mutable active project state.

    wxAuiToolBarItem* m_publish_item;
    wxAuiToolBarItem* m_undo_item;
    wxAuiToolBarItem* m_redo_item;
    wxAuiToolBarItem* m_calib_item;
    wxAuiToolBarItem* maximize_btn;

    // [STATE] Publish button bitmaps that are swapped depending on whether publish is available.
    wxBitmap m_publish_bitmap;
    wxBitmap m_publish_disable_bitmap;

    wxBitmap maximize_bitmap;
    wxBitmap window_bitmap;

    int m_toolbar_h;
    // [STATE] Popup guards keep the toolbar from re-opening menus while one is closing.
    bool m_skip_popup_file_menu;
    bool m_skip_popup_dropdown_menu;
    bool m_skip_popup_calib_menu;
};
