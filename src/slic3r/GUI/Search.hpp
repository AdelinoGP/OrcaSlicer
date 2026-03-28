#ifndef slic3r_SearchComboBox_hpp_
#define slic3r_SearchComboBox_hpp_

#include <vector>
#include <map>

#include <boost/nowide/convert.hpp>

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/listctrl.h>

#include <wx/combo.h>

#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/srchctrl.h>

#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "libslic3r/Preset.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/PopupWindow.hpp"
#include "GUI_ObjectList.hpp"

namespace Slic3r {

// [INTENT] Declaration boundary for the two floating search overlays: preset-option search and
// object search. The implementation owns the popup chrome, while this header exposes the query
// state, row model, and event handoff points that downstream ports must preserve.
// [UNITY] Model these as query-driven overlay controllers backed by a reusable filtered list
// view and explicit dismiss/focus state.
// [PORTING_HAZARD:P2] The API surface is shaped by wxPopupWindow focus rules, manual row painting,
// and custom command events rather than a clean model/view split.

wxDECLARE_EVENT(wxCUSTOMEVT_JUMP_TO_OPTION, wxCommandEvent);
wxDECLARE_EVENT(wxCUSTOMEVT_EXIT_SEARCH, wxCommandEvent);
wxDECLARE_EVENT(wxCUSTOMEVT_JUMP_TO_OBJECT, wxCommandEvent);

namespace Search {

class SearchDialog;

// [STATE] Lightweight input snapshot used to seed the option index: the config pointer is
// borrowed, the preset type selects the option namespace, and the mode trims what is searchable.
struct InputInfo
{
    DynamicPrintConfig* config{nullptr};
    Preset::Type        type{Preset::TYPE_INVALID};
    ConfigOptionMode    mode{comSimple};
};

// [STATE] Group/category metadata cached per option key so the popup can render localized
// breadcrumbs and category-sensitive suffixes without re-querying the config bundle.
struct GroupAndCategory
{
    wxString group;
    wxString category;
};

// [INTENT] Canonical option record for fuzzy search, carrying both English and localized labels
// plus category/group metadata needed to build the popup rows.
// [UNITY] Equivalent is a search-index row DTO feeding a list view with separate display and
// localization fields.
struct Option
{
    //    bool operator<(const Option& other) const { return other.label > this->label; }
    bool operator<(const Option& other) const { return other.key > this->key; }

    // Fuzzy matching works at a character level. Thus matching with wide characters is a safer bet than with short characters,
    // though for some languages (Chinese?) it may not work correctly.
    std::wstring key;
    Preset::Type type{Preset::TYPE_INVALID};
    std::wstring label;
    std::wstring label_local;
    std::wstring group;
    std::wstring group_local;
    std::wstring category;
    std::wstring category_local;
    bool         multi_category{false};

    std::string opt_key() const;
};

// [STATE] Search result row with rendered markup, tooltip text, and the source option index.
// The strings are already encoded for the popup renderer, so the row widget only needs to
// present them and propagate selection.
struct FoundOption
{
    // UTF8 encoding, to be consumed by ImGUI by reference.
    std::string label;
    std::string marked_label;
    std::string tooltip;
    size_t      option_idx{0};
    int         outScore{0};

    // Returning pointers to contents of std::string members, to be used by ImGUI for rendering.
    void get_marked_label_and_tooltip(const char** label, const char** tooltip) const;
};

// [STATE] Display toggles that control whether the popup shows category prefixes and whether the
// English label path is used for scoring/rendering.
struct OptionViewParameters
{
    bool category{false};
    bool english{false};

    int hovered_id{0};
};

// [INTENT] Query/index manager for preset-option search. It caches option metadata, tracks the
// active search string, and publishes selection back to the owning dialog via custom events.
// [STATE] Holds the current filtered result set, the option cache, and a back-pointer to the
// active popup; none of these are owned by this class.
// [UNITY] Map to a controller/service pair that owns the search index and feeds a floating
// results panel.
class OptionsSearcher
{
    std::string  search_line;
    Preset::Type search_type = Preset::TYPE_INVALID;

    std::map<std::string, GroupAndCategory> groups_and_categories;
    PrinterTechnology                       printer_technology;

    std::vector<Option>      options{};
    std::vector<FoundOption> found{};

    void append_options(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode);

    void sort_options();
    void sort_found()
    {
        std::sort(found.begin(), found.end(), [](const FoundOption& f1, const FoundOption& f2) {
            return f1.outScore > f2.outScore || (f1.outScore == f2.outScore && f1.label < f2.label);
        });
    };

    size_t options_size() const { return options.size(); }
    size_t found_size() const { return found.size(); }

public:
    OptionViewParameters view_params;

    SearchDialog* search_dialog{nullptr};

    OptionsSearcher();
    ~OptionsSearcher();

    void init(std::vector<InputInfo> input_values);
    void apply(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode);
    bool search();
    bool search(const std::string& search, bool force = false, Preset::Type type = Preset::TYPE_INVALID);

    void add_key(const std::string& opt_key, Preset::Type type, const wxString& group, const wxString& category);

    size_t size() const { return found_size(); }

    const FoundOption& operator[](const size_t pos) const noexcept { return found[pos]; }
    const Option&      get_option(size_t pos_in_filter) const;
    const Option&      get_option(const std::string& opt_key, Preset::Type type) const;
    Option             get_option(const std::string& opt_key, const wxString& label, Preset::Type type) const;

    const std::vector<FoundOption>& found_options() { return found; }
    const GroupAndCategory&         get_group_and_category(const std::string& opt_key) { return groups_and_categories[opt_key]; }
    std::string&                    search_string() { return search_line; }

    void set_printer_technology(PrinterTechnology pt) { printer_technology = pt; }

    void sort_options_by_key()
    {
        std::sort(options.begin(), options.end(), [](const Option& o1, const Option& o2) { return o1.key < o2.key; });
    }
    void sort_options_by_label() { sort_options(); }

    void show_dialog(Preset::Type type, wxWindow* parent, TextInput* input, wxWindow* ssearch_btn);
    void dlg_sys_color_changed();
    void dlg_msw_rescale();
};

//------------------------------------------
//          SearchDialog
//------------------------------------------
class SearchDialog;
class SearchObjectDialog;
// [INTENT] Manually painted popup row for either preset options or object search results.
// [STATE] Stores the display text, row index, optional tooltip, and back-pointer to the owning
// popup so pointer events can dispatch selection.
// [UNITY] Replace with a row prefab or custom list item that uses pointer enter/down/up events
// and rich-text/highlight rendering.
class SearchItem : public wxWindow
{
public:
    wxString                      m_text;
    int                           m_index;
    SearchDialog*                 m_sdialog{nullptr};
    SearchObjectDialog*           m_search_object_dialog{nullptr};
    GUI::ObjectDataViewModelNode* m_item{nullptr};

    SearchItem(wxWindow*           parent,
               wxString            text,
               int                 index,
               SearchDialog*       sdialog       = nullptr,
               SearchObjectDialog* search_dialog = nullptr,
               wxString            tooltip       = "");
    ~SearchItem() {};

    wxSize DrawTextString(wxDC& dc, const wxString& text, const wxPoint& pt, bool bold);
    void   OnPaint(wxPaintEvent& event);
    void   on_mouse_enter(wxMouseEvent& evt);
    void   on_mouse_leave(wxMouseEvent& evt);
    void   on_mouse_left_down(wxMouseEvent& evt);
    void   on_mouse_left_up(wxMouseEvent& evt);
};

//------------------------------------------
//          SearchDialog
//------------------------------------------
class SearchListModel;
// [INTENT] Popup controller for preset-option search. It owns the search textbox alias, scroll
// area, and popup layout, but the actual search index lives in OptionsSearcher.
// [EVENT] Text edits and clicks are routed back through wx custom events to jump to a preset
// option or exit the popup.
// [UNITY] Implement as a floating search panel anchored to the triggering control, with a
// persistent list and explicit focus-loss dismissal.
class SearchDialog : public PopupWindow
{
public:
    wxColour m_bg_colour;
    wxColour m_thumb_color;

    wxBoxSizer* m_sizer_body{nullptr};
    wxBoxSizer* m_sizer_main{nullptr};
    wxBoxSizer* m_sizer_border{nullptr};

    wxWindow* m_border_panel{nullptr};
    wxWindow* m_client_panel{nullptr};

    wxWindow* m_event_tag{nullptr};
    wxWindow* m_search_item_tag{nullptr};

    int       em;
    const int POPUP_WIDTH  = 38;
    const int POPUP_HEIGHT = 40;

    TextInput*   search_line{nullptr};
    wxTextCtrl*  search_line2{nullptr};
    Preset::Type search_type = Preset::TYPE_INVALID;

    ScrolledWindow* m_scrolledWindow{nullptr};

    OptionsSearcher* searcher{nullptr};

    void OnInputText(wxCommandEvent& event);
    void OnLeftUpInTextCtrl(wxEvent& event);

    void update_list();

public:
    SearchDialog(OptionsSearcher* searcher, Preset::Type type, wxWindow* parent, TextInput* input, wxWindow* search_btn);
    ~SearchDialog();

    void MSWDismissUnfocusedPopup();
    void Popup(wxPoint position = wxDefaultPosition);
    void OnDismiss();
    void Dismiss();
    void Die();
    void msw_rescale();
};

// ----------------------------------------------------------------------------
// SearchListModel
// ----------------------------------------------------------------------------

// [INTENT] Virtual list backing for object search results. It keeps icon indices and marked text
// in a lightweight model so the popup can redraw without creating one widget per row.
// [UNITY] Use a ListView/TreeView data source with an icon column and a text column that
// preserves match markup.
class SearchListModel : public wxDataViewVirtualListModel
{
    std::vector<std::pair<wxString, int>> m_values;
    ScalableBitmap                        m_icon[5];

public:
    enum { colIcon, colMarkedText, colMax };

    SearchListModel(wxWindow* parent);

    // helper methods to change the model

    void Clear();
    void Prepend(const std::string& text);
    void msw_rescale();

    // implementation of base class virtuals to define model

    unsigned int GetColumnCount() const override { return colMax; }
    wxString     GetColumnType(unsigned int col) const override;
    void         GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const override;
    bool         GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr& attr) const override { return true; }
    bool         SetValueByRow(const wxVariant& variant, unsigned int row, unsigned int col) override { return false; }
};

// [INTENT] Popup controller for object search within the active ObjectList. It reuses the same
// popup chrome as the preset search dialog but drives selection from the scene/object model.
// [STATE] Tracks a teardown guard to avoid reentrant dismissal while focus is changing.
// [EVENT] Text edits rebuild the object name cache and row clicks jump to the selected object.
// [UNITY] Port as the same floating search panel, backed by a scene/object search adapter.
class SearchObjectDialog : public PopupWindow
{
public:
    SearchObjectDialog(GUI::ObjectList* object_list, wxWindow* parent, TextInput* input);
    ~SearchObjectDialog();

    void MSWDismissUnfocusedPopup();
    void Popup(wxPoint position = wxDefaultPosition);
    void OnDismiss();
    void Dismiss();
    void Die();

    void OnInputText(wxCommandEvent& event);
    void OnLeftUpInTextCtrl(wxEvent& event);

    void update_list();

public:
    GUI::ObjectList* m_object_list{nullptr};

    int       em;
    const int POPUP_WIDTH  = 41;
    const int POPUP_HEIGHT = 45;

    TextInput*  search_line{nullptr};
    wxTextCtrl* search_line2{nullptr};

    ScrolledWindow* m_scrolledWindow{nullptr};

    wxColour m_bg_color;
    wxColour m_thumb_color;

    wxBoxSizer* m_sizer_body{nullptr};
    wxBoxSizer* m_sizer_main{nullptr};
    wxBoxSizer* m_sizer_border{nullptr};

    wxWindow* m_border_panel{nullptr};
    wxWindow* m_client_panel{nullptr};

private:
    bool m_is_dismissing{false};
};

} // namespace Search
} // namespace Slic3r

#endif // slic3r_SearchComboBox_hpp_
