#ifndef slic3r_ExtrusionSimulator_hpp_
#define slic3r_ExtrusionSimulator_hpp_

#include "libslic3r.h"
#include "ExtrusionEntity.hpp"
#include "BoundingBox.hpp"

namespace Slic3r {

// [INTENT] Selects how physically realistic the extrusion preview simulation should be.
// Lower modes favor speed; higher modes model spread/overfill behavior for visual analysis.
enum ExtrusionSimulationType {
    ExtrusionSimulationSimple,
    ExtrusionSimulationDontSpread,
    ExtrisopmSimulationSpreadNotOverfilled,
    ExtrusionSimulationSpreadFull,
    ExtrusionSimulationSpreadExcess
};

// An opaque class, to keep the boost stuff away from the header.
class ExtrusionSimulatorImpl;

// [INTENT] Converts ExtrusionPath geometry into an RGBA raster image used by viewport preview.
// [STATE] Mutable rendering state lives in image_size/viewport/bbox and the hidden pimpl buffers.
// Call order is significant: set geometry bounds, deposit paths into accumulator, evaluate image.
// [MEMORY] Owns implementation via raw pointer with out-of-line constructor/destructor.
// [COUPLING] Couples extrusion geometry (ExtrusionPath) to GUI texture upload through image_ptr().
// [HAZARD] H1210: image_ptr() returns internal storage; pointer lifetime is tied to this object
// and may be invalidated by later mutating calls.
class ExtrusionSimulator
{
public:
    ExtrusionSimulator();
    ~ExtrusionSimulator();

    // Size of the image, that will be returned by image_ptr().
    // The image may be bigger than the viewport as many graphics drivers
    // expect the size of a texture to be rounded to a power of two.
    void set_image_size(const Point& image_size);
    // Which part of the image shall be rendered to?
    void set_viewport(const BoundingBox& viewport);
    // Shift and scale of the rendered extrusion paths into the viewport.
    void set_bounding_box(const BoundingBox& bbox);

    // Reset the extrusion accumulator to zero for all buckets.
    void reset_accumulator();
    // Paint a thick path into an extrusion buffer.
    // A simple implementation is provided now, splatting a rectangular extrusion for each linear segment.
    // In the future, spreading and suqashing of a material will be simulated.
    void extrude_to_accumulator(const ExtrusionPath& path, const Point& shift, ExtrusionSimulationType simulationType);
    // Evaluate the content of the accumulator and paint it into the viewport.
    // After this call the image_ptr() call will return a valid image.
    void evaluate_accumulator(ExtrusionSimulationType simulationType);
    // An RGBA image of image_size, to be loaded into a GPU texture.
    const void* image_ptr() const;

private:
    // [STATE] Requested output texture dimensions.
    Point image_size;
    // [STATE] World-space viewport currently targeted for rendering.
    BoundingBox viewport;
    // [STATE] Source geometry bounds used to map paths into the viewport.
    BoundingBox bbox;

    // [MEMORY] Opaque implementation to isolate heavy dependencies and internal buffers.
    ExtrusionSimulatorImpl* pimpl;
};

} // namespace Slic3r

#endif /* slic3r_ExtrusionSimulator_hpp_ */
