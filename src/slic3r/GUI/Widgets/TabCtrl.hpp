#ifndef slic3r_GUI_TabCtrl_hpp_
#define slic3r_GUI_TabCtrl_hpp_

#include "Button.hpp"

wxDECLARE_EVENT(wxEVT_TAB_SEL_CHANGING, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_TAB_SEL_CHANGED, wxCommandEvent);

// [INTENT] TabCtrl is a skinned tab-strip container that owns the live tab order, keeps one
// selected page index, and exposes a vetoable selection contract for its consumers.
// [STATE] The raw Button* vector is the active child list, images is a replaceable image-list
// cache, and sel/bold cache the current active-tab emphasis state.
// [UNITY] Port this as a retained tab-strip controller with child tab visuals, a cancelable
// pre-change callback, and a confirmed-change callback after the active index commits.
// [PORTING_HAZARD:P2] This is not a stock notebook: overflow handling, selection echo, and child
// visibility are coupled to relayout logic, so Unity needs an explicit layout policy.
class TabCtrl : public StaticBox
{
    // [STATE] Raw child pointers are live handles to wx-owned Button objects created by AppendItem;
    // the vector order is the tab order and drives selection, relayout, and keyboard navigation.
    std::vector<Button*> btns;
    // [STATE] Image ownership is replacement-based: AssignImageList swaps the pointer and the
    // destructor deletes the last assigned list.
    wxImageList* images = nullptr;
    // [STATE] sizer is the layout spine used to hide/show tabs and stretch remaining space while
    // preserving the selected tab in view.
    wxBoxSizer* sizer = nullptr;

    // [STATE] sel tracks the active tab index or -1 when the control is unselected.
    int sel = -1;
    // [STATE] bold caches the font variant used to render emphasized tabs without recomputing it.
    wxFont bold;

public:
    TabCtrl(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0);

    ~TabCtrl();

public:
    virtual bool SetFont(wxFont const& font) override;

public:
    // [INTENT] Append a new tab, create its Button child, and bind it into the strip/layout model.
    int AppendItem(const wxString& item, int image = -1, int selImage = -1, void* clientData = nullptr);

    // [INTENT] Remove one tab and reconcile selection/layout around the deleted child.
    bool DeleteItem(int item);

    // [INTENT] Clear all tabs and reset the selection contract to an empty model.
    void DeleteAllItems();

    unsigned int GetCount() const;

    int GetSelection() const;

    void SelectItem(int item);

    void Unselect();

    virtual void Rescale();

    wxString GetItemText(unsigned int item) const;
    void     SetItemText(unsigned int item, wxString const& value);

    bool GetItemBold(unsigned int item) const;
    void SetItemBold(unsigned int item, bool bold);

    void* GetItemData(unsigned int item) const;
    void  SetItemData(unsigned int item, void* clientData);

    void AssignImageList(wxImageList* imageList);

    void SetItemTextColour(unsigned int item, const StateColor& col);

    // [UNCLEAR] These helpers present a linear visibility API, but the control is not actually a
    // virtualized list; Unity likely needs a real viewport/overflow model rather than a stub.
    int  GetFirstVisibleItem() const;
    int  GetNextVisible(int item) const;
    bool IsVisible(unsigned int item) const;

private:
    // [INTENT] Keep the control size and overflow layout synchronized with the active selection.
    virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);

#ifdef __WIN32__
    // [EVENT] Arrow-key navigation needs dialog-code interception on Windows so the control can
    // claim cursor keys instead of letting the host dialog consume them first.
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

    // [INTENT] Recompute which tabs remain visible and how much spacer remains after the active tab.
    void relayout();

    // [EVENT] Child button clicks collapse back into one selection index and focus claim.
    void buttonClicked(wxCommandEvent& event);
    // [EVENT] Arrow-key selection advances without wraparound; the active index stays bounded.
    void keyDown(wxKeyEvent& event);

    // [INTENT] Custom paint draws only the chrome and selected underline; the Button children
    // still own the tab labels and active-state fills.
    void doRender(wxDC& dc) override;

    // some useful events
    // [EVENT] Two-phase public selection contract: CHANGING first for veto, CHANGED after commit.
    bool sendTabCtrlEvent(bool changing = false);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_TabCtrl_hpp_
