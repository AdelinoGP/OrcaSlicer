#ifndef slic3r_ExPolygonCollection_hpp_
#define slic3r_ExPolygonCollection_hpp_

#include "libslic3r.h"
#include "ExPolygon.hpp"
#include "Line.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class ExPolygonCollection;
typedef std::vector<ExPolygonCollection> ExPolygonCollections;

class ExPolygonCollection
{
public:
    // [MEMORY] Container owns ExPolygon values directly; no external lifetime or pointer ownership contract.
    ExPolygons expolygons;

    ExPolygonCollection() {}
    explicit ExPolygonCollection(const ExPolygon& expolygon);
    explicit ExPolygonCollection(const ExPolygons& expolygons) : expolygons(expolygons) {}
    // [INTENT] Explicit conversions force call sites to acknowledge potentially lossy shape projection
    // (e.g., dropping hole grouping when flattening to Polygons/Points).
    explicit               operator Points() const;
    explicit               operator Polygons() const;
    explicit               operator ExPolygons&();
    void                   scale(double factor);
    void                   translate(double x, double y);
    void                   rotate(double angle, const Point& center);
    template<class T> bool contains(const T& item) const;
    bool                   contains_b(const Point& point) const;
    void                   simplify(double tolerance);
    // [COUPLING] convex_hull/lines/contours bridge this aggregate to downstream geometry algorithms
    // that operate on primitive polygon and segment containers.
    Polygon  convex_hull() const;
    Lines    lines() const;
    Polygons contours() const;
    void     append(const ExPolygons& expolygons);
};

extern BoundingBox get_extents(const ExPolygonCollection& expolygon);

} // namespace Slic3r

#endif
