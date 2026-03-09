// [INTENT] SlicingAdaptive: computes a variable layer-height profile for adaptive slicing.
// For each Z position, selects the maximum allowable layer height such that stairstepping
// error stays below a user-set quality threshold. Based on:
//   Florens Wasserfall et al., "Adaptive Slicing for the FDM Process Revisited" (CASE-2017)
//   DOI: 10.1109/COASE.2017.8256074
//
// [STATE] Two key data structures:
//   m_faces            — vector<FaceZ> sorted by z_span.first; each entry stores z_span,
//                        n_cos (|normal_z|), n_sin (sqrt(nx²+ny²)) for one mesh triangle.
//   m_slicing_params   — min/max/default layer heights from the active print config.
// current_facet is an in/out parameter passed to next_layer_height() — it is a persistent
// iterator into m_faces that advances monotonically as Z increases, giving amortised O(1)
// per call for the face-scan inner loop.
//
// [COUPLING] Reads ModelObject::raw_mesh() and ModelInstance::get_matrix() — depends on
// the Model being fully constructed before prepare() is called. Called from
// Slic3r::adaptive_layer_heights_profile() in Slicing.cpp before the slicing pipeline runs.
//
// [HAZARD] Multiple layer-height formula variants are present but commented out (lines 55-67).
// The active formula is Vojtech's triangle-area error metric with clamping at 90°:
//   h = min(delta/0.184, 1.44 * delta * sqrt(sin/cos))
// This is the FOURTH formula in the source. The other three (Waserfall, Cura, Vojtech v1)
// are preserved as comments for reference. A port must use ONLY the uncommented line.
//
// [HAZARD] horizontal_facet_distance() uses a linear O(n) scan over ALL faces starting
// from index 0 on every call (line 201). Unlike next_layer_height() which uses the
// current_facet cursor, this function restarts from 0 each call. For high-polygon-count
// objects, this is called once per layer — total O(L × T) where L = layers, T = triangles.
//
// [HAZARD] prepare() uses only instances.front() transform (line 80) — same pattern
// as H1088 in PrintObject.cpp. Multi-instance objects are adapted based only on
// instance 0's transform.
#include "libslic3r.h"
#include "Model.hpp"
#include "TriangleMesh.hpp"
#include "SlicingAdaptive.hpp"

#include <boost/log/trivial.hpp>
#include <cfloat>

// Based on the work of Florens Waserfall (@platch on github)
// and his paper
// Florens Wasserfall, Norman Hendrich, Jianwei Zhang:
// Adaptive Slicing for the FDM Process Revisited
// 13th IEEE Conference on Automation Science and Engineering (CASE-2017), August 20-23, Xi'an, China. DOI: 10.1109/COASE.2017.8256074
// https://tams.informatik.uni-hamburg.de/publications/2017/Adaptive%20Slicing%20for%20the%20FDM%20Process%20Revisited.pdf

// Vojtech believes that there is a bug in @platch's derivation of the triangle area error metric.
// Following Octave code paints graphs of recommended layer height versus surface slope angle.
#if 0
adeg=0:1:85;
a=adeg*pi/180;
t=tan(a);
tsqr=sqrt(tan(a));
lerr=1./cos(a);
lerr2=1./(0.3+cos(a));
plot(adeg, t, 'b', adeg, sqrt(t), 'g', adeg, 0.5 * lerr, 'm', adeg, 0.5 * lerr2, 'r')
xlabel("angle(deg), 0 - horizontal wall, 90 - vertical wall");
ylabel("layer height");
legend("tan(a) as cura - topographic lines distance limit", "sqrt(tan(a)) as PrusaSlicer - error triangle area limit", "old slic3r - max distance metric", "new slic3r - Waserfall paper");
#endif

#ifndef NDEBUG
#define ADAPTIVE_LAYER_HEIGHT_DEBUG
#endif /* NDEBUG */

namespace Slic3r {

// By Florens Waserfall aka @platch:
// This constant essentially describes the volumetric error at the surface which is induced
// by stacking "elliptic" extrusion threads. It is empirically determined by
// 1. measuring the surface profile of printed parts to find
// the ratio between layer height and profile height and then
// 2. computing the geometric difference between the model-surface and the elliptic profile.
//
// The definition of the roughness formula is in
// https://tams.informatik.uni-hamburg.de/publications/2017/Adaptive%20Slicing%20for%20the%20FDM%20Process%20Revisited.pdf
// (page 51, formula (8))
// Currenty @platch's error metric formula is not used.
// static constexpr const double SURFACE_CONST = 0.18403;

// [INTENT] layer_height_from_slope: given a face's geometric properties, compute the maximum
// allowable layer height that keeps stairstepping error below max_surface_deviation.
// The active formula (4th variant) clamps between roughness-at-90° and the triangle-area metric.
// [HAZARD] The three commented-out alternatives above must NOT be ported — only this active line.
static inline float layer_height_from_slope(const SlicingAdaptive::FaceZ& face, float max_surface_deviation)
{
    // @platch's formula, see his paper "Adaptive Slicing for the FDM Process Revisited".
    //    return float(max_surface_deviation / (SURFACE_CONST + 0.5 * std::abs(normal_z)));

    // Constant stepping in horizontal direction, as used by Cura.
    //    return (face.n_cos > 1e-5) ? float(max_surface_deviation * face.n_sin / face.n_cos) : FLT_MAX;

    // Constant error measured as an area of the surface error triangle, Vojtech's formula.
    //    return (face.n_cos > 1e-5) ? float(1.44 * max_surface_deviation * sqrt(face.n_sin / face.n_cos)) : FLT_MAX;

    // Constant error measured as an area of the surface error triangle, Vojtech's formula with clamping to roughness at 90 degrees.
    return std::min(max_surface_deviation / 0.184f,
                    (face.n_cos > 1e-5) ? float(1.44 * max_surface_deviation * sqrt(face.n_sin / face.n_cos)) : FLT_MAX);

    // Constant stepping along the surface, equivalent to the "surface roughness" metric by Perez and later Pandey et all, see @platch's
    // paper for references.
    //    return float(max_surface_deviation * face.n_sin);
}

void SlicingAdaptive::clear() { m_faces.clear(); }

// [INTENT] prepare(): transforms and collects all mesh triangles into FaceZ entries sorted by
// z_span.first. Only instance[0]'s transform is applied (same single-instance limitation as
// PrintObject — see H1088). Called once before the adaptive layer height loop.
// [STATE] m_faces is populated and sorted here; current_facet cursors start at 0 after this call.
void SlicingAdaptive::prepare(const ModelObject& object)
{
    this->clear();

    TriangleMesh         mesh           = object.raw_mesh();
    const ModelInstance& first_instance = *object.instances.front();
    mesh.transform(first_instance.get_matrix(), first_instance.is_left_handed());

    // 1) Collect faces from mesh.
    m_faces.reserve(mesh.facets_count());
    for (stl_triangle_vertex_indices face : mesh.its.indices) {
        stl_vertex              vertex[3] = {mesh.its.vertices[face[0]], mesh.its.vertices[face[1]], mesh.its.vertices[face[2]]};
        stl_vertex              n         = face_normal_normalized(vertex);
        std::pair<float, float> face_z_span{std::min(std::min(vertex[0].z(), vertex[1].z()), vertex[2].z()),
                                            std::max(std::max(vertex[0].z(), vertex[1].z()), vertex[2].z())};
        m_faces.emplace_back(FaceZ({face_z_span, std::abs(n.z()), std::sqrt(n.x() * n.x() + n.y() * n.y())}));
    }

    // 2) Sort faces lexicographically by their Z span.
    std::sort(m_faces.begin(), m_faces.end(), [](const FaceZ& f1, const FaceZ& f2) { return f1.z_span < f2.z_span; });
}

// [INTENT] next_layer_height(): core adaptive slicing function. For a given print_z (bottom of
// the previous layer), returns the maximum height for the NEXT layer such that all faces whose
// z-spans overlap the new layer are within the quality factor constraint.
// [STATE] current_facet is an in/out monotone cursor into the sorted m_faces array.
// It advances forward-only as print_z increases, giving amortised O(1) per face visit.
// The two-pass scan: first finds the minimum height from faces starting below print_z+height,
// then refines against newly-entered faces that start within the proposed layer.
// [HAZARD] quality_factor=0 → min layer height; quality_factor=1 → min layer height again
// (both extremes give minimum). quality_factor=0.5 → default layer height (maximum quality).
// This is counterintuitive — see the lerp formula. A port must replicate this mapping exactly.
// current_facet is in/out parameter, rememebers the index of the last face of m_faces visited,
// where this function will start from.
// print_z - the top print surface of the previous layer.
// returns height of the next layer.
float SlicingAdaptive::next_layer_height(const float print_z, float quality_factor, size_t& current_facet)
{
    float height = (float) m_slicing_params.max_layer_height;

    float max_surface_deviation;

    {
#if 0
// @platch's formula for quality:
	    double delta_min = SURFACE_CONST * m_slicing_params.min_layer_height;
	    double delta_mid = (SURFACE_CONST + 0.5) * m_slicing_params.layer_height;
	    double delta_max = (SURFACE_CONST + 0.5) * m_slicing_params.max_layer_height;
#else
        // Vojtech's formula for triangle area error metric.
        double delta_min = m_slicing_params.min_layer_height;
        double delta_mid = m_slicing_params.layer_height;
        double delta_max = m_slicing_params.max_layer_height;
#endif
        max_surface_deviation = (quality_factor < 0.5f) ? lerp(delta_min, delta_mid, 2. * quality_factor) :
                                                          lerp(delta_max, delta_mid, 2. * (1. - quality_factor));
    }

    // find all facets intersecting the slice-layer
    size_t ordered_id = current_facet;
    {
        bool first_hit = false;
        for (; ordered_id < m_faces.size(); ++ordered_id) {
            const std::pair<float, float>& zspan = m_faces[ordered_id].z_span;
            // facet's minimum is higher than slice_z -> end loop
            if (zspan.first >= print_z)
                break;
            // facet's maximum is higher than slice_z -> store the first event for next cusp_height call to begin at this point
            if (zspan.second > print_z) {
                // first event?
                if (!first_hit) {
                    first_hit     = true;
                    current_facet = ordered_id;
                }
                // skip touching facets which could otherwise cause small cusp values
                if (zspan.second < print_z + EPSILON)
                    continue;
                // compute cusp-height for this facet and store minimum of all heights
                height = std::min(height, layer_height_from_slope(m_faces[ordered_id], max_surface_deviation));
            }
        }
    }

    // lower height limit due to printer capabilities
    height = std::max(height, float(m_slicing_params.min_layer_height));

    // check for sloped facets inside the determined layer and correct height if necessary
    if (height > float(m_slicing_params.min_layer_height)) {
        for (; ordered_id < m_faces.size(); ++ordered_id) {
            const std::pair<float, float>& zspan = m_faces[ordered_id].z_span;
            // facet's minimum is higher than slice_z + height -> end loop
            if (zspan.first >= print_z + height)
                break;

            // skip touching facets which could otherwise cause small cusp values
            if (zspan.second < print_z + EPSILON)
                continue;

            // Compute cusp-height for this facet and check against height.
            float reduced_height = layer_height_from_slope(m_faces[ordered_id], max_surface_deviation);

            float z_diff = zspan.first - print_z;
            if (reduced_height < z_diff) {
                assert(z_diff < height + EPSILON);
                // The currently visited triangle's slope limits the next layer height so much, that
                // the lowest point of the currently visible triangle is already above the newly proposed layer height.
                // This means, that we need to limit the layer height so that the offending newly visited triangle
                // is just above of the new layer.
#ifdef ADAPTIVE_LAYER_HEIGHT_DEBUG
                BOOST_LOG_TRIVIAL(trace) << "cusp computation, height is reduced from " << height << "to " << z_diff << " due to z-diff";
#endif /* ADAPTIVE_LAYER_HEIGHT_DEBUG */
                height = z_diff;
            } else if (reduced_height < height) {
#ifdef ADAPTIVE_LAYER_HEIGHT_DEBUG
                BOOST_LOG_TRIVIAL(trace) << "adaptive layer computation: height is reduced from " << height << "to " << reduced_height
                                         << " due to higher facet";
#endif /* ADAPTIVE_LAYER_HEIGHT_DEBUG */
                height = reduced_height;
            }
        }
        // lower height limit due to printer capabilities again
        height = std::max(height, float(m_slicing_params.min_layer_height));
    }

#ifdef ADAPTIVE_LAYER_HEIGHT_DEBUG
    BOOST_LOG_TRIVIAL(trace) << "adaptive layer computation, layer-bottom at z:" << print_z << ", quality_factor:" << quality_factor
                             << ", resulting layer height:" << height;
#endif /* ADAPTIVE_LAYER_HEIGHT_DEBUG */
    return height;
}

// [INTENT] horizontal_facet_distance(): returns the distance from z to the nearest horizontal
// face (flat top surface) above z, or max_layer_height if none found within that distance.
// This detects flat top features so the adaptive slicer can snap layer boundaries to them.
// [HAZARD] O(L×T) total cost: this function always scans from index 0, unlike next_layer_height
// which uses the current_facet cursor. For high-polygon objects, this is a performance hazard.
// See file-header [HAZARD]. No cursor equivalent is maintained for this function.
// Returns the distance to the next horizontal facet in Z-dir
// to consider horizontal object features in slice thickness
float SlicingAdaptive::horizontal_facet_distance(float z)
{
    for (size_t i = 0; i < m_faces.size(); ++i) {
        std::pair<float, float> zspan = m_faces[i].z_span;
        // facet's minimum is higher than max forward distance -> end loop
        if (zspan.first > z + m_slicing_params.max_layer_height)
            break;
        // min_z == max_z -> horizontal facet
        if (zspan.first > z && zspan.first == zspan.second)
            return zspan.first - z;
    }

    // objects maximum?
    return (z + (float) m_slicing_params.max_layer_height > (float) m_slicing_params.object_print_z_height()) ?
               std::max((float) m_slicing_params.object_print_z_height() - z, 0.f) :
               (float) m_slicing_params.max_layer_height;
}

}; // namespace Slic3r
