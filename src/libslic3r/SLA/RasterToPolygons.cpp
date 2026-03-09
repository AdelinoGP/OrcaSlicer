// [INTENT] RasterToPolygons.cpp — Marching-squares reconstruction of SLA layer polygons
// from an AGGRaster grayscale pixel buffer.
//
// [ALGORITHM]
//   1. Specialize `marchsq::_RasterTraits<RasterGrayscaleAA>` so the marching-squares
//      algorithm can read pixel values and dimensions from the raster.
//   2. Call `marchsq::execute(rst, 128, windowsize)` — threshold=128 (midpoint of 0-255).
//   3. Convert each `marchsq::Ring` (integer pixel coordinates) to a `Polygon` in
//      scaled coord_t units by multiplying by pixel_dimensions.
//   4. Union all polygons via Clipper to merge overlapping rings and establish
//      contour/hole winding order.
//   5. Apply the inverse of the raster Trafo to each ExPolygon:
//      - Undo mirror_y: reflect around height axis
//      - Undo mirror_x: reflect around width axis
//      - Undo center_translate: subtract (center_x, center_y)
//      - Undo flipXY: swap X and Y
//      - Fix winding: XOR parity check `(mirror_x + mirror_y + flipXY) % 2` reverses
//        all polygon winding if an odd number of transforms invert orientation.
//
// [MEMORY] `rings`, `polys`, `unioned` all locally allocated. Returned by value.
// [CONCURRENCY] Stateless — safe to call concurrently on separate raster objects.
//
// [HAZARD] H919 — `width` and `height` use swapped pixel dimension fields:
//   `width = scaled(cols * pxd.h_mm)` and `height = scaled(rows * pxd.w_mm)`.
//   The `w_mm` and `h_mm` are swapped relative to the variable names. This is intentional
//   when flipXY is active (portrait mode swaps rows/cols) but the same formula is used
//   regardless of flipXY state. If flipXY is false, using h_mm for width and w_mm for
//   height still produces correct results ONLY if the printer has square pixels (w_mm==h_mm).
//   For non-square pixels in landscape mode, the mirror correction distances will be wrong.
//
// [COUPLING] `marchsq::_RasterTraits` specialization is in `namespace marchsq` —
//   this pollutes the marchsq namespace with an SLA-specific specialization.

#include "RasterToPolygons.hpp"

#include "AGGRaster.hpp"
#include "libslic3r/MarchingSquares.hpp"
#include "MTUtils.hpp"
#include "ClipperUtils.hpp"

namespace marchsq {

// Specialize this struct to register a raster type for the Marching squares alg
// [COUPLING] Trait specialization in marchsq namespace — makes RasterGrayscaleAA
// visible to the generic marchsq algorithm without changing marchsq headers.
template<> struct _RasterTraits<Slic3r::sla::RasterGrayscaleAA>
{
    using Rst = Slic3r::sla::RasterGrayscaleAA;

    // The type of pixel cell in the raster
    using ValueType = uint8_t;

    // Value at a given position
    static uint8_t get(const Rst& rst, size_t row, size_t col) { return rst.read_pixel(col, row); }

    // Number of rows and cols of the raster
    static size_t rows(const Rst& rst) { return rst.resolution().height_px; }
    static size_t cols(const Rst& rst) { return rst.resolution().width_px; }
};

} // namespace marchsq

namespace Slic3r { namespace sla {

// [INTENT] Apply a function to every vertex of an ExPolygon (contour + holes).
template<class Fn> void foreach_vertex(ExPolygon& poly, Fn&& fn)
{
    for (auto& p : poly.contour.points)
        fn(p);
    for (auto& h : poly.holes)
        for (auto& p : h.points)
            fn(p);
}

// [INTENT] Main reconstruction function. See file-level algorithm description.
ExPolygons raster_to_polygons(const RasterGrayscaleAA& rst, Vec2i32 windowsize)
{
    size_t rows = rst.resolution().height_px, cols = rst.resolution().width_px;

    if (rows < 2 || cols < 2)
        return {};

    Polygons polys;
    long     w_rows = std::max(1l, long(windowsize.y()));
    long     w_cols = std::max(1l, long(windowsize.x()));

    // [STATE] Threshold=128: pixels above midpoint are "filled". windowsize controls cell size.
    std::vector<marchsq::Ring> rings = marchsq::execute(rst, 128, {w_rows, w_cols});

    polys.reserve(rings.size());

    auto pxd = rst.pixel_dimensions();

    // [INTENT] Convert pixel-coordinate rings to scaled coord_t polygons.
    for (const marchsq::Ring& ring : rings) {
        Polygon poly;
        Points& pts = poly.points;
        pts.reserve(ring.size());

        for (const marchsq::Coord& crd : ring)
            pts.emplace_back(scaled(crd.c * pxd.w_mm), scaled(crd.r * pxd.h_mm));

        polys.emplace_back(poly);
    }

    // reverse the raster transformations
    ExPolygons unioned = union_ex(polys);
    // [HAZARD] H919 — pxd.h_mm used for width, pxd.w_mm for height — swapped.
    // Correct only when w_mm == h_mm (square pixels) or when flipXY is active.
    coord_t width = scaled(cols * pxd.h_mm), height = scaled(rows * pxd.w_mm);

    auto tr = rst.trafo();
    for (ExPolygon& expoly : unioned) {
        if (tr.mirror_y)
            foreach_vertex(expoly, [height](Point& p) { p.y() = height - p.y(); });

        if (tr.mirror_x)
            foreach_vertex(expoly, [width](Point& p) { p.x() = width - p.x(); });

        expoly.translate(-tr.center_x, -tr.center_y);

        if (tr.flipXY)
            foreach_vertex(expoly, [](Point& p) { std::swap(p.x(), p.y()); });

        // [INTENT] XOR parity: odd number of orientation-inverting transforms → reverse winding.
        if ((tr.mirror_x + tr.mirror_y + tr.flipXY) % 2) {
            expoly.contour.reverse();
            for (auto& h : expoly.holes)
                h.reverse();
        }
    }

    return unioned;
}

}} // namespace Slic3r::sla
