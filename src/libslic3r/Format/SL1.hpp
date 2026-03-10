// [INTENT] SL1 archive adapter for OrcaSlicer's SLA pipeline.
//
// This format layer does two inverse jobs:
//   1. Export: take an already-processed `SLAPrint`, serialize printer/profile metadata into
//      INI files, and write one encoded raster image per layer into a ZIP container.
//   2. Import: read that ZIP container back, recover display geometry / layer heights from the
//      embedded profile, convert PNG slices into ExPolygons with marching squares, then loft them
//      into an `indexed_triangle_set` for the generic model pipeline.
//
// [COUPLING] The API bridges `SLAPrint`, `SLAPrinterConfig`, `RasterBase`, ZIP helpers, and the
//            generic mesh reconstruction path in `SlicesToTriangleMesh`. Translators need both the
//            SLA print-state model and the core mesh model to reproduce this round trip.
// [MEMORY] `SL1Archive` inherits the base `SLAArchive::m_layers` raster cache. Export is only valid
//          after `SLAArchive::draw_layers()` has populated that vector; no ownership is transferred.
// [HAZARD H1191] Import reconstructs geometry from raster slices, not from original CAD triangles.
//                Loading an `.sl1` archive is therefore lossy and resolution-dependent by design.

#ifndef ARCHIVETRAITS_HPP
#define ARCHIVETRAITS_HPP

#include <string>

#include "libslic3r/Zipper.hpp"
#include "libslic3r/SLAPrint.hpp"

namespace Slic3r {

class SL1Archive : public SLAArchive
{
    // [STATE] Cached printer/display config used to parameterize raster creation and metadata export.
    // `apply()` mutates this snapshot and clears the inherited `m_layers` cache when display geometry changes.
    SLAPrinterConfig m_cfg;

protected:
    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder               get_encoder() const override;

public:
    SL1Archive() = default;
    explicit SL1Archive(const SLAPrinterConfig& cfg) : m_cfg(cfg) {}
    explicit SL1Archive(SLAPrinterConfig&& cfg) : m_cfg(std::move(cfg)) {}

    // [INTENT] Serialize one finished SLA print job into the Prusa SL1 ZIP layout:
    // `config.ini`, `prusaslicer.ini`, then sequential raster image entries.
    // [COUPLING] Assumes `print` has already rasterized layers into `SLAArchive::m_layers`.
    void export_print(Zipper& zipper, const SLAPrint& print, const std::string& projectname = "");
    void export_print(const std::string& fname, const SLAPrint& print, const std::string& projectname = "")
    {
        Zipper zipper(fname);
        export_print(zipper, print, projectname);
    }

    void apply(const SLAPrinterConfig& cfg) override
    {
        auto diff = m_cfg.diff(cfg);
        if (!diff.empty()) {
            // [STATE] Any config delta invalidates previously encoded rasters because display resolution,
            // orientation, mirroring, or gamma may change the exact bytes written per layer.
            m_cfg.apply_only(cfg, diff);
            m_layers = {};
        }
    }
};

// [INTENT] Lightweight metadata-only import used by UI/profile restore paths that need printer settings
// without rebuilding the mesh.
ConfigSubstitutions import_sla_archive(const std::string& zipfname, DynamicPrintConfig& out);

// [INTENT] Full geometry import path for `.sl1`/`.sl1s`: decode raster slices, infer printer geometry,
// and reconstruct a watertight-ish triangle mesh by stacking slice contours.
// [HAZARD H1192] Geometry fidelity is bounded by the archived raster resolution and the marching-squares
// window size supplied by the caller; no original voxel or signed-distance data is preserved.
ConfigSubstitutions import_sla_archive(
    const std::string&       zipfname,
    Vec2i32                  windowsize,
    indexed_triangle_set&    out,
    DynamicPrintConfig&      profile,
    std::function<bool(int)> progr = [](int) { return true; });

inline ConfigSubstitutions import_sla_archive(
    const std::string& zipfname, Vec2i32 windowsize, indexed_triangle_set& out, std::function<bool(int)> progr = [](int) { return true; })
{
    DynamicPrintConfig profile;
    return import_sla_archive(zipfname, windowsize, out, profile, progr);
}

class MissingProfileError : public RuntimeError
{
    using RuntimeError::RuntimeError;
};

} // namespace Slic3r

#endif // ARCHIVETRAITS_HPP
