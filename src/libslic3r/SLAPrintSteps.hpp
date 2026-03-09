#ifndef SLAPRINTSTEPS_HPP
#define SLAPRINTSTEPS_HPP

// [INTENT] SLAPrintSteps.hpp — declares SLAPrint::Steps, the command object that
//   implements each SLA pipeline step.
//
// Steps class design:
//   - Constructed once per process() call; holds derived constants (objectstep_scale, ilh, etc.)
//   - execute(SLAPrintObjectStep, obj) dispatches to the per-object step methods.
//   - execute(SLAPrintStep)           dispatches to the print-level step methods.
//   - label() / progressrange()       provide UI progress information.
//
// Progress accounting:
//   - Per-object steps occupy [min_objstatus=0 .. max_objstatus=50]% of total progress.
//   - Print-level steps occupy [50 .. 100]%.
//   - objectstep_scale = (max - min) / (objectcount * 100) normalizes each step weight.
//
// [CONCURRENCY] Steps methods are called from the background processing thread only.
//   report_status() and throw_if_canceled() delegate to SLAPrint's thread-safe primitives.
//
// [COUPLING] Steps accesses SLAPrint private members directly (friend class SLAPrint::Steps
//   is declared in SLAPrint.hpp). Changes to SLAPrint data layout require updating Steps.

#include <random>

#include <libslic3r/SLAPrint.hpp>

#include <libslic3r/SLA/Hollowing.hpp>
#include <libslic3r/SLA/SupportTree.hpp>

namespace Slic3r {

class SLAPrint::Steps
{
private:
    SLAPrint* m_print = nullptr;
    // [INTENT] m_rng: Mersenne Twister used to add tiny random jitter to drain hole
    //   positions/normals during drilling, preventing degenerate CGAL mesh booleans.
    std::mt19937 m_rng;

public:
    // [INTENT] Progress range allocation: per-object steps use [0..50]%, print steps use [50..100]%.
    static const constexpr unsigned min_objstatus = 0;
    static const constexpr unsigned max_objstatus = 50;

private:
    const size_t objcount;

    // [INTENT] Initial layer height in mm (double), float, and scaled (coord_t).
    //   Precomputed once at Steps construction from material_config.initial_layer_height.
    const double  ilhd;
    const float   ilh;
    const coord_t ilhs;

    // [INTENT] objectstep_scale: maps each step's [0..100] progress sub-range into
    //   the [min_objstatus..max_objstatus] global range, accounting for object count.
    //   Formula: (max_objstatus - min_objstatus) / (objcount * 100.0)
    const double objectstep_scale;

    template<class... Args> void report_status(Args&&... args) { m_print->m_report_status(*m_print, std::forward<Args>(args)...); }

    double current_status() const { return m_print->m_report_status.status(); }
    void   throw_if_canceled() const { m_print->throw_if_canceled(); }
    bool   canceled() const { return m_print->canceled(); }
    // [INTENT] initialize_printer_input: merges all SLAPrintObject slice indices
    //   into m_print->m_printer_input (sorted PrintLayer list) for rasterization.
    void initialize_printer_input();

    // [INTENT] apply_printer_corrections: applies absolute_correction offset and
    //   elephant-foot compensation to the slice polygons in-place.
    //   Called at the end of slice_model() (soModel) and slice_supports() (soSupport).
    void apply_printer_corrections(SLAPrintObject& po, SliceOrigin o);

public:
    explicit Steps(SLAPrint* print);

    // [INTENT] hollow_model: compute SDF-based interior, store in m_hollowing_data.
    //   Skipped if hollowing_enable is false.
    void hollow_model(SLAPrintObject& po);
    // [INTENT] drill_holes: boolean-subtract drain hole cylinders from hollowed mesh.
    //   Uses CGAL mesh boolean (plus/minus); adds jitter to avoid degenerate cases.
    //   NOTE: The entire implementation is currently commented out (#if 0 style block).
    void drill_holes(SLAPrintObject& po);
    // [INTENT] slice_model: build m_slice_index, m_model_height_levels, m_model_slices.
    //   Also constructs SupportData if supports/pad are enabled.
    void slice_model(SLAPrintObject& po);
    // [INTENT] support_points: auto-place or copy user-defined SLA support points.
    void support_points(SLAPrintObject& po);
    // [INTENT] support_tree: build the pillar+bridge support mesh from support points.
    void support_tree(SLAPrintObject& po);
    // [INTENT] generate_pad: add raft/pad geometry below the support tree.
    void generate_pad(SLAPrintObject& po);
    // [INTENT] slice_supports: slice the support tree mesh at the same height grid
    //   as the model, storing results in m_supportdata->support_slices.
    void slice_supports(SLAPrintObject& po);

    // [INTENT] merge_slices_and_eval_stats: aggregate all per-object slice records into
    //   m_printer_input, compute material volumes and estimated print time in parallel.
    void merge_slices_and_eval_stats();
    // [INTENT] rasterize: encode each PrintLayer's ExPolygons into bitmap form via
    //   SLAArchive::draw_layers() (TBB-parallelised over layers).
    void rasterize();

    void execute(SLAPrintObjectStep step, SLAPrintObject& obj);
    void execute(SLAPrintStep step);

    static std::string label(SLAPrintObjectStep step);
    static std::string label(SLAPrintStep step);

    double progressrange(SLAPrintObjectStep step) const;
    double progressrange(SLAPrintStep step) const;
};

} // namespace Slic3r

#endif // SLAPRINTSTEPS_HPP
