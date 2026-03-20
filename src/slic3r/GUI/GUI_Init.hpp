#ifndef slic3r_GUI_Init_hpp_
#define slic3r_GUI_Init_hpp_

#include <libslic3r/Preset.hpp>
#include <libslic3r/PrintConfig.hpp>

// [INTENT] Defines the initialization parameters and entry point for the OrcaSlicer GUI.
// [THREAD] Main thread entry point and configuration storage.
namespace Slic3r { namespace GUI {

struct OpenGLVersions
{
    static const std::vector<std::pair<int, int>> core;
};

// [INTENT] Parameters passed to GUI entry point, capturing CLI arguments and initial configuration state.
// [STATE] Command line arguments, project files to load, and OpenGL profile requirements.
// [UNITY] Map CLI arguments to System.Environment.GetCommandLineArgs(). Initial configs can be handled by a Bootstrapper ScriptableObject.
struct GUI_InitParams
{
    int    argc;
    char** argv;

    // Substitutions of unknown configuration values done during loading of user presets.
    PresetsConfigSubstitutions preset_substitutions;

    // [STATE] List of configuration files (.ini) to load at startup.
    std::vector<std::string> load_configs;
    DynamicPrintConfig       extra_config;
    // [STATE] List of 3D models or project files (.3mf, .stl, etc.) passed as arguments.
    std::vector<std::string> input_files;

    // [STATE] Desired OpenGL version; critical for legacy GL vs Core Profile decisions.
    // [UNITY] Handled by Project Settings -> Player -> Rendering.
    std::pair<int, int> opengl_version{0, 0};
    bool                opengl_debug{false};
    bool                opengl_compatibility_profile{false};

    // BBS: remove start_as_gcodeviewer logic
    // bool	                    start_as_gcodeviewer;
    bool input_gcode{false};
};

// [INTENT] Entry point for GUI mode, initializes the application singleton and enters the main event loop.
// [UNITY] Use a Bootstrapper scene or a [RuntimeInitializeOnLoadMethod] to initialize global state.
int GUI_Run(GUI_InitParams& params);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_Init_hpp_
