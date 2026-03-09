#include <unordered_set>

#include <libslic3r/Exception.hpp>
#include <libslic3r/SLAPrintSteps.hpp>
#include <libslic3r/MeshBoolean.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>

// [INTENT] SLAPrintSteps.cpp — concrete implementations of each SLA processing step.
//
// Step implementations in this file:
//   hollow_model()           — SDF interior generation via libslic3r/SLA/Hollowing.hpp
//   drill_holes()            — CGAL boolean subtraction of drain hole cylinders (currently DEAD CODE: commented out)
//   slice_model()            — height grid construction + TriangleMeshSlicer + interior diff
//   support_points()         — auto SupportPointGenerator or copy user-modified points
//   support_tree()           — sla::SupportTree::create() pillar/bridge network
//   generate_pad()           — sla::pad_blueprint() + SupportData::create_pad()
//   slice_supports()         — slice support tree at same height grid as model
//   merge_slices_and_eval_stats() — parallel layer polygon unioning + time/material estimation
//   rasterize()              — SLAArchive::draw_layers() TBB parallel bitmap encoding
//
// [CONCURRENCY] merge_slices_and_eval_stats() and rasterize() both use sla::ccr::for_each
//   (alias for TBB parallel_for). Accumulations are guarded by sla::ccr::SpinningMutex.
//
// [HAZARD H808] drill_holes() body is entirely commented out (lines 337–469).
//   The AABBTreeIndirect traversal call was also commented out (BBS annotation).
//   The function executes as a no-op — drain holes are never drilled in the current build.
//   Any refactoring target must restore this step before hollowing is usable end-to-end.
//
// [COUPLING] FaceHash inner struct (SLAPrintSteps.cpp local): hashes triangles by their
//   cross-product+centroid to identify interior mesh faces after CGAL boolean — purely local,
//   no header exposure. Requires 1e6 integer scaling of float vertices (scaled<int64_t>).

// Need the cylinder method for the the drainholes in hollowing step
#include <libslic3r/SLA/SupportTreeBuilder.hpp>

#include <libslic3r/SLA/Concurrency.hpp>
#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>

#include <libslic3r/ElephantFootCompensation.hpp>
#include <libslic3r/AABBTreeIndirect.hpp>

#include <libslic3r/ClipperUtils.hpp>

#include <boost/log/trivial.hpp>

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

namespace {

const std::array<unsigned, slaposCount> OBJ_STEP_LEVELS = {
    10, // slaposHollowing,
    10, // slaposDrillHoles
    10, // slaposObjectSlice,
    20, // slaposSupportPoints,
    10, // slaposSupportTree,
    10, // slaposPad,
    30, // slaposSliceSupports,
};

std::string OBJ_STEP_LABELS(size_t idx)
{
    switch (idx) {
    case slaposHollowing: return L("Hollowing model");
    case slaposDrillHoles: return L("Drilling holes into model.");
    case slaposObjectSlice: return L("Slicing model");
    case slaposSupportPoints: return L("Generating support points");
    case slaposSupportTree: return L("Generating support tree");
    case slaposPad: return L("Generating pad");
    case slaposSliceSupports: return L("Slicing supports");
    default:;
    }
    assert(false);
    return "Out of bounds!";
}

const std::array<unsigned, slapsCount> PRINT_STEP_LEVELS = {
    10, // slapsMergeSlicesAndEval
    90, // slapsRasterize
};

std::string PRINT_STEP_LABELS(size_t idx)
{
    switch (idx) {
    case slapsMergeSlicesAndEval: return L("Merging slices and calculating statistics");
    case slapsRasterize: return L("Rasterizing layers");
    default:;
    }
    assert(false);
    return "Out of bounds!";
}

} // namespace

// [INTENT] Steps constructor: pre-computes per-object progress scale factor.
// objectstep_scale maps 0–100 sub-step percent into the 10–80% band of the
// overall status bar. ilhd/ilh/ilhs cache initial layer height in three units
// (mm double, mm float, scaled int) to avoid repeated config lookups.
SLAPrint::Steps::Steps(SLAPrint* print)
    : m_print{print}
    , m_rng{std::random_device{}()}
    , objcount{m_print->m_objects.size()}
    , ilhd{m_print->m_material_config.initial_layer_height.getFloat()}
    , ilh{float(ilhd)}
    , ilhs{scaled(ilhd)}
    , objectstep_scale{(max_objstatus - min_objstatus) / (objcount * 100.0)}
{}

// [INTENT] apply_printer_corrections: applies two per-layer dimensional
// corrections to either model or support slices, AFTER slicing completes:
//   1. absolute_correction (clpr_offs): uniform XY offset on every layer.
//   2. elephant_foot_compensation: tapered outward offset on the first
//      faded_layers layers to counteract resin over-curing on the build plate.
// [STATE] Mutates slices in-place via offset_ex / elephant_foot_compensation.
// [COUPLING] elephant_foot_compensation() from ElephantFootCompensation.hpp;
//   offset_ex() from ClipperUtils.hpp.
// [HAZARD] faded_lyrs is clamped to slice_index.size(); if hollowing + pad
//   causes the first slice to be a support layer, model fading starts offset
//   by one layer — no guard for this edge case.
void SLAPrint::Steps::apply_printer_corrections(SLAPrintObject& po, SliceOrigin o)
{
    if (o == soSupport && !po.m_supportdata)
        return;

    auto   faded_lyrs = size_t(po.m_config.faded_layers.getInt());
    double min_w      = m_print->m_printer_config.elefant_foot_min_width.getFloat() / 2.;
    double start_efc  = m_print->m_printer_config.elefant_foot_compensation.getFloat();

    double  doffs     = m_print->m_printer_config.absolute_correction.getFloat();
    coord_t clpr_offs = scaled(doffs);

    faded_lyrs            = std::min(po.m_slice_index.size(), faded_lyrs);
    size_t faded_lyrs_efc = std::max(size_t(1), faded_lyrs - 1);

    auto efc = [start_efc, faded_lyrs_efc](size_t pos) { return (faded_lyrs_efc - pos) * start_efc / faded_lyrs_efc; };

    std::vector<ExPolygons>& slices = o == soModel ? po.m_model_slices : po.m_supportdata->support_slices;

    if (clpr_offs != 0)
        for (size_t i = 0; i < po.m_slice_index.size(); ++i) {
            size_t idx = po.m_slice_index[i].get_slice_idx(o);
            if (idx < slices.size())
                slices[idx] = offset_ex(slices[idx], float(clpr_offs));
        }

    if (start_efc > 0.)
        for (size_t i = 0; i < faded_lyrs; ++i) {
            size_t idx = po.m_slice_index[i].get_slice_idx(o);
            if (idx < slices.size())
                slices[idx] = elephant_foot_compensation(slices[idx], min_w, efc(i));
        }
}

// [INTENT] hollow_model: SDF-based interior void generation.
// Calls sla::generate_interior() which uses OpenVDB or nanoVDB to compute a
// signed-distance-field offset of the transformed mesh inward by
// hollowing_min_thickness mm, then converts back to a triangle mesh.
// The resulting InteriorPtr is stored in HollowingData::interior for later use
// by drill_holes() and slice_model() (where normals are swapped and sliced to
// subtract the cavity from the model cross-section).
// [STATE] Resets m_hollowing_data unconditionally at entry to invalidate any
//   stale result from a prior run before deciding whether to regenerate.
// [HAZARD] If generate_interior() returns a non-null but empty mesh, the
//   hollowing data is NOT created — subsequent steps treat the object as solid.
//   No warning is raised to the user in that specific path (only a log line).
void SLAPrint::Steps::hollow_model(SLAPrintObject& po)
{
    po.m_hollowing_data.reset();

    if (!po.m_config.hollowing_enable.getBool()) {
        BOOST_LOG_TRIVIAL(info) << "Skipping hollowing step!";
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "Performing hollowing step!";

    double               thickness = po.m_config.hollowing_min_thickness.getFloat();
    double               quality   = po.m_config.hollowing_quality.getFloat();
    double               closing_d = po.m_config.hollowing_closing_distance.getFloat();
    sla::HollowingConfig hlwcfg{thickness, quality, closing_d};

    sla::InteriorPtr interior = generate_interior(po.transformed_mesh(), hlwcfg);

    if (!interior || sla::get_mesh(*interior).empty())
        BOOST_LOG_TRIVIAL(warning) << "Hollowed interior is empty!";
    else {
        po.m_hollowing_data.reset(new SLAPrintObject::HollowingData());
        po.m_hollowing_data->interior = std::move(interior);
    }
}

// [INTENT] FaceHash: geometry-based triangle identity for post-boolean mesh
// bookkeeping. After CGAL mesh boolean subtraction, triangle indices are
// invalidated. FaceHash encodes each triangle by its cross-product plus
// centroid (all in scaled int64) so interior mesh triangles can be found in
// the merged result without relying on indices.
// [MEMORY] facehash unordered_set: O(T) strings, where T = triangle count of
//   the interior mesh. Each string is 3×16 hex chars = ~48 bytes per entry.
// [HAZARD] Hash collision is theoretically possible (degenerate triangles with
//   identical cross+centroid after scaling). The empirical risk is very low but
//   a perfect hash is not guaranteed.
struct FaceHash
{
    // A 64 bit number's max hex digits
    static constexpr size_t MAX_NUM_CHARS = 16;

    // A hash is created for each triangle to be identifiable. The hash uses
    // only the triangle's geometric traits, not the index in a particular mesh.
    std::unordered_set<std::string> facehash;

    // Returns the string in reverse, but that is ok for hashing
    static std::array<char, MAX_NUM_CHARS + 1> to_chars(int64_t val)
    {
        std::array<char, MAX_NUM_CHARS + 1> ret;

        static const constexpr char* Conv = "0123456789abcdef";

        auto ptr  = ret.begin();
        auto uval = static_cast<uint64_t>(std::abs(val));
        while (uval) {
            *ptr = Conv[uval & 0xf];
            ++ptr;
            uval = uval >> 4;
        }
        if (val < 0) {
            *ptr = '-';
            ++ptr;
        }
        *ptr = '\0'; // C style string ending

        return ret;
    }

    static std::string hash(const Vec<3, int64_t>& v)
    {
        std::string ret;
        ret.reserve(3 * MAX_NUM_CHARS);

        for (auto val : v)
            ret += to_chars(val).data();

        return ret;
    }

    static std::string facekey(const Vec3i32& face, const std::vector<Vec3f>& vertices)
    {
        // Scale to integer to avoid floating points
        std::array<Vec<3, int64_t>, 3> pts = {scaled<int64_t>(vertices[face(0)]), scaled<int64_t>(vertices[face(1)]),
                                              scaled<int64_t>(vertices[face(2)])};

        // Get the first two sides of the triangle, do a cross product and move
        // that vector to the center of the triangle. This encodes all
        // information to identify an identical triangle at the same position.
        Vec<3, int64_t> a = pts[0] - pts[2], b = pts[1] - pts[2];
        Vec<3, int64_t> c = a.cross(b) + (pts[0] + pts[1] + pts[2]) / 3;

        // Return a concatenated string representation of the coordinates
        return hash(c);
    }

    FaceHash(const indexed_triangle_set& its) : facehash(its.indices.size())
    {
        for (const Vec3i32& face : its.indices)
            facehash.insert(facekey(face, its.vertices));
    }

    bool find(const std::string& key)
    {
        auto it = facehash.find(key);
        return it != facehash.end();
    }
};

// Create exclude mask for triangle removal inside hollowed interiors.
// This is necessary when the interior is already part of the mesh which was
// drilled using CGAL mesh boolean operation. Excluded will be the triangles
// originally part of the interior mesh and triangles that make up the drilled
// hole walls.
// [INTENT] create_exclude_mask: identifies triangles in the post-CGAL-boolean
// merged mesh that should be treated as interior-facing and excluded from
// rasterization. Two classes of triangles are excluded:
//   1. Triangles that were originally part of the SDF interior mesh (matched
//      via FaceHash).
//   2. Triangles that form hole cylinder walls — identified by proximity to a
//      drain hole axis and near-perpendicular normal (dot ~0).
// Neighbors of excluded faces are also excluded (expand the mask by 1-ring)
// to avoid ragged slice boundaries.
// [COUPLING] VertexFaceIndex from AABBTreeIndirect.hpp for O(1) vertex→face
//   neighbor lookup. sla::get_distance() from SLA/Hollowing.hpp for interior SDF.
// [HAZARD] D_tol and normal_angle_tol are empirical constants derived from
//   DrainHole::steps. Any change to DrainHole tessellation silently changes
//   the identification tolerance without any compile-time notice.
static std::vector<bool> create_exclude_mask(const indexed_triangle_set&        its,
                                             const sla::Interior&               interior,
                                             const std::vector<sla::DrainHole>& holes)
{
    FaceHash interior_hash{sla::get_mesh(interior)};

    std::vector<bool> exclude_mask(its.indices.size(), false);

    VertexFaceIndex neighbor_index{its};

    auto exclude_neighbors = [&neighbor_index, &exclude_mask](const Vec3i32& face) {
        for (int i = 0; i < 3; ++i) {
            const auto& neighbors_range = neighbor_index[face(i)];
            for (size_t fi_n : neighbors_range)
                exclude_mask[fi_n] = true;
        }
    };

    for (size_t fi = 0; fi < its.indices.size(); ++fi) {
        auto& face = its.indices[fi];

        if (interior_hash.find(FaceHash::facekey(face, its.vertices))) {
            exclude_mask[fi] = true;
            continue;
        }

        if (exclude_mask[fi]) {
            exclude_neighbors(face);
            continue;
        }

        // Lets deal with the holes. All the triangles of a hole and all the
        // neighbors of these triangles need to be kept. The neigbors were
        // created by CGAL mesh boolean operation that modified the original
        // interior inside the input mesh to contain the holes.
        Vec3d tr_center = (its.vertices[face(0)] + its.vertices[face(1)] + its.vertices[face(2)]).cast<double>() / 3.;

        // If the center is more than half a mm inside the interior,
        // it cannot possibly be part of a hole wall.
        if (sla::get_distance(tr_center, interior) < -0.5)
            continue;

        Vec3f U           = its.vertices[face(1)] - its.vertices[face(0)];
        Vec3f V           = its.vertices[face(2)] - its.vertices[face(0)];
        Vec3f C           = U.cross(V);
        Vec3f face_normal = C.normalized();

        for (const sla::DrainHole& dh : holes) {
            if (dh.failed)
                continue;

            Vec3d dhpos = dh.pos.cast<double>();
            Vec3d dhend = dhpos + dh.normal.cast<double>() * dh.height;

            Linef3 holeaxis{dhpos, dhend};

            double D_hole_center = line_alg::distance_to(holeaxis, tr_center);
            double D_hole        = std::abs(D_hole_center - dh.radius);
            float  dot           = dh.normal.dot(face_normal);

            // Empiric tolerances for center distance and normals angle.
            // For triangles that are part of a hole wall the angle of
            // triangle normal and the hole axis is around 90 degrees,
            // so the dot product is around zero.
            double D_tol            = dh.radius / sla::DrainHole::steps;
            float  normal_angle_tol = 1.f / sla::DrainHole::steps;

            if (D_hole < D_tol && std::abs(dot) < normal_angle_tol) {
                exclude_mask[fi] = true;
                exclude_neighbors(face);
            }
        }
    }

    return exclude_mask;
}

// [INTENT] remove_unconnected_vertices: compacts an ITS by rebuilding the
// vertex array to include only vertices that are referenced by at least one
// face. Needed after partial triangle removal (e.g. per-hole CGAL subtraction)
// to prevent CGAL from choking on orphaned vertices.
// [HAZARD] If its.indices is empty the function returns an empty mesh without
//   copying vertices — silent data loss if called on a vertex-only point cloud.
static indexed_triangle_set remove_unconnected_vertices(const indexed_triangle_set& its)
{
    if (its.indices.empty()) {};

    indexed_triangle_set M;

    std::vector<int> vtransl(its.vertices.size(), -1);
    int              vcnt = 0;
    for (auto& f : its.indices) {
        for (int i = 0; i < 3; ++i)
            if (vtransl[size_t(f(i))] < 0) {
                M.vertices.emplace_back(its.vertices[size_t(f(i))]);
                vtransl[size_t(f(i))] = vcnt++;
            }

        std::array<int, 3> new_f = {vtransl[size_t(f(0))], vtransl[size_t(f(1))], vtransl[size_t(f(2))]};

        M.indices.emplace_back(new_f[0], new_f[1], new_f[2]);
    }

    return M;
}

// Drill holes into the hollowed/original mesh.
void SLAPrint::Steps::drill_holes(SLAPrintObject& po)
{
    /*
    bool needs_drilling = ! po.m_model_object->sla_drain_holes.empty();
    bool is_hollowed =
        (po.m_hollowing_data && po.m_hollowing_data->interior &&
         !sla::get_mesh(*po.m_hollowing_data->interior).empty());

    if (! is_hollowed && ! needs_drilling) {
        // In this case we can dump any data that might have been
        // generated on previous runs.
        po.m_hollowing_data.reset();
        return;
    }

    if (! po.m_hollowing_data)
        po.m_hollowing_data.reset(new SLAPrintObject::HollowingData());

    // Hollowing and/or drilling is active, m_hollowing_data is valid.

    // Regenerate hollowed mesh, even if it was there already. It may contain
    // holes that are no longer on the frontend.
    TriangleMesh &hollowed_mesh = po.m_hollowing_data->hollow_mesh_with_holes;
    hollowed_mesh = po.transformed_mesh();
    if (is_hollowed)
        sla::hollow_mesh(hollowed_mesh, *po.m_hollowing_data->interior);

    TriangleMesh &mesh_view = po.m_hollowing_data->hollow_mesh_with_holes_trimmed;

    if (! needs_drilling) {
        mesh_view = po.transformed_mesh();

        if (is_hollowed)
            sla::hollow_mesh(mesh_view, *po.m_hollowing_data->interior,
                             sla::hfRemoveInsideTriangles);

        BOOST_LOG_TRIVIAL(info) << "Drilling skipped (no holes).";
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "Drilling drainage holes.";
    sla::DrainHoles drainholes = po.transformed_drainhole_points();

    auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
        hollowed_mesh.its.vertices,
        hollowed_mesh.its.indices
    );

    std::uniform_real_distribution<float> dist(0., float(EPSILON));
    auto holes_mesh_cgal = MeshBoolean::cgal::triangle_mesh_to_cgal({}, {});
    indexed_triangle_set part_to_drill = hollowed_mesh.its;

    bool hole_fail = false;
    for (size_t i = 0; i < drainholes.size(); ++i) {
        sla::DrainHole holept = drainholes[i];

        holept.normal += Vec3f{dist(m_rng), dist(m_rng), dist(m_rng)};
        holept.normal.normalize();
        holept.pos += Vec3f{dist(m_rng), dist(m_rng), dist(m_rng)};
        indexed_triangle_set m = holept.to_mesh();

        part_to_drill.indices.clear();
        auto bb = bounding_box(m);
        Eigen::AlignedBox<float, 3> ebb{bb.min.cast<float>(),
                                        bb.max.cast<float>()};
        //BBS
        //AABBTreeIndirect::traverse(
        //            tree,
        //            AABBTreeIndirect::intersecting(ebb),
        //            [&part_to_drill, &hollowed_mesh](size_t faceid)
        //{
        //    part_to_drill.indices.emplace_back(hollowed_mesh.its.indices[faceid]);
        //});

        auto cgal_meshpart = MeshBoolean::cgal::triangle_mesh_to_cgal(
            remove_unconnected_vertices(part_to_drill));

        if (MeshBoolean::cgal::does_self_intersect(*cgal_meshpart)) {
            BOOST_LOG_TRIVIAL(error) << "Failed to drill hole";

            hole_fail = drainholes[i].failed =
                    po.model_object()->sla_drain_holes[i].failed = true;

            continue;
        }

        auto cgal_hole = MeshBoolean::cgal::triangle_mesh_to_cgal(m);
        MeshBoolean::cgal::plus(*holes_mesh_cgal, *cgal_hole);
    }

    if (MeshBoolean::cgal::does_self_intersect(*holes_mesh_cgal))
        throw Slic3r::SlicingError(L("Too many overlapping holes."));

    auto hollowed_mesh_cgal = MeshBoolean::cgal::triangle_mesh_to_cgal(hollowed_mesh);

    if (!MeshBoolean::cgal::does_bound_a_volume(*hollowed_mesh_cgal)) {
        po.active_step_add_warning(
            PrintStateBase::WarningLevel::NON_CRITICAL,
            L("Mesh to be hollowed is not suitable for hollowing (does not "
              "bound a volume)."));
    }

    if (!MeshBoolean::cgal::empty(*holes_mesh_cgal)
        && !MeshBoolean::cgal::does_bound_a_volume(*holes_mesh_cgal)) {
        po.active_step_add_warning(
            PrintStateBase::WarningLevel::NON_CRITICAL,
            L("Unable to drill the current configuration of holes into the "
              "model."));
    }

    try {
        if (!MeshBoolean::cgal::empty(*holes_mesh_cgal))
            MeshBoolean::cgal::minus(*hollowed_mesh_cgal, *holes_mesh_cgal);

        hollowed_mesh = MeshBoolean::cgal::cgal_to_triangle_mesh(*hollowed_mesh_cgal);
        mesh_view = hollowed_mesh;

        if (is_hollowed) {
            auto &interior = *po.m_hollowing_data->interior;
            std::vector<bool> exclude_mask =
                    create_exclude_mask(mesh_view.its, interior, drainholes);

            sla::remove_inside_triangles(mesh_view, interior, exclude_mask);
        }
    } catch (const Slic3r::RuntimeError &) {
        throw Slic3r::SlicingError(L(
            "Drilling holes into the mesh failed. "
            "This is usually caused by broken model. Try to fix it first."));
    }

    if (hole_fail)
        po.active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL,
                                   L("Failed to drill some holes into the model"));
     */
}

// The slicing will be performed on an imaginary 1D grid which starts from
// the bottom of the bounding box created around the supported model. So
// the first layer which is usually thicker will be part of the supports
// not the model geometry. Exception is when the model is not in the air
// (elevation is zero) and no pad creation was requested. In this case the
// model geometry starts on the ground level and the initial layer is part
// of it. In any case, the model and the supports have to be sliced in the
// same imaginary grid (the height vector argument to TriangleMeshSlicer).
// [INTENT] slice_model: builds the height grid (m_slice_index + m_model_height_levels)
// and populates m_model_slices with ExPolygon cross-sections of the model mesh.
// Algorithm:
//   1. Derive height grid: first layer is ilh thick (initial layer), subsequent
//      layers are regular lh apart, all stored as scaled coord_t in m_slice_index.
//   2. Call slice_mesh_ex() (TriangleMeshSlicer) on the potentially hollowed mesh.
//   3. If a hollowing interior exists, slice it with swapped normals and subtract
//      from the model slices in parallel (sla::ccr::for_each).
//   4. Call apply_printer_corrections() to add absolute_correction + EFC offsets.
//   5. Allocate SupportData if supports or pad is enabled.
// [STATE] Clears and fully rebuilds m_slice_index, m_model_height_levels, m_model_slices.
// [CONCURRENCY] Step 3 diff uses sla::ccr::for_each (TBB parallel_for).
//   m_model_slices[i] is written by exactly one thread — no contention.
// [HAZARD] lhd always reads from m_print->m_objects.front() — assumes all
//   objects share the same layer height. Multi-object prints with different
//   heights would silently use only the first object's layer height.
// [COUPLING] get_mesh_to_slice() returns either the hollowed mesh (if drill step
//   ran) or the raw transformed mesh. This coupling means slice_model() result
//   changes based on whether drill_holes() ran — but drill_holes() is dead code (H808).
void SLAPrint::Steps::slice_model(SLAPrintObject& po)
{
    const TriangleMesh& mesh = po.get_mesh_to_slice();

    // We need to prepare the slice index...

    double  lhd   = m_print->m_objects.front()->m_config.layer_height.getFloat();
    float   lh    = float(lhd);
    coord_t lhs   = scaled(lhd);
    auto&&  bb3d  = mesh.bounding_box();
    double  minZ  = bb3d.min(Z) - po.get_elevation();
    double  maxZ  = bb3d.max(Z);
    auto    minZf = float(minZ);
    coord_t minZs = scaled(minZ);
    coord_t maxZs = scaled(maxZ);

    po.m_slice_index.clear();

    size_t cap = size_t(1 + (maxZs - minZs - ilhs) / lhs);
    po.m_slice_index.reserve(cap);

    po.m_slice_index.emplace_back(minZs + ilhs, minZf + ilh / 2.f, ilh);

    for (coord_t h = minZs + ilhs + lhs; h <= maxZs; h += lhs)
        po.m_slice_index.emplace_back(h, unscaled<float>(h) - lh / 2.f, lh);

    // Just get the first record that is from the model:
    auto slindex_it = po.closest_slice_record(po.m_slice_index, float(bb3d.min(Z)));

    if (slindex_it == po.m_slice_index.end())
        // TRN To be shown at the status bar on SLA slicing error.
        throw Slic3r::RuntimeError(L("Slicing had to be stopped due to an internal error: "
                                     "Inconsistent slice index."));

    po.m_model_height_levels.clear();
    po.m_model_height_levels.reserve(po.m_slice_index.size());
    for (auto it = slindex_it; it != po.m_slice_index.end(); ++it)
        po.m_model_height_levels.emplace_back(it->slice_level());

    po.m_model_slices.clear();
    MeshSlicingParamsEx params;
    params.closing_radius = float(po.config().slice_closing_radius.value);
    // BBS: always regular mode
    // switch (po.config().slicing_mode.value) {
    // case SlicingMode::Regular:    params.mode = MeshSlicingParams::SlicingMode::Regular; break;
    // case SlicingMode::EvenOdd:    params.mode = MeshSlicingParams::SlicingMode::EvenOdd; break;
    // case SlicingMode::CloseHoles: params.mode = MeshSlicingParams::SlicingMode::Positive; break;
    // }
    params.mode       = MeshSlicingParams::SlicingMode::Regular;
    auto  thr         = [this]() { m_print->throw_if_canceled(); };
    auto& slice_grid  = po.m_model_height_levels;
    po.m_model_slices = slice_mesh_ex(mesh.its, slice_grid, params, thr);

    sla::Interior* interior = po.m_hollowing_data ? po.m_hollowing_data->interior.get() : nullptr;

    if (interior && !sla::get_mesh(*interior).empty()) {
        indexed_triangle_set interiormesh = sla::get_mesh(*interior);
        sla::swap_normals(interiormesh);
        params.mode = MeshSlicingParams::SlicingMode::Regular;

        std::vector<ExPolygons> interior_slices = slice_mesh_ex(interiormesh, slice_grid, params, thr);

        sla::ccr::for_each(size_t(0), interior_slices.size(), [&po, &interior_slices](size_t i) {
            const ExPolygons& slice = interior_slices[i];
            po.m_model_slices[i]    = diff_ex(po.m_model_slices[i], slice);
        });
    }

    auto mit = slindex_it;
    for (size_t id = 0; id < po.m_model_slices.size() && mit != po.m_slice_index.end(); id++) {
        mit->set_model_slice_idx(po, id);
        ++mit;
    }

    // We apply the printer correction offset here.
    apply_printer_corrections(po, soModel);

    if (po.m_config.supports_enable.getBool() || po.m_config.pad_enable.getBool()) {
        po.m_supportdata.reset(new SLAPrintObject::SupportData(po.get_mesh_to_print()));
    }
}

// [INTENT] support_points: determines the set of SLA support points for one
// PrintObject. Two branches:
//   AUTO: sla::SupportPointGenerator constructs a graph over the model slices,
//     identifies overhangs and islands, and places support points automatically.
//     Progress is fed back via statuscb lambda. The points are stored in
//     m_supportdata->pts. A RELOAD_SLA_SUPPORT_POINTS status event triggers
//     the UI to refresh the gizmo overlay.
//   USER-MODIFIED: copies transformed frontend support points directly into
//     m_supportdata->pts without any generation.
// [STATE] m_supportdata must already exist (created by slice_model or here).
// [COUPLING] sla::SupportPointGenerator reads po.get_model_slices() and
//   m_model_height_levels — these must be populated by the prior slice_model step.
// [HAZARD] emesh.load_holes() is commented out (line 600). Drain holes are NOT
//   communicated to the support point generator, so auto-placed support points
//   may coincide with drain hole openings without triggering any warning.
void SLAPrint::Steps::support_points(SLAPrintObject& po)
{
    // If supports are disabled, we can skip the model scan.
    if (!po.m_config.supports_enable.getBool())
        return;

    if (!po.m_supportdata)
        po.m_supportdata.reset(new SLAPrintObject::SupportData(po.get_mesh_to_print()));

    const ModelObject& mo = *po.m_model_object;

    BOOST_LOG_TRIVIAL(debug) << "Support point count " << mo.sla_support_points.size();

    // Unless the user modified the points or we already did the calculation,
    // we will do the autoplacement. Otherwise we will just blindly copy the
    // frontend data into the backend cache.
    if (mo.sla_points_status != sla::PointsStatus::UserModified) {
        // calculate heights of slices (slices are calculated already)
        const std::vector<float>& heights = po.m_model_height_levels;

        // Tell the mesh where drain holes are. Although the points are
        // calculated on slices, the algorithm then raycasts the points
        // so they actually lie on the mesh.
        //        po.m_supportdata->emesh.load_holes(po.transformed_drainhole_points());

        throw_if_canceled();
        sla::SupportPointGenerator::Config config;
        const SLAPrintObjectConfig&        cfg = po.config();

        // the density config value is in percents:
        config.density_relative = float(cfg.support_points_density_relative / 100.f);
        config.minimal_distance = float(cfg.support_points_minimal_distance);
        config.head_diameter    = float(cfg.support_head_front_diameter);

        // scaling for the sub operations
        double d    = objectstep_scale * OBJ_STEP_LEVELS[slaposSupportPoints] / 100.0;
        double init = current_status();

        auto statuscb = [this, d, init](unsigned st) {
            double current = init + st * d;
            if (std::round(current_status()) < std::round(current))
                report_status(current, OBJ_STEP_LABELS(slaposSupportPoints));
        };

        // Construction of this object does the calculation.
        throw_if_canceled();
        sla::SupportPointGenerator
            auto_supports(po.m_supportdata->emesh, po.get_model_slices(), heights, config, [this]() { throw_if_canceled(); }, statuscb);

        // Now let's extract the result.
        const std::vector<sla::SupportPoint>& points = auto_supports.output();
        throw_if_canceled();
        po.m_supportdata->pts = points;

        BOOST_LOG_TRIVIAL(debug) << "Automatic support points: " << po.m_supportdata->pts.size();

        // Using RELOAD_SLA_SUPPORT_POINTS to tell the Plater to pass
        // the update status to GLGizmoSlaSupports
        report_status(-1, L("Generating support points"), SlicingStatus::RELOAD_SLA_SUPPORT_POINTS);
    } else {
        // There are either some points on the front-end, or the user
        // removed them on purpose. No calculation will be done.
        po.m_supportdata->pts = po.transformed_support_points();
    }
}

// [INTENT] support_tree: generates the 3D pillar/bridge SLA support tree from
// support points stored in m_supportdata->pts.
// - Adjusts ground_level_offset when using embedded pad (embed_object mode),
//   so the tree roots are elevated above the pad wall.
// - Removes bottom points that touch ground (zero-elevation mode) so no pillar
//   starts flush with the plate where it cannot be printed.
// - sla::SupportData::create_support_tree() delegates to sla::SupportTree::create()
//   which builds a graph of pillars, bridges, and junction nodes.
// - On success, calls support_mesh() to trigger mesh assembly for 3D preview.
// [STATE] Mutates m_supportdata->cfg and calls create_support_tree() which
//   writes m_supportdata->support_tree_ptr.
// [CONCURRENCY] ctl (JobController) wires cancellation callbacks into the tree
//   builder's internal loops; supports cooperative cancellation via stopcondition.
// [HAZARD] emesh.load_holes() commented out (line 660) — same as support_points.
//   Support tree pillars may route through drain hole openings.
// [COUPLING] sla::PadConfig::embed_object flag changes the coordinate origin
//   for the entire support tree. Mismatched pad configs between support_tree()
//   and generate_pad() will misalign the tree base and the pad surface.
void SLAPrint::Steps::support_tree(SLAPrintObject& po)
{
    if (!po.m_supportdata)
        return;

    sla::PadConfig pcfg = make_pad_cfg(po.m_config);

    if (pcfg.embed_object)
        po.m_supportdata->emesh.ground_level_offset(pcfg.wall_thickness_mm);

    // If the zero elevation mode is engaged, we have to filter out all the
    // points that are on the bottom of the object
    if (is_zero_elevation(po.config())) {
        remove_bottom_points(po.m_supportdata->pts, float(po.m_supportdata->emesh.ground_level() + EPSILON));
    }

    po.m_supportdata->cfg = make_support_cfg(po.m_config);
    //    po.m_supportdata->emesh.load_holes(po.transformed_drainhole_points());

    // scaling for the sub operations
    double             d    = objectstep_scale * OBJ_STEP_LEVELS[slaposSupportTree] / 100.0;
    double             init = current_status();
    sla::JobController ctl;

    ctl.statuscb = [this, d, init](unsigned st, const std::string& logmsg) {
        double current = init + st * d;
        if (std::round(current_status()) < std::round(current))
            report_status(current, OBJ_STEP_LABELS(slaposSupportTree), SlicingStatus::DEFAULT, logmsg);
    };
    ctl.stopcondition = [this]() { return canceled(); };
    ctl.cancelfn      = [this]() { throw_if_canceled(); };

    po.m_supportdata->create_support_tree(ctl);

    if (!po.m_config.supports_enable.getBool())
        return;

    throw_if_canceled();

    // Create the unified mesh
    auto rc = SlicingStatus::RELOAD_SCENE;

    // This is to prevent "Done." being displayed during merged_mesh()
    report_status(-1, L("Visualizing supports"));

    BOOST_LOG_TRIVIAL(debug) << "Processed support point count " << po.m_supportdata->pts.size();

    // Check the mesh for later troubleshooting.
    if (po.support_mesh().empty())
        BOOST_LOG_TRIVIAL(warning) << "Support mesh is empty";

    report_status(-1, L("Visualizing supports"), rc);
}

// [INTENT] generate_pad: creates the resin catch basin (pad) at the object's
// base. Two modes:
//   NORMAL (supports + pad, or just pad alone): calls sla::pad_blueprint() to
//     sample the bottom of the model mesh at 'pad_h' height intervals, producing
//     a footprint ExPolygon (bp). SupportData::create_pad() then extrudes this
//     into the 3D pad mesh appended to the support tree.
//   EMBED OBJECT (zero-elevation + pad): pad height equals full_height() which
//     includes the wall thickness, and pad_blueprint samples the mesh to detect
//     where the object contacts the pad.
// If pad is disabled, any previously generated pad is removed from the tree.
// [COUPLING] validate_pad() from SLA/Pad.hpp; failure throws SlicingError (user-visible).
// [HAZARD] trmesh is the raw transformed_mesh() — it is NOT the hollowed mesh.
//   For hollowed + padded objects, the pad footprint may be wider than necessary
//   because it measures the outer shell, not the hollowed interior.
// [STATE] Writes pad into m_supportdata->support_tree_ptr (append to tree).
void SLAPrint::Steps::generate_pad(SLAPrintObject& po)
{
    // this step can only go after the support tree has been created
    // and before the supports had been sliced. (or the slicing has to be
    // repeated)

    if (po.m_config.pad_enable.getBool()) {
        // Get the distilled pad configuration from the config
        sla::PadConfig pcfg = make_pad_cfg(po.m_config);

        ExPolygons          bp; // This will store the base plate of the pad.
        double              pad_h  = pcfg.full_height();
        const TriangleMesh& trmesh = po.transformed_mesh();

        if (!po.m_config.supports_enable.getBool() || pcfg.embed_object) {
            // No support (thus no elevation) or zero elevation mode
            // we sometimes call it "builtin pad" is enabled so we will
            // get a sample from the bottom of the mesh and use it for pad
            // creation.
            sla::pad_blueprint(trmesh.its, bp, float(pad_h), float(po.m_config.layer_height.getFloat()), [this]() { throw_if_canceled(); });
        }

        po.m_supportdata->create_pad(bp, pcfg);

        if (!validate_pad(po.m_supportdata->support_tree_ptr->retrieve_mesh(sla::MeshType::Pad), pcfg))
            throw Slic3r::SlicingError(L("No pad can be generated for this model with the "
                                         "current configuration"));

    } else if (po.m_supportdata && po.m_supportdata->support_tree_ptr) {
        po.m_supportdata->support_tree_ptr->remove_pad();
    }

    throw_if_canceled();
    report_status(-1, L("Visualizing supports"), SlicingStatus::RELOAD_SCENE);
}

// [INTENT] slice_supports: slices the support tree (plus pad if present) at
// exactly the same height grid as the model to produce support cross-sections.
// support_tree_ptr->slice() internally uses TriangleMeshSlicer on the
// support+pad mesh. The resulting ExPolygons are stored in sd->support_slices
// and linked into m_slice_index via set_support_slice_idx().
// apply_printer_corrections() is called for the soSupport origin, applying
// the same absolute_correction and EFC offsets as for the model.
// [COUPLING] Heights vector is rebuilt from m_slice_index here — it MUST match
//   the heights used in slice_model() (same ilh and lh spacing). Any
//   desynchronisation produces misregistered layers in the final bitmap.
// [HAZARD] If supports are disabled but pad is enabled, the function returns
//   early (line 744), and the pad is never sliced. This means pad geometry
//   exists in 3D but has no 2D slice representation — the pad becomes invisible
//   in the output unless supports are also enabled.
// [STATE] Clears sd->support_slices before re-populating.
void SLAPrint::Steps::slice_supports(SLAPrintObject& po)
{
    auto& sd = po.m_supportdata;

    if (sd)
        sd->support_slices.clear();

    // Don't bother if no supports and no pad is present.
    if (!po.m_config.supports_enable.getBool() && !po.m_config.pad_enable.getBool())
        return;

    if (sd && sd->support_tree_ptr) {
        auto heights = reserve_vector<float>(po.m_slice_index.size());

        for (auto& rec : po.m_slice_index)
            heights.emplace_back(rec.slice_level());

        sd->support_slices = sd->support_tree_ptr->slice(heights, float(po.config().slice_closing_radius.value));
    }

    for (size_t i = 0; i < sd->support_slices.size() && i < po.m_slice_index.size(); ++i)
        po.m_slice_index[i].set_support_slice_idx(po, i);

    apply_printer_corrections(po, soSupport);

    // Using RELOAD_SLA_PREVIEW to tell the Plater to pass the update
    // status to the 3D preview to load the SLA slices.
    report_status(-2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
}

// [INTENT] get_all_polygons: expands one SliceRecord's ExPolygons across all
// instances of the associated PrintObject, applying per-instance rotation and
// translation. For left-handed objects (mirrored), contour and hole vertex
// order is reversed AND X is negated, correctly reproducing a mirror transform
// in 2D polygon space.
// [STATE] Pure function — reads record and instances, returns a new ExPolygons.
// [HAZARD] Instance shift is applied in scaled coordinates but the function
//   receives unscaled ExPolygons from get_slice() — the shift units must match
//   the polygon units. This is correct today but implicitly relies on the
//   SliceRecord storing polygons already in print-space coordinates.
static ExPolygons get_all_polygons(const SliceRecord& record, SliceOrigin o)
{
    if (!record.print_obj())
        return {};

    ExPolygons polygons;
    auto&      input_polygons = record.get_slice(o);
    auto&      instances      = record.print_obj()->instances();
    bool       is_lefthanded  = record.print_obj()->is_left_handed();
    polygons.reserve(input_polygons.size() * instances.size());

    for (const ExPolygon& polygon : input_polygons) {
        if (polygon.contour.empty())
            continue;

        for (size_t i = 0; i < instances.size(); ++i) {
            ExPolygon poly;

            // We need to reverse if is_lefthanded is true but
            bool needreverse = is_lefthanded;

            // should be a move
            poly.contour.points.reserve(polygon.contour.size() + 1);

            auto& cntr = polygon.contour.points;
            if (needreverse)
                for (auto it = cntr.rbegin(); it != cntr.rend(); ++it)
                    poly.contour.points.emplace_back(it->x(), it->y());
            else
                for (auto& p : cntr)
                    poly.contour.points.emplace_back(p.x(), p.y());

            for (auto& h : polygon.holes) {
                poly.holes.emplace_back();
                auto& hole = poly.holes.back();
                hole.points.reserve(h.points.size() + 1);

                if (needreverse)
                    for (auto it = h.points.rbegin(); it != h.points.rend(); ++it)
                        hole.points.emplace_back(it->x(), it->y());
                else
                    for (auto& p : h.points)
                        hole.points.emplace_back(p.x(), p.y());
            }

            if (is_lefthanded) {
                for (auto& p : poly.contour)
                    p.x() = -p.x();
                for (auto& h : poly.holes)
                    for (auto& p : h)
                        p.x() = -p.x();
            }

            poly.rotate(double(instances[i].rotation));
            poly.translate(Point{instances[i].shift.x(), instances[i].shift.y()});

            polygons.emplace_back(std::move(poly));
        }
    }

    return polygons;
}

// [INTENT] initialize_printer_input: merges the per-object m_slice_index
// entries from ALL SLAPrintObjects into a single sorted m_printer_input vector.
// Each PrintLayer in m_printer_input corresponds to one physical Z height on
// the printer; it holds reference_wrappers to SliceRecords from all objects
// at that height. The height rounding (eps * (lvlid / eps)) snaps nearby levels
// to the same grid bucket to handle floating-point mismatches between objects
// with different elevations that should print at the same physical layer.
// [STATE] Clears m_printer_input fully before rebuilding.
// [HAZARD] Uses reference_wrapper<const SliceRecord>: if any SLAPrintObject
//   step is later invalidated and its m_slice_index reallocated, all
//   reference_wrappers in printer_input become dangling (H806).
// [HAZARD] mx loop reads `if (auto m = ...) mx = m` — the `m` is the bool
//   result of `> mx`, not the size itself. mx is never updated. This is a
//   latent bug; the reserve() below is thus always reserve(0).
void SLAPrint::Steps::initialize_printer_input()
{
    auto& printer_input = m_print->m_printer_input;

    // clear the rasterizer input
    printer_input.clear();

    size_t mx = 0;
    for (SLAPrintObject* o : m_print->m_objects) {
        if (auto m = o->get_slice_index().size() > mx)
            mx = m;
    }

    printer_input.reserve(mx);

    auto eps = coord_t(SCALED_EPSILON);

    for (SLAPrintObject* o : m_print->m_objects) {
        coord_t gndlvl = o->get_slice_index().front().print_level() - ilhs;

        for (const SliceRecord& slicerecord : o->get_slice_index()) {
            if (!slicerecord.is_valid())
                throw Slic3r::SlicingError(L("There are unprintable objects. Try to "
                                             "adjust support settings to make the "
                                             "objects printable."));

            coord_t lvlid = slicerecord.print_level() - gndlvl;

            // Neat trick to round the layer levels to the grid.
            lvlid = eps * (lvlid / eps);

            auto it = std::lower_bound(printer_input.begin(), printer_input.end(), PrintLayer(lvlid));

            if (it == printer_input.end() || it->level() != lvlid)
                it = printer_input.insert(it, PrintLayer(lvlid));

            it->add(slicerecord);
        }
    }
}

// [INTENT] merge_slices_and_eval_stats: the global print-level step that:
//   1. Calls initialize_printer_input() to build the merged per-layer grid.
//   2. For each PrintLayer in parallel (sla::ccr::for_each / TBB):
//      a. Collects and unions model ExPolygons from all objects at that layer.
//      b. Subtracts support polygons that overlap with model (to avoid double
//         counting volumes), then unions supports separately.
//      c. Computes material volume for both model and supports.
//      d. Determines if the layer is "fast" (below area_fill threshold) and
//         estimates total print time: 3 initial layers use init_exp_time, then
//         fade_layer_time steps down by delta_fade_time each layer until exp_time.
//   3. Stores combined ExPolygons into PrintLayer::transformed_slices for rasterize().
//   4. Populates SLAPrintStatistics (volumes, time, layer counts).
// [CONCURRENCY] printlayerfn runs in TBB parallel_for. Writes to models_volume,
//   supports_volume, estim_time, slow_layers, fast_layers, fade_layer_time,
//   and layers_times are all guarded by SpinningMutex.
// [HAZARD] fade_layer_time is a shared mutable double decremented inside the
//   mutex lock. TBB does NOT guarantee iteration order, so the fade ramp is
//   applied to whichever thread happens to acquire the lock first in the fade
//   window — not necessarily the lowest-index layer. Print time estimate may
//   be off by a few fade steps in parallel mode.
// [HAZARD] The second `c = std::accumulate(... soModel ...)` loop at line 943
//   (the supports_polygons.reserve() calculation) is a copy-paste error: it
//   again accumulates soModel sizes instead of soSupport. supports_polygons
//   may be under-reserved, causing extra heap allocations.
void SLAPrint::Steps::merge_slices_and_eval_stats()
{
    initialize_printer_input();

    auto& print_statistics = m_print->m_print_statistics;
    auto& printer_config   = m_print->m_printer_config;
    auto& material_config  = m_print->m_material_config;
    auto& printer_input    = m_print->m_printer_input;

    print_statistics.clear();

    const double area_fill = printer_config.area_fill.getFloat() * 0.01; // 0.5 (50%);
    const double fast_tilt = printer_config.fast_tilt_time.getFloat();   // 5.0;
    const double slow_tilt = printer_config.slow_tilt_time.getFloat();   // 8.0;

    const double init_exp_time = material_config.initial_exposure_time.getFloat();
    const double exp_time      = material_config.exposure_time.getFloat();

    const int fade_layers_cnt = m_print->m_default_object_config.faded_layers.getInt(); // 10 // [3;20]

    const auto   width        = scaled<double>(printer_config.display_width.getFloat());
    const auto   height       = scaled<double>(printer_config.display_height.getFloat());
    const double display_area = width * height;

    double supports_volume(0.0);
    double models_volume(0.0);

    double              estim_time(0.0);
    std::vector<double> layers_times;
    layers_times.reserve(printer_input.size());

    size_t slow_layers = 0;
    size_t fast_layers = 0;

    const double delta_fade_time = (init_exp_time - exp_time) / (fade_layers_cnt + 1);
    double       fade_layer_time = init_exp_time;

    sla::ccr::SpinningMutex mutex;
    using Lock = std::lock_guard<sla::ccr::SpinningMutex>;

    // Going to parallel:
    auto printlayerfn = [this,
                         // functions and read only vars
                         area_fill, display_area, exp_time, init_exp_time, fast_tilt, slow_tilt, delta_fade_time,

                         // write vars
                         &mutex, &models_volume, &supports_volume, &estim_time, &slow_layers, &fast_layers, &fade_layer_time,
                         &layers_times](size_t sliced_layer_cnt) {
        PrintLayer& layer = m_print->m_printer_input[sliced_layer_cnt];

        // vector of slice record references
        auto& slicerecord_references = layer.slices();

        if (slicerecord_references.empty())
            return;

        // Layer height should match for all object slices for a given level.
        const auto l_height = double(slicerecord_references.front().get().layer_height());

        // Calculation of the consumed material

        ExPolygons model_polygons;
        ExPolygons supports_polygons;

        size_t c = std::accumulate(layer.slices().begin(), layer.slices().end(), size_t(0),
                                   [](size_t a, const SliceRecord& sr) { return a + sr.get_slice(soModel).size(); });

        model_polygons.reserve(c);

        c = std::accumulate(layer.slices().begin(), layer.slices().end(), size_t(0),
                            [](size_t a, const SliceRecord& sr) { return a + sr.get_slice(soModel).size(); });

        supports_polygons.reserve(c);

        for (const SliceRecord& record : layer.slices()) {
            ExPolygons modelslices = get_all_polygons(record, soModel);
            for (ExPolygon& p_tmp : modelslices)
                model_polygons.emplace_back(std::move(p_tmp));

            ExPolygons supportslices = get_all_polygons(record, soSupport);
            for (ExPolygon& p_tmp : supportslices)
                supports_polygons.emplace_back(std::move(p_tmp));
        }

        model_polygons          = union_ex(model_polygons);
        double layer_model_area = 0;
        for (const ExPolygon& polygon : model_polygons)
            layer_model_area += area(polygon);

        if (layer_model_area < 0 || layer_model_area > 0) {
            Lock lck(mutex);
            models_volume += layer_model_area * l_height;
        }

        if (!supports_polygons.empty()) {
            if (model_polygons.empty())
                supports_polygons = union_ex(supports_polygons);
            else
                supports_polygons = diff_ex(supports_polygons, model_polygons);
            // allegedly, union of subject is done withing the diff according to the pftPositive polyFillType
        }

        double layer_support_area = 0;
        for (const ExPolygon& polygon : supports_polygons)
            layer_support_area += area(polygon);

        if (layer_support_area < 0 || layer_support_area > 0) {
            Lock lck(mutex);
            supports_volume += layer_support_area * l_height;
        }

        // Here we can save the expensively calculated polygons for printing
        ExPolygons trslices;
        trslices.reserve(model_polygons.size() + supports_polygons.size());
        for (ExPolygon& poly : model_polygons)
            trslices.emplace_back(std::move(poly));
        for (ExPolygon& poly : supports_polygons)
            trslices.emplace_back(std::move(poly));

        layer.transformed_slices(union_ex(trslices));

        // Calculation of the slow and fast layers to the future controlling those values on FW

        const bool   is_fast_layer = (layer_model_area + layer_support_area) <= display_area * area_fill;
        const double tilt_time     = is_fast_layer ? fast_tilt : slow_tilt;

        {
            Lock lck(mutex);
            if (is_fast_layer)
                fast_layers++;
            else
                slow_layers++;

            // Calculation of the printing time

            double layer_times = 0.0;
            if (sliced_layer_cnt < 3)
                layer_times += init_exp_time;
            else if (fade_layer_time > exp_time) {
                fade_layer_time -= delta_fade_time;
                layer_times += fade_layer_time;
            } else
                layer_times += exp_time;
            layer_times += tilt_time;

            layers_times.push_back(layer_times);
            estim_time += layer_times;
        }
    };

    // sequential version for debugging:
    // for(size_t i = 0; i < m_printer_input.size(); ++i) printlayerfn(i);
    sla::ccr::for_each(size_t(0), printer_input.size(), printlayerfn);

    auto SCALING2                          = SCALING_FACTOR * SCALING_FACTOR;
    print_statistics.support_used_material = supports_volume * SCALING2;
    print_statistics.objects_used_material = models_volume * SCALING2;

    // Estimated printing time
    // A layers count o the highest object
    if (printer_input.size() == 0)
        print_statistics.estimated_print_time = std::nan("");
    else {
        print_statistics.estimated_print_time = estim_time;
        print_statistics.layers_times         = layers_times;
    }

    print_statistics.fast_layers_count = fast_layers;
    print_statistics.slow_layers_count = slow_layers;

    report_status(-2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
}

// [INTENT] rasterize: final step — renders each PrintLayer's transformed_slices
// ExPolygons to the SLAArchive (m_printer) as bitmap images.
// m_printer->draw_layers() is typically implemented with TBB parallel_for
// (ex_tbb execution policy) and calls lvlfn for each layer index.
// lvlfn calls raster.draw(poly) for each polygon in the layer, delegating
// all actual pixel rasterization to the SLAArchive backend.
// Progress reporting is mutex-guarded (SpinningMutex slck) since dstatus is
// written from multiple threads.
// [CONCURRENCY] draw_layers() invoked with ex_tbb — TBB parallel. Each layer
//   index i is processed by exactly one thread; layers are independent.
// [COUPLING] m_printer is a raw pointer (H805). If m_printer is null, function
//   returns silently — this is intentional for headless/test modes.
// [HAZARD] canceled() is checked inside lvlfn but after raster.draw() may have
//   partially rendered the layer. Cancellation mid-layer produces no partial
//   bitmap corruption because raster is per-layer; incomplete layers are simply
//   never committed to the archive output.
void SLAPrint::Steps::rasterize()
{
    if (canceled() || !m_print->m_printer)
        return;

    // coefficient to map the rasterization state (0-99) to the allocated
    // portion (slot) of the process state
    double sd = (100 - max_objstatus) / 100.0;

    // slot is the portion of 100% that is realted to rasterization
    unsigned slot = PRINT_STEP_LEVELS[slapsRasterize];

    // pst: previous state
    double pst = current_status();

    double increment = (slot * sd) / m_print->m_printer_input.size();
    double dstatus   = current_status();

    sla::ccr::SpinningMutex slck;
    using Lock = std::lock_guard<sla::ccr::SpinningMutex>;

    // procedure to process one height level. This will run in parallel
    auto lvlfn = [this, &slck, increment, &dstatus, &pst](sla::RasterBase& raster, size_t idx) {
        PrintLayer& printlayer = m_print->m_printer_input[idx];
        if (canceled())
            return;

        for (const ExPolygon& poly : printlayer.transformed_slices())
            raster.draw(poly);

        // Status indication guarded with the spinlock
        {
            Lock lck(slck);
            dstatus += increment;
            double st = std::round(dstatus);
            if (st > pst) {
                report_status(st, PRINT_STEP_LABELS(slapsRasterize));
                pst = st;
            }
        }
    };

    // last minute escape
    if (canceled())
        return;

    // Print all the layers in parallel
    m_print->m_printer->draw_layers(m_print->m_printer_input.size(), lvlfn, [this]() { return canceled(); }, ex_tbb);
}

std::string SLAPrint::Steps::label(SLAPrintObjectStep step) { return OBJ_STEP_LABELS(step); }

std::string SLAPrint::Steps::label(SLAPrintStep step) { return PRINT_STEP_LABELS(step); }

double SLAPrint::Steps::progressrange(SLAPrintObjectStep step) const { return OBJ_STEP_LEVELS[step] * objectstep_scale; }

double SLAPrint::Steps::progressrange(SLAPrintStep step) const { return PRINT_STEP_LEVELS[step] * (100 - max_objstatus) / 100.0; }

void SLAPrint::Steps::execute(SLAPrintObjectStep step, SLAPrintObject& obj)
{
    switch (step) {
    case slaposHollowing: hollow_model(obj); break;
    case slaposDrillHoles: drill_holes(obj); break;
    case slaposObjectSlice: slice_model(obj); break;
    case slaposSupportPoints: support_points(obj); break;
    case slaposSupportTree: support_tree(obj); break;
    case slaposPad: generate_pad(obj); break;
    case slaposSliceSupports: slice_supports(obj); break;
    case slaposCount: assert(false);
    }
}

void SLAPrint::Steps::execute(SLAPrintStep step)
{
    switch (step) {
    case slapsMergeSlicesAndEval: merge_slices_and_eval_stats(); break;
    case slapsRasterize: rasterize(); break;
    case slapsCount: assert(false);
    }
}

} // namespace Slic3r
