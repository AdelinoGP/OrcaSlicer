#include "FilamentLoad.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

#include <boost/log/trivial.hpp>

#include "CalibUtils.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Retained wizard host for filament-change progress: the page stack swaps between load, unload, and VT-load step indicators while
// the dialog stays alive. [STATE] The control keeps three child step widgets plus a per-step label table, AMS/slot identity, and
// mode-specific step lists in sync with current machine state. [UNITY] Port this as a state-driven wizard panel with three retained
// subviews and a typed step model instead of relying on wxSimplebook page indices. [PORTING_HAZARD:P2] The implementation uses implicit
// page numbers (0/1/2) and one special-cased confirm step, so the Unity model should make workflow states explicit.
FilamentLoad::FilamentLoad(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
    : wxSimplebook(parent, wxID_ANY, pos, size)
{
    SetBackgroundColour(*wxWHITE);
    m_filament_load_steps    = new FilamentStepIndicator(this, wxID_ANY);
    m_filament_unload_steps  = new ::FilamentStepIndicator(this, wxID_ANY);
    m_filament_vt_load_steps = new ::FilamentStepIndicator(this, wxID_ANY);

    this->AddPage(m_filament_load_steps, wxEmptyString, false);
    this->AddPage(m_filament_unload_steps, wxEmptyString, false);
    this->AddPage(m_filament_vt_load_steps, wxEmptyString, false);
    // UpdateStepCtrl(false);

    FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_IDLE]               = _L("Idling...");
    FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]        = _L("Heat the nozzle");
    FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]       = _L("Cut filament");
    FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT] = _L("Pull back current filament");
    FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]  = _L("Push new filament into extruder");
    FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_GRAB_NEW_FILAMENT]  = _L("Grab new filament");
    FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT] = _L("Purge old filament");
    // FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_FEED_FILAMENT]       = _L("Feed Filament");
    FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CONFIRM_EXTRUDED] = _L("Confirm extruded");
    FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]   = _L("Check filament location");
}

void FilamentLoad::SetFilamentStep(FilamentStep item_idx, FilamentStepType f_type)
{
    // [EVENT] UI-thread step updates drive the active page, label selection, and visibility of the book.
    // [STATE] The method keeps the visible page, selected step, and slot label aligned with the current AMS/slot context.
    // [UNCLEAR] The repeated SetSelection/Layout/Hide sequence suggests the control is also used as a transient overlay; confirm the
    // intended visibility policy when porting.
    if (item_idx == FilamentStep::STEP_IDLE) {
        m_filament_load_steps->Idle();
        m_filament_unload_steps->Idle();
        m_filament_vt_load_steps->Idle();
        if (IsShown()) {
            Hide();
        }
        return;
    }

    if (!IsShown()) {
        Show();
    }
    wxString step_str = wxEmptyString;
    if (item_idx < FilamentStep::STEP_COUNT) {
        step_str = FILAMENT_CHANGE_STEP_STRING[item_idx];
    }

    auto step_control = m_filament_load_steps;
    if (f_type == FilamentStepType::STEP_TYPE_LOAD) {
        step_control = m_filament_load_steps;
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (GetSelection() != 0) {
                SetSelection(0);
            }
            m_filament_load_steps->SelectItem(m_filament_load_steps->GetItemUseText(step_str));
        } else {
            m_filament_load_steps->Idle();
            Hide();
            Layout();
        }
    } else if (f_type == FilamentStepType::STEP_TYPE_UNLOAD) {
        step_control = m_filament_unload_steps;
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (GetSelection() != 1) {
                SetSelection(1);
                Layout();
            }
            m_filament_unload_steps->SelectItem(m_filament_unload_steps->GetItemUseText(step_str));
        } else {
            m_filament_unload_steps->Idle();
            Hide();
            Layout();
        }
    } else if (f_type == FilamentStepType::STEP_TYPE_VT_LOAD) {
        step_control = m_filament_vt_load_steps;
        SetSelection(2);
        Layout();
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (item_idx == STEP_CONFIRM_EXTRUDED) {
                m_filament_vt_load_steps->SelectItem(2);
            } else {
                m_filament_vt_load_steps->SelectItem(m_filament_vt_load_steps->GetItemUseText(step_str));
            }
        } else {
            m_filament_vt_load_steps->Idle();
            Hide();
            Layout();
        }
    } else {
        step_control = m_filament_load_steps;
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            SetSelection(0);
            m_filament_load_steps->SelectItem(m_filament_load_steps->GetItemUseText(step_str));
        } else {
            m_filament_load_steps->Idle();
            Hide();
            Layout();
        }
    }

    wxString slot_info = L"AMS-";
    slot_info          = slot_info + std::to_string(m_ams_id);
    slot_info          = slot_info + L'-';
    slot_info          = slot_info + std::to_string(m_slot_id);
    slot_info          = slot_info + L" Slot";
    step_control->SetSlotInformation(slot_info);
}

void FilamentLoad::SetupSteps(bool has_fila_to_switch)
{
    // [INTENT] Rebuild the step list from machine capabilities: AMS family and extrusion-existence change which steps are shown for
    // load/unload/VT load. [STATE] This repopulates the retained child indicators from scratch, so the displayed workflow always reflects
    // the latest machine mode. [UNITY] Mirror this with a view-model that regenerates a ListView/stepper dataset from capability flags
    // instead of mutating widgets in place.
    m_filament_load_steps->DeleteAllItems();
    m_filament_unload_steps->DeleteAllItems();
    m_filament_vt_load_steps->DeleteAllItems();

    if (m_ams_model == AMSModel::GENERIC_AMS || m_ext_model == AMSModel::N3F_AMS || m_ext_model == AMSModel::N3S_AMS) {
        if (has_fila_to_switch) {
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);
        } else {
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);
        }

        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_GRAB_NEW_FILAMENT]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
    }

    if (m_ams_model == AMSModel::AMS_LITE || m_ext_model == AMSModel::AMS_LITE) {
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
    }

    Layout();
    Fit();
}

void FilamentLoad::show_nofilament_mode(bool show)
{
    // [STATE] Empty-filament mode clears all step content and returns each indicator to Idle without destroying the book page.
    // [UNITY] Treat this as a model reset plus view refresh, not as a permanent teardown of the stepper widget.
    // [UNCLEAR] The `show` parameter is currently ignored, so this looks like a legacy API shim; Unity should collapse it into explicit
    // enter/exit states or remove the flag.
    m_filament_load_steps->DeleteAllItems();
    m_filament_unload_steps->DeleteAllItems();
    m_filament_vt_load_steps->DeleteAllItems();
    m_filament_load_steps->Idle();
    m_filament_unload_steps->Idle();
    m_filament_vt_load_steps->Idle();
    this->Layout();
    Refresh();
    /*if (!show)
    {
        m_filament_load_steps->Idle();
        m_filament_unload_steps->Idle();
        m_filament_vt_load_steps->Idle();
    }
    else {
        this->Show();
        this->Layout();
    }*/
}

void FilamentLoad::set_min_size(const wxSize& minSize)
{
    // [STATE] Size and color setters are fan-out helpers that keep the three retained child indicators visually consistent with the parent
    // container.
    this->SetMinSize(minSize);
    m_filament_load_steps->SetMinSize(minSize);
    m_filament_unload_steps->SetMinSize(minSize);
    m_filament_vt_load_steps->SetMinSize(minSize);
}

void FilamentLoad::set_max_size(const wxSize& minSize)
{
    this->SetMaxSize(minSize);
    m_filament_load_steps->SetMaxSize(minSize);
    m_filament_unload_steps->SetMaxSize(minSize);
    m_filament_vt_load_steps->SetMaxSize(minSize);
}

void FilamentLoad::set_background_color(const wxColour& colour)
{
    m_filament_load_steps->SetBackgroundColour(colour);
    m_filament_unload_steps->SetBackgroundColour(colour);
    m_filament_vt_load_steps->SetBackgroundColour(colour);
}

}} // namespace Slic3r::GUI
