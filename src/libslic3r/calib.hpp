#pragma once
#include <string>
#define calib_pressure_advance_dd

#include "GCodeWriter.hpp"
#include "PrintConfig.hpp"
#include "BoundingBox.hpp"
#include "CustomGCode.hpp"

namespace Slic3r {

class GCode;
class Model;
class ModelObject;

enum class CalibMode : int {
    Calib_None = 0,
    Calib_PA_Line,
    Calib_PA_Pattern,
    Calib_PA_Tower,
    Calib_Auto_PA_Line,
    Calib_Flow_Rate,
    Calib_Temp_Tower,
    Calib_Vol_speed_Tower,
    Calib_VFA_Tower,
    Calib_Retraction_tower,
    Calib_Input_shaping_freq,
    Calib_Input_shaping_damp,
    Calib_Cornering
};

enum class CalibState { Start = 0, Preset, Calibration, CoarseSave, FineCalibration, Save, Finish };

struct Calib_Params
{
    Calib_Params() : mode(CalibMode::Calib_None){};
    int extruder_id = 0;
    double    start, end, step;
    bool      print_numbers;
    double freqStartX, freqEndX, freqStartY, freqEndY;
    int test_model;
    std::string shaper_type;
    std::vector<double> accelerations;
    std::vector<double> speeds;

    CalibMode mode;
};

enum FlowRatioCalibrationType {
    COMPLETE_CALIBRATION = 0,
    FINE_CALIBRATION,
};

class X1CCalibInfos
{
public:
    struct X1CCalibInfo
    {
        int         extruder_id = 0;
        int         tray_id;
        int         ams_id = 0;
        int         slot_id = 0;
        int         bed_temp;
        ExtruderType        extruder_type{ExtruderType::etDirectDrive};
        NozzleVolumeType    nozzle_volume_type = NozzleVolumeType::nvtStandard;
        int         nozzle_temp;
        float       nozzle_diameter;
        std::string filament_id;
        std::string setting_id;
        float       max_volumetric_speed;
        float       flow_rate = 0.98f; // for flow ratio
    };

    std::vector<X1CCalibInfo> calib_datas;
    CalibMode                 cali_mode{ CalibMode::Calib_None };
};

class CaliPresetInfo
{
public:
    int         tray_id;
    int         extruder_id;
    NozzleVolumeType nozzle_volume_type;
    BedType     bed_type;
    float       nozzle_diameter;
    int         nozzle_pos_id{-1};
    std::string nozzle_sn;
    std::string filament_id;
    std::string setting_id;
    std::string name;

    CaliPresetInfo &operator=(const CaliPresetInfo &other)
    {
        this->tray_id         = other.tray_id;
        this->extruder_id     = other.extruder_id;
        this->nozzle_volume_type = other.nozzle_volume_type;
        this->nozzle_diameter = other.nozzle_diameter;
        this->nozzle_pos_id   = other.nozzle_pos_id;
        this->nozzle_sn       = other.nozzle_sn;
        this->filament_id     = other.filament_id;
        this->setting_id      = other.setting_id;
        this->name            = other.name;
        return *this;
    }
};

struct PrinterCaliInfo
{
    std::string                 dev_id;
    bool                        cali_finished = true;
    float                       cache_flow_ratio;
    std::vector<CaliPresetInfo> selected_presets;
    FlowRatioCalibrationType    cache_flow_rate_calibration_type = FlowRatioCalibrationType::COMPLETE_CALIBRATION;
};

class PACalibResult
{
public:
    enum CalibResult {
        CALI_RESULT_SUCCESS = 0,
        CALI_RESULT_PROBLEM = 1,
        CALI_RESULT_FAILED  = 2,
    };
    int         extruder_id = 0;
    NozzleVolumeType nozzle_volume_type;
    int         tray_id = 0;
    int         ams_id = 0;
    int         slot_id = 0;
    int         cali_idx = -1;
    int         nozzle_pos_id = -1; //-1 means no nozzle pos
    float       nozzle_diameter;
    std::string nozzle_sn;
    std::string filament_id;
    std::string setting_id;
    std::string name;
    float       k_value    = 0.0;
    float       n_coef     = 0.0;
    int         confidence = -1; // 0: success  1: uncertain  2: failed
};

struct PACalibIndexInfo
{
    int         extruder_id = 0;
    NozzleVolumeType nozzle_volume_type;
    int         tray_id = 0;
    int         ams_id = 0;
    int         slot_id = 0;
    int         cali_idx = -1; // -1 means default
    int         nozzle_pos_id = -1; //-1 means no nozzle pos
    float       nozzle_diameter;
    std::string nozzle_sn;
    std::string filament_id;
};

struct PACalibExtruderInfo
{
    int              extruder_id = 0;
    NozzleVolumeType nozzle_volume_type;
    int              nozzle_pos_id = -1; //-1 means no nozzle pos
    float            nozzle_diameter;
    std::string      nozzle_sn;
    std::string      filament_id = "";
    bool             use_extruder_id{true};
    bool             use_nozzle_volume_type{true};
};

struct PACalibTabInfo
{
    float pa_calib_tab_nozzle_dia;
    int   extruder_id;
    NozzleVolumeType nozzle_volume_type;
};

class FlowRatioCalibResult
{
public:
    int         tray_id;
    float       nozzle_diameter;
    std::string filament_id;
    std::string setting_id;
    float       flow_ratio;
    int         confidence; // 0: success  1: uncertain  2: failed
};

// F11 (fork ticket, native-slicing rip-out §2): the calibration G-code
// generator declarations (DrawBoxOptArgs, CalibPressureAdvance,
// CalibPressureAdvanceLine, SuggestedConfigCalibPAPattern,
// CalibPressureAdvancePattern) were deleted with the native slicing pipeline.
// The type block above is retained because the calibration dialogs (kept as a
// mock UI) embed those types as members.

} // namespace Slic3r