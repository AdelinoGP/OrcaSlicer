#ifndef slic3r_BedShapeDialog_hpp_
#define slic3r_BedShapeDialog_hpp_
// The bed shape dialog.
// The dialog opens from Print Settins tab->Bed Shape : Set...

#include "GUI_Utils.hpp"
#include "2DBed.hpp"
#include "I18N.hpp"

#include <libslic3r/BuildVolume.hpp>

#include "Widgets/ComboBox.hpp"

#include <wx/dialog.h>
#include <wx/simplebook.h> // ORCA

namespace Slic3r { namespace GUI {

class ConfigOptionsGroup;

using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;
using ConfigOptionsGroupWkp = std::weak_ptr<ConfigOptionsGroup>;

// [INTENT] Capture the dialog's canonical bed outline and parameter helpers so the UI can switch between rectangles, circles, or custom polygons.
struct BedShape
{
    enum class PageType { Rectangle, Circle, Custom };

    // [STATE] Parameter enum drives which set of config fields each page exposes (size/origin/diameter).
    enum class Parameter { RectSize, RectOrigin, Diameter };

    // [INTENT] Translate an arbitrary polygon into a normalized BuildVolume record that the UI and persistence layer can consume.
    BedShape(const Pointfs& points);

    // [STATE] Short-lived guard used to choose between the Rectangle/Circle pages and the Custom editor.
    bool is_custom() { return m_build_volume.type() == BuildVolume_Type::Convex || m_build_volume.type() == BuildVolume_Type::Custom; }

    // [EVENT] Append the configuration controls for the given parameter so the option group matches the selected page.
    static void append_option_line(ConfigOptionsGroupShp optgroup, Parameter param);
    // [UNITY] Text shown to users maps to entries in a ScriptableObject-backed localization table in the Unity port.
    static wxString get_name(PageType type);

    // [STATE] Determine which page currently represents the cached BuildVolume so the Simplebook flips accordingly.
    PageType get_page_type();

    // [STATE] Build the display string that includes numeric parameters to show in dropdowns.
    wxString get_full_name_with_params();
    // [STATE] Rehydrate the cached BuildVolume values when the option group is committed.
    void apply_optgroup_values(ConfigOptionsGroupShp optgroup);

private:
    // [STATE] Source of truth for the bed shape type and numeric parameters currently in play.
    BuildVolume m_build_volume;
};

class BedShapePanel : public wxPanel
{
    static const std::string NONE;
    static const std::string EMPTY_STRING;

    // [OPENGL] 2D preview surface for the bed polygon, rendered on the UI thread through Bed_2D.
    Bed_2D* m_canvas;
    // [STATE] Current polygon that gets serialized and drives the preview.
    Pointfs m_shape;
    // [STATE] Previously loaded points, used to determine whether a custom mesh has been replaced.
    Pointfs m_loaded_shape;
    // [STATE] Persistent path state for the texture/model buttons to avoid re-opening dialogs.
    std::string m_custom_texture;
    std::string m_custom_model;

public:
    BedShapePanel(wxWindow* parent) : wxPanel(parent, wxID_ANY), m_custom_texture(NONE), m_custom_model(NONE) {}

    // [INTENT] Build the dropdown + Simplebook layout, hook option changes to `update_shape()`, and preload custom texture/model paths.
    // [UNITY] Equivalent: UI Toolkit VisualElement tree with a `PopupField<ListView>` selector and a RenderTexture preview panel.
    void build_panel(const Pointfs& default_pt, const std::string& custom_texture, const std::string& custom_model);

    // Returns the resulting bed shape polygon. This value will be stored to the ini file.
    const Pointfs&     get_shape() const { return m_shape; }
    const std::string& get_custom_texture() const { return (m_custom_texture != NONE) ? m_custom_texture : EMPTY_STRING; }
    const std::string& get_custom_model() const { return (m_custom_model != NONE) ? m_custom_model : EMPTY_STRING; }

private:
    // [INTENT] Create a new options page that hosts the controls for a given title.
    ConfigOptionsGroupShp init_shape_options_page(const wxString& title);
    // [EVENT] Switch the Simplebook page when the dropdown changes and reapply the cached values.
    void activate_options_page(ConfigOptionsGroupShp options_group);
    // [STATE] Builds the texture panel row and wires it to `load_texture()`.
    wxPanel* init_texture_panel();
    // [STATE] Builds the model panel row and wires it to `load_stl()`.
    wxPanel* init_model_panel();
    // [STATE] Copy values from the currently active ConfigOptionsGroup into `m_shape`.
    void set_shape(const Pointfs& points);
    // [THREAD] Refresh `m_canvas` on the UI thread whenever `m_shape` mutates.
    void update_preview();
    // [EVENT] Called after option controls change to keep `m_shape` and metadata synced.
    void update_shape();
    // [EVENT] Triggered by the STL load button; runs the blocking file dialog on the UI thread and injects the new points.
    void load_stl();
    // [EVENT] Triggered by the texture picker, updates `m_custom_texture` and refreshes the preview.
    void load_texture();
    // [EVENT] Triggered by the model picker, updates `m_custom_model` and refreshes the preview mesh.
    void load_model();

    // [STATE] Simplebook holding the Rectangle/Circle/Custom pages; the Unity port must replicate this slot via a TabView.
    wxSimplebook* m_shape_options_book;
    // [STATE] Dropdown that chooses the active `BedShape::PageType`; Unity should map this to a `PopupField` bound to the BedShape model.
    ComboBox* m_shape_combo;
    // [STATE] Preserve each options group so we can read/write the values when the user switches pages.
    std::vector<ConfigOptionsGroupShp> m_optgroups;

    friend class BedShapeDialog;
};

// [INTENT] Dialog that hosts the BedShapePanel and mediates between Print Settings and the stored polygon data.
// [UNITY] Consider mapping to a UI Toolkit Window + VisualElement stack driven by a ScriptableObject-backed BedShapeModel.
class BedShapeDialog : public DPIDialog
{
    // [STATE] Panel ownership; the dialog simply forwards getters to it.
    BedShapePanel* m_panel;

public:
    BedShapeDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _(L("Bed Shape")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    {}

    // [INTENT] Populate the panel with defaults plus any preselected texture/model before showing the modal.
    void build_dialog(const Pointfs& default_pt, const ConfigOptionString& custom_texture, const ConfigOptionString& custom_model);

    const Pointfs&     get_shape() const { return m_panel->get_shape(); }
    const std::string& get_custom_texture() const { return m_panel->get_custom_texture(); }
    const std::string& get_custom_model() const { return m_panel->get_custom_model(); }

protected:
    // [EVENT] Invoked when the DPI-aware parent suggests a new rectangle; the Unity port should mirror this via `Display.onDpiChanged`.
    // [THREAD] Always runs on the UI thread because wxWidgets dispatches DPI events synchronously.
    // [PORTING_HAZARD:P3] Unity's DPI scaling requires manually re-layout rather than relying on wxSimplebook's resizing hooks.
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

}} // namespace Slic3r::GUI

#endif /* slic3r_BedShapeDialog_hpp_ */
