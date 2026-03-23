#ifndef slic3r_GUI_ObjectLayers_hpp_
#define slic3r_GUI_ObjectLayers_hpp_

#include "GUI_ObjectSettings.hpp"
#include "wxExtensions.hpp"

#ifdef __WXOSX__
#include "libslic3r/PrintConfig.hpp"
#endif

class wxBoxSizer;

namespace Slic3r {
class ModelObject;

namespace GUI {
class ConfigOptionsGroup;

typedef double                        coordf_t;
typedef std::pair<coordf_t, coordf_t> t_layer_height_range;

class ObjectLayers;

enum EditorType {
    etUndef       = 0,
    etMinZ        = 1,
    etMaxZ        = 2,
    etLayerHeight = 4,
};

// [INTENT] Lightweight editor for min/max/layer heights that keeps the 3D layer selection model in sync with typed values.
// [UNITY] Replace with a Canvas overlay that hosts InputFields + a LayerHeightEditor MonoBehaviour wired to the preview controller.
// [PORTING_HAZARD:P3] wxTextCtrl focus/Enter handling uses kill-focus guards; Unity's InputField Submit events require a different focus
// bookkeeping strategy.
class LayerRangeEditor : public wxTextCtrl
{
    bool m_enter_pressed{false};   // [STATE] tracks whether Enter committed the change so PlusMinusButton ignores the following focus loss.
    bool m_call_kill_focus{false}; // [STATE] guards re-entrant focus events when buttons update the editor text.
    wxString   m_valid_value;      // [STATE] caches the last successful parse so we can restore it on invalid input.
    EditorType m_type;

    std::function<void(EditorType)> m_set_focus_data;

public:
    LayerRangeEditor(
        ObjectLayers*                   parent,
        const wxString&                 value             = wxEmptyString,
        EditorType                      type              = etUndef,
        std::function<void(EditorType)> set_focus_data_fn = [](EditorType) { ; },
        // callback parameters: new value, from enter, dont't update panel UI (when called from edit field's kill focus handler for the
        // PlusMinusButton)
        std::function<bool(coordf_t, bool, bool)> edit_fn = [](coordf_t, bool, bool) { return false; });
    ~LayerRangeEditor() {}

    EditorType type() const { return m_type; }
    // [EVENT] keeps the parent ObjectLayers aware of which EditorType last gained focus so buttons can refresh their cached ranges.
    void set_focus_data() const { m_set_focus_data(m_type); }
    void msw_rescale();

private:
    coordf_t get_value();
};

// [INTENT] Object-specific layer-height inspector that keeps text edits, plus/minus buttons, and the 3D preview synchronized for the
// current model object. [UNITY] Implement as a LayerHeightPanel MonoBehaviour that spawns InputFields and Buttons on a Canvas, binding to a
// shared LayerConfig ScriptableObject.
class ObjectLayers : public OG_Settings
{
    ScalableBitmap m_bmp_delete;
    ScalableBitmap m_bmp_add;
    ModelObject*   m_object{
        nullptr}; // [STATE] the inspected object, dictates which layer data is shown and which undo/redo target is updated.

    wxFlexGridSizer*     m_grid_sizer;
    t_layer_height_range m_selectable_range; // [STATE] this clamp ensures PlusMinusButton ranges stay within the allowed slider bounds.
    EditorType           m_selection_type{etUndef}; // [STATE] the kind of editor that currently owns keyboard focus for styling keyframes.

public:
    ObjectLayers(wxWindow* parent);
    ~ObjectLayers() {}

    // Button remembers the layer height range, for which it has been created.
    // The layer height range for this button is updated when the low or high boundary of the layer height range is updated
    // by the respective text edit field, so that this button emits an action for an up to date layer height range value.
    // [STATE] Button stores the layer height range it was last bound to so clicks always emit fresh values.
    // [EVENT] click handlers read this range and notify the preview/controller about the intended stretch.
    class PlusMinusButton : public ScalableButton
    {
    public:
        PlusMinusButton(wxWindow* parent, const ScalableBitmap& bitmap, std::pair<coordf_t, coordf_t> range)
            : ScalableButton(parent, wxID_ANY, bitmap), range(range)
        {}
        // updated when the text edit field loses focus for any PlusMinusButton.
        std::pair<coordf_t, coordf_t> range;
    };

    // [EVENT] each editor calls this when it grabs focus so the panel highlights that row and updates the PlusMinus caches.
    void select_editor(LayerRangeEditor* editor, const bool is_last_edited_range);
    // Create sizer with layer height range and layer height text edit fields, without buttons.
    // If the delete and add buttons are provided, the respective text edit fields will modify the layer height ranges of thes buttons
    // on value change, so that these buttons work with up to date values.
    wxSizer* create_layer(const t_layer_height_range& range,
                          PlusMinusButton*            delete_button,
                          PlusMinusButton* add_button); // [INTENT] builds the row template reused every time we refresh the layer list.
    // [INTENT] generated once when object/layer range changes; this mirrors Unity's Rebuild() for List/VisualElement trees.
    void create_layers_list();
    // [EVENT] triggered when a child editor mutates so cached buttons and focus states stay accurate.
    void update_layers_list();

    // [OPENGL] notifies the viewport about the active range so highlight overlays and selection caches refresh.
    // [UNITY] Trigger the PreviewController MonoBehaviour (LineRenderer + RenderTexture) to re-sample the layer cursor.
    void update_scene_from_editor_selection() const;

    // [EVENT][THREAD] OG_Settings calls this to toggle visibility; keep the work on the main thread and rebuild children as needed.
    void UpdateAndShow(const bool show) override;
    // [PORTING_HAZARD:P3] The Windows-focused msw_rescale DPI logic must translate to Unity RectTransform scaling paths.
    void msw_rescale();
    // [EVENT] reacts to theme/color changes coming from the wxWidgets paint loop; Unity needs similar ThemeManager hooks.
    void sys_color_changed();
    // [EVENT] clear focus metadata when a new object is bound so stale highlight states disappear.
    void reset_selection();
    // [STATE] update the selectable range used by editors and PlusMinusButton instances.
    void set_selectable_range(const t_layer_height_range& range) { m_selectable_range = range; }

    friend class LayerRangeEditor;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ObjectLayers_hpp_
