#ifndef slic3r_GUI_ObjectTable_hpp_
#define slic3r_GUI_ObjectTable_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/generic/gridsel.h>
#include <wx/grid.h>
#include <wx/renderer.h>
#include <wx/gdicmn.h>
#include <wx/valnum.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/popupwin.h>

#include "Plater.hpp"
#include "libslic3r/Model.hpp"
// #include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "OptionsGroup.hpp"
#include "GUI_Factories.hpp"
#include "GUI_ObjectTableSettings.hpp"
#include "Widgets/TextInput.hpp"

class ComboBox;
class TextInput;

namespace Slic3r { namespace GUI {

// [INTENT] Coordinate the wxGrid-based object table and its supporting editors/renderers so the object/volume settings stay in sync with
// ModelConfig state. [STATE] Columns, rows, selection caches, and Z-order (range selection + sort column) control how data is rendered and
// edited in this panel. [EVENT] Each grid/editor class wires into wxGrid events so clicks, keyboard input, and selection changes recompute
// ModelConfig data. [UNITY] UI Toolkit VisualElement tree (ListView + custom cell renderers) backed by ScriptableObject-style ModelConfig
// proxies would mirror the column data interactions. [PORTING_HAZARD:P3] Relies heavily on wxGrid custom renderers/editors, event tables,
// and ConfigOption plumbing that has no direct Unity counterpart.

class ObjectTablePanel;

// [INTENT] Paint the filament/color icon cell without leaving wxGrid default paint path so assets stay in sync with color cache.
// [STATE] Uses bitmap cache references to match filament index to icon.
// [EVENT] Draw() is invoked by wxGrid during refresh, so selection flags are the only event context available.
// [UNITY] UI Toolkit ListView cell template with `VisualElement` image + selection overlay replicates this renderer.
// [PORTING_HAZARD:P3] wxGridCellRenderer hooks differ from any Unity layout system, so the port needs to replicate grid frame callbacks and
// selection state tracking.
class GridCellIconRenderer : public wxGridCellRenderer
{
public:
    virtual void Draw(wxGrid& grid, wxGridCellAttr& attr, wxDC& dc, const wxRect& rect, int row, int col, bool isSelected) wxOVERRIDE;

    virtual wxSize GetBestSize(wxGrid& WXUNUSED(grid), wxGridCellAttr& attr, wxDC& dc, int WXUNUSED(row), int WXUNUSED(col)) wxOVERRIDE;

    virtual GridCellIconRenderer* Clone() const wxOVERRIDE;
};

// [INTENT] Wrap the TextInput control to intercept typed characters, propagate edit completion, and keep numeric/enum fields valid.
// [STATE] Stores `m_value` so BeginEdit/EndEdit can diff against the prior state before updating ModelConfig.
// [EVENT] StartingKey handles keystrokes before wxGrid publishes them; BeginEdit/EndEdit/ApplyEdit mediate between control and table
// changes. [UNITY] Use UI Toolkit TextField with custom `InputFieldController` MonoBehaviour for inline editing and data binding.
// [PORTING_HAZARD:P3] Unity does not expose per-cell event hooks, so we must synthesize selection context and keyboard gating manually from
// ListView events.
class GridCellTextEditor : public wxGridCellTextEditor
{
public:
    GridCellTextEditor();
    ~GridCellTextEditor();

    virtual void Create(wxWindow* parent, wxWindowID id, wxEvtHandler* evtHandler) wxOVERRIDE;
    void         StartingKey(wxKeyEvent& event) wxOVERRIDE;
    virtual void SetSize(const wxRect& rect) wxOVERRIDE;
    virtual void BeginEdit(int row, int col, wxGrid* grid) wxOVERRIDE;
    virtual bool EndEdit(int row, int col, const wxGrid* grid, const wxString& oldval, wxString* newval) wxOVERRIDE;
    virtual void ApplyEdit(int row, int col, wxGrid* grid) wxOVERRIDE;

protected:
    ::TextInput* Text() const { return (::TextInput*) m_control; }
    wxDECLARE_NO_COPY_CLASS(GridCellTextEditor);

private:
    wxString m_value;
};

// [INTENT] Provide a drop-down of available filaments with icons so the grid ties color metadata to each row.
// [STATE] Maintains `m_icons` for bitmap rendering and `m_cached_value` for tracking the committed selection.
// [EVENT] TryActivate/DoActivate gate combos so cell activation only fires once per user gesture.
// [UNITY] UI Toolkit DropdownField with `VisualElement` glyphs and `ListView` binding mirrors this behavior.
// [PORTING_HAZARD:P3] Unity lacks wxGrid electricals, so replicating close-up/commit notifications requires manual ListView focus handling.
class GridCellFilamentsEditor : public wxGridCellChoiceEditor
{
public:
    GridCellFilamentsEditor(size_t                  count       = 0,
                            const wxString          choices[]   = NULL,
                            bool                    allowOthers = false,
                            std::vector<wxBitmap*>* bitmaps     = NULL);
    GridCellFilamentsEditor(const wxArrayString& choices, bool allowOthers = false, std::vector<wxBitmap*>* bitmaps = NULL);

    virtual void Create(wxWindow* parent, wxWindowID id, wxEvtHandler* evtHandler) wxOVERRIDE;
    virtual void SetSize(const wxRect& rect) wxOVERRIDE;

    virtual wxGridCellEditor* Clone() const wxOVERRIDE;

    virtual void BeginEdit(int row, int col, wxGrid* grid) wxOVERRIDE;
    virtual bool EndEdit(int row, int col, const wxGrid* grid, const wxString& oldval, wxString* newval) wxOVERRIDE;

    virtual wxGridActivationResult TryActivate(int row, int col, wxGrid* grid, const wxGridActivationSource& actSource) wxOVERRIDE;
    virtual void                   DoActivate(int row, int col, wxGrid* grid) wxOVERRIDE;

protected:
    ::ComboBox* Combo() const { return (::ComboBox*) m_control; }
    void        OnComboCloseUp(wxCommandEvent& evt);

    std::vector<wxBitmap*>* m_icons;

    wxDECLARE_NO_COPY_CLASS(GridCellFilamentsEditor);

private:
    int m_cached_value{-1};
};

// [INTENT] Draw filament choices inline so color chips/names stay aligned with the same bitmap list used for editors.
// [STATE] Reuses `GridCellChoiceRenderer` configuration to read the filament index and selection state before painting icons.
// [UNITY] Mirror with a `VisualElement` template that renders sprite+label for each filament choice in the ListView cell.
// [PORTING_HAZARD:P3] The renderer pipeline depends on wxGrid internal layout and paint passes; Unity port requires replicating per-cell
// drawing order, focus highlight, and multi-select state.
class GridCellFilamentsRenderer : public wxGridCellChoiceRenderer
{
public:
    virtual void Draw(wxGrid& grid, wxGridCellAttr& attr, wxDC& dc, const wxRect& rect, int row, int col, bool isSelected) wxOVERRIDE;

    virtual wxSize GetBestSize(wxGrid& WXUNUSED(grid), wxGridCellAttr& attr, wxDC& dc, int WXUNUSED(row), int WXUNUSED(col)) wxOVERRIDE;

    virtual GridCellFilamentsRenderer* Clone() const wxOVERRIDE;
};

// [INTENT] Generalized choice editor handling consistent column activation + value caching when a combo sits inside a grid cell.
// [STATE] Tracks `m_cached_value` to detect whether the user actually changed the value before committing.
// [EVENT] TryActivate/DoActivate + EndEdit orchestrate commit/cancel in response to user input and `wxGridActivationSource` signals.
// [UNITY] UI Toolkit ComboBox with `SerializedProperty` binder can mimic the commit semantics and restore on escape.
// [PORTING_HAZARD:P3] wxGrid's dynamic activation sources are not available in Unity, so the port must implement equivalent gating around
// pointer/focus events.
class GridCellChoiceEditor : public wxGridCellChoiceEditor
{
public:
    GridCellChoiceEditor(size_t count = 0, const wxString choices[] = NULL);
    GridCellChoiceEditor(const wxArrayString& choices);

    virtual void Create(wxWindow* parent, wxWindowID id, wxEvtHandler* evtHandler) wxOVERRIDE;
    virtual void SetSize(const wxRect& rect) wxOVERRIDE;

    virtual wxGridCellEditor* Clone() const wxOVERRIDE;

    virtual void BeginEdit(int row, int col, wxGrid* grid) wxOVERRIDE;
    virtual bool EndEdit(int row, int col, const wxGrid* grid, const wxString& oldval, wxString* newval) wxOVERRIDE;

    virtual wxGridActivationResult TryActivate(int row, int col, wxGrid* grid, const wxGridActivationSource& actSource) wxOVERRIDE;
    virtual void                   DoActivate(int row, int col, wxGrid* grid) wxOVERRIDE;

protected:
    ::ComboBox* Combo() const { return (::ComboBox*) m_control; }
    void        OnComboCloseUp(wxCommandEvent& evt);
    wxDECLARE_NO_COPY_CLASS(GridCellChoiceEditor);

private:
    int m_cached_value{-1};
};

// [INTENT] Keep combo box labels readable by drawing them in the renderer so editors stay lightweight.
// [STATE] Reads the current combo value and alignment hints straight from wxGrid before painting.
// [UNITY] Custom cell template in UI Toolkit that draws label + icon can emulate the same visual.
// [PORTING_HAZARD:P3] Rendering order depends on wxGrid internals, so port must trigger cell redraw after data changes.
class GridCellComboBoxRenderer : public wxGridCellChoiceRenderer
{
public:
    virtual void Draw(wxGrid& grid, wxGridCellAttr& attr, wxDC& dc, const wxRect& rect, int row, int col, bool isSelected) wxOVERRIDE;

    virtual wxSize GetBestSize(wxGrid& WXUNUSED(grid), wxGridCellAttr& attr, wxDC& dc, int WXUNUSED(row), int WXUNUSED(col)) wxOVERRIDE;

    virtual GridCellComboBoxRenderer* Clone() const wxOVERRIDE;
};

// [INTENT] Treat support toggles as boolean checkboxes that mirror ModelConfig options while providing resetable string labels.
// [STATE] `m_value`, `ms_stringValues` track the boolean and its human-readable representation.
// [UNITY] Map to UI Toolkit Toggle (StyleSheet-driven) with binding to a ScriptableObject `ConfigOptionBool` entry.
// [PORTING_HAZARD:P3] wxGrid expects bool editors to toggle via `DoActivate`, so Unity port must simulate this using pointer/touch events.
class GridCellSupportEditor : public wxGridCellBoolEditor
{
public:
    GridCellSupportEditor() {}
    virtual void DoActivate(int row, int col, wxGrid* grid) wxOVERRIDE;

private:
    void SetValueFromGrid(int row, int col, wxGrid* grid);
    void SetGridFromValue(int row, int col, wxGrid* grid) const;

    wxString GetStringValue() const { return GetStringValue(m_value); }

    static wxString GetStringValue(bool value) { return ms_stringValues[value]; }

    bool m_value;

    static wxString ms_stringValues[2];

    wxDECLARE_NO_COPY_CLASS(GridCellSupportEditor);
};

// [INTENT] Draw the support checkbox or label so read-only rows still explain why support state exists.
// [STATE] Interprets boolean state from ModelConfig so disabled cells still match underlying data.
// [UNITY] A VisualElement template with a faux checkbox icon plus label covers the same narrative.
// [PORTING_HAZARD:P3] Rendering is baked into wxGrid paint passes and can't be swapped dynamically without re-rendering the cell.
class GridCellSupportRenderer : public wxGridCellBoolRenderer
{
public:
    virtual void Draw(wxGrid& grid, wxGridCellAttr& attr, wxDC& dc, const wxRect& rect, int row, int col, bool isSelected) wxOVERRIDE;

    virtual wxSize GetBestSize(wxGrid& WXUNUSED(grid), wxGridCellAttr& attr, wxDC& dc, int WXUNUSED(row), int WXUNUSED(col)) wxOVERRIDE;

    virtual GridCellSupportRenderer* Clone() const wxOVERRIDE;
};

// [INTENT] Extend wxGrid to expose ModelConfig fields while keeping clipboard, selection, and key events in sync.
// [STATE] Tracks `m_selected_block`, `input_string`, and cell data so keyboard navigation remains consistent with column counts.
// [EVENT] OnCellLeftClick/OnRangeSelected and the key handlers translate user interaction back into ObjectGridTable operations.
// [THREAD] wxGrid callbacks always run on the main wxWidgets UI thread, so any object/model updates from background slicing must be
// marshaled back before mutating this grid.
// [UNITY] Replace with UI Toolkit ListView that reports DragSelect/Click events and Keyboard events through a `ListViewController`
// MonoBehaviour. [PORTING_HAZARD:P2] wxGrid event macros and text paste handlers have no direct Unity equivalent, requiring a custom
// focus/focus-lost pipeline. ObjectGrid for the param setting table
class ObjectGrid : public wxGrid
{
public:
    ObjectGrid(wxWindow*       parent,
               wxWindowID      id,
               const wxPoint&  pos   = wxDefaultPosition,
               const wxSize&   size  = wxDefaultSize,
               long            style = wxWANTS_CHARS,
               const wxString& name  = wxASCII_STR(wxGridNameStr))
        : wxGrid(parent, id, pos, size, style, name)
    {}

    ~ObjectGrid() {}

    /*virtual wxPen GetColGridLinePen(int col)
    {
        if (col % 2 == 1)
            return wxPen(*wxBLUE, 2, wxPENSTYLE_SOLID);
        else
            return *wxTRANSPARENT_PEN;
    }

    virtual wxPen GetRowGridLinePen(int row)
    {
        return wxNullPen;
    }*/

    bool OnCellLeftClick(wxGridEvent& event, int row, int col, ConfigOptionType type);
    void OnRangeSelected(wxGridRangeSelectEvent& ev);
    void OnColHeadLeftClick(wxGridEvent& event);

    virtual void DrawColLabels(wxDC& dc, const wxArrayInt& cols);
    virtual void DrawColLabel(wxDC& dc, int col);

    // set ObjectGridTable and ObjectTablePanel as friend
    friend class ObjectGridTable;
    friend class ObjectTablePanel;

    wxString input_string;
    wxString m_cell_data;

protected:
    // void OnSize( wxSizeEvent& );
    void OnKeyDown(wxKeyEvent&);
    void OnKeyUp(wxKeyEvent&);
    void OnChar(wxKeyEvent&);

private:
    wxDECLARE_EVENT_TABLE();
    wxGridBlockCoords m_selected_block;
    void              paste_data(wxTextDataObject& text_data);
};

// [INTENT] Bridge ModelConfig/ModelVolume data with wxGrid cells, keep column metadata and value copies synchronized, and answer
// selection/sort requests. [STATE] Owns `m_grid_data` (per-object rows), `m_col_data` (column metadata), `m_selected_cells`, `m_sort_col`,
// and `m_current_row/m_current_col` so UI state survives reloads. [EVENT] Provides callbacks such as OnCellLeftClick, OnRangeSelected,
// OnCellValueChanged, and reload/selection helpers that respond to UI interactions and model updates. [THREAD] All of this table state
// is maintained on the UI thread because the `wxGridTableBase` contract is not thread-safe; dispatchers must queue events for any background
// config refresh before calling these helpers. [UNITY] Equivalent to a UI Toolkit
// ListView bound to an `ObservableCollection<ObjectRowViewModel>` plus a controller that mirrors sorting, selection, and config resets.
// [PORTING_HAZARD:P2] Deep coupling with `ConfigOption*` fields and `DynamicPrintConfig` means the Unity port must replicate config
// ownership to avoid race conditions when multiple tables mutate shared state.
class ObjectGridTable : public wxGridTableBase
{
public:
    static std::string category_all;
    static std::string plate_outside;
    enum GridRowType { row_object = 0, row_volume = 1 };
    enum GridColType {
        /*  col_plate_index = 0,
          col_assemble_name = 1,
          col_name = col_assemble_name + 1,
          col_printable = col_name + 1,
          col_printable_reset = col_printable + 1,
          col_filaments = col_printable_reset + 1,
          col_filaments_reset = col_filaments + 1,
          col_layer_height = col_filaments_reset + 1,
          col_layer_height_reset = col_layer_height + 1,
          col_wall_loops = col_layer_height_reset + 1,
          col_wall_loops_reset = col_wall_loops + 1,
          col_fill_density = col_wall_loops_reset + 1,
          col_fill_density_reset = col_fill_density + 1,
          col_enable_support = col_fill_density_reset + 1,
          col_enable_support_reset = col_enable_support + 1,
          col_brim_type = col_enable_support_reset + 1,
          col_brim_type_reset = col_brim_type + 1,
          col_speed_perimeter = col_brim_type_reset + 1,
          col_speed_perimeter_reset = col_speed_perimeter + 1,
          col_max*/
        col_printable       = 0,
        col_printable_reset = 1,
        col_plate_index     = 2,
        // col_assemble_name         = 3,
        col_name                  = 3,
        col_filaments             = 4,
        col_filaments_reset       = 5,
        col_layer_height          = 6,
        col_layer_height_reset    = 7,
        col_wall_loops            = 8,
        col_wall_loops_reset      = 9,
        col_fill_density          = 10,
        col_fill_density_reset    = 11,
        col_enable_support        = 12,
        col_enable_support_reset  = 13,
        col_brim_type             = 14,
        col_brim_type_reset       = 15,
        col_speed_perimeter       = 16,
        col_speed_perimeter_reset = 17,
        col_max
    };

    struct ObjectGridRow
    {
        int                object_id;
        int                volume_id;
        GridRowType        row_type;
        ConfigOptionString plate_index;
        // ConfigOptionString          assemble_name;
        // ConfigOptionString          ori_assemble_name;
        ConfigOptionString         name;
        ConfigOptionString         ori_name;
        ConfigOptionBool           printable;
        ConfigOptionBool           ori_printable;
        ConfigOptionInt            filaments;
        ConfigOptionInt            ori_filaments;
        ConfigOptionFloat          layer_height;
        ConfigOptionFloat          ori_layer_height;
        ConfigOptionInt            wall_loops;
        ConfigOptionInt            ori_wall_loops;
        ConfigOptionPercent        sparse_infill_density;
        ConfigOptionPercent        ori_fill_density;
        ConfigOptionBool           enable_support;
        ConfigOptionBool           ori_enable_support;
        ConfigOptionEnum<BrimType> brim_type;
        ConfigOptionEnum<BrimType> ori_brim_type;
        ConfigOptionFloat          speed_perimeter;
        ConfigOptionFloat          ori_speed_perimeter;

        ModelConfig*    config;
        ModelVolumeType model_volume_type;

        ObjectGridRow(int obj_id, int vol_id, GridRowType type) : object_id(obj_id), volume_id(vol_id), row_type(type) { config = nullptr; }

        ConfigOption& operator[](GridColType idx)
        {
            switch (idx) {
            case col_plate_index:
                return plate_index;
                /*case col_assemble_name:
                    return assemble_name;*/
            case col_name: return name;
            case col_printable: return printable;
            case col_printable_reset: return ori_printable;
            case col_filaments: return filaments;
            case col_filaments_reset: return ori_filaments;
            case col_layer_height: return layer_height;
            case col_layer_height_reset: return ori_layer_height;
            case col_wall_loops: return wall_loops;
            case col_wall_loops_reset: return ori_wall_loops;
            case col_fill_density: return sparse_infill_density;
            case col_fill_density_reset: return ori_fill_density;
            case col_enable_support: return enable_support;
            case col_enable_support_reset: return ori_enable_support;
            case col_brim_type: return brim_type;
            case col_brim_type_reset: return ori_brim_type;
            case col_speed_perimeter: return speed_perimeter;
            case col_speed_perimeter_reset: return ori_speed_perimeter;
            default: break;
            }
            return name;
        }
    };
    typedef std::function<bool(ObjectGridRow* row1, ObjectGridRow* row2)> compare_row_func;

    struct ObjectGridCol
    {
        int              size;
        ConfigOptionType type;
        std::string      key;
        std::string      category;
        bool             b_for_object;
        bool             b_icon;
        bool             b_editable;
        bool             b_from_config;
        wxArrayString    choices;
        int              choice_count;
        int              horizontal_align;

        ObjectGridCol(ConfigOptionType option_type,
                      std::string      key_str,
                      std::string      cat,
                      bool             only_object,
                      bool             icon,
                      bool             edit,
                      bool             config,
                      int              ho_align)
            : type(option_type)
            , key(key_str)
            , category(cat)
            , b_for_object(only_object)
            , b_icon(icon)
            , b_editable(edit)
            , b_from_config(config)
            , horizontal_align(ho_align)
        {
            if (b_icon)
                size = 32;
            else
                size = -1;
            choice_count = 0;
        }

        ~ObjectGridCol() {}
    };
    ObjectGridTable(ObjectTablePanel* panel) : m_panel(panel) {}
    ~ObjectGridTable();

    void     release_object_configs();
    wxString convert_filament_string(int index, wxString& filament_str);

    virtual int  GetNumberRows() wxOVERRIDE;
    virtual int  GetNumberCols() wxOVERRIDE;
    virtual bool IsEmptyCell(int row, int col) wxOVERRIDE;

    // virtual wxString GetColLabelValue( int col ) wxOVERRIDE;

    virtual wxString GetTypeName(int row, int col) wxOVERRIDE;
    virtual bool     CanGetValueAs(int row, int col, const wxString& typeName) wxOVERRIDE;
    virtual bool     CanSetValueAs(int row, int col, const wxString& typeName) wxOVERRIDE;

    virtual wxString GetValue(int row, int col) wxOVERRIDE;
    virtual void     SetValue(int row, int col, const wxString& value) wxOVERRIDE;

    virtual long   GetValueAsLong(int row, int col) wxOVERRIDE;
    virtual bool   GetValueAsBool(int row, int col) wxOVERRIDE;
    virtual double GetValueAsDouble(int row, int col) wxOVERRIDE;

    virtual void SetValueAsLong(int row, int col, long value) wxOVERRIDE;
    virtual void SetValueAsBool(int row, int col, bool value) wxOVERRIDE;
    virtual void SetValueAsDouble(int row, int col, double value) wxOVERRIDE;

    void     SetColLabelValue(int col, const wxString&) wxOVERRIDE;
    wxString GetColLabelValue(int col) wxOVERRIDE;

    template<typename TYPE>
    const TYPE* get_object_config_value(const DynamicPrintConfig& global_config, ModelConfig* obj_config, std::string& config_option)
    {
        if (obj_config->has(config_option))
            return static_cast<const TYPE*>(obj_config->option(config_option));
        else {
            const TYPE* ptr = global_config.option<TYPE>(config_option);
            // todo: how to deal with nullptr
            return ptr;
        }
    }

    template<typename TYPE>
    const TYPE* get_volume_config_value(const DynamicPrintConfig& global_config,
                                        ModelConfig*              obj_config,
                                        ModelConfig*              volume_config,
                                        std::string&              config_option)
    {
        if (volume_config->has(config_option))
            return static_cast<const TYPE*>(volume_config->option(config_option));
        else if (obj_config->has(config_option))
            return static_cast<const TYPE*>(obj_config->option(config_option));
        else {
            const TYPE* ptr = global_config.option<TYPE>(config_option);
            // todo: how to deal with nullptr
            return ptr;
        }
    }

    int            get_row_count() { return m_grid_data.size() + 1; }
    int            get_col_count() { return m_col_data.size(); }
    ObjectGridCol* get_grid_col(int col) { return m_col_data[col]; }
    ObjectGridRow* get_grid_row(int row) { return m_grid_data[row]; }
    void           construct_object_configs(ObjectGrid* object_grid);
    void           update_value_to_config(ModelConfig* config, std::string& key, ConfigOption& new_value, ConfigOption& ori_value);
    void update_filament_to_config(ModelConfig* config, std::string& key, ConfigOption& new_value, ConfigOption& ori_value, bool is_object);
    void update_volume_values_from_object(int row, int col);
    void update_value_to_object(Model* model, ObjectGridRow* grid_row, int col);
    wxBitmap& get_undo_bitmap(bool selected = false);
    wxBitmap* get_color_bitmap(int color_index);
    bool      OnCellLeftClick(int row, int col, ConfigOptionType& type);
    void      OnSelectCell(int row, int col);
    void      OnRangeSelected(int row, int col, int row_count, int col_count);
    // void OnRangeSelecting( wxGridRangeSelectEvent& );
    // void OnCellValueChanging( wxGridEvent& );
    void OnCellValueChanged(int row, int col);
    // set the selection by object id and volume id
    void SetSelection(int object_id, int volume_id);
    // sort the table row datas by default
    void sort_by_default();
    void sort_by_col(int col);

    // reload data caused by settings in the side window
    void reload_object_data(ObjectGridRow* grid_row, const std::string& category, DynamicPrintConfig& global_config);
    void reload_part_data(ObjectGridRow*      volume_row,
                          ObjectGridRow*      object_row,
                          const std::string&  category,
                          DynamicPrintConfig& global_config);
    void reload_cell_data(int row, const std::string& category);
    void resetValuesInCurrentCell(wxEvent& WXUNUSED(event));
    void enable_reset_all_button(bool enable);

    int               m_icon_col_width{0};
    int               m_icon_row_height{0};
    ObjectTablePanel* m_panel{nullptr};

private:
    std::vector<ObjectGridRow*> m_grid_data;
    std::vector<ObjectGridCol*> m_col_data;
    bool                        m_data_valid{false};

    std::list<wxGridCellCoords> m_selected_cells;

    int m_sort_col{-1};

    void init_cols(ObjectGrid* object_grid);
    // generic function for sort row datas
    void sort_row_data(compare_row_func sort_func);
    // update the row properties for the data has been sorted
    void update_row_properties();
    int  m_current_row{-1};
    int  m_current_col{-1};

    wxArrayString m_colLabels;
};

// [INTENT] Layout the grid, search field, reset buttons, and filament metadata while bridging to ObjectGridTable reloads.
// [STATE] Range selection bounds, cached filament names/colors, current row/col, and DPI-aware UI elements govern state updates.
// [EVENT] Exposes OnCellLeftClick, OnRangeSelected, OnCellValueChanged, and OnSize events to keep the grid/table responsive to user tweaks.
// [THREAD] The panel runs entirely on the wxWidgets UI thread, so any resets triggered by background model updates must marshal through
// `wxQueueEvent` or similar before touching these controls. [UNITY] A UI Toolkit scrollable Panel with a ListView and side controls bound
// via a MonoBehaviour controller mirrors this structure. [PORTING_HAZARD:P2] wxPanel uses `wxDECLARE_EVENT_TABLE` macros and custom bitmaps
// (ScalableButton) that need explicit porting to Unity's event system and sprite assets. the main panel
class ObjectTablePanel : public wxPanel
{
public:
    int range_select_left_col;
    int range_select_right_col;
    int range_select_top_row;
    int range_select_bottom_row;

    void OnCellLeftClick(wxGridEvent&);
    void OnRowSize(wxGridSizeEvent&);
    void OnColSize(wxGridSizeEvent&);
    void OnSelectCell(wxGridEvent&);
    void OnRangeSelected(wxGridRangeSelectEvent&);
    // void OnRangeSelecting( wxGridRangeSelectEvent& );
    // void OnCellValueChanging( wxGridEvent& );
    void OnCellValueChanged(wxGridEvent&);
    // void OnCellBeginDrag( wxGridEvent& );
public:
    ObjectTablePanel(wxWindow*       parent,
                     wxWindowID      id,
                     const wxPoint&  pos,
                     const wxSize&   size,
                     long            style,
                     const wxString& name,
                     Plater*         platerObj,
                     Model*          modelObj);
    ~ObjectTablePanel();

    void   load_data();
    void   SetSelection(int object_id, int volume_id);
    void   sort_by_default() { m_object_grid_table->sort_by_default(); }
    wxSize get_init_size();
    void   resetAllValuesInSideWindow(int row, bool is_object, ModelObject* object, ModelConfig* config, const std::string& category);
    void   msw_rescale();

    // set ObjectGridTable as friend
    friend class ObjectGridTable;

    // [STATE] m_filaments_name/colors keep the icon column palette in sync with the filaments drop-down and color cache so rows
    // can present the same index-based color chips. [UNITY] Store the palette in a ScriptableObject alongside Texture2D chips and expose it
    // to a VisualElement `ListView` cell template. [PORTING_HAZARD:P3] Unity uses Texture2D while wxWidgets uses `wxColour`, so color
    // metadata must be converted before rendering.
    std::vector<wxString> m_filaments_name;
    std::vector<wxColour> m_filaments_colors;
    int                   m_filaments_count{1};
    void                  set_default_filaments_and_colors()
    {
        m_filaments_count = 1;
        m_filaments_colors.push_back(*wxGREEN);
        m_filaments_name.push_back("Generic PLA");
    }

private:
    wxColour             m_bg_colour;
    wxColour             m_hover_colour;
    wxBoxSizer*          m_top_sizer{nullptr};
    wxBoxSizer*          m_page_sizer{nullptr};
    wxBoxSizer*          m_page_top_sizer{nullptr};
    wxTextCtrl*          m_search_line{nullptr};
    ObjectGrid*          m_object_grid{nullptr};
    ObjectGridTable*     m_object_grid_table{nullptr};
    wxStaticText*        m_page_text{nullptr};
    ScalableButton*      m_global_reset{nullptr};
    wxScrolledWindow*    m_side_window{nullptr};
    ObjectTableSettings* m_object_settings{nullptr};
    Model*               m_model{nullptr};
    ModelConfig*         m_config{nullptr};
    Plater*              m_plater{nullptr};

    int m_cur_row{-1};
    int m_cur_col{-1};

    int init_bitmap();
    int init_filaments_and_colors();

    wxFloatingPointValidator<float> m_float_validator;
    wxBitmap                        m_undo_bitmap;
    std::vector<wxBitmap*>          m_color_bitmaps;
    ScalableBitmap                  m_bmp_reset;
    ScalableBitmap                  m_bmp_reset_disable;

private:
    wxDECLARE_ABSTRACT_CLASS(ObjectGrid);
    wxDECLARE_EVENT_TABLE();
};

// [INTENT] Show object table in a popup dialog, manage DPI/resizing, and propagate key events so multi-object edits happen via dialog
// controls. [STATE] Stores popup dimensions, DPI-aware sizer, title text, and pointers to the panel/model/plater for re-use across popups.
// [EVENT] OnClose, OnText, OnSize, and `on_dpi_changed`/`on_sys_color_changed` ensure the dialog stays responsive when the app changes
// scaling or theme. [THREAD] The popup lifecycle is bound to the wxWidgets UI thread and DPI notifications already target that thread,
// so any backend refresh must dispatch to the UI thread before touching `ObjectTableDialog`. [UNITY] Replace with a UI Toolkit overlay
// window (Floating VisualElement) that hosts the `ObjectTablePanel` equivalent and uses `RenderTexture` or `Screen Space Overlay` to
// appear modal. [PORTING_HAZARD:P2] The dialog currently relies on wxWidgets lifecycle + DPIDialog hooks, so the Unity port must
// manage modal focus and DPI scaling manually.
class ObjectTableDialog : public GUI::DPIDialog
{
    const int POPUP_WIDTH  = FromDIP(512);
    const int POPUP_HEIGHT = FromDIP(1024);

    // wxPanel*             m_panel{ nullptr };
    wxBoxSizer*   m_top_sizer{nullptr};
    wxStaticText* m_static_title{nullptr};
    // wxTimer*             m_refresh_timer;
    ObjectTablePanel* m_obj_panel{nullptr};
    Model*            m_model{nullptr};
    Plater*           m_plater{nullptr};

public:
    ObjectTableDialog(wxWindow* parent, Plater* platerObj, Model* modelObj, wxSize maxSize);
    ~ObjectTableDialog();
    void Popup(int obj_idx = -1, int vol_idx = -1, wxPoint position = wxDefaultPosition);
    void OnClose(wxCloseEvent& evt);
    void OnText(wxKeyEvent& evt);
    void OnSize(wxSizeEvent& event);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;
};

}} // namespace Slic3r::GUI
#endif // slic3r_GUI_ObjectTable_hpp_
