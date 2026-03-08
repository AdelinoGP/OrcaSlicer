// [INTENT] BridgeDetector.cpp — implementation of bridge orientation detection.
//
// A bridge is a span of filament deposited over empty space, anchored at its endpoints
// by solid material. The quality of a bridge depends critically on direction:
// too-long unsupported lines sag. This detector finds the angle that maximises the
// total length of lines whose BOTH endpoints land inside anchor regions.
//
// Algorithm summary (BridgeDetector class):
//   initialize()        — outsets the bridge area, clips with lower slice to find _edges
//                         (supporting edges) and _anchor_regions (supporting polygons).
//   detect_angle()      — sweeps candidate angles; for each, generates a grid of parallel
//                         lines across the bounding box, clips to the bridge area, and
//                         counts how much total length has BOTH endpoints in anchor regions.
//                         Picks the angle with maximum coverage, breaking ties in favour
//                         of shorter max-span.
//   bridge_direction_candidates() — returns angles to test: uniform 5-degree grid +
//                         angles of bridge contour edges + angles of anchor support edges.
//   coverage()          — given an angle, returns the trapezoid polygons that are bridged
//                         (i.e. each vertical strip fully spanning two anchor regions).
//   unsupported_edges() — returns perimeter segments that are NOT supported by lower slices
//                         and would benefit from additional support.
//
// [COUPLING] ClipperUtils (offset, intersection_pl, intersection_ln, intersection_ex,
//            union_safety_offset), Geometry (directions_parallel, rad2deg).
// [CONCURRENCY] No shared state; each BridgeDetector instance is self-contained.
//               Safe for parallel construction in multi-region slicing.

#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include <algorithm>

namespace Slic3r {

// [INTENT] Constructor 1: takes a single ExPolygon by value. Moves it into expolygons_owned
//   so the caller need not keep any polygon alive. The expolygons reference then aliases
//   the local expolygons_owned vector.
// [MEMORY] _expolygon is moved once; no copies of the polygon data are made.
BridgeDetector::BridgeDetector(ExPolygon _expolygon, const ExPolygons& _lower_slices, coord_t _spacing)
    : // The original infill polygon, not inflated.
    expolygons(expolygons_owned)
    ,
    // All surfaces of the object supporting this region.
    lower_slices(_lower_slices)
    , spacing(_spacing)
{
    this->expolygons_owned.push_back(std::move(_expolygon));
    initialize();
}

// [INTENT] Constructor 2: takes const ExPolygons& — no copy. Caller must guarantee the
//   external ExPolygons outlive this object. Used when the caller owns the polygons for
//   the lifetime of slicing (e.g. LayerRegion keeps them alive throughout).
// [HAZARD H335] No lifetime enforcement; dangling reference is silent UB in release builds.
BridgeDetector::BridgeDetector(const ExPolygons& _expolygons, const ExPolygons& _lower_slices, coord_t _spacing)
    : // The original infill polygon, not inflated.
    expolygons(_expolygons)
    ,
    // All surfaces of the object supporting this region.
    lower_slices(_lower_slices)
    , spacing(_spacing)
{
    initialize();
}

// [INTENT] initialize() — common setup called by both constructors.
//   Computes _edges and _anchor_regions from the bridge expolygon and lower_slices.
//   Steps:
//   1. Outset bridge area by 'spacing' (one extrusion width) to probe for overlap.
//   2. Extract contours of lower_slices; clip grown bridge outline against them
//      to get polylines lying on supporting surfaces → _edges.
//      These represent the "landing edges" of the bridge on solid material.
//   3. Intersect grown bridge area with lower_slices union → _anchor_regions.
//      These closed polygons are where bridge extrusions can actually anchor.
//
// [STATE] Mutates: this->_edges, this->_anchor_regions, this->resolution, this->angle.
// [HAZARD H336] union_safety_offset() used for _anchor_regions intersection (safety offset
//   avoids Clipper false-negative on touching contours). If this offset is too large, anchor
//   regions grow slightly and false-positive anchoring can occur for very narrow bridges.
void BridgeDetector::initialize()
{
    // 5 degrees stepping
    this->resolution = PI / 36.0;
    // output angle not known
    this->angle = -1.;

    // [INTENT] Outset the bridge region by one extrusion-width. This margin allows detection
    //   of anchor overlap for bridges whose contour exactly touches a supporting wall
    //   (zero-width intersection would be missed by exact clipping).
    // Outset our bridge by an arbitrary amout; we'll use this outer margin for detecting anchors.
    Polygons grown = offset(this->expolygons, float(this->spacing));

    // [INTENT] Clip grown bridge outline against lower-slice contours to detect supporting edges.
    //   Only contours (not holes) of lower slices are used — holes in the support surface
    //   cannot anchor a bridge. _edges is a list of polyline segments that lie on solid material.
    //   These are used to derive bridge direction candidates (angles aligned with support geometry).
    // Detect possible anchoring edges of this bridging region.
    // Detect what edges lie on lower slices by turning bridge contour and holes
    // into polylines and then clipping them with each lower slice's contour.
    // Currently _edges are only used to set a candidate direction of the bridge (see bridge_direction_candidates()).
    Polygons contours;
    contours.reserve(this->lower_slices.size());
    for (const ExPolygon& expoly : this->lower_slices)
        contours.push_back(expoly.contour);
    this->_edges = intersection_pl(to_polylines(grown), contours);

#ifdef SLIC3R_DEBUG
    printf("  bridge has %zu support(s)\n", this->_edges.size());
#endif

    // detect anchors as intersection between our bridge expolygon and the lower slices
    // safety offset required to avoid Clipper from detecting empty intersection while Boost actually found some edges
    this->_anchor_regions = intersection_ex(grown, union_safety_offset(this->lower_slices));

    /*
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("bridge.svg",
            expolygons      => [ $self->expolygon ],
            red_expolygons  => $self->lower_slices,
            polylines       => $self->_edges,
        );
    }
    */
}

// [INTENT] detect_angle() — brute-force bridge direction optimization.
//   For each candidate angle θ:
//     1. Rotate coordinate space so θ-direction lines become vertical (Y-axis lines).
//     2. Generate a dense grid of horizontal test lines spanning the anchor_regions bbox,
//        spaced by this->spacing (one extrusion width apart) in the rotated frame.
//     3. Clip lines to clip_area (slightly grown bridge polygon) using intersection_ln().
//     4. For each clipped line: check if BOTH endpoints are inside _anchor_regions using
//        expolygons_contain(). Lines with both endpoints anchored are "bridged lines".
//     5. Sum total length of bridged lines → coverage score for this angle.
//   After all angles, sort by coverage (descending). Among angles within one extrusion-width
//   of the best coverage, prefer the one with the shorter max bridge span (max_length).
//   Writes result to this->angle.
//
// [HAZARD H337] O(N_angles × N_lines × N_anchor_polygons): expolygons_contain() does a
//   point-in-polygon test for every endpoint of every line. For complex anchor regions
//   this is O(N_vertices) per point. Total complexity scales as:
//   O(N_angles × (bbox_height / spacing) × N_anchor_vertices).
//   For a 200mm part at 0.4mm spacing: ~500 lines × 37 angles ≈ 18 500 point-in-polygon tests.
// [HAZARD H338] Lines generated via rotation — cos/sin applied to integer bbox coords.
//   round() used for coord_t conversion; accumulated rounding error at line endpoints
//   can cause false "not in anchor" classification for lines touching anchor edge.
//   clip_area (grown by spacing/2) is a mitigation but not a guarantee.
// [STATE] Mutates this->angle only. All intermediate state is local.
bool BridgeDetector::detect_angle(double bridge_direction_override)
{
    if (this->_edges.empty() || this->_anchor_regions.empty())
        // The bridging region is completely in the air, there are no anchors available at the layer below.
        return false;

    std::vector<BridgeDirection> candidates;
    if (bridge_direction_override == 0.) {
        std::vector<double> angles = bridge_direction_candidates();
        candidates.reserve(angles.size());
        for (size_t i = 0; i < angles.size(); ++i)
            candidates.emplace_back(BridgeDirection(angles[i]));
    } else
        candidates.emplace_back(BridgeDirection(bridge_direction_override));

    // [INTENT] clip_area: slightly grown bridge expolygon used to clip test lines.
    //   Growing by spacing/2 ensures line endpoints near the bridge edge can still pass
    //   the expolygons_contain check (avoids false-negative from numeric precision).
    /*  Outset the bridge expolygon by half the amount we used for detecting anchors;
        we'll use this one to clip our test lines and be sure that their endpoints
        are inside the anchors and not on their contours leading to false negatives. */
    Polygons clip_area = offset(this->expolygons, 0.5f * float(this->spacing));

    /*  we'll now try several directions using a rudimentary visibility check:
        bridge in several directions and then sum the length of lines having both
        endpoints within anchors */

    bool have_coverage = false;
    for (size_t i_angle = 0; i_angle < candidates.size(); ++i_angle) {
        const double angle = candidates[i_angle].angle;

        Lines lines;
        {
            // [INTENT] get_extents_rotated rotates anchor_regions bbox to axis-aligned frame
            //   at -angle, so that test lines at spacing intervals in Y cover the whole span.
            //   Lines are computed back in world coordinates using the rotation matrix [c,-s;s,c].
            // Get an oriented bounding box around _anchor_regions.
            BoundingBox bbox = get_extents_rotated(this->_anchor_regions, -angle);
            // Cover the region with line segments.
            lines.reserve((bbox.max(1) - bbox.min(1) + this->spacing) / this->spacing);
            double s = sin(angle);
            double c = cos(angle);
            // FIXME Vojtech: The lines shall be spaced half the line width from the edge, but then
            //  some of the test cases fail. Need to adjust the test cases then?
            //            for (coord_t y = bbox.min(1) + this->spacing / 2; y <= bbox.max(1); y += this->spacing)
            for (coord_t y = bbox.min(1); y <= bbox.max(1); y += this->spacing)
                lines.push_back(Line(Point((coord_t) round(c * bbox.min(0) - s * y), (coord_t) round(c * y + s * bbox.min(0))),
                                     Point((coord_t) round(c * bbox.max(0) - s * y), (coord_t) round(c * y + s * bbox.max(0)))));
        }

        double total_length = 0;
        double max_length   = 0;
        {
            Lines  clipped_lines     = intersection_ln(lines, clip_area);
            size_t archored_line_num = 0;
            for (size_t i = 0; i < clipped_lines.size(); ++i) {
                const Line& line = clipped_lines[i];
                // [INTENT] Both endpoints must be inside anchor regions for this line to count.
                //   A line that starts or ends outside anchor regions is not truly bridged.
                if (expolygons_contain(this->_anchor_regions, line.a) && expolygons_contain(this->_anchor_regions, line.b)) {
                    // This line could be anchored.
                    double len = line.length();
                    total_length += len;
                    max_length = std::max(max_length, len);
                    archored_line_num++;
                }
            }
            if (clipped_lines.size() > 0 && archored_line_num > 0) {
                candidates[i_angle].archored_percent = (double) archored_line_num / (double) clipped_lines.size();
            }
        }
        if (total_length == 0.)
            continue;

        have_coverage = true;
        // Sum length of bridged lines.
        candidates[i_angle].coverage = total_length;
        /*  The following produces more correct results in some cases and more broken in others.
            TODO: investigate, as it looks more reliable than line clipping. */
        // $directions_coverage{$angle} = sum(map $_->area, @{$self->coverage($angle)}) // 0;
        // max length of bridged lines
        candidates[i_angle].max_length = max_length;
    }

    // if no direction produced coverage, then there's no bridge direction
    if (!have_coverage)
        return false;

    // [INTENT] Sort descending by coverage. Then walk forward while within one spacing-width
    //   of the best: prefer the shorter max_length among near-equal-coverage candidates.
    //   This avoids picking an angle with marginally more coverage but much longer bridges.
    // sort directions by coverage - most coverage first
    std::sort(candidates.begin(), candidates.end());

    // if any other direction is within extrusion width of coverage, prefer it if shorter
    // TODO: There are two options here - within width of the angle with most coverage, or within width of the currently perferred?
    size_t i_best = 0;
    //    for (size_t i = 1; i < candidates.size() && abs(candidates[i_best].archored_percent - candidates[i].archored_percent) < EPSILON; ++ i)
    for (size_t i = 1; i < candidates.size() && candidates[i_best].coverage - candidates[i].coverage < this->spacing; ++i)
        if (candidates[i].max_length < candidates[i_best].max_length)
            i_best = i;

    this->angle = candidates[i_best].angle;
    // [INTENT] Bridge lines are direction-symmetric (180° flip is identical). Fold to [0, PI).
    if (this->angle >= PI)
        this->angle -= PI;

#ifdef SLIC3R_DEBUG
    printf("  Optimal infill angle is %d degrees\n", (int) Slic3r::Geometry::rad2deg(this->angle));
#endif

    return true;
}

// [INTENT] bridge_direction_candidates() — builds the set of angles to test.
//   Three sources of candidates are merged:
//   1. Uniform angular grid: 0, resolution, 2×resolution, ... π (default: every 5°, 37 values).
//   2. Angles of bridge contour edges: if a bridge region has a diagonal wall, bridging
//      parallel to it minimises the span. Polygon edge directions are used as candidates.
//   3. Angles of support edge polylines: captures the orientation of C-shaped support geometry.
//      Opening direction of a C-support is typically perpendicular to its longest edge.
//   After merge: de-duplicate within 1° tolerance using Geometry::directions_parallel().
//   The PI-0 wrap is handled by checking first vs. last candidate.
//
// [HAZARD H339] erase-in-loop with index adjustment: O(N²) for large angle sets.
//   Typically N is small (<200), so not a practical bottleneck, but non-idiomatic.
// [HAZARD H340] for loop: `int i = 0; i <= PI/this->resolution` — PI is irrational, so
//   PI/resolution can have fractional part. Loop terminates correctly due to <=,
//   but the last value may slightly exceed PI. Geometry::directions_parallel handles the wrap.
std::vector<double> BridgeDetector::bridge_direction_candidates() const
{
    // we test angles according to configured resolution
    std::vector<double> angles;
    for (int i = 0; i <= PI / this->resolution; ++i)
        angles.push_back(i * this->resolution);

    // we also test angles of each bridge contour
    {
        Lines lines = to_lines(this->expolygons);
        for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line)
            angles.push_back(line->direction());
    }

    /*  we also test angles of each open supporting edge
        (this finds the optimal angle for C-shaped supports) */
    for (const Polyline& edge : this->_edges)
        if (edge.first_point() != edge.last_point())
            angles.push_back(Line(edge.first_point(), edge.last_point()).direction());

    // remove duplicates
    double min_resolution = PI / 180.0; // 1 degree
    std::sort(angles.begin(), angles.end());
    for (size_t i = 1; i < angles.size(); ++i) {
        if (Slic3r::Geometry::directions_parallel(angles[i], angles[i - 1], min_resolution)) {
            angles.erase(angles.begin() + i);
            --i;
        }
    }
    /*  compare first value with last one and remove the greatest one (PI)
        in case they are parallel (PI, 0) */
    if (Slic3r::Geometry::directions_parallel(angles.front(), angles.back(), min_resolution))
        angles.pop_back();

    return angles;
}

/*
static void get_trapezoids(const ExPolygon &expoly, Polygons* polygons) const
{
    ExPolygons expp;
    expp.push_back(expoly);
    boost::polygon::get_trapezoids(*polygons, expp);
}

void ExPolygon::get_trapezoids(ExPolygon clone, Polygons* polygons, double angle) const
{
    clone.rotate(PI/2 - angle, Point(0,0));
    clone.get_trapezoids(polygons);
    for (Polygons::iterator polygon = polygons->begin(); polygon != polygons->end(); ++polygon)
        polygon->rotate(-(PI/2 - angle), Point(0,0));
}
*/

// [INTENT] get_trapezoids2() — decomposes an ExPolygon into vertical trapezoid strips
//   at every unique X coordinate of the polygon vertices. This is a simplified form of the
//   classic sweep-line trapezoidation algorithm. Each strip from x[i] to x[i+1] is
//   intersected with the polygon to produce one or more trapezoid slices.
// [HAZARD H341] May return more trapezoids than geometrically minimal: if multiple parts
//   of the polygon share the same X coordinate, the strip is split at that X even if
//   the result is a single connected trapezoid. This is acceptable since coverage() unions
//   the result anyway.
// [HAZARD H342] Operates on bounding-box height for strip rectangles, which can include
//   polygon holes. Holes are handled by the intersection() clip — strips inside holes are
//   removed by Clipper.
// This algorithm may return more trapezoids than necessary
// (i.e. it may break a single trapezoid in several because
// other parts of the object have x coordinates in the middle)
static void get_trapezoids2(const ExPolygon& expoly, Polygons* polygons)
{
    Polygons src_polygons = to_polygons(expoly);
    // get all points of this ExPolygon
    const Points pp = to_points(src_polygons);

    // build our bounding box
    BoundingBox bb(pp);

    // get all x coordinates
    std::vector<coord_t> xx;
    xx.reserve(pp.size());
    for (Points::const_iterator p = pp.begin(); p != pp.end(); ++p)
        xx.push_back(p->x());
    std::sort(xx.begin(), xx.end());

    // find trapezoids by looping from first to next-to-last coordinate
    Polygons rectangle;
    rectangle.emplace_back(Polygon());
    for (std::vector<coord_t>::const_iterator x = xx.begin(); x != xx.end() - 1; ++x) {
        coord_t next_x = *(x + 1);
        if (*x != next_x) {
            // intersect with rectangle
            // append results to return value
            rectangle.front() = {{*x, bb.min.y()}, {next_x, bb.min.y()}, {next_x, bb.max.y()}, {*x, bb.max.y()}};
            polygons_append(*polygons, intersection(rectangle, src_polygons));
        }
    }
}

// [INTENT] Angle-rotated wrapper for get_trapezoids2().
//   Rotates expoly by (PI/2 - angle) so strips become aligned with the bridge direction,
//   decomposes, then rotates results back. Allows coverage() to work in bridge-direction space.
static void get_trapezoids2(const ExPolygon& expoly, Polygons* polygons, double angle)
{
    ExPolygon clone = expoly;
    clone.rotate(PI / 2 - angle, Point(0, 0));
    get_trapezoids2(clone, polygons);
    for (Polygon& polygon : *polygons)
        polygon.rotate(-(PI / 2 - angle), Point(0, 0));
}

// [INTENT] get_trapezoids3_half() — precision variant of trapezoidation used by coverage(precise=true).
//   Unlike get_trapezoids2, strips are spaced at uniform 'spacing' intervals (not at polygon vertex X coords),
//   and each strip is inset by spacing/4 on each side to produce slightly narrower slices.
//   The inset ensures that slices that span two anchor regions both have their midpoint
//   closer to the anchor, reducing false-positive bridge coverage near the edges.
//   This is called "half" because it uses spacing/4 inset (half of the spacing/2 margin used elsewhere).
// [HAZARD H343] spacing/4 computed as integer: (coord_t)spacing / 4. If spacing < 4 (scaled units),
//   this rounds to 0 and the inset is lost. Impractical in real use (spacing << 1 scaled unit would
//   be sub-micron), but worth noting for port.
// [HAZARD H344] xx vector built without sorting (commented-out sort), assuming min_x→max_x scan is
//   already ordered. This is correct because the loop increments x by spacing, but the comment
//   implies it was changed from a vertex-based approach at some point.
void get_trapezoids3_half(const ExPolygon& expoly, Polygons* polygons, float spacing)
{
    // get all points of this ExPolygon
    Points pp = to_points(expoly);

    if (pp.empty())
        return;

    // build our bounding box
    BoundingBox bb(pp);

    // get all x coordinates
    coord_t              min_x = pp[0].x(), max_x = pp[0].x();
    std::vector<coord_t> xx;
    for (Points::const_iterator p = pp.begin(); p != pp.end(); ++p) {
        if (min_x > p->x())
            min_x = p->x();
        if (max_x < p->x())
            max_x = p->x();
    }
    for (coord_t x = min_x; x < max_x - (coord_t) (spacing / 2); x += (coord_t) spacing) {
        xx.push_back(x);
    }
    xx.push_back(max_x);
    // std::sort(xx.begin(), xx.end());

    // find trapezoids by looping from first to next-to-last coordinate
    for (std::vector<coord_t>::const_iterator x = xx.begin(); x != xx.end() - 1; ++x) {
        coord_t next_x = *(x + 1);
        if (*x == next_x)
            continue;

        // build rectangle
        Polygon poly;
        poly.points.resize(4);
        poly[0].x() = *x + (coord_t) spacing / 4;
        poly[0].y() = bb.min(1);
        poly[1].x() = next_x - (coord_t) spacing / 4;
        poly[1].y() = bb.min(1);
        poly[2].x() = next_x - (coord_t) spacing / 4;
        poly[2].y() = bb.max(1);
        poly[3].x() = *x + (coord_t) spacing / 4;
        poly[3].y() = bb.max(1);

        // intersect with this expolygon
        // append results to return value
        polygons_append(*polygons, intersection(Polygons{poly}, to_polygons(expoly)));
    }
}
}
}

static void get_trapezoids2(const ExPolygon& expoly, Polygons* polygons, double angle)
{
    ExPolygon clone = expoly;
    clone.rotate(PI / 2 - angle, Point(0, 0));
    get_trapezoids2(clone, polygons);
    for (Polygon& polygon : *polygons)
        polygon.rotate(-(PI / 2 - angle), Point(0, 0));
}

void get_trapezoids3_half(const ExPolygon& expoly, Polygons* polygons, float spacing)
{
    // get all points of this ExPolygon
    Points pp = to_points(expoly);

    if (pp.empty())
        return;

    // build our bounding box
    BoundingBox bb(pp);

    // get all x coordinates
    coord_t              min_x = pp[0].x(), max_x = pp[0].x();
    std::vector<coord_t> xx;
    for (Points::const_iterator p = pp.begin(); p != pp.end(); ++p) {
        if (min_x > p->x())
            min_x = p->x();
        if (max_x < p->x())
            max_x = p->x();
    }
    for (coord_t x = min_x; x < max_x - (coord_t) (spacing / 2); x += (coord_t) spacing) {
        xx.push_back(x);
    }
    xx.push_back(max_x);
    // std::sort(xx.begin(), xx.end());

    // find trapezoids by looping from first to next-to-last coordinate
    for (std::vector<coord_t>::const_iterator x = xx.begin(); x != xx.end() - 1; ++x) {
        coord_t next_x = *(x + 1);
        if (*x == next_x)
            continue;

        // build rectangle
        Polygon poly;
        poly.points.resize(4);
        poly[0].x() = *x + (coord_t) spacing / 4;
        poly[0].y() = bb.min(1);
        poly[1].x() = next_x - (coord_t) spacing / 4;
        poly[1].y() = bb.min(1);
        poly[2].x() = next_x - (coord_t) spacing / 4;
        poly[2].y() = bb.max(1);
        poly[3].x() = *x + (coord_t) spacing / 4;
        poly[3].y() = bb.max(1);

        // intersect with this expolygon
        // append results to return value
        polygons_append(*polygons, intersection(Polygons{poly}, to_polygons(expoly)));
    }
}

// [INTENT] coverage() — returns the polygon regions that are successfully bridged at 'angle'.
//   Uses trapezoidation to identify contiguous strips that span two distinct anchor regions.
//   Two modes (controlled by 'precise' parameter):
//   - imprecise (get_trapezoids2): strips at vertex X coords, validated by checking if
//     any strip edge intersects two anchors with >= spacing length overlap.
//   - precise (get_trapezoids3_half): strips at uniform spacing intervals with inset,
//     validated by checking if the strip polygon intersects >= 2 distinct anchor polygons.
//     When >= 2 anchors, trims the strip y-extent to the anchor bounding-box range
//     (so covered area only extends between anchor centers, not beyond them).
//
// Algorithm:
//   1. Rotate anchor regions and bridge expolygon by (PI/2 - angle) → work in vertical strip space.
//   2. For each sub-expolygon: offset by spacing/2, decompose into trapezoids.
//   3. For each trapezoid: test if it spans two anchor regions (precise or imprecise check).
//   4. Union all "covered" trapezoids (rotation creates tiny overlaps).
//   5. Rotate covered polygons back to world coordinates.
//
// [HAZARD H345] The loop `for (ExPolygon expolygon : this->expolygons)` copies each expolygon
//   for in-place rotation. For large models with many bridge regions this is O(N_points) per call.
// [HAZARD H346] The imprecise mode checks `supported_line.length() >= this->spacing` — this
//   threshold is heuristic. Very narrow anchors slightly narrower than one extrusion width
//   would be falsely excluded. The precise mode does not have this threshold.
// [HAZARD H347] covered = union_(covered) before rotate-back: necessary because rotation
//   introduces fractional pixel gaps between adjacent trapezoids. Without union, the result
//   has micro-slivers that can cause downstream Clipper failures.
Polygons BridgeDetector::coverage(double angle, bool precise) const
{
    if (angle == -1)
        angle = this->angle;

    Polygons covered;

    if (angle != -1) {
        // Get anchors, convert them to Polygons and rotate them.
        Polygons anchors = to_polygons(this->_anchor_regions);
        polygons_rotate(anchors, PI / 2.0 - angle);
        // same for region which do not need bridging
        // Polygons supported_area = diff(this->lower_slices.expolygons, this->_anchor_regions, true);
        // polygons_rotate(anchors, PI / 2.0 - angle);

        for (ExPolygon expolygon : this->expolygons) {
            // Clone our expolygon and rotate it so that we work with vertical lines.
            expolygon.rotate(PI / 2.0 - angle);
            // Outset the bridge expolygon by half the amount we used for detecting anchors;
            // we'll use this one to generate our trapezoids and be sure that their vertices
            // are inside the anchors and not on their contours leading to false negatives.
            for (ExPolygon& expoly : offset_ex(expolygon, 0.5f * float(this->spacing))) {
                // Compute trapezoids according to a vertical orientation
                Polygons trapezoids;
                if (!precise)
                    get_trapezoids2(expoly, &trapezoids, PI / 2);
                else
                    get_trapezoids3_half(expoly, &trapezoids, float(this->spacing));
                for (Polygon& trapezoid : trapezoids) {
                    size_t n_supported = 0;
                    if (!precise) {
                        // not nice, we need a more robust non-numeric check
                        // imporvment 1: take into account when we go in the supported area.
                        for (const Line& supported_line : intersection_ln(trapezoid.lines(), anchors))
                            if (supported_line.length() >= this->spacing)
                                ++n_supported;
                    } else {
                        Polygons intersects = intersection(Polygons{trapezoid}, anchors);
                        n_supported         = intersects.size();

                        if (n_supported >= 2) {
                            // [INTENT] Trim the trapezoid's Y extent to exactly span between
                            //   the two innermost anchor centers. This prevents "covered" from
                            //   extending beyond the actual anchor material.
                            // trim it to not allow to go outside of the intersections
                            BoundingBox center_bound = intersects[0].bounding_box();
                            coord_t     min_y = center_bound.center()(1), max_y = center_bound.center()(1);
                            for (Polygon& poly_bound : intersects) {
                                center_bound = poly_bound.bounding_box();
                                if (min_y > center_bound.center()(1))
                                    min_y = center_bound.center()(1);
                                if (max_y < center_bound.center()(1))
                                    max_y = center_bound.center()(1);
                            }
                            coord_t min_x = trapezoid[0](0), max_x = trapezoid[0](0);
                            for (Point& p : trapezoid.points) {
                                if (min_x > p(0))
                                    min_x = p(0);
                                if (max_x < p(0))
                                    max_x = p(0);
                            }
                            // add what get_trapezoids3 has removed (+EPSILON)
                            min_x -= (this->spacing / 4 + 1);
                            max_x += (this->spacing / 4 + 1);
                            coord_t mid_x = (min_x + max_x) / 2;
                            for (Point& p : trapezoid.points) {
                                if (p(1) < min_y)
                                    p(1) = min_y;
                                if (p(1) > max_y)
                                    p(1) = max_y;
                                if (p(0) > min_x && p(0) < mid_x)
                                    p(0) = min_x;
                                if (p(0) < max_x && p(0) > mid_x)
                                    p(0) = max_x;
                            }
                        }
                    }

                    if (n_supported >= 2) {
                        // add it
                        covered.push_back(std::move(trapezoid));
                    }
                }
            }
        }

        // Unite the trapezoids before rotation, as the rotation creates tiny gaps and intersections between the trapezoids
        // instead of exact overlaps.
        covered = union_(covered);
        // Intersect trapezoids with actual bridge area to remove extra margins and append it to result.
        polygons_rotate(covered, -(PI / 2.0 - angle));
        // covered = intersection(this->expolygons, covered);
#if 0
        {
            my @lines = map @{$_->lines}, @$trapezoids;
            $_->rotate(-(PI/2 - $angle), [0,0]) for @lines;
            
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output(
                "coverage_" . rad2deg($angle) . ".svg",
                expolygons          => [$self->expolygon],
                green_expolygons    => $self->_anchor_regions,
                red_expolygons      => $coverage,
                lines               => \@lines,
            );
        }
#endif
    }
    return covered;
}

// [INTENT] unsupported_edges() — returns the bridge perimeter segments that overhang
//   lower slices AND are NOT parallel to the bridging direction.
//   Non-parallel overhanging edges are the ones that cannot be self-supported by
//   the bridge direction: they are endpoints/sides of the bridge that land in air.
//   The caller (PrintObject, SupportMaterial) uses these to decide if additional
//   support structures are needed.
//
//   Algorithm:
//   1. Grow lower_slices by one spacing (to handle touching contours).
//   2. For each bridge expolygon: find edges not covered by grown lower slices
//      (diff_pl removes the supported portion).
//   3. Filter: keep only lines that are NOT parallel to the bridge angle
//      (parallel lines are bridged across; only non-parallel ends need support).
//
// [HAZARD H348] diff_pl can produce short spurious polylines at clipper junctions.
//   Downstream callers must tolerate zero-length or near-zero polylines.
// [HAZARD H349] Angle-parallel filter uses Geometry::directions_parallel() which has
//   its own tolerance. If the bridge angle tolerance is too loose, slightly-non-parallel
//   edges may be incorrectly classified as supported, missing real overhang.
/*  This method returns the bridge edges (as polylines) that are not supported
    but would allow the entire bridge area to be bridged with detected angle
    if supported too */
void BridgeDetector::unsupported_edges(double angle, Polylines* unsupported) const
{
    if (angle == -1)
        angle = this->angle;
    if (angle == -1)
        return;

    Polygons grown_lower = offset(this->lower_slices, float(this->spacing));

    for (ExPolygons::const_iterator it_expoly = this->expolygons.begin(); it_expoly != this->expolygons.end(); ++it_expoly) {
        // get unsupported bridge edges (both contour and holes)
        Lines unsupported_lines = to_lines(diff_pl(to_polylines(*it_expoly), grown_lower));
        /*  Split into individual segments and filter out edges parallel to the bridging angle
            TODO: angle tolerance should probably be based on segment length and flow width,
            so that we build supports whenever there's a chance that at least one or two bridge
            extrusions would be anchored within such length (i.e. a slightly non-parallel bridging
            direction might still benefit from anchors if long enough)
            double angle_tolerance = PI / 180.0 * 5.0; */
        for (const Line& line : unsupported_lines)
            if (!Slic3r::Geometry::directions_parallel(line.direction(), angle)) {
                unsupported->emplace_back(Polyline());
                unsupported->back().points.emplace_back(line.a);
                unsupported->back().points.emplace_back(line.b);
            }
    }

    /*
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(
            "unsupported_" . rad2deg($angle) . ".svg",
            expolygons          => [$self->expolygon],
            green_expolygons    => $self->_anchor_regions,
            red_expolygons      => union_ex($grown_lower),
            no_arrows           => 1,
            polylines           => \@bridge_edges,
            red_polylines       => $unsupported,
        );
    }
    */
}

// [INTENT] Convenience overload — allocates and returns Polylines by value.
//   Used by callers that don't pre-allocate the output vector.
Polylines BridgeDetector::unsupported_edges(double angle) const
{
    Polylines pp;
    this->unsupported_edges(angle, &pp);
    return pp;
}

} // namespace Slic3r
