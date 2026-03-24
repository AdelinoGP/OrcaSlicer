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
    // [INTENT][THREAD] Bind event handlers, menu ownership, and layout helpers to the parent frame from the UI thread.
    void Init(wxFrame* parent);
    // [INTENT][THREAD] Tear down dropdown menus and event sinks on destruction so dangling callbacks are cleared; Unity should dispose the
    // VisualElement toolbar on the main thread and unbind MonoBehaviour events before destroying the window.
    ~BBLTopbar();
    // [STATE][UNITY] Cache the measured toolbar width so Unity layouts (VisualElement toolbar + layoutData) can keep button spacing consistent.
    void UpdateToolbarWidth(int width);
    // [STATE][UNITY] DPI-aware spacing refresh that mirrors Unity's CanvasScaler-based resizing when the system scale changes.
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
    // [STATE][UNITY] Toggle between publish icons/labels so Unity can swap `Button` sprites and maintain the same enabled/disabled semantics.
    void show_publish_button(bool show);
    void OnSaveProject(wxAuiToolBarEvent& event);
    void OnUndo(wxAuiToolBarEvent& event);
    void OnRedo(wxAuiToolBarEvent& event);
    void OnModelStoreClicked(wxAuiToolBarEvent& event);
    void OnPublishClicked(wxAuiToolBarEvent& event);

    // [STATE][UNITY] Helpers keep the dropdown owner, title, and frame metadata synchronized so Unity can anchor menus to the matching
    // VisualElement.
    wxAuiToolBarItem* FindToolByCurrentPosition();

    // [STATE][EVENT][UNITY] File and dropdown menus are owned here so their assertions about window state are centralized; Unity will
    // reproduce this via `ToolbarMenu` assets bound to shared command data.
    void    SetFileMenu(wxMenu* file_menu);
    void    AddDropDownSubMenu(wxMenu* sub_menu, const wxString& title);
    void    AddDropDownMenuItem(wxMenuItem* menu_item);
    wxMenu* GetTopMenu();
    wxMenu* GetCalibMenu();
    // [STATE][UNITY] Title captions are mirrored in Unity with `Label` elements bound to ScriptableObject data (player name, project name).
    void SetTitle(wxString title);
    // [STATE] Track the maximized rectangle to restore window geometry and keep Unity's layout data in sync.
    void SetMaximizedSize();
    // [STATE] Apply the stored toolbar window dimensions before layout adjustments (map to keeping `RectTransform` default sizes in Unity).
    void SetWindowSize();

    // [STATE][UNITY] Undo/redo enablement mirrors the command stack state; Unity should toggle `Button.interactable` on the main thread.
    void EnableUndoRedoItems();
    // [STATE][UNITY] Keep the toolbar buttons visually disabled when no undo/redo is available.
    void DisableUndoRedoItems();

    // [STATE] Snapshot the frame geometry before layout shifts so toggling maximize/minimize works reliably in the Unity port.
    void SaveNormalRect();

    // [STATE][UNITY] Conditionally render the calibration button and platform menu; Unity would add/remove the VisualElement via class toggling.
    void ShowCalibrationButton(bool show = true);

protected:
#ifdef __WIN32__
    // [THREAD][PORTING_HAZARD:P3] Windows message hook for toolbar drag/double-click handling runs on the UI thread; Unity must
    // re-interpret these gestures with `PointerDown/PointerUp` events.
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

    // [STATE][UNITY] Publish/undo/redo/calc items rely on the mutable active project state and translate to Unity `Button` command bindings.

    wxAuiToolBarItem* m_publish_item;
    wxAuiToolBarItem* m_undo_item;
    wxAuiToolBarItem* m_redo_item;
    wxAuiToolBarItem* m_calib_item;
    // [STATE][UNITY] Dedicated maximize/restore tool item helps the Unity toolbar swap the correct `Button` sprite when the window state changes.
    wxAuiToolBarItem* maximize_btn;

    // [STATE] Publish button bitmaps that are swapped depending on whether publish is available.
    wxBitmap m_publish_bitmap;
    wxBitmap m_publish_disable_bitmap;

    // [STATE][UNITY][PORTING_HAZARD:P3] Cached bitmaps for the native maximize/restore controls; Unity must mirror these as
    // `Sprite`/`Texture2D` assets and manage their lifetime to prevent leaking GPU handles when buttons rebind.
    wxBitmap maximize_bitmap;
    wxBitmap window_bitmap;

    // [STATE] Last measured toolbar height used when switching between icon-only and full-width layouts for the Unity toolbar height animation.
    int m_toolbar_h;
    // [STATE][EVENT] Popup guards keep the toolbar from re-opening menus while one is closing, avoiding duplicate event loops.
    bool m_skip_popup_file_menu;
    bool m_skip_popup_dropdown_menu;
    bool m_skip_popup_calib_menu;
};
