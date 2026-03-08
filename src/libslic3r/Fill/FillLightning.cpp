// [INTENT] Thin adapter layer connecting the Lightning Infill subsystem (Fill/Lightning/)
// to the generic Fill interface (Fill::_fill_surface_single). Contains only:
//   1. Filler::_fill_surface_single() — delegates to pre-built Layer objects inside Generator
//   2. GeneratorDeleter::operator()()  — custom deleter enabling opaque unique_ptr
//   3. build_generator()               — factory function for the Generator
//
// Lightning Infill origin: "Ribbed Support Vaults for 3D Printing of Hollowed Objects"
// (Tricard, Claux, Lefebvre). The implementation derives from CuraEngine (Ultimaker)
// with Orca extensions: multiline_fill(), chain_or_connect_infill(), divide-by-zero guard.
//
// [COUPLING] Depends on:
//   - Fill/Lightning/Generator.hpp (owns all per-object tree data)
//   - FillBase.hpp                 (multiline_fill, chain_or_connect_infill, FillParams)
//   - ShortestPath.hpp             (chain_or_connect_infill may use it internally)
//   - ClipperUtils.hpp             (intersection_pl)
//   - Print.hpp                    (PrintObject, passed to build_generator)
//
// [STATE] The Filler object is STATELESS except for:
//   - Filler::generator  — raw non-owning pointer to a Generator managed by PrintObject.
//     The PrintObject builds the Generator once via build_generator() and stores it;
//     the Filler only borrows it per-layer call. Lifetime: Generator outlives all Filler uses.
//   - Inherited Fill::layer_id, Fill::spacing, Fill::overlap (set before each call)
//
// [MEMORY] Generator lifetime is managed by GeneratorPtr (unique_ptr with GeneratorDeleter).
// The GeneratorDeleter is needed because Generator is defined in a separate TU (Generator.cpp)
// and the destructor call must occur there (avoids incomplete-type deletion).
// [HAZARD H264] GeneratorDeleter pattern means that if Generator.hpp is ever changed to expose
// the full definition, the custom deleter becomes redundant but harmless. If someone removes it
// and uses plain unique_ptr<Generator>, the destructor fires in the wrong TU on MSVC with /MT
// (different heap per TU), causing a heap corruption crash. Keep the custom deleter.
//
// [CONCURRENCY] No shared mutable state in this file. Generator is built once (single-threaded)
// and then only read (const Layer&) from multiple threads. Thread-safe as long as the Generator
// is not rebuilt during slicing. This is enforced by the PrintObject lifecycle.
//
// [HAZARD H265] NAMESPACE BUG: FillLightning.cpp closes with
//   `} // namespace Slic3r::FillAdaptive`
// instead of `} // namespace Slic3r::FillLightning`. This is a stale copy-paste from
// FillAdaptive.cpp. The actual closing brace IS correct (both modules use the same outer
// `Slic3r::FillLightning` namespace opened on line 8), but the comment is misleading and will
// confuse any automated namespace-extraction tool or documentation generator.
//
// [HAZARD H266] FillLightning.hpp line 39: `} // namespace FillAdaptive` — same copy-paste
// namespace comment bug as in .cpp. The Filler class IS inside FillLightning, not FillAdaptive.

#include "../ClipperUtils.hpp"
#include "../Print.hpp"
#include "../ShortestPath.hpp"
#include "FillBase.hpp"
#include "FillLightning.hpp"
#include "Lightning/Generator.hpp"

namespace Slic3r::FillLightning {

// [INTENT] Entry point called by the generic infill infrastructure once per ExPolygon per layer.
// Retrieves the pre-built Lightning tree for this layer from the Generator (computed at print-
// prepare time, not here), converts the tree's branches to Polylines, clips to the ExPolygon
// boundary, optionally applies Orca's multiline offset, then chains/connects the resulting lines.
//
// [STATE] Read-only access to Generator::m_lightning_layers[layer_id] via getTreesForLayer().
// Mutates: polylines_out (output accumulation).
// Does NOT modify the Lightning tree or the Generator.
//
// [COUPLING] Calls:
//   - generator->getTreesForLayer(layer_id)   — borrows const Layer&
//   - layer.convertToLines(...)               — converts tree branches → Polylines
//   - multiline_fill(...)                     — Orca extension: offset fill lines
//   - intersection_pl(...)                    — Clipper-based clip to expolygon
//   - chain_or_connect_infill(...)            — reorder/connect for G-code efficiency
//
// [MEMORY] fill_lines is a local Polylines; moved into intersection_pl then into
// chain_or_connect_infill. No heap allocations escape this function.
//
// [HAZARD H267] `generator` is a raw non-owning pointer. If the PrintObject is destroyed
// or the Generator is rebuilt between prepare_infill() and this call (e.g., settings change
// mid-slice), `generator` becomes dangling. No null-check or validity assertion exists.
// The caller (Print::process) must ensure Generator lifetime covers all Filler calls.
//
// [HAZARD H268] `this->layer_id` is a size_t index into Generator::m_lightning_layers.
// If layer_id >= m_lightning_layers.size() (possible if the layer count changes between
// generator construction and fill), getTreesForLayer() fires assert(layer_id < size) in debug
// but reads out-of-bounds in release (UB). No range check in release builds.
//
// [UNCLEAR] `scaled<coord_t>(0.5 * this->spacing - this->overlap)` is the line_overlap
// parameter passed to convertToLines → removeJunctionOverlap. It controls how much tree-node
// junction endpoints are retracted to avoid double-printing the junction area. The 0.5 factor
// halves the spacing because junctions from both sides contribute. No comment in original code.
void Filler::_fill_surface_single(const FillParams&              params,
                                  unsigned int                   thickness_layers,
                                  const std::pair<float, Point>& direction,
                                  ExPolygon                      expolygon,
                                  Polylines&                     polylines_out)
{
    // [STATE] getTreesForLayer returns a const Layer& — no copy, no mutation.
    // The Layer holds tree_roots (std::vector<NodeSPtr>) populated during Generator construction.
    const Layer& layer = generator->getTreesForLayer(this->layer_id);

    // [INTENT] Convert tree structure into printable Polylines.
    // convertToLines clips internally to limit_to_outline (polygon boundary) and applies
    // junction overlap reduction by line_overlap = 0.5*spacing - overlap.
    // [MEMORY] fill_lines is value-initialized; moved below.
    Polylines fill_lines = layer.convertToLines(to_polygons(expolygon), scaled<coord_t>(0.5 * this->spacing - this->overlap));

    // [INTENT] Orca multiline extension: offset lines inward/outward to produce multiple
    // parallel lines per infill pass when params.multiline > 1. This is an Orca-only feature
    // not present in original Cura/PrusaSlicer Lightning.
    // [COUPLING] multiline_fill() is defined in FillBase.cpp.
    // [HAZARD H269] If params.multiline == 1 (default), multiline_fill is a no-op.
    // If multiline > 1, the offset lines may extend outside the expolygon; intersection_pl
    // below re-clips. This double-clip is intentional.
    // Apply multiline offset if needed
    multiline_fill(fill_lines, params, spacing);
    // [INTENT] Final clip to the expolygon boundary using Clipper intersection.
    // Necessary because convertToLines only clips to polygon outlines (Polygons, not ExPolygon),
    // and multiline offsets may push lines outside the boundary.
    // [MEMORY] fill_lines is moved into intersection_pl; result is a new Polylines allocated
    // by Clipper. fill_lines is in a valid-but-unspecified state after the move.
    fill_lines = Slic3r::intersection_pl(std::move(fill_lines), expolygon);

    // [INTENT] Reorder and optionally connect the clipped polylines to minimize travel moves.
    // chain_or_connect_infill may use ShortestPath for optimal ordering or connect endpoints
    // that are within spacing distance, reducing retract/deretract cycles.
    // [MEMORY] fill_lines is moved into chain_or_connect_infill; result goes to polylines_out.
    chain_or_connect_infill(std::move(fill_lines), expolygon, polylines_out, this->spacing, params);
}

// [INTENT] Custom deleter for GeneratorPtr (unique_ptr<Generator, GeneratorDeleter>).
// Required because Generator is defined in Generator.cpp (separate TU from this file).
// Without a custom deleter, unique_ptr<Generator> would instantiate ~Generator() inline in
// any TU that includes FillLightning.hpp — but if Generator's full definition is not visible
// in that TU, this produces an "incomplete type" compile error or, worse, a no-op delete.
//
// [MEMORY] The `delete p` here runs in the TU where Generator is fully defined (Lightning/
// Generator.cpp or this file, both linked into libslic3r). This ensures the destructor fires
// in the correct module with the correct heap (critical for MSVC DLL builds).
void GeneratorDeleter::operator()(Generator* p) { delete p; }

// [INTENT] Factory function for GeneratorPtr. Called once per PrintObject during prepare_infill.
// Constructs the entire Lightning tree for all layers — this is an O(N_layers × N_overhang_points)
// operation and may take 0.5–5 seconds for large models.
// [COUPLING] Depends on PrintObject (for layer data, region configs, nozzle diameter).
// [STATE] Returns heap-allocated Generator wrapped in unique_ptr. All per-layer tree data
// is computed in the Generator constructor and stored in Generator::m_lightning_layers.
// [CONCURRENCY] Not called concurrently. PrintObject::prepare_infill runs single-threaded.
GeneratorPtr build_generator(const PrintObject& print_object, const std::function<void()>& throw_on_cancel_callback)
{
    return GeneratorPtr(new Generator(print_object, throw_on_cancel_callback));
}

} // namespace Slic3r::FillLightning
// [HAZARD H265] Comment above says FillAdaptive (copy-paste bug from original); namespace is correct.
