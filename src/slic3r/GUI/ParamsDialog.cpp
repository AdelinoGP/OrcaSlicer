#include "ParamsDialog.hpp"
#include "I18N.hpp"
#include "ParamsPanel.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Tab.hpp"

#include "libslic3r/Utils.hpp"

namespace pt = boost::property_tree;
typedef pt::ptree JSON;

// [INTENT] ParamsDialog is a modal dialog that hosts a ParamsPanel for editing print/filament/printer parameters.
// [INTENT] Provides DPI scaling handling and close event management with dirty preset checks.
// [STATE] m_panel holds the ParamsPanel; m_editing_filament_id tracks filament being edited.
// [EVENT] Binds wxEVT_SHOW to manage window disabler; wxEVT_CLOSE_WINDOW to handle dirty preset checks and filament editing.
// [UNITY] Map to a UI Toolkit modal dialog (UIDocument) with a root VisualElement hosting a ParamsPanel component.
// [UNITY] DPIDialog base maps to a Canvas with Canvas Scaler; wxWindowDisabler is not needed in Unity.
// [PORTING_HAZARD:P2] The dirty preset check and veto logic requires a custom confirmation dialog in Unity.

namespace Slic3r { namespace GUI {

// [INTENT] Constructor: creates the dialog, initializes ParamsPanel, sets layout, and binds event handlers.
// [INTENT] wxEVT_SHOW manages a wxWindowDisabler to block input to other windows while this dialog is shown.
// [INTENT] wxEVT_CLOSE_WINDOW handles dirty preset checks (currently disabled) and filament editing cleanup.
// [STATE] m_panel is the ParamsPanel; m_editing_filament_id is cleared on close.
// [EVENT] Bind wxEVT_SHOW and wxEVT_CLOSE_WINDOW as described.
// [UNITY] Constructor maps to UI Toolkit Instantiate or UIDocument creation; wxWindowDisabler is not needed.
// [UNITY] Event binding maps to UI Toolkit.RegisterCallback or C# event subscription.
ParamsDialog::ParamsDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
{
    m_panel        = new ParamsPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    auto* topsizer = new wxBoxSizer(wxVERTICAL);
    topsizer->Add(m_panel, 1, wxALL | wxEXPAND, 0, NULL);

    SetSizerAndFit(topsizer);
    SetSize({75 * em_unit(), 60 * em_unit()});

    Layout();
    Center();
    Bind(wxEVT_SHOW, [this](auto& event) {
        if (IsShown()) {
            m_winDisabler = new wxWindowDisabler(this);
        } else {
            delete m_winDisabler;
            m_winDisabler = nullptr;
        }
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](auto& event) {
#if 0
		auto tab = dynamic_cast<Tab *>(m_panel->get_current_tab());
        if (event.CanVeto() && tab->m_presets->current_is_dirty()) {
			bool ok = tab->may_discard_current_dirty_preset();
			if (!ok)
				event.Veto();
            else {
                tab->m_presets->discard_current_changes();
                tab->load_current_preset();
                Hide();
            }
        } else {
            Hide();
        }
#else
        Hide();
        if (!m_editing_filament_id.empty()) {
            Filamentinformation* filament_info = new Filamentinformation();
            filament_info->filament_id         = m_editing_filament_id;
            wxQueueEvent(wxGetApp().plater(), new SimpleEvent(EVT_MODIFY_FILAMENT, filament_info));
            m_editing_filament_id.clear();
        }
#endif
        wxGetApp().sidebar().finish_param_edit();
    });

    // wxGetApp().UpdateDlgDarkUI(this);
}

// [INTENT] Popup: shows the dialog, updates dark UI, reparents on Windows, centers, sets just_edit flag for current tab.
// [INTENT] The just_edit flag indicates whether we are editing an existing filament.
// [STATE] m_editing_filament_id determines just_edit flag.
// [EVENT] No direct event binding; called by parent to show dialog.
// [UNITY] Popup maps to a UI Toolkit panel.SetActive(true) and Focus().
// [UNITY] Reparenting not needed in Unity.
// [UNITY] just_edit flag can be passed as a parameter to the ParamsPanel component.
void ParamsDialog::Popup()
{
    wxGetApp().UpdateDlgDarkUI(this);
#ifdef __WIN32__
    Reparent(wxGetApp().mainframe);
#endif
    Center();
    if (m_panel && m_panel->get_current_tab()) {
        bool just_edit = false;
        if (!m_editing_filament_id.empty())
            just_edit = true;
        dynamic_cast<Tab*>(m_panel->get_current_tab())->set_just_edit(just_edit);
    }
    Show();
}

// [INTENT] on_dpi_changed: handles DPI scaling changes, resizes dialog and panel, and refreshes.
// [INTENT] Called by wxWidgets when DPI changes (e.g., moving between monitors).
// [STATE] No new state; uses em_unit() for sizing.
// [EVENT] Triggered by wxWidgets system, not bound in this file.
// [UNITY] Not needed in Unity; UI Toolkit automatically scales with Canvas Scaler.
// [UNITY] If custom DPI handling is required, use Screen.dpi and adjust layout.
void ParamsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    Fit();
    SetSize({75 * em_unit(), 60 * em_unit()});
    m_panel->msw_rescale();
    Refresh();
}

}} // namespace Slic3r::GUI
