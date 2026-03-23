#ifndef slic3r_GUI_ObjectTableSettings_hpp_
#define slic3r_GUI_ObjectTableSettings_hpp_

#include <memory>
#include <map>
#include <string>
#include <vector>
#include <wx/panel.h>
#include "wxExtensions.hpp"

class wxBoxSizer;

namespace Slic3r {
class DynamicPrintConfig;
class ModelConfig;
namespace GUI {
// [INTENT][STATE][UNITY] Base helper for collapsible ConfigOptionsGroup panels shown beside the object grid, matching a UI Toolkit
// VisualElement docked inside a ScrollView.
class ConfigOptionsGroup;
class ObjectGridTable;
struct SimpleSettingData;

class OTG_Settings
{
protected:
    // [STATE] owning pointer keeps the ConfigOptionsGroup alive while the panel is visible.
    std::shared_ptr<ConfigOptionsGroup> m_og;
    // [STATE][PORTING_HAZARD:P3] parent window drives lifetime, so Unity needs a MonoBehaviour-managed container instead.
    wxWindow* m_parent;

public:
    OTG_Settings(wxWindow* parent, const bool staticbox);
    virtual ~OTG_Settings() {}

    // [STATE] reflects whether the wx sizer is currently visible, letting callers skip redundant toggles.
    virtual bool IsShown();
    // [EVENT] toggles visibility; Unity mapping is `VisualElement.style.display` toggling.
    virtual void Show(const bool show);
    // [EVENT] counterpart to Show for hiding the group.
    virtual void Hide();
    // [EVENT][STATE] refreshes the layout while ensuring the visible flag stays in sync to avoid flicker.
    virtual void UpdateAndShow(const bool show);

    // [INTENT] exposes the sizer for parents to insert these sections, akin to VisualElement hierarchies.
    virtual wxSizer*    get_sizer();
    ConfigOptionsGroup* get_og() { return m_og.get(); }
    wxWindow*           parent() const { return m_parent; }
};

// [INTENT][UNITY] Bridges the object grid and the right-side settings panel, similar to a Unity UI Toolkit ListView feeding VisualElement
// groups from a ScriptableObject ModelConfig.
class ObjectTableSettings : public OTG_Settings
{
    // [STATE] sizer for stacking every object/part settings block; used to hide/show sections when selection changes.
    wxBoxSizer* m_settings_list_sizer{nullptr};
    // [STATE] cached option groups to avoid rebuilding the VisualElement tree from scratch on each update.
    std::vector<std::shared_ptr<ConfigOptionsGroup>> m_og_settings;
    // [UNITY][PORTING_HAZARD:P2] Unity should treat these as pooled VisualElement templates created from a ScriptableObject config so the
    // ListView refresh is fast.

    // [STATE][UNITY] config snapshot currently shown to users, analogous to a ScriptableObject instance used by a ListView data provider.
    DynamicPrintConfig m_current_config;
    // [STATE][PORTING_HAZARD:P3] baseline for comparison to detect overrides; Unity ports must keep this mirror in sync to drive
    // mixed-value badges.
    DynamicPrintConfig m_origin_config;
    // [STATE] icons for the Reset button states to avoid recalculating on every change.
    ScalableBitmap m_bmp_reset;
    ScalableBitmap m_bmp_reset_focus;
    ScalableBitmap m_bmp_reset_disable;

    // [EVENT][STATE][UNITY] back-reference to the grid that drives selection changes, similar to binding a ListView selection callback.
    ObjectGridTable* m_table{nullptr};
    // [STATE] row index currently represented by the settings panel.
    int m_current_row{0};
    // [STATE] category name used to group settings, e.g., "Perimeters".
    std::string m_current_category;
    // [STATE][UNCLEAR] number of differing values across the selection; presumably tracks how many objects deviate.
    int m_current_different{0};
    // [STATE] per-category counters to rebuild the mixed-state indicators without scanning every field.
    std::map<std::string, int> m_different_map;

public:
    ObjectTableSettings(wxWindow* parent, ObjectGridTable* table);
    ~ObjectTableSettings() { m_different_map.clear(); }

    // [EVENT][INTENT][UNITY] rebuilds the visible config groups when selection changes so overrides stay in sync with the table; parallels
    // rebuilding the VisualElement tree when a ListView selection changes.
    bool update_settings_list(
        bool is_object, bool is_multiple_selection, ModelObject* object, ModelConfig* config, const std::string& category);
    /* Additional check for override options: Add options, if its needed.
     * Example: if Infill is set to 100%, and Fill Pattern is missed in config_to,
     * we should add sparse_infill_pattern to avoid endless loop in update
     */
    // [EVENT][PORTING_HAZARD:P3][UNITY] inserts implied options to prevent recursive rebuilds when dependent values drop out, mirroring how
    // Unity property watchers need guard rails to avoid endless loops.
    bool add_missed_options(ModelConfig* config_to, const DynamicPrintConfig& config_from);
    // return visible count
    // [INTENT][STATE][EVENT][UNITY] refreshes visibility tallies for an option group after toggling columns, enabling column-aware
    // VisualElement updates.
    int update_extra_column_visible_status(ConfigOptionsGroup*                   option_group,
                                           const std::vector<SimpleSettingData>& option_keys,
                                           ModelConfig*                          config);
    // [EVENT][STATE] writes the user-side UI edits back into the ModelConfig and the cached DynamicPrintConfig, similar to pushing
    // VisualElement bindings back to a ScriptableObject.
    void update_config_values(bool is_object, ModelObject* object, ModelConfig* config, const std::string& category);
    // [EVENT][STATE][UNITY] main entry point for showing or hiding the settings pane for a given row; toggles VisualElement display and
    // keeps grid highlight and panel visibility aligned.
    void UpdateAndShow(int                row,
                       const bool         show,
                       bool               is_object,
                       bool               is_multiple_selection,
                       ModelObject*       object,
                       ModelConfig*       config,
                       const std::string& category);
    // [EVENT][STATE] invoked when a single key changes; propagates the change to caches and the grid, akin to KeyDown callbacks updating a
    // ScriptableObject data model.
    void ValueChanged(int row, bool is_object, ModelObject* object, ModelConfig* config, const std::string& category, const std::string& key);
    // [EVENT][STATE][PORTING_HAZARD:P3][UNITY] resets all per-row overrides back to the baseline, mirroring a Unity `Button` command that
    // rewrites the ScriptableObject but warning that the bitmap reset icons are manually scaled.
    void resetAllValues(int row, bool is_object, ModelObject* object, ModelConfig* config, const std::string& category);
    // [PORTING_HAZARD:P3][INTENT] only used on Windows for bitmap rescaling; Unity should let CanvasScaler handle DPI instead of manual tweaks.
    void msw_rescale();
};
wxDECLARE_EVENT(EVT_LOCK_DISABLE, wxCommandEvent);
wxDECLARE_EVENT(EVT_LOCK_ENABLE, wxCommandEvent);
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ObjectTableSettings_hpp_
