#pragma once

// PNP fork: shared warning record emitted by PnpConfigTranslator (F01) and
// consumed by the config-warnings sink (F03) and the GUI warning notification
// (wayfinder ticket 013, reopened past v1).

#include <string>

namespace Slic3r { namespace GUI {

enum class PnpWarningClass {
    UnsupportedFeature, // orca feature pnp cannot honor
    NotYetMapped,       // key absent from the translation table (Tier-D default)
    NoOp,               // key sent nowhere by design, no behavioral loss
    LossyFallback,      // key sent, but value substituted (e.g. seam aligned->nearest)
    // SchemaBridgeMap ticket 03: a key read from a project or preset that this build
    // cannot resolve -- typically a setting belonging to a pnp module the current
    // install does not have. Kept verbatim and written back on save. Unlike the four
    // classes above this one is emitted at *load* time, not during a slice, so it
    // carries no orca_value/sent_value and never passes through
    // filter_pnp_config_warnings (which needs a resolved full config to work).
    UnresolvedPreserved,
};

inline const char* to_string(PnpWarningClass c)
{
    switch (c) {
    case PnpWarningClass::UnsupportedFeature: return "unsupported-feature";
    case PnpWarningClass::NotYetMapped:       return "not-yet-mapped";
    case PnpWarningClass::NoOp:               return "no-op";
    case PnpWarningClass::LossyFallback:      return "lossy-fallback";
    case PnpWarningClass::UnresolvedPreserved: return "unresolved-preserved";
    }
    return "unknown";
}

struct PnpConfigWarning {
    std::string     key;        // orca config key
    PnpWarningClass warn_class;
    std::string     orca_value;  // serialized orca value
    std::string     sent_value;  // serialized substituted value; empty unless lossy-fallback
};

}} // namespace Slic3r::GUI
