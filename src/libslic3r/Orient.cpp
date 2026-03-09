// [INTENT] Implementation of the automatic object-orientation algorithm for OrcaSlicer.
// Given a TriangleMesh, evaluates a set of candidate orientations and picks the one
// that minimises an empirically-tuned "unprintability" cost function.
//
// [ALGORITHM OVERVIEW]
// 1. preprocess(): compute face normals, face areas, convex hull, is_appearance flags.
// 2. process(): build candidate orientations from area-dominant normals + supplemental
//    fixed directions (18 unit-sphere points). Remove duplicates.
// 3. For each orientation: project vertices along orientation vector, compute cost
//    features (bottom area, overhang area, contour, low-angle faces, bottom hull),
//    evaluate target_function → scalar unprintability.
// 4. Sort orientations by unprintability, pick best. Tie-break: prefer {0,0,1}.
// 5. Convert best orientation vector to axis+angle and rotation matrix.
//
// [STATE] All intermediate state (normals/areas matrices, z_projected, orientations
// vector, params) lives in the AutoOrienter class. Each mesh gets its own AutoOrienter
// instance. No shared mutable state across instances (assuming progressind is thread-safe).
//
// [CONCURRENCY] _orient() has two paths:
//   - Sequential: standard for-loop over meshes.
//   - Parallel (params.parallel==true): tbb::parallel_for over meshes.
//     [HAZARD H1002] progressfn is called from TBB worker threads in the parallel path.
//     GUI progress callbacks must be thread-safe.
//
// [MEMORY] AutoOrienter stores several Eigen matrices (normals, areas, z_projected,
// z_max, z_median, z_mean) proportional to face_count. Convex hull is stored separately.
// For large meshes (100k+ faces) the memory footprint can be significant.
// mesh_convex_hull stores a COPY of the convex hull mesh (not a reference).
//
// [COUPLING] Uses Geometry::rotation_from_two_vectors and Geometry::extract_euler_angles.
// Uses TriangleMesh::convex_hull_3d() and its_face_normals().
// Uses Clipper-based coord_t types implicitly (via ClipperUtils.hpp).

#include "Orient.hpp"
#include "Geometry.hpp"
#include <numeric>
#include <ClipperUtils.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/log/trivial.hpp>
#include <tbb/parallel_for.h>

#if defined(_MSC_VER) && defined(__clang__)
#define BOOST_NO_CXX17_HDR_STRING_VIEW
#endif

#include <boost/multiprecision/integer.hpp>
#include <boost/rational.hpp>

#undef MAX3
#define MAX3(a, b, c) std::max(std::max(a, b), c)

#undef MEDIAN
#define MEDIAN3(a, b, c) std::max(std::min(a, b), std::min(std::max(a, b), c))
#ifndef SQ
#define SQ(x) ((x) * (x))
#endif

namespace Slic3r { namespace orientation {

// [INTENT] Aggregates the scalar cost components computed for each orientation.
// All fields are in mesh-units (mm²) except `radius`, `volume` (mm³), and
// the dimensionless `unprintability` score.
// [MEMORY] CostItems uses memset(this,0,...) for zero-initialisation — safe only
// because all fields are trivially copyable plain floats.
struct CostItems
{
    float overhang;
    float bottom;
    float bottom_hull;
    float contour;
    float area_laf;       // area_of_low_angle_faces
    float area_projected; // area of projected 2D profile
    float volume;
    float area_total;                  // total area of all faces
    float radius;                      // radius of bounding box
    float height_to_bottom_hull_ratio; // affects stability, the lower the better
    float unprintability;
    CostItems(CostItems const& other) = default;
    CostItems() { memset(this, 0, sizeof(*this)); }
    static std::string field_names()
    {
        return "                                      overhang, bottom, bothull, contour, A_laf, A_prj, unprintability";
    }
    std::string field_values()
    {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1);
        ss << overhang << ",\t" << bottom << ",\t" << bottom_hull << ",\t" << contour << ",\t" << area_laf << ",\t" << area_projected
           << ",\t" << unprintability;
        return ss.str();
    }
};

// A class encapsulating the libnest2d Nester class and extending it with other
// management and spatial index structures for acceleration.
// [INTENT] Single-mesh orientation optimizer. Created per-mesh, destroyed after process().
// [STATE] All Eigen matrices are row-major, indexed by face index.
//   normals (face_count x 3): float face normals from ITS
//   normals_quantize (face_count x 3): normals quantized to 3 decimal places
//   normals_hull, normals_hull_quantize: same for convex hull faces
//   areas (face_count x 1): float face areas in mm²
//   areas_hull (hull_face_count x 1): hull face areas
//   is_apperance (face_count x 1): 1.0 if face is exterior-appearance type
//   z_projected (face_count x 3): vertex z-projections along current orientation
//   z_max, z_median, z_mean (face_count x 1): per-face extremes along orientation
//   z_max_hull (hull_face_count x 1): per-hull-face max z-projection
class AutoOrienter
{
public:
    int                face_count_hull;
    OrientMesh*        orient_mesh = NULL;
    TriangleMesh*      mesh;
    TriangleMesh       mesh_convex_hull; // [MEMORY] full copy of convex hull — O(vertices) allocation
    Eigen::MatrixXf    normals, normals_quantize, normals_hull, normals_hull_quantize;
    Eigen::VectorXf    areas, areas_hull;
    Eigen::VectorXf    is_apperance; // whether a facet is outer apperance
    Eigen::MatrixXf    z_projected;
    Eigen::VectorXf    z_max, z_max_hull; // max of projected z
    Eigen::VectorXf    z_median;          // median of projected z
    Eigen::VectorXf    z_mean;            // mean of projected z
    std::vector<Vec3f> face_normals;
    std::vector<Vec3f> face_normals_hull;
    OrientParams       params;

    std::vector<Vec3f>            orientations;     // Vec3f == stl_normal
    std::function<void(unsigned)> progressind = {}; // default empty indicator function

public:
    AutoOrienter(OrientMesh*                   orient_mesh_,
                 const OrientParams&           params_,
                 std::function<void(unsigned)> progressind_,
                 std::function<bool(void)>     stopcond_)
    {
        orient_mesh = orient_mesh_;
        mesh        = &orient_mesh->mesh;
        params      = params_;
        progressind = progressind_;
        // [INTENT] Override ASCENT threshold from per-object overhang_angle, converting
        // degrees to cos(180° - angle) for the "facing down" detection convention.
        // params.ASCENT = cos(PI - 30°) ≈ -0.866 for default 30° overhang angle.
        params.ASCENT = cos(PI - orient_mesh->overhang_angle * PI / 180); // use per-object overhang angle

        // BOOST_LOG_TRIVIAL(info) << orient_mesh->name << ", angle=" << orient_mesh->overhang_angle << ", params.ASCENT=" << params.ASCENT;
        // std::cout << orient_mesh->name << ", angle=" << orient_mesh->overhang_angle << ", params.ASCENT=" << params.ASCENT;

        preprocess();
    }

    AutoOrienter(TriangleMesh* mesh_)
    {
        mesh = mesh_;
        preprocess();
    }

    // [INTENT] Hash for Vec3f used in unordered_map keyed on quantized face normals.
    // [HAZARD H1003] (P2/Medium): VecHash uses `int(n1(0)*100+100)` — for normal
    // components in [-1,1] this maps to ints in [0,200], which is safe. But the additive
    // combination (linear hash) produces very poor distribution: normals differing only
    // in their z component always share the same bucket contribution from x and y.
    // Expected collision rate is high for normals on a regular grid. Not a correctness
    // issue (unordered_map handles collisions), but degrades performance for dense meshes.
    struct VecHash
    {
        size_t operator()(const Vec3f& n1) const
        {
            return std::hash<coord_t>()(int(n1(0) * 100 + 100)) + std::hash<coord_t>()(int(n1(1) * 100 + 100)) * 101 +
                   std::hash<coord_t>()(int(n1(2) * 100 + 100)) * 10221;
        }
    };

    // [INTENT] Quantize normal vector to 3 decimal places (1 mil precision on unit sphere).
    // Used to group nearly-identical face normals into the same bucket for area accumulation.
    Vec3f quantize_vec3f(const Vec3f n1)
    {
        return Vec3f(floor(n1(0) * 1000) / 1000, floor(n1(1) * 1000) / 1000, floor(n1(2) * 1000) / 1000);
    }

    // [INTENT] Main entry point. Returns the best-orientation vector (pointing "down",
    // i.e., in the direction to place flat on the bed). Caller converts to axis+angle.
    //
    // [ALGORITHM]
    //   1. Initialise candidate orientations with {0,0,-1} (original orientation).
    //   2. area_cumulation_accurate on mesh normals: add top-10 area-dominant normals.
    //   3. area_cumulation_accurate on hull normals: add top-14 hull area-dominant normals.
    //   4. add_supplements: append 18 fixed unit-sphere points.
    //   5. remove_duplicates (tolerance 1e-7).
    //   6. For each orientation: project_vertices, get_features, target_function.
    //   7. Sort by unprintability, pick lowest. Tie-break: prefer {0,0,1}.
    //
    // [HAZARD H1004] (P2/Medium): process() always writes to BOTH BOOST_LOG_TRIVIAL(info)
    // AND std::cout for every orientation evaluated. For a mesh with 40 candidate
    // orientations this produces 40 lines to stdout in a production build — this is
    // debug noise that degrades performance in CLI/headless builds and pollutes CI logs.
    // The commented-out std::cout lines in the constructor suggest this was intentional
    // during development but never cleaned up.
    Vec3d process()
    {
        orientations = {{0, 0, -1}}; // original orientation

        area_cumulation_accurate(face_normals, normals_quantize, areas, 10);

        area_cumulation_accurate(face_normals_hull, normals_hull_quantize, areas_hull, 14);

        add_supplements();

        if (progressind)
            progressind(20);

        remove_duplicates();

        if (progressind)
            progressind(30);

        std::unordered_map<Vec3f, CostItems, VecHash> results;
        BOOST_LOG_TRIVIAL(info) << CostItems::field_names();
        std::cout << CostItems::field_names() << std::endl; // [HAZARD H1004] stdout noise
        for (int i = 0; i < orientations.size(); i++) {
            Vec3f orientation = -orientations[i];

            project_vertices(orientation);

            auto cost_items = get_features(orientation, params.min_volume);

            float unprintability = target_function(cost_items, params.min_volume);

            results[orientation] = cost_items;

            BOOST_LOG_TRIVIAL(info) << std::fixed << std::setprecision(4) << "orientation:" << orientation.transpose()
                                    << ", cost:" << std::fixed << std::setprecision(4) << cost_items.field_values();
            std::cout << std::fixed << std::setprecision(4) << "orientation:" << orientation.transpose() << ", cost:" << std::fixed
                      << std::setprecision(4) << cost_items.field_values() << std::endl; // [HAZARD H1004]
        }
        if (progressind)
            progressind(60);

        typedef std::pair<Vec3f, CostItems> PAIR;
        std::vector<PAIR>                   results_vector(results.begin(), results.end());
        sort(results_vector.begin(), results_vector.end(),
             [](const PAIR& p1, const PAIR& p2) { return p1.second.unprintability < p2.second.unprintability; });

        if (progressind)
            progressind(80);

        // [INTENT] Tie-breaking: if the best orientation is NOT already {0,0,1} (upright)
        // and there is another orientation with the same unprintability that IS {0,0,1},
        // prefer upright to avoid unnecessary rotation (stability).
        Vec3f n1               = {0, 0, 1};
        auto  best_orientation = results_vector[0].first;

        for (int i = 1; i < results_vector.size() - 1; i++) {
            if (abs(results_vector[i].second.unprintability - results_vector[0].second.unprintability) < EPSILON &&
                abs(results_vector[0].first.dot(n1) - 1) > EPSILON) {
                if (abs(results_vector[i].first.dot(n1) - 1) < EPSILON * EPSILON) {
                    best_orientation = n1;
                    break;
                }
            } else {
                break;
            }
        }

        BOOST_LOG_TRIVIAL(info) << std::fixed << std::setprecision(6) << "best:" << best_orientation.transpose()
                                << ", costs:" << results_vector[0].second.field_values();
        std::cout << std::fixed << std::setprecision(6) << "best:" << best_orientation.transpose()
                  << ", costs:" << results_vector[0].second.field_values() << std::endl; // [HAZARD H1004]

        return best_orientation.cast<double>();
    }

    // [INTENT] Precompute face normals, face areas, appearance flags, convex hull data.
    // All subsequent feature computations reuse these precomputed matrices.
    // [MEMORY] Allocates: normals (face_count x 3 float), normals_quantize (same),
    // areas (face_count), is_apperance (face_count), mesh_convex_hull (copy of hull ITS),
    // normals_hull (hull_face_count x 3), normals_hull_quantize (same), areas_hull.
    // [HAZARD H1005] (P2/Medium): `auto its = mesh->its;` copies the entire ITS —
    // for large meshes this is a significant allocation. Done twice (once for mesh,
    // once for hull). Each call to project_vertices also copies mesh->its. These copies
    // are defensive (avoids aliasing) but expensive; a port should use const references.
    void preprocess()
    {
        int count_apperance = 0;
        {
            int  face_count  = mesh->facets_count();
            auto its         = mesh->its; // [HAZARD H1005] copies full ITS
            face_normals     = its_face_normals(its);
            areas            = Eigen::VectorXf::Zero(face_count);
            is_apperance     = Eigen::VectorXf::Zero(face_count);
            normals          = Eigen::MatrixXf::Zero(face_count, 3);
            normals_quantize = Eigen::MatrixXf::Zero(face_count, 3);
            for (size_t i = 0; i < face_count; i++) {
                float area              = its.facet_area(i);
                normals.row(i)          = face_normals[i];
                normals_quantize.row(i) = quantize_vec3f(face_normals[i]);
                areas(i)                = area;
                is_apperance(i)         = (its.get_property(i).type == EnumFaceTypes::eExteriorAppearance);
                count_apperance += (is_apperance(i) == 1);
            }
        }

        if (orient_mesh)
            BOOST_LOG_TRIVIAL(debug) << orient_mesh->name << ", count_apperance=" << count_apperance;

        // get convex hull statistics
        {
            mesh_convex_hull = mesh->convex_hull_3d();
            // mesh_convex_hull.write_binary("convex_hull_debug.stl");

            int  face_count       = mesh_convex_hull.facets_count();
            auto its              = mesh_convex_hull.its; // [HAZARD H1005] copies hull ITS
            face_count_hull       = mesh_convex_hull.facets_count();
            face_normals_hull     = its_face_normals(its);
            areas_hull            = Eigen::VectorXf::Zero(face_count);
            normals_hull          = Eigen::MatrixXf::Zero(face_count_hull, 3);
            normals_hull_quantize = Eigen::MatrixXf::Zero(face_count_hull, 3);
            for (size_t i = 0; i < face_count; i++) {
                float area = its.facet_area(i);
                // We cannot use quantized vector here, the accumulated error will result in bad orientations.
                normals_hull.row(i)          = face_normals_hull[i];
                normals_hull_quantize.row(i) = quantize_vec3f(face_normals_hull[i]);
                areas_hull(i)                = area;
            }
        }
    }

    // [INTENT] Accumulate face areas by quantized normal direction; add the top
    // num_directions area-dominant directions to the orientations candidate list.
    // (Legacy function — area_cumulation_accurate supersedes this.)
    void area_cumulation(const Eigen::MatrixXf& normals_, const Eigen::VectorXf& areas_, int num_directions = 10)
    {
        std::unordered_map<stl_normal, float, VecHash> alignments;
        // init to 0
        for (size_t i = 0; i < areas_.size(); i++)
            alignments.insert(std::pair(normals_.row(i), 0));
        // cumulate areas
        for (size_t i = 0; i < areas_.size(); i++) {
            alignments[normals_.row(i)] += areas_(i);
        }

        typedef std::pair<stl_normal, float> PAIR;
        std::vector<PAIR>                    align_counts(alignments.begin(), alignments.end());
        sort(align_counts.begin(), align_counts.end(), [](const PAIR& p1, const PAIR& p2) { return p1.second > p2.second; });

        num_directions = std::min((size_t) num_directions, align_counts.size());
        for (size_t i = 0; i < num_directions; i++) {
            orientations.push_back(align_counts[i].first);
            // orientations.push_back(its_face_normals(mesh->its)[i]);
            BOOST_LOG_TRIVIAL(debug) << align_counts[i].first.transpose() << ", area: " << align_counts[i].second;
        }
    }

    // [INTENT] Like area_cumulation but returns the *original* (non-quantized) normal
    // from the *largest single face* within each quantized-normal bucket, rather than
    // the quantized vector itself. This avoids accumulated quantization error when the
    // best orientation candidate is derived from a large flat face.
    // [ALGORITHM] Two-pass per bucket: track (largest_single_area, representative_normal)
    // and (total_area). Sort buckets by total_area, take top num_directions.
    // This function is to make sure to return the accurate normal rather than quantized normal
    void area_cumulation_accurate(std::vector<Vec3f>&    normals_,
                                  const Eigen::MatrixXf& quantize_normals_,
                                  const Eigen::VectorXf& areas_,
                                  int                    num_directions = 10)
    {
        std::unordered_map<stl_normal, std::pair<std::vector<float>, Vec3f>, VecHash> alignments_;
        Vec3f                                                                         n1            = {0, 0, 0};
        std::vector<float>                                                            current_areas = {0, 0};
        // init to 0
        for (size_t i = 0; i < areas_.size(); i++) {
            alignments_.insert(std::pair(quantize_normals_.row(i), std::pair(current_areas, n1)));
        }
        // cumulate areas
        for (size_t i = 0; i < areas_.size(); i++) {
            alignments_[quantize_normals_.row(i)].first[1] += areas_(i);
            if (areas_(i) > alignments_[quantize_normals_.row(i)].first[0]) {
                alignments_[quantize_normals_.row(i)].second   = normals_[i];
                alignments_[quantize_normals_.row(i)].first[0] = areas_(i);
            }
        }

        typedef std::pair<stl_normal, std::pair<std::vector<float>, Vec3f>> PAIR;
        std::vector<PAIR>                                                   align_counts(alignments_.begin(), alignments_.end());
        sort(align_counts.begin(), align_counts.end(),
             [](const PAIR& p1, const PAIR& p2) { return p1.second.first[1] > p2.second.first[1]; });

        num_directions = std::min((size_t) num_directions, align_counts.size());
        for (size_t i = 0; i < num_directions; i++) {
            orientations.push_back(align_counts[i].second.second);
            BOOST_LOG_TRIVIAL(debug) << align_counts[i].second.second.transpose() << ", area: " << align_counts[i].second.first[1];
        }
    }

    // [INTENT] Append 18 canonical unit-sphere orientations to ensure coverage of
    // all axis-aligned and 45° orientations independent of the mesh geometry.
    // These cover: bottom (-Z), top (+Z), 4 sides (±X, ±Y), 8 diagonal-horizontal,
    // 4 diagonal-downward-45°, 2 diagonal-upward-45°.
    void add_supplements()
    {
        std::vector<Vec3f> vecs = {{0, 0, -1},
                                   {0.70710678, 0, -0.70710678},
                                   {0, 0.70710678, -0.70710678},
                                   {-0.70710678, 0, -0.70710678},
                                   {0, -0.70710678, -0.70710678},
                                   {1, 0, 0},
                                   {0.70710678, 0.70710678, 0},
                                   {0, 1, 0},
                                   {-0.70710678, 0.70710678, 0},
                                   {-1, 0, 0},
                                   {-0.70710678, -0.70710678, 0},
                                   {0, -1, 0},
                                   {0.70710678, -0.70710678, 0},
                                   {0.70710678, 0, 0.70710678},
                                   {0, 0.70710678, 0.70710678},
                                   {-0.70710678, 0, 0.70710678},
                                   {0, -0.70710678, 0.70710678},
                                   {0, 0, 1}};
        orientations.insert(orientations.end(), vecs.begin(), vecs.end());
    }

    /// <summary>
    /// remove duplicate orientations
    /// </summary>
    /// <param name="tol">tolerance. default 0.01 =sin(0.57\degree)</param>
    // [ALGORITHM] O(N²) pairwise comparison. For the typical ~30-45 candidate count
    // after add_supplements this is negligible.
    // [HAZARD H1006] (P3/Low): The function also removes zero-vectors from orientations.
    // If all candidates are somehow zero (degenerate mesh), the orientations list becomes
    // empty and results_vector[0] in process() will crash with out-of-bounds access.
    void remove_duplicates(double tol = 0.0000001)
    {
        for (auto it = orientations.begin() + 1; it < orientations.end();) {
            bool duplicate = false;
            for (auto it_ok = orientations.begin(); it_ok < it; it_ok++) {
                if (it_ok->isApprox(*it, tol)) {
                    duplicate = true;
                    break;
                }
            }
            const Vec3f all_zero = {0, 0, 0};
            if (duplicate || it->isApprox(all_zero, tol))
                it = orientations.erase(it);
            else
                it++;
        }
    }

    // [INTENT] Project all face vertices of mesh (and convex hull) onto the orientation
    // vector, computing per-face z-max, z-median, z-mean for use in feature extraction.
    // Called once per candidate orientation inside process().
    // [HAZARD H1005 continued] `auto its = mesh->its;` inside both loops copies the ITS
    // on every orientation evaluation — O(face_count) allocation per candidate.
    // [HAZARD H1007] (P3/Low): `its.get_vertex(i, v)` is called with triangle index i
    // but vertex sub-index v — this is a non-standard ITS access pattern. If get_vertex
    // does NOT return vertex i's v-th vertex (0,1,2) but instead vertex index v of the
    // whole mesh, the z-projections would be completely wrong. Requires verification of
    // the ITS API contract.
    void project_vertices(Vec3f orientation)
    {
        int  face_count = mesh->facets_count();
        auto its        = mesh->its; // [HAZARD H1005] copies full ITS
        z_projected.resize(face_count, 3);
        z_max.resize(face_count, 1);
        z_median.resize(face_count, 1);
        z_mean.resize(face_count, 1);
        for (size_t i = 0; i < face_count; i++) {
            float z0          = its.get_vertex(i, 0).dot(orientation);
            float z1          = its.get_vertex(i, 1).dot(orientation);
            float z2          = its.get_vertex(i, 2).dot(orientation);
            z_projected(i, 0) = z0;
            z_projected(i, 1) = z1;
            z_projected(i, 2) = z2;
            z_max(i)          = MAX3(z0, z1, z2);
            z_median(i)       = MEDIAN3(z0, z1, z2);
            z_mean(i)         = (z0 + z1 + z2) / 3;
        }

        z_max_hull.resize(mesh_convex_hull.facets_count(), 1);
        its = mesh_convex_hull.its; // [HAZARD H1005] copies hull ITS
        for (size_t i = 0; i < z_max_hull.rows(); i++) {
            float z0      = its.get_vertex(i, 0).dot(orientation);
            float z1      = its.get_vertex(i, 1).dot(orientation);
            float z2      = its.get_vertex(i, 2).dot(orientation);
            z_max_hull(i) = MAX3(z0, z1, z2);
        }
    }

    // [INTENT] Argsort helper — returns indices that would sort vec in ascending or
    // descending order. Used in the #else block of get_features (contour computation),
    // which is currently disabled (#if 1 selects the simple path).
    static Eigen::VectorXi argsort(const Eigen::VectorXf& vec, std::string order = "ascend")
    {
        Eigen::VectorXi               ind = Eigen::VectorXi::LinSpaced(vec.size(), 0, vec.size() - 1); //[0 1 2 3 ... N-1]
        std::function<bool(int, int)> rule;
        if (order == "ascend") {
            rule = [vec](int i, int j) -> bool { return vec(i) < vec(j); };
        } else {
            rule = [vec](int i, int j) -> bool { return vec(i) > vec(j); };
        }
        std::sort(ind.data(), ind.data() + ind.size(), rule);
        return ind;

        // sorted_vec.resize(vec.size());
        // for (int i = 0; i < vec.size(); i++) {
        //     sorted_vec(i) = vec(ind(i));
        // }
    }

    // [INTENT] Compute the cost feature vector for a given orientation.
    // Requires project_vertices() to have been called first for this orientation.
    //
    // [ALGORITHM]
    //   bottom: sum of face areas where z_max < min_z + FIRST_LAY_H (0.5 weight) +
    //           faces where z_max < min_z + FIRST_LAY_H/2 (full weight)
    //   overhang: sum of areas of faces where normal_projection < ASCENT (facing downward),
    //             not in the bottom zone; weighted by appearance penalty if flagged
    //   contour: approximated as 4*sqrt(bottom_area) (Euclidean perimeter of square
    //            with same area)
    //   bottom_hull: bottom area of the convex hull (proxy for stability)
    //   area_laf: area of low-angle faces (LAF_MIN < |normal_z| < LAF_MAX) above bottom
    //
    // [HAZARD H1008] (P3/Low): `costs.area_total` is set to mesh->bounding_box().area()
    // (bounding box surface area), NOT the total face area of the mesh. The name
    // area_total is misleading — it's the bounding box area. This value is computed but
    // never used in target_function(); it exists only for logging.
    //
    // [HAZARD H1009] (P3/Low): The contour computation (active path) uses
    // `4 * sqrt(costs.bottom)` — the perimeter of a square with area equal to bottom.
    // A comment says "the simple way for contour is even better for faces of small
    // bridges". The more accurate #else path is permanently disabled.
    // previously calc_overhang
    CostItems get_features(Vec3f orientation, bool min_volume = true)
    {
        CostItems costs;
        costs.area_total = mesh->bounding_box().area(); // [HAZARD H1008] bounding box area, not total face area
        costs.radius     = mesh->bounding_box().radius();
        // volume
        costs.volume = mesh->stats().volume > 0 ? mesh->stats().volume : its_volume(mesh->its);

        float total_min_z = z_projected.minCoeff();
        // filter bottom area
        auto bottom_condition      = (z_max.array() < total_min_z + this->params.FIRST_LAY_H - EPSILON).eval();
        auto bottom_condition_hull = (z_max_hull.array() < total_min_z + this->params.FIRST_LAY_H - EPSILON).eval();
        auto bottom_condition_2nd  = (z_max.array() < total_min_z + this->params.FIRST_LAY_H / 2.f - EPSILON).eval();
        // The first layer is sliced on half of the first layer height.
        // The bottom area is measured by accumulating first layer area with the facets area below first layer height.
        // By combining these two factors, we can avoid the wrong orientation of large planar faces while not influence the
        // orientations of complex objects with small bottom areas.
        costs.bottom = bottom_condition.select(areas, 0).sum() * 0.5 + bottom_condition_2nd.select(areas, 0).sum();

        // filter overhang
        Eigen::VectorXf normal_projection(normals.rows(), 1); // = this->normals.dot(orientation);
        for (size_t i = 0; i < normals.rows(); i++) {
            normal_projection(i) = normals.row(i).dot(orientation);
        }
        // [INTENT] Appearance faces receive APPERANCE_FACE_SUPP (3x) penalty multiplier
        // so they contribute more heavily to overhang cost — avoids placing cosmetic
        // surfaces in the support zone.
        auto areas_appearance = areas
                                    .cwiseProduct((is_apperance * params.APPERANCE_FACE_SUPP +
                                                   Eigen::VectorXf::Ones(is_apperance.rows(), is_apperance.cols())))
                                    .eval();
        auto overhang_areas   = ((normal_projection.array() < params.ASCENT) * (!bottom_condition_2nd)).select(areas_appearance, 0).eval();
        Eigen::MatrixXf inner = normal_projection.array() - params.ASCENT;
        inner                 = inner.cwiseMin(0).cwiseAbs();
        if (min_volume) {
            Eigen::MatrixXf heights = z_mean.array() - total_min_z;
            costs.overhang          = (heights.array() * overhang_areas.array() * inner.array()).sum();
        } else {
            costs.overhang = overhang_areas.array().cwiseAbs().sum();
        }

        {
            // contour perimeter
#if 1
            // the simple way for contour is even better for faces of small bridges
            costs.contour = 4 * sqrt(costs.bottom); // [HAZARD H1009] perimeter of equal-area square
#else
            float contour       = 0;
            int   face_count    = mesh->facets_count();
            auto  its           = mesh->its;
            int   contour_amout = 0;
            for (size_t i = 0; i < face_count; i++) {
                if (bottom_condition(i)) {
                    Eigen::VectorXi index = argsort(z_projected.row(i));
                    stl_vertex      line  = its.get_vertex(i, index(0)) - its.get_vertex(i, index(1));
                    contour += line.norm();
                    contour_amout++;
                }
            }
            costs.contour += contour + params.CONTOUR_AMOUNT * contour_amout;
#endif
        }

        // bottom of convex hull
        costs.bottom_hull = (bottom_condition_hull).select(areas_hull, 0).sum();

        // low angle faces
        auto            normal_projection_abs = normal_projection.cwiseAbs().eval();
        Eigen::MatrixXf laf_areas = ((normal_projection_abs.array() < params.LAF_MAX) * (normal_projection_abs.array() > params.LAF_MIN) *
                                     (z_max.array() > total_min_z + params.FIRST_LAY_H))
                                        .select(areas, 0);
        costs.area_laf = laf_areas.sum();

        // height to bottom_hull_area ratio
        // float total_max_z = z_projected.maxCoeff();
        // costs.height_to_bottom_hull_ratio = SQ(total_max_z) / (costs.bottom_hull + 1e-7);

        return costs;
    }

    // [INTENT] Compute the scalar unprintability score from CostItems.
    // The formula is a ratio: weighted overhang and low-angle penalty in the numerator,
    // bottom area and contour in the denominator — so larger bottom areas reduce cost.
    //
    // [HAZARD H1010] (P2/Medium): The denominator is:
    //   TAR_D + CONTOUR_F*contour + BOTTOM_F*bottom + BOTTOM_HULL_F*bottom_hull + ...
    // If all bottom areas and contour are near-zero (e.g. a perfect sphere with no
    // flat bottom), the denominator approaches TAR_D (=0.628 for OrientParams,
    // =1.0 for OrientParamsArea). This is non-zero but the cost function becomes
    // dominated by the numerator with no geometric grounding. The sphere case will
    // produce arbitrary tie-breaking between orientations.
    //
    // [HAZARD H1011] (P3/Low): `(costs.bottom < params.BOTTOM_MIN) * 100` adds 100
    // as a penalty for unstable orientations. This 100 is a magic constant larger
    // than any realistically achievable overhang cost for typical objects. Any orientation
    // with bottom < 0.1 mm² is treated as infinitely bad, which may reject technically
    // valid orientations for very small or thin objects.
    float target_function(CostItems& costs, bool min_volume)
    {
        float cost        = 0;
        float bottom      = costs.bottom;      // std::min(costs.bottom, params.BOTTOM_MAX);
        float bottom_hull = costs.bottom_hull; // std::min(costs.bottom_hull, params.BOTTOM_HULL_MAX);
        if (min_volume) {
            float overhang = costs.overhang / 25;
            cost           = params.TAR_A * (overhang + params.TAR_B) +
                   params.RELATIVE_F *
                       (/*costs.volume/100*/ overhang * params.TAR_C + params.TAR_D +
                        params.TAR_LAF * costs.area_laf * params.use_low_angle_face) /
                       (params.TAR_D + params.CONTOUR_F * costs.contour + params.BOTTOM_F * bottom + params.BOTTOM_HULL_F * bottom_hull +
                        params.TAR_E * overhang + params.TAR_PROJ_AREA * costs.area_projected);
        } else {
            float overhang = costs.overhang;
            cost           = params.RELATIVE_F *
                   (costs.overhang * params.TAR_C + params.TAR_D + params.TAR_LAF * costs.area_laf * params.use_low_angle_face) /
                   (params.TAR_D + params.CONTOUR_F * costs.contour + params.BOTTOM_F * bottom + params.BOTTOM_HULL_F * bottom_hull +
                    params.TAR_PROJ_AREA * costs.area_projected);
        }
        cost += (costs.bottom < params.BOTTOM_MIN) * 100; // [HAZARD H1011] magic stability penalty
        // +(costs.height_to_bottom_hull_ratio > params.height_to_bottom_hull_ratio_MIN) * 110;

        costs.unprintability = cost;

        return cost;
    }
};

// [INTENT] Internal dispatcher — orients all meshes in meshs_ either sequentially
// or in parallel via TBB, storing results back into each OrientMesh.
// [CONCURRENCY] In the parallel path, each mesh has its own AutoOrienter so there
// is no shared mutable state between TBB tasks EXCEPT for progressfn — which is
// called from TBB threads with the mesh index.
// [HAZARD H1002 continued] The parallel path calls `progressfn(i, mesh_.name)` from
// TBB worker threads where i is the range index, not a remaining count. If progressfn
// accesses GUI state it must be synchronised.
void _orient(OrientMeshs&                               meshs_,
             const OrientParams&                        params,
             std::function<void(unsigned, std::string)> progressfn,
             std::function<bool()>                      stopfn)
{
    if (!params.parallel) {
        for (size_t i = 0; i != meshs_.size(); ++i) {
            auto& mesh_ = meshs_[i];
            progressfn(i, mesh_.name);
            // auto progressfn_i = [&](unsigned cnt) {progressfn(cnt, "Orienting " + mesh_.name); };
            AutoOrienter orienter(&mesh_, params, /*progressfn_i*/ {}, stopfn);
            mesh_.orientation = orienter.process();
            Geometry::rotation_from_two_vectors(mesh_.orientation, {0, 0, 1}, mesh_.axis, mesh_.angle, &mesh_.rotation_matrix);
            BOOST_LOG_TRIVIAL(info) << std::fixed << std::setprecision(3) << "v,phi: " << mesh_.axis.transpose() << ", " << mesh_.angle;
            // flush_logs();
        }
    } else {
        // [CONCURRENCY] TBB parallel_for — each iteration is independent.
        // progressfn captured by value (copy of std::function) — safe for TBB capture.
        // stopfn captured by reference — if stopfn has shared state, external locking needed.
        tbb::parallel_for(tbb::blocked_range<size_t>(0, meshs_.size()), [&meshs_, &params, progressfn,
                                                                         stopfn](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i) {
                auto& mesh_ = meshs_[i];
                progressfn(i, mesh_.name); // [HAZARD H1002] called from TBB thread
                AutoOrienter orienter(&mesh_, params, {}, stopfn);
                mesh_.orientation = orienter.process();
                Geometry::rotation_from_two_vectors(mesh_.orientation, {0, 0, 1}, mesh_.axis, mesh_.angle, &mesh_.rotation_matrix);
                mesh_.euler_angles = Geometry::extract_euler_angles(mesh_.rotation_matrix);
                BOOST_LOG_TRIVIAL(debug) << "rotation_from_two_vectors: " << mesh_.orientation << "; " << mesh_.axis << "; " << mesh_.angle
                                         << "; euler: " << mesh_.euler_angles.transpose();
            }
        });
    }
}

// [INTENT] Public API entry point for batch orientation. Delegates to _orient.
// `excludes` parameter is accepted but NOT used in the current implementation —
// it is a placeholder for future support of excluded (pre-placed) objects.
void orient(OrientMeshs& arrangables, const OrientMeshs& excludes, const OrientParams& params)
{
    auto& cfn = params.stopcondition;
    auto& pri = params.progressind;

    _orient(arrangables, params, pri, cfn);
}

// [INTENT] Convenience overload that orients a single ModelObject in-place by
// calling obj->rotate(axis, angle). Bypasses the full OrientMesh pipeline.
// [HAZARD H1000] DEPRECATED — source comment says "this function should be deleted".
// Applies rotation directly to ModelObject's all instances without transformation
// history. If the object already has a non-identity transformation (e.g., was already
// rotated or translated) the applied rotation composes with the existing transform,
// which may produce unexpected results.
void orient(ModelObject* obj)
{
    auto         m = obj->mesh();
    AutoOrienter orienter(&m);
    Vec3d        orientation = orienter.process();
    Vec3d        axis;
    double       angle;
    Geometry::rotation_from_two_vectors(orientation, {0, 0, 1}, axis, angle);

    obj->rotate(angle, axis);
    obj->ensure_on_bed();
}

// [INTENT] Convenience overload that orients a single ModelInstance in-place by
// applying the rotation matrix directly via instance->rotate(matrix).
// [HAZARD H1000 continued] Similar caveats to orient(ModelObject*) — applies
// rotation without accounting for existing instance transform history.
void orient(ModelInstance* instance)
{
    auto         m = instance->get_object()->mesh();
    AutoOrienter orienter(&m);
    Vec3d        orientation = orienter.process();
    Vec3d        axis;
    double       angle;
    Matrix3d     rotation_matrix;
    Geometry::rotation_from_two_vectors(orientation, {0, 0, 1}, axis, angle, &rotation_matrix);
    instance->rotate(rotation_matrix);
}

}} // namespace Slic3r::orientation
