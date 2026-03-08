#include "Flow.hpp"
#include "I18N.hpp"
#include "Print.hpp"
#include <cmath>
#include <assert.h>

#include <boost/algorithm/string/predicate.hpp>

// Mark string for localization and translate.
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

// [INTENT] Error constructors produce user-readable messages with localized strings.
//          Centralized here so callers throw without constructing the message inline.
FlowErrorNegativeSpacing::FlowErrorNegativeSpacing()
    : FlowError("Flow::spacing() produced negative spacing. Did you set some extrusion width too small?")
{}

FlowErrorNegativeFlow::FlowErrorNegativeFlow()
    : FlowError("Flow::mm3_per_mm() produced negative flow. Did you set some extrusion width too small?")
{}

// [INTENT] Returns a heuristic default line width for each role when the user leaves width at 0.
//          Support/interface/transition/top-solid → 1.0× nozzle (narrower = better support quality).
//          Perimeter/infill/solid → 1.125× nozzle (slight overextrusion improves adhesion).
// [COUPLING] FlowRole enum values must stay in sync with this switch; adding a role without
//            a case here silently falls through to the default (1.125× nozzle).
// This static method returns a sane extrusion width default.
float Flow::auto_extrusion_width(FlowRole role, float nozzle_diameter)
{
    switch (role) {
    case frSupportMaterial:
    case frSupportMaterialInterface:
    case frSupportTransition:
    case frTopSolidInfill: return nozzle_diameter;
    default:
    case frExternalPerimeter:
    case frPerimeter:
    case frSolidInfill:
    case frInfill: return 1.125f * nozzle_diameter;
    }
}

// [INTENT] Internal helper: maps config option key strings to FlowRole enum values.
//          Used only by extrusion_width() to enable auto-width fallback per role.
// [COUPLING] Key strings must exactly match the config option names in PrintConfig.hpp.
//            If a config key is renamed, this mapping silently throws at runtime.
// [HAZARD] Throws RuntimeError for any unknown opt_key — this is a programming error,
//          not a user error; it will terminate the slice with an internal error message.
// Used by the Flow::extrusion_width() funtion to provide hints to the user on default extrusion width values,
// and to provide reasonable values to the PlaceholderParser.
static inline FlowRole opt_key_to_flow_role(const std::string& opt_key)
{
    if (opt_key == "inner_wall_line_width" ||
        // or all the defaults:
        opt_key == "line_width" || opt_key == "initial_layer_line_width")
        return frPerimeter;
    else if (opt_key == "outer_wall_line_width")
        return frExternalPerimeter;
    else if (opt_key == "sparse_infill_line_width")
        return frInfill;
    else if (opt_key == "internal_solid_infill_line_width")
        return frSolidInfill;
    else if (opt_key == "top_surface_line_width")
        return frTopSolidInfill;
    else if (opt_key == "support_line_width")
        return frSupportMaterial;
    else
        throw Slic3r::RuntimeError("opt_key_to_flow_role: invalid argument");
};

// [INTENT] Helper for extrusion_width(): throws FlowErrorMissingVariable with a localizable
//          message naming both the requesting opt_key and the missing dependent option.
static inline void throw_on_missing_variable(const std::string& opt_key, const char* dependent_opt_key)
{
    throw FlowErrorMissingVariable(
        (boost::format(L("Failed to calculate line width of %1%. Cannot get value of \"%2%\" ")) % opt_key % dependent_opt_key).str());
}

// [INTENT] Resolves a config option to an absolute extrusion width in mm.
//          Resolution order:
//            1. If opt->value == 0, substitute "line_width" (the global default).
//            2. If still 0, call auto_extrusion_width() for the role-based heuristic.
//            3. If percent, convert relative to nozzle_diameter.
//            4. Otherwise return the literal float value.
// [HAZARD H552] Dead #if 0 block (lines 70-78): was intended to handle initial_layer_line_width == 0
//               by substituting "inner_wall_line_width" first. Disabled by #if 0.
//               Current behavior: initial_layer_line_width == 0 falls through to "line_width",
//               which may differ from user expectation (they may expect inner_wall_line_width width).
// [COUPLING] Requires both "line_width" and "nozzle_diameter" to exist in config resolver;
//            throws FlowErrorMissingVariable if either is absent.
// Used to provide hints to the user on default extrusion width values, and to provide reasonable values to the PlaceholderParser.
double Flow::extrusion_width(const std::string&                opt_key,
                             const ConfigOptionFloatOrPercent* opt,
                             const ConfigOptionResolver&       config,
                             const unsigned int                first_printing_extruder)
{
    assert(opt != nullptr);

#if 0
// This is the logic used for skit / brim, but not for the rest of the 1st layer.
	if (opt->value == 0. && first_layer) {
		// The "initial_layer_line_width" was set to zero, try a substitute.
		opt = config.option<ConfigOptionFloatOrPercent>("inner_wall_line_width");
		if (opt == nullptr)
    		throw_on_missing_variable(opt_key, "inner_wall_line_width");
	}
#endif

    if (opt->value == 0.) {
        // The role specific extrusion width value was set to zero, try the role non-specific extrusion width.
        opt = config.option<ConfigOptionFloatOrPercent>("line_width");
        if (opt == nullptr)
            throw_on_missing_variable(opt_key, "line_width");
    }

    auto opt_nozzle_diameters = config.option<ConfigOptionFloats>("nozzle_diameter");
    if (opt_nozzle_diameters == nullptr)
        throw_on_missing_variable(opt_key, "nozzle_diameter");

    if (opt->percent) {
        return opt->get_abs_value(float(opt_nozzle_diameters->get_at(first_printing_extruder)));
    }

    if (opt->value == 0.) {
        // If user left option to 0, calculate a sane default width.
        return auto_extrusion_width(opt_key_to_flow_role(opt_key), float(opt_nozzle_diameters->get_at(first_printing_extruder)));
    }

    return opt->value;
}

// [INTENT] Overload convenience: looks up opt by key before delegating to the 4-arg version.
// Used to provide hints to the user on default extrusion width values, and to provide reasonable values to the PlaceholderParser.
double Flow::extrusion_width(const std::string& opt_key, const ConfigOptionResolver& config, const unsigned int first_printing_extruder)
{
    return extrusion_width(opt_key, config.option<ConfigOptionFloatOrPercent>(opt_key), config, first_printing_extruder);
}

// [INTENT] Primary factory for non-bridge flows used throughout the slicing pipeline.
//          Resolves config option (0 = auto, % = relative, >0 = absolute) to a concrete
//          Flow object via auto_extrusion_width() or get_abs_value() as appropriate.
// [HAZARD] Throws Slic3r::InvalidArgument if height <= 0.
//          Throws FlowErrorNegativeSpacing if the resulting w/h ratio produces negative spacing.
// This constructor builds a Flow object from an extrusion width config setting
// and other context properties.
Flow Flow::new_from_config_width(FlowRole role, const ConfigOptionFloatOrPercent& width, float nozzle_diameter, float height)
{
    if (height <= 0)
        throw Slic3r::InvalidArgument("Invalid flow height supplied to new_from_config_width()");

    float w;
    if (!width.percent && width.value <= 0.) {
        // If user left option to 0, calculate a sane default width.
        w = auto_extrusion_width(role, nozzle_diameter);
    } else {
        // If user set a manual value, use it.
        w = float(width.get_abs_value(nozzle_diameter));
    }

    return Flow(w, height, rounded_rectangle_extrusion_spacing(w, height), nozzle_diameter, false);
}

// [INTENT] Returns a new Flow with a different center-to-center spacing while keeping
//          the air-gap (spacing - width) constant. For bridges, adjusts the thread diameter.
// [STATE] m_spacing is updated; for non-bridge m_width changes by the same delta as spacing.
//         For bridge, m_width == m_height == new_diameter.
// [HAZARD] Throws InvalidArgument if the resulting width would be less than height
//          (physically impossible for the rounded-rectangle model).
// Adjust extrusion flow for new extrusion line spacing, maintaining the old spacing between extrusions.
Flow Flow::with_spacing(float new_spacing) const
{
    Flow out = *this;
    if (m_bridge) {
        // Diameter of the rounded extrusion.
        assert(m_width == m_height);
        float gap          = m_spacing - m_width;
        auto  new_diameter = new_spacing - gap;
        out.m_width = out.m_height = new_diameter;
    } else {
        assert(m_width >= m_height);
        out.m_width += new_spacing - m_spacing;
        if (out.m_width < out.m_height)
            throw Slic3r::InvalidArgument(
                L("Invalid spacing supplied to Flow::with_spacing(), check your layer height and extrusion width"));
    }
    out.m_spacing = new_spacing;
    return out;
}

// [INTENT] Adjusts width (and/or height) to achieve a target cross-section area while
//          keeping m_spacing fixed. Used for gap fill and flow-ratio overrides.
// [STATE] Three possible outcomes depending on area_new vs area:
//   1. area_new ≈ area (within EPSILON) → returns *this unchanged.
//   2. area_new > area: increase flow.
//      a. new_full_spacing (= area_new/m_height) > m_spacing: no air gap → grow height too.
//      b. new_full_spacing <= m_spacing: widen within existing spacing band.
//   3. area_new < area: decrease flow.
//      a. width_new > m_height: shrink width.
//      b. width_new <= m_height: degenerate to circular cross-section (gap fill).
// [HAZARD H550] In branch 2b (increasing, but new_full_spacing <= m_spacing), the code calls
//               with_width(rounded_rectangle_extrusion_width_from_spacing(area / m_height, m_height))
//               using the OLD area, not area_new. This appears to be a latent bug —
//               when area is increasing but the new spacing fits, the width returned is
//               computed from the original area, not the requested area_new.
// [UNCLEAR] Comment says "filling up the spacing without an air gap" but the condition
//           new_full_spacing > m_spacing is the case where NEW full spacing EXCEEDS current
//           spacing — i.e. the extrusion would normally require an air gap. The else branch
//           (new_full_spacing <= m_spacing) is the case where it fits. Comment is inverted.
// Adjust the width / height of a rounded extrusion model to reach the prescribed cross section area while maintaining extrusion spacing.
Flow Flow::with_cross_section(float area_new) const
{
    assert(!m_bridge);
    assert(m_width >= m_height);

    // Adjust for bridge_flow, maintain the extrusion spacing.
    float area = this->mm3_per_mm();
    if (area_new > area + EPSILON) {
        // Increasing the flow rate.
        float new_full_spacing = area_new / m_height;
        if (new_full_spacing > m_spacing) {
            // Filling up the spacing without an air gap. Grow the extrusion in height.
            float height = area_new / m_spacing;
            return Flow(rounded_rectangle_extrusion_width_from_spacing(m_spacing, height), height, m_spacing, m_nozzle_diameter, false);
        } else {
            return this->with_width(rounded_rectangle_extrusion_width_from_spacing(area / m_height, m_height));
        }
    } else if (area_new < area - EPSILON) {
        // Decreasing the flow rate.
        float width_new = m_width - (area - area_new) / m_height;
        assert(width_new > 0);
        if (width_new > m_height) {
            // Shrink the extrusion width.
            return this->with_width(width_new);
        } else {
            // Create a rounded extrusion.
            auto dmr = float(sqrt(area_new / M_PI));
            return Flow(dmr, dmr, m_spacing, m_nozzle_diameter, false);
        }
    } else
        return *this;
}

// [INTENT] Core geometric formula: spacing = width - height*(1 - π/4).
//          Derived from area conservation: rectangle (w*h) minus two semicircles (π*(h/2)²).
//          The π/4 ≈ 0.785 factor comes from the ratio of circle to square areas.
// [HAZARD] Throws FlowErrorNegativeSpacing if result ≤ 0 (physically: width < height*(1 - π/4)).
//          This can be triggered by user setting extrusion width too small relative to layer height.
float Flow::rounded_rectangle_extrusion_spacing(float width, float height)
{
    auto out = width - height * float(1. - 0.25 * PI);
    if (out <= 0.f)
        throw FlowErrorNegativeSpacing();
    return out;
}

// [INTENT] Inverse of rounded_rectangle_extrusion_spacing: given spacing → width.
//          width = spacing + height*(1 - π/4).
float Flow::rounded_rectangle_extrusion_width_from_spacing(float spacing, float height)
{
    return float(spacing + height * (1. - 0.25 * PI));
}

// [INTENT] Bridge thread spacing = diameter + BRIDGE_EXTRA_SPACING (50 µm).
//          The gap prevents adjacent bridge threads from fusing into a solid sheet
//          before cooling, which would increase sag.
// [HAZARD H551] BRIDGE_EXTRA_SPACING is raw mm (0.05). If porting to a system using
//               scaled integer coordinates, this value must be multiplied by SCALING_FACTOR.
float Flow::bridge_extrusion_spacing(float dmr) { return dmr + BRIDGE_EXTRA_SPACING; }

// [INTENT] Returns volumetric flow rate per unit of XY travel (mm³/mm).
//          Two formulas:
//            Bridge: circle cross-section: π*(w/2)² = π*w²/4
//            Normal: rounded rectangle: h*(w - h*(1 - π/4))
//          The normal formula is the area of a rectangle (w*h) minus two semicircle ends (π*(h/2)²).
// [HAZARD] Throws FlowErrorNegativeFlow if result ≤ 0. Same root cause as FlowErrorNegativeSpacing
//          but checked here rather than in the spacing formula (defensive).
// [UNCLEAR] The commented-out assert (line 208) was disabled — likely because gap fill
//           intentionally creates flows that would trigger it in some edge cases.
// This method returns extrusion volume per head move unit.
double Flow::mm3_per_mm() const
{
    float res = m_bridge ?
                    // Area of a circle with dmr of this->width.
                    float((m_width * m_width) * 0.25 * PI) :
                    // Rectangle with semicircles at the ends. ~ h (w - 0.215 h)
                    float(m_height * (m_width - m_height * (1. - 0.25 * PI)));
    // assert(res > 0.);
    if (res <= 0.)
        throw FlowErrorNegativeFlow();
    return res;
}

// [INTENT] Constructs a Flow for support material extrusion with the correct nozzle/width
//          from the object's support config options.
// [COUPLING] Reads both object->config() (PrintObjectConfig) and object->print()->config()
//            (PrintConfig); cannot be called without a fully configured PrintObject.
// [HAZARD H548] `support_filament - 1` is used as the nozzle_diameter array index.
//               When support_filament == 0 (meaning "use current extruder, no tool change"),
//               the expression evaluates to -1 (wraps to SIZE_MAX for size_t).
//               The inline comment acknowledges this: "get_at will return the 0th component."
//               This relies on get_at()'s internal clamping of out-of-bounds indices to 0.
//               Any reimplementation of get_at() without that clamping will read out-of-bounds.
Flow support_material_flow(const PrintObject* object, float layer_height)
{
    return Flow::new_from_config_width(frSupportMaterial,
                                       // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the
                                       // Flow class takes care of the percent to value substitution.
                                       (object->config().support_line_width.value > 0) ? object->config().support_line_width :
                                                                                         object->config().line_width,
                                       // if object->config().support_filament == 0 (which means to not trigger tool change, but use the
                                       // current extruder instead), get_at will return the 0th component.
                                       float(object->print()->config().nozzle_diameter.get_at(object->config().support_filament - 1)),
                                       (layer_height > 0.f) ? layer_height : float(object->config().layer_height.value));
}

// [INTENT] BBS addition: returns a bridging flow for tree-support transition layers.
//          A bridging flow uses a circular cross-section with diameter == nozzle_diameter,
//          and spacing == diameter + BRIDGE_EXTRA_SPACING.
// [HAZARD H548] Same support_filament - 1 index hazard as support_material_flow above.
// [UNCLEAR] Why does support transition always use bridging flow? The tree support transition
//           connects support pillars to the model surface; bridging flow (no height compression)
//           may be chosen to avoid crushing the filament against the model.
// BBS
Flow support_transition_flow(const PrintObject* object)
{
    // BBS: support transition of tree support is bridge flow
    float dmr = float(object->print()->config().nozzle_diameter.get_at(object->config().support_filament - 1));
    return Flow::bridging_flow(dmr, dmr);
}

// [INTENT] Constructs support material flow for the first layer specifically.
//          Prefers initial_layer_line_width over support_line_width for the first layer
//          to ensure good bed adhesion of support structures.
// [HAZARD H548] Same support_filament - 1 index hazard (get_at with SIZE_MAX wraps to 0).
// [COUPLING] Reads from print_config (global) and object->config() (per-object) and
//            falls back through three width options in priority order:
//            initial_layer_line_width → support_line_width → line_width.
Flow support_material_1st_layer_flow(const PrintObject* object, float layer_height)
{
    const PrintConfig& print_config = object->print()->config();
    const auto&        width        = (print_config.initial_layer_line_width.value > 0) ? print_config.initial_layer_line_width :
                                                                                          object->config().support_line_width;
    return Flow::new_from_config_width(frSupportMaterial,
                                       // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the
                                       // Flow class takes care of the percent to value substitution.
                                       (width.value > 0) ? width : object->config().line_width,
                                       float(print_config.nozzle_diameter.get_at(object->config().support_filament - 1)),
                                       (layer_height > 0.f) ? layer_height : float(print_config.initial_layer_print_height.value));
}

// [INTENT] Constructs flow for support interface layers (the dense top/bottom contact layers
//          between support and model). Uses support_interface_filament for extruder selection.
// [HAZARD H548] `support_interface_filament - 1` has the same off-by-one hazard as
//               support_material_flow: index -1 wraps to SIZE_MAX, clamped to 0 by get_at().
// [COUPLING] Width resolution: support_line_width (if > 0) else line_width (object default).
Flow support_material_interface_flow(const PrintObject* object, float layer_height)
{
    return Flow::new_from_config_width(frSupportMaterialInterface,
                                       // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the
                                       // Flow class takes care of the percent to value substitution.
                                       (object->config().support_line_width > 0) ? object->config().support_line_width :
                                                                                   object->config().line_width,
                                       // if object->config().support_interface_filament == 0 (which means to not trigger tool change, but
                                       // use the current extruder instead), get_at will return the 0th component.
                                       float(object->print()->config().nozzle_diameter.get_at(object->config().support_interface_filament -
                                                                                              1)),
                                       (layer_height > 0.f) ? layer_height : float(object->config().layer_height.value));
}

} // namespace Slic3r
