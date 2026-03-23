#ifndef slic3r_AMSMaterialsSetting_hpp_
#define slic3r_AMSMaterialsSetting_hpp_

#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "DeviceManager.hpp"
#include "wx/clrpicker.h"
#include "wx/colourdata.h"
#include "Widgets/RadioBox.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/HyperLink.hpp"
#include "slic3r/Utils/CalibUtils.hpp"

#define AMS_MATERIALS_SETTING_DEF_COLOUR wxColour(255, 255, 255)
#define AMS_MATERIALS_SETTING_GREY900 wxColour(38, 46, 48)
#define AMS_MATERIALS_SETTING_GREY800 wxColour(50, 58, 61)
#define AMS_MATERIALS_SETTING_GREY700 wxColour(107, 107, 107)
#define AMS_MATERIALS_SETTING_GREY300 wxColour(174, 174, 174)
#define AMS_MATERIALS_SETTING_GREY200 wxColour(248, 248, 248)
#define AMS_MATERIALS_SETTING_BODY_WIDTH FromDIP(380)
#define AMS_MATERIALS_SETTING_LABEL_WIDTH FromDIP(80)
#define AMS_MATERIALS_SETTING_COMBOX_WIDTH wxSize(FromDIP(250), FromDIP(30))
#define AMS_MATERIALS_SETTING_BUTTON_SIZE wxSize(FromDIP(90), FromDIP(24))
#define AMS_MATERIALS_SETTING_INPUT_SIZE wxSize(FromDIP(90), FromDIP(24))

namespace Slic3r { namespace GUI {

// [INTENT] Interactive swatch that renders the AMS material colour, selection border, and transparency indicator inside the slot list.
// [UNITY] Mirrors a UI Toolkit `VisualElement` with an `Image` background plus toggled selection state in a ListView row whose MouseDown
// toggles the slot controller.
class ColorPicker : public wxWindow
{
public:
    wxBitmap       m_bitmap_border;
    wxBitmap       m_bitmap_border_dark;
    wxBitmap       m_bitmap_transparent;
    ScalableBitmap m_bitmap_transparent_def; // default transparent material

    wxColour              m_colour;
    std::vector<wxColour> m_cols;
    bool                  m_selected{false};
    bool                  m_show_full{false};
    bool                  m_is_empty{false};
    int                   ctype;

    bool transparent_changed{false};

    ColorPicker(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    ~ColorPicker();

    void msw_rescale();
    void set_color(wxColour col);
    void set_colors(std::vector<wxColour> cols);
    void set_selected(bool sel)
    {
        m_selected = sel;
        Refresh();
    };
    void set_show_full(bool full)
    {
        m_show_full = full;
        Refresh();
    };
    void is_empty(bool empty) { m_is_empty = empty; };

    void paintEvent(wxPaintEvent& evt);
    void render(wxDC& dc);
    void doRender(wxDC& dc);
};

// [INTENT] Populates AMS/default palettes, keeps colour pickers grouped, and stays open while the mouse interacts with the custom palette
// widgets. [STATE] Tracks master colour vectors plus picker widgets so we can rebind palettes when the parent dialog updates slots.
// [PORTING_HAZARD:P3] `PopupWindow::ProcessLeftDown` overrides rely on wx focus semantics that need a carefully mapped Unity modal
// hierarchy.
class ColorPickerPopup : public PopupWindow
{
public:
    ScalableBitmap        m_ts_bitmap_custom;
    wxStaticBitmap*       m_ts_stbitmap_custom;
    StaticBox*            m_custom_cp;
    wxColourData*         m_clrData;
    StaticBox*            m_def_color_box;
    wxFlexGridSizer*      m_ams_fg_sizer;
    wxColour              m_def_col;
    std::vector<wxColour> m_def_colors;
    std::vector<wxColour> m_ams_colors;
    // [STATE] Picker widgets organized for defaults and AMS palettes so the popup can re-render when the palette list changes.
    std::vector<ColorPicker*> m_color_pickers;
    std::vector<ColorPicker*> m_ams_color_pickers;

public:
    ColorPickerPopup(wxWindow* parent);
    ~ColorPickerPopup() {};
    // [EVENT] Custom colour picker taps update the palette state before closing.
    void         on_custom_clr_picker(wxMouseEvent& event);
    void         set_ams_colours(std::vector<wxColour> ams);
    void         set_def_colour(wxColour col);
    void         paintEvent(wxPaintEvent& evt);
    void         Popup();
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent& event) wxOVERRIDE;

public:
};

// [INTENT] Dialog that wires DeviceManager AMS slots to UI controls so users can edit colour/temperature metadata, run resets, and save
// settings. [STATE] Keeps track of the active slot, associated colour picker, and filament metadata so the view can be reconstructed after
// every Popup/update cycle. [UNITY] Would become a UI Toolkit `Window` `VisualElement` overlay containing ListView rows bound to a
// ScriptableObject `AMSProfile` plus buttons wired through UnityEvent. [PORTING_HAZARD:P2] Depends on the wx `DPIDialog` lifecycle,
// `MachineObject` pointer ownership, and synchronous DeviceManager state that Unity will need to abstract via async controllers.
class AMSMaterialsSetting : public DPIDialog
{
public:
    AMSMaterialsSetting(wxWindow* parent, wxWindowID id);
    ~AMSMaterialsSetting();
    void create();

    // [EVENT] Paint handler for the dialog background and custom button art.
    void paintEvent(wxPaintEvent& evt);
    // [EVENT] Completes minimum-temperature entry and syncs the preset value.
    void input_min_finish();
    // [EVENT] Completes maximum-temperature entry and validates the range.
    void input_max_finish();
    // [STATE] Refreshes each control to match the active `MachineObject`/AMS metadata.
    void update();
    // [EVENT] Overrides Show to keep DPI/layout state while toggling visibility.
    bool Show(bool show) override;
    // [EVENT] Presents the dialog with optional filament/slot defaults.
    void Popup(wxString filament = wxEmptyString,
               wxString sn       = wxEmptyString,
               wxString temp_min = wxEmptyString,
               wxString temp_max = wxEmptyString,
               wxString k        = wxEmptyString,
               wxString n        = wxEmptyString);

    // [EVENT] Broadcasts slot/filament selection updates to dependent views.
    void post_select_event(int index);
    void set_color(wxColour color);
    void set_empty_color(wxColour color);
    void set_colors(std::vector<wxColour> colors);
    void set_ctype(int ctype);

    // [EVENT] Colour picker command event propagates colour changes to the updated slot metadata.
    void on_picker_color(wxCommandEvent& color);
    // [STATE] DeviceManager pointer for the AMS slot currently edited; must stay valid while the dialog lives.
    MachineObject* obj{nullptr};
    // [STATE] Selected AMS device/slot indices (0-3) extracted from the underlying metadata.
    int ams_id{0};  /* 0 ~ 3 */
    int slot_id{0}; /* 0 ~ 3 */

    std::string ams_filament_id;
    std::string ams_setting_id;

    // [STATE] Tracks whether the selected filament slot is third-party or native AMS data for conditional UI hints.
    bool             m_is_third;
    wxString         m_brand_filament;
    wxString         m_brand_sn;
    wxString         m_brand_tmp;
    wxColour         m_brand_colour;
    std::string      m_filament_type;
    ColorPickerPopup m_color_picker_popup;
    ColorPicker*     m_clr_picker;
    Label*           m_clr_name;
    // [STATE] Calibration result cache elected by the `m_comboBox_cali_result` dropdown when toggling slots.
    std::vector<PACalibResult> m_pa_profile_items;

protected:
    void create_panel_normal(wxWindow* parent);
    void create_panel_kn(wxWindow* parent);
    // [EVENT] DPI changes force layout rebuilds for high-dpi screens.
    void on_dpi_changed(const wxRect& suggested_rect) override;
    // [EVENT] Selection handlers for filament, calibration, and confirm/reset buttons.
    void on_select_filament(wxCommandEvent& evt);
    void on_select_cali_result(wxCommandEvent& evt);
    void on_select_ok(wxCommandEvent& event);
    void on_select_reset(wxCommandEvent& event);
    void on_select_close(wxCommandEvent& event);
    void on_clr_picker(wxMouseEvent& event);
    // [STATE] Virtual trays (non-AMS hardware) skip pellet-specific controls.
    bool is_virtual_tray();
    void update_widgets();

    // [STATE] Switches widget editability when printing vs tuning calibration results.
    void update_filament_editing(bool is_printing);

protected:
    // [STATE] Colour states shared by the confirm/reset buttons to preserve the AMS theme.
    StateColor    m_btn_bg_green;
    StateColor    m_btn_bg_gray;
    wxPanel*      m_panel_normal;
    wxPanel*      m_panel_SN;
    wxStaticText* m_sn_number;
    wxStaticText* warning_text;
    // wxPanel *           m_panel_body;
    wxStaticText*   m_title_filament;
    wxStaticText*   m_title_pa_profile;
    wxStaticText*   m_title_colour;
    wxStaticText*   m_title_temperature;
    TextInput*      m_input_nozzle_min;
    TextInput*      m_input_nozzle_max;
    ScalableBitmap* degree;
    wxStaticBitmap* bitmap_max_degree;
    wxStaticBitmap* bitmap_min_degree;
    Button*         m_button_reset;
    Button*         m_button_confirm;
    Label*          m_tip_readonly;
    Button*         m_button_close;
    wxColourData*   m_clrData;

    wxPanel*      m_panel_kn;
    wxStaticText* m_ratio_text;
    HyperLink*    m_wiki_ctrl;
    wxStaticText* m_k_param;
    TextInput*    m_input_k_val;
    wxStaticText* m_n_param;
    TextInput*    m_input_n_val;
    // [STATE] Tracks the index opened in the filament dropdown when the dialog is shown.
    int m_filament_selection;

    int m_pa_cali_select_id = 0;

    // [PORTING_HAZARD:P3] macOS uses `wxComboBox` while other platforms use a custom ComboBox; Unity should expose one cross-platform
    // picker bound to the AMS slot model.
#ifdef __APPLE__
    wxComboBox* m_comboBox_filament;
#else
    ComboBox* m_comboBox_filament;
#endif
    ComboBox*  m_comboBox_cali_result;
    TextInput* m_readonly_filament;

    struct FilamentInfos
    {
        std::string filament_id;
        std::string setting_id;
    };
    // [UNITY] Port this to a Dictionary<string, FilamentInfoSO> keyed by AMS IDs for fast slot lookups.
    std::map<std::string, FilamentInfos> map_filament_items;
};

wxDECLARE_EVENT(EVT_SELECTED_COLOR, wxCommandEvent);

}} // namespace Slic3r::GUI

#endif
