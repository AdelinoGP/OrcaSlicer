// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] HalfEdge is the minimal directed-edge record used by Arachne's medial-axis graph.
// Each undirected skeleton segment is represented as a twin pair, while `next`/`prev` connect
// successive edges along one quad chain so higher-level code can walk faces and node fans.
// [STATE] All topology is stored as mutable raw pointers because SkeletalTrapezoidation rewires
// edges repeatedly during collapse and transition insertion.
// [MEMORY] This type does not own any pointed-to objects; lifetime comes from HalfEdgeGraph's
// list storage. A copied HalfEdge duplicates only the pointers, not the adjacent nodes/edges.
// [COUPLING] Algorithms in SkeletalTrapezoidationGraph.cpp assume the specific navigation idiom
// `outgoing = outgoing->twin->next` visits all edges around a node.
// [HAZARD] There are no guards against partially wired edges. Any port must preserve the ability
// to represent temporarily inconsistent topology during graph edits, or rewrite the mutation code
// as transactional updates.

#ifndef UTILS_HALF_EDGE_H
#define UTILS_HALF_EDGE_H

#include <forward_list>
#include <optional>

namespace Slic3r::Arachne {

template<typename node_data_t, typename edge_data_t, typename derived_node_t, typename derived_edge_t> class HalfEdgeNode;

template<typename node_data_t, typename edge_data_t, typename derived_node_t, typename derived_edge_t> class HalfEdge
{
    using edge_t = derived_edge_t;
    using node_t = derived_node_t;

public:
    // [STATE] Per-edge payload supplied by the derived graph layer: for Arachne this stores central
    // flags, transition markers, source-edge provenance, and cached extrusion junctions.
    edge_data_t data;
    // [STATE] Opposite directed view of the same geometric segment.
    // [HAZARD] Many traversals assume twin is non-null once construction finishes.
    edge_t* twin = nullptr;
    // [STATE] Next directed edge when walking forward through one quad/rib chain.
    edge_t* next = nullptr;
    // [STATE] Previous directed edge in that same chain.
    edge_t* prev = nullptr;
    // [STATE] Origin node of this directed edge.
    node_t* from = nullptr;
    // [STATE] Destination node of this directed edge.
    node_t* to = nullptr;
    // [INTENT] Data payload is copied in by value so a new edge starts with an explicit semantic role
    // (normal segment, transition end, extra Voronoi rib, etc.) before its topology is wired.
    HalfEdge(edge_data_t data) : data(data) {}
    bool operator==(const edge_t& other) { return this == &other; }
};

} // namespace Slic3r::Arachne
#endif // UTILS_HALF_EDGE_H
