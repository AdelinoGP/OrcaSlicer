#ifndef slic3r_ProjectDirtyStateManager_hpp_
#define slic3r_ProjectDirtyStateManager_hpp_

#include "libslic3r/Preset.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Tracks whether the active project has diverged from its saved baseline.
// It combines plater undo/redo state, preset selection state, and project config snapshots.
// [STATE] The saved-baseline snapshots below are compared against live app state to drive the dirty flag.
// [EVENT] update_from_* hooks are called from undo/redo, preset refresh, and save/reset lifecycle points.
// [UNITY] Map this to a C# project-dirty service that listens to project/preset events and exposes one computed bool.
// [PORTING_HAZARD:P2] Dirty detection relies on snapshot equality for presets and project_config, so the Unity port needs a stable
// serialized baseline.
class ProjectDirtyStateManager
{
public:
    void update_from_undo_redo_stack(bool dirty);
    void update_from_presets();
    void reset_after_save();
    void reset_initial_presets();

    void set_plater_dirty(bool is_dirty) { m_plater_dirty = is_dirty; }
    bool is_dirty() const { return m_plater_dirty || m_project_config_dirty || m_presets_dirty; }
    bool is_presets_dirty() const { return m_presets_dirty; }

#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    void render_debug_window() const;
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

private:
    // [STATE] Aggregate dirty flags; any true value makes the project dirty.
    bool m_plater_dirty{false};
    // [STATE] Preset comparisons are tracked separately because they can change without geometry edits.
    bool m_presets_dirty{false};
    // [STATE] Project config snapshot comparison is part of the dirty-state decision.
    bool m_project_config_dirty{false};
    // [STATE] Baseline preset names and config captured at the last save/reset.
    std::array<std::string, Preset::TYPE_COUNT> m_initial_presets;
    DynamicPrintConfig                          m_initial_project_config;

    // [STATE] Filament presets need separate snapshot storage because the UI treats names and colors independently.
    std::vector<std::string> m_initial_filament_presets_names;  // all filament preset type name
    std::vector<std::string> m_initial_filament_presets_colors; // all filament preset color
};

}} // namespace Slic3r::GUI

#endif // slic3r_ProjectDirtyStateManager_hpp_
