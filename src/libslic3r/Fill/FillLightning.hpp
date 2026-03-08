// [INTENT] Public interface for the Lightning Infill adapter layer.
// Declares the Filler class (Fill subclass), the GeneratorPtr (opaque unique_ptr),
// and the build_generator() factory. The Generator implementation is kept opaque
// via the custom GeneratorDeleter pattern — consumers never see Generator's full definition.
//
// Lightning Infill: minimal-material tree-branching infill pattern based on
// "Ribbed Support Vaults for 3D Printing of Hollowed Objects" (Tricard et al.)
// Originally from CuraEngine (Ultimaker, AGPL3). Ported to OrcaSlicer with:
//   - multiline_fill extension (Orca-specific)
//   - divide-by-zero guard in Generator constructor
//   - generateTreesforSupport() second constructor for tree-support usage
//
// [COUPLING] This header is included by:
//   - FillLightning.cpp (implements Filler and GeneratorDeleter)
//   - Print.cpp / PrintObject.cpp (stores GeneratorPtr, calls build_generator)
//   - Any code that needs to check generator pointer or call build_generator
//
// [HAZARD H266] Namespace comment bug: Line 39 closes with `} // namespace FillAdaptive`
// but the actual namespace opened on line 10 is `FillLightning`. Copy-paste error from
// FillAdaptive.hpp. The brace itself is syntactically correct; the comment is wrong.
// Any automated namespace-parser or code-generation tool will produce incorrect output.

#ifndef slic3r_FillLightning_hpp_
#define slic3r_FillLightning_hpp_

#include "FillBase.hpp"

namespace Slic3r {

class PrintObject;

namespace FillLightning {

// [INTENT] Forward-declare Generator so consumers (PrintObject.cpp, Fill.cpp) can hold
// a GeneratorPtr without including the full Generator.hpp header and its transitive deps
// (Layer.hpp, TreeNode.hpp, DistanceField.hpp, EdgeGrid.hpp, etc.).
// This is a classic PImpl-lite pattern: opaque type + custom deleter.
class Generator;

// [INTENT] Custom deleter for GeneratorPtr (unique_ptr<Generator, GeneratorDeleter>).
// Needed because unique_ptr<Generator> would try to call ~Generator() inline in any TU
// that includes this header, but Generator is an incomplete type here. Without the custom
// deleter pattern, this would be a compile error.
//
// [MEMORY] operator()(Generator* p) is defined in FillLightning.cpp where Generator is
// a complete type, ensuring the destructor runs in the correct TU/heap.
//
// [HAZARD H264] If Generator.hpp is ever included transitively before this file, the
// compiler may allow plain unique_ptr<Generator> without this deleter, creating a portability
// risk (MSVC /MT: different heap per TU → heap corruption on cross-TU delete).
// Always use GeneratorPtr, never unique_ptr<Generator>.
struct GeneratorDeleter
{
    void operator()(Generator* p);
};

// [INTENT] Owning smart pointer for the Generator. One GeneratorPtr per PrintObject.
// The Generator is expensive to build (O(N_layers × N_overhang_points)) and is reused
// for all calls to Filler::_fill_surface_single() on the same PrintObject.
// [MEMORY] Heap-allocated; freed when the PrintObject is destroyed or the generator is
// rebuilt on a settings change.
using GeneratorPtr = std::unique_ptr<Generator, GeneratorDeleter>;

// [INTENT] Factory function. Constructs the Generator for a PrintObject.
// Called once at prepare_infill time. Computes all internal overhangs and builds
// all lightning trees bottom-up for every layer.
// [COUPLING] PrintObject must have fill_surfaces already populated.
// [CONCURRENCY] Not thread-safe. Must be called from the main PrintObject preparation
// thread, not concurrently with any Filler that borrows the same Generator.
GeneratorPtr build_generator(const PrintObject& print_object, const std::function<void()>& throw_on_cancel_callback);

// [INTENT] Fill subclass for Lightning Infill. One instance is created per infill region
// per layer. Holds a non-owning pointer to the Generator (owned by PrintObject).
//
// [STATE] Stateless per-call. The generator pointer is set once by PrintObject during
// fill setup. Inherited Fill members (layer_id, spacing, overlap) are set before each
// _fill_surface_single() call.
//
// [MEMORY] Does NOT own generator. Generator lifetime is managed by the PrintObject.
// Filler must not outlive the Generator.
//
// [CONCURRENCY] Multiple Filler instances may exist and call _fill_surface_single()
// concurrently (TBB parallel_for over layers), but each reads a different
// m_lightning_layers[layer_id] — no shared mutable state. Thread-safe as long as
// Generator::m_lightning_layers is not modified during slicing.
class Filler : public Slic3r::Fill
{
public:
    ~Filler() override = default;

    // [INTENT] Returns false — Lightning Infill lines are not self-crossing by design.
    // Tree branches never cross each other because getBestGroundingLocation() checks for
    // polygon collisions and the tree structure prevents topological crossings.
    bool is_self_crossing() override { return false; }

    // [STATE] Non-owning raw pointer to the Generator. Set by PrintObject before use.
    // Must not be nullptr when _fill_surface_single() is called.
    // [HAZARD H267] No null-check inside _fill_surface_single(). If generator is nullptr
    // or stale (PrintObject destroyed), the call is UB.
    Generator* generator{nullptr};

protected:
    // [INTENT] Clone creates a shallow copy of the Filler (including the generator pointer).
    // Used by the Fill factory mechanism. The cloned Filler borrows the same Generator.
    // [HAZARD] Clone shares the raw generator pointer — the clone must not outlive the original.
    Fill* clone() const override { return new Filler(*this); }

    // [INTENT] Main infill generation entry point. Retrieves pre-built tree for layer_id,
    // converts to Polylines, applies Orca multiline offset, clips to expolygon, chains for output.
    // [COUPLING] Calls Generator::getTreesForLayer, Layer::convertToLines, multiline_fill,
    //            intersection_pl, chain_or_connect_infill.
    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;

    // [INTENT] Returns false — the G-code exporter IS allowed to reorder Lightning lines.
    // (contrast: no_sort() == true would prevent reordering, used for pattern-sensitive fills)
    // [UNCLEAR] Typo in original: "reoder" should be "reorder". Left as-is to match original.
    bool no_sort() const override { return false; }
};

} // namespace FillLightning
// [HAZARD H266] Original comment here says `FillAdaptive` — wrong; namespace is FillLightning.
} // namespace Slic3r

#endif // slic3r_FillLightning_hpp_
