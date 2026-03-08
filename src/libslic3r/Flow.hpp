#ifndef slic3r_Flow_hpp_
#define slic3r_Flow_hpp_

#include "libslic3r.h"
#include "Config.hpp"
#include "Exception.hpp"
#include "ExtrusionEntity.hpp"

namespace Slic3r {

// [INTENT] Forward declaration – avoids including Print.hpp here; only pointers to PrintObject used.
class PrintObject;

// [INTENT] Extra spacing added between adjacent bridge threads to prevent fusion.
// [HAZARD H551] Value is in mm, hardcoded at 50 µm. Not scaled to coord_t.
//               Any port that uses scaled integers throughout CANNOT use this macro directly;
//               must multiply by SCALING_FACTOR (1e6) before comparison with coord_t values.
// Extra spacing of bridge threads, in mm.
#define BRIDGE_EXTRA_SPACING 0.05

// [INTENT] Enumerates all logical roles for extrusions; used to select the correct
//          line-width config option and flow defaults throughout the slicing pipeline.
// [COUPLING] Values are used as array indices in some lookup tables — reordering corrupts lookups.
// [HAZARD] frSupportTransition (last entry) was added by BBS (Bambu); upstream PrusaSlicer
//          does not have it. Any merge from upstream must preserve this ordering.
enum FlowRole {
    frExternalPerimeter,
    frPerimeter,
    frInfill,
    frSolidInfill,
    frTopSolidInfill,
    frSupportMaterial,
    frSupportMaterialInterface,
    frSupportTransition, // BBS
};

// [INTENT] Base exception for all flow-calculation errors; derives from InvalidArgument
//          so callers can catch the broad category if they don't need specifics.
class FlowError : public Slic3r::InvalidArgument
{
public:
    FlowError(const std::string& what_arg) : Slic3r::InvalidArgument(what_arg) {}
    FlowError(const char* what_arg) : Slic3r::InvalidArgument(what_arg) {}
};

// [INTENT] Thrown when rounded_rectangle_extrusion_spacing() returns ≤ 0.
//          Indicates user-configured extrusion width is smaller than layer height (physically impossible).
class FlowErrorNegativeSpacing : public FlowError
{
public:
    FlowErrorNegativeSpacing();
};

// [INTENT] Thrown when mm3_per_mm() returns ≤ 0 (cross-section area is non-positive).
//          Same root cause as FlowErrorNegativeSpacing but caught at a different call site.
class FlowErrorNegativeFlow : public FlowError
{
public:
    FlowErrorNegativeFlow();
};

// [INTENT] Thrown when a required config key (e.g. "nozzle_diameter", "line_width") is absent
//          from the config resolver when computing extrusion widths.
class FlowErrorMissingVariable : public FlowError
{
public:
    FlowErrorMissingVariable(const std::string& what_arg) : FlowError(what_arg) {}
};

// [INTENT] Immutable value type representing the physical cross-section of a single extrusion pass.
//          Encapsulates: width (mm), height/layer-height (mm), center-to-center spacing (mm),
//          nozzle diameter (mm), and a bridge flag.
// [STATE] All fields are floats (mm). No coord_t / scaled integer inside this class.
//         Callers use scaled_width() / scaled_spacing() helpers when passing to Clipper.
// [MEMORY] Value semantics — copied freely, no heap allocation, no virtual dispatch.
// [CONCURRENCY] Immutable after construction (except set_spacing). Safe to share across threads
//               as long as set_spacing() is not called concurrently (no mutex).
// [HAZARD H549] operator== ignores m_spacing. Two Flow objects with identical w/h/nozzle/bridge
//               but different spacing (via set_spacing) compare equal yet produce different G-code.
//               Containers keyed on Flow equality (e.g. std::set, hash maps) will alias them.
class Flow
{
public:
    Flow() = default;
    // [INTENT] Convenience constructor for non-bridge flows; spacing is derived automatically.
    Flow(float width, float height, float nozzle_diameter)
        : Flow(width, height, rounded_rectangle_extrusion_spacing(width, height), nozzle_diameter, false)
    {}

    // Non bridging flow: Maximum width of an extrusion with semicircles at the ends.
    // Bridging flow: Bridge thread diameter.
    float width() const { return m_width; }
    // [INTENT] Returns width in scaled Clipper integer coordinates (factor 1e6).
    coord_t scaled_width() const { return coord_t(scale_(m_width)); }
    // Non bridging flow: Layer height.
    // Bridging flow: Bridge thread diameter = layer height.
    float height() const { return m_height; }
    // Spacing between the extrusion centerlines.
    float spacing() const { return m_spacing; }
    // [HAZARD H549] Mutating spacing without updating width breaks the invariant
    //               spacing = width - height*(1 - π/4). Use only for special overrides.
    void    set_spacing(float spacing) { m_spacing = spacing; }
    coord_t scaled_spacing() const { return coord_t(scale_(m_spacing)); }
    // Nozzle diameter.
    float nozzle_diameter() const { return m_nozzle_diameter; }
    // Is it a bridge?
    bool bridge() const { return m_bridge; }
    // [INTENT] Cross section area (mm²) of one mm of extrusion travel.
    //          For bridges: circle (πr²). For normal: rounded rectangle.
    // [HAZARD] Throws FlowErrorNegativeFlow if result ≤ 0.
    double mm3_per_mm() const;

    // [INTENT] Computes outward expansion of the first perimeter for elephant-foot compensation.
    //          The formula blends 50% of width with 30% of spacing — empirically tuned.
    // Elephant foot compensation spacing to be used to detect narrow parts, where the elephant foot compensation cannot be applied.
    // To be used on frExternalPerimeter only.
    // Enable some perimeter squish (see INSET_OVERLAP_TOLERANCE).
    // Here an overlap of 0.2x external perimeter spacing is allowed for by the elephant foot compensation.
    coord_t scaled_elephant_foot_spacing() const { return coord_t(0.5f * float(this->scaled_width() + 0.6f * this->scaled_spacing())); }

    // [HAZARD H549] m_spacing intentionally omitted from equality — see class comment.
    bool operator==(const Flow& rhs) const
    {
        return m_width == rhs.m_width && m_height == rhs.m_height && m_nozzle_diameter == rhs.m_nozzle_diameter && m_bridge == rhs.m_bridge;
    }

    bool operator!=(const Flow& rhs) const
    {
        return m_width != rhs.m_width || m_height != rhs.m_height || m_nozzle_diameter != rhs.m_nozzle_diameter || m_bridge != rhs.m_bridge;
    }

    // [INTENT] Ordering by volumetric flow rate; used for sorting extrusion roles by flow amount.
    bool operator<(const Flow& rhs) const { return this->mm3_per_mm() < rhs.mm3_per_mm(); }

    // [INTENT] Return a copy with a different width; spacing is recomputed from the new width.
    //          Asserts non-bridge (bridge flows use diameter, not separate w/h).
    Flow with_width(float width) const
    {
        assert(!m_bridge);
        return Flow(width, m_height, rounded_rectangle_extrusion_spacing(width, m_height), m_nozzle_diameter, m_bridge);
    }
    // [INTENT] Return a copy with a different height; spacing is recomputed from the new height.
    Flow with_height(float height) const
    {
        assert(!m_bridge);
        return Flow(m_width, height, rounded_rectangle_extrusion_spacing(m_width, height), m_nozzle_diameter, m_bridge);
    }
    // [INTENT] Return a copy with a different center-to-center spacing, adjusting width to match.
    // Adjust extrusion flow for new extrusion line spacing, maintaining the old spacing between extrusions.
    Flow with_spacing(float spacing) const;
    // [INTENT] Return a copy whose cross-section area equals `area`, maintaining the same spacing.
    //          Used by gap-fill and flow-ratio adjustments.
    // [HAZARD H550] Increasing-flow branch: when new_full_spacing > m_spacing the comment says
    //               "filling up the spacing without an air gap" and grows height, but the else-arm
    //               uses `area / m_height` (the OLD area) — likely a latent copy-paste error.
    // Adjust the width / height of a rounded extrusion model to reach the prescribed cross section area while maintaining extrusion spacing.
    Flow with_cross_section(float area) const;

    // [INTENT] Convenience wrapper: scale flow by a dimensionless ratio (e.g. 0.9 = 90% flow).
    Flow with_flow_ratio(double ratio) const { return this->with_cross_section(this->mm3_per_mm() * ratio); }

    // [INTENT] Factory for bridge flows: diameter = height = dmr, spacing = dmr + BRIDGE_EXTRA_SPACING.
    static Flow bridging_flow(float dmr, float nozzle_diameter)
    {
        return Flow{dmr, dmr, bridge_extrusion_spacing(dmr), nozzle_diameter, true};
    }

    // [INTENT] Primary factory used by slicing pipeline: constructs from a config width option
    //          (which may be 0 = auto, a % of nozzle, or an absolute mm value).
    static Flow new_from_config_width(FlowRole role, const ConfigOptionFloatOrPercent& width, float nozzle_diameter, float height);

    // [INTENT] Closed-form formula: spacing = width - height*(1 - π/4).
    //          Derived from area conservation of the rounded-rectangle cross-section.
    // Spacing of extrusions with rounded extrusion model.
    static float rounded_rectangle_extrusion_spacing(float width, float height);
    // [INTENT] Inverse of rounded_rectangle_extrusion_spacing: given spacing → width.
    // Width of extrusions with rounded extrusion model.
    static float rounded_rectangle_extrusion_width_from_spacing(float spacing, float height);
    // [INTENT] Bridge spacing = diameter + BRIDGE_EXTRA_SPACING (50 µm gap to prevent fusion).
    // [HAZARD H551] BRIDGE_EXTRA_SPACING is raw mm; see macro comment.
    // Spacing of round thread extrusions.
    static float bridge_extrusion_spacing(float dmr);

    // [INTENT] Returns a heuristic default line width when user leaves the config at 0.
    //          Support/top-solid roles → 1.0× nozzle; everything else → 1.125× nozzle.
    // Sane extrusion width default based on nozzle diameter.
    // The defaults were derived from manual Prusa MK3 profiles.
    static float auto_extrusion_width(FlowRole role, float nozzle_diameter);

    // [INTENT] Resolves an extrusion-width config option to an absolute mm value for display/hints.
    //          Falls back through: role-specific opt → "line_width" → auto_extrusion_width().
    // [HAZARD H552] A dead #if 0 block was intended to handle initial_layer_line_width == 0 by
    //               falling back to inner_wall_line_width; currently this case falls through to
    //               "line_width" instead, which may differ from what user intended.
    // Extrusion width from full config, taking into account the defaults (when set to zero) and ratios (percentages).
    // Precise value depends on layer index (1st layer vs. other layers vs. variable layer height),
    // on active extruder etc. Therefore the value calculated by this function shall be used as a hint only.
    static double extrusion_width(const std::string&                opt_key,
                                  const ConfigOptionFloatOrPercent* opt,
                                  const ConfigOptionResolver&       config,
                                  const unsigned int                first_printing_extruder = 0);
    static double extrusion_width(const std::string&          opt_key,
                                  const ConfigOptionResolver& config,
                                  const unsigned int          first_printing_extruder = 0);

private:
    // [INTENT] Full constructor; private to enforce invariants (spacing must be derived or
    //          explicitly overridden). Public factories always go through this.
    Flow(float width, float height, float spacing, float nozzle_diameter, bool bridge)
        : m_width(width), m_height(height), m_spacing(spacing), m_nozzle_diameter(nozzle_diameter), m_bridge(bridge)
    {
        // Gap fill violates this condition.
        // assert(width >= height);
    }

    // [STATE] All four geometry fields are floats in mm. Zero-initialized by default.
    //         m_spacing is NOT always derivable from m_width/m_height (set_spacing() can override).
    float m_width{0};
    float m_height{0};
    float m_spacing{0};
    float m_nozzle_diameter{0};
    bool  m_bridge{false};
};

// [INTENT] Free functions that construct Flow for support material roles, reading from PrintObject config.
// [COUPLING] All four functions call object->print()->config() and object->config() — tightly coupled
//            to the PrintObject/Print hierarchy. Cannot be called without a full PrintObject.
// [HAZARD H548] support_material_flow and support_material_1st_layer_flow use
//               `support_filament - 1` as nozzle_diameter index. If support_filament == 0
//               (meaning "use current extruder, no tool change"), get_at(-1) wraps to SIZE_MAX
//               which the current get_at() implementation clamps to index 0 by design.
//               Any refactoring of get_at() that removes this clamping behavior will produce
//               an out-of-bounds read. The comment in the source acknowledges this but relies
//               on the undocumented clamping behavior of get_at().
extern Flow support_material_flow(const PrintObject* object, float layer_height = 0.f);
// [INTENT] BBS addition: creates a bridging flow for support transition layers (tree support).
//          Always returns bridging_flow(nozzle_diameter, nozzle_diameter) — spacing computed from dmr.
// [HAZARD H548] Same support_filament - 1 index hazard as support_material_flow above.
extern Flow support_transition_flow(const PrintObject* object); // BBS
extern Flow support_material_1st_layer_flow(const PrintObject* object, float layer_height = 0.f);
extern Flow support_material_interface_flow(const PrintObject* object, float layer_height = 0.f);

} // namespace Slic3r

#endif
