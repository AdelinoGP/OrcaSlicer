// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] linearAlg2D.hpp collects the tiny orientation/turn-angle predicates that Arachne uses
// in hot geometric loops. These helpers keep polygon-side tests and local corner classification in
// one place so WallToolPaths and the straight-skeleton code can stay in scaled-integer space until
// the final angle conversion step.
// [COUPLING] Depends directly on `Point.hpp` for `Point`, `Vec2i64`, and `cross2()`. Callers such
// as `WallToolPaths.cpp` assume the same integer coordinate conventions as the rest of libslic3r.
// [HAZARD] `pointIsLeftOfLine()` and the determinant branch of `getAngleLeft()` both rely on 64-bit
// widened arithmetic to avoid overflow from coord_t deltas. A port that narrows intermediates back
// to 32-bit will silently flip orientation and angle decisions on large models.

#ifndef UTILS_LINEAR_ALG_2D_H
#define UTILS_LINEAR_ALG_2D_H

#include "../../Point.hpp"

namespace Slic3r::Arachne::LinearAlg2D {

/*!
 * Returns the determinant of the 2D matrix defined by the the vectors ab and ap as rows.
 *
 * The returned value is zero for \p p lying (approximately) on the line going through \p a and \p b
 * The value is positive for values lying to the left and negative for values lying to the right when looking from \p a to \p b.
 *
 * \param p the point to check
 * \param a the from point of the line
 * \param b the to point of the line
 * \return a positive value when \p p lies to the left of the line from \p a to \p b
 */
static inline int64_t pointIsLeftOfLine(const Point& p, const Point& a, const Point& b)
{
    // [INTENT] Signed 2D cross product of AB and AP. Arachne uses the sign only, but returning the
    // full magnitude lets callers reuse it as a cheap distance-proportional orientation score.
    return int64_t(b.x() - a.x()) * int64_t(p.y() - a.y()) - int64_t(b.y() - a.y()) * int64_t(p.x() - a.x());
}

/*!
 * Compute the angle between two consecutive line segments.
 *
 * The angle is computed from the left side of b when looking from a.
 *
 *   c
 *    \                     .
 *     \ b
 * angle|
 *      |
 *      a
 *
 * \param a start of first line segment
 * \param b end of first segment and start of second line segment
 * \param c end of second line segment
 * \return the angle in radians between 0 and 2 * pi of the corner in \p b
 */
static inline float getAngleLeft(const Point& a, const Point& b, const Point& c)
{
    // [INTENT] Build vectors BA and BC around the corner at `b` so the result describes how much of
    // the polygon interior lies on the left when traversing a->b->c. WallToolPaths uses this to rank
    // candidate thin-wall junctions and decide where extra wall handling is needed.
    const Vec2i64 ba   = (a - b).cast<int64_t>();
    const Vec2i64 bc   = (c - b).cast<int64_t>();
    const int64_t dott = ba.dot(bc);     // dot product
    const int64_t det  = cross2(ba, bc); // determinant
    if (det == 0) {
        // [HAZARD] Collinear corners are split into "same direction" vs "reverse direction" by sign
        // checks on one component at a time. Zero-length segments would make this heuristic bogus, so
        // callers must avoid feeding duplicated consecutive points into this helper.
        if ((ba.x() != 0 && (ba.x() > 0) == (bc.x() > 0)) || (ba.x() == 0 && (ba.y() > 0) == (bc.y() > 0)))
            return 0; // pointy bit
        else
            return float(M_PI); // straight bit
    }
    // [HAZARD] The final angle leaves integer space and uses floating-point atan2(). Boundary cases
    // near 0 or 2*pi therefore inherit platform math-library differences, even though the inputs are
    // exact integers.
    const float angle = -atan2(double(det), double(dott)); // from -pi to pi
    if (angle >= 0)
        return angle;
    else
        return M_PI * 2 + angle;
}

} // namespace Slic3r::Arachne::LinearAlg2D
#endif // UTILS_LINEAR_ALG_2D_H
