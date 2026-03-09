// Measure extents of the planned extrusions.
// To be used for collision reporting.
//
// [INTENT] Provides four public query functions that compute axis-aligned bounding boxes
// (in floating-point mm) for different categories of extrusion in a finished Print:
//   1. get_print_extrusions_extents      — skirt/brim projection (used by wipe-tower collision check)
//   2. get_print_object_extrusions_extents — per-object extrusions up to a Z cutoff
//   3. get_wipe_tower_extrusions_extents  — wipe-tower body for layers <= max_print_z
//   4. get_wipe_tower_priming_extrusions_extents — wipe-tower priming strips
//
// [COUPLING] All four functions depend on the Print/PrintObject/WipeTower data having been
// fully populated (i.e. slicing + G-code generation completed).  Calling them on a
// partially-built Print produces empty or incorrect bounding boxes with no error.
//
// [STATE] All functions are pure read-only queries; they carry no mutable state of their own.

#ifndef slic3r_PrintExtents_hpp_
#define slic3r_PrintExtents_hpp_

#include "libslic3r.h"

namespace Slic3r {

class Print;
class PrintObject;
class BoundingBoxf;

// Returns a bounding box of a projection of the brim and skirt.
// [INTENT] BBS note: brim bbox was removed — only skirt is included.
// [HAZARD] If neither skirt nor brim is enabled, returns an undefined (empty) BoundingBoxf
//          with no signal to the caller; callers must check bboxf.defined before use.
BoundingBoxf get_print_extrusions_extents(const Print& print);

// Returns a bounding box of a projection of the object extrusions at z <= max_print_z.
// [INTENT] Used to detect whether any object extrusion overlaps the wipe tower or priming strips.
// [COUPLING] Iterates print_object.layers() and support layers; assumes layer->print_z is
//            sorted ascending (breaks early on first layer exceeding max_print_z).
// [HAZARD] dynamic_cast on every fill entity inside the loop — O(N_layers * N_regions * N_fills)
//          RTTI calls; no virtual dispatch alternative used here.
BoundingBoxf get_print_object_extrusions_extents(const PrintObject& print_object, const coordf_t max_print_z);

// Returns a bounding box of a projection of the wipe tower for the layers <= max_print_z.
// The projection does not contain the priming regions.
// [INTENT] Applies the wipe tower's plate-relative translation + rotation (Eigen Transform2d)
//          to transform tower-local extrusion positions into world coordinates before bounding.
// [COUPLING] Reads wipe_tower_x/y from print.config() via get_at(plate_idx) + plate_origin offset.
//            Depends on wipe_tower_data().tool_changes being sorted by print_z ascending.
BoundingBoxf get_wipe_tower_extrusions_extents(const Print& print, const coordf_t max_print_z);

// Returns a bounding box of the wipe tower priming extrusions.
// [INTENT] Priming extrusions are stored in tower-local coordinates with NO rotation applied
//          (unlike get_wipe_tower_extrusions_extents which applies the Transform2d).
// [HAZARD] Asymmetry with get_wipe_tower_extrusions_extents: priming bbox is in tower-local
//          space; body bbox is in world space.  Callers that compare the two directly will
//          get incorrect collision results unless they also apply the same Transform2d to
//          the priming result.
BoundingBoxf get_wipe_tower_priming_extrusions_extents(const Print& print);

}; // namespace Slic3r

#endif /* slic3r_PrintExtents_hpp_ */
