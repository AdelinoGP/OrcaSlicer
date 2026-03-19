#ifndef _WIPE_TOWER_DIALOG_H_
#define _WIPE_TOWER_DIALOG_H_

#include <wx/dialog.h>
#include <wx/webview.h>
#include "libslic3r/PrintConfig.hpp"
#include "Widgets/SpinInput.hpp"

#include "RammingChart.hpp"

// [INTENT] Custom panel to visualize and adjust ramming settings (purge volume, line width, time) for wipe tower.
// [UNITY] Map to a custom UI Toolkit VisualElement or uGUI component container.
class RammingPanel : public wxPanel
{
public:
    // [INTENT] Constructor.
    RammingPanel(wxWindow* parent);
    // [INTENT] Constructor with initial data.
    RammingPanel(wxWindow* parent, const std::string& data);
    // [INTENT] Serializes current parameter state for storage.
    std::string get_parameters();

private:
    // [STATE] Pointer to chart widget for visual feedback.
    Chart* m_chart = nullptr;
    // [STATE] Input widgets for parameters. [UNITY] Map to UI Toolkit Slider/TextField or uGUI equivalent.
    SpinInput* m_widget_volume                           = nullptr;
    SpinInput* m_widget_ramming_line_width_multiplicator = nullptr;
    SpinInput* m_widget_ramming_step_multiplicator       = nullptr;
    SpinInput* m_widget_time                             = nullptr;
    // [STATE] Internal parameter tracking.
    int m_ramming_step_multiplicator;
    int m_ramming_line_width_multiplicator;

    // [INTENT] UI event handler for parameter changes. [PORTING_HAZARD:P2] Map to Unity event system.
    void line_parameters_changed();
};

// [INTENT] Dialog wrapper for the RammingPanel to provide a modal editing experience.
// [UNITY] Map to a Modal Window or full-screen overlay in Unity.
class RammingDialog : public wxDialog
{
public:
    // [INTENT] Constructor.
    RammingDialog(wxWindow* parent, const std::string& parameters);
    // [INTENT] Retrieves serialized parameters from the panel.
    std::string get_parameters() { return m_output_data; }

private:
    // [STATE] The contained panel instance.
    RammingPanel* m_panel_ramming = nullptr;
    // [STATE] Buffer for output data.
    std::string m_output_data;
};

bool is_flush_config_modified();
void open_flushing_dialog(wxEvtHandler* parent, const wxEvent& event);

// [INTENT] Main dialog for configuring wipe tower flushing volumes and patterns using a webview-based interface.
// [UNITY] Requires custom implementation; webview may need to be replaced by a native C# plugin or a UI Toolkit overlay, or potentially a
// web-based asset/dashboard.
class WipingDialog : public wxDialog
{
public:
    using VolumeMatrix = std::vector<std::vector<double>>;

    WipingDialog(wxWindow* parent, const int max_flush_volume = Slic3r::g_max_flush_volume);
    static VolumeMatrix CalcFlushingVolumes(int extruder_id);
    std::vector<double> GetFlattenMatrix() const;
    std::vector<double> GetMultipliers() const;
    bool                GetSubmitFlag() const { return m_submit_flag; }

private:
    static int CalcFlushingVolume(const wxColour& from_, const wxColour& to_, int min_flush_volume, int nozzle_flush_dataset);
    wxString   BuildTableObjStr();
    wxString   BuildTextObjStr(bool multi_language = true);
    void       StoreFlushData(int                                     extruder_num,
                              const std::vector<std::vector<double>>& flush_volume_vecs,
                              const std::vector<double>&              flush_multipliers);

    // [STATE] Webview bridge for visualization. [PORTING_HAZARD:P1] Hard dependency on wxWebView (platform native).
    wxWebView* m_webview;
    // [STATE] Maximum allowed flushing volume.
    int m_max_flush_volume;

    // [STATE] Flushing matrices and multipliers data.
    VolumeMatrix        m_raw_matrixs;
    std::vector<double> m_flush_multipliers;
    bool                m_submit_flag{false};
};

#endif // _WIPE_TOWER_DIALOG_H_
