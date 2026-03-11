#ifndef MINAREABOUNDINGBOX_HPP
#define MINAREABOUNDINGBOX_HPP

#include "libslic3r/Point.hpp"

namespace Slic3r {

class Polygon;
class ExPolygon;

// [INTENT] Cleanup pass used before rotating-calipers processing; collinear chains
//          do not change extrema but add noise and unnecessary edge tests.
void remove_collinear_points(Polygon& p);
void remove_collinear_points(ExPolygon& p);

/// A class that holds a rotated bounding box. If instantiated with a polygon
/// type it will hold the minimum area bounding box for the given polygon.
/// If the input polygon is convex, the complexity is linear to the number of 
/// points. Otherwise a convex hull of O(n*log(n)) has to be performed.
class MinAreaBoundigBox {
    // [STATE] Axis + projected extents define the oriented frame lazily exposed via
    //         width/height/area without reconstructing corner geometry.
    Point m_axis;    
    long double m_bottom = 0.0l, m_right = 0.0l;
public:
    
    // Polygons can be convex or simple (convex or concave with possible holes)
    enum PolygonLevel {
        pcConvex, pcSimple
    };
   
    // Constructors with various types of geometry data used in Slic3r.
    // If the convexity is known apriory, pcConvex can be used to skip
    // convex hull calculation.
    // [INTENT] Overloads make call sites independent of polygon container shape while
    //          funneling all variants into the same minimal-area oriented rectangle logic.
    // [HAZARD] `pcConvex` is a trust contract; passing non-convex data under this mode
    //          can silently produce invalid bounds because hull reduction is skipped.
    explicit MinAreaBoundigBox(const Polygon&, PolygonLevel = pcSimple);
    explicit MinAreaBoundigBox(const ExPolygon&, PolygonLevel = pcSimple);
    explicit MinAreaBoundigBox(const Points&, PolygonLevel = pcSimple);
    
    // Returns the angle in radians needed for the box to be aligned with the 
    // X axis. Rotate the polygon by this angle and it will be aligned.
    double angle_to_X()  const;
    
    // The box width
    long double width()  const;
    
    // The box height
    long double height() const;
    
    // The box area
    long double area()   const;
    
    // The axis of the rotated box. If the angle_to_X is not sufficiently 
    // precise, use this unnormalized direction vector.
    const Point& axis()  const { return m_axis; }
};

}

#endif // MINAREABOUNDINGBOX_HPP
