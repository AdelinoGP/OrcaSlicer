#ifndef slic3r_ConfigManipulation_hpp_
#define slic3r_ConfigManipulation_hpp_

/*	 Class for validation config options
 *	 and update (enable/disable) IU components
 *
 *	 Used for config validation for global config (Print Settings Tab)
 *	 and local config (overrides options on sidebar)
 * */

#include "libslic3r/PrintConfig.hpp"
#include "Field.hpp"

namespace Slic3r {

class ModelConfig;
class ObjectBase;

namespace GUI {

/*
 [INTENT]
 ConfigManipulation is a controller for managing configuration inter-dependencies and UI state.
 It implements "business rules" for the GUI, such as:
 - Showing/hiding UI fields based on other settings (e.g., hide infill settings if density is 0).
 - Validating input ranges and showing warning dialogs.
 - Propagating configuration changes between global, plate, and object levels.

 [STATE]
 - m_applying_keys: Set of keys currently being modified to prevent re-entrancy issues.
 - is_msg_dlg_already_exist: Guard to prevent multiple modal warning dialogs from stacking.
 - cb_toggle_field / cb_toggle_line: Callbacks to the actual UI widgets to update their visibility/enabled state.

 [UNITY]
 This logic should be refactored into a "ConfigCoordinator" service or ScriptableObject.
 - UI Toolkit VisualElements should bind to this service to react to dependency changes.
 - Use a Data-Binding approach (e.g., Unity's UI Toolkit data binding or a custom Reactive pattern)
   to replace manual toggle_field/toggle_line calls.
 - Validation logic should be separate from the UI modal display logic.

 [PORTING_HAZARD:P2]
 The class is tightly coupled to wxWidgets modal dialogs (MessageDialog) and specific UI widget structures.
 The massive "toggle_print_fff_options" and "update_print_fff_config" methods contain hundreds of
 slicer-specific rules that must be ported carefully to maintain parity with OrcaSlicer's UX.
*/
class ConfigManipulation
{
    bool is_msg_dlg_already_exist{false};
    bool m_is_initialized_support_material_overhangs_queried{false};
    bool m_support_material_overhangs_queried{false};
    bool is_BBL_Printer{false};

    // function to loading of changed configuration
    std::function<void()>                                               load_config     = nullptr;
    std::function<void(const std::string&, bool toggle, int opt_index)> cb_toggle_field = nullptr;
    std::function<void(const std::string&, bool toggle, int opt_index)> cb_toggle_line  = nullptr;
    // callback to propagation of changed value, if needed
    std::function<void(const std::string&, const boost::any&)> cb_value_change = nullptr;
    // BBS: change local config to const DynamicPrintConfig
    const DynamicPrintConfig* local_config = nullptr;
    // ModelConfig* local_config = nullptr;
    wxWindow* m_msg_dlg_parent{nullptr};

    t_config_option_keys m_applying_keys;

public:
    ConfigManipulation(std::function<void()>                                               load_config,
                       std::function<void(const std::string&, bool toggle, int opt_index)> cb_toggle_field,
                       std::function<void(const std::string&, bool toggle, int opt_index)> cb_toggle_line,
                       std::function<void(const std::string&, const boost::any&)>          cb_value_change,
                       // BBS: change local config to DynamicPrintConfig
                       const DynamicPrintConfig* local_config   = nullptr,
                       wxWindow*                 msg_dlg_parent = nullptr)
        : load_config(load_config)
        , cb_toggle_field(cb_toggle_field)
        , cb_toggle_line(cb_toggle_line)
        , cb_value_change(cb_value_change)
        , m_msg_dlg_parent(msg_dlg_parent)
        , local_config(local_config)
    {}
    ConfigManipulation() {}

    ~ConfigManipulation()
    {
        load_config     = nullptr;
        cb_toggle_field = nullptr;
        cb_toggle_line  = nullptr;
        cb_value_change = nullptr;
    }

    bool is_applying() const;

    void                        apply(DynamicPrintConfig* config, DynamicPrintConfig* new_config);
    t_config_option_keys const& applying_keys() const;
    void                        toggle_field(const std::string& field_key, const bool toggle, int opt_index = -1);
    void                        toggle_line(const std::string& field_key, const bool toggle, int opt_index = -1);

    // FFF print
    void update_print_fff_config(DynamicPrintConfig* config, const bool is_global_config = false, const bool is_plate_config = false);
    void toggle_print_fff_options(DynamicPrintConfig* config, const bool is_global_config = false);
    void apply_null_fff_config(DynamicPrintConfig*                        config,
                               std::vector<std::string> const&            keys,
                               std::map<ObjectBase*, ModelConfig*> const& configs);

    // BBS: FFF filament nozzle temperature range
    void check_nozzle_recommended_temperature_range(DynamicPrintConfig* config);
    void check_nozzle_temperature_range(DynamicPrintConfig* config);
    void check_nozzle_temperature_initial_layer_range(DynamicPrintConfig* config);
    void check_filament_max_volumetric_speed(DynamicPrintConfig* config);
    void check_chamber_temperature(DynamicPrintConfig* config);
    void set_is_BBL_Printer(bool is_bbl_printer) { is_BBL_Printer = is_bbl_printer; };
    bool get_is_BBL_Printer() { return is_BBL_Printer; };
    // SLA print
    void update_print_sla_config(DynamicPrintConfig* config, const bool is_global_config = false);
    void toggle_print_sla_options(DynamicPrintConfig* config);

    bool is_initialized_support_material_overhangs_queried() { return m_is_initialized_support_material_overhangs_queried; }
    void initialize_support_material_overhangs_queried(bool queried)
    {
        m_is_initialized_support_material_overhangs_queried = true;
        m_support_material_overhangs_queried                = queried;
    }
    int show_spiral_mode_settings_dialog(bool is_object_config = false);

private:
    bool get_temperature_range(DynamicPrintConfig* config, int& range_low, int& range_high);
};

} // namespace GUI
} // namespace Slic3r

#endif /* slic3r_ConfigManipulation_hpp_ */
