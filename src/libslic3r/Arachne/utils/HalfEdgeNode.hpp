// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] HalfEdgeNode is the vertex record paired with HalfEdge. In Arachne it represents a
// Voronoi/skeleton junction plus the geometric point where several half-edge chains meet.
// [STATE] Adjacency is implicit: the node stores only one `incident_edge`, and callers recover the
// full fan by repeatedly applying `edge = edge->twin->next` until they return to the starting edge.
// [MEMORY] Like HalfEdge, this type is non-owning. The pointed-to incident edge lives in the graph's
// list arena and must outlive every traversal rooted at this node.
// [COUPLING] STHalfEdgeNode layers distance-to-boundary, bead-propagation, and transition metadata on
// top of this template; the generic node shape here is intentionally tiny so graph edits stay cheap.
// [HAZARD] `incident_edge` is a cached entry point, not a validated adjacency list. If rewiring code
// forgets to retarget it after collapsing or splitting edges, whole regions become unreachable.

#ifndef UTILS_HALF_EDGE_NODE_H
#define UTILS_HALF_EDGE_NODE_H

#include <list>

#include "../../Point.hpp"

namespace Slic3r::Arachne {

template<typename node_data_t, typename edge_data_t, typename derived_node_t, typename derived_edge_t> class HalfEdge;

template<typename node_data_t, typename edge_data_t, typename derived_node_t, typename derived_edge_t> class HalfEdgeNode
{
    using edge_t = derived_edge_t;
    using node_t = derived_node_t;

public:
    // [STATE] Per-node payload supplied by the derived graph layer. In Arachne this carries the local
    // radius (distance_to_boundary), transition ratio, and propagated beading state.
    node_data_t data;
    // [STATE] Integer-coordinate location of the skeleton junction in OrcaSlicer's scaled space.
    Point p;
    // [STATE] One outgoing edge chosen as the canonical start of fan traversal around this node.
    // [HAZARD] Null is only valid during construction or after severe graph degeneration.
    edge_t* incident_edge = nullptr;
    // [INTENT] Copy payload and point into the graph-owned node arena; adjacency is connected later.
    HalfEdgeNode(node_data_t data, Point p) : data(data), p(p) {}

    bool operator==(const node_t& other) { return this == &other; }
};

} // namespace Slic3r::Arachne
#endif // UTILS_HALF_EDGE_NODE_H
