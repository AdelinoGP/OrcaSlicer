#include "CalibUtils.hpp"
#include "../GUI/I18N.hpp"
#include "../GUI/GUI_App.hpp"
#include "../GUI/DeviceCore/DevStorage.h"
#include "../GUI/DeviceManager.hpp"
#include "../GUI/Jobs/ProgressIndicator.hpp"
#include "../GUI/PartPlate.hpp"
#include "libslic3r/CutUtils.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Utils.hpp"

#include "libslic3r/Model.hpp"
#include "slic3r/GUI/Jobs/BoostThreadWorker.hpp"
#include "slic3r/GUI/Jobs/PlaterWorker.hpp"
#include "../GUI/MsgDialog.hpp"
#include "libslic3r/FlushVolCalc.hpp"

#include "../GUI/DeviceCore/DevConfig.h"
#include "../GUI/DeviceCore/DevExtruderSystem.h"
#include "../GUI/DeviceCore/DevManager.h"
#include "../GUI/DeviceCore/DevStorage.h"
#include "libslic3r/FlushVolCalc.hpp"
#include "../GUI/Plater.hpp"

namespace Slic3r {
namespace GUI {
const double MIN_PA_K_VALUE = 0.0;
const double MAX_PA_K_VALUE = 2.0;

std::unique_ptr<Worker> CalibUtils::print_worker;

// Built lazily so temporary_dir() is read on first use, after set_temporary_dir()
// has run at startup (it isolates the temp root per user to avoid cross-user collisions).
static const std::string& calib_temp_dir()
{
    static const std::string dir = temporary_dir() + "/calib";
    return dir;
}
static std::string calib_temp_file(const std::string& name) { return calib_temp_dir() + "/" + name; }

static const std::string gcode_filename  = "temp.gcode";
static const std::string model_filename  = "test.3mf";
static const std::string config_filename = "test_config.3mf";

static std::string MachineBedTypeString[7] = {
    "auto",
    "suprtack",
    "pc",
    "ep",
    "pei",
    "pte",
    "pct",
};

static wxString nozzle_not_set_text = _L("The printer nozzle information has not been set.\nPlease configure it before proceeding with the calibration.");
static wxString nozzle_volume_type_not_match_text = _L("The nozzle type does not match the actual printer nozzle type.\nPlease click the Sync button above and restart the calibration.");

std::vector<std::string> not_support_auto_pa_cali_filaments = {
    "GFU03", // TPU 90A
    "GFU04"  // TPU 85A
};

void get_default_k_n_value(const std::string &filament_id, float &k, float &n)
{
    if (filament_id.compare("GFU01") == 0) {
        /* TPU 95A */
        k = 0.25;
        n = 1.0;
    } else if (filament_id.compare("GFU03") == 0) {
        /* TPU 90A */
        k = 0.35;
        n = 1.0;
    } else if (filament_id.compare("GFU04") == 0) {
        /* TPU 85A */
        k = 0.65;
        n = 1.0;
    } else if (filament_id.compare("GFG00") == 0 || filament_id.compare("GFG01") == 0 || filament_id.compare("GFG60") == 0 || filament_id.compare("GFL06") == 0 ||
               filament_id.compare("GFL55") == 0 || filament_id.compare("GFG99") == 0 || filament_id.compare("GFG98") == 0 || filament_id.compare("GFG97") == 0 ||
               filament_id.compare("GFG50") == 0 || filament_id.compare("GFU02") == 0 || filament_id.compare("GFU98") == 0 || filament_id.compare("GFS00") == 0 ||
               filament_id.compare("GFS02") == 0) {
        /* 0.04 filaments */
        k = 0.04;
        n = 1.0;
    } else {
        /* other */
        k = 0.02;
        n = 1.0;
    }
}

wxString get_nozzle_volume_type_name(NozzleVolumeType type)
{
    if (NozzleVolumeType::nvtStandard == type) {
        return _L("Standard");
    } else if (NozzleVolumeType::nvtHighFlow == type) {
        return _L("High Flow");
    } else if (NozzleVolumeType::nvtHybrid == type) {
        return _L("Hybrid");
    } else if (NozzleVolumeType::nvtTPUHighFlow == type) {
        return _L("TPU High Flow");
    }
    return wxString();
}

void update_speed_parameter( const std::string& key)
{
    auto preset_bundle   = wxGetApp().preset_bundle;
    auto& printer_config  = preset_bundle->printers.get_edited_preset().config;
    auto& filament_config = preset_bundle->filaments.get_edited_preset().config;
    auto& print_config    = preset_bundle->prints.get_edited_preset().config;

    int extruder_nums = preset_bundle->get_printer_extruder_count();
    std::vector<int> extruder_types      = printer_config.option<ConfigOptionEnumsGeneric>("extruder_type")->values;
    std::vector<int> nozzle_volume_types = preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;

    float nozzle_diameter = printer_config.option<ConfigOptionFloats>("nozzle_diameter")->values[0];
    float layer_height = print_config.option<ConfigOptionFloat>("layer_height")->value;
    float line_width = print_config.get_abs_value("line_width", nozzle_diameter);
    if (line_width <= 0.) line_width = Flow::auto_extrusion_width(frPerimeter, nozzle_diameter);

    Flow flow = Flow(line_width, layer_height, nozzle_diameter);

    for (size_t i = 0; i < extruder_nums; ++i) {
        int index = get_index_for_extruder_parameter(filament_config, "filament_max_volumetric_speed", i, ExtruderType(extruder_types[i]), NozzleVolumeType(nozzle_volume_types[i]));
        double filament_max_volumetric_speed = filament_config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(index);
        double max_speed = filament_max_volumetric_speed / flow.mm3_per_mm();

        index = get_index_for_extruder_parameter(print_config, key, i, ExtruderType(extruder_types[i]), NozzleVolumeType(nozzle_volume_types[i]));
        ConfigOptionFloatsNullable *speed_opt = print_config.option<ConfigOptionFloatsNullable>(key);
        speed_opt->values[index] = max_speed;
    }
}

std::vector<double> generate_max_speed_parameter_value(const std::string &key, const bool linear, const int pass)
{
    auto  preset_bundle   = wxGetApp().preset_bundle;
    auto &printer_config  = preset_bundle->printers.get_edited_preset().config;
    auto &filament_config = preset_bundle->filaments.get_edited_preset().config;
    auto &print_config    = preset_bundle->prints.get_edited_preset().config;

    int              extruder_nums       = preset_bundle->get_printer_extruder_count();
    std::vector<int> extruder_types      = printer_config.option<ConfigOptionEnumsGeneric>("extruder_type")->values;
    std::vector<int> nozzle_volume_types = preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;

    float nozzle_diameter = printer_config.option<ConfigOptionFloats>("nozzle_diameter")->values[0];
    float layer_height    = print_config.option<ConfigOptionFloat>("layer_height")->value;
    float line_width      = print_config.get_abs_value("line_width", nozzle_diameter);

    Flow flow = Flow(line_width, layer_height, nozzle_diameter);

    std::vector<double> speed_values;
    speed_values.reserve(extruder_nums * nozzle_volume_types.size());

    for (size_t i = 0; i < extruder_nums; ++i) {
        int    index                         = get_index_for_extruder_parameter(filament_config, "filament_max_volumetric_speed", i, ExtruderType(extruder_types[i]),
                                                                                NozzleVolumeType(nozzle_volume_types[i]));
        double filament_max_volumetric_speed = filament_config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(index);
        double cur_flowrate                  = filament_config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(index);
        double max_speed                     = linear ? filament_max_volumetric_speed / (flow.mm3_per_mm() * (cur_flowrate + (pass == 2 ? 0.035 : 0.05)) / cur_flowrate) :
                                                        filament_max_volumetric_speed / (flow.mm3_per_mm() * (pass == 1 ? 1.2 : 1));

        index = get_index_for_extruder_parameter(print_config, key, i, ExtruderType(extruder_types[i]), NozzleVolumeType(nozzle_volume_types[i]));
        ConfigOptionFloatsNullable *speed_opt = print_config.option<ConfigOptionFloatsNullable>(key);
        double speed_value = std::floor(std::min(speed_opt->values[index], max_speed));
        for (size_t v_id = 0; v_id < nozzle_volume_types.size(); ++v_id) {
            speed_values.emplace_back(speed_value);
        }
    }
    return speed_values;
}

static int get_physical_extruder_idx(std::vector<int> physical_extruder_maps, int extruder_id)
{
    for (size_t index = 0; index < physical_extruder_maps.size(); ++index) {
        if (physical_extruder_maps[index] == extruder_id) {
            return index;
        }
    }
    return extruder_id;
}

void get_tray_ams_and_slot_id(MachineObject* obj, int in_tray_id, int &ams_id, int &slot_id, int &tray_id)
{
    assert(obj);
    if (!obj)
        return;

    if (in_tray_id == VIRTUAL_TRAY_MAIN_ID || in_tray_id == VIRTUAL_TRAY_DEPUTY_ID) {
        ams_id  = in_tray_id;
        slot_id = 0;
        tray_id = ams_id;
        if (!obj->is_enable_np)
            tray_id = VIRTUAL_TRAY_DEPUTY_ID;
    } else {
        ams_id  = in_tray_id / 4;
        slot_id = in_tray_id % 4;
        tray_id = in_tray_id;
    }
}

std::string get_calib_mode_name(CalibMode cali_mode, int stage)
{
    switch(cali_mode) {
    case CalibMode::Calib_PA_Line:
        return "pa_line_calib_mode";
    case CalibMode::Calib_Auto_PA_Line:
        return "auto_pa_line_calib_mode";
    case CalibMode::Calib_PA_Pattern:
        return "pa_pattern_calib_mode";
    case CalibMode::Calib_Flow_Rate:
        if (stage == 1)
            return "flow_rate_coarse_calib_mode";
        else if (stage == 2)
            return "flow_rate_fine_calib_mode";
        else
            return "flow_rate_coarse_calib_mode";
    case CalibMode::Calib_Temp_Tower:
        return "temp_tower_calib_mode";
    case CalibMode::Calib_Vol_speed_Tower:
        return "vol_speed_tower_calib_mode";
    case CalibMode::Calib_VFA_Tower:
        return "vfa_tower_calib_mode";
    case CalibMode::Calib_Retraction_tower:
        return "retration_tower_calib_mode";
    case CalibMode::Calib_Input_shaping_freq:
        return "input_shaping_freq_calib_mode";
    case CalibMode::Calib_Input_shaping_damp:
        return "input_shaping_damp_calib_mode";
    case CalibMode::Calib_Cornering:
        return "cornering_calib_mode";
    default:
        assert(false);
        return "";
    }
}

static wxString to_wstring_name(std::string name)
{
    if (name == "hardened_steel") {
        return _L("Hardened Steel");
    } else if (name == "stainless_steel") {
        return _L("Stainless Steel");
    } else if (name == "tungsten_carbide") {
        return _L("Tungsten Carbide");
    } else if (name == "brass") {
        return _L("Brass");
    }

    return wxEmptyString;
}

static bool is_same_nozzle_type(const DynamicPrintConfig &full_config, const MachineObject *obj, wxString& error_msg)
{
    if (obj == nullptr)
        return true;

    NozzleType nozzle_type = obj->GetExtderSystem()->GetNozzleType(0);
    int printer_nozzle_hrc = Print::get_hrc_by_nozzle_type(nozzle_type);

    if (full_config.has("required_nozzle_HRC")) {
        int filament_nozzle_hrc = full_config.opt_int("required_nozzle_HRC", 0);
        if (abs(filament_nozzle_hrc) > abs(printer_nozzle_hrc)) {
            BOOST_LOG_TRIVIAL(info) << "filaments hardness mismatch:  printer_nozzle_hrc = " << printer_nozzle_hrc << ", filament_nozzle_hrc = " << filament_nozzle_hrc;
            std::string filament_type = full_config.opt_string("filament_type", 0);
            error_msg = wxString::Format(_L("Printing %1s material with %2s nozzle may cause nozzle damage."), filament_type, to_wstring_name(NozzleTypeEumnToStr[obj->GetExtderSystem()->GetNozzleType(0)]));
            error_msg += "\n";

            MessageDialog msg_dlg(nullptr, error_msg, wxEmptyString, wxICON_WARNING | wxOK | wxCANCEL);
            auto          result = msg_dlg.ShowModal();
            if (result == wxID_OK) {
                error_msg.clear();
                return true;
            } else {
                error_msg.clear();
                return false;
            }
        }
    }

    return true;
}

static bool check_nozzle_diameter_and_type(const DynamicPrintConfig &full_config, wxString& error_msg)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        error_msg = _L("Need select printer");
        return false;
    }

    MachineObject *obj = dev->get_selected_machine();
    if (obj == nullptr) {
        error_msg = _L("Need select printer");
        return false;
    }

    if (!Slic3r::GUI::wxGetApp().plater()->check_printer_initialized(obj))
        return false;

    // P1P/S
    if (obj->GetExtderSystem()->GetNozzleType(0) == NozzleType::ntUndefine)
        return true;

    if (!is_same_nozzle_type(full_config, obj, error_msg))
        return false;

    return true;
}

static void init_multi_extruder_params_for_cali(DynamicPrintConfig& config, const CalibInfo& calib_info)
{
    int extruder_count = 1;
    auto nozzle_diameters_opt = dynamic_cast<const ConfigOptionFloats*>(config.option("nozzle_diameter"));
    if (nozzle_diameters_opt != nullptr) {
        extruder_count = (int)(nozzle_diameters_opt->size());
    }
    std::vector<int>& nozzle_volume_types =  dynamic_cast<ConfigOptionEnumsGeneric*>(config.option("nozzle_volume_type", true))->values;
    nozzle_volume_types.clear();
    nozzle_volume_types.resize(extruder_count, (int)calib_info.nozzle_volume_type);

    std::vector<int> physical_extruder_maps = dynamic_cast<ConfigOptionInts*>(config.option("physical_extruder_map", true))->values;
    int extruder_id = calib_info.extruder_id;
    for (size_t index = 0; index < extruder_count; ++index) {
        if (physical_extruder_maps[index] == extruder_id)
        {
            nozzle_volume_types[index] = (int) calib_info.nozzle_volume_type;
            extruder_id = index + 1;
            break;
        }
    }

    int num_filaments = 1;
    auto filament_colour_opt = dynamic_cast<const ConfigOptionStrings*>(config.option("filament_colour"));
    if (filament_colour_opt != nullptr) {
        num_filaments = (int)(filament_colour_opt->size());
    }
    std::vector<int>& filament_maps = config.option<ConfigOptionInts>("filament_map", true)->values;
    filament_maps.clear();
    filament_maps.resize(num_filaments, extruder_id);

    config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode", true)->value = FilamentMapMode::fmmManual;

}


CalibMode CalibUtils::get_calib_mode_by_name(const std::string name, int& cali_stage)
{
    if (name == "pa_line_calib_mode") {
        cali_stage = 0;
        return CalibMode::Calib_PA_Line;
    }
    else if (name == "pa_pattern_calib_mode") {
        cali_stage = 1;
        return CalibMode::Calib_PA_Line;
    }
    else if (name == "auto_pa_line_calib_mode") {
        cali_stage = 2;
        return CalibMode::Calib_PA_Line;
    }
    else if (name == "flow_rate_coarse_calib_mode") {
        cali_stage = 1;
        return CalibMode::Calib_Flow_Rate;
    }
    else if (name == "flow_rate_fine_calib_mode") {
        cali_stage = 2;
        return CalibMode::Calib_Flow_Rate;
    }
    else if (name == "temp_tower_calib_mode")
        return CalibMode::Calib_Temp_Tower;
    else if (name == "vol_speed_tower_calib_mode")
        return CalibMode::Calib_Vol_speed_Tower;
    else if (name == "vfa_tower_calib_mode")
        return CalibMode::Calib_VFA_Tower;
    else if (name == "retration_tower_calib_mode")
        return CalibMode::Calib_Retraction_tower;
    else if (name == "input_shaping_freq_calib_mode")
        return CalibMode::Calib_Input_shaping_freq;
    else if (name == "input_shaping_damp_calib_mode")
        return CalibMode::Calib_Input_shaping_damp;
    else if (name == "cornering_calib_mode")
        return CalibMode::Calib_Cornering;
    return CalibMode::Calib_None;
}

bool CalibUtils::validate_input_name(wxString name)
{
    if (name.Length() > 40) {
        MessageDialog msg_dlg(nullptr, _L("The name cannot exceed 40 characters."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    name.erase(std::remove(name.begin(), name.end(), L' '), name.end());
    if (name.IsEmpty()) {
        MessageDialog msg_dlg(nullptr, _L("The name cannot be empty."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    return true;
}

bool CalibUtils::validate_input_k_value(wxString k_text, float* output_value)
{
    float default_k = 0.0f;
    if (k_text.IsEmpty()) {
        *output_value = default_k;
        return false;
    }

    double k_value = 0.0;
    try {
        if(!k_text.ToDouble(&k_value))
            return false;
    }
    catch (...) {
        ;
    }

    if (k_value <= MIN_PA_K_VALUE || k_value >= MAX_PA_K_VALUE) {
        *output_value = default_k;
        return false;
    }

    *output_value = k_value;
    return true;
};

bool CalibUtils::validate_input_flow_ratio(wxString flow_ratio, float* output_value) {
    float default_flow_ratio = 1.0f;

    if (flow_ratio.IsEmpty()) {
        *output_value = default_flow_ratio;
        return false;
    }

    double flow_ratio_value = 0.0;
    try {
        flow_ratio.ToDouble(&flow_ratio_value);
    }
    catch (...) {
        ;
    }

    if (flow_ratio_value <= 0.0 || flow_ratio_value >= 2.0) {
        *output_value = default_flow_ratio;
        return false;
    }

    *output_value = flow_ratio_value;
    return true;
}

static void cut_model(Model &model, double z, ModelObjectCutAttributes attributes)
{
    size_t obj_idx = 0;
    size_t instance_idx = 0;
    if (!attributes.has(ModelObjectCutAttribute::KeepUpper) && !attributes.has(ModelObjectCutAttribute::KeepLower))
        return;

    auto* object = model.objects[0];

    const Vec3d instance_offset = object->instances[instance_idx]->get_offset();
    Cut         cut(object, instance_idx, Geometry::translation_transform(z * Vec3d::UnitZ() - instance_offset), attributes);
    const auto  new_objects = cut.perform_with_plane();
    model.delete_object(obj_idx);

    for (ModelObject *model_object : new_objects) {
        auto *object = model.add_object(*model_object);
        object->sort_volumes(true);
        std::string object_name = object->name.empty() ? fs::path(object->input_file).filename().string() : object->name;
        object->ensure_on_bed();
    }
}

static void read_model_from_file(const std::string& input_file, Model& model)
{
    LoadStrategy              strategy = LoadStrategy::LoadModel;
    ConfigSubstitutionContext config_substitutions{ForwardCompatibilitySubstitutionRule::Enable};
    int                       plate_to_slice = 0;

    bool                  is_bbl_3mf;
    Semver                file_version;
    DynamicPrintConfig    config;
    PlateDataPtrs         plate_data_src;
    std::vector<Preset *> project_presets;

    model = Model::read_from_file(input_file, &config, &config_substitutions, strategy, &plate_data_src, &project_presets,
        &is_bbl_3mf, &file_version, nullptr, nullptr, nullptr, plate_to_slice);

    model.add_default_instances();

    const std::string extension      = fs::path(input_file).extension().string();
    const bool        is_project_file = extension == ".3mf" || extension == ".3MF" || extension == ".amf" || extension == ".AMF";
    for (auto object : model.objects) {
        if (!is_project_file)
            object->center_around_origin(false);
        object->ensure_on_bed(is_project_file);
    }
}

std::array<Vec3d, 4> get_cut_plane_points(const BoundingBoxf3 &bbox, const double &cut_height)
{
    std::array<Vec3d, 4> plane_pts;
    plane_pts[0] = Vec3d(bbox.min(0), bbox.min(1), cut_height);
    plane_pts[1] = Vec3d(bbox.max(0), bbox.min(1), cut_height);
    plane_pts[2] = Vec3d(bbox.max(0), bbox.max(1), cut_height);
    plane_pts[3] = Vec3d(bbox.min(0), bbox.max(1), cut_height);
    return plane_pts;
}

void CalibUtils::calib_PA(const X1CCalibInfos& calib_infos, int mode, wxString& error_message)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    if (calib_infos.calib_datas.size() > 0) {
        if (!check_printable_status_before_cali(obj_, calib_infos, error_message))
            return;
        obj_->command_start_pa_calibration(calib_infos, mode);
    }
}

void CalibUtils::emit_get_PA_calib_results(float nozzle_diameter)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_get_pa_calibration_result(nozzle_diameter);
}

bool CalibUtils::get_PA_calib_results(std::vector<PACalibResult>& pa_calib_results)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return false;

    pa_calib_results = obj_->pa_calib_results;
    return pa_calib_results.size() > 0;
}

void CalibUtils::emit_get_PA_calib_infos(const PACalibExtruderInfo &cali_info)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_get_pa_calibration_tab(cali_info);
}

bool CalibUtils::get_PA_calib_tab(std::vector<PACalibResult> &pa_calib_infos)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return false;

    if (obj_->has_get_pa_calib_tab) {
        pa_calib_infos.assign(obj_->pa_calib_tab.begin(), obj_->pa_calib_tab.end());
    }
    return obj_->has_get_pa_calib_tab;
}

void CalibUtils::set_PA_calib_result(const std::vector<PACalibResult> &pa_calib_values, bool is_auto_cali)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_set_pa_calibration(pa_calib_values, is_auto_cali);
}

void CalibUtils::select_PA_calib_result(const PACalibIndexInfo& pa_calib_info)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->commnad_select_pa_calibration(pa_calib_info);
}

void CalibUtils::delete_PA_calib_result(const PACalibIndexInfo& pa_calib_info)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_delete_pa_calibration(pa_calib_info);
}

void CalibUtils::calib_flowrate_X1C(const X1CCalibInfos& calib_infos, wxString& error_message)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    if (calib_infos.calib_datas.size() > 0) {
         if (!check_printable_status_before_cali(obj_, calib_infos, error_message))
            return;
        obj_->command_start_flow_ratio_calibration(calib_infos);
    }
    else {
        BOOST_LOG_TRIVIAL(info) << "flow_rate_cali: auto | send info | cali_datas is empty.";
    }
}

void CalibUtils::emit_get_flow_ratio_calib_results(float nozzle_diameter)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_get_flow_ratio_calibration_result(nozzle_diameter);
}

bool CalibUtils::get_flow_ratio_calib_results(std::vector<FlowRatioCalibResult>& flow_ratio_calib_results)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return false;

    flow_ratio_calib_results = obj_->flow_ratio_results;
    return flow_ratio_calib_results.size() > 0;
}

bool CalibUtils::calib_flowrate(int pass, const CalibInfo &calib_info, wxString &error_message)
{
    // F11 (native-slicing rip-out, fork ticket ticket-008 sec.2): the
    // slice-and-upload calibration path (Print::process / export_gcode via
    // process_and_store_3mf, and the CalibPressureAdvance* generators) was
    // removed. Signature retained for the calibration UI/wizard callers;
    // returns a not-implemented result.
    error_message = _L("Calibration is not supported in this build.");
    return false;
}

void CalibUtils::calib_pa_pattern(const CalibInfo &calib_info, Model& model)
{
    // F11 (native-slicing rip-out, fork ticket ticket-008 sec.2):
    // calibration G-code generation was removed. Signature retained; no-op.
}

void CalibUtils::set_for_auto_pa_model_and_config(const std::vector<CalibInfo> &calib_infos, DynamicPrintConfig &full_config, Model &model)
{
    // F11 (native-slicing rip-out, fork ticket ticket-008 sec.2):
    // calibration G-code generation was removed. Signature retained; no-op.
}

bool CalibUtils::calib_generic_auto_pa_cali(const std::vector<CalibInfo> &calib_infos, wxString &error_message)
{
    // F11 (native-slicing rip-out, fork ticket ticket-008 sec.2): the
    // slice-and-upload calibration path (Print::process / export_gcode via
    // process_and_store_3mf, and the CalibPressureAdvance* generators) was
    // removed. Signature retained for the calibration UI/wizard callers;
    // returns a not-implemented result.
    error_message = _L("Calibration is not supported in this build.");
    return false;
}

bool CalibUtils::calib_generic_PA(const CalibInfo &calib_info, wxString &error_message)
{
    // F11 (native-slicing rip-out, fork ticket ticket-008 sec.2): the
    // slice-and-upload calibration path (Print::process / export_gcode via
    // process_and_store_3mf, and the CalibPressureAdvance* generators) was
    // removed. Signature retained for the calibration UI/wizard callers;
    // returns a not-implemented result.
    error_message = _L("Calibration is not supported in this build.");
    return false;
}

void CalibUtils::calib_temptue(const CalibInfo &calib_info, wxString &error_message)
{
    // F11 (native-slicing rip-out, fork ticket ticket-008 sec.2): the
    // slice-and-upload calibration path (Print::process / export_gcode via
    // process_and_store_3mf, and the CalibPressureAdvance* generators) was
    // removed. Signature retained for the calibration UI/wizard callers;
    // returns a not-implemented result.
    error_message = _L("Calibration is not supported in this build.");
}

void CalibUtils::calib_max_vol_speed(const CalibInfo &calib_info, wxString &error_message)
{
    // F11 (native-slicing rip-out, fork ticket ticket-008 sec.2): the
    // slice-and-upload calibration path (Print::process / export_gcode via
    // process_and_store_3mf, and the CalibPressureAdvance* generators) was
    // removed. Signature retained for the calibration UI/wizard callers;
    // returns a not-implemented result.
    error_message = _L("Calibration is not supported in this build.");
}

void CalibUtils::calib_VFA(const CalibInfo &calib_info, wxString &error_message)
{
    // F11 (native-slicing rip-out, fork ticket ticket-008 sec.2): the
    // slice-and-upload calibration path (Print::process / export_gcode via
    // process_and_store_3mf, and the CalibPressureAdvance* generators) was
    // removed. Signature retained for the calibration UI/wizard callers;
    // returns a not-implemented result.
    error_message = _L("Calibration is not supported in this build.");
}

void CalibUtils::calib_retraction(const CalibInfo &calib_info, wxString &error_message)
{
    // F11 (native-slicing rip-out, fork ticket ticket-008 sec.2): the
    // slice-and-upload calibration path (Print::process / export_gcode via
    // process_and_store_3mf, and the CalibPressureAdvance* generators) was
    // removed. Signature retained for the calibration UI/wizard callers;
    // returns a not-implemented result.
    error_message = _L("Calibration is not supported in this build.");
}

bool CalibUtils::is_support_auto_pa_cali(std::string filament_id)
{
    auto iter = std::find(not_support_auto_pa_cali_filaments.begin(), not_support_auto_pa_cali_filaments.end(), filament_id);
    if (iter != not_support_auto_pa_cali_filaments.end()) {
        return false;
    }
    return true;
}

int CalibUtils::get_selected_calib_idx(const std::vector<PACalibResult> &pa_calib_values, int cali_idx) {
    for (int i = 0; i < pa_calib_values.size(); ++i) {
        if(pa_calib_values[i].cali_idx == cali_idx)
            return i;
    }
    return -1;
}

bool CalibUtils::get_pa_k_n_value_by_cali_idx(const MachineObject *obj, int cali_idx, float &out_k, float &out_n) {
    if (!obj)
        return false;

    for (auto pa_calib_info : obj->pa_calib_tab) {
        if (pa_calib_info.cali_idx == cali_idx) {
            out_k = pa_calib_info.k_value;
            out_n = pa_calib_info.n_coef;
            return true;
        }
    }
    return false;
}

bool CalibUtils::check_printable_status_before_cali(const MachineObject *obj, const X1CCalibInfos &cali_infos, wxString &error_message)
{
    if (!obj) {
        error_message = _L("Need select printer");
        return false;
    }

    for (const auto& cali_info : cali_infos.calib_datas) {
        if (cali_infos.cali_mode == CalibMode::Calib_PA_Line && !is_support_auto_pa_cali(cali_info.filament_id)) {
            error_message = _L("TPU 90A/TPU 85A is too soft and does not support automatic Flow Dynamics calibration.");
            return false;
        }
    }

    bool  is_multi_extruder = obj->is_multi_extruders();

    for (const auto &cali_info : cali_infos.calib_datas) {
        wxString name = "";
        if (is_multi_extruder) { name = cali_info.extruder_id == MAIN_EXTRUDER_ID ? _L("right") + " ": _L("left") + " "; }

        float cali_diameter = cali_info.nozzle_diameter;
        int   extruder_id   = cali_info.extruder_id;
        if (extruder_id >= obj->GetExtderSystem()->GetTotalExtderSize()) {
            error_message = _L("The number of printer extruders and the printer selected for calibration does not match.");
            return false;
        }


        if (is_approx(double(cali_info.nozzle_diameter), 0.2) && !obj->is_series_x()) {
            error_message = wxString::Format(_L("The nozzle diameter of %s extruder is 0.2mm which does not support automatic Flow Dynamics calibration."), name);
            return false;
        }

        
        NozzleFlowType nozzle_volume_type = obj->GetExtderSystem()->GetNozzleFlowType(extruder_id);
        if (!obj->GetExtderSystem()->NozzleDiameterMatchesOrUnknown(extruder_id, cali_info.nozzle_diameter)) {
            if (is_multi_extruder)
                error_message = wxString::Format(_L("The currently selected nozzle diameter of %s extruder does not match the actual nozzle diameter.\n"
                               "Please click the Sync button above and restart the calibration."), name);
            else
                error_message = _L("The nozzle diameter does not match the actual printer nozzle diameter.\n"
                               "Please click the Sync button above and restart the calibration.");
            return false;
        }

        if (nozzle_volume_type == NozzleFlowType::NONE_FLOWTYPE) {
            if (is_multi_extruder)
                error_message = wxString::Format(_L("Printer %s nozzle information has not been set. Please configure it before proceeding with the calibration."), name);
            else
                error_message = nozzle_not_set_text;
            return false;
        }

        if (NozzleVolumeType(nozzle_volume_type - 1) != cali_info.nozzle_volume_type) {
            if (is_multi_extruder)
                error_message = wxString::Format(_L("The currently selected nozzle type of %s extruder does not match the actual printer nozzle type.\n"
                                                "Please click the Sync button above and restart the calibration."), name);
            else
                error_message = nozzle_volume_type_not_match_text;
            return false;
        }
    }
    return true;
}

bool CalibUtils::check_printable_status_before_cali(const MachineObject *obj, const std::vector<CalibInfo> &cali_infos, wxString &error_message)
{
    if (!obj) {
        error_message = _L("Need select printer");
        return false;
    }

    if (cali_infos.empty())
        return true;

    bool is_multi_extruder = obj->is_multi_extruders();

    for (const auto &cali_info : cali_infos) {
        wxString name = "";
        if (is_multi_extruder) { name = cali_info.extruder_id == MAIN_EXTRUDER_ID ? _L("right") + " " : _L("left") + " "; }

        if (cali_info.params.mode == CalibMode::Calib_Auto_PA_Line && !is_support_auto_pa_cali(cali_info.filament_prest->filament_id)) {
            error_message = _L("TPU 90A/TPU 85A is too soft and does not support automatic Flow Dynamics calibration.");
            return false;
        }


        if (is_approx(double(cali_info.nozzle_diameter), 0.2) && !obj->is_series_x()) {
            error_message = wxString::Format(_L("The nozzle diameter of %s extruder is 0.2mm which does not support automatic Flow Dynamics calibration."), name);
            return false;
        }

        float cali_diameter = cali_info.nozzle_diameter;
        int   extruder_id   = cali_info.extruder_id;
        if (extruder_id >= obj->GetExtderSystem()->GetTotalExtderSize()) {
            error_message = _L("The number of printer extruders and the printer selected for calibration does not match.");
            return false;
        }

        if (!obj->GetExtderSystem()->NozzleDiameterMatchesOrUnknown(extruder_id, cali_info.nozzle_diameter)) {
            if (is_multi_extruder)
                error_message = wxString::Format(_L("The currently selected nozzle diameter of %s extruder does not match the actual nozzle diameter.\n"
                                                    "Please click the Sync button above and restart the calibration."), name);
            else
                error_message = _L("The nozzle diameter does not match the actual printer nozzle diameter.\n"
                                   "Please click the Sync button above and restart the calibration.");
            return false;
        }

                
        NozzleFlowType nozzle_volume_type = obj->GetExtderSystem()->GetNozzleFlowType(extruder_id);
        
        if (nozzle_volume_type == NozzleFlowType::NONE_FLOWTYPE) {
            if (is_multi_extruder)
                error_message = wxString::Format(_L("Printer %s nozzle information has not been set. Please configure it before proceeding with the calibration."), name);
            else
                error_message = nozzle_not_set_text;
            return false;
        }

        if (NozzleVolumeType(nozzle_volume_type - 1) != cali_info.nozzle_volume_type) {
            if (is_multi_extruder)
                error_message = wxString::Format(_L("The currently selected nozzle type of %s extruder does not match the actual printer nozzle type.\n"
                                                "Please click the Sync button above and restart the calibration."), name);
            else
                error_message = nozzle_volume_type_not_match_text;
            return false;
        }
    }

    return true;
}

bool CalibUtils::check_printable_status_before_cali(const MachineObject* obj, const CalibInfo& cali_info, wxString& error_message)
{
    return check_printable_status_before_cali(obj, std::vector<CalibInfo>{cali_info}, error_message);
}

bool CalibUtils::process_and_store_3mf(Model *model, const DynamicPrintConfig &full_config, const Calib_Params &params, wxString &error_message)
{
    // F11 (native-slicing rip-out, fork ticket ticket-008 sec.2): the
    // slice-and-upload calibration path (Print::process / export_gcode via
    // process_and_store_3mf, and the CalibPressureAdvance* generators) was
    // removed. Signature retained for the calibration UI/wizard callers;
    // returns a not-implemented result.
    error_message = _L("Calibration is not supported in this build.");
    return false;
}

void CalibUtils::send_to_print(const CalibInfo &calib_info, wxString &error_message, int flow_ratio_mode)
{
    {  // before send
        json j;
        j["print"]["cali_mode"]       = calib_info.params.mode;
        j["print"]["start"]           = calib_info.params.start;
        j["print"]["end"]             = calib_info.params.end;
        j["print"]["step"]            = calib_info.params.step;
        j["print"]["print_numbers"]   = calib_info.params.print_numbers;
        j["print"]["flow_ratio_mode"] = flow_ratio_mode;
        j["print"]["tray_id"]         = calib_info.select_ams;
        j["print"]["dev_id"]          = calib_info.dev_id;
        j["print"]["bed_type"]        = calib_info.bed_type;
        j["print"]["printer_prest"]   = calib_info.printer_prest ? calib_info.printer_prest->name : "";
        j["print"]["filament_prest"]  = calib_info.filament_prest ? calib_info.filament_prest->name : "";
        j["print"]["print_prest"]     = calib_info.print_prest ? calib_info.print_prest->name : "";
        BOOST_LOG_TRIVIAL(info) << "send_cali_job - before send: " << j.dump();
    }

    std::string dev_id = calib_info.dev_id;
    std::string select_ams = calib_info.select_ams;
    std::shared_ptr<ProgressIndicator> process_bar = calib_info.process_bar;
    BedType bed_type = calib_info.bed_type;

    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        error_message = _L("Need select printer");
        return;
    }

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr) {
        error_message = _L("Need select printer");
        return;
    }

    if (obj_->is_in_upgrading()) {
        error_message = _L("Cannot send a print job while the printer is updating firmware.");
        return;
    }
    else if (obj_->is_system_printing()) {
        error_message = _L("The printer is executing instructions. Please restart printing after it ends.");
        return;
    }
    else if (obj_->is_in_printing()) {
        error_message = _L("The printer is busy with another print job.");
        return;
    }

    else if (!obj_->GetConfig()->SupportPrintWithoutSD() && (obj_->GetStorage()->get_sdcard_state() == DevStorage::SdcardState::NO_SDCARD)) {
        error_message = _L("Storage needs to be inserted before printing.");
        return;
    }
    if (obj_->is_lan_mode_printer()) {
        if (obj_->GetStorage()->get_sdcard_state() == DevStorage::SdcardState::NO_SDCARD) {
            error_message = _L("Storage needs to be inserted before printing via LAN.");
            return;
        }
    }

    print_worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(wxGetApp().plater(), std::move(process_bar), "calib_worker");

    auto print_job              = std::make_unique<PrintJob>(dev_id);
    print_job->m_dev_ip         = obj_->get_dev_ip();
    print_job->m_ftp_folder     = obj_->get_ftp_folder();
    print_job->m_access_code    = obj_->get_access_code();


#if !BBL_RELEASE_TO_PUBLIC
    print_job->m_local_use_ssl_for_ftp = wxGetApp().app_config->get("enable_ssl_for_ftp") == "true" ? true : false;
    print_job->m_local_use_ssl = wxGetApp().app_config->get("enable_ssl_for_mqtt") == "true" ? true : false;
#else
    print_job->m_local_use_ssl_for_ftp = obj_->local_use_ssl_for_ftp;
    print_job->m_local_use_ssl = obj_->local_use_ssl;
#endif

    print_job->connection_type  = obj_->connection_type();
    print_job->cloud_print_only = obj_->is_support_cloud_print_only;

    PrintPrepareData job_data;
    job_data.is_from_plater = false;
    job_data.plate_idx = 0;
    job_data._3mf_config_path = calib_temp_file(config_filename);
    job_data._3mf_path = calib_temp_file(model_filename);
    job_data._temp_path = calib_temp_dir();

    PlateListData plate_data;
    plate_data.is_valid = true;
    plate_data.plate_count = 1;
    plate_data.cur_plate_index = 0;
    plate_data.bed_type = bed_type;

    print_job->job_data = job_data;
    print_job->plate_data = plate_data;
    print_job->m_print_type = "from_normal";

    print_job->task_ams_mapping = "[" + select_ams + "]";
    print_job->task_ams_mapping_info = "";
    print_job->task_use_ams = !devPrinterUtil::IsVirtualSlot(select_ams);

    std::string new_ams_mapping = "[{\"ams_id\":" + std::to_string(calib_info.ams_id) + ", \"slot_id\":" + std::to_string(calib_info.slot_id) + "}]";
    print_job->task_ams_mapping2 = new_ams_mapping;

    CalibMode cali_mode       = calib_info.params.mode;
    print_job->m_project_name = get_calib_mode_name(cali_mode, flow_ratio_mode);
    print_job->set_calibration_task(true);

    print_job->sdcard_state = obj_->GetStorage()->get_sdcard_state();
    print_job->has_sdcard =  wxGetApp().app_config->get("allow_abnormal_storage") == "true"
            ? (print_job->sdcard_state == DevStorage::SdcardState::HAS_SDCARD_NORMAL
               || print_job->sdcard_state == DevStorage::SdcardState::HAS_SDCARD_ABNORMAL)
            : print_job->sdcard_state == DevStorage::SdcardState::HAS_SDCARD_NORMAL;
    print_job->could_emmc_print = obj_->can_use_emmc_print();
    print_job->set_print_config(MachineBedTypeString[bed_type], true, false, false, false, true, false, 0, 0, 0);
    print_job->set_print_job_finished_event(wxGetApp().plater()->get_send_calibration_finished_event(), print_job->m_project_name);

    {  // after send: record the print job
        json j;
        j["print"]["project_name"]    = print_job->m_project_name;
        j["print"]["is_cali_task"]    = print_job->m_is_calibration_task;
        BOOST_LOG_TRIVIAL(info) << "send_cali_job - after send: " << j.dump();
    }

    replace_job(*print_worker, std::move(print_job));
}

void CalibUtils::send_to_print(const std::vector<CalibInfo> &calib_infos, wxString &error_message, int flow_ratio_mode)
{
    std::string                        dev_id      = calib_infos[0].dev_id;
    std::shared_ptr<ProgressIndicator> process_bar = calib_infos[0].process_bar;
    BedType                            bed_type    = calib_infos[0].bed_type;

    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        error_message = _L("Need select printer");
        return;
    }

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr) {
        error_message = _L("Need select printer");
        return;
    }

    if (obj_->is_in_upgrading()) {
        error_message = _L("Cannot send a print job while the printer is updating firmware.");
        return;
    } else if (obj_->is_system_printing()) {
        error_message = _L("The printer is executing instructions. Please restart printing after it ends.");
        return;
    } else if (obj_->is_in_printing()) {
        error_message = _L("The printer is busy with another print job.");
        return;
    }

    else if (!obj_->GetConfig()->SupportPrintWithoutSD() && (obj_->GetStorage()->get_sdcard_state() == DevStorage::SdcardState::NO_SDCARD)) {
        error_message = _L("Storage needs to be inserted before printing.");
        return;
    }
    if (obj_->is_lan_mode_printer()) {
        if (obj_->GetStorage()->get_sdcard_state() == DevStorage::SdcardState::NO_SDCARD) {
            error_message = _L("Storage needs to be inserted before printing via LAN.");
            return;
        }
    }

    print_worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(wxGetApp().plater(), std::move(process_bar), "calib_worker");

    auto print_job                = std::make_shared<PrintJob>(dev_id);
    print_job->m_dev_ip      = obj_->get_dev_ip();
    print_job->m_ftp_folder  = obj_->get_ftp_folder();
    print_job->m_access_code = obj_->get_access_code();

#if !BBL_RELEASE_TO_PUBLIC
    print_job->m_local_use_ssl_for_ftp  = wxGetApp().app_config->get("enable_ssl_for_ftp") == "true" ? true : false;
    print_job->m_local_use_ssl = wxGetApp().app_config->get("enable_ssl_for_mqtt") == "true" ? true : false;
#else
    print_job->m_local_use_ssl_for_ftp  = obj_->local_use_ssl_for_ftp;
    print_job->m_local_use_ssl = obj_->local_use_ssl;
#endif

    print_job->connection_type  = obj_->connection_type();
    print_job->cloud_print_only = obj_->is_support_cloud_print_only;

    PrintPrepareData job_data;
    job_data.is_from_plater   = false;
    job_data.plate_idx        = 0;
    job_data._3mf_config_path = calib_temp_file(config_filename);
    job_data._3mf_path        = calib_temp_file(model_filename);
    job_data._temp_path       = calib_temp_dir();

    PlateListData plate_data;
    plate_data.is_valid        = true;
    plate_data.plate_count     = 1;
    plate_data.cur_plate_index = 0;
    plate_data.bed_type        = bed_type;

    print_job->job_data     = job_data;
    print_job->plate_data   = plate_data;
    print_job->m_print_type = "from_normal";

    // set AMS mapping
    std::string select_ams     = "[";
    std::string new_select_ams = "[";
    for (size_t i = 0; i < calib_infos.size(); ++i) {
        select_ams += calib_infos[i].select_ams;
        new_select_ams += "{\"ams_id\":" + std::to_string(calib_infos[i].ams_id) + ", \"slot_id\":" + std::to_string(calib_infos[i].slot_id) + "}";
        if (i != calib_infos.size() - 1) {
            select_ams += ",";
            new_select_ams += ",";
        }
    }
    select_ams += "]";
    new_select_ams += "]";
    print_job->task_ams_mapping      = select_ams;
    print_job->task_ams_mapping_info = "";
    print_job->task_ams_mapping2     = new_select_ams;

    print_job->task_use_ams = false;
    for (const CalibInfo& calib_info : calib_infos) {
        if (calib_info.select_ams != VIRTUAL_AMS_MAIN_ID_STR && calib_info.select_ams != VIRTUAL_AMS_DEPUTY_ID_STR) {
            print_job->task_use_ams = true;
            break;
        }
    }

    CalibMode cali_mode       = calib_infos[0].params.mode;
    print_job->m_project_name = get_calib_mode_name(cali_mode, flow_ratio_mode);
    print_job->set_calibration_task(true);

    print_job->sdcard_state = obj_->GetStorage()->get_sdcard_state();
    print_job->has_sdcard =  wxGetApp().app_config->get("allow_abnormal_storage") == "true"
            ? (print_job->sdcard_state == DevStorage::SdcardState::HAS_SDCARD_NORMAL
               || print_job->sdcard_state == DevStorage::SdcardState::HAS_SDCARD_ABNORMAL)
            : print_job->sdcard_state == DevStorage::SdcardState::HAS_SDCARD_NORMAL;
    print_job->could_emmc_print = obj_->can_use_emmc_print();
    print_job->set_print_config(MachineBedTypeString[bed_type], true, true, false, false, true, false, 0, 1, 0);
    print_job->set_print_job_finished_event(wxGetApp().plater()->get_send_calibration_finished_event(), print_job->m_project_name);

    { // after send: record the print job
        json j;
        j["print"]["ams_mapping"] = print_job->task_ams_mapping;
        j["print"]["ams_mapping_2"] = print_job->task_ams_mapping2;
        j["print"]["project_name"] = print_job->m_project_name;
        j["print"]["is_cali_task"] = print_job->m_is_calibration_task;
        BOOST_LOG_TRIVIAL(info) << "send_cali_job - after send: " << j.dump();
    }

    replace_job(*print_worker, std::move(print_job));
}
}
}
