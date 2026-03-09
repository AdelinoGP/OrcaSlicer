// [INTENT] AGGRaster.hpp — Concrete SLA raster implementation using the Anti-Grain
// Geometry (AGG) library. Draws ExPolygons into a grayscale pixel buffer for
// per-layer SLA light exposure images.
//
// Template parameters allow swapping pixel format, renderer, rasterizer, and scanline.
// The production type `RasterGrayscaleAA` uses:
//   - agg::pixfmt_gray8 (8bpp grayscale)
//   - agg::renderer_scanline_aa_solid (anti-aliased solid fill)
//   - default rasterizer and scanline_p8
//
// [STATE] m_buf: pixel buffer (vector of TPixel, owned). m_rbuf: non-owning AGG view
//   into m_buf. m_trafo: coordinate transform. All set at construction; immutable after.
//
// [MEMORY] m_buf is resized to res.pixels() on construction — no reallocation during use.
//   m_rbuf is a raw pointer view into m_buf.data(); m_buf must not be moved/reallocated
//   after construction or m_rbuf becomes dangling.
//
// [CONCURRENCY] Not thread-safe. Concurrent `draw()` calls race on m_rasterizer and m_buf.
//
// [COUPLING] `m_pxdim_scaled` stores SCALING_FACTOR / pixel_size_mm (precomputed inverse).
//   Polygon coordinates (coord_t scaled int32) are converted to pixel positions by
//   multiplying by m_pxdim_scaled. This is the bridge between the integer polygon
//   coordinate system and the float pixel coordinate system.
//
// [HAZARD] H917 — `AGGRaster` constructor uses `m_pxdim_scaled(SCALING_FACTOR, SCALING_FACTOR)`
//   then divides by `pd.w_mm` / `pd.h_mm`. If `pd.w_mm == 0` or `pd.h_mm == 0`, the
//   division is skipped (assert + if-guard), but `m_pxdim_scaled` retains the raw
//   SCALING_FACTOR value (~1e-6), producing wildly incorrect pixel positions for all
//   subsequent draws. The assert fires only in debug builds.
//
// [HAZARD] H918 — `_to_path` closes the polygon by appending `v.front()` as the last
//   point, then `_to_path_flpxy` does the same with X and Y swapped. Both close the
//   polygon by duplicating the first point. AGG does NOT need explicit closure —
//   the scanline rasterizer auto-closes paths. The duplicate closing point creates an
//   infinitesimally thin degenerate edge that the rasterizer handles gracefully but
//   which adds a tiny performance overhead per polygon.
//
// [COUPLING] `contour()` and `holes()` free functions at Slic3r namespace scope
//   (not sla::) are defined here and used by `_draw`. This pollutes the Slic3r
//   namespace globally — any future type with a `contour` or `holes` member function
//   can shadow these.

#ifndef AGGRASTER_HPP
#define AGGRASTER_HPP

#include <libslic3r/SLA/RasterBase.hpp>
#include "libslic3r/ExPolygon.hpp"

// For rasterizing
#include <agg/agg_basics.h>
#include <agg/agg_rendering_buffer.h>
#include <agg/agg_pixfmt_gray.h>
#include <agg/agg_pixfmt_rgb.h>
#include <agg/agg_renderer_base.h>
#include <agg/agg_renderer_scanline.h>

#include <agg/agg_scanline_p.h>
#include <agg/agg_rasterizer_scanline_aa.h>
#include <agg/agg_path_storage.h>

namespace Slic3r {

// [COUPLING] Free functions at Slic3r scope — pollute the Slic3r namespace globally.
inline const Polygon&  contour(const ExPolygon& p) { return p.contour; }
inline const Polygons& holes(const ExPolygon& p) { return p.holes; }

namespace sla {

// [INTENT] White/Black color constants for the given pixel type.
template<class Color> struct Colors
{
    static const Color White;
    static const Color Black;
};

template<class Color> const Color Colors<Color>::White = Color{255};
template<class Color> const Color Colors<Color>::Black = Color{0};

// [INTENT] Template raster canvas — parameterized over pixel format, renderer, etc.
// See class-level comment above for design rationale and hazards.
template<class PixelRenderer,
         template<class /*agg::renderer_base<PixelRenderer>*/> class Renderer,
         class Rasterizer = agg::rasterizer_scanline_aa<>,
         class Scanline   = agg::scanline_p8>
class AGGRaster : public RasterBase
{
public:
    using TColor     = typename PixelRenderer::color_type;
    using TValue     = typename TColor::value_type;
    using TPixel     = typename PixelRenderer::pixel_type;
    using TRawBuffer = agg::rendering_buffer;

protected:
    Resolution m_resolution;
    PixelDim   m_pxdim_scaled; // used for scaled coordinate polygons

    // [STATE] m_buf owns the pixel data. m_rbuf is a non-owning view.
    // [HAZARD] H917: m_buf must never be moved/reallocated after construction.
    std::vector<TPixel>   m_buf;
    agg::rendering_buffer m_rbuf;

    PixelRenderer m_pixrenderer;

    agg::renderer_base<PixelRenderer>           m_raw_renderer;
    Renderer<agg::renderer_base<PixelRenderer>> m_renderer;

    Trafo      m_trafo;
    Scanline   m_scanlines;
    Rasterizer m_rasterizer;

    void flipy(agg::path_storage& path) const { path.flip_y(0, double(m_resolution.height_px)); }

    void flipx(agg::path_storage& path) const { path.flip_x(0, double(m_resolution.width_px)); }

    double            getPx(const Point& p) { return p(0) * m_pxdim_scaled.w_mm; }
    double            getPy(const Point& p) { return p(1) * m_pxdim_scaled.h_mm; }
    agg::path_storage to_path(const Polygon& poly) { return to_path(poly.points); }

    // [INTENT] Convert polygon points to AGG path (normal X/Y order).
    // [HAZARD] H918 — closes path by appending v.front(); AGG auto-closes, so this
    //   creates a degenerate duplicate edge.
    template<class PointVec> agg::path_storage _to_path(const PointVec& v)
    {
        agg::path_storage path;

        auto it = v.begin();
        path.move_to(getPx(*it), getPy(*it));
        while (++it != v.end())
            path.line_to(getPx(*it), getPy(*it));
        path.line_to(getPx(v.front()), getPy(v.front()));

        return path;
    }

    // [INTENT] Convert polygon points to AGG path with X/Y swapped (portrait mode).
    template<class PointVec> agg::path_storage _to_path_flpxy(const PointVec& v)
    {
        agg::path_storage path;

        auto it = v.begin();
        path.move_to(getPy(*it), getPx(*it));
        while (++it != v.end())
            path.line_to(getPy(*it), getPx(*it));
        path.line_to(getPy(v.front()), getPx(v.front()));

        return path;
    }

    // [INTENT] Full transform: flip XY if portrait, translate to center, apply mirrors.
    template<class PointVec> agg::path_storage to_path(const PointVec& v)
    {
        auto path = m_trafo.flipXY ? _to_path_flpxy(v) : _to_path(v);

        path.translate_all_paths(m_trafo.center_x * m_pxdim_scaled.w_mm, m_trafo.center_y * m_pxdim_scaled.h_mm);

        if (m_trafo.mirror_x)
            flipx(path);
        if (m_trafo.mirror_y)
            flipy(path);

        return path;
    }

    // [INTENT] Draw one polygon (contour + holes) via AGG scanline rasterizer.
    template<class P> void _draw(const P& poly)
    {
        m_rasterizer.reset();

        m_rasterizer.add_path(to_path(contour(poly)));
        for (auto& h : holes(poly))
            m_rasterizer.add_path(to_path(h));

        agg::render_scanlines(m_rasterizer, m_scanlines, m_renderer);
    }

public:
    // [INTENT] Construct raster: allocate pixel buffer, set up AGG pipeline.
    // [HAZARD] H917 — pd.w_mm/h_mm == 0 skips the scale factor division, leaving
    //   m_pxdim_scaled at raw SCALING_FACTOR. Only asserts in debug builds.
    template<class GammaFn>
    AGGRaster(
        const Resolution& res, const PixelDim& pd, const Trafo& trafo, const TColor& foreground, const TColor& background, GammaFn&& gammafn)
        : m_resolution(res)
        , m_pxdim_scaled(SCALING_FACTOR, SCALING_FACTOR)
        , m_buf(res.pixels())
        , m_rbuf(reinterpret_cast<TValue*>(m_buf.data()),
                 unsigned(res.width_px),
                 unsigned(res.height_px),
                 int(res.width_px * PixelRenderer::num_components))
        , m_pixrenderer(m_rbuf)
        , m_raw_renderer(m_pixrenderer)
        , m_renderer(m_raw_renderer)
        , m_trafo(trafo)
    {
        // Visual Studio compiler gives warnings about possible division by zero.
        assert(pd.w_mm != 0 && pd.h_mm != 0);
        if (pd.w_mm != 0 && pd.h_mm != 0) {
            m_pxdim_scaled.w_mm /= pd.w_mm;
            m_pxdim_scaled.h_mm /= pd.h_mm;
        }
        m_renderer.color(foreground);
        clear(background);

        m_rasterizer.gamma(gammafn);
    }

    Trafo      trafo() const override { return m_trafo; }
    Resolution resolution() const { return m_resolution; }
    PixelDim   pixel_dimensions() const { return {SCALING_FACTOR / m_pxdim_scaled.w_mm, SCALING_FACTOR / m_pxdim_scaled.h_mm}; }

    void draw(const ExPolygon& poly) override { _draw(poly); }

    EncodedRaster encode(RasterEncoder encoder) const override
    {
        return encoder(m_buf.data(), m_resolution.width_px, m_resolution.height_px, 1);
    }

    void clear(const TColor color) { m_raw_renderer.clear(color); }
};

/*
 * Captures an anti-aliased monochrome canvas where vectorial
 * polygons can be rasterized. Fill color is always white and the background is
 * black. Contours are anti-aliased.
 *
 * A gamma function can be specified at compile time to make it more flexible.
 */
using _RasterGrayscaleAA = AGGRaster<agg::pixfmt_gray8, agg::renderer_scanline_aa_solid>;

// [INTENT] Production grayscale AA raster type. Exposes `read_pixel` for marching-squares.
class RasterGrayscaleAA : public _RasterGrayscaleAA
{
    using Base = _RasterGrayscaleAA;
    using typename Base::TColor;
    using typename Base::TValue;

public:
    template<class GammaFn>
    RasterGrayscaleAA(const Resolution& res, const PixelDim& pd, const RasterBase::Trafo& trafo, GammaFn&& fn)
        : Base(res, pd, trafo, Colors<TColor>::White, Colors<TColor>::Black, std::forward<GammaFn>(fn))
    {}

    uint8_t read_pixel(size_t col, size_t row) const
    {
        static_assert(std::is_same<TValue, uint8_t>::value, "Not grayscale pix");

        uint8_t px;
        Base::m_buf[row * Base::resolution().width_px + col].get(px);
        return px;
    }

    void clear() { Base::clear(Colors<TColor>::Black); }
};

// [INTENT] Convenience subclass that applies a power-law gamma correction.
class RasterGrayscaleAAGammaPower : public RasterGrayscaleAA
{
public:
    RasterGrayscaleAAGammaPower(const Resolution& res, const PixelDim& pd, const RasterBase::Trafo& trafo, double gamma = 1.)
        : RasterGrayscaleAA(res, pd, trafo, agg::gamma_power(gamma))
    {}
};

} // namespace sla
} // namespace Slic3r

#endif // AGGRASTER_HPP
