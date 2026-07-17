// PNP fork (wayfinder ticket F05): per-plate temp 3MF export for pnp_cli.
// See PnpModelExport.hpp for the contract.

#include "PnpModelExport.hpp"

#include <set>
#include <utility>
#include <vector>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "PartPlate.hpp"

namespace Slic3r {
namespace GUI {

namespace {

bool pnp_export_fail(std::string* error, const std::string& reason)
{
    BOOST_LOG_TRIVIAL(error) << "export_plate_3mf_for_pnp: " << reason;
    if (error != nullptr)
        *error = reason;
    return false;
}

} // anonymous namespace

bool export_plate_3mf_for_pnp(int plate_idx, const boost::filesystem::path& path, std::string* error)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return pnp_export_fail(error, "no plater available");

    if (path.empty())
        return pnp_export_fail(error, "empty output path");

    PartPlateList& plate_list = plater->get_partplate_list();
    if (plate_idx < 0 || plate_idx >= plate_list.get_plate_count())
        return pnp_export_fail(error, (boost::format("plate index %1% out of range (plate count %2%)")
                                       % plate_idx % plate_list.get_plate_count()).str());

    PartPlate* plate = plate_list.get_plate(plate_idx);
    if (plate == nullptr)
        return pnp_export_fail(error, (boost::format("plate %1% not found") % plate_idx).str());
    if (plate->empty())
        return pnp_export_fail(error, (boost::format("plate %1% has no objects") % plate_idx).str());

    Model& src_model = plater->model();

    // Plate contents are re-homed so the plate origin becomes (0,0): pnp_cli
    // sees a normal single-bed scene. Only XY is shifted; plates never differ
    // in Z. Instance transforms (rotation, mirror, any scale — including
    // non-uniform) pass through untouched: no baking, no normalization.
    const Vec3d plate_origin = plate->get_origin();
    const Vec3d local_shift(-plate_origin.x(), -plate_origin.y(), 0.0);

    // Build a temporary model holding only this plate's instances. ModelObject
    // copies keep volumes (parts, modifiers, negative volumes), per-object /
    // per-volume config (Orca key names, including extruder/filament
    // assignments) and layer ranges intact, so the store path writes them into
    // Metadata/model_settings.config exactly as for a full project save.
    Model plate_model;
    std::set<std::pair<int, int>> exported_obj_insts; // (object, instance) indices in plate_model
    for (int obj_idx = 0; obj_idx < (int)src_model.objects.size(); ++obj_idx) {
        const ModelObject* src_object = src_model.objects[obj_idx];

        bool any_on_plate = false;
        for (int inst_idx = 0; inst_idx < (int)src_object->instances.size(); ++inst_idx) {
            if (plate->contain_instance(obj_idx, inst_idx)) {
                any_on_plate = true;
                break;
            }
        }
        if (!any_on_plate)
            continue;

        ModelObject* dst_object = plate_model.add_object(*src_object);
        // Drop instances that live on other plates (reverse order keeps indices stable).
        for (int inst_idx = (int)src_object->instances.size() - 1; inst_idx >= 0; --inst_idx) {
            if (!plate->contain_instance(obj_idx, inst_idx))
                dst_object->delete_instance((size_t)inst_idx);
        }
        // Translate the surviving instances to plate-local coordinates.
        const int dst_obj_idx = (int)plate_model.objects.size() - 1;
        for (int inst_idx = 0; inst_idx < (int)dst_object->instances.size(); ++inst_idx) {
            ModelInstance* inst = dst_object->instances[inst_idx];
            inst->set_offset(inst->get_offset() + local_shift);
            exported_obj_insts.insert({dst_obj_idx, inst_idx});
        }
    }

    if (plate_model.objects.empty())
        return pnp_export_fail(error, (boost::format("plate %1% has no exportable instances") % plate_idx).str());

    // Single plate entry describing the whole exported (single-bed) scene.
    PlateData* plate_data = new PlateData(0, exported_obj_insts, /*lock_state=*/false);
    plate_data->plate_name = plate->get_plate_name();
    plate_data->config.apply(*plate->config());

    PlateDataPtrs plate_data_list;
    plate_data_list.push_back(plate_data);

    // store_bbs_3mf dereferences the config unconditionally when writing
    // model_settings.config / slice_info.config, so it must be non-null.
    DynamicPrintConfig cfg = wxGetApp().preset_bundle->full_config_secure();

    StoreParams store_params;
    store_params.path            = into_u8(from_path(path));
    store_params.model           = &plate_model;
    store_params.plate_data_list = plate_data_list;
    store_params.config          = &cfg;
    // Silence: temp export, never becomes the project filename. SkipAuxiliary:
    // no Auxiliaries/ payload. No gcode, no thumbnails, no SplitModel — a
    // plain single 3D/3dmodel.model with one <build> holding only this
    // plate's instances.
    store_params.strategy        = SaveStrategy::Silence | SaveStrategy::SkipAuxiliary | SaveStrategy::Zip64;

    const bool store_result = Slic3r::store_bbs_3mf(store_params);

    release_PlateData_list(plate_data_list);

    if (!store_result)
        return pnp_export_fail(error, (boost::format("failed to write 3MF for plate %1% to %2%")
                                       % plate_idx % path.string()).str());

    BOOST_LOG_TRIVIAL(info) << "export_plate_3mf_for_pnp: wrote plate " << plate_idx
                            << " (" << plate_model.objects.size() << " objects) to " << store_params.path;
    return true;
}

} // namespace GUI
} // namespace Slic3r
