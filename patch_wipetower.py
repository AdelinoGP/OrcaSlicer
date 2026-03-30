import sys

def patch_file(file_path):
    with open(file_path, 'r') as f:
        content = f.read()

    replacements = [
        (
            "RammingPanel::RammingPanel(wxWindow* parent, const std::string& parameters)",
            "// [INTENT] Constructor for the RammingPanel, initializes the chart and parameter widgets.\n// [UNITY] Map to UI Toolkit custom element initialization, binding input elements to the chart data.\nRammingPanel::RammingPanel(wxWindow* parent, const std::string& parameters)"
        ),
        (
            "void RammingPanel::line_parameters_changed()",
            "// [INTENT] Updates internal variables when line parameters are changed via spin controls.\nvoid RammingPanel::line_parameters_changed()"
        ),
        (
            "std::string RammingPanel::get_parameters()",
            "// [INTENT] Serializes the panel's current ramming parameters into a string.\nstd::string RammingPanel::get_parameters()"
        ),
        (
            "bool is_flush_config_modified()",
            "// [INTENT] Checks if the flush volume configuration differs from the calculated default values.\n// [STATE] Reads project_config flush_volumes_matrix and flush_multiplier.\nbool is_flush_config_modified()"
        ),
        (
            "void open_flushing_dialog(wxEvtHandler* parent, const wxEvent& event)",
            "// [INTENT] Opens the wiping dialog and updates project configuration on confirmation.\n// [EVENT] Posts wxEvent back to the parent upon successful confirmation.\nvoid open_flushing_dialog(wxEvtHandler* parent, const wxEvent& event)"
        ),
        (
            "static std::vector<float> MatrixFlatten(const WipingDialog::VolumeMatrix& matrix)",
            "// [INTENT] Helper to flatten a 2D volume matrix into a 1D vector.\nstatic std::vector<float> MatrixFlatten(const WipingDialog::VolumeMatrix& matrix)"
        ),
        (
            "wxString WipingDialog::BuildTableObjStr()",
            "// [INTENT] Constructs a JSON string containing the initialization data for the webview UI (colors, limits, matrices).\n// [STATE] Builds state object from full_config settings.\nwxString WipingDialog::BuildTableObjStr()"
        ),
        (
            "wxString WipingDialog::BuildTextObjStr(bool multi_language)",
            "// [INTENT] Constructs a JSON string containing localized UI text for the webview interface.\nwxString WipingDialog::BuildTextObjStr(bool multi_language)"
        ),
        (
            "WipingDialog::WipingDialog(wxWindow* parent, const int max_flush_volume)",
            "// [INTENT] Constructor for the WipingDialog, sets up the webview and its script message handler.\n// [PORTING_HAZARD:P1] Relies heavily on wxWebView to render the flush volume matrix UI and handle interaction via JSON messages.\n// [UNITY] Implement as a native Unity UI Toolkit layout rather than embedding a browser, or use a webview plugin if 1:1 UI is strictly required.\nWipingDialog::WipingDialog(wxWindow* parent, const int max_flush_volume)"
        ),
        (
            "m_webview->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, [this](wxWebViewEvent& evt) {",
            "// [EVENT] Handles JSON messages sent from the JavaScript side of the webview (init, updateMatrix, storeData, quit).\n    // [THREAD] Uses CallAfter to queue UI updates on the main thread from the script callback.\n    m_webview->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, [this](wxWebViewEvent& evt) {"
        ),
        (
            "int WipingDialog::CalcFlushingVolume(const wxColour& from, const wxColour& to, int min_flush_volume, int nozzle_flush_dataset)",
            "// [INTENT] Calculates the flushing volume required when switching between two specific colors.\nint WipingDialog::CalcFlushingVolume(const wxColour& from, const wxColour& to, int min_flush_volume, int nozzle_flush_dataset)"
        ),
        (
            "WipingDialog::VolumeMatrix WipingDialog::CalcFlushingVolumes(int extruder_id)",
            "// [INTENT] Calculates the default flushing volume matrix for all colors associated with the given extruder.\nWipingDialog::VolumeMatrix WipingDialog::CalcFlushingVolumes(int extruder_id)"
        ),
        (
            "void WipingDialog::StoreFlushData(int                                     extruder_num,",
            "// [INTENT] Updates internal flush matrices and multipliers with new data from the webview UI.\nvoid WipingDialog::StoreFlushData(int                                     extruder_num,"
        ),
        (
            "std::vector<double> WipingDialog::GetFlattenMatrix() const",
            "// [INTENT] Retrieves the current flush volume matrices as a flattened 1D vector.\nstd::vector<double> WipingDialog::GetFlattenMatrix() const"
        ),
        (
            "std::vector<double> WipingDialog::GetMultipliers() const { return m_flush_multipliers; }",
            "// [INTENT] Retrieves the current flush multipliers.\nstd::vector<double> WipingDialog::GetMultipliers() const { return m_flush_multipliers; }"
        )
    ]

    for old, new_s in replacements:
        if old in content:
            content = content.replace(old, new_s)
        else:
            print(f"Warning: could not find {old}")

    with open(file_path, 'w') as f:
        f.write(content)

patch_file("src/slic3r/GUI/WipeTowerDialog.cpp")
