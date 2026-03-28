#ifndef slic3r_GUI_ComboBox_hpp_
#define slic3r_GUI_ComboBox_hpp_

#include "TextInput.hpp"
#include "DropDown.hpp"

#define CB_NO_DROP_ICON DD_NO_CHECK_ICON
#define CB_NO_TEXT DD_NO_TEXT

// [INTENT] Editable combo box that composes a TextInput with a DropDown popup and wxItemContainer item storage.
// [STATE] `items` mirrors the model data; `drop` owns the popup shell; the boolean flags switch between dropdown, text-only, and
// replace-text/image display modes. [EVENT] Mouse, wheel, key, and move hooks funnel back into shared edit/select logic and eventually
// rebroadcast selection changes via `sendComboBoxEvent()`. [UNITY] Port as a composite control: editable text field + anchored popup
// ListView, with a separate display-mode renderer for the text/image replacement path. [PORTING_HAZARD:P2] wxItemContainer-style mutation
// exposes per-item client data, bitmaps, aliases, and tooltips directly; Unity needs an explicit item view-model instead of mutating
// widgets in place.
class ComboBox : public wxWindowWithItems<TextInput, wxItemContainer>
{
    typedef DropDown::Item Item;
    std::vector<Item>      items;

    DropDown drop;
    bool     drop_down                = false;
    bool     text_off                 = false;
    bool     is_replace_text_to_image = false;
    wxString replace_text;
    wxString image_for_text;

public:
    // [INTENT] Constructor wires the editable field, popup list, initial items, and optional style flags into one retained control.
    ComboBox(wxWindow*       parent,
             wxWindowID      id,
             const wxString& value     = wxEmptyString,
             const wxPoint&  pos       = wxDefaultPosition,
             const wxSize&   size      = wxDefaultSize,
             int             n         = 0,
             const wxString  choices[] = NULL,
             long            style     = 0);

    DropDown& GetDropDown() { return drop; }

    // [STATE] Font changes need to propagate to both the editable text area and the dropdown presentation.
    virtual bool SetFont(wxFont const& font) override;

public:
    // [STATE] Item mutation APIs keep the combo-box model and the popup content in sync.
    int Append(const wxString& item, const wxBitmap& bitmap = wxNullBitmap, int item_style = 0);
    int Append(const wxString& item, const wxBitmap& bitmap, void* clientData, int item_style = 0);
    int Append(const wxString& item, const wxBitmap& bitmap, const wxString& group, void* clientData = nullptr, int item_style = 0);

    int SetItems(const std::vector<DropDown::Item>& the_items);

    void         set_replace_text(wxString text, wxString image_name);
    unsigned int GetCount() const override;

    int GetSelection() const override;

    void SetSelection(int n) override;

    void SelectAndNotify(int n);

    virtual void Rescale() override;

    wxString GetValue() const;
    void     SetValue(const wxString& value);

    void     SetLabel(const wxString& label) override;
    wxString GetLabel() const override;

    int  GetFlag(unsigned int n);
    void SetFlag(unsigned int n, int value);

    void     SetTextLabel(const wxString& label);
    wxString GetTextLabel() const;

    wxString GetString(unsigned int n) const override;
    void     SetString(unsigned int n, wxString const& value) override;

    wxString GetItemTooltip(unsigned int n) const;
    void     SetItemTooltip(unsigned int n, wxString const& value);

    wxString GetItemAlias(unsigned int n) const;
    void     SetItemAlias(unsigned int n, wxString const& value);

    wxBitmap GetItemBitmap(unsigned int n);
    void     SetItemBitmap(unsigned int n, wxBitmap const& bitmap);
    bool     is_drop_down() { return drop_down; }
    void     DeleteOneItem(unsigned int pos) { DoDeleteOneItem(pos); }

protected:
    // [INTENT] wxItemContainer overrides keep the editable selection and popup list consistent with the underlying vector-backed model.
    virtual int  DoInsertItems(const wxArrayStringsAdapter& items, unsigned int pos, void** clientData, wxClientDataType type) override;
    virtual void DoClear() override;

    void DoDeleteOneItem(unsigned int pos) override;

    void* DoGetItemClientData(unsigned int n) const override;
    void  DoSetItemClientData(unsigned int n, void* data) override;

    void OnEdit() override;

    // [EVENT] Emits the synthetic combo-box change event after the selection or edit value is committed.
    void sendComboBoxEvent();

#ifdef __WIN32__
    // [PORTING_HAZARD:P3] Windows message interception may hide platform-specific input/IME behavior that Unity must handle with its own
    // input bridge.
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    // [EVENT] Low-level input handlers coordinate dropdown toggling, navigation, and text editing.
    // some useful events
    void mouseDown(wxMouseEvent& event);
    void mouseWheelMoved(wxMouseEvent& event);
    void keyDown(wxKeyEvent& event);
    void onMove(wxMoveEvent& event);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_ComboBox_hpp_
