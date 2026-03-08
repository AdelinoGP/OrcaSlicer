// [INTENT] EdgeGrid implements a uniform spatial hash grid over a set of 2D polygon/polyline
// contours. Its primary purpose is O(1)-amortised neighbourhood queries:
//   - Nearest edge and signed distance from a point to the stored contours
//   - Point-in-polygon tests via a Danielsson chamfer-metric SDF
//   - Intersection detection between stored edges and query segments
// The grid is used throughout the slicing pipeline for seam placement, perimeter offset
// collision, support material placement, and bridge detection.
//
// [STATE] After `create()` / `create_from_m_contours()`:
//   m_contours   — raw-pointer wrappers around caller-owned Points data
//   m_cells      — flat [rows × cols] array; each cell holds [begin, end) into m_cell_data
//   m_cell_data  — flat array of (contour_idx, segment_idx) pairs, sorted by cell
//   m_bbox       — expanded by ±16 integer units around all contour points
//   m_signed_distance_field — optional float SDF, populated only if calculate_sdf() called
//
// [MEMORY] The Grid does NOT own the point data. `Contour` objects store raw `const Point*`
// pointers into the caller's `Polygon`, `ExPolygon`, or `Points` containers. The caller must
// keep those containers alive for the full lifetime of the Grid. No reference counting or
// lifetime tracking is performed.
//
// [HAZARD H539] `Contour` stores raw pointers (m_begin / m_end) into caller-owned storage.
// If the source `Polygon` or `Points` container is resized, moved, or destroyed while the
// Grid is in use, all segment lookups produce undefined behaviour. This is particularly
// dangerous when grids are stored in structs alongside the source polygons, as C++ move
// semantics can silently invalidate the pointers.
//
// [HAZARD H540] `create_from_m_contours()` rasterises edges using an integer Bresenham
// walk with `int64_t` accumulators of the form `(coord * m_resolution)`. `coord_t` is
// typically `int32_t`; `m_resolution` is also `coord_t`. The product of two int32 values
// fits in int64, but intermediate terms like `int64_t(dy) * m_resolution` then multiplied
// again could approach int64 limits for very large print areas (metres-scale coordinates
// in nanometre units). Any port should verify the range of `coord_t` and use 128-bit or
// floating-point accumulators if needed.
//
// [HAZARD H541] `calculate_sdf()` initialises the `signs` array to byte value `4`
// (meaning "signum not yet propagated"). The propagation sweep relies on adjacent cells
// that were seeded from contour edges (bit 1 set) to flood-fill the signum. If the grid
// contains no closed contours (all open polylines), or the contours do not enclose any
// cells, the flood fill may leave large regions with signum byte `0` (positive/outside),
// which is indistinguishable from a valid "outside" sign. For degenerate inputs the SDF
// can be entirely unsigned (all positive) rather than correctly signed.
//
// [HAZARD H542] `signed_distance_bilinear()` silently clamps out-of-bounds query points to
// the grid boundary, then adjusts the returned distance by adding/subtracting the out-of-
// bounds offset. This adjustment is linear and unbounded — for a point far outside the
// grid, the returned "distance" grows without bound and may exceed any caller-specified
// threshold. Callers expecting the function to return a clamped or NaN value for points
// outside the grid will be surprised.
//
// [HAZARD H543] In `closest_point_signed_distance()` and `signed_distance_edges()`, when
// the query point is closest to a vertex (t_pt < 0 path), the sign is determined by the
// cross product (`det`) of the two adjacent segments. The assert `det != 0` fires in Debug
// if the two adjacent segments are collinear (degenerate polygon vertex). In Release the
// assert is absent and `sign_min` retains its previous value or stays 0, producing a
// silently wrong sign for the closest point.
//
// [HAZARD H544] `contours_simplified()` uses a `goto end_of_poly` label to break out of a
// nested for loop when the polygon chain closes. This is the same pattern as H528 in
// Geometry.cpp — unusual in modern C++ and must be carefully replicated or restructured
// (e.g. with a lambda or flag variable) during porting.
//
// [HAZARD H545] `intersecting_edges()` has a dead-branch deduplication bug. The `jfirst`
// flag is computed to determine which pair order to use, but BOTH branches of the ternary
// operator `?:` emit `(icontour, ipt, jcontour, jpt)` — the `jfirst` branch is never used.
// The final `sort_remove_duplicates()` call compensates by deduplicating the output, so
// behaviour is functionally correct, but every intersecting pair is inserted once (not
// twice), and the deduplication is unnecessary work.
//
// [HAZARD H546] `EdgeGrid::Grid::inside()` and `EdgeGrid::Grid::line_cell_intersect()` are
// declared inside a `#if 0` block. The `inside()` body contains `//FIXME finish this!`
// with zero-initialised local variables (`vx = 0`, `det = 0`) — the implementation is
// intentionally incomplete and has never been finished. Any port must be aware this
// point-in-polygon path is dead/broken and should use `signed_distance()` or
// `cell_inside_or_crossing()` instead.
//
// [HAZARD H547] `calculate_sdf()` stores the distance field as `std::vector<float>`. In
// OrcaSlicer, `coord_t` is `int32_t` and coordinates are in nanometres (1 unit = 1 nm).
// A bed of 300 mm = 300,000,000 nm. `float` has only ~7 significant decimal digits, so
// distances above ~16 mm (16,777,216 nm) lose sub-nanometre precision. For large prints
// the SDF is quantised to micrometres, not nanometres. Callers using the SDF for
// sub-millimetre seam or support placement will see low-precision results on large beds.

#include <algorithm>
#include <vector>
#include <float.h>
#include <unordered_map>

#include <png.h>

#include "libslic3r.h"
#include "ClipperUtils.hpp"
#include "EdgeGrid.hpp"
#include "Geometry.hpp"
#include "SVG.hpp"
#include "PNGReadWrite.hpp"

// #define EDGE_GRID_DEBUG_OUTPUT

#if 0
// Enable debugging and assert in this file.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r {

// [INTENT] All `create()` overloads convert caller-owned polygon/polyline data into
// raw-pointer `Contour` objects stored in `m_contours`, then delegate to
// `create_from_m_contours()`. The caller MUST keep source polygons alive (see H539).

void EdgeGrid::Grid::create(const Polygons& polygons, coord_t resolution)
{
    // Collect the contours.
    m_contours.clear();
    m_contours.reserve(std::count_if(polygons.begin(), polygons.end(), [](const Polygon& p) { return !p.empty(); }));
    for (const Polygon& polygon : polygons)
        if (!polygon.empty())
            m_contours.emplace_back(polygon.points, false);

    create_from_m_contours(resolution);
}

void EdgeGrid::Grid::create(const std::vector<const Polygon*>& polygons, coord_t resolution)
{
    // Collect the contours.
    m_contours.clear();
    m_contours.reserve(std::count_if(polygons.begin(), polygons.end(), [](const Polygon* p) { return !p->empty(); }));
    for (const Polygon* polygon : polygons)
        if (!polygon->empty())
            m_contours.emplace_back(polygon->points, false);

    create_from_m_contours(resolution);
}

void EdgeGrid::Grid::create(const std::vector<Points>& polygons, coord_t resolution, bool open_polylines)
{
    // Collect the contours.
    m_contours.clear();
    m_contours.reserve(std::count_if(polygons.begin(), polygons.end(), [](const Points& p) { return p.size() > 1; }));
    for (const Points& points : polygons)
        if (points.size() > 1) {
            const Point* begin = points.data();
            const Point* end   = points.data() + points.size();
            bool         open  = open_polylines;
            if (open_polylines) {
                if (*begin == end[-1]) {
                    open = false;
                    --end;
                }
            } else {
                // assert(*begin != end[-1]);
            }

            m_contours.emplace_back(begin, end, open);
        }

    create_from_m_contours(resolution);
}

void EdgeGrid::Grid::create(const Polygons& polygons, const Polylines& polylines, coord_t resolution)
{
    // Collect the contours.
    m_contours.clear();
    m_contours.reserve(std::count_if(polygons.begin(), polygons.end(), [](const Polygon& p) { return p.size() > 1; }) +
                       std::count_if(polylines.begin(), polylines.end(), [](const Polyline& p) { return p.size() > 1; }));

    for (const Polyline& polyline : polylines)
        if (polyline.size() > 1) {
            const Point* begin = polyline.points.data();
            const Point* end   = polyline.points.data() + polyline.size();
            bool         open  = true;
            if (*begin == end[-1]) {
                open = false;
                --end;
            }
            m_contours.emplace_back(begin, end, open);
        }

    for (const Polygon& polygon : polygons)
        if (polygon.size() > 1)
            m_contours.emplace_back(polygon.points, false);

    create_from_m_contours(resolution);
}

void EdgeGrid::Grid::create(const ExPolygon& expoly, coord_t resolution)
{
    m_contours.clear();
    m_contours.reserve((expoly.contour.empty() ? 0 : 1) +
                       std::count_if(expoly.holes.begin(), expoly.holes.end(), [](const Polygon& p) { return !p.empty(); }));
    if (!expoly.contour.empty())
        m_contours.emplace_back(expoly.contour.points, false);
    for (const Polygon& hole : expoly.holes)
        if (!hole.empty())
            m_contours.emplace_back(hole.points, false);

    create_from_m_contours(resolution);
}

void EdgeGrid::Grid::create(const ExPolygons& expolygons, coord_t resolution)
{
    // Count the contours.
    size_t ncontours = 0;
    for (const ExPolygon& expoly : expolygons) {
        if (!expoly.contour.empty())
            ++ncontours;
        ncontours += std::count_if(expoly.holes.begin(), expoly.holes.end(), [](const Polygon& p) { return !p.empty(); });
    }

    // Collect the contours.
    m_contours.clear();
    m_contours.reserve(ncontours);
    for (const ExPolygon& expoly : expolygons) {
        if (!expoly.contour.empty())
            m_contours.emplace_back(expoly.contour.points, false);
        for (const Polygon& hole : expoly.holes)
            if (!hole.empty())
                m_contours.emplace_back(hole.points, false);
    }

    create_from_m_contours(resolution);
}

// [INTENT] `create_from_m_contours()` builds the spatial index in two Bresenham raster
// passes over all contour segments:
//   Pass 1: count how many segments touch each cell  → set cell.end
//   Prefix-sum: convert counts to [begin, end) ranges in m_cell_data
//   Pass 2: fill m_cell_data with (contour_idx, segment_idx) entries
//
// [COUPLING] Calls `visit_cells_intersecting_line()` (defined in the header as a template
// so it can be reused by callers). Any change to the Bresenham walk there must be mirrored
// in the rasterisation loop here.
//
// [HAZARD H540] The Bresenham accumulators `ex` / `ey` are computed as:
//   ex = (coord_t offset) * int64_t(dy)  — product of two coord_t values.
// `coord_t` is int32_t; `dy` fits in int32_t. The product fits in int64_t.
// However the reset term `int64_t(dy) * m_resolution` could overflow if
// `dy * m_resolution > INT64_MAX`. With typical nanometre resolution this is
// only a concern for objects > ~2 km — but the invariant is not asserted.
// m_contours has been initialized. Now fill in the edge grid.
void EdgeGrid::Grid::create_from_m_contours(coord_t resolution)
{
    assert(resolution > 0);
    // 1) Measure the bounding box.
    for (const Contour& contour : m_contours) {
        // assert(contour.num_segments() > 0);
        // assert(*contour.begin() != contour.end()[-1]);
        for (const Slic3r::Point& pt : contour)
            m_bbox.merge(pt);
    }

    coord_t eps = 16;
    m_bbox.min(0) -= eps;
    m_bbox.min(1) -= eps;
    m_bbox.max(0) += eps;
    m_bbox.max(1) += eps;

    // 2) Initialize the edge grid.
    m_resolution = resolution;
    m_cols       = (m_bbox.max(0) - m_bbox.min(0) + m_resolution - 1) / m_resolution;
    m_rows       = (m_bbox.max(1) - m_bbox.min(1) + m_resolution - 1) / m_resolution;
    m_cells.assign(m_rows * m_cols, Cell());

    // 3) First round of contour rasterization, count the edges per grid cell.
    for (size_t i = 0; i < m_contours.size(); ++i) {
        const Contour& contour = m_contours[i];
        for (size_t j = 0; j < contour.num_segments(); ++j) {
            // End points of the line segment.
            Slic3r::Point p1(contour.segment_start(j));
            Slic3r::Point p2(contour.segment_end(j));
            p1(0) -= m_bbox.min(0);
            p1(1) -= m_bbox.min(1);
            p2(0) -= m_bbox.min(0);
            p2(1) -= m_bbox.min(1);
            // Get the cells of the end points.
            coord_t ix  = p1(0) / m_resolution;
            coord_t iy  = p1(1) / m_resolution;
            coord_t ixb = p2(0) / m_resolution;
            coord_t iyb = p2(1) / m_resolution;
            assert(ix >= 0 && size_t(ix) < m_cols);
            assert(iy >= 0 && size_t(iy) < m_rows);
            assert(ixb >= 0 && size_t(ixb) < m_cols);
            assert(iyb >= 0 && size_t(iyb) < m_rows);
            // Account for the end points.
            ++m_cells[iy * m_cols + ix].end;
            if (ix == ixb && iy == iyb)
                // Both ends fall into the same cell.
                continue;
            // Raster the centeral part of the line.
            coord_t dx = std::abs(p2(0) - p1(0));
            coord_t dy = std::abs(p2(1) - p1(1));
            if (p1(0) < p2(0)) {
                int64_t ex = int64_t((ix + 1) * m_resolution - p1(0)) * int64_t(dy);
                if (p1(1) < p2(1)) {
                    // x positive, y positive
                    int64_t ey = int64_t((iy + 1) * m_resolution - p1(1)) * int64_t(dx);
                    do {
                        assert(ix <= ixb && iy <= iyb);
                        if (ex < ey) {
                            ey -= ex;
                            ex = int64_t(dy) * m_resolution;
                            ix += 1;
                        } else if (ex == ey) {
                            ex = int64_t(dy) * m_resolution;
                            ey = int64_t(dx) * m_resolution;
                            ix += 1;
                            iy += 1;
                        } else {
                            assert(ex > ey);
                            ex -= ey;
                            ey = int64_t(dx) * m_resolution;
                            iy += 1;
                        }
                        ++m_cells[iy * m_cols + ix].end;
                    } while (ix != ixb || iy != iyb);
                } else {
                    // x positive, y non positive
                    int64_t ey = int64_t(p1(1) - iy * m_resolution) * int64_t(dx);
                    do {
                        assert(ix <= ixb && iy >= iyb);
                        if (ex <= ey) {
                            ey -= ex;
                            ex = int64_t(dy) * m_resolution;
                            ix += 1;
                        } else {
                            ex -= ey;
                            ey = int64_t(dx) * m_resolution;
                            iy -= 1;
                        }
                        ++m_cells[iy * m_cols + ix].end;
                    } while (ix != ixb || iy != iyb);
                }
            } else {
                int64_t ex = int64_t(p1(0) - ix * m_resolution) * int64_t(dy);
                if (p1(1) < p2(1)) {
                    // x non positive, y positive
                    int64_t ey = int64_t((iy + 1) * m_resolution - p1(1)) * int64_t(dx);
                    do {
                        assert(ix >= ixb && iy <= iyb);
                        if (ex < ey) {
                            ey -= ex;
                            ex = int64_t(dy) * m_resolution;
                            ix -= 1;
                        } else {
                            assert(ex >= ey);
                            ex -= ey;
                            ey = int64_t(dx) * m_resolution;
                            iy += 1;
                        }
                        ++m_cells[iy * m_cols + ix].end;
                    } while (ix != ixb || iy != iyb);
                } else {
                    // x non positive, y non positive
                    int64_t ey = int64_t(p1(1) - iy * m_resolution) * int64_t(dx);
                    do {
                        assert(ix >= ixb && iy >= iyb);
                        if (ex < ey) {
                            ey -= ex;
                            ex = int64_t(dy) * m_resolution;
                            ix -= 1;
                        } else if (ex == ey) {
                            // The lower edge of a grid cell belongs to the cell.
                            // Handle the case where the ray may cross the lower left corner of a cell in a general case,
                            // or a left or lower edge in a degenerate case (horizontal or vertical line).
                            if (dx > 0) {
                                ex = int64_t(dy) * m_resolution;
                                ix -= 1;
                            }
                            if (dy > 0) {
                                ey = int64_t(dx) * m_resolution;
                                iy -= 1;
                            }
                        } else {
                            assert(ex > ey);
                            ex -= ey;
                            ey = int64_t(dx) * m_resolution;
                            iy -= 1;
                        }
                        ++m_cells[iy * m_cols + ix].end;
                    } while (ix != ixb || iy != iyb);
                }
            }
        }
    }

    // 4) Prefix sum the numbers of hits per cells to get an index into m_cell_data.
    size_t cnt = m_cells.front().end;
    for (size_t i = 1; i < m_cells.size(); ++i) {
        m_cells[i].begin = cnt;
        cnt += m_cells[i].end;
        m_cells[i].end = cnt;
    }

    // 5) Allocate the cell data.
    m_cell_data.assign(cnt, std::pair<size_t, size_t>(size_t(-1), size_t(-1)));

    // 6) Finally fill in m_cell_data by rasterizing the lines once again.
    for (size_t i = 0; i < m_cells.size(); ++i)
        m_cells[i].end = m_cells[i].begin;

    struct Visitor
    {
        Visitor(std::vector<std::pair<size_t, size_t>>& cell_data, std::vector<Cell>& cells, size_t cols)
            : cell_data(cell_data), cells(cells), cols(cols), i(0), j(0)
        {}

        inline bool operator()(coord_t iy, coord_t ix)
        {
            cell_data[cells[iy * cols + ix].end++] = std::pair<size_t, size_t>(i, j);
            // Continue traversing the grid along the edge.
            return true;
        }

        std::vector<std::pair<size_t, size_t>>& cell_data;
        std::vector<Cell>&                      cells;
        size_t                                  cols;
        size_t                                  i;
        size_t                                  j;
    } visitor(m_cell_data, m_cells, m_cols);

    assert(visitor.i == 0);
    for (; visitor.i < m_contours.size(); ++visitor.i) {
        const Contour& contour = m_contours[visitor.i];
        for (visitor.j = 0; visitor.j < contour.num_segments(); ++visitor.j)
            this->visit_cells_intersecting_line(contour.segment_start(visitor.j), contour.segment_end(visitor.j), visitor);
    }
}

// [HAZARD H546] The entire block below (from `#if 0` through the matching `#endif` at line ~693)
// is permanently disabled dead code. It contains:
//   - `div_floor()` helper — a variant of the Bresenham start-cell calculation
//   - `line_cell_intersect()` — an older O(K) brute-force edge-segment intersection tester
//   - `intersect()` (on MultiPoint) — a polygon/polyline intersection tester using the above
//   - `inside()` — a HALF-COMPLETED point-in-polygon test with `//FIXME finish this!` comments
//     and zero-initialised variables that make the logic nonfunctional
// None of these functions are callable. Do NOT port the `inside()` implementation — it is broken.
// Use `signed_distance()` with a small search_radius to determine inside/outside status instead.

#if 0
// Divide, round to a grid coordinate.
// Divide x/y, round down. y is expected to be positive.
static inline coord_t div_floor(coord_t x, coord_t y)
{
	assert(y > 0);
	return ((x < 0) ? (x - y + 1) : x) / y;
}

// Walk the polyline, test whether any lines of this polyline does not intersect
// any line stored into the grid.
bool EdgeGrid::Grid::intersect(const MultiPoint &polyline, bool closed)
{
	size_t n = polyline.points.size();
	if (closed)
		++ n;
	for (size_t i = 0; i < n; ++ i) {
		size_t j = i + 1;
		if (j == polyline.points.size())
			j = 0;
		Point p1src = polyline.points[i];
		Point p2src = polyline.points[j];
		Point p1 = p1src;
		Point p2 = p2src;
		// Discretize the line segment p1, p2.
		p1(0) -= m_bbox.min(0);
		p1(1) -= m_bbox.min(1);
		p2(0) -= m_bbox.min(0);
		p2(1) -= m_bbox.min(1);
		// Get the cells of the end points.
		coord_t ix = div_floor(p1(0), m_resolution);
		coord_t iy = div_floor(p1(1), m_resolution);
		coord_t ixb = div_floor(p2(0), m_resolution);
		coord_t iyb = div_floor(p2(1), m_resolution);
//		assert(ix >= 0 && ix < m_cols);
//		assert(iy >= 0 && iy < m_rows);
//		assert(ixb >= 0 && ixb < m_cols);
//		assert(iyb >= 0 && iyb < m_rows);
		// Account for the end points.
		if (line_cell_intersect(p1src, p2src, m_cells[iy*m_cols + ix]))
			return true;
		if (ix == ixb && iy == iyb)
			// Both ends fall into the same cell.
			continue;
		// Raster the centeral part of the line.
		coord_t dx = std::abs(p2(0) - p1(0));
		coord_t dy = std::abs(p2(1) - p1(1));
		if (p1(0) < p2(0)) {
			int64_t ex = int64_t((ix + 1)*m_resolution - p1(0)) * int64_t(dy);
			if (p1(1) < p2(1)) {
				int64_t ey = int64_t((iy + 1)*m_resolution - p1(1)) * int64_t(dx);
				do {
					assert(ix <= ixb && iy <= iyb);
					if (ex < ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix += 1;
					}
					else if (ex == ey) {
						ex = int64_t(dy) * m_resolution;
						ey = int64_t(dx) * m_resolution;
						ix += 1;
						iy += 1;
					}
					else {
						assert(ex > ey);
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy += 1;
					}
					if (line_cell_intersect(p1src, p2src, m_cells[iy*m_cols + ix]))
						return true;
				} while (ix != ixb || iy != iyb);
			}
			else {
				int64_t ey = int64_t(p1(1) - iy*m_resolution) * int64_t(dx);
				do {
					assert(ix <= ixb && iy >= iyb);
					if (ex <= ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix += 1;
					}
					else {
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy -= 1;
					}
					if (line_cell_intersect(p1src, p2src, m_cells[iy*m_cols + ix]))
						return true;
				} while (ix != ixb || iy != iyb);
			}
		}
		else {
			int64_t ex = int64_t(p1(0) - ix*m_resolution) * int64_t(dy);
			if (p1(1) < p2(1)) {
				int64_t ey = int64_t((iy + 1)*m_resolution - p1(1)) * int64_t(dx);
				do {
					assert(ix >= ixb && iy <= iyb);
					if (ex < ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix -= 1;
					}
					else {
						assert(ex >= ey);
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy += 1;
					}
					if (line_cell_intersect(p1src, p2src, m_cells[iy*m_cols + ix]))
						return true;
				} while (ix != ixb || iy != iyb);
			}
			else {
				int64_t ey = int64_t(p1(1) - iy*m_resolution) * int64_t(dx);
				do {
					assert(ix >= ixb && iy >= iyb);
					if (ex < ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix -= 1;
					}
					else if (ex == ey) {
						if (dx > 0) {
							ex = int64_t(dy) * m_resolution;
							ix -= 1;
						}
						if (dy > 0) {
							ey = int64_t(dx) * m_resolution;
							iy -= 1;
						}
					}
					else {
						assert(ex > ey);
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy -= 1;
					}
					if (line_cell_intersect(p1src, p2src, m_cells[iy*m_cols + ix]))
						return true;
				} while (ix != ixb || iy != iyb);
			}
		}
	}
	return false;
}

bool EdgeGrid::Grid::line_cell_intersect(const Point &p1a, const Point &p2a, const Cell &cell)
{
	BoundingBox bbox(p1a, p1a);
	bbox.merge(p2a);
	int64_t va_x = p2a(0) - p1a(0);
	int64_t va_y = p2a(1) - p1a(1);
	for (size_t i = cell.begin; i != cell.end; ++ i) {
		const std::pair<size_t, size_t> &cell_data = m_cell_data[i];
		// Contour indexed by the ith line of this cell.
		const Slic3r::Points &contour = *m_contours[cell_data.first];
		// Point indices in contour indexed by the ith line of this cell.
		size_t idx1 = cell_data.second;
		size_t idx2 = idx1 + 1;
		if (idx2 == contour.size())
			idx2 = 0;
		// The points of the ith line of this cell and its bounding box.
		const Point &p1b = contour[idx1];
		const Point &p2b = contour[idx2];
		BoundingBox bbox2(p1b, p1b);
		bbox2.merge(p2b);
		// Do the bounding boxes intersect?
		if (! bbox.overlap(bbox2))
			continue;
		// Now intersect the two line segments using exact arithmetics.
		int64_t w1_x = p1b(0) - p1a(0);
		int64_t w1_y = p1b(1) - p1a(1);
		int64_t w2_x = p2b(0) - p1a(0);
		int64_t w2_y = p2b(1) - p1a(1);
		int64_t side1 = va_x * w1_y - va_y * w1_x;
		int64_t side2 = va_x * w2_y - va_y * w2_x;
		if (side1 == side2 && side1 != 0)
			// The line segments don't intersect.
			continue;
		w1_x = p1a(0) - p1b(0);
		w1_y = p1a(1) - p1b(1);
		w2_x = p2a(0) - p1b(0);
		w2_y = p2a(1) - p1b(1);
		int64_t vb_x = p2b(0) - p1b(0);
		int64_t vb_y = p2b(1) - p1b(1);
		side1 = vb_x * w1_y - vb_y * w1_x;
		side2 = vb_x * w2_y - vb_y * w2_x;
		if (side1 == side2 && side1 != 0)
			// The line segments don't intersect.
			continue;
		// The line segments intersect.
		return true;
	}
	// The line segment (p1a, p2a) does not intersect any of the line segments inside this cell.
	return false;
}

// Test, whether a point is inside a contour.
bool EdgeGrid::Grid::inside(const Point &pt_src)
{
	Point p = pt_src;
	p(0) -= m_bbox.min(0);
	p(1) -= m_bbox.min(1);
	// Get the cell of the point.
	if (p(0) < 0 || p(1) < 0)
		return false;
	coord_t ix = p(0) / m_resolution;
	coord_t iy = p(1) / m_resolution;
	if (ix >= m_cols || iy >= m_rows)
		return false;

	size_t i_closest = (size_t)-1;
	bool   inside = false;

	{
		// Hit in the first cell?
		const Cell &cell = m_cells[iy * m_cols + ix];
		for (size_t i = cell.begin; i != cell.end; ++ i) {
			const std::pair<size_t, size_t> &cell_data = m_cell_data[i];
			// Contour indexed by the ith line of this cell.
			const Slic3r::Points &contour = *m_contours[cell_data.first];
			// Point indices in contour indexed by the ith line of this cell.
			size_t idx1 = cell_data.second;
			size_t idx2 = idx1 + 1;
			if (idx2 == contour.size())
				idx2 = 0;
			const Point &p1 = contour[idx1];
			const Point &p2 = contour[idx2];
			if (p1(1) < p2(1)) {
				if (p(1) < p1(1) || p(1) > p2(1))
					continue;
				//FIXME finish this!
				int64_t vx = 0;// pt_src
				//FIXME finish this!
				int64_t det = 0;
			} else if (p1(1) != p2(1)) {
				assert(p1(1) > p2(1));
				if (p(1) < p2(1) || p(1) > p1(1))
					continue;
			} else {
				assert(p1(1) == p2(1));
				if (p1(1) == p(1)) {
					if (p(0) >= p1(0) && p(0) <= p2(0))
						// On the segment.
						return true;
					// Before or after the segment.
					size_t idx0 = idx1 - 1;
					size_t idx2 = idx1 + 1;
					if (idx0 == (size_t)-1)
						idx0 = contour.size() - 1;
					if (idx2 == contour.size())
						idx2 = 0;
				}
			}
		}
	}

	//FIXME This code follows only a single direction. Better to follow the direction closest to the bounding box.
}
#endif

template<const int INCX, const int INCY> struct PropagateDanielssonSingleStep
{
    PropagateDanielssonSingleStep(float* aL, unsigned char* asigns, size_t astride, coord_t aresolution)
        : L(aL), signs(asigns), stride(astride), resolution(aresolution)
    {}
    inline void operator()(int r, int c, int addr_delta)
    {
        size_t addr = r * stride + c;
        if ((signs[addr] & 2) == 0) {
            float* v     = &L[addr << 1];
            float  l     = v[0] * v[0] + v[1] * v[1];
            float* v2s   = v + (addr_delta << 1);
            float  v2[2] = {v2s[0] + INCX * resolution, v2s[1] + INCY * resolution};
            float  l2    = v2[0] * v2[0] + v2[1] * v2[1];
            if (l2 < l) {
                v[0] = v2[0];
                v[1] = v2[1];
            }
        }
    }
    float*         L;
    unsigned char* signs;
    size_t         stride;
    coord_t        resolution;
};

struct PropagateDanielssonSingleVStep3
{
    PropagateDanielssonSingleVStep3(float* aL, unsigned char* asigns, size_t astride, coord_t aresolution)
        : L(aL), signs(asigns), stride(astride), resolution(aresolution)
    {}
    inline void operator()(int r, int c, int addr_delta, bool has_l, bool has_r)
    {
        size_t addr = r * stride + c;
        if ((signs[addr] & 2) == 0) {
            float* v     = &L[addr << 1];
            float  l     = v[0] * v[0] + v[1] * v[1];
            float* v2s   = v + (addr_delta << 1);
            float  v2[2] = {v2s[0], v2s[1] + resolution};
            float  l2    = v2[0] * v2[0] + v2[1] * v2[1];
            if (l2 < l) {
                v[0] = v2[0];
                v[1] = v2[1];
            }
            if (has_l) {
                float* v2sl = v2s - 1;
                v2[0]       = v2sl[0] + resolution;
                v2[1]       = v2sl[1] + resolution;
                l2          = v2[0] * v2[0] + v2[1] * v2[1];
                if (l2 < l) {
                    v[0] = v2[0];
                    v[1] = v2[1];
                }
            }
            if (has_r) {
                float* v2sr = v2s + 1;
                v2[0]       = v2sr[0] + resolution;
                v2[1]       = v2sr[1] + resolution;
                l2          = v2[0] * v2[0] + v2[1] * v2[1];
                if (l2 < l) {
                    v[0] = v2[0];
                    v[1] = v2[1];
                }
            }
        }
    }
    float*         L;
    unsigned char* signs;
    size_t         stride;
    coord_t        resolution;
};

// [INTENT] `calculate_sdf()` computes an approximate signed distance field for the stored
// closed contours using three passes:
//   1. Seed: for each contour edge, set exact distance + signum at nearby grid corners.
//   2. Propagate signum: flood-fill the binary inside/outside sign across all corners.
//   3. Propagate distance: Danielsson chamfer metric sweep (two passes: top-down, bottom-up).
//
// [STATE] After this call, `m_signed_distance_field` is a (rows+1)×(cols+1) float array,
// indexed by grid corners (not cell centres). Negative = inside contours, positive = outside.
//
// [CONCURRENCY] No locking. Must not be called from multiple threads on the same Grid.
//
// [HAZARD H541] If all contours are open polylines (no closed contours), the signum
// propagation flood-fill has no seeds with bit 1 set. The `signs` array retains its
// initial value of 4 ("not propagated") everywhere, which is then treated as positive
// (outside). The resulting SDF will have all-positive values — unsigned distance, not
// signed. The assert `contour.closed()` inside the loop catches this in Debug builds only.
//
// [HAZARD H547] The distance field is stored as `float`. At nanometre resolution
// (coord_t = int32_t, 1 unit = 1 nm), distances above ~16 mm lose sub-nanometre
// precision. Use double if sub-millimetre accuracy is required for large print beds.
void EdgeGrid::Grid::calculate_sdf()
{
#ifdef EDGE_GRID_DEBUG_OUTPUT
    static int iRun = 0;
    ++iRun;
#endif

    // 1) Initialize a signum and an unsigned vector to a zero iso surface.
    size_t nrows = m_rows + 1;
    size_t ncols = m_cols + 1;
    // Unsigned vectors towards the closest point on the surface.
    std::vector<float> L(nrows * ncols * 2, FLT_MAX);
    // Bit 0 set - negative.
    // Bit 1 set - original value, the distance value shall not be changed by the Danielsson propagation.
    // Bit 2 set - signum not propagated yet.
    std::vector<unsigned char> signs(nrows * ncols, 4);
    // SDF will be initially filled with unsigned DF.
    //	m_signed_distance_field.assign(nrows * ncols, FLT_MAX);
    float search_radius = float(m_resolution << 1);
    m_signed_distance_field.assign(nrows * ncols, search_radius);
    // For each cell:
    for (int r = 0; r < (int) m_rows; ++r) {
        for (int c = 0; c < (int) m_cols; ++c) {
            const Cell& cell = m_cells[r * m_cols + c];
            // For each segment in the cell:
            for (size_t i = cell.begin; i != cell.end; ++i) {
                const Contour& contour = m_contours[m_cell_data[i].first];
                assert(contour.closed());
                size_t ipt = m_cell_data[i].second;
                // End points of the line segment.
                const Slic3r::Point& p1 = contour.segment_start(ipt);
                const Slic3r::Point& p2 = contour.segment_end(ipt);
                // Segment vector
                const Slic3r::Point v_seg = p2 - p1;
                // l2 of v_seg
                const int64_t l2_seg = int64_t(v_seg(0)) * int64_t(v_seg(0)) + int64_t(v_seg(1)) * int64_t(v_seg(1));
                // For each corner of this cell and its 1 ring neighbours:
                for (int corner_y = -1; corner_y < 3; ++corner_y) {
                    coord_t corner_r = r + corner_y;
                    if (corner_r < 0 || (size_t) corner_r >= nrows)
                        continue;
                    for (int corner_x = -1; corner_x < 3; ++corner_x) {
                        coord_t corner_c = c + corner_x;
                        if (corner_c < 0 || (size_t) corner_c >= ncols)
                            continue;
                        float&        d_min = m_signed_distance_field[corner_r * ncols + corner_c];
                        Slic3r::Point pt(m_bbox.min(0) + corner_c * m_resolution, m_bbox.min(1) + corner_r * m_resolution);
                        Slic3r::Point v_pt = pt - p1;
                        // dot(p2-p1, pt-p1)
                        int64_t t_pt = int64_t(v_seg(0)) * int64_t(v_pt(0)) + int64_t(v_seg(1)) * int64_t(v_pt(1));
                        if (t_pt < 0) {
                            // Closest to p1.
                            double dabs = sqrt(int64_t(v_pt(0)) * int64_t(v_pt(0)) + int64_t(v_pt(1)) * int64_t(v_pt(1)));
                            if (dabs < d_min) {
                                // Previous point.
                                const Slic3r::Point& p0         = contour.segment_prev(ipt);
                                Slic3r::Point        v_seg_prev = p1 - p0;
                                int64_t t2_pt = int64_t(v_seg_prev(0)) * int64_t(v_pt(0)) + int64_t(v_seg_prev(1)) * int64_t(v_pt(1));
                                if (t2_pt > 0) {
                                    // Inside the wedge between the previous and the next segment.
                                    // Set the signum depending on whether the vertex is convex or reflex.
                                    int64_t det = int64_t(v_seg_prev(0)) * int64_t(v_seg(1)) - int64_t(v_seg_prev(1)) * int64_t(v_seg(0));
                                    assert(det != 0);
                                    d_min = dabs;
                                    // Fill in an unsigned vector towards the zero iso surface.
                                    float* l = &L[(corner_r * ncols + corner_c) << 1];
                                    l[0]     = std::abs(v_pt(0));
                                    l[1]     = std::abs(v_pt(1));
#ifdef _DEBUG
                                    double dabs2 = sqrt(l[0] * l[0] + l[1] * l[1]);
                                    assert(std::abs(dabs - dabs2) < 1e-4 * std::max(dabs, dabs2));
#endif /* _DEBUG */
                                    signs[corner_r * ncols + corner_c] = ((det < 0) ? 1 : 0) | 2;
                                }
                            }
                        } else if (t_pt > l2_seg) {
                            // Closest to p2. Then p2 is the starting point of another segment, which shall be discovered in the same cell.
                            continue;
                        } else {
                            // Closest to the segment.
                            assert(t_pt >= 0 && t_pt <= l2_seg);
                            int64_t d_seg = int64_t(v_seg(1)) * int64_t(v_pt(0)) - int64_t(v_seg(0)) * int64_t(v_pt(1));
                            double  d     = double(d_seg) / sqrt(double(l2_seg));
                            double  dabs  = std::abs(d);
                            if (dabs < d_min) {
                                d_min = dabs;
                                // Fill in an unsigned vector towards the zero iso surface.
                                float* l    = &L[(corner_r * ncols + corner_c) << 1];
                                float  linv = float(d_seg) / float(l2_seg);
                                l[0]        = std::abs(float(v_seg(1)) * linv);
                                l[1]        = std::abs(float(v_seg(0)) * linv);
#ifdef _DEBUG
                                double dabs2 = sqrt(l[0] * l[0] + l[1] * l[1]);
                                assert(std::abs(dabs - dabs2) <= 1e-4 * std::max(dabs, dabs2));
#endif /* _DEBUG */
                                signs[corner_r * ncols + corner_c] = ((d_seg < 0) ? 1 : 0) | 2;
                            }
                        }
                    }
                }
            }
        }
    }

#ifdef EDGE_GRID_DEBUG_OUTPUT
    {
        std::vector<uint8_t> pixels(ncols * nrows * 3, 0);
        for (coord_t r = 0; r < nrows; ++r) {
            for (coord_t c = 0; c < ncols; ++c) {
                uint8_t* pxl = pixels.data() + (((nrows - r - 1) * ncols) + c) * 3;
                float    d   = m_signed_distance_field[r * ncols + c];
                if (d != search_radius) {
                    float s  = 255 * d / search_radius;
                    int   is = std::max(0, std::min(255, int(floor(s + 0.5f))));
                    pxl[0]   = 255;
                    pxl[1]   = 255 - is;
                    pxl[2]   = 255 - is;
                } else {
                    pxl[0] = 0;
                    pxl[1] = 255;
                    pxl[2] = 0;
                }
            }
        }
        png::write_rgb_to_file_scaled(debug_out_path("unsigned_df-%d.png", iRun), ncols, nrows, pixels, 10);
    }
    {
        std::vector<uint8_t> pixels(ncols * nrows * 3, 0);
        for (coord_t r = 0; r < nrows; ++r) {
            for (coord_t c = 0; c < ncols; ++c) {
                unsigned char* pxl = pixels.data() + (((nrows - r - 1) * ncols) + c) * 3;
                float          d   = m_signed_distance_field[r * ncols + c];
                if (d != search_radius) {
                    float s  = 255 * d / search_radius;
                    int   is = std::max(0, std::min(255, int(floor(s + 0.5f))));
                    if ((signs[r * ncols + c] & 1) == 0) {
                        // Positive
                        pxl[0] = 255;
                        pxl[1] = 255 - is;
                        pxl[2] = 255 - is;
                    } else {
                        // Negative
                        pxl[0] = 255 - is;
                        pxl[1] = 255 - is;
                        pxl[2] = 255;
                    }
                } else {
                    pxl[0] = 0;
                    pxl[1] = 255;
                    pxl[2] = 0;
                }
            }
        }
        png::write_rgb_to_file_scaled(debug_out_path("signed_df-%d.png", iRun), ncols, nrows, pixels, 10);
    }
#endif // EDGE_GRID_DEBUG_OUTPUT

// 2) Propagate the signum.
#define PROPAGATE_SIGNUM_SINGLE_STEP(DELTA) \
    do { \
        size_t         addr    = r * ncols + c; \
        unsigned char& cur_val = signs[addr]; \
        if (cur_val & 4) { \
            unsigned char old_val = signs[addr + (DELTA)]; \
            if ((old_val & 4) == 0) \
                cur_val = old_val & 1; \
        } \
    } while (0);
    // Top to bottom propagation.
    for (size_t r = 0; r < nrows; ++r) {
        if (r > 0)
            for (size_t c = 0; c < ncols; ++c)
                PROPAGATE_SIGNUM_SINGLE_STEP(-int(ncols));
        for (size_t c = 1; c < ncols; ++c)
            PROPAGATE_SIGNUM_SINGLE_STEP(-1);
        for (int c = int(ncols) - 2; c >= 0; --c)
            PROPAGATE_SIGNUM_SINGLE_STEP(+1);
    }
    // Bottom to top propagation.
    for (int r = int(nrows) - 2; r >= 0; --r) {
        for (size_t c = 0; c < ncols; ++c)
            PROPAGATE_SIGNUM_SINGLE_STEP(+ncols);
        for (size_t c = 1; c < ncols; ++c)
            PROPAGATE_SIGNUM_SINGLE_STEP(-1);
        for (int c = int(ncols) - 2; c >= 0; --c)
            PROPAGATE_SIGNUM_SINGLE_STEP(+1);
    }
#undef PROPAGATE_SIGNUM_SINGLE_STEP

    // 3) Propagate the distance by the Danielsson chamfer metric.
    // Top to bottom propagation.
    PropagateDanielssonSingleStep<1, 0> danielsson_hstep(L.data(), signs.data(), ncols, m_resolution);
    PropagateDanielssonSingleStep<0, 1> danielsson_vstep(L.data(), signs.data(), ncols, m_resolution);
    PropagateDanielssonSingleVStep3     danielsson_vstep3(L.data(), signs.data(), ncols, m_resolution);
    // Top to bottom propagation.
    for (size_t r = 0; r < nrows; ++r) {
        if (r > 0)
            for (size_t c = 0; c < ncols; ++c)
                danielsson_vstep(r, c, -int(ncols));
        //				PROPAGATE_DANIELSSON_SINGLE_VSTEP3(-int(ncols), c != 0, c + 1 != ncols);
        for (size_t c = 1; c < ncols; ++c)
            danielsson_hstep(r, c, -1);
        for (int c = int(ncols) - 2; c >= 0; --c)
            danielsson_hstep(r, c, +1);
    }
    // Bottom to top propagation.
    for (int r = int(nrows) - 2; r >= 0; --r) {
        for (size_t c = 0; c < ncols; ++c)
            danielsson_vstep(r, c, +ncols);
        //			PROPAGATE_DANIELSSON_SINGLE_VSTEP3(+int(ncols), c != 0, c + 1 != ncols);
        for (size_t c = 1; c < ncols; ++c)
            danielsson_hstep(r, c, -1);
        for (int c = int(ncols) - 2; c >= 0; --c)
            danielsson_hstep(r, c, +1);
    }

    // Update signed distance field from absolte vectors to the iso-surface.
    for (size_t r = 0; r < nrows; ++r) {
        for (size_t c = 0; c < ncols; ++c) {
            size_t addr = r * ncols + c;
            float* v    = &L[addr << 1];
            float  d    = sqrt(v[0] * v[0] + v[1] * v[1]);
            if (signs[addr] & 1)
                d = -d;
            m_signed_distance_field[addr] = d;
        }
    }

#ifdef EDGE_GRID_DEBUG_OUTPUT
    {
        std::vector<uint8_t> pixels(ncols * nrows * 3, 0);
        float                search_radius = float(m_resolution * 5);
        for (coord_t r = 0; r < nrows; ++r) {
            for (coord_t c = 0; c < ncols; ++c) {
                uint8_t* pxl  = pixels.data() + (((nrows - r - 1) * ncols) + c) * 3;
                uint8_t  sign = signs[r * ncols + c];
                switch (sign) {
                case 0:
                    // Positive, outside of a narrow band.
                    pxl[0] = 0;
                    pxl[1] = 0;
                    pxl[2] = 255;
                    break;
                case 1:
                    // Negative, outside of a narrow band.
                    pxl[0] = 255;
                    pxl[1] = 0;
                    pxl[2] = 0;
                    break;
                case 2:
                    // Positive, outside of a narrow band.
                    pxl[0] = 100;
                    pxl[1] = 100;
                    pxl[2] = 255;
                    break;
                case 3:
                    // Negative, outside of a narrow band.
                    pxl[0] = 255;
                    pxl[1] = 100;
                    pxl[2] = 100;
                    break;
                case 4:
                    // This shall not happen. Undefined signum.
                    pxl[0] = 0;
                    pxl[1] = 255;
                    pxl[2] = 0;
                    break;
                default:
                    // This shall not happen. Invalid signum value.
                    pxl[0] = 255;
                    pxl[1] = 255;
                    pxl[2] = 255;
                    break;
                }
            }
        }
        png::write_rgb_to_file_scaled(debug_out_path("signed_df-signs-%d.png", iRun), ncols, nrows, pixels, 10);
    }
#endif // EDGE_GRID_DEBUG_OUTPUT

#ifdef EDGE_GRID_DEBUG_OUTPUT
    {
        std::vector<uint8_t> pixels(ncols * nrows * 3, 0);
        float                search_radius = float(m_resolution * 5);
        for (coord_t r = 0; r < nrows; ++r) {
            for (coord_t c = 0; c < ncols; ++c) {
                uint8_t* pxl = pixels.data() + (((nrows - r - 1) * ncols) + c) * 3;
                float    d   = m_signed_distance_field[r * ncols + c];
                float    s   = 255.f * fabs(d) / search_radius;
                int      is  = std::max(0, std::min(255, int(floor(s + 0.5f))));
                if (d < 0.f) {
                    pxl[0] = 255;
                    pxl[1] = 255 - is;
                    pxl[2] = 255 - is;
                } else {
                    pxl[0] = 255 - is;
                    pxl[1] = 255 - is;
                    pxl[2] = 255;
                }
            }
        }
        png::write_rgb_to_file_scaled(debug_out_path("signed_df2-%d.png", iRun), ncols, nrows, pixels, 10);
    }
#endif // EDGE_GRID_DEBUG_OUTPUT
}

// [INTENT] `signed_distance_bilinear()` performs a bilinear interpolation over the
// pre-computed `m_signed_distance_field` grid corners. It is the fallback path in
// `signed_distance()` when the edge search fails to find a contour within `search_radius`.
//
// [HAZARD H542] Points outside the grid bounding box are clamped to the boundary, then
// the absolute out-of-bounds offset is added/subtracted to the interpolated value. This
// linear extrapolation is unbounded — for a query point far outside the grid the returned
// value is large but finite. Callers that expect NaN or `FLT_MAX` for out-of-bounds points
// will receive an arbitrarily large (but signed) distance instead.
float EdgeGrid::Grid::signed_distance_bilinear(const Point& pt) const
{
    coord_t x       = pt(0) - m_bbox.min(0);
    coord_t y       = pt(1) - m_bbox.min(1);
    coord_t w       = m_resolution * m_cols;
    coord_t h       = m_resolution * m_rows;
    bool    clamped = false;
    coord_t xcl     = x;
    coord_t ycl     = y;
    if (x < 0) {
        xcl     = 0;
        clamped = true;
    } else if (x >= w) {
        xcl     = w - 1;
        clamped = true;
    }
    if (y < 0) {
        ycl     = 0;
        clamped = true;
    } else if (y >= h) {
        ycl     = h - 1;
        clamped = true;
    }

    coord_t cell_c = coord_t(floor(xcl / m_resolution));
    coord_t cell_r = coord_t(floor(ycl / m_resolution));
    float   tx     = float(xcl - cell_c * m_resolution) / float(m_resolution);
    assert(tx >= -1e-5 && tx < 1.f + 1e-5);
    float ty = float(ycl - cell_r * m_resolution) / float(m_resolution);
    assert(ty >= -1e-5 && ty < 1.f + 1e-5);
    size_t addr = cell_r * (m_cols + 1) + cell_c;
    float  f00  = m_signed_distance_field[addr];
    float  f01  = m_signed_distance_field[addr + 1];
    addr += m_cols + 1;
    float f10 = m_signed_distance_field[addr];
    float f11 = m_signed_distance_field[addr + 1];
    float f0  = (1.f - tx) * f00 + tx * f01;
    float f1  = (1.f - tx) * f10 + tx * f11;
    float f   = (1.f - ty) * f0 + ty * f1;

    if (clamped) {
        if (f > 0) {
            if (x < 0)
                f += -x;
            else if (x >= w)
                f += x - w + 1;
            if (y < 0)
                f += -y;
            else if (y >= h)
                f += y - h + 1;
        } else {
            if (x < 0)
                f -= -x;
            else if (x >= w)
                f -= x - w + 1;
            if (y < 0)
                f -= -y;
            else if (y >= h)
                f -= y - h + 1;
        }
    }

    return f;
}

// [INTENT] `closest_point_signed_distance()` performs a brute-force search over all
// contour segments that fall within the bounding box of `pt ± search_radius`. Returns
// the closest point with signed distance and the segment parameter `t` ∈ [0,1).
//
// [STATE] Returns `ClosestPointResult::valid() == false` if no segment is found within
// search_radius. The returned `t` is normalised to [0,1) (not raw dot-product).
//
// [HAZARD H543] When the query point is closest to a vertex (t_pt < 0 path), the sign is
// determined by the cross product `det` of the two adjacent segments. The `assert(det != 0)`
// fires in Debug if the adjacent segments are collinear (degenerate vertex, 180° angle).
// In Release this assert is absent; `sign_min` retains its previous iteration's value or
// stays 0, silently producing a wrong signed distance for that vertex.
EdgeGrid::Grid::ClosestPointResult EdgeGrid::Grid::closest_point_signed_distance(const Point& pt, coord_t search_radius) const
{
    BoundingBox bbox;
    bbox.min = bbox.max = Point(pt(0) - m_bbox.min(0), pt(1) - m_bbox.min(1));
    bbox.defined        = true;
    // Upper boundary, round to grid and test validity.
    bbox.max(0) += search_radius;
    bbox.max(1) += search_radius;
    ClosestPointResult result;
    if (bbox.max(0) < 0 || bbox.max(1) < 0)
        return result;
    bbox.max(0) /= m_resolution;
    bbox.max(1) /= m_resolution;
    if ((size_t) bbox.max(0) >= m_cols)
        bbox.max(0) = m_cols - 1;
    if ((size_t) bbox.max(1) >= m_rows)
        bbox.max(1) = m_rows - 1;
    // Lower boundary, round to grid and test validity.
    bbox.min(0) -= search_radius;
    bbox.min(1) -= search_radius;
    if (bbox.min(0) < 0)
        bbox.min(0) = 0;
    if (bbox.min(1) < 0)
        bbox.min(1) = 0;
    bbox.min(0) /= m_resolution;
    bbox.min(1) /= m_resolution;
    // Is the interval empty?
    if (bbox.min(0) > bbox.max(0) || bbox.min(1) > bbox.max(1))
        return result;
    // Traverse all cells in the bounding box.
    double d_min = double(search_radius);
    // Signum of the distance field at pt.
    int    sign_min   = 0;
    double l2_seg_min = 1.;
    for (int r = bbox.min(1); r <= bbox.max(1); ++r) {
        for (int c = bbox.min(0); c <= bbox.max(0); ++c) {
            const Cell& cell = m_cells[r * m_cols + c];
            for (size_t i = cell.begin; i < cell.end; ++i) {
                const size_t   contour_idx = m_cell_data[i].first;
                const Contour& contour     = m_contours[contour_idx];
                assert(contour.closed());
                size_t ipt = m_cell_data[i].second;
                // End points of the line segment.
                const Slic3r::Point& p1    = contour.segment_start(ipt);
                const Slic3r::Point& p2    = contour.segment_end(ipt);
                const Slic3r::Point  v_seg = p2 - p1;
                const Slic3r::Point  v_pt  = pt - p1;
                // dot(p2-p1, pt-p1)
                int64_t t_pt = int64_t(v_seg(0)) * int64_t(v_pt(0)) + int64_t(v_seg(1)) * int64_t(v_pt(1));
                // l2 of seg
                int64_t l2_seg = int64_t(v_seg(0)) * int64_t(v_seg(0)) + int64_t(v_seg(1)) * int64_t(v_seg(1));
                if (t_pt < 0) {
                    // Closest to p1.
                    double dabs = sqrt(int64_t(v_pt(0)) * int64_t(v_pt(0)) + int64_t(v_pt(1)) * int64_t(v_pt(1)));
                    if (dabs < d_min) {
                        // Previous point.
                        const Slic3r::Point& p0         = contour.segment_prev(ipt);
                        Slic3r::Point        v_seg_prev = p1 - p0;
                        int64_t              t2_pt = int64_t(v_seg_prev(0)) * int64_t(v_pt(0)) + int64_t(v_seg_prev(1)) * int64_t(v_pt(1));
                        if (t2_pt > 0) {
                            // Inside the wedge between the previous and the next segment.
                            d_min = dabs;
                            // Set the signum depending on whether the vertex is convex or reflex.
                            int64_t det = int64_t(v_seg_prev(0)) * int64_t(v_seg(1)) - int64_t(v_seg_prev(1)) * int64_t(v_seg(0));
                            assert(det != 0);
                            sign_min               = (det > 0) ? 1 : -1;
                            result.contour_idx     = contour_idx;
                            result.start_point_idx = ipt;
                            result.t               = 0.;
#ifndef NDEBUG
                            Vec2d  vfoot         = (p1 - pt).cast<double>();
                            double dist_foot     = vfoot.norm();
                            double dist_foot_err = dist_foot - d_min;
                            assert(std::abs(dist_foot_err) < 1e-7 * d_min);
#endif /* NDEBUG */
                        }
                    }
                } else if (t_pt > l2_seg) {
                    // Closest to p2. Then p2 is the starting point of another segment, which shall be discovered in the same cell.
                    continue;
                } else {
                    // Closest to the segment.
                    assert(t_pt >= 0 && t_pt <= l2_seg);
                    int64_t d_seg = int64_t(v_seg(1)) * int64_t(v_pt(0)) - int64_t(v_seg(0)) * int64_t(v_pt(1));
                    double  d     = double(d_seg) / sqrt(double(l2_seg));
                    double  dabs  = std::abs(d);
                    if (dabs < d_min) {
                        d_min                  = dabs;
                        sign_min               = (d_seg < 0) ? -1 : ((d_seg == 0) ? 0 : 1);
                        l2_seg_min             = l2_seg;
                        result.contour_idx     = contour_idx;
                        result.start_point_idx = ipt;
                        result.t               = t_pt;
#ifndef NDEBUG
                        Vec2d  foot      = p1.cast<double>() * (1. - result.t / l2_seg_min) + p2.cast<double>() * (result.t / l2_seg_min);
                        Vec2d  vfoot     = foot - pt.cast<double>();
                        double dist_foot = vfoot.norm();
                        double dist_foot_err = dist_foot - d_min;
                        assert(std::abs(dist_foot_err) < 1e-7 || std::abs(dist_foot_err) < 1e-7 * d_min);
#endif /* NDEBUG */
                    }
                }
            }
        }
    }
    if (result.contour_idx != size_t(-1) && d_min <= double(search_radius)) {
        result.distance = d_min * sign_min;
        result.t /= l2_seg_min;
        assert(result.t >= 0. && result.t <= 1.);
#ifndef NDEBUG
        {
            const Contour&       contour = m_contours[result.contour_idx];
            const Slic3r::Point& p1      = contour.segment_start(result.start_point_idx);
            const Slic3r::Point& p2      = contour.segment_end(result.start_point_idx);
            Vec2d                vfoot;
            if (result.t == 0)
                vfoot = p1.cast<double>() - pt.cast<double>();
            else
                vfoot = p1.cast<double>() * (1. - result.t) + p2.cast<double>() * result.t - pt.cast<double>();
            double dist_foot     = vfoot.norm();
            double dist_foot_err = dist_foot - std::abs(result.distance);
            assert(std::abs(dist_foot_err) < 1e-7 || std::abs(dist_foot_err) < 1e-7 * std::abs(result.distance));
        }
#endif /* NDEBUG */
    } else
        result = ClosestPointResult();
    return result;
}

// [INTENT] `signed_distance_edges()` is the primary brute-force signed-distance search,
// used both directly and as the primary path in `signed_distance()`. Unlike
// `closest_point_signed_distance()`, it also sets `*pon_segment` to indicate whether the
// closest point is on a segment interior vs. a vertex.
//
// [COUPLING] Logically identical to `closest_point_signed_distance()` but returns less
// metadata (no contour_idx / start_point_idx / t). Any fix to the sign determination in
// one function must be mirrored in the other.
bool EdgeGrid::Grid::signed_distance_edges(const Point& pt, coord_t search_radius, coordf_t& result_min_dist, bool* pon_segment) const
{
    BoundingBox bbox;
    bbox.min = bbox.max = Point(pt(0) - m_bbox.min(0), pt(1) - m_bbox.min(1));
    bbox.defined        = true;
    // Upper boundary, round to grid and test validity.
    bbox.max(0) += search_radius;
    bbox.max(1) += search_radius;
    if (bbox.max(0) < 0 || bbox.max(1) < 0)
        return false;
    bbox.max(0) /= m_resolution;
    bbox.max(1) /= m_resolution;
    if ((size_t) bbox.max(0) >= m_cols)
        bbox.max(0) = m_cols - 1;
    if ((size_t) bbox.max(1) >= m_rows)
        bbox.max(1) = m_rows - 1;
    // Lower boundary, round to grid and test validity.
    bbox.min(0) -= search_radius;
    bbox.min(1) -= search_radius;
    if (bbox.min(0) < 0)
        bbox.min(0) = 0;
    if (bbox.min(1) < 0)
        bbox.min(1) = 0;
    bbox.min(0) /= m_resolution;
    bbox.min(1) /= m_resolution;
    // Is the interval empty?
    if (bbox.min(0) > bbox.max(0) || bbox.min(1) > bbox.max(1))
        return false;
    // Traverse all cells in the bounding box.
    double d_min = double(search_radius);
    // Signum of the distance field at pt.
    int  sign_min   = 0;
    bool on_segment = false;
    for (int r = bbox.min(1); r <= bbox.max(1); ++r) {
        for (int c = bbox.min(0); c <= bbox.max(0); ++c) {
            const Cell& cell = m_cells[r * m_cols + c];
            for (size_t i = cell.begin; i < cell.end; ++i) {
                const Contour& contour = m_contours[m_cell_data[i].first];
                assert(contour.closed());
                size_t ipt = m_cell_data[i].second;
                // End points of the line segment.
                const Slic3r::Point& p1    = contour.segment_start(ipt);
                const Slic3r::Point& p2    = contour.segment_end(ipt);
                Slic3r::Point        v_seg = p2 - p1;
                Slic3r::Point        v_pt  = pt - p1;
                // dot(p2-p1, pt-p1)
                int64_t t_pt = int64_t(v_seg(0)) * int64_t(v_pt(0)) + int64_t(v_seg(1)) * int64_t(v_pt(1));
                // l2 of seg
                int64_t l2_seg = int64_t(v_seg(0)) * int64_t(v_seg(0)) + int64_t(v_seg(1)) * int64_t(v_seg(1));
                if (t_pt < 0) {
                    // Closest to p1.
                    double dabs = sqrt(int64_t(v_pt(0)) * int64_t(v_pt(0)) + int64_t(v_pt(1)) * int64_t(v_pt(1)));
                    if (dabs < d_min) {
                        // Previous point.
                        const Slic3r::Point& p0         = contour.segment_prev(ipt);
                        Slic3r::Point        v_seg_prev = p1 - p0;
                        int64_t              t2_pt = int64_t(v_seg_prev(0)) * int64_t(v_pt(0)) + int64_t(v_seg_prev(1)) * int64_t(v_pt(1));
                        if (t2_pt > 0) {
                            // Inside the wedge between the previous and the next segment.
                            d_min = dabs;
                            // Set the signum depending on whether the vertex is convex or reflex.
                            int64_t det = int64_t(v_seg_prev(0)) * int64_t(v_seg(1)) - int64_t(v_seg_prev(1)) * int64_t(v_seg(0));
                            assert(det != 0);
                            sign_min   = (det > 0) ? 1 : -1;
                            on_segment = false;
                        }
                    }
                } else if (t_pt > l2_seg) {
                    // Closest to p2. Then p2 is the starting point of another segment, which shall be discovered in the same cell.
                    continue;
                } else {
                    // Closest to the segment.
                    assert(t_pt >= 0 && t_pt <= l2_seg);
                    int64_t d_seg = int64_t(v_seg(1)) * int64_t(v_pt(0)) - int64_t(v_seg(0)) * int64_t(v_pt(1));
                    double  d     = double(d_seg) / sqrt(double(l2_seg));
                    double  dabs  = std::abs(d);
                    if (dabs < d_min) {
                        d_min      = dabs;
                        sign_min   = (d_seg < 0) ? -1 : ((d_seg == 0) ? 0 : 1);
                        on_segment = true;
                    }
                }
            }
        }
    }
    if (d_min >= search_radius)
        return false;
    result_min_dist = d_min * sign_min;
    if (pon_segment != NULL)
        *pon_segment = on_segment;
    return true;
}

bool EdgeGrid::Grid::signed_distance(const Point& pt, coord_t search_radius, coordf_t& result_min_dist) const
{
    if (signed_distance_edges(pt, search_radius, result_min_dist))
        return true;
    if (m_signed_distance_field.empty())
        return false;
    result_min_dist = signed_distance_bilinear(pt);
    return true;
}

// [INTENT] `contours_simplified()` converts the rasterised binary cell grid back into
// a set of `Polygons` that tightly enclose the cells occupied by contour edges. It is
// used to generate support material footprints: the edge-grid shadow is expanded by
// `offset` on each output corner, giving a rough contour slightly inside or outside the
// original.
//
// [HAZARD H544] The polygon chain-walking loop uses `goto end_of_poly` to break out of
// the inner loop when it detects the polygon has closed. Same pattern as H528 in
// Geometry.cpp. During porting, replace with a flag or a lambda to avoid `goto`.
//
// [COUPLING] Relies on `cell_inside_or_crossing()` which reads `m_signed_distance_field`
// if populated. If `calculate_sdf()` has NOT been called, the SDF check is skipped
// (empty check), and only cell.begin < cell.end is used for inside/crossing detection.
Polygons EdgeGrid::Grid::contours_simplified(coord_t offset, bool fill_holes) const
{
    assert(std::abs(2 * offset) < m_resolution);

    typedef std::unordered_multimap<Point, int, PointHash> EndPointMapType;
    // 0) Prepare a binary grid.
    size_t            cell_rows = m_rows + 2;
    size_t            cell_cols = m_cols + 2;
    std::vector<char> cell_inside(cell_rows * cell_cols, false);
    for (int r = 0; r < int(cell_rows); ++r)
        for (int c = 0; c < int(cell_cols); ++c)
            cell_inside[r * cell_cols + c] = cell_inside_or_crossing(r - 1, c - 1);
    // Fill in empty cells, which have a left / right neighbor filled.
    // Fill in empty cells, which have the top / bottom neighbor filled.
    if (fill_holes) {
        std::vector<char> cell_inside2(cell_inside);
        for (int r = 1; r + 1 < int(cell_rows); ++r) {
            for (int c = 1; c + 1 < int(cell_cols); ++c) {
                int addr = r * cell_cols + c;
                if ((cell_inside2[addr - 1] && cell_inside2[addr + 1]) || (cell_inside2[addr - cell_cols] && cell_inside2[addr + cell_cols]))
                    cell_inside[addr] = true;
            }
        }
    }

    // 1) Collect the lines.
    std::vector<Line> lines;
    EndPointMapType   start_point_to_line_idx;
    for (coord_t r = 0; r <= coord_t(m_rows); ++r) {
        for (coord_t c = 0; c <= coord_t(m_cols); ++c) {
            size_t addr    = (r + 1) * cell_cols + c + 1;
            bool   left    = cell_inside[addr - 1];
            bool   top     = cell_inside[addr - cell_cols];
            bool   current = cell_inside[addr];
            if (left != current) {
                lines.push_back(left ? Line(Point(c, r + 1), Point(c, r)) : Line(Point(c, r), Point(c, r + 1)));
                start_point_to_line_idx.insert(std::pair<Point, int>(lines.back().a, int(lines.size()) - 1));
            }
            if (top != current) {
                lines.push_back(top ? Line(Point(c, r), Point(c + 1, r)) : Line(Point(c + 1, r), Point(c, r)));
                start_point_to_line_idx.insert(std::pair<Point, int>(lines.back().a, int(lines.size()) - 1));
            }
        }
    }

    // 2) Chain the lines.
    std::vector<char> line_processed(lines.size(), false);
    Polygons          out;
    for (int i_candidate = 0; i_candidate < int(lines.size()); ++i_candidate) {
        if (line_processed[i_candidate])
            continue;
        Polygon poly;
        line_processed[i_candidate] = true;
        poly.points.push_back(lines[i_candidate].b);
        int i_line_current = i_candidate;
        for (;;) {
            std::pair<EndPointMapType::iterator, EndPointMapType::iterator> line_range = start_point_to_line_idx.equal_range(
                lines[i_line_current].b);
            // The interval has to be non empty, there shall be at least one line continuing the current one.
            assert(line_range.first != line_range.second);
            int i_next = -1;
            for (EndPointMapType::iterator it = line_range.first; it != line_range.second; ++it) {
                if (it->second == i_candidate) {
                    // closing the loop.
                    goto end_of_poly;
                }
                if (line_processed[it->second])
                    continue;
                if (i_next == -1) {
                    i_next = it->second;
                } else {
                    // This is a corner, where two lines meet exactly. Pick the line, which encloses a smallest angle with
                    // the current edge.
                    const Line&  line_current = lines[i_line_current];
                    const Line&  line_next    = lines[it->second];
                    const Vector v1           = line_current.vector();
                    const Vector v2           = line_next.vector();
                    int64_t      cross        = int64_t(v1(0)) * int64_t(v2(1)) - int64_t(v2(0)) * int64_t(v1(1));
                    if (cross > 0) {
                        // This has to be a convex right angle. There is no better next line.
                        i_next = it->second;
                        break;
                    }
                }
            }
            line_processed[i_next] = true;
            i_line_current         = i_next;
            poly.points.push_back(lines[i_line_current].b);
        }
    end_of_poly:
        out.push_back(std::move(poly));
    }

    // 3) Scale the polygons back into world, shrink slightly and remove collinear points.
    for (size_t i = 0; i < out.size(); ++i) {
        Polygon& poly = out[i];
        for (size_t j = 0; j < poly.points.size(); ++j) {
            Point& p = poly.points[j];
            p(0) *= m_resolution;
            p(1) *= m_resolution;
            p(0) += m_bbox.min(0);
            p(1) += m_bbox.min(1);
        }
        // Shrink the contour slightly, so if the same contour gets discretized and simplified again, one will get the same result.
        // Remove collineaer points.
        Points pts;
        pts.reserve(poly.points.size());
        for (size_t j = 0; j < poly.points.size(); ++j) {
            size_t j0 = (j == 0) ? poly.points.size() - 1 : j - 1;
            size_t j2 = (j + 1 == poly.points.size()) ? 0 : j + 1;
            Point  v  = poly.points[j2] - poly.points[j0];
            if (v(0) != 0 && v(1) != 0) {
                // This is a corner point. Copy it to the output contour.
                Point p = poly.points[j];
                p(1) += (v(0) < 0) ? -offset : offset;
                p(0) += (v(1) > 0) ? -offset : offset;
                pts.push_back(p);
            }
        }
        poly.points = std::move(pts);
    }
    return out;
}

// [INTENT] `intersecting_edges()` returns all pairs of contour segments that geometrically
// intersect. The algorithm is O(N·K) where K is the maximum number of segments per cell.
// Typical K is small (a few segments per cell), so this is fast in practice.
//
// [HAZARD H545] Dead-branch deduplication bug: the `jfirst` flag is computed to determine
// which pair order to emit, but BOTH branches of the ternary operator `?:` produce
// identical pairs `(icontour, ipt, jcontour, jpt)`. The intended swapped pair for the
// `jfirst` case is never emitted. The subsequent `sort_remove_duplicates()` is then
// redundant since no actual duplicates are inserted. The output is functionally correct
// (each intersecting pair appears exactly once), but the ordering guarantee implied by
// `jfirst` is not achieved. Any port should fix this or simplify by removing `jfirst`.
std::vector<std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge>> EdgeGrid::Grid::intersecting_edges() const
{
    std::vector<std::pair<ContourEdge, ContourEdge>> out;
    // For each cell:
    for (int r = 0; r < (int) m_rows; ++r) {
        for (int c = 0; c < (int) m_cols; ++c) {
            const Cell& cell = m_cells[r * m_cols + c];
            // For each pair of segments in the cell:
            for (size_t i = cell.begin; i != cell.end; ++i) {
                const Contour& icontour = m_contours[m_cell_data[i].first];
                size_t         ipt      = m_cell_data[i].second;
                // End points of the line segment and their vector.
                const Slic3r::Point& ip1 = icontour.segment_start(ipt);
                const Slic3r::Point& ip2 = icontour.segment_end(ipt);
                for (size_t j = i + 1; j != cell.end; ++j) {
                    const Contour& jcontour = m_contours[m_cell_data[j].first];
                    size_t         jpt      = m_cell_data[j].second;
                    // End points of the line segment and their vector.
                    const Slic3r::Point& jp1 = jcontour.segment_start(jpt);
                    const Slic3r::Point& jp2 = jcontour.segment_end(jpt);
                    if (&icontour == &jcontour && (&ip1 == &jp2 || &jp1 == &ip2))
                        // Segments of the same contour share a common vertex.
                        continue;
                    if (Geometry::segments_intersect(ip1, ip2, jp1, jp2)) {
                        // The two segments intersect. Add them to the output.
                        int jfirst = (&jcontour < &icontour) || (&jcontour == &icontour && jpt < ipt);
                        out.emplace_back(jfirst ? std::make_pair(std::make_pair(&icontour, ipt), std::make_pair(&jcontour, jpt)) :
                                                  std::make_pair(std::make_pair(&icontour, ipt), std::make_pair(&jcontour, jpt)));
                    }
                }
            }
        }
    }
    Slic3r::sort_remove_duplicates(out);
    return out;
}

bool EdgeGrid::Grid::has_intersecting_edges() const
{
    // For each cell:
    for (int r = 0; r < (int) m_rows; ++r) {
        for (int c = 0; c < (int) m_cols; ++c) {
            const Cell& cell = m_cells[r * m_cols + c];
            // For each pair of segments in the cell:
            for (size_t i = cell.begin; i != cell.end; ++i) {
                const Contour& icontour = m_contours[m_cell_data[i].first];
                size_t         ipt      = m_cell_data[i].second;
                // End points of the line segment and their vector.
                const Slic3r::Point& ip1 = icontour.segment_start(ipt);
                const Slic3r::Point& ip2 = icontour.segment_end(ipt);
                for (size_t j = i + 1; j != cell.end; ++j) {
                    const Contour& jcontour = m_contours[m_cell_data[j].first];
                    size_t         jpt      = m_cell_data[j].second;
                    // End points of the line segment and their vector.
                    const Slic3r::Point& jp1 = jcontour.segment_start(jpt);
                    const Slic3r::Point& jp2 = jcontour.segment_end(jpt);
                    if (!(&icontour == &jcontour && (&ip1 == &jp2 || &jp1 == &ip2)) && Geometry::segments_intersect(ip1, ip2, jp1, jp2))
                        return true;
                }
            }
        }
    }
    return false;
}

// [INTENT] `save_png()` is a debugging utility that renders a signed distance field
// visualisation of the Grid to a PNG file. It is only called via `EDGE_GRID_DEBUG_OUTPUT`
// macro or from unit tests. Not used in release builds.
// [COUPLING] Calls `grid.signed_distance()` which requires `calculate_sdf()` to have
// been called first (or falls back to `signed_distance_bilinear()` which will return 0
// for all points if the SDF is empty).
void EdgeGrid::save_png(const EdgeGrid::Grid& grid, const BoundingBox& bbox, coord_t resolution, const char* path, size_t scale)
{
    coord_t w = (bbox.max(0) - bbox.min(0) + resolution - 1) / resolution;
    coord_t h = (bbox.max(1) - bbox.min(1) + resolution - 1) / resolution;

    std::vector<uint8_t> pixels(w * h * 3, 0);

    const coord_t search_radius        = grid.resolution() * 2;
    const coord_t display_blend_radius = grid.resolution() * 2;
    for (coord_t r = 0; r < h; ++r) {
        for (coord_t c = 0; c < w; ++c) {
            unsigned char* pxl = pixels.data() + (((h - r - 1) * w) + c) * 3;
            Point          pt(c * resolution + bbox.min(0), r * resolution + bbox.min(1));
            coordf_t       min_dist;
            bool           on_segment = true;
#if 0
			if (grid.signed_distance_edges(pt, search_radius, min_dist, &on_segment)) {
#else
            if (grid.signed_distance(pt, search_radius, min_dist)) {
#endif
                float s  = 255 * std::abs(min_dist) / float(display_blend_radius);
                int   is = std::max(0, std::min(255, int(floor(s + 0.5f))));
                if (min_dist < 0) {
                    if (on_segment) {
                        pxl[0] = 255;
                        pxl[1] = 255 - is;
                        pxl[2] = 255 - is;
                    } else {
                        pxl[0] = 255;
                        pxl[1] = 0;
                        pxl[2] = 255 - is;
                    }
                } else {
                    if (on_segment) {
                        pxl[0] = 255 - is;
                        pxl[1] = 255 - is;
                        pxl[2] = 255;
                    } else {
                        pxl[0] = 255 - is;
                        pxl[1] = 0;
                        pxl[2] = 255;
                    }
                }
            } else {
                pxl[0] = 0;
                pxl[1] = 255;
                pxl[2] = 0;
            }

            float gridx = float(pt(0) - grid.bbox().min(0)) / float(grid.resolution());
            float gridy = float(pt(1) - grid.bbox().min(1)) / float(grid.resolution());
            if (gridx >= -0.4f && gridy >= -0.4f && gridx <= grid.cols() + 0.4f && gridy <= grid.rows() + 0.4f) {
                int   ix = int(floor(gridx + 0.5f));
                int   iy = int(floor(gridy + 0.5f));
                float dx = gridx - float(ix);
                float dy = gridy - float(iy);
                float d  = sqrt(dx * dx + dy * dy) * float(grid.resolution()) / float(resolution);
                if (d < 1.f) {
                    // Less than 1 pixel from the grid point.
                    float t = 0.5f + 0.5f * d;
                    pxl[0]  = (unsigned char) (t * pxl[0]);
                    pxl[1]  = (unsigned char) (t * pxl[1]);
                    pxl[2]  = (unsigned char) (t * pxl[2]);
                }
            }

            float dgrid = fabs(min_dist) / float(grid.resolution());
            float igrid = floor(dgrid + 0.5f);
            dgrid       = std::abs(dgrid - igrid) * float(grid.resolution()) / float(resolution);
            if (dgrid < 1.f) {
                // Less than 1 pixel from the grid point.
                float t = 0.5f + 0.5f * dgrid;
                pxl[0]  = (unsigned char) (t * pxl[0]);
                pxl[1]  = (unsigned char) (t * pxl[1]);
                pxl[2]  = (unsigned char) (t * pxl[2]);
                if (igrid > 0.f) {
                    // Other than zero iso contour.
                    int g  = pxl[1] + 255.f * (1.f - t);
                    pxl[1] = std::min(g, 255);
                }
            }
        }
    }

    png::write_rgb_to_file_scaled(path, w, h, pixels, scale);
}

// Find all pairs of intersectiong edges from the set of polygons.
// [INTENT] Free-function convenience wrapper. Builds a Grid from `polygons` using the
// average edge length as the cell resolution, then calls `Grid::intersecting_edges()`.
// Used for mesh validation and debugging.
// [COUPLING] The auto-resolution heuristic (average edge length) may produce a very
// coarse grid for polygons with a mix of long and short edges, causing many false
// positives in the intersection test. A fixed or caller-specified resolution would be
// more predictable.
std::vector<std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge>> intersecting_edges(const Polygons& polygons)
{
    double      len = 0;
    size_t      cnt = 0;
    BoundingBox bbox;
    for (const Polygon& poly : polygons) {
        if (poly.points.size() < 2)
            continue;
        for (size_t i = 0; i < poly.points.size(); ++i) {
            bbox.merge(poly.points[i]);
            size_t j = (i == 0) ? (poly.points.size() - 1) : i - 1;
            len += (poly.points[j] - poly.points[i]).cast<double>().norm();
            ++cnt;
        }
    }

    std::vector<std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge>> out;
    if (cnt > 0) {
        len /= double(cnt);
        bbox.offset(20);
        EdgeGrid::Grid grid;
        grid.set_bbox(bbox);
        grid.create(polygons, len);
        out = grid.intersecting_edges();
    }
    return out;
}

// Find all pairs of intersectiong edges from the set of polygons, highlight them in an SVG.
void export_intersections_to_svg(const std::string& filename, const Polygons& polygons)
{
    std::vector<std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge>> intersections = intersecting_edges(polygons);
    BoundingBox                                                                      bbox          = get_extents(polygons);
    SVG                                                                              svg(filename.c_str(), bbox);
    svg.draw(union_ex(polygons), "gray", 0.25f);
    svg.draw_outline(polygons, "black");
    std::set<const EdgeGrid::Contour*> intersecting_contours;
    for (const std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge>& ie : intersections) {
        intersecting_contours.insert(ie.first.first);
        intersecting_contours.insert(ie.second.first);
    }
    // Highlight the contours with intersections.
    coord_t line_width = coord_t(scale_(0.01));
    for (const EdgeGrid::Contour* ic : intersecting_contours) {
        if (ic->open())
            svg.draw(Polyline(Points(ic->begin(), ic->end())), "green");
        else {
            Polygon polygon(Points(ic->begin(), ic->end()));
            svg.draw_outline(polygon, "green");
            svg.draw_outline(polygon, "black", line_width);
        }
    }
    // Paint the intersections.
    for (const std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge>& intersecting_edges : intersections) {
        auto edge = [](const EdgeGrid::Grid::ContourEdge& e) {
            return Line(e.first->segment_start(e.second), e.first->segment_end(e.second));
        };
        svg.draw(edge(intersecting_edges.first), "red", line_width);
        svg.draw(edge(intersecting_edges.second), "red", line_width);
    }
    svg.Close();
}

} // namespace Slic3r
