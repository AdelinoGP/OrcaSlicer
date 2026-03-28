#ifndef slic3r_Tabbook_hpp_
#define slic3r_Tabbook_hpp_

// #ifdef _WIN32

#include <wx/bookctrl.h>
#include <wx/sizer.h>
#include "wxExtensions.hpp"

class ScalableButton;
class TabButton;

// custom message the ButtonsListCtrl sends to its parent (Notebook) to notify a selection change:
wxDECLARE_EVENT(wxCUSTOMEVT_TABBOOK_SEL_CHANGED, wxCommandEvent);

// [INTENT] TabButtonsListCtrl owns the chrome for the notebook's tab rail: it holds the live
//          page buttons, forwards selection changes to the parent, and keeps footer/status text
//          outside the page content area.
// [STATE] The control stores the selected index, button sizing, optional footer label, and a
//         caller-owned side-tools sizer that gets reparented into this container.
// [UNITY] Port as a retained vertical tab rail with reusable button items, a separate footer text
//         element, and explicit selection callbacks into the page host.
class TabButtonsListCtrl : public wxControl
{
public:
    // BBS
    TabButtonsListCtrl(wxWindow* parent, wxBoxSizer* side_tools = NULL);
    ~TabButtonsListCtrl() {}

    void OnPaint(wxPaintEvent&);
    // [EVENT] Selection is visual-only here; the actual page swap is deferred to the parent via
    //         wxCUSTOMEVT_TABBOOK_SEL_CHANGED so the container can keep notebook state coherent.
    // [PORTING_HAZARD:P2] The method assumes sel names a real button; Unity should guard the index
    //                     before updating the rail highlight.
    void SetSelection(int sel);
    void showNewTag(int sel, bool show = false);
    // [STATE] DPI/theme changes rebuild the shared arrow bitmap and propagate size/font updates to
    //         every button, so the rail relayouts as one unit instead of per-child manually.
    void Rescale();
    // [EVENT] Insertion creates a live TabButton, binds its click event, and posts the custom page
    //         change event back to the notebook owner.
    // [UNITY] This becomes a pooled tab-item prefab with an onClick callback into the host view-model.
    bool InsertPage(size_t n, const wxString& text, bool bSelect = false, const std::string& bmp_name = "");
    // [STATE] Removing a page destroys the corresponding button and reflows the rail, so the rail
    //         owns the live lifetime of inserted page buttons.
    void RemovePage(size_t n);
    // [STATE] Icons are optional and can be swapped independently from the page label.
    bool SetPageImage(size_t n, const std::string& bmp_name);
    // [STATE] Labels remain mutable even though the rail is icon-first, because the notebook stores
    //         user-visible text for localization and context hints.
    void          SetPageText(size_t n, const wxString& strText);
    wxString      GetPageText(size_t n) const;
    const wxSize& GetPaddingSize(size_t n);
    void          SetPaddingSize(const wxSize& size);
    // [INTENT] Footer text is a late-bound hint/status row, not part of selection state.
    void SetFooterText(const wxString& text);
    // [UNCLEAR] This looks like legacy public state for a single button pointer; the header does
    //           not use it directly, so the Unity port should verify whether any callers still
    //           depend on this alias before preserving it.
    TabButton* pageButton;

private:
    wxWindow*               m_parent;
    wxFlexGridSizer*        m_buttons_sizer;
    wxBoxSizer*             m_sizer;
    ScalableBitmap          m_arrow_img;
    std::vector<TabButton*> m_pageButtons;
    int                     m_selection{-1};
    int                     m_btn_margin;
    int                     m_line_margin;
    wxStaticText*           m_footer_text{nullptr};
};

class Tabbook : public wxBookCtrlBase
{
public:
    // [INTENT] Tabbook is a notebook variant that routes page selection through the custom tab rail
    //          and keeps page visibility/focus aligned with the selected rail item.
    // [STATE] It owns the side control rail, show/hide transition settings, and the current page
    //         mapping used by the base notebook implementation.
    // [UNITY] Port as a page-host controller with a separate tab-rail view and explicit page swap
    //         events rather than a single monolithic notebook widget.
    Tabbook(wxWindow*      parent,
            wxWindowID     winid = wxID_ANY,
            const wxPoint& pos   = wxDefaultPosition,
            const wxSize&  size  = wxDefaultSize,
            // BBS
            wxBoxSizer* side_tools = NULL,
            long        style      = 0)
    {
        Init();
        Create(parent, winid, pos, size, side_tools, style);
    }

    bool Create(wxWindow*      parent,
                wxWindowID     winid = wxID_ANY,
                const wxPoint& pos   = wxDefaultPosition,
                const wxSize&  size  = wxDefaultSize,
                // BBS
                wxBoxSizer* side_tools = NULL,
                long        style      = 0)
    {
        if (!wxBookCtrlBase::Create(parent, winid, pos, size, style))
            return false;

        m_bookctrl = new TabButtonsListCtrl(this, side_tools);

        wxSizer* mainSizer = new wxBoxSizer(IsVertical() ? wxVERTICAL : wxHORIZONTAL);

        if (style & wxBK_RIGHT || style & wxBK_BOTTOM)
            mainSizer->Add(0, 0, 1, wxEXPAND, 0);

        m_controlSizer = new wxBoxSizer(IsVertical() ? wxHORIZONTAL : wxVERTICAL);
        m_controlSizer->Add(m_bookctrl, wxSizerFlags(0).Expand());
        wxSizerFlags flags;
        if (IsVertical())
            flags.Expand();
        else
            flags.Expand();
        mainSizer->Add(m_controlSizer, flags.Border(wxALL, m_controlMargin));
        SetSizer(mainSizer);

        // [EVENT] The rail posts a custom selection event; the notebook listens here so page
        //         selection stays synchronized with the rail highlight.
        this->Bind(wxCUSTOMEVT_TABBOOK_SEL_CHANGED, [this](wxCommandEvent& evt) {
            if (int page_idx = evt.GetId(); page_idx >= 0)
                SetSelection(page_idx);
        });

        this->Bind(wxEVT_NAVIGATION_KEY, &Tabbook::OnNavigationKey, this);

        return true;
    }

    // Methods specific to this class.

    // A method allowing to add a new page without any label (which is unused
    // by this control) and show it immediately.
    // [INTENT] Convenience wrapper for adding a page without a meaningful tab label; the rail uses
    //          the page content and image state instead of the textual label.
    bool ShowNewPage(wxWindow* page) { return AddPage(page, wxString(), "" /*true */ /* select it */); }

    // Set effect to use for showing/hiding pages.
    // [STATE] Page transitions are skinnable through wxShowEffect so the notebook can animate page
    //         swaps without changing the selection model.
    void SetEffects(wxShowEffect showEffect, wxShowEffect hideEffect)
    {
        m_showEffect = showEffect;
        m_hideEffect = hideEffect;
    }

    // Or the same effect for both of them.
    void SetEffect(wxShowEffect effect) { SetEffects(effect, effect); }

    // And the same for time outs.
    // [STATE] The transition timeouts pair with the effects above and must stay in sync with the
    //         current show/hide animation choice.
    void SetEffectsTimeouts(unsigned showTimeout, unsigned hideTimeout)
    {
        m_showTimeout = showTimeout;
        m_hideTimeout = hideTimeout;
    }

    void SetEffectTimeout(unsigned timeout) { SetEffectsTimeouts(timeout, timeout); }

    // Implement base class pure virtual methods.

    // adds a new page to the control
    // [EVENT] Adding a page updates both the wxBookCtrlBase page list and the custom rail button
    //         list, then optionally selects the newly inserted page.
    bool AddPage(wxWindow* page, const wxString& text, const std::string& bmp_name, bool bSelect = false)
    {
        DoInvalidateBestSize();
        return InsertNewPage(GetPageCount(), page, text, bmp_name, bSelect);
    }

    //// Page management
    // [EVENT] Inserts are mirrored into the rail and the notebook content list in the same call so
    //         the visible page count and button count cannot drift apart.
    virtual bool InsertPage(size_t n, wxWindow* page, const wxString& text, bool bSelect = false, int imageId = NO_IMAGE) override
    {
        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect, imageId))
            return false;

        GetBtnsListCtrl()->InsertPage(n, text, bSelect);

        if (!DoSetSelectionAfterInsertion(n, bSelect))
            page->Hide();

        return true;
    }

    // [EVENT] This helper is the image-aware path used by the custom rail; it inserts the page and
    //         then mirrors the tab button metadata.
    bool InsertNewPage(size_t n, wxWindow* page, const wxString& text, const std::string& bmp_name = "", bool bSelect = false)
    {
        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect))
            return false;

        GetBtnsListCtrl()->InsertPage(n, text, bSelect, bmp_name);

        if (bSelect)
            SetSelection(n);

        return true;
    }

    // [STATE] Removal always collapses selection back to the first page, which is a subtle policy
    //         that Unity should surface explicitly instead of assuming default-index fallback.
    bool RemovePage(size_t n)
    {
        if (!wxBookCtrlBase::RemovePage(n))
            return false;

        SetSelection(0);
        return true;
    }

    // [EVENT] Selection is a two-phase operation: update the rail highlight, tell wxBookCtrlBase,
    //         then hide any page that should no longer be visible.
    // [PORTING_HAZARD:P2] The method touches m_pages and GetPage(n) directly, so invalid indices or
    //                     empty notebooks can break the assumption that the selected page exists.
    virtual int SetSelection(size_t n) override
    {
        GetBtnsListCtrl()->SetSelection(n);
        int ret = DoSetSelection(n, SetSelection_SendEvent);

        // check that only the selected page is visible and others are hidden:
        for (size_t page = 0; page < m_pages.size(); page++) {
            wxWindow* win_a = GetPage(page);
            wxWindow* win_b = GetPage(n);
            if (page != n && GetPage(page) != GetPage(n)) {
                m_pages[page]->Hide();
            }
        }

        return ret;
    }

    // [EVENT] ChangeSelection updates the rail without sending the selection-changed event, matching
    //         wxBookCtrlBase semantics for silent state changes.
    virtual int ChangeSelection(size_t n) override
    {
        GetBtnsListCtrl()->SetSelection(n);
        return DoSetSelection(n);
    }

    // Neither labels nor images are supported but we still store the labels
    // just in case the user code attaches some importance to them.
    // [STATE] The tab label is stored even though the custom rail renders its own button chrome.
    virtual bool SetPageText(size_t n, const wxString& strText) override
    {
        wxCHECK_MSG(n < GetPageCount(), false, wxS("Invalid page"));

        GetBtnsListCtrl()->SetPageText(n, strText);

        return true;
    }

    virtual wxString GetPageText(size_t n) const override
    {
        wxCHECK_MSG(n < GetPageCount(), wxString(), wxS("Invalid page"));
        return GetBtnsListCtrl()->GetPageText(n);
    }

    virtual bool SetPageImage(size_t WXUNUSED(n), int WXUNUSED(imageId)) override { return false; }

    virtual int GetPageImage(size_t WXUNUSED(n)) const override { return NO_IMAGE; }

    // [STATE] String-based image lookup is preserved for callers that already speak bitmap names.
    bool SetPageImage(size_t n, const std::string& bmp_name) { return GetBtnsListCtrl()->SetPageImage(n, bmp_name); }

    // Override some wxWindow methods too.
    // [EVENT] Focus is forwarded to the current page so keyboard navigation lands in page content,
    //         not on the wrapper notebook shell.
    virtual void SetFocus() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetFocus();
    }

    TabButtonsListCtrl* GetBtnsListCtrl() const { return static_cast<TabButtonsListCtrl*>(m_bookctrl); }

    // [STATE] Rescale is a thin rail proxy so callers can refresh all button metrics from the host.
    void Rescale() { GetBtnsListCtrl()->Rescale(); }

    // [INTENT] The footer row is managed by the rail and can be updated independently of page data.
    void SetFooterText(const wxString& text) { GetBtnsListCtrl()->SetFooterText(text); }

    // [EVENT] Keyboard traversal is forwarded between the notebook shell, the rail, and the active
    //         page so Shift-Tab / Tab can move focus through nested widgets without losing context.
    // [PORTING_HAZARD:P3] This method encodes wx-specific focus choreography; Unity should replace
    //                     it with explicit focus groups and navigation rules.
    void OnNavigationKey(wxNavigationKeyEvent& event)
    {
        if (event.IsWindowChange()) {
            // change pages
            // AdvanceSelection(event.GetDirection());
            this->GetGrandParent()->HandleWindowEvent(event);
        } else {
            // we get this event in 3 cases
            //
            // a) one of our pages might have generated it because the user TABbed
            // out from it in which case we should propagate the event upwards and
            // our parent will take care of setting the focus to prev/next sibling
            //
            // or
            //
            // b) the parent panel wants to give the focus to us so that we
            // forward it to our selected page. We can't deal with this in
            // OnSetFocus() because we don't know which direction the focus came
            // from in this case and so can't choose between setting the focus to
            // first or last panel child
            //
            // or
            //
            // c) we ourselves (see MSWTranslateMessage) generated the event
            //
            wxWindow* const parent = GetParent();

            // the wxObject* casts are required to avoid MinGW GCC 2.95.3 ICE
            const bool isFromParent = event.GetEventObject() == (wxObject*) parent;
            const bool isFromSelf   = event.GetEventObject() == (wxObject*) this;
            const bool isForward    = event.GetDirection();

            if (isFromSelf && !isForward) {
                // focus is currently on notebook tab and should leave
                // it backwards (Shift-TAB)
                event.SetCurrentFocus(this);
                parent->HandleWindowEvent(event);
            } else if (isFromParent || isFromSelf) {
                // no, it doesn't come from child, case (b) or (c): forward to a
                // page but only if entering notebook page (i.e. direction is
                // backwards (Shift-TAB) comething from out-of-notebook, or
                // direction is forward (TAB) from ourselves),
                if (m_selection != wxNOT_FOUND && (!event.GetDirection() || isFromSelf)) {
                    // so that the page knows that the event comes from it's parent
                    // and is being propagated downwards
                    event.SetEventObject(this);

                    wxWindow* page = m_pages[m_selection];
                    if (!page->HandleWindowEvent(event)) {
                        page->SetFocus();
                    }
                    // else: page manages focus inside it itself
                } else // otherwise set the focus to the notebook itself
                {
                    SetFocus();
                }
            } else {
                // it comes from our child, case (a), pass to the parent, but only
                // if the direction is forwards. Otherwise set the focus to the
                // notebook itself. The notebook is always the 'first' control of a
                // page.
                if (!isForward) {
                    SetFocus();
                } else if (parent) {
                    event.SetCurrentFocus(this);
                    parent->HandleWindowEvent(event);
                }
            }
        }
    }

protected:
    // [INTENT] The base class expects this hook, but the custom rail already performed the visible
    //         selection update, so there is no extra work here.
    virtual void UpdateSelectedPage(size_t WXUNUSED(newsel)) override
    {
        // Nothing to do here, but must be overridden to avoid the assert in
        // the base class version.
    }

    // [EVENT] Create the standard wxBookCtrl event types so parent code that expects notebook events
    //         still observes the custom control.
    virtual wxBookCtrlEvent* CreatePageChangingEvent() const override { return new wxBookCtrlEvent(wxEVT_BOOKCTRL_PAGE_CHANGING, GetId()); }

    // [EVENT] Mirror the base-class changed-event type so listeners see a normal page-changed signal.
    virtual void MakeChangedEvent(wxBookCtrlEvent& event) override { event.SetEventType(wxEVT_BOOKCTRL_PAGE_CHANGED); }

    // [STATE] Removing a page must update both the notebook content vector and the custom rail list
    //         before the selection is re-evaluated.
    virtual wxWindow* DoRemovePage(size_t page) override
    {
        wxWindow* const win = wxBookCtrlBase::DoRemovePage(page);
        if (win) {
            GetBtnsListCtrl()->RemovePage(page);
            DoSetSelectionAfterRemoval(page);
        }

        return win;
    }

    // [STATE] Size updates only need to resize the active page because the rail owns its own layout.
    virtual void DoSize() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetSize(GetPageRect());
    }

    // [STATE] Page visibility uses show/hide effects rather than instant toggles when animations are
    //         enabled.
    virtual void DoShowPage(wxWindow* page, bool show) override
    {
        if (show)
            page->ShowWithEffect(m_showEffect, m_showTimeout);
        else
            page->HideWithEffect(m_hideEffect, m_hideTimeout);
    }

private:
    // [STATE] Init strips the notebook border and clears animation effects so the custom chrome can
    //         take over the page frame.
    void Init()
    {
        // We don't need any border as we don't have anything to separate the
        // page contents from.
        SetInternalBorder(0);

        // No effects by default.
        m_showEffect = m_hideEffect = wxSHOW_EFFECT_NONE;

        m_showTimeout = m_hideTimeout = 0;
    }

    wxShowEffect m_showEffect, m_hideEffect;

    unsigned m_showTimeout, m_hideTimeout;

    TabButtonsListCtrl* m_ctrl{nullptr};
};
// #endif // _WIN32
#endif // slic3r_Tabbook_hpp_
