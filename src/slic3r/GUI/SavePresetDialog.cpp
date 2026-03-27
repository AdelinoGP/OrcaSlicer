#include "SavePresetDialog.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/wupdlock.h>

#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "Tab.hpp"

#include "Widgets/DialogButtons.hpp"

using Slic3r::GUI::format_wxstr;

namespace Slic3r { namespace GUI {

#define BORDER_W 10

//-----------------------------------------------
//          SavePresetDialog::Item
//-----------------------------------------------

// [INTENT] Each Item instance owns one row of the modal: editable preset name, live validity feedback, and the
// project/physical-printer toggle state for a single Preset::Type.
// [STATE] The row keeps non-owning access to the tab's preset collection and the parent dialog so validation can
// reflect live bundle state while the modal is open.
// [UNITY] Model this as a row view-model plus input row in a modal controller, with immediate validation updates
// and a shared preset repository snapshot.
// [PORTING_HAZARD:P2] The row mutates shared preset state indirectly through selection and overwrite rules, so a
// Unity port must preserve the same live coupling instead of treating the dialog as a pure form submit.
SavePresetDialog::Item::Item(Preset::Type type, const std::string& suffix, wxBoxSizer* sizer, SavePresetDialog* parent)
    : m_type(type), m_parent(parent)
{
    Tab* tab = wxGetApp().get_tab(m_type);
    assert(tab);
    m_presets = tab->get_presets();

    const Preset& sel_preset  = m_presets->get_selected_preset();
    std::string   preset_name = sel_preset.is_default ? "Untitled" :
                                sel_preset.is_system  ? (boost::format(("%1% - %2%")) % sel_preset.name % suffix).str() :
                                                        sel_preset.name;

    // if name contains extension
    if (boost::iends_with(preset_name, ".ini")) {
        size_t len = preset_name.length() - 4;
        preset_name.resize(len);
    }

    std::vector<std::string> values;
    for (const Preset& preset : *m_presets) {
        // BBS: add project embedded preset logic and refine is_external
        if (preset.is_default || preset.is_system)
            // if (preset.is_default || preset.is_system || preset.is_external)
            continue;
        values.push_back(preset.name);
    }

    wxStaticText* label_top = new wxStaticText(m_parent, wxID_ANY,
                                               from_u8((boost::format(_utf8(L("Save %s as"))) % into_u8(tab->title())).str()));
    label_top->SetFont(::Label::Body_14);
    label_top->SetForegroundColour(wxColour(38, 46, 48));

    //    m_valid_bmp = new wxStaticBitmap(m_parent, wxID_ANY, create_scaled_bitmap("blank_16", m_parent));
    //
    //    m_combo = new wxComboBox(m_parent, wxID_ANY, from_u8(preset_name), wxDefaultPosition, wxSize(35 * wxGetApp().em_unit(), -1));
    //    for (const std::string& value : values)
    //        m_combo->Append(from_u8(value));
    //
    //    m_combo->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update(); });
    // #ifdef __WXOSX__
    //    // Under OSX wxEVT_TEXT wasn't invoked after change selection in combobox,
    //    // So process wxEVT_COMBOBOX too
    //    m_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { update(); });
    // #endif //__WXOSX__
    //    wxBoxSizer *combo_sizer = new wxBoxSizer(wxHORIZONTAL);
    //    combo_sizer->Add(m_valid_bmp, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, BORDER_W);
    //    combo_sizer->Add(m_combo, 1, wxEXPAND, BORDER_W);

    wxBoxSizer* input_sizer_h = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* input_sizer_v = new wxBoxSizer(wxVERTICAL);

    /*m_input_ctrl = new wxTextCtrl(this, wxID_ANY, from_u8(preset_name), wxDefaultPosition, wxDefaultSize,wxBORDER_NONE);*/

    m_input_ctrl = new ::TextInput(parent, from_u8(preset_name), wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                   wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled),
                        std::pair<wxColour, int>(*wxWHITE, StateColor::Enabled));
    m_input_ctrl->SetBackgroundColor(input_bg);
    m_input_ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update(); });
    m_input_ctrl->SetMinSize(wxSize(SAVE_PRESET_DIALOG_INPUT_SIZE));
    m_input_ctrl->SetMaxSize(wxSize(SAVE_PRESET_DIALOG_INPUT_SIZE));

    input_sizer_v->Add(m_input_ctrl, 1, wxEXPAND, 0);
    input_sizer_h->Add(input_sizer_v, 1, wxALIGN_CENTER, 0);
    input_sizer_h->Layout();

    m_valid_label = new wxStaticText(m_parent, wxID_ANY, "");
    m_valid_label->SetForegroundColour(wxColor(255, 111, 0));

    sizer->Add(label_top, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, BORDER_W);
    sizer->Add(input_sizer_h, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, BORDER_W);
    sizer->Add(m_valid_label, 0, wxEXPAND | wxLEFT | wxRIGHT, BORDER_W);

    if (m_type == Preset::TYPE_PRINTER)
        m_parent->add_info_for_edit_ph_printer(sizer);

    // ORCA RadioGroup
    m_radio_group = new RadioGroup(m_parent,
                                   {
                                       _L("User Preset"),          // 0
                                       _L("Preset Inside Project") // 1
                                   },
                                   wxVERTICAL);

    sizer->Add(m_radio_group, 0, wxEXPAND | wxTOP | wxLEFT, BORDER_W);

    if (parent->m_mode == comDevelop) {
        m_detach_checkbox = new wxCheckBox(parent, wxID_ANY, _L("Detach from parent"));
        sizer->Add(m_detach_checkbox, 0, wxALIGN_LEFT | wxALL, BORDER_W);
        // Set initial state (unchecked by default)
        m_detach_checkbox->SetValue(m_detach);
        // Bind the checkbox event to update the detach state for this item
        m_detach_checkbox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { m_detach = m_detach_checkbox->GetValue(); });
    }

    m_radio_group->Bind(wxEVT_COMMAND_RADIOBOX_SELECTED,
                        [this](wxCommandEvent& e) { m_save_to_project = m_radio_group->GetSelection() == 1; });

    bool is_project_embedded = m_presets->get_edited_preset().is_project_embedded;
    m_radio_group->SetSelection(is_project_embedded ? 1 : 0);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", create item: type" << Preset::get_type_string(m_type) << ", preset " << m_preset_name
                            << ", is_project_embedded = " << is_project_embedded;
    update();
}

// [EVENT] Text input, radio selection, and printer-specific editing all funnel into this live validator.
// [STATE] It computes validity, warning, and overwrite messages from the current text, existing presets, and
// project-embedded selection state, then pushes the result back into the modal controls.
// [UNITY] Use a per-field validation pass in the controller/view-model layer so button enablement and warning text
// update as the user types.
// [PORTING_HAZARD:P3] Validation rules are coupled to product-specific naming conventions and preset alias lookups,
// so reimplementation needs the same rule table, not just generic text checks.
void SavePresetDialog::Item::update()
{
    m_preset_name = into_u8(m_input_ctrl->GetTextCtrl()->GetValue());

    m_valid_type = Valid;
    wxString info_line;

    const char* unusable_symbols = "<>[]:/\\|?*\"";

    const std::string unusable_suffix = PresetCollection::get_suffix_modified(); //"(modified)";
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (m_preset_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            info_line    = _L("Name is invalid;") + "\n" + _L("illegal characters:") + " " + unusable_symbols;
            m_valid_type = NoValid;
            break;
        }
    }

    if (m_valid_type == Valid && m_preset_name.find(unusable_suffix) != std::string::npos) {
        info_line    = _L("Name is invalid;") + "\n" + _L("illegal suffix:") + "\n\t" + from_u8(PresetCollection::get_suffix_modified());
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid &&
        (m_preset_name == "Default Setting" || m_preset_name == "Default Filament" || m_preset_name == "Default Printer")) {
        info_line    = _L("Name is unavailable.");
        m_valid_type = NoValid;
    }

    const Preset* existing = m_presets->find_preset(m_preset_name, false);
    if (m_valid_type == Valid && existing && (existing->is_default || existing->is_system)) {
        info_line    = _L("Overwriting a system profile is not allowed.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && existing && m_preset_name != m_presets->get_selected_preset_name()) {
        if (existing->is_compatible)
            info_line = from_u8((boost::format(_u8L("Preset \"%1%\" already exists.")) % m_preset_name).str());
        else
            info_line = from_u8(
                (boost::format(_u8L("Preset \"%1%\" already exists and is incompatible with the current printer.")) % m_preset_name).str());
        info_line += "\n" + _L("Please note that saving will overwrite this preset.");
        m_valid_type = Warning;
    }

    if (m_valid_type == Valid && m_preset_name.empty()) {
        info_line    = _L("The name is not allowed to be empty.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && m_preset_name.find_first_of(' ') == 0) {
        info_line    = _L("The name is not allowed to start with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && m_preset_name.find_last_of(' ') == m_preset_name.length() - 1) {
        info_line    = _L("The name is not allowed to end with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && m_presets->get_preset_name_by_alias(m_preset_name) != m_preset_name) {
        info_line    = _L("The name cannot be the same as a preset alias name.");
        m_valid_type = NoValid;
    }

    // BBS: add project embedded presets logic
    if (existing) { // ORCA RadioGroup
        if (existing->is_project_embedded) {
            m_radio_group->SetSelection(1);
            m_save_to_project = true;
        } else {
            m_radio_group->SetSelection(0);
            m_save_to_project = false;
        }
        m_radio_group->Disable();
    } else {
        m_radio_group->Enable();
        m_radio_group->SetSelection(m_save_to_project ? 1 : 0);
    }

    m_valid_label->SetLabel(info_line);
    m_valid_label->Show(!info_line.IsEmpty());

    // update_valid_bmp();

    if (m_type == Preset::TYPE_PRINTER)
        m_parent->update_info_for_edit_ph_printer(m_preset_name);

    m_parent->layout();
}

void SavePresetDialog::Item::update_valid_bmp()
{
    std::string bmp_name = m_valid_type == Warning ? "obj_warning" : m_valid_type == NoValid ? "cross" : "blank_16";
    m_valid_bmp->SetBitmap(create_scaled_bitmap(bmp_name, m_parent));
}

// [INTENT] Warning-state acceptance is destructive: it resolves the overwrite by deleting the existing preset
// and optionally propagating that deletion to the cloud-synced preset record.
// [THREAD] This stays on the UI thread and assumes the cloud delete call is safe to issue synchronously from the
// modal confirmation path.
// [PORTING_HAZARD:P1] The confirmation path is not just a local dialog close; it can trigger remote preset cleanup,
// so a Unity port needs an explicit async side-effect boundary and failure handling.
void SavePresetDialog::Item::accept()
{
    if (m_valid_type == Warning) {
        // BBS add sync info
        auto    it               = m_presets->find_preset(m_preset_name, false);
        Preset& current_preset   = *it;
        current_preset.sync_info = "delete";
        if (!current_preset.setting_id.empty()) {
            BOOST_LOG_TRIVIAL(info) << "delete preset = " << current_preset.name << ", setting_id = " << current_preset.setting_id;
            wxGetApp().delete_preset_from_cloud(current_preset.setting_id);
        }
        m_presets->delete_preset(m_preset_name);
    }
}

void SavePresetDialog::Item::DoSetSize(int x, int y, int width, int height, int sizeFlags /*= wxSIZE_AUTO*/)
{ wxWindow::DoSetSize(x, y, width, height, sizeFlags); }

//-----------------------------------------------
//          SavePresetDialog
//-----------------------------------------------

SavePresetDialog::SavePresetDialog(wxWindow* parent, Preset::Type type, int mode, std::string suffix)
    : DPIDialog(parent, wxID_ANY, _L("Save preset"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX), m_mode(mode)
{
    build(std::vector<Preset::Type>{type}, suffix);
    wxGetApp().UpdateDlgDarkUI(this);
}

SavePresetDialog::SavePresetDialog(wxWindow* parent, std::vector<Preset::Type> types, int mode, std::string suffix)
    : DPIDialog(parent, wxID_ANY, _L("Save preset"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX), m_mode(mode)
{
    build(types, suffix);
    wxGetApp().UpdateDlgDarkUI(this);
}

SavePresetDialog::~SavePresetDialog()
{
    for (auto item : m_items) {
        delete item;
    }
}

void SavePresetDialog::build(std::vector<Preset::Type> types, std::string suffix)
{
    // [INTENT] Build the modal shell, instantiate per-type Item rows, and attach the shared OK/Cancel footer.
    // [STATE] This is where the dialog allocates the row container and establishes the main sizer hierarchy used
    // by later live validation and DPI relayout.
    // [UNITY] A Unity port should create the panel tree up front and keep row controllers alive for the lifetime
    // of the modal window.
    // def setting
    SetBackgroundColour(SAVE_PRESET_DIALOG_DEF_COLOUR);
    SetFont(wxGetApp().normal_font());

    if (suffix.empty())
        suffix = _CTX_utf8(L_CONTEXT("Copy", "PresetName"), "PresetName");

    wxBoxSizer* m_Sizer_main = new wxBoxSizer(wxVERTICAL);

    m_presets_sizer = new wxBoxSizer(wxVERTICAL);

    // Add first item
    for (Preset::Type type : types)
        AddItem(type, suffix);

    auto dlg_btns = new DialogButtons(this, {"OK", "Cancel"});

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &SavePresetDialog::accept, this);

    dlg_btns->GetCANCEL()->Bind(wxEVT_BUTTON, &SavePresetDialog::on_select_cancel, this);

    m_Sizer_main->Add(m_presets_sizer, 0, wxEXPAND | wxALL, BORDER_W);
    m_Sizer_main->Add(dlg_btns, 0, wxEXPAND);

    SetSizer(m_Sizer_main);
    m_Sizer_main->SetSizeHints(this);

    this->Centre(wxBOTH);
}

void SavePresetDialog::on_select_cancel(wxCommandEvent& event) { EndModal(wxID_CANCEL); }

void SavePresetDialog::AddItem(Preset::Type type, const std::string& suffix)
{ m_items.emplace_back(new Item{type, suffix, m_presets_sizer, this}); }

std::string SavePresetDialog::get_name() { return m_items.front()->preset_name(); }

std::string SavePresetDialog::get_name(Preset::Type type)
{
    for (const Item* item : m_items)
        if (item->type() == type)
            return item->preset_name();
    return "";
}

void SavePresetDialog::input_name_from_other(std::string new_preset_name)
{
    // only work for one-item
    Item* curr_item = m_items[0];
    curr_item->m_input_ctrl->GetTextCtrl()->SetValue(new_preset_name);
}

void SavePresetDialog::confirm_from_other()
{
    for (Item* item : m_items) {
        item->accept();
        if (item->type() == Preset::TYPE_PRINTER)
            update_physical_printers(item->preset_name());
    }
}

// BBS: add project relate
bool SavePresetDialog::get_save_to_project_selection(Preset::Type type)
{
    for (const Item* item : m_items)
        if (item->type() == type)
            return item->save_to_project();
    return false;
}

bool SavePresetDialog::get_detach_value(Preset::Type type)
{
    for (const Item* item : m_items)
        if (item->type() == type)
            return item->is_detached();
    return false;
}

bool SavePresetDialog::enable_ok_btn() const
{
    for (const Item* item : m_items)
        if (!item->is_valid())
            return false;

    return true;
}

void SavePresetDialog::add_info_for_edit_ph_printer(wxBoxSizer* sizer)
{
    // [INTENT] Printer saves need extra bookkeeping because the chosen preset may be bound to a physical printer
    // record that must be renamed, detached, or rebound after the dialog commits.
    // [STATE] The dialog caches the selected printer/preset names so the follow-up controls can compare old and new
    // values and only show the action panel when a real change exists.
    // [UNITY] Keep this as a conditional secondary panel under the preset dialog, not a separate workflow, because
    // it is only relevant when the active printer is already selected.
    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    m_ph_printer_name                   = printers.get_selected_printer_name();
    m_old_preset_name                   = printers.get_selected_printer_preset_name();

    wxString msg_text = from_u8(
        (boost::format(_u8L("Printer \"%1%\" is selected with preset \"%2%\"")) % m_ph_printer_name % m_old_preset_name).str());
    m_label = new wxStaticText(this, wxID_ANY, msg_text);
    m_label->SetFont(wxGetApp().bold_font());

    m_action      = ChangePreset;
    m_radio_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxStaticBox* action_stb = new wxStaticBox(this, wxID_ANY, "");
    if (!wxOSX)
        action_stb->SetBackgroundStyle(wxBG_STYLE_PAINT);
    action_stb->SetFont(wxGetApp().bold_font());

    wxStaticBoxSizer* stb_sizer = new wxStaticBoxSizer(action_stb, wxVERTICAL);
    for (int id = 0; id < 3; id++) {
        wxRadioButton* btn = new wxRadioButton(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, id == 0 ? wxRB_GROUP : 0);
        btn->SetValue(id == int(ChangePreset));
        btn->Bind(wxEVT_RADIOBUTTON, [this, id](wxCommandEvent&) { m_action = (ActionType) id; });
        stb_sizer->Add(btn, 0, wxEXPAND | wxTOP, 5);
    }
    m_radio_sizer->Add(stb_sizer, 1, wxEXPAND | wxTOP, 2 * BORDER_W);

    sizer->Add(m_label, 0, wxEXPAND | wxLEFT | wxTOP, 3 * BORDER_W);
    sizer->Add(m_radio_sizer, 1, wxEXPAND | wxLEFT, 3 * BORDER_W);
}

void SavePresetDialog::update_info_for_edit_ph_printer(const std::string& preset_name)
{
    // [EVENT] This refreshes the physical-printer action copy when the edited preset name changes.
    // [STATE] The action panel is hidden unless a printer is selected and the name differs from the cached original.
    // [UNCLEAR] The exact semantics of the three radio choices depend on downstream printer persistence code;
    // current hypothesis is change-in-place, add-as-new, or simple switch.
    bool show = wxGetApp().preset_bundle->physical_printers.has_selection() && m_old_preset_name != preset_name;

    m_label->Show(show);
    m_radio_sizer->ShowItems(show);
    if (!show) {
        this->SetMinSize(wxSize(100, 50));
        return;
    }

    if (wxSizerItem* sizer_item = m_radio_sizer->GetItem(size_t(0))) {
        if (wxStaticBoxSizer* stb_sizer = static_cast<wxStaticBoxSizer*>(sizer_item->GetSizer())) {
            wxString msg_text = format_wxstr(_L("Please choose an action with \"%1%\" preset after saving."), preset_name);
            stb_sizer->GetStaticBox()->SetLabel(msg_text);

            wxString choices[] = {format_wxstr(_L("For \"%1%\", change \"%2%\" to \"%3%\" "), m_ph_printer_name, m_old_preset_name,
                                               preset_name),
                                  format_wxstr(_L("For \"%1%\", add \"%2%\" as a new preset"), m_ph_printer_name, preset_name),
                                  format_wxstr(_L("Simply switch to \"%1%\""), preset_name)};

            size_t n = 0;
            for (const wxString& label : choices)
                stb_sizer->GetItem(n++)->GetWindow()->SetLabel(label);
        }
        Refresh();
    }
}

void SavePresetDialog::layout()
{
    this->Layout();
    this->Fit();
}

void SavePresetDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    // for (Item *item : m_items) item->update_valid_bmp();

    // const wxSize& size = wxSize(45 * em, 35 * em);
    // SetMinSize(/*size*/ wxSize(100, 50));

    Fit();
    Refresh();
}

void SavePresetDialog::update_physical_printers(const std::string& preset_name)
{
    // [INTENT] Commit the save and then apply any printer-binding side effects chosen by the user.
    // [THREAD] The update is synchronous and UI-thread owned; any future Unity port should treat the printer
    // mutation as an explicit transaction boundary.
    // [PORTING_HAZARD:P2] The commit path can mutate both preset storage and printer selection state, so the port
    // needs a single source of truth for the post-save action.
    if (m_action == UndefAction)
        return;

    PhysicalPrinterCollection& physical_printers = wxGetApp().preset_bundle->physical_printers;
    if (!physical_printers.has_selection())
        return;

    std::string printer_preset_name = physical_printers.get_selected_printer_preset_name();

    if (m_action == Switch)
        // unselect physical printer, if it was selected
        physical_printers.unselect_printer();
    else {
        PhysicalPrinter printer = physical_printers.get_selected_printer();

        if (m_action == ChangePreset)
            printer.delete_preset(printer_preset_name);

        if (printer.add_preset(preset_name))
            physical_printers.save_printer(printer);

        physical_printers.select_printer(printer.get_full_name(preset_name));
    }
}

void SavePresetDialog::accept(wxCommandEvent& event)
{
    for (Item* item : m_items) {
        item->accept();
        if (item->type() == Preset::TYPE_PRINTER)
            update_physical_printers(item->preset_name());
    }

    EndModal(wxID_OK);
}

}} // namespace Slic3r::GUI
