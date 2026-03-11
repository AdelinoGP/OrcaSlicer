#ifndef slic3r_ExPolygonSerialize_hpp_
#define slic3r_ExPolygonSerialize_hpp_

#include "ExPolygon.hpp"
#include "Point.hpp" // Cereal serialization of Point
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

/// <summary>
/// External Cereal serialization of ExPolygons
/// </summary>

// Serialization through the Cereal library
#include <cereal/access.hpp>
namespace cereal {

template<class Archive> void serialize(Archive& archive, Slic3r::Polygon& polygon)
{
    // [INTENT] Persist raw point rings so undo/redo and snapshot systems can reconstruct exact geometry.
    archive(polygon.points);
}

template<class Archive> void serialize(Archive& archive, Slic3r::ExPolygon& expoly)
{
    // [COUPLING] Serialized field order must stay aligned with Polygon/ExPolygon layout across versions.
    // [HAZARD] Reordering members or changing contour/holes semantics without migration will break old snapshots.
    archive(expoly.contour, expoly.holes);
}

} // namespace cereal
#endif // slic3r_ExPolygonSerialize_hpp_
