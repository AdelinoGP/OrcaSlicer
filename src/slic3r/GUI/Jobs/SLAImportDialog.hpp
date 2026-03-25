// [INTENT]
// This header file defines the `SLAImportDialog` class, a dialog box that allows
// users to import SLA (stereolithography) archives (`.sl1`, `.sl1s`, `.zip`).
// The dialog provides options to select the file and configure the import
// process, such as choosing what to import (model, profile, or both) and the
// quality of the import (which affects the marching squares algorithm).
//
// This class implements the `SLAImportJobView` interface, which means it provides
// the necessary data for the `SLAImportJob`.
//
// [UNITY]
// In a Unity port, this wxWidgets dialog would be replaced by a UI panel created
// with the Unity UI Toolkit or a custom MonoBehaviour-based UI.
// - The file picker would be replaced by a native file dialog accessed through a
//   C# library or a custom plugin.
// - The dropdowns for import options and quality would be `Dropdown` UI elements.
// - The logic for getting the selected options would be handled by the C# script
//   that manages the UI panel.

#ifndef SLAIMPORTDIALOG_HPP
#define SLAIMPORTDIALOG_HPP

#include "SLAImportJob.hpp"

#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/filename.h>
#include <wx/filepicker.h>

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/I18N.hpp"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

// #include "libslic3r/Model.hpp"
// #include "libslic3r/PresetBundle.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] The `SLAImportDialog` class, derived from `wxDialog`, provides the UI for
// importing SLA archives. It also implements the `SLAImportJobView` interface
// to provide the necessary data to the `SLAImportJob`.
class SLAImportDialog : public wxDialog, public SLAImportJobView
{
    wxFilePickerCtrl* m_filepicker;
    wxComboBox *      m_import_dropdown, *m_quality_dropdown;

public:
    // [INTENT] Constructs the dialog, creating and arranging all the UI elements.
    SLAImportDialog(Plater* plater) : wxDialog{plater, wxID_ANY, "Import SLA archive"}
    {
        auto szvert    = new wxBoxSizer{wxVERTICAL};
        auto szfilepck = new wxBoxSizer{wxHORIZONTAL};

        // [UI] A file picker for selecting the SLA archive file.
        m_filepicker = new wxFilePickerCtrl(this, wxID_ANY, from_u8(wxGetApp().app_config->get_last_dir()), _(L("Choose SLA archive:")),
                                            "SL1 / SL1S archive files (*.sl1, *.sl1s, *.zip)|*.sl1;*.SL1;*.sl1s;*.SL1S;*.zip;*.ZIP",
                                            wxDefaultPosition, wxDefaultSize, wxFLP_DEFAULT_STYLE | wxFD_OPEN | wxFD_FILE_MUST_EXIST);

        szfilepck->Add(new wxStaticText(this, wxID_ANY, _L("Import file") + ": "), 0, wxALIGN_CENTER);
        szfilepck->Add(m_filepicker, 1);
        szvert->Add(szfilepck, 0, wxALL | wxEXPAND, 5);

        auto szchoices = new wxBoxSizer{wxHORIZONTAL};

        static const std::vector<wxString> inp_choices = {_(L("Import model and profile")), _(L("Import profile only")),
                                                          _(L("Import model only"))};

        // [UI] A dropdown for selecting what to import.
        m_import_dropdown = new wxComboBox(this, wxID_ANY, inp_choices[0], wxDefaultPosition, wxDefaultSize, inp_choices.size(),
                                           inp_choices.data(), wxCB_READONLY | wxCB_DROPDOWN);

        szchoices->Add(m_import_dropdown);
        szchoices->Add(new wxStaticText(this, wxID_ANY, _L("Quality") + ": "), 0, wxALIGN_CENTER | wxALL, 5);

        static const std::vector<wxString> qual_choices = {_(L("Accurate")), _(L("Balanced")), _(L("Quick"))};

        // [UI] A dropdown for selecting the import quality.
        m_quality_dropdown = new wxComboBox(this, wxID_ANY, qual_choices[0], wxDefaultPosition, wxDefaultSize, qual_choices.size(),
                                            qual_choices.data(), wxCB_READONLY | wxCB_DROPDOWN);
        szchoices->Add(m_quality_dropdown);

        // [EVENT] When the import selection changes, enable/disable the quality dropdown.
        m_import_dropdown->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
            if (get_selection() == Sel::profileOnly)
                m_quality_dropdown->Disable();
            else
                m_quality_dropdown->Enable();
        });

        szvert->Add(szchoices, 0, wxALL, 5);
        szvert->AddStretchSpacer(1);
        auto szbtn = new wxBoxSizer(wxHORIZONTAL);
        szbtn->Add(new wxButton{this, wxID_CANCEL});
        szbtn->Add(new wxButton{this, wxID_OK});
        szvert->Add(szbtn, 0, wxALIGN_RIGHT | wxALL, 5);

        SetSizerAndFit(szvert);
    }

    // [INTENT] Returns the user's selection for what to import (model, profile, or both).
    Sel get_selection() const override
    {
        int sel = m_import_dropdown->GetSelection();
        return Sel(std::min(int(Sel::modelOnly), std::max(0, sel)));
    }

    // [INTENT] Returns the window size for the marching squares algorithm based on
    // the selected quality. A smaller window size results in a more accurate import.
    Vec2i32 get_marchsq_windowsize() const override
    {
        enum { Accurate, Balanced, Fast };

        switch (m_quality_dropdown->GetSelection()) {
        case Fast: return {8, 8};
        case Balanced: return {4, 4};
        default:
        case Accurate: return {2, 2};
        }
    }

    // [INTENT] Returns the path of the selected file.
    std::string get_path() const override { return m_filepicker->GetPath().ToUTF8().data(); }
};

}} // namespace Slic3r::GUI

#endif // SLAIMPORTDIALOG_HPP
