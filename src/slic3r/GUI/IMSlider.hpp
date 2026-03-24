#ifndef slic3r_GUI_IMSlider_hpp_
#define slic3r_GUI_IMSlider_hpp_

#include "TickCode.hpp"
#include <imgui/imgui.h>
#include <wx/slider.h>

#include <set>

class wxMenu;
struct IMGUI_API ImRect;

namespace Slic3r {

using namespace CustomGCode;
class PrintObject;
class Layer;

namespace GUI {

/* For exporting GCode in GCodeWriter is used XYZF_NUM(val) = PRECISION(val, 3) for XYZ values.
 * So, let use same value as a permissible error for layer height.
 */
constexpr double epsilon() { return 0.0011; }

bool equivalent_areas(const double& bottom_area, const double& top_area);

// return true if color change was detected
bool check_color_change(PrintObject* object,
                        size_t       frst_layer_id,
                        size_t       layers_cnt,
                        bool         check_overhangs,
                        // what to do with detected color change
                        // and return true when detection have to be desturbed
                        std::function<bool(Layer*)> break_condition);

/* [STATE] Selected handle: lower/higher/undefined; Unity porters can mirror this with a VisualElement/Slider pair that tracks the active
 * thumb index for binding focus restoration. */
enum SelectedSlider { ssUndef = 0, ssLower = 1, ssHigher = 2 };

// [STATE] Mode switches adjust what overlays/backends render (regular slider, SLA preview, sequential FFF/G-code) and influence how
// render() paints ticks.
enum DrawMode {
    dmRegular,
    dmSlaPrint,
    dmSequentialFffPrint,
    dmSequentialGCodeView,
};

// [STATE] LabelType selects the metadata shown next to ticks (layer, height, time) so Unity can bind the same template fields.
enum LabelType {
    ltHeightWithLayer,
    ltHeight,
    ltEstimatedTime,
};

/* [INTENT] IMSlider exposes a dual-handle slider that drives preview-layer ranges, synchronizes extra tick metadata, and replicates the
 * wx/ImGui hybrid UI while also owning texture assets. [UNITY] Equivalent to a UI Toolkit VisualElement with two Thumb controls bound to a
 * ScriptableObject range model, plus a custom GraphicRaycaster overlay for label/tick rendering. [PORTING_HAZARD:P3] This shielded hybrid
 * uses wx menu bindings and ImGui helpers together, so Unity needs to reify the contextual popup logic separately. */
class IMSlider
{
public:
    // [INTENT] Bridge between wxWidgets/ImGui slider visuals and the slicing layer; owns textures, tick metadata, menu wiring, and slider
    // range state. [UNITY] Equivalent to a UI Toolkit `VisualElement` with dual thumbs bound to a ScriptableObject range model plus an
    // overlay for custom tick rendering.
    IMSlider(int lowerValue, int higherValue, int minValue, int maxValue, long style = wxSL_VERTICAL);

    // [OPENGL] Allocate GPU texture atlas, must be invoked on the GL thread prior to render.
    bool init_texture();

    ~IMSlider() {}

    int            GetMinValue() const { return m_min_value; }
    int            GetMaxValue() const { return m_max_value; }
    double         GetMinValueD() { return m_values.empty() ? 0. : m_values[m_min_value]; }
    double         GetMaxValueD() { return m_values.empty() ? 0. : m_values[m_max_value]; }
    int            GetLowerValue() const { return m_lower_value; }
    int            GetHigherValue() const { return m_higher_value; }
    int            GetActiveValue() const;
    double         GetLowerValueD() { return get_double_value(ssLower); }
    double         GetHigherValueD() { return get_double_value(ssHigher); }
    SelectedSlider GetSelection() { return m_selection; }

    // Set low and high slider position. If the span is non-empty, disable the "one layer" mode.
    void SetLowerValue(const int lower_val);
    void SetHigherValue(const int higher_val);
    void SetSelectionSpan(const int lower_val, const int higher_val);
    void SetMaxValue(const int max_value);
    void SetKoefForLabels(const double koef) { m_label_koef = koef; }
    // [UNITY] Mirror incoming values to a ScriptableObject-backed range so UI Toolkit slider thumbs stay in sync with slicing configuration.
    void SetSliderValues(const std::vector<double>& values);
    // [STATE] Alternate values cache the overhang-specific ramp so slider presets can show both computed and legacy curves simultaneously.
    void SetSliderAlternateValues(const std::vector<double>& values) { m_alternate_values = values; }

    Info GetTicksValues() const;
    // [STATE][EVENT][UNITY] Accepts TickCode metadata from the slicing engine so Unity can mirror the same overlays and tooltip commands.
    void SetTicksValues(const Info& custom_gcode_per_print_z);
    // [STATE][UNITY][PORTING_HAZARD:P3] Cache per-layer durations (plus the aggregate total) to keep tick tooltips and time overlays
    // synchronized; porters should expose this data via a UI Toolkit `VisualElement` list bound to a ScriptableObject timeline model
    // without altering the original time scaling.
    void SetLayersTimes(const std::vector<float>& layers_times, float total_time);
    void SetLayersTimes(const std::vector<double>& layers_times);

    void SetDrawMode(bool is_sequential_print);
    void SetDrawMode(DrawMode mode) { m_draw_mode = mode; }
    // [STATE] Draw mode toggles sequential visualization of prints, affecting label math and color bands in render().
    // BBS
    // [STATE] Extra style controls hover/pressed icons for one-layer toggle and requires syncing with ImGui widget config.
    void SetExtraStyle(long style) { m_extra_style = style; }
    // [STATE] Manipulation mode determines whether the slider drives a single extruder or multi-extruder preview.
    void SetManipulationMode(Mode mode) { m_mode = mode; }
    Mode GetManipulationMode() const { return m_mode; }
    // [UNCLEAR] Enables multi-extruder preview by locking slider interactions to one extruder, but the exact interplay with color change
    // locking is inherited from legacy wizard logic.
    // [STATE][PORTING_HAZARD:P3] `m_only_extruder` acts as a viewport clamp that Unity ports must enforce when the preview is limited to a
    // single extruder to avoid misaligned slider handles.
    void SetModeAndOnlyExtruder(const bool is_one_extruder_printed_model, const int only_extruder, bool can_change_color);
    // [STATE][UNITY] Stores the preview colors for each extruder so Unity can tint tick bands consistently with ImGui's legend.
    void SetExtruderColors(const std::vector<std::string>& extruder_colors);

    // [STATE] Flagged when a new slicing job replaces the preview so textures and cached ticks can refresh; mirror this by resetting the
    // VisualElement data source upon new print start.
    bool IsNewPrint();

    // [STATE][UNITY] Flips the disabled visual style so a Unity `RenderTexture` overlay can add an `is-disabled` class when preview data is
    // missing or restricted.
    void set_render_as_disabled(bool value) { m_render_as_disabled = value; }
    bool is_rendering_as_disabled() const { return m_render_as_disabled; }

    bool is_horizontal() const { return m_style == wxSL_HORIZONTAL; }
    bool is_one_layer() const { return m_is_one_layer; }
    bool is_lower_at_min() const { return m_lower_value == m_min_value; }
    bool is_higher_at_max() const { return m_higher_value == m_max_value; }
    bool is_full_span() const { return this->is_lower_at_min() && this->is_higher_at_max(); }

    void UseDefaultColors(bool def_colors_on) { m_ticks.set_default_colors(def_colors_on); }

    // [EVENT] Mouse wheel scrolls adjust slider range and may trigger deferred tick-change events.
    void on_mouse_wheel(wxMouseEvent& evt);
    // [EVENT][THREAD] Queue up tick-change notifications without blocking worker threads; UI poll happens via check_ticks_changed_event().
    void post_ticks_changed_event(Type type = Unknown);
    bool check_ticks_changed_event(Type type);
    // [STATE] Toggling one-layer mode forces m_is_one_layer and resets the range span when the user hits the menu action.
    bool switch_one_layer_mode();
    void show_go_to_layer(bool show) { m_show_go_to_layer_dialog = show; }

    // [OPENGL][THREAD] Draws slider bars, tick icons, and labels on the GL canvas every frame; Unity port should replicate with a
    // RenderTexture overlay and GraphicRaycaster interaction.
    bool render(int canvas_width, int canvas_height);

    // BBS update scroll value changed
    // [STATE] Dirty flag ensures we only re-render the slider when the underlying values change.
    bool is_dirty() { return m_dirty; }
    void set_as_dirty(bool dirty = true) { m_dirty = dirty; }
    // [THREAD] Background tasks read this flag to know when the UI still requires a tick-change notification.
    bool is_need_post_tick_event() { return m_is_need_post_tick_changed_event; }
    void reset_post_tick_event(bool val = false)
    {
        m_is_need_post_tick_changed_event = val;
        m_tick_change_event_type          = Type::Unknown;
    }
    Type get_post_tick_event_type() { return m_tick_change_event_type; }

    float m_scale = 1.0;
    // [STATE] Scale controls the pixel density for ImGui drawing; Unity should mirror this scale when rendering tick bands;
    // [PORTING_HAZARD:P3] the same multiplier must drive both the slider track and tick-label math so overlay geometry stays aligned.
    void set_scale(float scale = 1.0);
    // [STATE] Toggles between light/dark palettes affecting tick icons and tooltip legibility; [UNITY] porters should flip a UI Toolkit
    // class list and swap tint colors in a `StyleSheet` to match the ImGui toggle.
    void on_change_color_mode(bool is_dark);
    // [STATE][EVENT] Right-click menu availability toggles the contextual tick editing UX and gates the context menu so Unity's
    // `ContextualMenuManager` mirrors when tick editing should be available.
    void set_menu_enable(bool enable = true) { m_menu_enable = enable; }

protected:
    void add_custom_gcode(std::string custom_gcode);
    void add_code_as_tick(Type type, int selected_extruder = -1);
    void delete_tick(const TickCode& tick);
    void do_go_to_layer(size_t layer_number); // menu
    void correct_lower_value();
    void correct_higher_value();
    bool horizontal_slider(const char* str_id, int* v, int v_min, int v_max, const ImVec2& size, float scale = 1.0);
    void render_go_to_layer_dialog();                              // menu
    void render_input_custom_gcode(std::string custom_gcode = ""); // menu
    // [EVENT][UNITY][PORTING_HAZARD:P3] Draws the context menu entry that exposes tick editing commands via ImGui popups; Unity needs to
    // hook its `ContextualMenuManager` so menu commands map back to the slider data.
    void render_menu();
    // [EVENT][UNITY] Adds the "Create Tick" submenu anchored to the slider for fast adjustments; port this submenu into a UI Toolkit menu
    // anchored to the slider thumb.
    void render_add_menu(); // menu
    // [EVENT][UNITY] Paints the tick edit submenu triggered by right-clicking an existing tick, bound to TickCode data so Unity can
    // replicate the editor in a VisualElement popup.
    void render_edit_menu(const TickCode& tick); // menu
    void draw_background_and_groove(const ImRect& bg_rect, const ImRect& groove);
    void draw_colored_band(const ImRect& groove, const ImRect& slideable_region);
    void draw_custom_label_block(const ImVec2 anchor, Type type);
    void draw_ticks(const ImRect& slideable_region);
    void draw_tick_on_mouse_position(const ImRect& slideable_region);
    void show_tooltip(const TickCode& tick);      // menu
    void show_tooltip(const std::string tooltip); // menu
    bool vertical_slider(const char*     str_id,
                         int*            higher_value,
                         int*            lower_value,
                         std::string&    higher_label,
                         std::string&    lower_label,
                         int             v_min,
                         int             v_max,
                         const ImVec2&   size,
                         SelectedSlider& selection,
                         bool            one_layer_flag = false,
                         float           scale          = 1.0f);
    bool is_wipe_tower_layer(int tick) const;

private:
    // [INTENT] Build label text for a tick based on the requested label flavor (height/time). Unity port should keep this logic for tooltip
    // synchronization.
    std::string get_label(int tick, LabelType label_type = ltHeightWithLayer);
    double      get_double_value(const SelectedSlider& selection);
    int         get_tick_from_value(double value, bool force_lower_bound = false);
    float       get_pos_from_value(int v_min, int v_max, int value, const ImRect& rect);
    int         get_tick_near_point(int v_min, int v_max, const ImVec2& pt, const ImRect& rect);

    std::string get_color_for_tool_change_tick(std::set<TickCode>::const_iterator it) const;
    // Get active extruders for tick.
    // Means one current extruder for not existing tick OR
    // 2 extruders - for existing tick (extruder before ToolChangeCode and extruder of current existing tick)
    // Use those values to disable selection of active extruders
    std::array<int, 2> get_active_extruders_for_tick(int tick) const;

    // Use those values to disable selection of active extruders
    // [STATE] Dark-theme flag toggles tinted slider icons and label contrast for high-DPI rendering.
    bool m_is_dark = false;

    // [STATE] macOS slider metrics differ (rounded track, smaller handles) so we store this heuristic for asset selection.
    bool is_osx{false};
    int  m_min_value;
    int  m_max_value;
    int  m_lower_value;
    int  m_higher_value;
    int  m_one_layer_value; // ORCA
    bool m_dirty = false;

    bool m_render_as_disabled{false};

    SelectedSlider m_selection;
    bool           m_is_one_layer             = false;
    bool           m_menu_enable              = true;  // menu
    bool           m_show_menu                = false; // menu
    bool           m_show_custom_gcode_window = false; // menu
    bool           m_show_go_to_layer_dialog  = false; // menu
    bool           m_force_mode_apply         = true;
    bool           m_is_wipe_tower            = false; // This flag indicates that there is multiple extruder print with wipe tower
    bool           m_is_spiral_vase           = false;

    /* BBS slider images */
    // [OPENGL][STATE][UNITY] GPU texture handles for the slider badges/toggles; Unity should map them to cached `Sprite` assets and apply
    // them to the `VisualElement` background states, keeping separate idle/hover variants.
    void* m_one_layer_on_id;
    void* m_one_layer_on_hover_id;
    void* m_one_layer_off_id;
    void* m_one_layer_off_hover_id;
    void* m_one_layer_on_light_id;
    void* m_one_layer_on_hover_light_id;
    void* m_one_layer_off_light_id;
    void* m_one_layer_off_hover_light_id;
    void* m_one_layer_on_dark_id;
    void* m_one_layer_on_hover_dark_id;
    void* m_one_layer_off_dark_id;
    void* m_one_layer_off_hover_dark_id;
    void* m_pause_icon_id;
    void* m_custom_icon_id;
    void* m_delete_icon_id;

    DrawMode m_draw_mode     = dmRegular;
    Mode     m_mode          = SingleExtruder;
    int      m_only_extruder = -1;

    long  m_style;
    long  m_extra_style;
    float m_label_koef{1.0};

    float m_zero_layer_height = 0.0f;
    // [STATE] Value ramp derived from preview slice heights used by render() to draw tick positions.
    std::vector<double> m_values;
    // [STATE][EVENT] Precomputed TickCode metadata and color cues that drive context menu entries.
    TickCodeInfo m_ticks;
    // [STATE] Layer durations align slider labels with estimated time overlays.
    std::vector<double>      m_layers_times;
    std::vector<double>      m_layers_values;
    std::vector<std::string> m_extruder_colors;
    // [STATE] Prevents color mode toggling when the preview action is locked by the active profile.
    bool m_can_change_color;
    // [STATE] Stores the last printed object indices string used for menu tooltips, keeping tick edits mapped to the correct objects in
    // both wxWidgets and Unity menus.
    std::string m_print_obj_idxs;
    // [THREAD] Flag bubbled from background tasks to drive tick-change event posting.
    bool m_is_need_post_tick_changed_event{false};
    Type m_tick_change_event_type;

    std::vector<double> m_alternate_values;

    // [STATE][EVENT] Buffers the menu text for custom G-code and the Go-to-layer input so Unity's text fields can preserve typed values
    // while menus stay open.
    char m_custom_gcode[1024] = {0}; // menu
    char m_layer_number[64]   = {0}; // menu
};

} // namespace GUI

} // namespace Slic3r

#endif // slic3r_GUI_IMSlider_hpp_
