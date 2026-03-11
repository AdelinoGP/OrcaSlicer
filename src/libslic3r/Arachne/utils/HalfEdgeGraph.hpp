// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] HalfEdgeGraph is the storage backbone for Arachne's straight-skeleton-style
// topology. It owns every node and directed half-edge that later algorithm phases mutate
// in place while classifying central regions, collapsing tiny quads, and inserting
// transition ribs.
// [MEMORY] Ownership is centralized in two std::list containers so raw pointers embedded in
// HalfEdge / HalfEdgeNode remain stable across insertions. This is critical because the graph
// stores a dense web of `twin`, `next`, `prev`, `from`, `to`, and `incident_edge` pointers.
// [COUPLING] SkeletalTrapezoidationGraph inherits this template and depends on list-backed
// pointer stability when it edits topology during collapseSmallEdges() and insertNode().
// [HAZARD] Removing `std::list` in a port is not a mechanical substitution: any replacement
// must guarantee address stability or convert every raw edge/node link into an explicit handle.

#ifndef UTILS_HALF_EDGE_GRAPH_H
#define UTILS_HALF_EDGE_GRAPH_H

#include <list>
#include <cassert>

#include "HalfEdge.hpp"
#include "HalfEdgeNode.hpp"

namespace Slic3r::Arachne {
template<class node_data_t, class edge_data_t, class derived_node_t, class derived_edge_t> // types of data contained in nodes and edges
class HalfEdgeGraph
{
public:
    using edge_t = derived_edge_t;
    using node_t = derived_node_t;
    using Edges  = std::list<edge_t>;
    using Nodes  = std::list<node_t>;
    // [STATE] Global edge arena for one half-edge graph instance. Algorithms append here and then
    // wire cross-links through raw pointers; erasure is intentionally rare because it would invalidate
    // higher-level traversal assumptions.
    Edges edges;
    // [STATE] Global node arena paired with `edges`. Nodes own no adjacency container of their own;
    // traversal starts from node.incident_edge and walks edge links instead.
    Nodes nodes;
};

} // namespace Slic3r::Arachne
#endif // UTILS_HALF_EDGE_GRAPH_H
