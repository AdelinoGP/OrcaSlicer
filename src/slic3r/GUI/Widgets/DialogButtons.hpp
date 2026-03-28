#ifndef slic3r_GUI_DialogButtons_hpp_
#define slic3r_GUI_DialogButtons_hpp_

#include "wx/wx.h"
#include "wx/sizer.h"
#include "map"
#include "set"

#include "Button.hpp"
#include "Label.hpp"

#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Reusable dialog-footer widget that turns a list of labels into semantic buttons
// (primary, alert, navigation) and keeps their layout stable across DPI changes.
// [UNITY] Port as a retained footer controller that owns button views and reapplies role styling after relayout.
// [PORTING_HAZARD:P2] Button meaning is inferred from translated labels and wxStandardID aliases, so Unity should keep an explicit
// semantic role map instead of matching raw strings.
class DialogButtons : public wxPanel
{
public:
    // [INTENT] Build the footer from a set of labels, optionally marking one as primary and reserving a left-aligned group.
    DialogButtons(wxWindow*             parent,
                  std::vector<wxString> non_translated_labels,
                  const wxString&       primary_btn_label          = "",
                  const int             left_aligned_buttons_count = 0);

    wxBoxSizer* GetSizer() const { return m_sizer; }

    // [INTENT] Find a button by its semantic wxWidgets standard ID.
    Button* GetButtonFromID(wxStandardID id);

    // [INTENT] Find a button by its translated label.
    Button* GetButtonFromLabel(wxString label);

    // [INTENT] Access a cached button by visual order.
    Button* GetButtonFromIndex(int index);

    Button* GetOK();
    Button* GetYES();
    Button* GetAPPLY();
    Button* GetCONFIRM();
    Button* GetNO();
    Button* GetCANCEL();
    Button* GetRETURN();
    Button* GetNEXT();
    Button* GetFIRST();
    Button* GetLAST();

    // [STATE] Cached primary label used to restore confirm styling after relayouts.
    void SetPrimaryButton(wxString label);

    // [STATE] Cached alert label used to restore destructive styling after relayouts.
    void SetAlertButton(wxString label);

    // [STATE] Controls how many buttons stay grouped on the left side of the footer.
    void SetLeftAlignedButtonsCount(int left_aligned_buttons_count);

    // [INTENT] Rebuild spacing, order, and focus bindings from the cached button set.
    void UpdateButtons();

    ~DialogButtons();

private:
    // [STATE] Parent window used for DPI conversion and event binding.
    wxWindow* m_parent;
    // [STATE] Footer layout container rebuilt whenever roles or DPI change.
    wxBoxSizer* m_sizer;
    // [STATE] Owned buttons shown in the footer, kept in visual order.
    std::vector<Button*> m_buttons;
    // [STATE] Translated label that should receive confirm styling.
    wxString m_primary;
    // [STATE] Translated label that should receive destructive styling.
    wxString m_alert;
    // [STATE] Number of buttons pinned to the left edge before the stretch spacer.
    int m_left_aligned_buttons_count;

    // [INTENT] Map known labels to wxWidgets standard IDs so common dialog actions can be recognized by role.
    // [PORTING_HAZARD:P3] Duplicate keys overwrite earlier aliases, so this behaves as a lossy label-to-ID lookup.
    const std::map<wxString, wxStandardID> m_standardIDs =
        {// Choice
         {"ok", wxID_OK},
         {"yes", wxID_YES},
         {"apply", wxID_APPLY},
         {"confirm", wxID_APPLY}, // no id for confirm, reusing wxID_APPLY
         {"no", wxID_NO},
         {"cancel", wxID_CANCEL},
         // Action
         {"open", wxID_PRINT},
         {"open", wxID_OPEN},
         {"add", wxID_ADD},
         {"copy", wxID_COPY},
         {"new", wxID_NEW},
         {"save", wxID_SAVE},
         {"save as", wxID_SAVEAS},
         {"refresh", wxID_REFRESH},
         {"retry", wxID_RETRY},
         {"ignore", wxID_IGNORE},
         {"help", wxID_HELP},
         {"clone", wxID_DUPLICATE},
         {"duplicate", wxID_DUPLICATE},
         {"select all", wxID_SELECTALL},
         {"replace", wxID_REPLACE},
         {"replace all", wxID_REPLACE_ALL},
         // Navigation
         {"return", wxID_BACKWARD}, // use return instead back. back mostly used as side of object in translations
         {"next", wxID_FORWARD},
         // Alert / Negative
         {"remove", wxID_REMOVE},
         {"delete", wxID_DELETE},
         {"abort", wxID_ABORT},
         {"stop", wxID_STOP},
         {"reset", wxID_RESET},
         {"clear", wxID_CLEAR},
         {"exit", wxID_EXIT},
         {"quit", wxID_EXIT}};

    // [INTENT] Standard IDs that should be treated as the primary confirm action when no explicit label is provided.
    std::set<wxStandardID> m_primaryIDs{wxID_OK, wxID_YES, wxID_APPLY, wxID_SAVE, wxID_PRINT};

    // [INTENT] Standard IDs that should be treated as alert/destructive actions.
    std::set<wxStandardID> m_alertIDs{wxID_REMOVE, wxID_DELETE, wxID_ABORT, wxID_STOP, wxID_RESET, wxID_CLEAR, wxID_EXIT};

    // [INTENT] Choose the first available button from a preferred ID list.
    Button* PickFromList(std::set<wxStandardID> ID_list);

    // [INTENT] Convert logical dialog spacing into device pixels using the parent window's DPI scale.
    int FromDIP(int d);

    // [EVENT] Reflow the footer after a DPI change.
    void on_dpi_changed(wxDPIChangedEvent& event);

    // [EVENT] Keyboard navigation between footer buttons.
    void on_keydown(wxKeyEvent& event);
};

}} // namespace Slic3r::GUI
#endif // !slic3r_GUI_DialogButtons_hpp_
