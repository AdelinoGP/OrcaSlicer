#include "SingleChoiceDialog.hpp"

#include "GUI_App.hpp"
#include "MainFrame.hpp"

#include "Widgets/DialogButtons.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Thin modal selector for choosing one string value from a prebuilt list.
// [STATE] Keeps only the live ComboBox pointer; the dialog does not own any model data beyond the transient choices array.
// [UNITY] Map to a modal UI Toolkit/Canvas panel with a bound dropdown and explicit OK/Cancel result handling.
SingleChoiceDialog::SingleChoiceDialog(
    const wxString& message, const wxString& caption, const wxArrayString& choices, int initialSelection, wxWindow* parent)
    : DPIDialog(parent ? parent : static_cast<wxWindow*>(wxGetApp().mainframe),
                wxID_ANY,
                caption,
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);

    const int   dlg_width = 200;
    wxBoxSizer* bSizer    = new wxBoxSizer(wxVERTICAL);
    bSizer->SetMinSize(wxSize(FromDIP(dlg_width), -1));

    wxStaticText* message_text = new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, 0);
    message_text->Wrap(-1);
    bSizer->Add(message_text, 0, wxALL, 5);

    // [PORTING_HAZARD:P2] The constructor assumes at least one choice; the Unity port should validate or normalize empty lists before opening.
    type_comboBox = new ComboBox(this, wxID_ANY, choices[0], wxDefaultPosition, wxSize(FromDIP(dlg_width - 10), -1), 0, NULL, wxCB_READONLY);
    for (const wxString& type_name : choices) {
        type_comboBox->Append(type_name);
    }
    bSizer->Add(type_comboBox, 0, wxALL | wxALIGN_CENTER, 5);
    bSizer->AddSpacer(FromDIP(10));
    type_comboBox->SetSelection(initialSelection);

    auto dlg_btns = new DialogButtons(this, {"OK", "Cancel"});

    // [EVENT] Buttons close the modal shell immediately; the selected index is read only after ShowModal() returns OK.
    // [UNITY] Use standard button callbacks that set a dialog result and dismiss the overlay.
    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { EndModal(wxID_OK); });

    dlg_btns->GetCANCEL()->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { EndModal(wxID_CANCEL); });

    bSizer->Add(dlg_btns, 0, wxEXPAND);

    this->SetSizer(bSizer);
    this->Layout();
    bSizer->Fit(this);
    wxGetApp().UpdateDlgDarkUI(this);
}
SingleChoiceDialog::~SingleChoiceDialog() {}
// [INTENT] Run the modal interaction and return the chosen combo index, or -1 on cancel.
// [STATE] The selection lives in the ComboBox widget itself, so the dialog is effectively a transient result wrapper.
int SingleChoiceDialog::GetSingleChoiceIndex() { return this->ShowModal() == wxID_OK ? GetTypeComboBox()->GetSelection() : -1; }

// [INTENT] DPIDialog hook retained for consistency with resizable dialogs, but this modal uses a fixed layout.
// [UNITY] Unity does not need an explicit DPI callback here; a responsive layout system should size the panel automatically.
void SingleChoiceDialog::on_dpi_changed(const wxRect& suggested_rect) {}
}} // namespace Slic3r::GUI
