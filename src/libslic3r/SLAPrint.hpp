#ifndef slic3r_SLAPrint_hpp_
#define slic3r_SLAPrint_hpp_

// [INTENT] SLAPrint.hpp — type declarations for the SLA (resin/photopolymer) print pipeline.
//
// Architecture:
//   SLAPrintObject : per-model-object node. Owns hollowing/support data and per-object slice vectors.
//   SLAPrint       : top-level FSM (Finite-State Machine). Owns PrintObjects, drives step execution,
//                    aggregates PrintLayer list for the rasterizer.
//   SLAArchive     : abstract rasterization sink; concrete impls encode layer bitmaps to .sl1 / .pwx etc.
//
// Pipeline step order (per-object, in SLAPrintObjectStep order):
//   slaposHollowing → slaposDrillHoles → slaposObjectSlice → slaposSupportPoints
//   → slaposSupportTree → slaposPad → slaposSliceSupports
// Then two print-level steps: slapsMergeSlicesAndEval → slapsRasterize
//
// [COUPLING] Inherits from PrintObjectBaseWithState / PrintBaseWithState (PrintBase.hpp) for the
//            step-state FSM, cancellation protocol, and background thread management.
//
// [CONCURRENCY] SLAArchive::draw_layers() is TBB-parallelised over layer indices.
//               SLAPrint::process() is serial across object steps but each step may use TBB internally.
//
// [STATE] SLAPrintObject caches:
//   m_transformed_rmesh (lazy CachedObject), m_model_slices, m_slice_index, m_supportdata, m_hollowing_data.
//   Invalidation is propagated through the step-dependency chain in invalidate_step().
//
// [HAZARD H803] SLAPrintObject::SupportData inherits from sla::SupportableMesh by value.
//               SupportData(const TriangleMesh& t) copies t.its — for large meshes this is an
//               expensive O(V+T) copy at construction time, not lazy. Any config change that
//               invalidates slaposHollowing triggers a full re-copy of the mesh.
//
// [HAZARD H804] SLAPrintObject::HollowingData::hollow_mesh_with_holes and
//               hollow_mesh_with_holes_trimmed are declared mutable — they are lazily populated
//               inside const accessors. No mutex guards these fields. Concurrent const calls
//               (e.g., GUI preview thread + slicing thread) can race on the lazy population.
//
// [HAZARD H805] SLAPrint::m_printer is a raw pointer to SLAArchive (set via set_printer()).
//               Lifetime is not managed by SLAPrint — the archive must outlive the print.
//               If the archive is destroyed before SLAPrint::process() completes (e.g., on UI
//               reset), the rasterization step dereferences a dangling pointer.
//
// [HAZARD H806] SLAPrint::PrintLayer stores SliceRecord as reference_wrapper<const SliceRecord>.
//               SliceRecords live in SLAPrintObject::m_slice_index. If any object's step is
//               invalidated after PrintLayer construction but before slapsRasterize, the referenced
//               SliceRecords may be destroyed, leaving dangling references in m_printer_input.

#include <cstdint>
#include <mutex>
#include "PrintBase.hpp"
#include "SLA/RasterBase.hpp"
#include "SLA/SupportTree.hpp"
#include "Execution/ExecutionTBB.hpp"
#include "Point.hpp"
#include "MTUtils.hpp"
#include "Zipper.hpp"

namespace Slic3r {

// [INTENT] SLAPrintStep: print-level steps applied after all objects are processed.
// slapsMergeSlicesAndEval: aggregate per-object SliceRecords into PrintLayer list + compute stats.
// slapsRasterize: encode each PrintLayer into a raster bitmap via SLAArchive.
enum SLAPrintStep : unsigned int { slapsMergeSlicesAndEval, slapsRasterize, slapsCount };

// [INTENT] SLAPrintObjectStep: per-object processing steps, must run in this order.
// Dependencies are enforced in SLAPrintObject::invalidate_step().
// slaposHollowing      → uses AABBMesh SDF to create interior offset surface
// slaposDrillHoles     → boolean-subtracts drain holes from hollowed mesh
// slaposObjectSlice    → slices mesh into layer polygons (ExPolygons per layer)
// slaposSupportPoints  → automatic/manual placement of support anchor points
// slaposSupportTree    → constructs pillar+bridge+pad mesh from support points
// slaposPad            → generates raft/pad base polygon
// slaposSliceSupports  → slices support tree mesh into layer polygons
enum SLAPrintObjectStep : unsigned int {
    slaposHollowing,
    slaposDrillHoles,
    slaposObjectSlice,
    slaposSupportPoints,
    slaposSupportTree,
    slaposPad,
    slaposSliceSupports,
    slaposCount
};

class SLAPrint;
class GLCanvas;

using _SLAPrintObjectBase = PrintObjectBaseWithState<SLAPrint, SLAPrintObjectStep, slaposCount>;

// Layers according to quantized height levels. This will be consumed by
// the printer (rasterizer) in the SLAPrint class.
// using coord_t = int64_t;

enum SliceOrigin { soSupport, soModel };

// [INTENT] SLAPrintObject: per-model-object node in the SLA pipeline FSM.
//   Owns all per-object state: transformed mesh cache, hollowing interior, support tree,
//   slice index, and per-object step masks.
// [STATE] Step lifecycle:
//   slaposHollowing → slaposDrillHoles → slaposObjectSlice →
//   slaposSupportPoints → slaposSupportTree → slaposPad → slaposSliceSupports
//   Each step invalidates all subsequent steps via invalidate_step().
// [COUPLING] Holds a back-pointer to SLAPrint (m_print) for cross-propagating
//   invalidation from object steps to print-level steps.
class SLAPrintObject : public _SLAPrintObjectBase
{
private: // Prevents erroneous use by other classes.
    using Inherited = _SLAPrintObjectBase;

public:
    // I refuse to grantee copying (Tamas)
    SLAPrintObject(const SLAPrintObject&)            = delete;
    SLAPrintObject& operator=(const SLAPrintObject&) = delete;

    // [INTENT] config()/trafo()/is_left_handed(): read-only accessors for
    //   the per-object configuration and 3D transformation applied during slicing.
    const SLAPrintObjectConfig& config() const { return m_config; }
    const Transform3d&          trafo() const { return m_trafo; }
    bool                        is_left_handed() const { return m_left_handed; }

    // [INTENT] Instance: a placed copy of this SLAPrintObject on the build plate.
    //   SLA differs from FFF: all instances share the same X/Y rotation and scale
    //   (enforced by sla_trafo()). Only Z-rotation and XY-shift vary per instance.
    struct Instance
    {
        Instance(ObjectID inst_id, const Point& shft, float rot) : instance_id(inst_id), shift(shft), rotation(rot) {}
        bool operator==(const Instance& rhs) const
        {
            return this->instance_id == rhs.instance_id && this->shift == rhs.shift && this->rotation == rhs.rotation;
        }
        // ID of the corresponding ModelInstance.
        ObjectID instance_id;
        // Slic3r::Point objects in scaled G-code coordinates
        Point shift;
        // Rotation along the Z axis, in radians.
        float rotation;
    };
    const std::vector<Instance>& instances() const { return m_instances; }

    bool         has_mesh(SLAPrintObjectStep step) const;
    TriangleMesh get_mesh(SLAPrintObjectStep step) const;

    // Get a support mesh centered around origin in XY, and with zero rotation around Z applied.
    // Support mesh is only valid if this->is_step_done(slaposSupportTree) is true.
    const TriangleMesh& support_mesh() const;
    // Get a pad mesh centered around origin in XY, and with zero rotation around Z applied.
    // Support mesh is only valid if this->is_step_done(slaposPad) is true.
    const TriangleMesh& pad_mesh() const;

    // Ready after this->is_step_done(slaposDrillHoles) is true
    const indexed_triangle_set& hollowed_interior_mesh() const;

    // Get the mesh that is going to be printed with all the modifications
    // like hollowing and drilled holes.
    const TriangleMesh& get_mesh_to_print() const
    {
        return (m_hollowing_data && is_step_done(slaposDrillHoles)) ? m_hollowing_data->hollow_mesh_with_holes_trimmed : transformed_mesh();
    }

    const TriangleMesh& get_mesh_to_slice() const
    {
        return (m_hollowing_data && is_step_done(slaposDrillHoles)) ? m_hollowing_data->hollow_mesh_with_holes : transformed_mesh();
    }

    // This will return the transformed mesh which is cached
    const TriangleMesh& transformed_mesh() const;

    sla::SupportPoints transformed_support_points() const;
    sla::DrainHoles    transformed_drainhole_points() const;

    // Get the needed Z elevation for the model geometry if supports should be
    // displayed. This Z offset should also be applied to the support
    // geometries. Note that this is not the same as the value stored in config
    // as the pad height also needs to be considered.
    double get_elevation() const;

    // This method returns the needed elevation according to the processing
    // status. If the supports are not ready, it is zero, if they are and the
    // pad is not, then without the pad, otherwise the full value is returned.
    double get_current_elevation() const;

    // This method returns the support points of this SLAPrintObject.
    const std::vector<sla::SupportPoint>& get_support_points() const;

    // [INTENT] SliceRecord: one printable layer's bookkeeping for a single SLAPrintObject.
    //   Stores quantized print_z (coord_t key into m_printer_input) and the floating-point
    //   slice_z used for actual mesh slicing.
    // [STATE] m_model_slices_idx / m_support_slices_idx are raw indices into
    //   SLAPrintObject::m_model_slices / m_supportdata->support_slices.
    //   NONE (size_t(-1)) means the slice was not set for that origin.
    // [COUPLING] m_po is a raw back-pointer to the owning SLAPrintObject.
    //   See [HAZARD H806]: if the object is invalidated after PrintLayer construction
    //   these indices and this pointer may dangle.
    class SliceRecord
    {
    public:
        // this will be the max limit of size_t
        static const size_t NONE = size_t(-1);

        static const SliceRecord EMPTY;

    private:
        coord_t m_print_z = 0;   // Top of the layer
        float   m_slice_z = 0.f; // Exact level of the slice
        float   m_height  = 0.f; // Height of the sliced layer

        size_t                m_model_slices_idx   = NONE;
        size_t                m_support_slices_idx = NONE;
        const SLAPrintObject* m_po                 = nullptr;

    public:
        SliceRecord(coord_t key, float slicez, float height) : m_print_z(key), m_slice_z(slicez), m_height(height) {}

        // The key will be the integer height level of the top of the layer.
        coord_t print_level() const { return m_print_z; }

        // Returns the exact floating point Z coordinate of the slice
        float slice_level() const { return m_slice_z; }

        // Returns the current layer height
        float layer_height() const { return m_height; }

        bool is_valid() const { return m_po && !std::isnan(m_slice_z); }

        const SLAPrintObject* print_obj() const { return m_po; }

        // Methods for setting the indices into the slice vectors.
        void set_model_slice_idx(const SLAPrintObject& po, size_t id)
        {
            m_po               = &po;
            m_model_slices_idx = id;
        }

        void set_support_slice_idx(const SLAPrintObject& po, size_t id)
        {
            m_po                 = &po;
            m_support_slices_idx = id;
        }

        const ExPolygons& get_slice(SliceOrigin o) const;
        size_t            get_slice_idx(SliceOrigin o) const { return o == soModel ? m_model_slices_idx : m_support_slices_idx; }
    };

private:
    template<class T> inline static T level(const SliceRecord& sr)
    {
        static_assert(std::is_arithmetic<T>::value, "Arithmetic only!");
        return std::is_integral<T>::value ? T(sr.print_level()) : T(sr.slice_level());
    }

    template<class T> inline static SliceRecord create_slice_record(T val)
    {
        static_assert(std::is_arithmetic<T>::value, "Arithmetic only!");
        return std::is_integral<T>::value ? SliceRecord{coord_t(val), 0.f, 0.f} : SliceRecord{0, float(val), 0.f};
    }

    // This is a template method for searching the slice index either by
    // an integer key: print_level or a floating point key: slice_level.
    // The eps parameter gives the max deviation in + or - direction.
    //
    // This method can be used in const or non-const contexts as well.
    template<class Container, class T>
    static auto closest_slice_record(Container& cont, T lvl, T eps = std::numeric_limits<T>::max()) -> decltype(cont.begin())
    {
        if (cont.empty())
            return cont.end();
        if (cont.size() == 1 && std::abs(level<T>(cont.front()) - lvl) > eps)
            return cont.end();

        SliceRecord query = create_slice_record(lvl);

        auto it = std::lower_bound(cont.begin(), cont.end(), query,
                                   [](const SliceRecord& r1, const SliceRecord& r2) { return level<T>(r1) < level<T>(r2); });

        if (it == cont.end())
            return it;

        T diff = std::abs(level<T>(*it) - lvl);

        if (it != cont.begin()) {
            auto it_prev   = std::prev(it);
            T    diff_prev = std::abs(level<T>(*it_prev) - lvl);
            if (diff_prev < diff) {
                diff = diff_prev;
                it   = it_prev;
            }
        }

        if (diff > eps)
            it = cont.end();

        return it;
    }

    const std::vector<ExPolygons>& get_model_slices() const { return m_model_slices; }
    const std::vector<ExPolygons>& get_support_slices() const;

public:
    // /////////////////////////////////////////////////////////////////////////
    //
    // These methods should be callable on the client side (e.g. UI thread)
    // when the appropriate steps slaposObjectSlice and slaposSliceSupports
    // are ready. All the print objects are processed before slapsRasterize so
    // it is safe to call them during and/or after slapsRasterize.
    //
    // /////////////////////////////////////////////////////////////////////////

    // Retrieve the slice index.
    const std::vector<SliceRecord>& get_slice_index() const { return m_slice_index; }

    // Search slice index for the closest slice to given print_level.
    // max_epsilon gives the allowable deviation of the returned slice record's
    // level.
    const SliceRecord& closest_slice_to_print_level(coord_t print_level, coord_t max_epsilon = std::numeric_limits<coord_t>::max()) const
    {
        auto it = closest_slice_record(m_slice_index, print_level, max_epsilon);
        return it == m_slice_index.end() ? SliceRecord::EMPTY : *it;
    }

    // Search slice index for the closest slice to given slice_level.
    // max_epsilon gives the allowable deviation of the returned slice record's
    // level. Use SliceRecord::is_valid() to check the result.
    const SliceRecord& closest_slice_to_slice_level(float slice_level, float max_epsilon = std::numeric_limits<float>::max()) const
    {
        auto it = closest_slice_record(m_slice_index, slice_level, max_epsilon);
        return it == m_slice_index.end() ? SliceRecord::EMPTY : *it;
    }

protected:
    // to be called from SLAPrint only.
    friend class SLAPrint;

    SLAPrintObject(SLAPrint* print, ModelObject* model_object);
    ~SLAPrintObject();

    void config_apply(const ConfigBase& other, bool ignore_nonexistent = false) { m_config.apply(other, ignore_nonexistent); }
    void config_apply_only(const ConfigBase& other, const t_config_option_keys& keys, bool ignore_nonexistent = false)
    {
        m_config.apply_only(other, keys, ignore_nonexistent);
    }

    void set_trafo(const Transform3d& trafo, bool left_handed)
    {
        m_transformed_rmesh.invalidate([this, &trafo, left_handed]() {
            m_trafo       = trafo;
            m_left_handed = left_handed;
        });
    }

    template<class InstVec> inline void set_instances(InstVec&& instances) { m_instances = std::forward<InstVec>(instances); }

    // Invalidates the step, and its depending steps in SLAPrintObject and SLAPrint.
    bool invalidate_step(SLAPrintObjectStep step);
    bool invalidate_all_steps();
    // Invalidate steps based on a set of parameters changed.
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key>& opt_keys);

    // Which steps have to be performed. Implicitly: all
    // to be accessible from SLAPrint
    std::vector<bool> m_stepmask;

private:
    // [INTENT] Object-specific configuration pulled from the layer hierarchy.
    // [STATE] Applied by SLAPrint::apply() via config_apply() / config_apply_only().
    SLAPrintObjectConfig m_config;

    // [INTENT] Combined transform: Translation in Z + Rotation by Y and Z + Scaling / Mirroring.
    // [STATE] Set by set_trafo(); changing it invalidates m_transformed_rmesh.
    // [COUPLING] sla_trafo() in SLAPrint builds this from instances.front() only — see H806.
    Transform3d m_trafo = Transform3d::Identity();
    // m_trafo is left handed -> 3x3 affine transformation has negative determinant.
    bool m_left_handed = false;

    std::vector<Instance> m_instances;

    // [INTENT] Per-layer ExPolygon contours from slicing the object mesh (or hollowed mesh).
    //   Indexed by m_slice_index[i].m_model_slices_idx.
    std::vector<ExPolygons> m_model_slices;

    // [INTENT] Unified slice index mapping quantized Z levels to model/support slice vectors.
    //   Kept sorted by print_level; searched with closest_slice_record<T>().
    // [STATE] Populated by slice_model(); cleared on slaposObjectSlice invalidation.
    std::vector<SliceRecord> m_slice_index;

    // [INTENT] Float Z heights corresponding to each model slice (subset of m_slice_index).
    //   Fed directly into slice_mesh_ex() as the height grid.
    std::vector<float> m_model_height_levels;

    // [INTENT] Lazy-computed, cached copy of the raw mesh after applying m_trafo.
    // [STATE] CachedObject<T> invalidates on set_trafo(); recomputes on first get().
    // [CONCURRENCY] CachedObject uses internal locking; see also [HAZARD H804].
    mutable CachedObject<TriangleMesh> m_transformed_rmesh;

    // [INTENT] SupportData: bundles the support-eligible mesh with computed support geometry.
    //   Inherits sla::SupportableMesh by value (indexed_triangle_set copy + AABBMesh).
    // [HAZARD H803] Constructor copies t.its — O(V+T) allocation, not lazy.
    // [STATE] Created in slice_model() when supports or pad is enabled;
    //         reset (freed) at the start of slaposHollowing invalidation.
    class SupportData : public sla::SupportableMesh
    {
    public:
        sla::SupportTree::UPtr  support_tree_ptr; // the supports
        std::vector<ExPolygons> support_slices;   // sliced supports
        TriangleMesh            tree_mesh, pad_mesh, full_mesh;

        inline SupportData(const TriangleMesh& t) : sla::SupportableMesh{t.its, {}, {}} {}

        sla::SupportTree::UPtr& create_support_tree(const sla::JobController& ctl)
        {
            support_tree_ptr = sla::SupportTree::create(*this, ctl);
            tree_mesh        = TriangleMesh{support_tree_ptr->retrieve_mesh(sla::MeshType::Support)};
            return support_tree_ptr;
        }

        void create_pad(const ExPolygons& blueprint, const sla::PadConfig& pcfg)
        {
            if (!support_tree_ptr)
                return;

            support_tree_ptr->add_pad(blueprint, pcfg);
            pad_mesh = TriangleMesh{support_tree_ptr->retrieve_mesh(sla::MeshType::Pad)};
        }
    };

    std::unique_ptr<SupportData> m_supportdata;

    // [INTENT] HollowingData: caches the SDF interior and the resulting hollowed meshes.
    //   hollow_mesh_with_holes — full interior-removed mesh (for slicing).
    //   hollow_mesh_with_holes_trimmed — same but with interior triangles removed (for display/printing).
    // [HAZARD H804] Both mesh fields are mutable for lazy population inside const accessors.
    //   No mutex protects them; concurrent const reads from GUI + slicing threads can race.
    class HollowingData
    {
    public:
        sla::InteriorPtr     interior;
        mutable TriangleMesh hollow_mesh_with_holes; // caching the complete hollowed mesh
        mutable TriangleMesh hollow_mesh_with_holes_trimmed;
    };

    std::unique_ptr<HollowingData> m_hollowing_data;
};

using PrintObjects = std::vector<SLAPrintObject*>;

using SliceRecord = SLAPrintObject::SliceRecord;

class TriangleMesh;

// [INTENT] SLAPrintStatistics: aggregated result data produced by slapsMergeSlicesAndEval.
//   Consumed by: UI progress display, output filename placeholders, total cost/weight labeling.
// [STATE] Populated by SLAPrint::Steps::merge_slices_and_eval_stats(); cleared on step invalidation.
// [COUPLING] config() / placeholders() build a DynamicConfig for PlaceholderParser substitution
//   in output_filename(). The key set must stay in sync with finalize_output_path().
struct SLAPrintStatistics
{
    SLAPrintStatistics() { clear(); }
    double              estimated_print_time;
    double              objects_used_material;
    double              support_used_material;
    size_t              slow_layers_count;
    size_t              fast_layers_count;
    double              total_cost;
    double              total_weight;
    std::vector<double> layers_times;

    // Config with the filled in print statistics.
    DynamicConfig config() const;
    // Config with the statistics keys populated with placeholder strings.
    static DynamicConfig placeholders();
    // Replace the print statistics placeholders in the path.
    std::string finalize_output_path(const std::string& path_in) const;

    void clear()
    {
        estimated_print_time  = 0.;
        objects_used_material = 0.;
        support_used_material = 0.;
        slow_layers_count     = 0;
        fast_layers_count     = 0;
        total_cost            = 0.;
        total_weight          = 0.;
        layers_times.clear();
    }
};

// [INTENT] SLAArchive: abstract rasterization sink.
//   Concrete implementations (SL1Archive, PWXArchive, etc.) convert layer bitmaps into
//   the format-specific archive bytes (ZIP of PNGs, CBDDLP, etc.).
// [CONCURRENCY] draw_layers() parallelises over layer indices using TBB (ExecutionTBB).
//   create_raster() and get_encoder() are called concurrently — must be const and thread-safe.
// [HAZARD H805] SLAPrint holds a raw SLAArchive* (m_printer) with no lifetime management.
//   If the archive is destroyed before SLAPrint::process() returns, rasterize() dereferences
//   a dangling pointer. The caller is responsible for ensuring lifetime.
// [COUPLING] set_printer() invalidates slapsRasterize so that any prior raster output is
//   discarded when the archive target changes.
class SLAArchive
{
protected:
    std::vector<sla::EncodedRaster> m_layers;

    virtual std::unique_ptr<sla::RasterBase> create_raster() const = 0;
    virtual sla::RasterEncoder               get_encoder() const   = 0;

public:
    virtual ~SLAArchive() = default;

    virtual void apply(const SLAPrinterConfig& cfg) = 0;

    // Fn have to be thread safe: void(sla::RasterBase& raster, size_t lyrid);
    template<class Fn, class CancelFn, class EP = ExecutionTBB>
    void draw_layers(size_t layer_num, Fn&& drawfn, CancelFn cancelfn = []() { return false; }, const EP& ep = {})
    {
        m_layers.resize(layer_num);
        execution::for_each(
            ep, size_t(0), m_layers.size(),
            [this, &drawfn, &cancelfn](size_t idx) {
                if (cancelfn())
                    return;

                sla::EncodedRaster& enc = m_layers[idx];
                auto                rst = create_raster();
                drawfn(*rst, idx);
                enc = rst->encode(get_encoder());
            },
            execution::max_concurrency(ep));
    }
};

/**
 * @brief This class is the high level FSM for the SLA printing process.
 *
 * It should support the background processing framework and contain the
 * metadata for the support geometries and their slicing. It should also
 * dispatch the SLA printing configuration values to the appropriate calculation
 * steps.
 */
class SLAPrint : public PrintBaseWithState<SLAPrintStep, slapsCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintBaseWithState<SLAPrintStep, slapsCount> Inherited;

    class Steps; // See SLAPrintSteps.cpp

public:
    SLAPrint() : m_stepmask(slapsCount, true) {}

    virtual ~SLAPrint() override { this->clear(); }

    PrinterTechnology technology() const noexcept override { return ptSLA; }

    void clear() override;
    bool empty() const override { return m_objects.empty(); }
    // List of existing PrintObject IDs, to remove notifications for non-existent IDs.
    std::vector<ObjectID> print_object_ids() const override;
    ApplyStatus           apply(const Model& model, DynamicPrintConfig config) override;
    void                  set_task(const TaskParams& params) override;
    void                  process(long long* time_cost_with_cache = nullptr, bool use_cache = false) override;
    void                  finalize() override;
    // Returns true if an object step is done on all objects and there's at least one object.
    bool is_step_done(SLAPrintObjectStep step) const;
    // Returns true if the last step was finished with success.
    bool finished() const override { return this->is_step_done(slaposSliceSupports) && this->Inherited::is_step_done(slapsRasterize); }

    const PrintObjects& objects() const { return m_objects; }
    // PrintObject by its ObjectID, to be used to uniquely bind slicing warnings to their source PrintObjects
    // in the notification center.
    const SLAPrintObject* get_print_object_by_model_object_id(ObjectID object_id) const
    {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
                               [object_id](const SLAPrintObject* obj) { return obj->model_object()->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }
    const SLAPrintObject* get_object(ObjectID object_id) const
    {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
                               [object_id](const SLAPrintObject* obj) { return obj->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }

    const SLAPrintConfig&       print_config() const { return m_print_config; }
    const SLAPrinterConfig&     printer_config() const { return m_printer_config; }
    const SLAMaterialConfig&    material_config() const { return m_material_config; }
    const SLAPrintObjectConfig& default_object_config() const { return m_default_object_config; }

    // Extracted value from the configuration objects
    Vec3d relative_correction() const;

    // Return sla tansformation for a given model_object
    Transform3d sla_trafo(const ModelObject& model_object) const;

    std::string output_filename(const std::string& filename_base = std::string()) const override;

    const SLAPrintStatistics& print_statistics() const { return m_print_statistics; }

    StringObjectException validate(StringObjectException*                  warning           = nullptr,
                                   Polygons*                               collison_polygons = nullptr,
                                   std::vector<std::pair<Polygon, float>>* height_polygons   = nullptr) const override;

    // [INTENT] An aggregation of SliceRecord-s from all the print objects for each
    // occupied layer. Slice record levels dont have to match exactly.
    // They are unified if the level difference is within +/- SCALED_EPSILON
    // [HAZARD H806] m_slices stores reference_wrapper<const SliceRecord>.
    //   If any SLAPrintObject is invalidated after this list is built (in
    //   initialize_printer_input()) but before slapsRasterize finishes,
    //   these references dangle. No validity check is performed at use time.
    class PrintLayer
    {
        coord_t m_level;

        // The collection of slice records for the current level.
        std::vector<std::reference_wrapper<const SliceRecord>> m_slices;

        ExPolygons m_transformed_slices;

        template<class Container> void transformed_slices(Container&& c) { m_transformed_slices = std::forward<Container>(c); }

        friend class SLAPrint::Steps;

    public:
        explicit PrintLayer(coord_t lvl) : m_level(lvl) {}

        // for being sorted in their container (see m_printer_input)
        bool operator<(const PrintLayer& other) const { return m_level < other.m_level; }

        void add(const SliceRecord& sr) { m_slices.emplace_back(sr); }

        coord_t level() const { return m_level; }

        auto slices() const -> const decltype(m_slices)& { return m_slices; }

        const ExPolygons& transformed_slices() const { return m_transformed_slices; }
    };

    // The aggregated and leveled print records from various objects.
    // TODO: use this structure for the preview in the future.
    const std::vector<PrintLayer>& print_layers() const { return m_printer_input; }

    void set_printer(SLAArchive* archiver);

private:
    // Implement same logic as in SLAPrintObject
    bool invalidate_step(SLAPrintStep st);

    // Invalidate steps based on a set of parameters changed.
    // [HAZARD H807] assert(false) fires in debug for unrecognized opt_keys.
    //   In release builds the assertion is silently skipped, so newly added config
    //   keys that are not listed in steps_full/steps_rasterize/steps_ignore will
    //   never invalidate any step — changes are silently ignored.
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key>& opt_keys, bool& invalidate_all_model_objects);

    // [INTENT] Four separate config layers. apply() diffs each and calls
    //   invalidate_state_by_config_options() for each changed key set.
    SLAPrintConfig       m_print_config;
    SLAPrinterConfig     m_printer_config;
    SLAMaterialConfig    m_material_config;
    SLAPrintObjectConfig m_default_object_config;

    // [STATE] Heap-allocated SLAPrintObjects; ownership is manual (new/delete in apply()).
    PrintObjects m_objects;
    // [STATE] Per-print-step mask: false = step disabled by set_task(); restored by finalize().
    std::vector<bool> m_stepmask;

    // [INTENT] Flattened, Z-sorted list of all per-object SliceRecords merged across objects.
    //   Built by initialize_printer_input() during slapsMergeSlicesAndEval.
    //   Consumed by slapsRasterize. See [HAZARD H806] for dangling reference risk.
    std::vector<PrintLayer> m_printer_input;

    // [INTENT] Concrete rasterization sink provided by the caller via set_printer().
    // [HAZARD H805] Raw pointer — no RAII lifetime management. Caller must ensure
    //   the archive outlives this SLAPrint instance (or at least outlives process()).
    SLAArchive* m_printer = nullptr;

    // Estimated print time, material consumed.
    SLAPrintStatistics m_print_statistics;

    // [INTENT] StatusReporter: funnel for status percentage + log messages.
    //   Wraps set_status() with Boost.Log output and tracks the last reported percentage.
    class StatusReporter
    {
        double m_st = 0;

    public:
        void operator()(
            SLAPrint& p, double st, const std::string& msg, unsigned flags = SlicingStatus::DEFAULT, const std::string& logmsg = "");

        double status() const { return m_st; }
    } m_report_status;

    friend SLAPrintObject;
};

// Helper functions:

bool is_zero_elevation(const SLAPrintObjectConfig& c);

sla::SupportTreeConfig make_support_cfg(const SLAPrintObjectConfig& c);

sla::PadConfig::EmbedObject builtin_pad_cfg(const SLAPrintObjectConfig& c);

sla::PadConfig make_pad_cfg(const SLAPrintObjectConfig& c);

bool validate_pad(const indexed_triangle_set& pad, const sla::PadConfig& pcfg);

} // namespace Slic3r

#endif /* slic3r_SLAPrint_hpp_ */
