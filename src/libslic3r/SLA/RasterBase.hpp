// [INTENT] RasterBase.hpp — Abstract interface for SLA rasterization.
// A raster is a pixel canvas onto which ExPolygons are drawn (burned) to create
// the per-layer light exposure image for an SLA printer. The abstract `RasterBase`
// allows swapping underlying renderer implementations (currently AGG-based).
//
// [STATE] Resolution, PixelDim, and Trafo fully describe the raster coordinate space.
// [MEMORY] Implementations own their pixel buffer. EncodedRaster wraps compressed output.
// [CONCURRENCY] Not thread-safe. Drawing is mutating; concurrent calls to `draw()` race.
//
// [COUPLING] `Trafo` encodes orientation (portrait/landscape), mirroring (X/Y), and
//   center offset in coord_t (scaled int32). AGGRaster inherits from RasterBase and
//   materializes these into the AGG rendering pipeline. RasterToPolygons reverses the
//   transformation after marching-squares reconstruction.
//
// [HAZARD] H914 — `Trafo::get_mirror()` uses a literal `roPortrait` instead of
//   `flipXY` (the corresponding member): `(roPortrait ? !mirror_x : mirror_x)`.
//   `roPortrait` is the enum VALUE 1 (truthy), so this is always `!mirror_x`.
//   This looks like a copy-paste bug: the intent was likely
//   `(flipXY ? !mirror_x : mirror_x)` to match the ctor logic.
//
// [HAZARD] H915 — `Trafo` ctor inverts mirror_y unconditionally (`!mirror[1]`)
//   "to make raster origin top-left". This means the semantic of `mirror_y` in Trafo
//   is INVERTED relative to the TMirroring input. Callers passing `MirrorY = {false, true}`
//   get `mirror_y = false`. This inversion is undocumented except for the brief comment
//   and is a source of off-by-one confusion when configuring printer mirroring.

#ifndef SLA_RASTERBASE_HPP
#define SLA_RASTERBASE_HPP

#include <ostream>
#include <memory>
#include <vector>
#include <array>
#include <utility>
#include <cstdint>

#include <libslic3r/ExPolygon.hpp>

namespace Slic3r { namespace sla {

// [INTENT] Raw byte buffer for compressed raster image output (PNG or PPM).
class EncodedRaster
{
protected:
    std::vector<uint8_t> m_buffer;
    std::string          m_ext;

public:
    EncodedRaster() = default;
    explicit EncodedRaster(std::vector<uint8_t>&& buf, std::string ext) : m_buffer(std::move(buf)), m_ext(std::move(ext)) {}

    size_t      size() const { return m_buffer.size(); }
    const void* data() const { return m_buffer.data(); }
    const char* extension() const { return m_ext.c_str(); }
};

/// Type that represents a resolution in pixels.
struct Resolution
{
    size_t width_px  = 0;
    size_t height_px = 0;

    Resolution() = default;
    Resolution(size_t w, size_t h) : width_px(w), height_px(h) {}
    size_t pixels() const { return width_px * height_px; }
};

/// Types that represents the dimension of a pixel in millimeters.
struct PixelDim
{
    double w_mm = 1.;
    double h_mm = 1.;

    PixelDim() = default;
    PixelDim(double px_width_mm, double px_height_mm) : w_mm(px_width_mm), h_mm(px_height_mm) {}
};

using RasterEncoder = std::function<EncodedRaster(const void* ptr, size_t w, size_t h, size_t num_components)>;

// [INTENT] Abstract raster canvas. Concrete implementations (AGGRaster) inherit from this.
class RasterBase
{
public:
    enum Orientation { roLandscape, roPortrait };

    using TMirroring                           = std::array<bool, 2>;
    static const constexpr TMirroring NoMirror = {false, false};
    static const constexpr TMirroring MirrorX  = {true, false};
    static const constexpr TMirroring MirrorY  = {false, true};
    static const constexpr TMirroring MirrorXY = {true, true};

    // [STATE] Raster coordinate transform: orientation, mirroring, center offset.
    struct Trafo
    {
        bool    mirror_x = false, mirror_y = false, flipXY = false;
        coord_t center_x = 0, center_y = 0;

        // Portrait orientation will make sure the drawed polygons are rotated
        // by 90 degrees.
        // [HAZARD] H915 — mirror_y is unconditionally inverted (!mirror[1]) to place
        // origin at top-left. Callers passing MirrorY={false,true} get mirror_y=false.
        // The semantic inversion is undocumented.
        Trafo(Orientation o = roLandscape, const TMirroring& mirror = NoMirror)
            // XY flipping implicitly does an X mirror
            : mirror_x(o == roPortrait ? !mirror[0] : mirror[0])
            , mirror_y(!mirror[1]) // Makes raster origin to be top left corner
            , flipXY(o == roPortrait)
        {}

        // [HAZARD] H914 — uses literal `roPortrait` (==1, always truthy) instead of `flipXY`.
        // Should be: (flipXY ? !mirror_x : mirror_x) to match ctor logic.
        TMirroring  get_mirror() const { return {(roPortrait ? !mirror_x : mirror_x), mirror_y}; }
        Orientation get_orientation() const { return flipXY ? roPortrait : roLandscape; }
        Point       get_center() const { return {center_x, center_y}; }
    };

    virtual ~RasterBase() = default;

    /// Draw a polygon with holes.
    virtual void draw(const ExPolygon& poly) = 0;

    /// Get the resolution of the raster.
    //    virtual Resolution resolution() const = 0;
    //    virtual PixelDim   pixel_dimensions() const = 0;
    virtual Trafo trafo() const = 0;

    virtual EncodedRaster encode(RasterEncoder encoder) const = 0;
};

// [INTENT] PNG encoder using miniz (tdefl_write_image_to_png_file_in_memory).
struct PNGRasterEncoder
{
    EncodedRaster operator()(const void* ptr, size_t w, size_t h, size_t num_components);
};

// [INTENT] PPM (grayscale P5) encoder for debugging / testing.
struct PPMRasterEncoder
{
    EncodedRaster operator()(const void* ptr, size_t w, size_t h, size_t num_components);
};

std::ostream& operator<<(std::ostream& stream, const EncodedRaster& bytes);

// If gamma is zero, thresholding will be performed which disables AA.
// [INTENT] Factory: creates the appropriate AGGRaster variant based on gamma value.
std::unique_ptr<RasterBase> create_raster_grayscale_aa(const Resolution&        res,
                                                       const PixelDim&          pxdim,
                                                       double                   gamma = 1.0,
                                                       const RasterBase::Trafo& tr    = {});

}} // namespace Slic3r::sla

#endif // SLARASTERBASE_HPP
