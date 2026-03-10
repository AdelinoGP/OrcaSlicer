#ifndef slic3r_Brim_hpp_
#define slic3r_Brim_hpp_

// [INTENT] Public API for brim generation.  Brim lines are extruded on the
//          first layer around object footprints to improve adhesion to the bed.
//          OrcaSlicer extends the original PrusaSlicer brim with:
//            - Per-object brim maps (keyed by ObjectID)
//            - Auto-brim width computation (BBS, based on second moment of area)
//            - Painted brim ears (user-placed anchor points)
//            - Auto brim ears (convex/concave vertex detection via SuperSlicer port)
//            - EFC (elephant foot compensation) outline integration (ORCA)
//            - Per-extruder printable-area clipping
//            - Support brim separation from object brim
//
// [COUPLING] Depends on Print, PrintObject, ExtrusionEntityCollection, and the
//            ClipperUtils polygon offset pipeline.  Results are written into
//            caller-supplied maps; the caller (Print::process()) stores them
//            and later serializes them into the G-code first layer.

#include "Point.hpp"

#include <map>
#include <vector>

namespace Slic3r {

class Print;
class ExtrusionEntityCollection;
class PrintTryCancel;
class ObjectID;

// Produce brim lines around those objects, that have the brim enabled.
// Collect islands_area to be merged into the final 1st layer convex hull.
// [INTENT] Main brim entry point called from Print::process().  Populates:
//   - islands_area: union of all object footprints (for skirt/convex hull)
//   - brimMap: per-object ExtrusionEntityCollection of object brims
//   - supportBrimMap: per-object ExtrusionEntityCollection of support brims
//   - objPrintVec: ordered list of (ObjectID, extruder_index) for brim ordering
//   - printExtruders: list of active extruder indices
//
// [CONCURRENCY] Called single-threaded from Print::process(). Internal helper
//          makeBrimInfill uses tbb::parallel_for for polyline chaining.
//
// [STATE] Writes firstLayerObjectBrimBoundingBox into each PrintObject as a
//         side-effect (used later for brim clearance checks and G-code comments).
void make_brim(const Print&                                    print,
               PrintTryCancel                                  try_cancel,
               Polygons&                                       islands_area,
               std::map<ObjectID, ExtrusionEntityCollection>&  brimMap,
               std::map<ObjectID, ExtrusionEntityCollection>&  supportBrimMap,
               std::vector<std::pair<ObjectID, unsigned int>>& objPrintVec,
               std::vector<unsigned int>&                      printExtruders);

// BBS: automatically make brim
// [INTENT] Alternative auto-brim entry point (BBS/Bambu extension).  Used when
//          brim_type == btAutoBrim.  Returns a single ExtrusionEntityCollection
//          with width determined by structural heuristics (second moment of area,
//          height, adhesion coefficient).
// [UNCLEAR → RESOLVED] Auto brim is handled inline in the main brim-generation path when `brim_type == btAutoBrim`, so this standalone declaration has no local definition.
//           — it may be defined elsewhere or is dead/unused code.  (see agent_journal)
ExtrusionEntityCollection make_brim_auto(const Print& print, PrintTryCancel try_cancel, Polygons& islands_area);

} // namespace Slic3r

#endif // slic3r_Brim_hpp_
