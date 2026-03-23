#ifndef slic3r_GUI_ObjectSettings_hpp_
#define slic3r_GUI_ObjectSettings_hpp_

#include <memory>
#include <vector>
#include <wx/panel.h>
#include "wxExtensions.hpp"

#define NEW_OBJECT_SETTING 1
// [STATE] Compile-time gate toggling the legacy sizer stack versus the TabPrintModel-aware path.
// [PORTING_HAZARD:P3] Unity ports must pick one flow or both because the macro reconfigures how override panels are created and cached.

class wxBoxSizer;

namespace Slic3r {
class DynamicPrintConfig;
class ModelConfig;
namespace GUI {
class ConfigOptionsGroup;

// [INTENT] Owns a single `ConfigOptionsGroup` panel per object/part and exposes show/hide lifecycle hooks shared by every flow.
// [UNITY] Map to a C# controller that keeps the VisualElement/ScrollView tree alive while swapping in new groups on demand.
class OG_Settings
{
protected:
    // [STATE] Keeps the underlying `ConfigOptionsGroup` alive while the sizer or derived controls remain attached.
    std::shared_ptr<ConfigOptionsGroup> m_og;
    // [STATE] Parent wxWindow used for measurement, reparenting, and theme queries.
    wxWindow* m_parent;

public:
    OG_Settings(wxWindow* parent, const bool staticbox);
    virtual ~OG_Settings() {}

    virtual bool IsShown();
    virtual void Show(const bool show);
    virtual void Hide();
    virtual void UpdateAndShow(const bool show);

    virtual wxSizer*    get_sizer();
    ConfigOptionsGroup* get_og() { return m_og.get(); }
    wxWindow*           parent() const { return m_parent; }
};

class TabPrintModel;

// [INTENT] Bridges per-object override panels with the left Object tab and coordinates how overrides are created, shown, and hidden.
// [UNITY] Represent as a MonoBehaviour controller that keeps a VisualElement tree in sync with the selected ModelConfig.
// [PORTING_HAZARD:P2] Legacy sizer/list and TabPrintModel paths expose different lifecycles, forcing Unity to reconcile both or lock to one.
#if !NEW_OBJECT_SETTING
class ObjectSettings : public OG_Settings
#else
class ObjectSettings
#endif
{
    // sizer for extra Object/Part's settings
#if !NEW_OBJECT_SETTING
    // [STATE] Vertical stack that owns each runtime-created ConfigOptionsGroup row.
    wxBoxSizer* m_settings_list_sizer{nullptr};
    // [STATE] Cache of active ConfigOptionsGroups matching each object override.
    std::vector<std::shared_ptr<ConfigOptionsGroup>> m_og_settings;

    // [STATE] Delete button bitmaps reused when removing overrides from the list.
    ScalableBitmap m_bmp_delete;
    ScalableBitmap m_bmp_delete_focus;
#else
    // [STATE] TabPrintModel parent so VisualElement reparenting can happen on tab switches.
    wxWindow* m_parent;
    // [STATE] Tracks the TabPrintModel that supplies the currently selected object selection.
    TabPrintModel* m_tab_active;
    // [UNITY] Mirror as a TabView controller that swaps VisualElements whenever the selected TabPrintModel changes.
#endif

public:
    ObjectSettings(wxWindow* parent);
    ~ObjectSettings() {}

    // [EVENT] Rebuilds or refreshes the override group list whenever selection or overrides mutate.
    bool update_settings_list();
    /* Additional check for override options: Add options, if its needed.
     * Example: if Infill is set to 100%, and Fill Pattern is missed in config_to,
     * we should add sparse_infill_pattern to avoid endless loop in update
     */
    // [INTENT] Fills in dependent options trimmed from `config_from` so `update_settings_list` stays stable.
    // [UNITY] Mirror as a validator that runs before writing overrides into a ScriptableObject asset.
    bool add_missed_options(ModelConfig* config_to, const DynamicPrintConfig& config_from);
    // [INTENT] Synchronizes UI override state back into the supplied ModelConfig so slicing respects the latest changes.
    // [UNITY] Equivalent to writing into a ScriptableObject-backed ModelConfig and invoking `MarkDirtyRepaint`.
    void update_config_values(ModelConfig* config);
    // [EVENT] Shows/hides the override pane without reconstructing the sizer tree so tabs can toggle visibility quickly.
    void UpdateAndShow(const bool show);
    // [EVENT] Recomputes DPI-scaled resources when Windows scaling changes, keeping bitmaps sharp.
    // [THREAD] Fired on the Win32 DPI-change notification on the UI thread; keep this renderer code on the main thread.
    // [PORTING_HAZARD:P3] Unity DPI updates happen via CanvasScaler/Screen.width changes, so ports need an explicit hook.
    void msw_rescale();
    // [EVENT] Refreshes bitmaps/colors when system theming changes, since wxWidgets does not repaint cached bitmaps automatically.
    // [THREAD] Called on the UI thread every time `wxSYS_COLOUR_CHANGED` fires.
    // [PORTING_HAZARD:P3] Unity lacks a direct OS color-change signal; keep the idea of reloading assets when the theme flips.
    // [UNITY] Map to a Unity callback that observes a ScriptableObject theme/Material and reapplies textures.
    void sys_color_changed();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ObjectSettings_hpp_
