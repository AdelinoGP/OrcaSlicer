#include "Tabbook.hpp"

// #ifdef _WIN32

#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "TabButton.hpp"

// BBS set font size
#include "Widgets/Label.hpp"

#include <wx/button.h>
#include <wx/sizer.h>

wxDEFINE_EVENT(wxCUSTOMEVT_TABBOOK_SEL_CHANGED, wxCommandEvent);

const static wxColour TAB_BUTTON_BG  = wxColour("#FEFFFF");
const static wxColour TAB_BUTTON_SEL = wxColour("#BFE1DE"); // ORCA

static const wxFont& TAB_BUTTON_FONT     = Label::Body_14;
static const wxFont& TAB_BUTTON_FONT_SEL = Label::Head_14;

static const int BUTTON_DEF_HEIGHT = 46;
static const int BUTTON_DEF_WIDTH  = 220;

// [INTENT] This control builds the left-side tab strip for a notebook-like page switcher.
// [STATE] It reuses a caller-owned sizer subtree, tracks the selected page index, and keeps
//         per-button text/icon/footer state so the sidebar can be refreshed without rebuilding.
// [UNITY] Map this to a vertical tab rail (ScrollRect/ListView or a button column) with a shared
//         selection model and reusable button prefabs rather than reparenting live widgets.
TabButtonsListCtrl::TabButtonsListCtrl(wxWindow* parent, wxBoxSizer* side_tools)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    SetBackgroundColour(TAB_BUTTON_BG);

    int em = em_unit(this);
    // BBS: no gap
    m_btn_margin  = 0;
    m_line_margin = std::lround(0.1 * em);

    m_arrow_img = ScalableBitmap(this, "monitor_arrow", 14);

    m_sizer = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(m_sizer);
    if (side_tools != NULL) {
        for (size_t idx = 0; idx < side_tools->GetItemCount(); idx++) {
            wxSizerItem* item     = side_tools->GetItem(idx);
            wxWindow*    item_win = item->GetWindow();
            if (item_win) {
                item_win->Reparent(this);
            }
        }
        m_sizer->Add(side_tools, 0, wxEXPAND | wxLEFT | wxTOP, m_btn_margin);
    }

    m_buttons_sizer = new wxFlexGridSizer(1, m_btn_margin, m_btn_margin);
    m_sizer->Add(m_buttons_sizer, 0, wxLEFT | wxTOP, m_btn_margin);
    m_sizer->AddStretchSpacer(1);
}

// [OPENGL] The custom paint pass is only sidebar chrome: it redraws the selected-page background
//          strip and the bottom separator line around wx child controls rather than painting the
//          page content itself.
// [PORTING_HAZARD:P2] The painting logic assumes the selection index is valid and the buttons are
//                     already laid out; a Unity port should keep the highlight in the retained view
//                     model so invalid indices cannot paint stale children.
void TabButtonsListCtrl::OnPaint(wxPaintEvent&)
{
    Slic3r::GUI::wxGetApp().UpdateDarkUI(this);
    const wxSize sz = GetSize();
    wxPaintDC    dc(this);

    if (m_selection < 0 || m_selection >= (int) m_pageButtons.size())
        return;

    const wxColour& btn_marker_color = Slic3r::GUI::wxGetApp().get_color_hovered_btn_label();

    // highlight selected notebook button

    for (int idx = 0; idx < int(m_pageButtons.size()); idx++) {
        TabButton* btn = m_pageButtons[idx];
        btn->SetBackgroundColor(idx == m_selection ? TAB_BUTTON_SEL : TAB_BUTTON_BG);

        wxPoint         pos  = btn->GetPosition();
        wxSize          size = btn->GetSize();
        const wxColour& clr  = StateColor::darkModeColorFor(idx == m_selection ? btn_marker_color : TAB_BUTTON_BG);
        dc.SetPen(clr);
        dc.SetBrush(clr);
        dc.DrawRectangle(pos.x, pos.y + size.y, size.x, sz.y - size.y);
    }
    dc.SetPen(btn_marker_color);
    dc.SetBrush(btn_marker_color);
    dc.DrawRectangle(1, sz.y - m_line_margin, sz.x, m_line_margin);
}

// [STATE] Rescaling rebuilds the arrow bitmap and propagates the new min size / font / bitmap to
//         every page button, so DPI changes are a full sidebar relayout rather than a skin swap.
// [UNITY] Use a DPI-aware layout pass plus icon atlas refresh; do not depend on per-widget manual
//         resizing of live child controls.
void TabButtonsListCtrl::Rescale()
{
    m_arrow_img = ScalableBitmap(this, "monitor_arrow", 14);

    int em = em_unit(this);
    for (TabButton* btn : m_pageButtons) {
        btn->SetMinSize({BUTTON_DEF_WIDTH * em / 10, BUTTON_DEF_HEIGHT * em / 10});
        btn->SetBitmap(m_arrow_img);
        btn->Rescale();
    }

    m_sizer->Layout();
}

void TabButtonsListCtrl::SetSelection(int sel)
{
    // [PORTING_HAZARD:P2] This is a strict index swap: the old and new selection must both exist,
    //                     otherwise the direct vector access will dereference invalid page buttons.
    if (m_selection == sel)
        return;
    if (m_selection >= 0) {
        m_pageButtons[m_selection]->SetBackgroundColor(TAB_BUTTON_BG);
        m_pageButtons[m_selection]->SetFont(TAB_BUTTON_FONT);
    }
    m_selection = sel;
    m_pageButtons[m_selection]->SetBackgroundColor(TAB_BUTTON_SEL);
    m_pageButtons[m_selection]->SetFont(TAB_BUTTON_FONT_SEL);
    Refresh();
}

// [STATE] The "new" badge is per-tab transient state, so this helper only toggles the visual flag
//         and repaints when the badge state actually changes.
// [UNITY] Keep this as a badge/notification marker on the tab item itself rather than baking it
//         into the selection controller.
void TabButtonsListCtrl::showNewTag(int sel, bool tag)
{
    if (m_pageButtons[sel]->GetShowNewTag() == tag) {
        return;
    }

    m_pageButtons[sel]->ShowNewTag(tag);
    Refresh();
}

// [EVENT] Button clicks are translated into a custom wx command event for the parent notebook
//         owner, keeping tab selection and page swapping outside the button control itself.
// [UNITY] Replace this with a button callback that writes into a shared selection controller and
//         raises a page-change event on the container, not a child-widget rebind.
bool TabButtonsListCtrl::InsertPage(size_t n, const wxString& text, bool bSelect /* = false*/, const std::string& bmp_name /* = ""*/)
{
    TabButton* btn = new TabButton(this, text, m_arrow_img, wxNO_BORDER);
    btn->SetCornerRadius(0);

    int em = em_unit(this);
    btn->SetMinSize({BUTTON_DEF_WIDTH * em / 10, BUTTON_DEF_HEIGHT * em / 10});

    btn->SetBackgroundColor(TAB_BUTTON_BG);
    btn->SetTextColor(*wxBLACK);
    btn->Bind(wxEVT_BUTTON, [this, btn](wxCommandEvent& event) {
        if (auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn); it != m_pageButtons.end()) {
            auto sel = it - m_pageButtons.begin();
            SetSelection(sel);
            wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_TABBOOK_SEL_CHANGED);
            evt.SetId(sel);
            wxPostEvent(this->GetParent(), evt);
        }
    });
    Slic3r::GUI::wxGetApp().UpdateDarkUI(btn);
    m_pageButtons.insert(m_pageButtons.begin() + n, btn);
    m_buttons_sizer->Insert(n, new wxSizerItem(btn));
    m_buttons_sizer->SetRows(m_pageButtons.size() + 1);
    m_sizer->Layout();
    return true;
}

// [STATE] Removing a page tears down the corresponding button and reflows the sizer, so the tab
//         rail owns the live button lifetime for every inserted page.
void TabButtonsListCtrl::RemovePage(size_t n)
{
    if (n >= m_pageButtons.size())
        return;
    TabButton* btn = m_pageButtons[n];
    m_pageButtons.erase(m_pageButtons.begin() + n);
    m_buttons_sizer->Remove(n);
    btn->Reparent(nullptr);
    btn->Destroy();
    m_sizer->Layout();
}

// [STATE] Page icons are optional and can be swapped independently from the button text, which is
//         why the sidebar keeps a per-tab bitmap slot even when the icon is cleared.
bool TabButtonsListCtrl::SetPageImage(size_t n, const std::string& bmp_name)
{
    if (n >= m_pageButtons.size())
        return false;

    ScalableBitmap bitmap;
    if (!bmp_name.empty())
        bitmap = ScalableBitmap(this, bmp_name, 14);
    m_pageButtons[n]->SetBitmap(bitmap);

    return true;
}

// [STATE] Text, padding, and footer rows are all mutable at runtime because the sidebar mirrors
//         notebook content that can be localized or context-driven.
void TabButtonsListCtrl::SetPageText(size_t n, const wxString& strText)
{
    TabButton* btn = m_pageButtons[n];
    btn->SetLabel(strText);
}

wxString TabButtonsListCtrl::GetPageText(size_t n) const
{
    TabButton* btn = m_pageButtons[n];
    return btn->GetLabel();
}

// [STATE] Padding is a shared layout primitive for all tab buttons, so the caller can keep the
//         rail visually aligned when page rows change size.
const wxSize& TabButtonsListCtrl::GetPaddingSize(size_t n) { return m_pageButtons[n]->GetPaddingSize(); }

// [STATE] Padding is a shared layout primitive for all tab buttons, so the caller can keep the
//         rail visually aligned when page rows change size.
void TabButtonsListCtrl::SetPaddingSize(const wxSize& size)
{
    for (auto& btn : m_pageButtons) {
        btn->SetPaddingSize(size);
    }
}

// [INTENT] The footer is a late-bound hint/status row for the sidebar rather than part of the tab
//          selection model.
// [UNITY] Model this as a separate footer Text element under the tab rail so it can be updated
//         independently of the selection list.
void TabButtonsListCtrl::SetFooterText(const wxString& text)
{
    if (!m_footer_text) {
        m_footer_text = new wxStaticText(this, wxID_ANY, text);
        m_footer_text->SetForegroundColour(wxColour(128, 128, 128));
        m_footer_text->SetFont(Label::Body_10);
        int em = em_unit(this);
        m_sizer->Add(m_footer_text, 0, wxALL, FromDIP(18)); // ORCA reduce / match left margin buttons on sidebars
    } else {
        m_footer_text->SetLabel(text);
    }
    m_sizer->Layout();
}

// #endif // _WIN32
