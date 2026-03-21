#include "ComboBox.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(ComboBox, TextInput)

EVT_LEFT_DOWN(ComboBox::mouseDown)
EVT_LEFT_DCLICK(ComboBox::mouseDown)
// EVT_MOUSEWHEEL(ComboBox::mouseWheelMoved)
EVT_KEY_DOWN(ComboBox::keyDown)

// catch paint events
END_EVENT_TABLE()

// [INTENT] Blend a clickable drop list with an editable text input so selection and typed values stay synchronized.
// [STATE] `drop`, `items`, `drop_down`, and `text_off` encode the combo state that must stay in sync with the text control.
// [UNITY] Model this with a TMP_InputField + Dropdown pair wired through a shared MonoBehaviour that mirrors `value` and `onValueChanged`.

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

static wxWindow* GetScrollParent(wxWindow* pWindow)
{
    wxWindow* pWin = pWindow;
    while (pWin->GetParent()) {
        auto pWin2 = pWin->GetParent();
        if (auto top = dynamic_cast<wxScrollHelper*>(pWin2))
            return dynamic_cast<wxWindow*>(pWin);
        pWin = pWin2;
    }
    return nullptr;
}

// [PORTING_HAZARD:P3] popup positioning depends on walking up to the native `wxScrollHelper` so Unity must monitor ScrollRect shifts before
// showing a dropdown or the overlay will clip.
ComboBox::ComboBox(wxWindow*       parent,
                   wxWindowID      id,
                   const wxString& value,
                   const wxPoint&  pos,
                   const wxSize&   size,
                   int             n,
                   const wxString  choices[],
                   long            style)
    : drop(items)
{
    if ((style & wxALIGN_MASK) == 0 && (style & wxCB_READONLY))
        style |= wxALIGN_RIGHT;
    text_off = style & CB_NO_TEXT;
    TextInput::Create(parent, "", value, (style & CB_NO_DROP_ICON) ? "" : "drop_down", pos, size, style | wxTE_PROCESS_ENTER);
    drop.Create(this, style & DD_STYLE_MASK);

    // [STATE] `drop` owns the item list and `drop_down` gates visibility so every handler below must keep them aligned on the UI thread.

    if (style & wxCB_READONLY) {
        GetTextCtrl()->Hide();
        TextInput::SetFont(Label::Body_14);
        TextInput::SetBorderColor(StateColor(std::make_pair(0xDBDBDB, (int) StateColor::Disabled),
                                             std::make_pair(0x009688, (int) StateColor::Hovered),
                                             std::make_pair(0xDBDBDB, (int) StateColor::Normal)));
        TextInput::SetBackgroundColor(
            StateColor(std::make_pair(0xF0F0F1, (int) StateColor::Disabled),
                       std::make_pair(0xE5F0EE, (int) StateColor::Focused), // ORCA updated background color for focused item
                       std::make_pair(*wxWHITE, (int) StateColor::Normal)));
        TextInput::SetLabelColor(
            StateColor(std::make_pair(0x6B6B6B, (int) StateColor::Disabled), // ORCA: Use same color for disabled text on combo boxes
                       std::make_pair(0x262E30, (int) StateColor::Normal)));
    }
    // [STATE] Read-only combos hide the text control so `TextInput` styling becomes the rendered label that Unity would replace with a
    // non-editable TMP_Text.
    if (auto scroll = GetScrollParent(this))
        // [EVENT][THREAD] Listen for wxEVT_MOVE on the scroll parent so the popup hides before the user drags the scroll area away from the combo.
        scroll->Bind(wxEVT_MOVE, &ComboBox::onMove, this);
    // [EVENT][THREAD] Relay wxEVT_COMBOBOX back out after keeping drop selection in sync; always running on the UI thread.
    drop.Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) {
        SetSelection(e.GetInt());
        e.SetEventObject(this);
        e.SetId(GetId());
        GetEventHandler()->ProcessEvent(e);
    });
    // [EVENT] EVT_DISMISS hides the popup gracefully so ComboBox can emit a CLOSEUP and reset `drop_down`.
    drop.Bind(EVT_DISMISS, [this](auto&) {
        drop_down = false;
        wxCommandEvent e(wxEVT_COMBOBOX_CLOSEUP);
        GetEventHandler()->ProcessEvent(e);
    });
    for (int i = 0; i < n; ++i)
        Append(choices[i]);
}

int ComboBox::GetSelection() const { return drop.GetSelection(); }

// [STATE] Keep the dropdown model, text label, icon, and tooltip synchronized whenever a new selection is chosen.
// [UNITY] In Unity this is equivalent to `Dropdown.value` updating a TMP label + icon sprite, so mirror these side effects on the main thread.
void ComboBox::SetSelection(int n)
{
    if (n == drop.selection)
        return;
    drop.SetSelection(n);
    SetLabel(drop.GetValue());
    if (drop.selection >= 0 && drop.iconSize.y > 0 && items[drop.selection].icon_textctrl.IsOk())
        SetIcon(items[drop.selection].icon_textctrl);
    else
        SetIcon("drop_down");

    if (drop.selection >= 0) {
        SetStaticTips(items[drop.selection].text_static_tips, wxNullBitmap);
    } else {
        SetStaticTips(wxEmptyString, wxNullBitmap);
    }
}

// [EVENT] SelectAndNotify fires the synthesized wxEVT_COMBOBOX with the new index, mirroring Unity's `onValueChanged` callbacks.
void ComboBox::SelectAndNotify(int n)
{
    SetSelection(n);
    sendComboBoxEvent();
}

// [STATE] Rescaling cascades to the dropdown so the cached icon/label geometry matches the font scaling; Unity would mirror this via
// CanvasScaler + layout rebuild.
void ComboBox::Rescale()
{
    TextInput::Rescale();
    drop.Rescale();
}

wxString ComboBox::GetValue() const { return drop.GetSelection() >= 0 ? drop.GetValue() : GetLabel(); }

// [STATE] Setting a value keeps the dropdown selection, icon, and tooltip coherent with the TextInput label.
void ComboBox::SetValue(const wxString& value)
{
    drop.SetValue(value);
    SetLabel(value);
    if (drop.selection >= 0 && drop.iconSize.y > 0 && items[drop.selection].icon_textctrl.IsOk())
        SetIcon(items[drop.selection].icon_textctrl);
    else
        SetIcon("drop_down");

    if (drop.selection >= 0) {
        SetStaticTips(items[drop.selection].text_static_tips, wxNullBitmap);
    } else {
        SetStaticTips(wxEmptyString, wxNullBitmap);
    }
}

// [STATE][PORTING_HAZARD:P2] `SetLabel` switches between the editable text control and a replacement image label, which Unity must
// replicate with dynamic TMP sprites when `replace_text` matches.
void ComboBox::SetLabel(const wxString& value)
{
    if (GetTextCtrl()->IsShown() || text_off)
        GetTextCtrl()->SetValue(value);
    else {
        if (is_replace_text_to_image) {
            auto new_value = value;
            if (new_value.starts_with(replace_text)) {
                new_value.Replace(replace_text, "", false); // replace first text
                TextInput::SetIcon_1(image_for_text);
                TextInput::SetLabel(new_value);
                return;
            }
        }
        TextInput::SetIcon_1("");
        TextInput::SetLabel(value);
    }
}

wxString ComboBox::GetLabel() const
{
    if (GetTextCtrl()->IsShown() || text_off)
        return GetTextCtrl()->GetValue();
    else
        return TextInput::GetLabel();
}

int ComboBox::GetFlag(unsigned int n)
{
    if (n >= items.size())
        return -1;
    return items[n].flag;
}

// [STATE] Flags influence rendering records yet stay tied to the `items` vector, so invalidating the dropdown triggers a repaint while
// preserving its layout. [OPENGL] `drop.Invalidate` depends on the wxWidgets paint cycle rather than a GL draw call; Unity must call
// `CanvasRenderer.SetVerticesDirty` instead.
void ComboBox::SetFlag(unsigned int n, int value)
{
    if (n >= items.size())
        return;
    items[n].flag = value;
    drop.Invalidate();
}

void ComboBox::SetTextLabel(const wxString& label) { TextInput::SetLabel(label); }

wxString ComboBox::GetTextLabel() const { return TextInput::GetLabel(); }

bool ComboBox::SetFont(wxFont const& font)
{
    if (GetTextCtrl() && GetTextCtrl()->IsShown())
        return GetTextCtrl()->SetFont(font);
    else
        return TextInput::SetFont(font);
}

// [STATE][UNITY] Appending updates `items` so the dropdown options stay consistent with the underlying list; Unity would mutate a
// `List<string>` and call `dropdown.RefreshShownValue()`.
int ComboBox::Append(const wxString& item, const wxBitmap& bitmap, int style)
{
    if (&bitmap && bitmap.IsOk()) {
        return Append(item, bitmap, nullptr, style);
    }
    return Append(item, wxNullBitmap, nullptr, style);
}

int ComboBox::Append(const wxString& text, const wxBitmap& bitmap, void* clientData, int style)
{
    if (&bitmap && bitmap.IsOk()) {
        return Append(text, bitmap, wxString{}, clientData, style);
    }
    return Append(text, wxNullBitmap, wxString{}, clientData, style);
}

int ComboBox::Append(const wxString& text, const wxBitmap& bitmap, const wxString& group, void* clientData, int style)
{
    auto valid_bit_map = (&bitmap && bitmap.IsOk()) ? bitmap : wxNullBitmap;
    Item item{text, wxEmptyString, valid_bit_map, valid_bit_map, clientData, group};
    item.style = style;
    items.push_back(item);
    SetClientDataType(wxClientData_Void);
    drop.Invalidate();
    return items.size() - 1;
}

// [STATE] `SetItems` replaces the entire dropdown dataset before invalidating, so Unity must refresh any bound `Dropdown.options` atomically.
int ComboBox::SetItems(const std::vector<DropDown::Item>& the_items)
{
    items = the_items;
    drop.Invalidate();
    return items.size() - 1;
}

// [STATE][PORTING_HAZARD:P3] Clearing the dropdown resets items, icon, and tooltip so Unity must unset any cached sprites before rebuilding
// the menu.
void ComboBox::DoClear()
{
    SetIcon("drop_down");
    items.clear();
    drop.Invalidate(true);
}

// [STATE] Deleting one item adjusts the cached vector and invalidates the dropdown so ComboBox keeps indices in range.
void ComboBox::DoDeleteOneItem(unsigned int pos)
{
    if (pos >= items.size())
        return;
    items.erase(items.begin() + pos);
    drop.Invalidate(true);
}

unsigned int ComboBox::GetCount() const { return items.size(); }

// [STATE][PORTING_HAZARD:P2] `set_replace_text` toggles whether specific strings show icons, so Unity must mirror this guard when composing
// UI sprites.
void ComboBox::set_replace_text(wxString text, wxString image_name)
{
    replace_text             = text;
    image_for_text           = image_name;
    is_replace_text_to_image = true;
}

wxString ComboBox::GetString(unsigned int n) const { return n < items.size() ? items[n].text : wxString{}; }

// [STATE] `SetString` keeps each item's stored text current and refreshes the UI when the selected row changed.
// [UNITY] In Unity, update the `Dropdown.options` entry and refresh the displayed value so cached indexes stay valid.
void ComboBox::SetString(unsigned int n, wxString const& value)
{
    if (n >= items.size())
        return;
    items[n].text = value;
    drop.Invalidate();
    if (n == drop.GetSelection())
        SetLabel(value);
}

wxString ComboBox::GetItemTooltip(unsigned int n) const
{
    if (n >= items.size())
        return wxString();
    return items[n].tip;
}

// [EVENT] Tooltips update the drop control immediately if the currently selected item changes so pointer hovers stay accurate.
void ComboBox::SetItemTooltip(unsigned int n, wxString const& value)
{
    if (n >= items.size())
        return;
    items[n].tip = value;
    if (n == drop.GetSelection())
        drop.SetToolTip(value);
}

wxBitmap ComboBox::GetItemBitmap(unsigned int n) { return items[n].icon; }

// [STATE] `SetItemBitmap` keeps icon bitmaps aligned with the dropdown's cache so the UI can swap sprites without reloading textures.
void ComboBox::SetItemBitmap(unsigned int n, wxBitmap const& bitmap)
{
    if (n >= items.size())
        return;
    items[n].icon = (&bitmap && bitmap.IsOk()) ? bitmap : wxNullBitmap;
    drop.Invalidate();
}

// [STATE] `DoInsertItems` merges external string arrays into `items`; Unity should update the Dropdown data source and call
// `RefreshShownValue` afterward.
int ComboBox::DoInsertItems(const wxArrayStringsAdapter& items, unsigned int pos, void** clientData, wxClientDataType type)
{
    if (pos > this->items.size())
        return -1;
    for (int i = 0; i < items.GetCount(); ++i) {
        Item item{items[i], wxEmptyString, wxNullBitmap, wxNullBitmap, clientData ? clientData[i] : NULL};
        this->items.insert(this->items.begin() + pos, item);
        ++pos;
    }
    drop.Invalidate(true);
    return pos - 1;
}

void* ComboBox::DoGetItemClientData(unsigned int n) const { return n < items.size() ? items[n].data : NULL; }

void ComboBox::DoSetItemClientData(unsigned int n, void* data)
{
    if (n < items.size())
        items[n].data = data;
}

// [EVENT][THREAD][PORTING_HAZARD:P2] mouseDown toggles the popup on the UI thread and relies on `drop.autoPosition`, so Unity must
// reproduce the overlay placement logic.
void ComboBox::mouseDown(wxMouseEvent& event)
{
    if (!IsEnabled()) {
        return;
    } /*on mac, the event may triggered even disabled*/

    SetFocus();
    if (drop_down) {
        drop.Hide();
    } else if (drop.HasDismissLongTime()) {
        drop.autoPosition();
        drop_down = true;
        drop.Popup(&drop);
        wxCommandEvent e(wxEVT_COMBOBOX_DROPDOWN);
        GetEventHandler()->ProcessEvent(e);
    }
}

// [EVENT] Mouse wheel adjusts selection when the dropdown is closed, mirroring a scroll wheel hooking to Unity's `Input.mouseScrollDelta`.
void ComboBox::mouseWheelMoved(wxMouseEvent& event)
{
    event.Skip();
    if (drop_down)
        return;
    auto         delta = event.GetWheelRotation() < 0 ? 1 : -1;
    unsigned int n     = GetSelection() + delta;
    if (n < GetCount()) {
        SetSelection((int) n);
        sendComboBoxEvent();
    }
}

// [EVENT] keyDown intercepts navigation, selection, and activation keys so Unity must duplicate this handling in its input pipeline.
void ComboBox::keyDown(wxKeyEvent& event)
{
    switch (event.GetKeyCode()) {
    case WXK_RETURN:
    case WXK_SPACE:
        if (drop_down) {
            drop.DismissAndNotify();
        } else if (drop.HasDismissLongTime()) {
            drop.autoPosition();
            drop_down = true;
            drop.Popup();
            wxCommandEvent e(wxEVT_COMBOBOX_DROPDOWN);
            GetEventHandler()->ProcessEvent(e);
        }
        break;
    case WXK_UP:
    case WXK_DOWN:
    case WXK_LEFT:
    case WXK_RIGHT:
        if ((event.GetKeyCode() == WXK_UP || event.GetKeyCode() == WXK_LEFT) && GetSelection() > 0) {
            SetSelection(GetSelection() - 1);
        } else if ((event.GetKeyCode() == WXK_DOWN || event.GetKeyCode() == WXK_RIGHT) && GetSelection() + 1 < items.size()) {
            SetSelection(GetSelection() + 1);
        } else {
            break;
        }
        sendComboBoxEvent();
        break;
    case WXK_TAB: HandleAsNavigationKey(event); break;
    default: event.Skip(); break;
    }
}

// [EVENT][THREAD] onMove hides the popup when the scroll parent shifts, keeping the dropdown invisible while the container changes position.
void ComboBox::onMove(wxMoveEvent& event)
{
    event.Skip();
    drop.Hide();
}

// [EVENT] OnEdit reuses the current TextCtrl value to keep the dropdown and typing stream in sync, similar to Unity's `InputField.onEndEdit`.
void ComboBox::OnEdit()
{
    auto value = GetTextCtrl()->GetValue();
    SetValue(value);
}

#ifdef __WIN32__

WXLRESULT ComboBox::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    if (nMsg == WM_GETDLGCODE) {
        return DLGC_WANTALLKEYS;
    }
    return TextInput::MSWWindowProc(nMsg, wParam, lParam);
}

#endif

// [PORTING_HAZARD:P3] Intercepting WM_GETDLGCODE ensures the combo grabs navigation keys; Unity needs a custom input focus handler to do the same.

// [EVENT][THREAD] sendComboBoxEvent packages the wxEVT_COMBOBOX on the UI thread so external handlers receive the updated selection safely.
void ComboBox::sendComboBoxEvent()
{
    wxCommandEvent event(wxEVT_COMBOBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(drop.GetSelection());
    event.SetString(drop.GetValue());
    GetEventHandler()->ProcessEvent(event);
}
