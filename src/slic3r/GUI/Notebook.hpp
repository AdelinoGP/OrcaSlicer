#ifndef slic3r_Notebook_hpp_
#define slic3r_Notebook_hpp_

// [INTENT] This header defines the interfaces for ButtonsListCtrl and Notebook classes.
// ButtonsListCtrl is a custom control for managing a list of buttons, typically used as tabs.
// Notebook is a custom notebook-like control that uses ButtonsListCtrl for navigation.

// #ifdef _WIN32

#include <wx/bookctrl.h> // [INTENT] Base class for generic book controls (like notebooks, tab controls).
#include <wx/sizer.h>    // [INTENT] For layout management.

// [PORTING_HAZARD:P1] Build environment issues: 'wx/bookctrl.h' and other wxWidgets types not found by LSP.
// This might indicate an incomplete wxWidgets installation or incorrect CMake configuration,
// which needs to be resolved for successful compilation in the C++ environment.
// For Unity porting, this highlights a dependency on wxWidgets' build system and proper inclusion paths.

class ModeSizer;
class ScalableButton;
class Button;

// [INTENT] Custom event for signaling a notebook tab (button) selection change.
// [UNITY] This custom event will be replaced by C# events or Unity UI Toolkit's UIElements events.
wxDECLARE_EVENT(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, wxCommandEvent);

// [INTENT] ButtonsListCtrl is a custom wxControl that displays a list of buttons,
// acting as a tab or navigation bar. It handles visual feedback for selection
// and integrates with external side tools.
// [STATE]
// m_selection: Currently selected button index.
// m_pageButtons: Vector of Button pointers, representing the individual tabs/pages.
// m_sizer, m_buttons_sizer: wxSizer objects managing the layout of buttons.
// [UNITY] This control will likely be implemented as a custom UI Toolkit VisualElement
// that contains multiple Button VisualElements. The layout will be managed by UI Toolkit's flexbox system.
class ButtonsListCtrl : public wxControl
{
public:
    // [INTENT] Constructor for ButtonsListCtrl. Initializes the control and sets up layout.
    // [UNITY] Corresponds to a custom VisualElement constructor or `UxmlFactory` method.
    ButtonsListCtrl(wxWindow* parent, wxBoxSizer* side_tools = NULL);
    // [INTENT] Destructor.
    ~ButtonsListCtrl() {}

    // [INTENT] Custom paint event handler to draw selection highlights and lines.
    // [EVENT] Binds to wxEVT_PAINT (though commented out in .cpp).
    // [OPENGL] This method uses basic GDI+ drawing, not OpenGL directly.
    // [UNITY] Custom rendering like this will need to be re-implemented using UI Toolkit's custom drawing APIs,
    // or by manipulating VisualElement styles/classes based on selection state.
    void OnPaint(wxPaintEvent&);

    // [INTENT] Sets the currently selected button (tab) and updates its visual appearance.
    // [STATE] Updates m_selection and button background/text colors.
    // [UNITY] This logic will translate to changing VisualElement styles (e.g., adding/removing USS classes)
    // or updating properties of individual UI Toolkit buttons.
    void SetSelection(int sel);

    // [INTENT] Updates the display mode, currently commented out in .cpp.
    // [UNCLEAR] Functionality seems unused or deprecated.
    void UpdateMode();

    // [INTENT] Rescales UI elements based on DPI changes.
    // [EVENT] Called when a rescale event occurs.
    // [UNITY] DPI scaling will be handled by Unity's Canvas Scaler or UI Toolkit's scaling mechanisms,
    // though explicit adjustments for custom elements may still be required.
    void Rescale();

    // [INTENT] Inserts a new button (page) into the list at a specified position.
    // [EVENT] Binds a click event to the new button to dispatch wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED.
    // [UNITY] New buttons will be instantiated as UI Toolkit VisualElements and added to a parent container.
    // Event binding will use UI Toolkit's EventSystem.
    bool InsertPage(
        size_t n, const wxString& text, bool bSelect = false, const std::string& bmp_name = "", const std::string& inactive_bmp_name = "");

    // [INTENT] Removes a button (page) from the list at a specified position.
    // [UNITY] Removing a VisualElement from its parent container.
    void RemovePage(size_t n);

    // [INTENT] Sets the image for a specific page button.
    // [UNCLEAR] The actual bitmap setting is commented out in .cpp.
    // [UNITY] Updating the `background-image` style property of a UI Toolkit VisualElement or an Image component.
    bool SetPageImage(size_t n, const std::string& bmp_name) const;

    // [INTENT] Sets the text label for a specific page button.
    // [UNITY] Updating the `text` property of a UI Toolkit Label or Button VisualElement.
    void SetPageText(size_t n, const wxString& strText);

    // [INTENT] Retrieves the text label for a specific page button.
    // [UNITY] Accessing the `text` property of a UI Toolkit Label or Button VisualElement.
    wxString GetPageText(size_t n) const;

private:
    // [STATE] Sizer for arranging buttons in a grid.
    wxFlexGridSizer* m_buttons_sizer;
    // [STATE] Main sizer for the entire control, arranging the buttons sizer and side tools.
    wxBoxSizer* m_sizer;
    // [STATE] Vector storing pointers to the custom Button controls acting as pages/tabs.
    std::vector<Button*> m_pageButtons;
    // [STATE] Index of the currently selected button, initialized to -1 (no selection).
    int m_selection{-1};
    // [STATE] Margin value used for spacing between buttons.
    int m_btn_margin;
    // [STATE] Margin for the visual highlight line.
    int m_line_margin;
    // ModeSizer*                      m_mode_sizer {nullptr}; // [UNCLEAR] Commented out, related to mode selection buttons.
};

// [INTENT] Notebook is a custom control that manages multiple "pages" or tabs,
// using ButtonsListCtrl for tab navigation. It inherits from wxBookCtrlBase,
// providing common book control functionality and handling page insertion, selection, and display.
// [STATE]
// m_bookctrl: An instance of ButtonsListCtrl used for tab navigation.
// m_controlSizer: Sizer for the navigation controls.
// m_showEffect, m_hideEffect: Visual effects for page transitions.
// m_showTimeout, m_hideTimeout: Timouts for visual effects.
// [UNITY] This will be implemented as a custom UI Toolkit Document (UXML) with a C# MonoBehaviour
// or a custom VisualElement that orchestrates a set of child VisualElements (pages)
// and a navigation bar (implemented using concepts from ButtonsListCtrl).
class Notebook : public wxBookCtrlBase
{
public:
    // [INTENT] Constructor for Notebook. Initializes and creates the control.
    // [UNITY] Corresponds to a custom VisualElement constructor or `UxmlFactory` method for the root Notebook element.
    Notebook(wxWindow*      parent,
             wxWindowID     winid = wxID_ANY,
             const wxPoint& pos   = wxDefaultPosition,
             const wxSize&  size  = wxDefaultSize,
             // BBS
             wxBoxSizer* side_tools = NULL,
             long        style      = 0);
    // [INTENT] Create method to initialize the wxWidgets control.
    // [UNITY] Initialization logic will be part of the C# MonoBehaviour's Awake/Start methods
    // or a VisualElement's `OnGeometryChanged` callback.
    bool Create(wxWindow*      parent,
                wxWindowID     winid = wxID_ANY,
                const wxPoint& pos   = wxDefaultPosition,
                const wxSize&  size  = wxDefaultSize,
                // BBS
                wxBoxSizer* side_tools = NULL,
                long        style      = 0);

    // Methods specific to this class.

    // [INTENT] Adds a new page to the control without any label and shows it immediately.
    // [UNITY] Method to add a new VisualElement page to the Notebook's content area.
    bool ShowNewPage(wxWindow* page);

    // [INTENT] Set effect to use for showing/hiding pages.
    // [UNITY] Page transition animations/effects will be handled by UI Toolkit animations or custom scripting.
    void SetEffects(wxShowEffect showEffect, wxShowEffect hideEffect);

    // [INTENT] Sets the same effect for both showing and hiding.
    void SetEffect(wxShowEffect effect);

    // [INTENT] Sets timeouts for showing and hiding effects.
    // [UNITY] These timeouts will be managed within Unity's animation system.
    void SetEffectsTimeouts(unsigned showTimeout, unsigned hideTimeout);

    // [INTENT] Sets the same timeout for both effects.
    void SetEffectTimeout(unsigned timeout);

    // Implement base class pure virtual methods.

    // [INTENT] Adds a new page to the control. Overrides wxBookCtrlBase::AddPage.
    // [UNITY] Adds a new VisualElement to the content area and creates a corresponding navigation item.
    bool AddPage(
        wxWindow* page, const wxString& text, const std::string& bmp_name, const std::string& inactive_bmp_name, bool bSelect = false);

    // [INTENT] Inserts a new page at a specific index. Overrides wxBookCtrlBase::InsertPage.
    // [UNITY] Inserts a VisualElement into the content area and its navigation item into the navigation bar.
    virtual bool InsertPage(size_t n, wxWindow* page, const wxString& text, bool bSelect = false, int imageId = NO_IMAGE) override;

    // [INTENT] Inserts a new page with bitmap names.
    // [UNITY] Similar to above, but also handles image assignments for the navigation item.
    bool InsertPage(size_t             n,
                    wxWindow*          page,
                    const wxString&    text,
                    const std::string& bmp_name          = "",
                    const std::string& inactive_bmp_name = "",
                    bool               bSelect           = false);

    // [INTENT] Sets the currently active page. Overrides wxBookCtrlBase::SetSelection.
    // [EVENT] Triggers a selection change.
    // [UNITY] Changes the visibility of VisualElement pages and updates the visual state of the corresponding navigation item.
    virtual int SetSelection(size_t n) override;

    // [INTENT] Changes the selection to a new page. Overrides wxBookCtrlBase::ChangeSelection.
    // [UNITY] Similar to SetSelection, but may have different internal event handling.
    virtual int ChangeSelection(size_t n) override;

    // [INTENT] Sets the text for a specific page's tab. Overrides wxBookCtrlBase::SetPageText.
    // [UNITY] Updates the `text` property of the corresponding navigation item's Label or Button.
    virtual bool SetPageText(size_t n, const wxString& strText) override;

    // [INTENT] Gets the text for a specific page's tab. Overrides wxBookCtrlBase::GetPageText.
    // [UNITY] Retrieves the `text` property from the corresponding navigation item.
    virtual wxString GetPageText(size_t n) const override;

    // [INTENT] Sets the image for a specific page's tab. Overrides wxBookCtrlBase::SetPageImage.
    // [UNITY] Updates the `background-image` style property of the corresponding navigation item.
    virtual bool SetPageImage(size_t WXUNUSED(n), int WXUNUSED(imageId)) override;

    // [INTENT] Gets the image ID for a specific page's tab. Overrides wxBookCtrlBase::GetPageImage.
    // [UNITY] Retrieves image information from the corresponding navigation item.
    virtual int GetPageImage(size_t WXUNUSED(n)) const override;

    // [INTENT] Sets the image for a specific page's tab using a bitmap name.
    // [UNITY] Updates the `background-image` style property of the corresponding navigation item.
    bool SetPageImage(size_t n, const std::string& bmp_name);

    // Override some wxWindow methods too.
    // [INTENT] Sets focus to the currently selected page. Overrides wxWindow::SetFocus.
    // [UNITY] Sets focus to the active VisualElement page.
    virtual void SetFocus() override;

    // [INTENT] Helper to get the internal ButtonsListCtrl.
    // [UNITY] Directly access the child VisualElement for the navigation bar.
    ButtonsListCtrl* GetBtnsListCtrl() const { return static_cast<ButtonsListCtrl*>(m_bookctrl); }

    // [INTENT] Propagates UpdateMode call to internal ButtonsListCtrl.
    void UpdateMode();

    // [INTENT] Propagates Rescale call to internal ButtonsListCtrl.
    void Rescale();

    // [INTENT] Handles keyboard navigation events for changing pages.
    // [EVENT] Binds to wxEVT_NAVIGATION_KEY.
    // [UNITY] Handles keyboard input events and updates the active page/tab.
    // [PORTING_HAZARD:P2] Complex keyboard navigation logic, particularly focus management across nested controls,
    // requires careful porting to Unity UI Toolkit's event and focus system.
    void OnNavigationKey(wxNavigationKeyEvent& event);

protected:
    // [INTENT] Overridden to avoid assert in base class, no action needed here.
    virtual void UpdateSelectedPage(size_t WXUNUSED(newsel)) override;

    // [INTENT] Creates a wxBookCtrlEvent for page changing.
    // [UNITY] Creates a custom C# event or uses Unity's standard event system for page change notifications.
    virtual wxBookCtrlEvent* CreatePageChangingEvent() const override;

    // [INTENT] Sets the event type to page changed.
    // [UNITY] Sets the event type for the C# event.
    virtual void MakeChangedEvent(wxBookCtrlEvent& event) override;

    // [INTENT] Removes a page from the control. Overrides wxBookCtrlBase::DoRemovePage.
    // [UNITY] Removes the VisualElement page and its corresponding navigation item.
    virtual wxWindow* DoRemovePage(size_t page) override;

    // [INTENT] Resizes the current page to fit the control. Overrides wxWindow::DoSize.
    // [UNITY] UI Toolkit's layout system handles sizing of VisualElements automatically.
    virtual void DoSize() override;

    // [INTENT] Shows or hides a page with visual effects. Overrides wxBookCtrlBase::DoShowPage.
    // [UNITY] Sets the `display` style property of a VisualElement to `DisplayStyle.Flex` or `DisplayStyle.None`,
    // possibly with UI Toolkit animations.
    virtual void DoShowPage(wxWindow* page, bool show) override;

private:
    // [INTENT] Internal initialization method.
    void Init();

    // [STATE] Visual effects for showing/hiding pages.
    wxShowEffect m_showEffect, m_hideEffect;

    // [STATE] Timouts for visual effects.
    unsigned m_showTimeout, m_hideTimeout;
};
// #endif // _WIN32
#endif // slic3r_Notebook_hpp_
