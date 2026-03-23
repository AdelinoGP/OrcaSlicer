#include "GUI_ObjectSettings.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Factories.hpp"
#include "Tab.hpp"
#include "MainFrame.hpp"

#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"

#include <boost/algorithm/string.hpp>

#include "I18N.hpp"
#include "ConfigManipulation.hpp"

#include <wx/wupdlock.h>

namespace Slic3r { namespace GUI {

// [INTENT] Wraps a shared ConfigOptionsGroup so downstream panels can show/hide a reusable option grid without tearing it down.
// [STATE] Keeps the shared `m_og` group alive while `m_parent` owns the sizer and repaint lifecycle.
// [UNITY] Mirror this as a MonoBehaviour holding a VisualElement + ScrollView pair, reusing the same show/hide semantics.
OG_Settings::OG_Settings(wxWindow* parent, const bool staticbox) : m_parent(parent)
{
    // BBS
    wxString title = "";
    // wxString title = staticbox ? " " : ""; // temporary workaround - #ys_FIXME
    m_og = std::make_shared<ConfigOptionsGroup>(parent, title);
}

bool OG_Settings::IsShown() { return m_og->sizer->IsEmpty() ? false : m_og->sizer->IsShown(size_t(0)); }

void OG_Settings::Show(const bool show) { m_og->Show(show); }

void OG_Settings::Hide() { Show(false); }

void OG_Settings::UpdateAndShow(const bool show)
{
    Show(show);
    //    m_parent->Layout();
}

wxSizer* OG_Settings::get_sizer() { return m_og->sizer; }

// [INTENT] Builds the object-specific settings panel, falling back to the legacy OG_Settings layout or the new tab-aware sequence depending
// on the compile flag. [STATE] Keeps `m_parent` alive for layout/resize callbacks and wires the bitmap assets for delete actions tied to
// the override list. [PORTING_HAZARD:P3] Depends on wx-specific singletons (`wxGetApp`, `wxWindow`, `wxSizer`), so Unity should rehydrate
// these states with a MonoBehaviour-driven SelectionController + ScrollView binding.
ObjectSettings::ObjectSettings(wxWindow* parent)
    :
#if !NEW_OBJECT_SETTING
    OG_Settings(parent, true)
#else
    m_parent(parent)
#endif
{
#if !NEW_OBJECT_SETTING
    m_og->activate();
    m_og->set_name(_(L("Additional process preset")));

    m_settings_list_sizer = new wxBoxSizer(wxVERTICAL);
    m_og->sizer->Add(m_settings_list_sizer, 1, wxEXPAND | wxLEFT, 5);

    m_bmp_delete       = ScalableBitmap(parent, "cross");
    m_bmp_delete_focus = ScalableBitmap(parent, "cross_focus");
#endif
}

#if !NEW_OBJECT_SETTING // BBS: new object settings

// [INTENT] Rebuilds the override option list when selection changes, generating ConfigOptionsGroup entries and delete buttons on demand.
// [STATE] Resets `m_settings_list_sizer` and the cached option groups to keep the UI consistent with the active configuration item.
// [THREAD] Runs on the main thread because it repeatedly calls into wxWidgets controls and uses `wxWindowUpdateLocker` to block repaint
// churn. [UNITY] Translate to a MonoBehaviour that rebuilds a UI Toolkit VisualElement ScrollView when the selection list changes, binding
// to a ScriptableObject model.
bool ObjectSettings::update_settings_list()
{
    m_settings_list_sizer->Clear(true);
    m_og_settings.resize(0);

    auto objects_ctrl  = wxGetApp().obj_list();
    auto objects_model = wxGetApp().obj_list()->GetModel();
    auto config        = wxGetApp().obj_list()->config();

    const auto item = objects_ctrl->GetSelection();

    if (!item || !objects_model->IsSettingsItem(item) || !config || objects_ctrl->multiple_selection())
        return false;

    const bool              is_object_settings = objects_model->GetItemType(objects_model->GetParent(item)) == itObject;
    SettingsFactory::Bundle cat_options        = SettingsFactory::get_bundle(&config->get(), is_object_settings);

    // [STATE] Builds the UI only when the selected object exposes override bundles; otherwise selection falls back to the parent.
    if (!cat_options.empty()) {
        std::vector<std::string> categories;
        categories.reserve(cat_options.size());

        // [EVENT] Injects a delete button per row so users can remove overrides directly from the combo list while keeping the undo
        // snapshot in sync. [THREAD] Posts `wxTheApp->CallAfter` to re-layout on the UI thread after mutating the config map.
        // [PORTING_HAZARD:P3] Unity will need an explicit `MainThreadDispatcher` plus SnapshotService because `wxTheApp->CallAfter` and the
        // legacy undo stack have no direct counterpart.
        auto extra_column = [config, this](wxWindow* parent, const Line& line) {
            auto opt_key = (line.get_options())[0].opt_id; // we assume that we have one option per line

            auto btn = new ScalableButton(parent, wxID_ANY, m_bmp_delete);
            btn->SetBackgroundColour(parent->GetBackgroundColour());
            btn->SetToolTip(_(L("Remove parameter")));

            btn->SetBitmapFocus(m_bmp_delete_focus.bmp());
            btn->SetBitmapHover(m_bmp_delete_focus.bmp());

            btn->Bind(wxEVT_BUTTON, [opt_key, config, this](wxEvent& event) {
                wxGetApp().plater()->take_snapshot(from_u8((boost::format("Delete Option %s") % opt_key).str()).ToStdString());
                config->erase(opt_key);
                wxGetApp().obj_list()->changed_object();
                wxTheApp->CallAfter([this]() {
                    wxWindowUpdateLocker noUpdates(m_parent);
                    update_settings_list();
                    m_parent->Layout();
                });
            });
            return btn;
        };

        for (auto& cat : cat_options) {
            categories.push_back(cat.first);

            auto optgroup            = std::make_shared<ConfigOptionsGroup>(m_og->ctrl_parent(), _(cat.first), config, false, extra_column);
            optgroup->label_width    = 15;
            optgroup->sidetext_width = 5;

            // [EVENT] Propagates every option change so the preview/digest updates and the selection list knows a process preset changed.
            optgroup->m_on_change = [this, config](const t_config_option_key& opt_id, const boost::any& value) {
                this->update_config_values(config);
                wxGetApp().obj_list()->changed_object();
            };

            // [EVENT] Rescales the delete button bitmap when DPI changes so the icon stays crisp.
            // [UNITY] Translate to a `SpriteAtlas` refresh on DPI change inside a MonoBehaviour-managed UI Toolkit tree.
            optgroup->rescale_extra_column_item = [this](wxWindow* win) {
                auto* ctrl = dynamic_cast<ScalableButton*>(win);
                if (ctrl == nullptr)
                    return;
                ctrl->SetBitmap_(m_bmp_delete);
                ctrl->SetBitmapFocus(m_bmp_delete_focus.bmp());
                ctrl->SetBitmapHover(m_bmp_delete_focus.bmp());
            };

            const bool is_extruders_cat = cat.first == "Extruders";
            for (auto& opt : cat.second) {
                Option option    = optgroup->get_option(opt);
                option.opt.width = 12;
                if (is_extruders_cat)
                    option.opt.max = wxGetApp().extruders_edited_cnt();
                optgroup->append_single_option_line(option);
            }
            optgroup->activate();
            for (auto& opt : cat.second)
                optgroup->get_field(opt)->m_on_change = [optgroup](const std::string& opt_id, const boost::any& value) {
                    // first of all take a snapshot and then change value in configuration
                    wxGetApp().plater()->take_snapshot(from_u8((boost::format("Change Option %s") % opt_id).str()).ToStdString());
                    optgroup->on_change_OG(opt_id, value);
                };

            optgroup->reload_config();

            m_settings_list_sizer->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 0);
            m_og_settings.push_back(optgroup);
        }

        if (!categories.empty()) {
            objects_model->UpdateSettingsDigest(item, categories);
            update_config_values(config);
        }
    } else {
        // [STATE] Reset selection to the parent when no override categories exist, mirroring Unity's fallback on list filters.
        objects_ctrl->select_item(objects_model->Delete(item));
        return false;
    }
    return true;
}

// [STATE][UNITY] Alternative path that mirrors the tabbed `ParamsPanel` controller when `NEW_OBJECT_SETTING` is active; keeps tabs
// synchronized with selection and config maps.
#else
bool ObjectSettings::update_settings_list()
{
    // [STATE] Only run when the mainframe is visible and the editor exists so the tab controllers are valid.
    if (!wxGetApp().is_editor())
        return false;
    if (!wxGetApp().mainframe->IsShown())
        return false;

    auto objects_ctrl  = wxGetApp().obj_list();
    auto objects_model = wxGetApp().obj_list()->GetModel();

    wxDataViewItemArray items;
    objects_ctrl->GetSelections(items);

    // [STATE] Collects plate/object config overrides so each TabPrint* controller can be rehydrated when selection changes.
    std::map<ObjectBase*, ModelConfig*> plate_configs;
    std::map<ObjectBase*, ModelConfig*> object_configs;
    bool                                is_plate_settings       = false;
    bool                                is_object_settings      = false;
    bool                                is_volume_settings      = false;
    bool                                is_layer_range_settings = false;
    bool                                is_layer_root           = false;
    ModelObject*                        parent_object           = nullptr;
    for (auto item : items) {
        auto type = objects_model->GetItemType(item);
        if (type != itPlate && type != itObject && type != itVolume && type != itLayerRoot && type != itLayer) {
            continue;
        }
        int            plate_id = objects_model->GetPlateIdByItem(item);
        PartPlateList& ppl      = wxGetApp().plater()->get_partplate_list();
        if (plate_id < 0 || plate_id >= ppl.get_plate_count()) {
            plate_id = ppl.get_curr_plate_index();
        }
        assert(plate_id >= 0 && plate_id < ppl.get_plate_count());
        static ModelConfig cfg;
        cfg.assign_config(*ppl.get_plate(plate_id)->config());
        if (type == itPlate) {
            is_plate_settings = true;
            plate_configs.emplace(ppl.get_plate(plate_id), &cfg);
            break;
        }

        const int obj_idx = objects_model->GetObjectIdByItem(item);
        assert(obj_idx >= 0);
        auto object = wxGetApp().model().objects[obj_idx];
        if (type == itObject) {
            is_object_settings = true;
            plate_configs.emplace(ppl.get_plate(plate_id), &cfg);
            object_configs.emplace(object, &object->config);
        } else if (type == itVolume) {
            is_volume_settings = true;
            if (parent_object && parent_object != object)
                return false;
            plate_configs.emplace(ppl.get_plate(plate_id), &cfg);
            parent_object     = object;
            const int vol_idx = objects_model->GetVolumeIdByItem(item);
            assert(vol_idx >= 0);
            auto volume = object->volumes[vol_idx];
            object_configs.emplace(volume, &volume->config);
        } else if (type == itLayer) {
            is_layer_range_settings = true;
            if (parent_object && parent_object != object)
                return false;
            plate_configs.emplace(ppl.get_plate(plate_id), &cfg);
            parent_object = object;

            t_layer_height_range height_range = objects_model->GetLayerRangeByItem(item);
            object_configs.emplace((ObjectBase*) (&object->layer_config_ranges.at(height_range)),
                                   &object->layer_config_ranges.at(height_range));
        } else if (type == itLayerRoot) {
            is_layer_root = true;
        }
    }

    auto tab_plate  = dynamic_cast<TabPrintPlate*>(wxGetApp().get_plate_tab());
    auto tab_object = dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab());
    auto tab_volume = dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab(true));
    auto tab_layer  = dynamic_cast<TabPrintModel*>(wxGetApp().get_layer_tab());

    // [UNITY] Mirror TabPrint tab switches with MonoBehaviour-controlled VisualElements so Unity can rebuild the right panel when selection
    // state changes.

    if (is_plate_settings) {
        tab_plate->set_model_config(plate_configs);
        tab_object->set_model_config({});
        tab_volume->set_model_config({});
        tab_layer->set_model_config({});
        ; // m_tab_active = tab_plate;
    } else if (is_object_settings) {
        tab_plate->set_model_config(plate_configs);
        tab_object->set_model_config(object_configs);
        tab_volume->set_model_config({});
        tab_layer->set_model_config({});
        // m_tab_active = tab_object;
    } else if (is_volume_settings) {
        tab_plate->set_model_config(plate_configs);
        tab_object->set_model_config({{parent_object, &parent_object->config}});
        tab_volume->set_model_config(object_configs);
        tab_layer->set_model_config({});
        // m_tab_active = tab_volume;
    } else if (is_layer_range_settings) {
        tab_plate->set_model_config(plate_configs);
        tab_object->set_model_config({{parent_object, &parent_object->config}});
        tab_volume->set_model_config({});
        tab_layer->set_model_config(object_configs);
        // m_tab_active = tab_layer;
    } else {
        tab_plate->set_model_config({});
        tab_object->set_model_config({});
        tab_volume->set_model_config({});
        tab_layer->set_model_config({});
        // m_tab_active = nullptr;
    }
    // [EVENT] Clears the active tab reference so the UI bound to ParamsPanel knows no tab is highlighted after list rebuilds.
    ((ParamsPanel*) tab_object->GetParent())->set_active_tab(nullptr);
    return true;
}

#endif

// [INTENT] Ensures the override config includes every option the printed preset exposed so the UI does not miss values.
// [UNITY] Mirror as a ValidationService that clones missing keys into a ScriptableObject before pushing updates from the inspector.
bool ObjectSettings::add_missed_options(ModelConfig* config_to, const DynamicPrintConfig& config_from)
{
    const DynamicPrintConfig& print_config = wxGetApp().plater()->printer_technology() == ptFFF ?
                                                 wxGetApp().preset_bundle->prints.get_edited_preset().config :
                                                 wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    bool                      is_added     = false;

    for (auto opt_key : config_from.diff(print_config))
        if (!config_to->has(opt_key)) {
            config_to->set_key_value(opt_key, config_from.option(opt_key)->clone());
            is_added = true;
        }

    return is_added;
}

// [STATE][THREAD] Recomputes the override data by merging the selected object's config with the main printer preset on the UI thread.
void ObjectSettings::update_config_values(ModelConfig* config)
{
    const auto objects_model      = wxGetApp().obj_list()->GetModel();
    const auto item               = wxGetApp().obj_list()->GetSelection();
    const auto printer_technology = wxGetApp().plater()->printer_technology();
    const bool is_object_settings = objects_model->GetItemType(objects_model->GetParent(item)) == itObject;

    if (!item || !objects_model->IsSettingsItem(item) || !config)
        return;

    // update config values according to configuration hierarchy
    DynamicPrintConfig main_config = printer_technology == ptFFF ? wxGetApp().preset_bundle->prints.get_edited_preset().config :
                                                                   wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;

    // [THREAD] Loads the main preset values, adds any missing options, and triggers another pass to resync UI components safely on the UI thread.
    auto load_config = [this, config, &main_config]() {
        /* Additional check for overrided options.
         * There is a case, when some options should to be added,
         * to avoid check loop in the next configuration update
         */
        bool is_added = add_missed_options(config, main_config);

        // load checked values from main_config to config
        config->apply_only(main_config, config->keys(), true);
        // Initialize UI components with the config values.
#if !NEW_OBJECT_SETTING
        for (auto og : m_og_settings)
            og->reload_config();
#else
#endif
        // next config check
        update_config_values(config);

        if (is_added) {
            // [THREAD] Forces an update_layout run with a `wxWindowUpdateLocker` because adding missing options mutates the layout tree.
            // [UNITY] Unity would refresh the VisualElement tree inside a main-thread coroutine rather than relying on
            // `wxWindowUpdateLocker`. #ysFIXME - Delete after testing! Very likely this CallAfret is no needed
            //            wxTheApp->CallAfter([this]() {
            wxWindowUpdateLocker noUpdates(m_parent);
            update_settings_list();
            m_parent->Layout();
            //            });
        }
    };

#if !NEW_OBJECT_SETTING
    // [STATE] Toggles the checkbox field in the ConfigOptionsGroup so overrides remain synchronized when the main config changes.
    auto toggle_field = [this](const t_config_option_key& opt_key, bool toggle, int opt_index) {
        Field* field = nullptr;
        for (auto og : m_og_settings) {
            field = og->get_fieldc(opt_key, opt_index);
            if (field != nullptr)
                break;
        }
        if (field)
            field->toggle(toggle);
    };
    // [STATE] Routes toggles to the tab-active controller when the new object settings flow is active.
#else
    auto toggle_field = [this](const t_config_option_key& opt_key, bool toggle, int opt_index) {
        m_tab_active->toggle_option(opt_key, toggle, opt_index);
    };
#endif

    // [INTENT] Wraps the config merge logic so updates/resets dispatch through `load_config` and UI toggles travel via `toggle_field`.
    // [UNITY] Equivalent to a `ConfigBinder` MonoBehaviour that wires UI events to the `ScriptableObject` model.
    ConfigManipulation config_manipulation(load_config, toggle_field, nullptr, nullptr, &(config->get()));

    // [STATE] Keeps the BBL printer flag in sync so the GUI can show/hide vendor-specific options exactly.
    config_manipulation.set_is_BBL_Printer(wxGetApp().preset_bundle->is_bbl_vendor());

    // [STATE] When the selection belongs to a specific object, pre-apply its config before merging the shared overrides.
    if (!is_object_settings) {
        const int obj_idx = objects_model->GetObjectIdByItem(item);
        assert(obj_idx >= 0);
        // for object's part first of all update konfiguration from object
        main_config.apply(wxGetApp().model().objects[obj_idx]->config.get(), true);
        // and then from its own config
    }

    main_config.apply(config->get(), true);
    printer_technology == ptFFF ? config_manipulation.update_print_fff_config(&main_config) :
                                  config_manipulation.update_print_sla_config(&main_config);

    printer_technology == ptFFF ? config_manipulation.toggle_print_fff_options(&main_config) :
                                  config_manipulation.toggle_print_sla_options(&main_config);
}

void ObjectSettings::UpdateAndShow(const bool show)
{
#if !NEW_OBJECT_SETTING
    // [EVENT] Triggers a rebuild when toggling the section; trying to show it runs `update_settings_list` and only displays when data exists.
    OG_Settings::UpdateAndShow(show ? update_settings_list() : false);
#else
    update_settings_list();
#endif
}

void ObjectSettings::msw_rescale()
{
#if !NEW_OBJECT_SETTING
    // [EVENT] Called by the Windows DPI handler so the delete icon and group controls redraw at the new scale.
    m_bmp_delete.msw_rescale();
    m_bmp_delete_focus.msw_rescale();

    for (auto group : m_og_settings)
        group->msw_rescale();
#endif
}

void ObjectSettings::sys_color_changed()
{
#if !NEW_OBJECT_SETTING
    // [EVENT] Refreshes cached bitmaps when the system theme changes.
    m_og->sys_color_changed();

    for (auto group : m_og_settings)
        group->sys_color_changed();
#endif
}

}} // namespace Slic3r::GUI
