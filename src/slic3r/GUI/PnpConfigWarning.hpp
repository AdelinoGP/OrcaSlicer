#pragma once

// PNP fork: shared warning record emitted by PnpConfigTranslator (F01) and
// consumed by the config-warnings sink (F03). Warnings are a dev instrument
// only — they are never surfaced in the UI (wayfinder ticket 013).

#include <string>

namespace Slic3r { namespace GUI {

enum class PnpWarningClass {
    UnsupportedFeature, // orca feature pnp cannot honor
    NotYetMapped,       // key absent from the translation table (Tier-D default)
    NoOp,               // key sent nowhere by design, no behavioral loss
    LossyFallback,      // key sent, but value substituted (e.g. seam aligned->nearest)
};

inline const char* to_string(PnpWarningClass c)
{
    switch (c) {
    case PnpWarningClass::UnsupportedFeature: return "unsupported-feature";
    case PnpWarningClass::NotYetMapped:       return "not-yet-mapped";
    case PnpWarningClass::NoOp:               return "no-op";
    case PnpWarningClass::LossyFallback:      return "lossy-fallback";
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
