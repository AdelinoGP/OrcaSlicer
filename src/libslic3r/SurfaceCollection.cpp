// [INTENT] SurfaceCollection.cpp — implementation of surface grouping, filtering, and
//          SVG debug export.  All mutation is single-threaded (no locks needed).
//
// [COUPLING] group() returns raw pointers into this->surfaces; callers must not modify
//            surfaces after calling group() until the SurfacesPtr groups are discarded.
#include "SurfaceCollection.hpp"
#include "BoundingBox.hpp"
#include "SVG.hpp"

#include <map>

namespace Slic3r {

// [INTENT] simplify() reduces vertex count of all expolygons in-place using Douglas-Peucker
//          with the given tolerance (in mm, since expolygon coords are scaled).
//          May split one expolygon into multiple smaller ones if simplification opens holes.
void SurfaceCollection::simplify(double tolerance)
{
    Surfaces ss;
    for (Surfaces::const_iterator it_s = this->surfaces.begin(); it_s != this->surfaces.end(); ++it_s) {
        ExPolygons expp;
        it_s->expolygon.simplify(tolerance, &expp);
        for (ExPolygons::const_iterator it_e = expp.begin(); it_e != expp.end(); ++it_e) {
            Surface s   = *it_s;
            s.expolygon = *it_e;
            ss.push_back(s);
        }
    }
    this->surfaces = ss;
}

/* group surfaces by common properties */
// [INTENT] Group surfaces by identical (type, thickness, thickness_layers, bridge_angle).
//          O(n^2) scan — acceptable because n is small (typically < 20 surfaces per layer).
//          Returns groups as raw pointers into this->surfaces; invalidated by any mutation.
void SurfaceCollection::group(std::vector<SurfacesPtr>* retval)
{
    for (Surfaces::iterator it = this->surfaces.begin(); it != this->surfaces.end(); ++it) {
        // find a group with the same properties
        SurfacesPtr* group = NULL;
        for (std::vector<SurfacesPtr>::iterator git = retval->begin(); git != retval->end(); ++git)
            if (!git->empty() && surfaces_could_merge(*git->front(), *it)) {
                group = &*git;
                break;
            }
        // if no group with these properties exists, add one
        if (group == NULL) {
            retval->resize(retval->size() + 1);
            group = &retval->back();
        }
        // append surface to group
        group->push_back(&*it);
    }
}

// [INTENT] filter_by_type — return raw const pointers to surfaces of the requested type.
//          Callers must not mutate the vector until done with the returned pointers.
SurfacesPtr SurfaceCollection::filter_by_type(const SurfaceType type) const
{
    SurfacesPtr ss;
    for (const Surface& surface : this->surfaces)
        if (surface.surface_type == type)
            ss.push_back(&surface);
    return ss;
}

// [INTENT] Multi-type variant: uses O(k) linear scan of the initializer_list per surface.
//          Acceptable when k (number of requested types) is small (typically <= 3).
SurfacesPtr SurfaceCollection::filter_by_types(std::initializer_list<SurfaceType> types) const
{
    SurfacesPtr ss;
    for (const Surface& surface : this->surfaces)
        if (std::find(types.begin(), types.end(), surface.surface_type) != types.end())
            ss.push_back(&surface);
    return ss;
}

// [INTENT] Append flat Polygons (contour + holes) for surfaces of the requested type
//          into an existing Polygons vector — avoids an intermediate allocation.
void SurfaceCollection::filter_by_type(SurfaceType type, Polygons* polygons) const
{
    for (const Surface& surface : this->surfaces)
        if (surface.surface_type == type)
            polygons_append(*polygons, to_polygons(surface.expolygon));
}

// [INTENT] keep_type — stable in-place partition retaining only surfaces of 'type'.
//          O(n) time, O(1) extra space.  Uses swap+j pattern to avoid allocations.
void SurfaceCollection::keep_type(const SurfaceType type)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i].surface_type == type) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void SurfaceCollection::keep_types(std::initializer_list<SurfaceType> types)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++i)
        if (std::find(types.begin(), types.end(), surfaces[i].surface_type) != types.end()) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++j;
        }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

// [INTENT] remove_type — inverse of keep_type: discard surfaces of the given type.
void SurfaceCollection::remove_type(const SurfaceType type)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i].surface_type != type) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

// [INTENT] remove_type with extraction — removed surfaces' expolygons are moved into
//          *polygons so the caller can use the geometry without re-allocating.
void SurfaceCollection::remove_type(const SurfaceType type, ExPolygons* polygons)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if (Surface& surface = surfaces[i]; surface.surface_type == type) {
            polygons->emplace_back(std::move(surface.expolygon));
        } else {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void SurfaceCollection::remove_types(std::initializer_list<SurfaceType> types)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++i)
        if (std::find(types.begin(), types.end(), surfaces[i].surface_type) == types.end()) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++j;
        }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

// [INTENT] Debug-only SVG export: draws each surface polygon in its type colour with
//          optional index labels.  Appends a legend below the bounding box.
void SurfaceCollection::export_to_svg(const char* path, bool show_labels)
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->surfaces.begin(); surface != this->surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG         svg(path, bbox);
    const float transparency = 0.5f;
    for (Surfaces::const_iterator surface = this->surfaces.begin(); surface != this->surfaces.end(); ++surface) {
        svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
        if (show_labels) {
            int  idx = int(surface - this->surfaces.begin());
            char label[64];
            sprintf(label, "%d", idx);
            svg.draw_text(surface->expolygon.contour.points.front(), label, "black");
        }
    }
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

} // namespace Slic3r
