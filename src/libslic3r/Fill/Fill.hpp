#ifndef slic3r_Fill_hpp_
#define slic3r_Fill_hpp_

// [INTENT] Top-level public header for the infill subsystem.
// Consumers only need to include this file; FillBase.hpp (and by extension
// all concrete Fill* strategy headers) are transitively pulled in.
// This header also defines the lightweight `Filler` aggregate used as the
// public-facing handle when creating infill fills from outside the module.
// [COUPLING] Includes `PrintConfig.hpp` and `FillBase.hpp`, so any module that includes this
//            header inherits the full infill/config dependency graph rather than a lightweight
//            forward-declared handle.

#include <memory.h>
#include <float.h>
#include <stdint.h>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"

#include "FillBase.hpp"

namespace Slic3r {

class ExtrusionEntityCollection;
class LayerRegion;

// [INTENT] Thin aggregate that pairs a heap-allocated Fill strategy object with
// its FillParams configuration.  Acts as a handle/ownership token so callers
// do not need to manage the Fill* lifetime directly.
//
// [COUPLING] The comment "An interface class to Perl" is a legacy artefact from
// the original Slic3r era when a Perl XS layer instantiated infill objects.
// Perl has been completely removed from OrcaSlicer; the comment no longer
// reflects reality.  `Filler` is now used only from C++ call-sites
// (e.g. Layer::make_fills(), generate_sparse_infill_polylines_for_anchoring()).
//
// [MEMORY] `fill` is a raw owning pointer — manual `delete` in destructor.
// No copy constructor or copy-assignment operator is defined, so the class is
// implicitly copyable (shallow copy), which would cause a double-free if a
// copy were made and both copies were destroyed.  In practice the class is
// only ever stack-allocated or immediately consumed, so this has not triggered
// a bug, but it is a latent hazard.
//
// [HAZARD] H400 — Raw owning pointer + implicit copy semantics.  A refactoring
// to `std::unique_ptr<Fill>` would remove the double-free risk, enforce move
// semantics, and eliminate the manual destructor.
//
// [STATE] Two public data members (no encapsulation):
//   fill   — heap-allocated polymorphic Fill strategy; nullptr until assigned.
//   params — value-type FillParams struct (spacing, density, angle, etc.).
// Caller is responsible for populating both before use.
// [CONCURRENCY] No internal locking. The aggregate is intended to be built, configured, and
//               consumed on a single thread. Copying or sharing one `Filler` across worker
//               threads would race on the mutable `fill` object it owns.
//
// An interface class to Perl, aggregating an instance of a Fill and a FillData.
class Filler
{
public:
    // [INTENT] Default-constructs with a null fill pointer; caller must assign
    // `fill` before calling any Fill methods or undefined behaviour results.
    Filler() : fill(nullptr) {}

    // [MEMORY] Destructor deletes the owned Fill strategy object.
    // Sets pointer to nullptr after delete as a defensive measure, though the
    // object is being destroyed so the assignment has no observable effect.
    ~Filler()
    {
        delete fill;
        fill = nullptr;
    }

    // [STATE] Owning raw pointer to the polymorphic fill strategy.
    // Concrete type is one of: FillRectilinear, FillGyroid, FillLightning, etc.
    // Created by Fill::new_from_type() factory in Fill.cpp.
    Fill* fill;

    // [STATE] Value-type configuration bundle for this fill instance.
    // Includes: spacing, density, angle, dont_adjust, etc.
    // Copied by value — changes after assignment do not propagate to `fill`.
    FillParams params;
};

} // namespace Slic3r

#endif // slic3r_Fill_hpp_
