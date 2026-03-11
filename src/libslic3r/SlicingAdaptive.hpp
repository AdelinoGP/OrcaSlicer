// Based on implementation by @platsch

#ifndef slic3r_SlicingAdaptive_hpp_
#define slic3r_SlicingAdaptive_hpp_

#include "Slicing.hpp"
#include "admesh/stl.h"

namespace Slic3r {

class ModelVolume;

class SlicingAdaptive
{
public:
    void clear();
    void set_slicing_parameters(SlicingParameters params) { m_slicing_params = params; }
    // [INTENT] Precompute per-face Z spans and orientation metrics so adaptive layer-height
    // queries avoid re-scanning full mesh geometry every step.
    // [COUPLING] Consumes ModelObject topology and SlicingParameters from the core slicing pipeline.
    void prepare(const ModelObject& object);
    // Return next layer height starting from the last print_z, using a quality measure
    // (quality in range from 0 to 1, 0 - highest quality at low layer heights, 1 - lowest print quality at high layer heights).
    // The layer height curve shall be centered roughly around the default profile's layer height for quality 0.5.
    // [INTENT] Transform local surface slope into an admissible next layer height under a quality knob.
    // [STATE] Updates `current_facet` as an incremental cursor to avoid repeated full-range facet searches.
    float next_layer_height(const float print_z, float quality, size_t& current_facet);
    // [INTENT] Measure horizontal projection error against tilted facets to cap layer height in steep regions.
    float horizontal_facet_distance(float z);

    struct FaceZ
    {
        std::pair<float, float> z_span;
        // Cosine of the normal vector towards the Z axis.
        float n_cos;
        // Sine of the normal vector towards the Z axis.
        float n_sin;
    };

protected:
    // [STATE] Mutable adaptive slicing configuration copied from print-level parameters.
    SlicingParameters m_slicing_params;

    // [MEMORY] Owns a compact per-face cache; rebuilt by prepare() and cleared between objects.
    // [HAZARD] Float-based z_span and trig values can become numerically fragile on huge coordinate ranges.
    std::vector<FaceZ> m_faces;
};

}; // namespace Slic3r

#endif /* slic3r_SlicingAdaptive_hpp_ */
