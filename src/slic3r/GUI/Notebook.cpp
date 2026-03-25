#include <wx/wx.h>       // [INTENT] General wxWidgets header for core functionalities.
#include <wx/notebook.h> // [INTENT] Required for wxNotebook or similar notebook-like controls.
#include <cmath>         // [INTENT] For std::lround.

// [INTENT] This file defines two GUI components: ButtonsListCtrl and Notebook.
// ButtonsListCtrl is a custom control that manages a list of buttons, typically used as tabs or page selectors within a notebook-like
// interface. Notebook is a custom wxNotebook-like control with specific initialization and styling.

#include "Notebook.hpp"
#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp" //BBS set font size

#include <wx/button.h>
#include <wx/sizer.h>

wxDEFINE_EVENT(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, wxCommandEvent);

// [INTENT] ButtonsListCtrl is a custom control that displays a list of buttons and manages their selection state.
// It is likely used as a tab bar for a custom notebook implementation.
// [UNITY] This component could be replicated in Unity using a UI Toolkit VisualElement with a Flexbox layout,
// where each button is a custom Button VisualElement. Selection state would be managed via USS classes.
ButtonsListCtrl::ButtonsListCtrl(wxWindow* parent, wxBoxSizer* side_tools)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL)
{
// [THREAD] This code runs on the UI thread.
#ifdef __WINDOWS__
    // [PLATFORM_SPECIFIC] Double buffering for Windows to reduce flicker.
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    // [STATE] Default background color for buttons, platform-specific.
    // [UNITY] These colors would be defined in Unity's USS (Unity Style Sheets) or ScriptableObjects for themes.
    wxColour default_btn_bg;
#ifdef __APPLE__
    default_btn_bg = wxColour("#3B4446"); // Gradient #414B4E
#else
    default_btn_bg = wxColour("#2D2D30"); // Gradient #414B4E
#endif

    SetBackgroundColour(default_btn_bg);

    // [STATE] `em` unit for scaling UI elements.
    // [UNITY] UI Toolkit uses `em` units, but the scaling factor might need adjustment.
    int em = em_unit(this); // Slic3r::GUI::wxGetApp().em_unit();
    // BBS: no gap
    m_btn_margin  = 0; // std::lround(0.3 * em);
    m_line_margin = std::lround(0.1 * em);

    // [INTENT] Sizer for layout management.
    // [UNITY] Flexbox layout in UI Toolkit would replace wxSizers.
    m_sizer = new wxBoxSizer(wxHORIZONTAL);
    this->SetSizer(m_sizer);

    m_buttons_sizer = new wxFlexGridSizer(1, m_btn_margin, m_btn_margin);
    m_sizer->Add(m_buttons_sizer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM, m_btn_margin);

    // [INTENT] Integrate external side tools into the control's layout.
    // [UNITY] This would involve parenting VisualElements to the appropriate layout container.
    if (side_tools != NULL) {
        m_sizer->AddStretchSpacer(1);
        for (size_t idx = 0; idx < side_tools->GetItemCount(); idx++) {
            wxSizerItem* item     = side_tools->GetItem(idx);
            wxWindow*    item_win = item->GetWindow();
            if (item_win) {
                // [PORTING_HAZARD:P2] Reparenting windows in Unity UI Toolkit would require careful management of the VisualElement hierarchy.
                item_win->Reparent(this);
            }
        }
        m_sizer->Add(side_tools, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, m_btn_margin);
    }

    // [EVENT] Custom paint events are disabled/commented out (BBS: disable custom paint).
    // [UNITY] Custom painting would be done by overriding `OnGenerateVisualContent` for VisualElements.
    // this->Bind(wxEVT_PAINT, &ButtonsListCtrl::OnPaint, this);
    // [EVENT] Event handler for system color changes.
    // [UNITY] Theme changes in Unity UI Toolkit are handled via USS and style classes.
    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](auto& e) {});
}

// [INTENT] Custom paint handler for drawing button highlights and the bottom line.
// This function is currently commented out in the constructor.
// [UNITY] If custom drawing is required, this logic would be reimplemented in Unity's UI Toolkit using custom VisualElements and their
// `OnGenerateVisualContent` method.
void ButtonsListCtrl::OnPaint(wxPaintEvent&)
{
    // Slic3r::GUI::wxGetApp().UpdateDarkUI(this);
    const wxSize sz = GetSize();
    wxPaintDC    dc(this);

    // [STATE] `m_selection` indicates the currently selected button.
    if (m_selection < 0 || m_selection >= (int) m_pageButtons.size())
        return;

    // [STATE] Colors for selected and default buttons.
    wxColour        selected_btn_bg("#1F8EEA");
    wxColour        default_btn_bg("#3B4446"); // Gradient #414B4E
    const wxColour& btn_marker_color = Slic3r::GUI::wxGetApp().get_color_hovered_btn_label();

    // highlight selected notebook button
    // [INTENT] Iterate through page buttons to apply appropriate background color based on selection.
    for (int idx = 0; idx < int(m_pageButtons.size()); idx++) {
        Button* btn = m_pageButtons[idx];

        // [EVENT] Setting background color, which might trigger a repaint of the button.
        btn->SetBackgroundColor(idx == m_selection ? selected_btn_bg : default_btn_bg);

        // [OPENGL] Direct drawing calls using wxPaintDC, setting pen and brush colors.
        // [UNITY] This drawing would be handled by USS for background colors and potentially custom drawing for markers.
        wxPoint         pos  = btn->GetPosition();
        wxSize          size = btn->GetSize();
        const wxColour& clr  = idx == m_selection ? btn_marker_color : default_btn_bg;
        dc.SetPen(clr);
        dc.SetBrush(clr);
        dc.DrawRectangle(pos.x, pos.y + size.y, size.x, sz.y - size.y);
    }

#if 0
    // highlight selected mode button (currently disabled)
    // [UNCLEAR] This block is conditionally compiled out. Its purpose is to highlight mode buttons.
    if (m_mode_sizer) {
        const std::vector<ModeButton*>& mode_btns = m_mode_sizer->get_btns();
        for (int idx = 0; idx < int(mode_btns.size()); idx++) {
            ModeButton* btn = mode_btns[idx];
            btn->SetBackgroundColor(btn->is_selected() ? selected_btn_bg : default_btn_bg);

            //wxPoint pos = btn->GetPosition();
            //wxSize size = btn->GetSize();
            //const wxColour& clr = btn->is_selected() ? btn_marker_color : default_btn_bg;
            //dc.SetPen(clr);
            //dc.SetBrush(clr);
            //dc.DrawRectangle(pos.x, pos.y + size.y, size.x, sz.y - size.y);
        }
    }
#endif

    // Draw orange bottom line
    // [OPENGL] Drawing a rectangle for the bottom line.
    // [UNITY] This would be a VisualElement with `border-bottom` styling.
    dc.SetPen(btn_marker_color);
    dc.SetBrush(btn_marker_color);
    dc.DrawRectangle(1, sz.y - m_line_margin, sz.x, m_line_margin);
}

// [INTENT] Updates the mode of an internal mode sizer, currently commented out.
// [UNCLEAR] The `m_mode_sizer` is commented out, indicating this functionality might be deprecated or unused.
void ButtonsListCtrl::UpdateMode()
{
    // m_mode_sizer->SetMode(Slic3r::GUI::wxGetApp().get_mode());
}

// [INTENT] Rescales the control and its contained buttons based on the current `em` unit.
// [UNITY] UI Toolkit's automatic layout and styling with `em` units would largely handle this,
// but specific size adjustments might still be needed for custom components.
void ButtonsListCtrl::Rescale()
{
    // m_mode_sizer->msw_rescale();
    int em = em_unit(this);
    for (Button* btn : m_pageButtons) {
        // BBS
        //  [INTENT] Set minimum size for buttons based on whether they have text.
        btn->SetMinSize({(btn->GetLabel().empty() ? 40 : 132) * em / 10, 36 * em / 10});
        btn->Rescale(); // [EVENT] Recursively rescale the button itself.
    }

    // BBS: no gap (commented out code for margin adjustments)
    // m_btn_margin = std::lround(0.3 * em);
    // m_line_margin = std::lround(0.1 * em);
    // m_buttons_sizer->SetVGap(m_btn_margin);
    // m_buttons_sizer->SetHGap(m_btn_margin);

    m_sizer->Layout(); // [EVENT] Trigger layout recalculation.
}

// [INTENT] Sets the currently selected button and updates its visual appearance.
// [STATE] `m_selection` tracks the index of the selected button.
// [UNITY] This would involve adding/removing USS classes for selected state and updating relevant VisualElement properties.
void ButtonsListCtrl::SetSelection(int sel)
{
    if (m_selection == sel)
        return;
    // BBS: change button color
    wxColour selected_btn_bg("#009688"); // Gradient #009688
    if (m_selection >= 0) {
        // [STATE] StateColor object manages colors for different button states (hovered, normal).
        // [UNITY] This logic would be encapsulated within USS for hover and normal states or managed by a custom MonoBehaviour.
        StateColor bg_color = StateColor(std::pair{wxColour(107, 107, 107), (int) StateColor::Hovered},
                                         std::pair{wxColour(59, 68, 70), (int) StateColor::Normal});
        m_pageButtons[m_selection]->SetBackgroundColor(bg_color);
        StateColor text_color = StateColor(std::pair{wxColour(254, 254, 254), (int) StateColor::Normal});
        m_pageButtons[m_selection]->SetSelected(false);
        m_pageButtons[m_selection]->SetTextColor(text_color);
    }
    m_selection = sel;

    // [STATE] Applying new background and text colors for the newly selected button.
    StateColor bg_color = StateColor(std::pair{wxColour(0, 150, 136), (int) StateColor::Hovered},
                                     std::pair{wxColour(0, 150, 136), (int) StateColor::Normal});
    m_pageButtons[m_selection]->SetBackgroundColor(bg_color);

    StateColor text_color = StateColor(std::pair{wxColour(254, 254, 254), (int) StateColor::Normal});
    m_pageButtons[m_selection]->SetSelected(true);
    m_pageButtons[m_selection]->SetTextColor(text_color);

    Refresh(); // [EVENT] Request a repaint of the control.
}

// [INTENT] Inserts a new page (button) into the list at a specified position.
// [UNITY] This would involve creating a new Button VisualElement, setting its properties,
// and adding it to the parent layout VisualElement. Event binding would use UIElements callbacks.
bool ButtonsListCtrl::InsertPage(
    size_t n, const wxString& text, bool bSelect /* = false*/, const std::string& bmp_name /* = ""*/, const std::string& inactive_bmp_name)
{
    // [INTENT] Create a new button instance.
    Button* btn = new Button(this, text.empty() ? text : " " + text, bmp_name, wxNO_BORDER);
    btn->SetCornerRadius(0);

    int em = em_unit(this);
    // BBS set size for button
    btn->SetMinSize({(text.empty() ? 40 : 136) * em / 10, 36 * em / 10});

    // [STATE] Default background and text colors for the new button.
    StateColor bg_color = StateColor(std::pair{wxColour(107, 107, 107), (int) StateColor::Hovered},
                                     std::pair{wxColour(59, 68, 70), (int) StateColor::Normal});

    btn->SetBackgroundColor(bg_color);
    StateColor text_color = StateColor(std::pair{wxColour(254, 254, 254), (int) StateColor::Normal});
    btn->SetTextColor(text_color);
    btn->SetInactiveIcon(inactive_bmp_name);
    btn->SetSelected(false);
    // [EVENT] Bind a click event handler to the button.
    // [UNITY] This would be `btn.clicked += () => { ... };` using Unity's UI Toolkit events.
    btn->Bind(wxEVT_BUTTON, [this, btn](wxCommandEvent& event) {
        if (auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn); it != m_pageButtons.end()) {
            auto sel = it - m_pageButtons.begin();
            // do it later
            // SetSelection(sel); // [INTENT] Original intention to set selection immediately, but commented out.

            // [EVENT] Post a custom event to the parent when a button is clicked.
            // [UNITY] This would be a custom event or a direct method call on a controller MonoBehaviour.
            wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED);
            evt.SetId(sel);
            wxPostEvent(this->GetParent(), evt);
        }
    });
    // [EVENT] Update button UI based on dark mode settings.
    Slic3r::GUI::wxGetApp().UpdateDarkUI(btn);
    // [STATE] Add the new button to the internal list of page buttons.
    m_pageButtons.insert(m_pageButtons.begin() + n, btn);
    // [INTENT] Add the button to the sizer for layout.
    m_buttons_sizer->Insert(n, new wxSizerItem(btn));
    m_buttons_sizer->SetCols(m_buttons_sizer->GetCols() + 1);
    m_sizer->Layout(); // [EVENT] Trigger layout recalculation.
    return true;
}

// [INTENT] Removes a page (button) from the list at a specified position.
// [UNITY] This would involve removing the corresponding VisualElement from its parent and disposing of it.
void ButtonsListCtrl::RemovePage(size_t n)
{
    Button* btn = m_pageButtons[n];
    m_pageButtons.erase(m_pageButtons.begin() + n); // [STATE] Remove from internal list.
    m_buttons_sizer->Remove(n);                     // [INTENT] Remove from layout sizer.
#if __WXOSX__
    // [PLATFORM_SPECIFIC] Different cleanup for macOS.
    // [PORTING_HAZARD:P3] Platform-specific UI element destruction/reparenting might need careful Unity handling.
    RemoveChild(btn);
#else
    btn->Reparent(nullptr); // [INTENT] Detach from current parent.
#endif
    btn->Destroy();    // [INTENT] Destroy the wxWidgets control.
    m_sizer->Layout(); // [EVENT] Trigger layout recalculation.
}

// [INTENT] Sets the image for a specific page button.
// [UNCLEAR] The actual bitmap setting `m_pageButtons[n]->SetBitmap_(bitmap);` is commented out.
// [UNITY] This would involve updating the `backgroundImage` style property of the Button VisualElement.
bool ButtonsListCtrl::SetPageImage(size_t n, const std::string& bmp_name) const
{
    if (n >= m_pageButtons.size())
        return false;

    // BBS
    // return m_pageButtons[n]->SetBitmap_(bmp_name);
    // [STATE] ScalableBitmap for handling different DPIs.
    // [UNITY] Unity UI Toolkit handles scalable images natively.
    ScalableBitmap bitmap(NULL, bmp_name);
    // m_pageButtons[n]->SetBitmap_(bitmap);
    return true;
}

// [INTENT] Sets the text label for a specific page button.
// [UNITY] This would involve updating the `text` property of the Button VisualElement.
void ButtonsListCtrl::SetPageText(size_t n, const wxString& strText)
{
    Button* btn = m_pageButtons[n];
    btn->SetLabel(strText);
}

// [INTENT] Retrieves the text label from a specific page button.
// [UNITY] This would involve reading the `text` property of the Button VisualElement.
wxString ButtonsListCtrl::GetPageText(size_t n) const
{
    Button* btn = m_pageButtons[n];
    return btn->GetLabel();
}

// #endif // _WIN32 (Original file structure suggests this section might have been conditionally compiled)

// [INTENT] Notebook class represents a custom notebook control, possibly a simplified version of wxNotebook.
// Its primary purpose is to manage multiple pages and their display.
// [UNITY] This could be mapped to a UI Toolkit TabView or a custom VisualElement that manages child VisualElements (pages) and their visibility.
void Notebook::Init()
{
    // [INTENT] Set internal border to zero as there's no visible separation needed.
    SetInternalBorder(0);

    // [STATE] No effects (show/hide) by default for page transitions.
    // [UNITY] Page transition effects would be handled via UI Toolkit animations or custom USS transitions.
    m_showEffect = m_hideEffect = wxSHOW_EFFECT_NONE;
    m_showTimeout = m_hideTimeout = 0;

    /* On Linux, Gstreamer wxMediaCtrl does not seem to get along well with
     * 32-bit X11 visuals (the overlay does not work).  Is this a wxWindows
     * bug?  Is this a Gstreamer bug?  No idea, but it is our problem ...
     * and anyway, this transparency thing just isn't all that interesting,
     * so we just don't do it on Linux.
     */
// [PLATFORM_SPECIFIC] Conditional compilation for GTK (Linux) related to media control transparency.
// [PORTING_HAZARD:P1] Media playback and transparency issues on specific platforms represent a critical porting hazard.
// This would need careful re-evaluation of media playback components in Unity for Linux.
#ifndef __WXGTK__
    // [INTENT] Enable transparent background style if not on GTK.
    SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif
}
}
m_sizer->Add(side_tools, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, m_btn_margin);
}

// BBS: disable custom paint
// this->Bind(wxEVT_PAINT, &ButtonsListCtrl::OnPaint, this);
Bind(wxEVT_SYS_COLOUR_CHANGED, [this](auto& e) {});
}

void ButtonsListCtrl::OnPaint(wxPaintEvent&)
{
    // Slic3r::GUI::wxGetApp().UpdateDarkUI(this);
    const wxSize sz = GetSize();
    wxPaintDC    dc(this);

    if (m_selection < 0 || m_selection >= (int) m_pageButtons.size())
        return;

    wxColour        selected_btn_bg("#1F8EEA");
    wxColour        default_btn_bg("#3B4446"); // Gradient #414B4E
    const wxColour& btn_marker_color = Slic3r::GUI::wxGetApp().get_color_hovered_btn_label();

    // highlight selected notebook button

    for (int idx = 0; idx < int(m_pageButtons.size()); idx++) {
        Button* btn = m_pageButtons[idx];

        btn->SetBackgroundColor(idx == m_selection ? selected_btn_bg : default_btn_bg);

        wxPoint         pos  = btn->GetPosition();
        wxSize          size = btn->GetSize();
        const wxColour& clr  = idx == m_selection ? btn_marker_color : default_btn_bg;
        dc.SetPen(clr);
        dc.SetBrush(clr);
        dc.DrawRectangle(pos.x, pos.y + size.y, size.x, sz.y - size.y);
    }

#if 0
    // highlight selected mode button
    if (m_mode_sizer) {
        const std::vector<ModeButton*>& mode_btns = m_mode_sizer->get_btns();
        for (int idx = 0; idx < int(mode_btns.size()); idx++) {
            ModeButton* btn = mode_btns[idx];
            btn->SetBackgroundColor(btn->is_selected() ? selected_btn_bg : default_btn_bg);

            //wxPoint pos = btn->GetPosition();
            //wxSize size = btn->GetSize();
            //const wxColour& clr = btn->is_selected() ? btn_marker_color : default_btn_bg;
            //dc.SetPen(clr);
            //dc.SetBrush(clr);
            //dc.DrawRectangle(pos.x, pos.y + size.y, size.x, sz.y - size.y);
        }
    }
#endif

    // Draw orange bottom line

    dc.SetPen(btn_marker_color);
    dc.SetBrush(btn_marker_color);
    dc.DrawRectangle(1, sz.y - m_line_margin, sz.x, m_line_margin);
}

void ButtonsListCtrl::UpdateMode()
{
    // m_mode_sizer->SetMode(Slic3r::GUI::wxGetApp().get_mode());
}

void ButtonsListCtrl::Rescale()
{
    // m_mode_sizer->msw_rescale();
    int em = em_unit(this);
    for (Button* btn : m_pageButtons) {
        // BBS
        btn->SetMinSize({(btn->GetLabel().empty() ? 40 : 132) * em / 10, 36 * em / 10});
        btn->Rescale();
    }

    // BBS: no gap
    // m_btn_margin = std::lround(0.3 * em);
    // m_line_margin = std::lround(0.1 * em);
    // m_buttons_sizer->SetVGap(m_btn_margin);
    // m_buttons_sizer->SetHGap(m_btn_margin);

    m_sizer->Layout();
}

void ButtonsListCtrl::SetSelection(int sel)
{
    if (m_selection == sel)
        return;
    // BBS: change button color
    wxColour selected_btn_bg("#009688"); // Gradient #009688
    if (m_selection >= 0) {
        StateColor bg_color = StateColor(std::pair{wxColour(107, 107, 107), (int) StateColor::Hovered},
                                         std::pair{wxColour(59, 68, 70), (int) StateColor::Normal});
        m_pageButtons[m_selection]->SetBackgroundColor(bg_color);
        StateColor text_color = StateColor(std::pair{wxColour(254, 254, 254), (int) StateColor::Normal});
        m_pageButtons[m_selection]->SetSelected(false);
        m_pageButtons[m_selection]->SetTextColor(text_color);
    }
    m_selection = sel;

    StateColor bg_color = StateColor(std::pair{wxColour(0, 150, 136), (int) StateColor::Hovered},
                                     std::pair{wxColour(0, 150, 136), (int) StateColor::Normal});
    m_pageButtons[m_selection]->SetBackgroundColor(bg_color);

    StateColor text_color = StateColor(std::pair{wxColour(254, 254, 254), (int) StateColor::Normal});
    m_pageButtons[m_selection]->SetSelected(true);
    m_pageButtons[m_selection]->SetTextColor(text_color);

    Refresh();
}

bool ButtonsListCtrl::InsertPage(
    size_t n, const wxString& text, bool bSelect /* = false*/, const std::string& bmp_name /* = ""*/, const std::string& inactive_bmp_name)
{
    Button* btn = new Button(this, text.empty() ? text : " " + text, bmp_name, wxNO_BORDER);
    btn->SetCornerRadius(0);

    int em = em_unit(this);
    // BBS set size for button
    btn->SetMinSize({(text.empty() ? 40 : 136) * em / 10, 36 * em / 10});

    StateColor bg_color = StateColor(std::pair{wxColour(107, 107, 107), (int) StateColor::Hovered},
                                     std::pair{wxColour(59, 68, 70), (int) StateColor::Normal});

    btn->SetBackgroundColor(bg_color);
    StateColor text_color = StateColor(std::pair{wxColour(254, 254, 254), (int) StateColor::Normal});
    btn->SetTextColor(text_color);
    btn->SetInactiveIcon(inactive_bmp_name);
    btn->SetSelected(false);
    btn->Bind(wxEVT_BUTTON, [this, btn](wxCommandEvent& event) {
        if (auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn); it != m_pageButtons.end()) {
            auto sel = it - m_pageButtons.begin();
            // do it later
            // SetSelection(sel);

            wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED);
            evt.SetId(sel);
            wxPostEvent(this->GetParent(), evt);
        }
    });
    Slic3r::GUI::wxGetApp().UpdateDarkUI(btn);
    m_pageButtons.insert(m_pageButtons.begin() + n, btn);
    m_buttons_sizer->Insert(n, new wxSizerItem(btn));
    m_buttons_sizer->SetCols(m_buttons_sizer->GetCols() + 1);
    m_sizer->Layout();
    return true;
}

void ButtonsListCtrl::RemovePage(size_t n)
{
    Button* btn = m_pageButtons[n];
    m_pageButtons.erase(m_pageButtons.begin() + n);
    m_buttons_sizer->Remove(n);
#if __WXOSX__
    RemoveChild(btn);
#else
    btn->Reparent(nullptr);
#endif
    btn->Destroy();
    m_sizer->Layout();
}

bool ButtonsListCtrl::SetPageImage(size_t n, const std::string& bmp_name) const
{
    if (n >= m_pageButtons.size())
        return false;

    // BBS
    // return m_pageButtons[n]->SetBitmap_(bmp_name);
    ScalableBitmap bitmap(NULL, bmp_name);
    // m_pageButtons[n]->SetBitmap_(bitmap);
    return true;
}

void ButtonsListCtrl::SetPageText(size_t n, const wxString& strText)
{
    Button* btn = m_pageButtons[n];
    btn->SetLabel(strText);
}

wxString ButtonsListCtrl::GetPageText(size_t n) const
{
    Button* btn = m_pageButtons[n];
    return btn->GetLabel();
}

// #endif // _WIN32

void Notebook::Init()
{
    // We don't need any border as we don't have anything to separate the
    // page contents from.
    SetInternalBorder(0);

    // No effects by default.
    m_showEffect = m_hideEffect = wxSHOW_EFFECT_NONE;

    m_showTimeout = m_hideTimeout = 0;

    /* On Linux, Gstreamer wxMediaCtrl does not seem to get along well with
     * 32-bit X11 visuals (the overlay does not work).  Is this a wxWindows
     * bug?  Is this a Gstreamer bug?  No idea, but it is our problem ...
     * and anyway, this transparency thing just isn't all that interesting,
     * so we just don't do it on Linux.
     */
#ifndef __WXGTK__
    SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif
}
