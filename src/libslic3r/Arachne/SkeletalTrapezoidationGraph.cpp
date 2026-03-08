// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] SkeletalTrapezoidationGraph.cpp — mutable half-edge graph that represents
// the Generalized Medial Axis Transform (gMAT) of the input polygon.
//
// The graph is initially built from a Voronoi diagram (in SkeletalTrapezoidation.cpp)
// and then mutated by two kinds of structural edits performed here:
//   1. collapseSmallEdges()  — removes near-degenerate quads created by closely-spaced
//      input vertices; keeps graph topology valid for later bead-count propagation.
//   2. insertNode() / insertRib() / makeRib() — split existing edges at transition
//      positions to insert new skeleton nodes (bead-count transitions) together with
//      perpendicular "rib" edges that connect the skeleton back to the source polygon.
//
// KEY INVARIANTS that must survive any refactor:
//   • Every edge has a non-null twin; EXTRA_VD and TRANSITION_END ribs are always
//     created in forward/backward pairs.
//   • quad_start.prev == nullptr marks the first edge in a "quad chain" (the
//     traversal unit for collapseSmallEdges).
//   • transition_ratio on all inserted mid-nodes is explicitly set to 0 — callers in
//     SkeletalTrapezoidation depend on this initialisation.
//   • After insertNode() the caller receives the *last* edge replacing the original
//     so it can continue graph traversal at the correct position.

#include "SkeletalTrapezoidationGraph.hpp"

#include <ankerl/unordered_dense.h>
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <iostream>
#include <cassert>
#include <cinttypes>

#include "../Line.hpp"
#include "libslic3r/Arachne/SkeletalTrapezoidationEdge.hpp"
#include "libslic3r/Arachne/SkeletalTrapezoidationJoint.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r::Arachne {

// [INTENT] Thin constructor — just forwards the edge data payload to HalfEdge<>.
// The is_central flag inside SkeletalTrapezoidationEdge begins as UNKNOWN; callers
// (SkeletalTrapezoidation::markRegions) must call setIsCentral() before isCentral()
// is safe to call.  Calling isCentral() on a freshly constructed STHalfEdge will
// assert-fail in debug builds.
STHalfEdge::STHalfEdge(SkeletalTrapezoidationEdge data) : HalfEdge(data) {}

// [INTENT] Determine whether traversal from this edge can ever reach a node whose
// distance_to_boundary is strictly greater than from->distance_to_boundary.
// Used by isLocalMaximum() on nodes to decide whether a node is a ridge peak.
//
// [STATE] "strict=false" (default): equidistant edges are allowed to count as going
// up — the function recurses through the fan of outgoing edges from `to` to find any
// truly upward edge.  "strict=true": equidistant edges return false immediately.
//
// [HAZARD] H205 — unbounded recursion risk: if the graph contains a cycle of
// equidistant edges (which should not occur in a valid gMAT but can appear after
// collapseSmallEdges on degenerate input), this function will recurse infinitely.
// There is no depth limit or visited-set guard.  Refactors MUST add cycle detection
// or convert to an iterative BFS/DFS with a visited set.
//
// [COUPLING] Relies on the twin->next fan-walk pattern: outgoing edges around a node
// are visited by following outgoing = outgoing->twin->next.  This convention is
// enforced by HalfEdgeGraph and must be preserved.
bool STHalfEdge::canGoUp(bool strict) const
{
    if (to->data.distance_to_boundary > from->data.distance_to_boundary) {
        return true;
    }
    if (to->data.distance_to_boundary < from->data.distance_to_boundary || strict) {
        return false;
    }

    // Edge is between equidistqant verts; recurse!
    // [HAZARD] H205 — no cycle guard; see above.
    for (edge_t* outgoing = next; outgoing != twin; outgoing = outgoing->twin->next) {
        if (outgoing->canGoUp()) {
            return true;
        }
        assert(outgoing->twin);
        if (!outgoing->twin)
            return false;
        assert(outgoing->twin->next);
        if (!outgoing->twin->next)
            return true; // This point is on the boundary?! Should never occur
    }
    return false;
}

// [INTENT] Canonical "is this edge going uphill on the medial-axis ridge?" predicate.
// Used when sorting/filtering edges for bead-count propagation in SkeletalTrapezoidation.
//
// [STATE] For equidistant edges the function computes distToGoUp() in both directions
// and calls the edge "upward" when the forward direction reaches a higher node sooner.
// When *neither* direction can reach a higher node, falls back to an arbitrary
// lexicographic tie-break (to->p < from->p) that is consistent across twin pairs
// (twin returns the opposite value).
//
// [HAZARD] H206 — tie-break by point coordinates: if two nodes share the same
// (x, y) coordinates (can happen after collapseSmallEdges merges nodes) the
// tie-break is non-deterministic across platforms with different Point orderings.
bool STHalfEdge::isUpward() const
{
    if (to->data.distance_to_boundary > from->data.distance_to_boundary) {
        return true;
    }
    if (to->data.distance_to_boundary < from->data.distance_to_boundary) {
        return false;
    }

    // Equidistant edge case:
    std::optional<coord_t> forward_up_dist  = this->distToGoUp();
    std::optional<coord_t> backward_up_dist = twin->distToGoUp();
    if (forward_up_dist && backward_up_dist) {
        return forward_up_dist < backward_up_dist;
    }

    if (forward_up_dist) {
        return true;
    }

    if (backward_up_dist) {
        return false;
    }
    return to->p < from->p; // Arbitrary ordering, which returns the opposite for the twin edge
}

// [INTENT] Returns the total path length along equidistant edges until we reach the
// first truly upward edge, or nullopt if no such edge exists from this direction.
// The length includes the length of this edge itself when it can go up.
//
// [STATE] Accumulates the minimum distance across all outgoing branches via a
// recursive BFS-like fan-walk.  Returns 0 if this edge already goes up.
//
// [HAZARD] H205 (shared with canGoUp) — same unbounded recursion risk on cycles of
// equidistant edges.  No visited-set guard.
//
// [MEMORY] Each recursive frame allocates one std::optional<coord_t> on the stack.
// Stack depth is bounded only by equidistant-edge chain length; not a concern for
// realistic polygon inputs but could be an issue for adversarial inputs.
std::optional<coord_t> STHalfEdge::distToGoUp() const
{
    if (to->data.distance_to_boundary > from->data.distance_to_boundary) {
        return 0;
    }
    if (to->data.distance_to_boundary < from->data.distance_to_boundary) {
        return std::optional<coord_t>();
    }

    // Edge is between equidistqant verts; recurse!
    std::optional<coord_t> ret;
    for (edge_t* outgoing = next; outgoing != twin; outgoing = outgoing->twin->next) {
        std::optional<coord_t> dist_to_up = outgoing->distToGoUp();
        if (dist_to_up) {
            if (ret) {
                ret = std::min(*ret, *dist_to_up);
            } else {
                ret = dist_to_up;
            }
        }
        assert(outgoing->twin);
        if (!outgoing->twin)
            return std::optional<coord_t>();
        assert(outgoing->twin->next);
        if (!outgoing->twin->next)
            return 0; // This point is on the boundary?! Should never occur
    }
    // [STATE] Adds euclidean length of this edge (integer cast from coord_t to int64_t
    // before norm() to avoid overflow in 32-bit coord systems).
    if (ret) {
        ret = *ret + (to->p - from->p).cast<int64_t>().norm();
    }
    return ret;
}

// [INTENT] Walk the chain starting at `this` to find the last edge whose `next` is
// null, then return its twin.  Used during graph construction to locate the "open
// end" of a partial quad chain so new edges can be appended.
//
// [HAZARD] H207 — infinite loop if graph contains a closed next-chain that never
// reaches a null next.  The loop guard `result == this` only catches the case where
// we return to the *starting* edge; a cycle not involving `this` would spin forever.
STHalfEdge* STHalfEdge::getNextUnconnected()
{
    edge_t* result = static_cast<STHalfEdge*>(this);
    while (result->next) {
        result = result->next;
        if (result == this) {
            return nullptr;
        }
    }
    return result->twin;
}

// [INTENT] Thin constructor — forwards joint data and position to HalfEdgeNode<>.
// distance_to_boundary is initialised to -1 (sentinel "not yet computed") inside
// SkeletalTrapezoidationJoint's default ctor.
STHalfEdgeNode::STHalfEdgeNode(SkeletalTrapezoidationJoint data, Point p) : HalfEdgeNode(data, p) {}

// [INTENT] Count the number of CENTRAL edges incident to this node; return true when
// the count exceeds 2.  A node is a "multi-intersection" when 3+ central branches
// meet — this occurs at T-junction or star-junction features in the medial axis and
// requires special handling during bead-count propagation.
//
// [STATE] Central/non-central status must already be set on all incident edges
// before this is called (assert would fire in isCentral() otherwise).
//
// [HAZARD] H208 — early return `false` on null outgoing edge (boundary node guard)
// means boundary nodes are never classified as multi-intersections even if they
// happen to have many central incident edges.  This is intentional but undocumented.
bool STHalfEdgeNode::isMultiIntersection()
{
    int     odd_path_count = 0;
    edge_t* outgoing       = this->incident_edge;
    do {
        if (!outgoing) { // This is a node on the outside
            return false;
        }
        if (outgoing->data.isCentral()) {
            odd_path_count++;
        }
    } while (outgoing = outgoing->twin->next, outgoing != this->incident_edge);
    return odd_path_count > 2;
}

// [INTENT] Returns true if ANY incident edge is central.  A node is "on the skeleton"
// if at least one of its edges is a medial-axis central edge.
//
// [COUPLING] Used in SkeletalTrapezoidation::generateSegments() to filter nodes that
// need bead-count assignments from nodes that are only structural (EXTRA_VD/TRANSITION_END).
bool STHalfEdgeNode::isCentral() const
{
    edge_t* edge = incident_edge;
    do {
        if (edge->data.isCentral()) {
            return true;
        }
        assert(edge->twin);
        if (!edge->twin)
            return false;
    } while (edge = edge->twin->next, edge != incident_edge);
    return false;
}

// [INTENT] Returns true if this node is a local maximum of distance_to_boundary —
// i.e. it is a ridge peak on the medial axis (no outgoing edge can go further up).
//
// [STATE] distance_to_boundary == 0 fast-path: boundary nodes are never local maxima.
// For all other nodes, the function asks canGoUp(strict) on every outgoing edge and
// returns false as soon as any outgoing edge can go higher.
//
// [COUPLING] Drives SkeletalTrapezoidation::generateMaxima() which seeds the
// bead-count propagation from ridge peaks downward.
bool STHalfEdgeNode::isLocalMaximum(bool strict) const
{
    if (data.distance_to_boundary == 0) {
        return false;
    }

    edge_t* edge = incident_edge;
    do {
        if (edge->canGoUp(strict)) {
            return false;
        }
        assert(edge->twin);
        if (!edge->twin)
            return false;

        if (!edge->twin->next) { // This point is on the boundary
            return false;
        }
    } while (edge = edge->twin->next, edge != incident_edge);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SkeletalTrapezoidationGraph — graph mutation methods
// ─────────────────────────────────────────────────────────────────────────────

// [INTENT] Remove near-degenerate quads from the graph where node pairs are closer
// than snap_dist (default 5 nm).  Two collapse patterns are handled:
//
//   Pattern A — "top collapse": quad_mid is short → merge quad_mid->to into
//               quad_mid->from and re-link surrounding edges.
//   Pattern B — "side collapse": both quad_start and quad_end are short → collapse
//               the entire quad by merging quad_start->from into quad_end->to and
//               replacing quad_start/quad_end twins with each other.
//
// [STATE] A "quad" is a chain of 2–3 edges sharing the same prev==nullptr entry.
// quad_start is the chain head, quad_end is the chain tail (quad_end->next == null),
// quad_mid is the middle edge (nullptr for 2-edge chains).
//
// [CONCURRENCY] Not thread-safe — modifies edges and nodes std::list in-place.
// The edge_it iterator is carefully kept valid by safelyRemoveEdge.
//
// [HAZARD] H209 — the count > 50 logging loop and count > 1000 break in Pattern A
// are overflow guards that silently truncate re-linking of edges when a node has
// more than 1000 outgoing edges.  In that degenerate case some edge->from pointers
// will still point to the deleted quad_mid->to node, leaving dangling pointers and
// corrupting the graph.  This is a pre-existing upstream bug.
//
// [HAZARD] H210 — Pattern B collapse does NOT handle the case where only one side
// (quad_start OR quad_end, but not both) is within snap_dist.  The comment "If only
// one side had collapsable length then the cell on the other side of that edge has to
// collapse" acknowledges this but no action is taken — the quad is left as-is which
// can leave very thin quads in the graph that may generate zero-width beads downstream.
//
// [MEMORY] edge_locator and node_locator are ankerl dense hash maps keyed by raw
// pointer — fine for single-threaded use but must not outlive the graph's lifetime.
void SkeletalTrapezoidationGraph::collapseSmallEdges(coord_t snap_dist)
{
    // [STATE] Build pointer→iterator maps so safelyRemoveEdge can erase arbitrary
    // positions from std::list<edge_t> and std::list<node_t> in O(1).
    ankerl::unordered_dense::map<edge_t*, Edges::iterator> edge_locator;
    ankerl::unordered_dense::map<node_t*, Nodes::iterator> node_locator;

    for (auto edge_it = edges.begin(); edge_it != edges.end(); ++edge_it) {
        edge_locator.emplace(&*edge_it, edge_it);
    }

    for (auto node_it = nodes.begin(); node_it != nodes.end(); ++node_it) {
        node_locator.emplace(&*node_it, node_it);
    }

    // [INTENT] Erase an edge from the list without invalidating the outer loop's
    // current iterator.  Sets edge_it_is_updated=true if the erased edge IS the
    // current iterator, in which case erase() already advanced edge_it.
    auto safelyRemoveEdge = [this, &edge_locator](edge_t* to_be_removed, Edges::iterator& current_edge_it, bool& edge_it_is_updated) {
        if (current_edge_it != edges.end() && to_be_removed == &*current_edge_it) {
            current_edge_it    = edges.erase(current_edge_it);
            edge_it_is_updated = true;
        } else {
            edges.erase(edge_locator[to_be_removed]);
        }
    };

    auto should_collapse = [snap_dist](node_t* a, node_t* b) { return shorter_then(a->p - b->p, snap_dist); };

    // [STATE] Only process chain heads (prev == nullptr).  Each chain is a "quad".
    for (auto edge_it = edges.begin(); edge_it != edges.end();) {
        if (edge_it->prev) {
            edge_it++;
            continue;
        }

        edge_t* quad_start = &*edge_it;
        edge_t* quad_end   = quad_start;
        while (quad_end->next)
            quad_end = quad_end->next;
        edge_t* quad_mid = (quad_start->next == quad_end) ? nullptr : quad_start->next;

        bool edge_it_is_updated = false;
        if (quad_mid && should_collapse(quad_mid->from, quad_mid->to)) {
            // [INTENT] Pattern A — collapse the middle edge.
            // All edges going from the old quad_mid->to node are re-pointed to quad_mid->from.
            assert(quad_mid->twin);
            if (!quad_mid->twin) {
                BOOST_LOG_TRIVIAL(warning) << "Encountered quad edge without a twin.";
                continue; // Prevent accessing unallocated memory.
            }
            int count = 0;
            for (edge_t* edge_from_3 = quad_end; edge_from_3 && edge_from_3 != quad_mid->twin; edge_from_3 = edge_from_3->twin->next) {
                edge_from_3->from     = quad_mid->from;
                edge_from_3->twin->to = quad_mid->from;
                if (count > 50) {
                    // [HAZARD] H209 — only logging starts at 50; hard break at 1000.
                    // Edges beyond index 1000 keep dangling from pointers.
                    std::cerr << edge_from_3->from->p << " - " << edge_from_3->to->p << '\n';
                }
                if (++count > 1000) {
                    break;
                }
            }

            // o-o > collapse top
            // | |
            // | |
            // | |
            // o o
            if (quad_mid->from->incident_edge == quad_mid) {
                if (quad_mid->twin->next) {
                    quad_mid->from->incident_edge = quad_mid->twin->next;
                } else {
                    quad_mid->from->incident_edge = quad_mid->prev->twin;
                }
            }

            nodes.erase(node_locator[quad_mid->to]);

            quad_mid->prev->next       = quad_mid->next;
            quad_mid->next->prev       = quad_mid->prev;
            quad_mid->twin->next->prev = quad_mid->twin->prev;
            quad_mid->twin->prev->next = quad_mid->twin->next;

            safelyRemoveEdge(quad_mid->twin, edge_it, edge_it_is_updated);
            safelyRemoveEdge(quad_mid, edge_it, edge_it_is_updated);
        }

        //  o-o
        //  | | > collapse sides
        //  o o
        if (should_collapse(quad_start->from, quad_end->to) &&
            should_collapse(quad_start->to, quad_end->from)) { // Collapse start and end edges and remove whole cell
            // [INTENT] Pattern B — bypass the quad entirely: make quad_start->twin and
            // quad_end->twin point directly at each other, merging the two side nodes.

            quad_start->twin->to        = quad_end->to;
            quad_end->to->incident_edge = quad_end->twin;
            if (quad_end->from->incident_edge == quad_end) {
                if (quad_end->twin->next) {
                    quad_end->from->incident_edge = quad_end->twin->next;
                } else {
                    quad_end->from->incident_edge = quad_end->prev->twin;
                }
            }
            nodes.erase(node_locator[quad_start->from]);

            quad_start->twin->twin = quad_end->twin;
            quad_end->twin->twin   = quad_start->twin;
            safelyRemoveEdge(quad_start, edge_it, edge_it_is_updated);
            safelyRemoveEdge(quad_end, edge_it, edge_it_is_updated);
        }
        // If only one side had collapsable length then the cell on the other side of that edge has to collapse
        // if we would collapse that one edge then that would change the quad_start and/or quad_end of neighboring cells
        // this is to do with the constraint that !prev == !twin.next

        if (!edge_it_is_updated) {
            edge_it++;
        }
    }
}

// [INTENT] Attach a perpendicular "rib" edge from prev_edge->to to the nearest point
// on the source polygon segment [start_source_point, end_source_point].
// This is used when processing input polygon vertices that fall exactly on a Voronoi
// edge — it ensures the graph remains connected to the polygon boundary.
//
// [STATE] Creates two new nodes (none — reuses prev_edge->to for the skeleton side)
// and one new boundary node at the foot of the perpendicular, plus one edge pair
// (forth_edge / back_edge) of type EXTRA_VD.  Updates prev_edge to point to back_edge
// so the caller can continue chaining.
//
// [COUPLING] distance_to_boundary is assigned here directly to prev_edge->to->data
// and to the new boundary node (=0).  The foot-of-perpendicular node MUST have
// distance_to_boundary == 0 — SkeletalTrapezoidation uses this as the polygon
// boundary indicator.
//
// [HAZARD] H211 — distance_to_boundary is stored as coord_t but computed via
// (Point - Point).cast<int64_t>().norm() which returns double, then implicitly
// truncated back to coord_t.  Sub-nanometer precision loss is expected and benign,
// but refactors should make the truncation explicit.
void SkeletalTrapezoidationGraph::makeRib(edge_t*& prev_edge, const Point& start_source_point, const Point& end_source_point)
{
    Point p;
    // [STATE] p receives the foot-of-perpendicular from prev_edge->to->p onto the
    // infinite line through [start_source_point, end_source_point].
    Line(start_source_point, end_source_point).distance_to_infinite_squared(prev_edge->to->p, &p);
    coord_t dist                             = (prev_edge->to->p - p).cast<int64_t>().norm();
    prev_edge->to->data.distance_to_boundary = dist;
    assert(dist >= 0);

    nodes.emplace_front(SkeletalTrapezoidationJoint(), p);
    node_t* node                    = &nodes.front();
    node->data.distance_to_boundary = 0; // boundary node sentinel

    edges.emplace_front(SkeletalTrapezoidationEdge(SkeletalTrapezoidationEdge::EdgeType::EXTRA_VD));
    edge_t* forth_edge = &edges.front();
    edges.emplace_front(SkeletalTrapezoidationEdge(SkeletalTrapezoidationEdge::EdgeType::EXTRA_VD));
    edge_t* back_edge = &edges.front();

    prev_edge->next     = forth_edge;
    forth_edge->prev    = prev_edge;
    forth_edge->from    = prev_edge->to;
    forth_edge->to      = node;
    forth_edge->twin    = back_edge;
    back_edge->twin     = forth_edge;
    back_edge->from     = node;
    back_edge->to       = prev_edge->to;
    node->incident_edge = back_edge;

    prev_edge = back_edge; // advance caller's cursor
}

// [INTENT] Split an existing half-edge at mid_node by inserting a rib (perpendicular
// connection to the source polygon) at that position.  The original edge is replaced
// by two edges (first, second) with a rib pair (outward_edge / inward_edge) between
// them.  Returns {first, second} so the caller (insertNode) can patch twin pointers
// across the two calls (one for the original edge, one for its twin).
//
// Topological result:
//
//   node_before ──first──> mid_node ──outward_edge──> source_node
//                                   <──inward_edge──
//               <──twin(first)──── ...
//   mid_node ──second──> node_after
//
// [STATE] transition_ratio on mid_node is set to 0 here explicitly.  This signals
// "no partial transition" at the ends of a transition span — required by
// SkeletalTrapezoidation::generateJunctions() which reads transition_ratio to compute
// the spacing of the first junction from the transition end.
//
// [STATE] outward_edge.setIsCentral(false) — the rib is NOT part of the medial axis.
//        first.setIsCentral(true) and second.setIsCentral(true) — the two skeleton
//        fragments inherit the centrality of the original edge.
//
// [HAZARD] H212 — assert(dist > 0) on line 364: if mid_node lies exactly on the
// source segment (dist == 0), the assert fires in debug but silently produces a
// zero-length rib in release.  Zero-length ribs cause degenerate extrusion junctions
// downstream in generateJunctions().
//
// [HAZARD] H213 — first->twin and second->twin are left as nullptr on exit (comment
// "we don't know these yet!").  insertNode() immediately patches them; but if
// insertRib() were ever called in isolation without the corresponding twin patch,
// isCentral() and bead-count propagation walks would crash on null-deref.
std::pair<SkeletalTrapezoidationGraph::edge_t*, SkeletalTrapezoidationGraph::edge_t*> SkeletalTrapezoidationGraph::insertRib(
    edge_t& edge, node_t* mid_node)
{
    edge_t* edge_before = edge.prev;
    edge_t* edge_after  = edge.next;
    node_t* node_before = edge.from;
    node_t* node_after  = edge.to;

    Point p = mid_node->p;

    // [STATE] getSource() walks the prev/next chain to find the original polygon
    // segment endpoints that spawned this Voronoi edge.
    const Line source_segment = getSource(edge);
    Point      px;
    source_segment.distance_to_squared(p, &px); // px = foot-of-perpendicular on segment
    coord_t dist = (p - px).cast<int64_t>().norm();
    assert(dist > 0); // [HAZARD] H212 — zero distance fires here
    mid_node->data.distance_to_boundary = dist;
    mid_node->data.transition_ratio =
        0; // Both transition end should have rest = 0, because at the ends a whole number of beads fits without rest

    nodes.emplace_back(SkeletalTrapezoidationJoint(), px);
    node_t* source_node                    = &nodes.back();
    source_node->data.distance_to_boundary = 0; // boundary node sentinel

    edge_t* first = &edge;
    edges.emplace_back(SkeletalTrapezoidationEdge());
    edge_t* second = &edges.back();
    edges.emplace_back(SkeletalTrapezoidationEdge(SkeletalTrapezoidationEdge::EdgeType::TRANSITION_END));
    edge_t* outward_edge = &edges.back();
    edges.emplace_back(SkeletalTrapezoidationEdge(SkeletalTrapezoidationEdge::EdgeType::TRANSITION_END));
    edge_t* inward_edge = &edges.back();

    if (edge_before) {
        edge_before->next = first;
    }
    first->next        = outward_edge;
    outward_edge->next = nullptr;
    inward_edge->next  = second;
    second->next       = edge_after;

    if (edge_after) {
        edge_after->prev = second;
    }
    second->prev       = inward_edge;
    inward_edge->prev  = nullptr;
    outward_edge->prev = first;
    first->prev        = edge_before;

    first->to        = mid_node;
    outward_edge->to = source_node;
    inward_edge->to  = mid_node;
    second->to       = node_after;

    first->from        = node_before;
    outward_edge->from = mid_node;
    inward_edge->from  = source_node;
    second->from       = mid_node;

    node_before->incident_edge = first;
    mid_node->incident_edge    = outward_edge;
    source_node->incident_edge = inward_edge;
    if (edge_after) {
        node_after->incident_edge = edge_after;
    }

    first->data.setIsCentral(true);
    outward_edge->data.setIsCentral(false); // TODO verify this is always the case.
    inward_edge->data.setIsCentral(false);
    second->data.setIsCentral(true);

    outward_edge->twin = inward_edge;
    inward_edge->twin  = outward_edge;

    first->twin  = nullptr; // we don't know these yet! [HAZARD] H213
    second->twin = nullptr;

    assert(second->prev->from->data.distance_to_boundary == 0);

    return std::make_pair(first, second);
}

// [INTENT] High-level node insertion: create a new mid_node on the medial axis at
// position `mid` with bead_count `mide_node_bead_count` (note: typo in parameter
// name is pre-existing upstream), insert it into both `edge` and its twin, and
// patch all twin pointers so the graph remains consistent.
//
// [STATE] Splits edge → {first, last} and edge->twin → {first_twin, last_twin}, then
// cross-connects:
//   first_input.twin    = last_twin
//   last_twin.twin      = first_input
//   last_input.twin     = first_twin
//   first_twin.twin     = last_input
//
// [COUPLING] mid_node->data.bead_count is set here to `mide_node_bead_count`.  The
// bead_count field is read by SkeletalTrapezoidation::propagateBeadingsUpward/
// Downward() and by generateJunctions() — it MUST be set before those passes run.
//
// Returns the last edge replacing `edge` (pointing to the same `to` node) so the
// caller can continue traversing from the right position.
SkeletalTrapezoidationGraph::edge_t* SkeletalTrapezoidationGraph::insertNode(edge_t* edge, Point mid, coord_t mide_node_bead_count)
{
    edge_t* last_edge_replacing_input = edge;

    nodes.emplace_back(SkeletalTrapezoidationJoint(), mid);
    node_t* mid_node = &nodes.back();

    // [STATE] Temporarily sever twin link so insertRib() on each side can't see the
    // other side's (not-yet-updated) twin pointer.
    edge_t* twin                                           = last_edge_replacing_input->twin;
    last_edge_replacing_input->twin                        = nullptr;
    twin->twin                                             = nullptr;
    std::pair<edge_t*, edge_t*> left_pair                  = insertRib(*last_edge_replacing_input, mid_node);
    std::pair<edge_t*, edge_t*> right_pair                 = insertRib(*twin, mid_node);
    edge_t*                     first_edge_replacing_input = left_pair.first;
    last_edge_replacing_input                              = left_pair.second;
    edge_t* first_edge_replacing_twin                      = right_pair.first;
    edge_t* last_edge_replacing_twin                       = right_pair.second;

    // [STATE] Patch cross-twins — each edge from one side is the twin of the
    // corresponding edge from the other side (outer→outer, inner→inner).
    first_edge_replacing_input->twin = last_edge_replacing_twin;
    last_edge_replacing_twin->twin   = first_edge_replacing_input;
    last_edge_replacing_input->twin  = first_edge_replacing_twin;
    first_edge_replacing_twin->twin  = last_edge_replacing_input;

    mid_node->data.bead_count = mide_node_bead_count;

    return last_edge_replacing_input;
}

// [INTENT] Reconstruct the original source polygon segment that a given edge (or
// its chain) was derived from during Voronoi construction.  Walks `prev` to the
// chain head and `next` to the chain tail to recover the two endpoint coordinates.
//
// [STATE] Source polygon endpoints are stored as the `from->p` of the chain head and
// the `to->p` of the chain tail.  This works because each Voronoi edge chain has its
// endpoints set to the polygon vertex positions during graph construction in
// SkeletalTrapezoidation::constructFromPolygons().
//
// [COUPLING] Used by insertRib() (via getSource) to find the foot-of-perpendicular
// for the new rib node.  Correctness depends on the chain-head/tail invariant being
// preserved through all graph edits.
Line SkeletalTrapezoidationGraph::getSource(const edge_t& edge) const
{
    const edge_t* from_edge = &edge;
    while (from_edge->prev)
        from_edge = from_edge->prev;

    const edge_t* to_edge = &edge;
    while (to_edge->next)
        to_edge = to_edge->next;

    return Line(from_edge->from->p, to_edge->to->p);
}

} // namespace Slic3r::Arachne
