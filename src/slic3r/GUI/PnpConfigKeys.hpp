#pragma once

// PNP fork (SchemaBridgeMap ticket 02): turn the `pnp_cli module config-schema`
// reply into `ConfigOptionDef`s registered in Orca's config core, so every pnp
// module key with no Orca counterpart becomes a first-class Orca key.
//
// Wire contract: pnp emits the *whole* key universe, not just module manifests.
// The reply carries `schema` (per-module manifest fields) and `host` (the keys
// pnp's host built-ins read, which no manifest declares — 62 of them at
// submodule dbf3449c). Both are registered; see ticket 01's inventory for why
// the module half alone is not enough.
//
// GUI-free by design so it can be unit tested: no wx, no file IO, no logging.

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r { namespace GUI { namespace PnpConfigKeys {

// Why a schema key was not turned into a registerable def. Reported so drift
// (ticket 06) can name what it skipped rather than dropping it silently.
struct SkippedKey
{
    std::string key;
    std::string reason;
};

// Parse `schema_doc` into defs for every pnp key that nothing else already
// binds.
//
// `already_bound(key)` must return true for a pnp key that some existing path
// already writes: an Orca key of the same name (the identity-routing case) or
// a curated-table target. Those keep their existing route and get no generated
// control — name match, then curated table, then PNP page, in that order.
std::vector<PnpConfigKeyDef> parse_schema(const nlohmann::json& schema_doc,
                                          const std::function<bool(const std::string&)>& already_bound,
                                          std::vector<SkippedKey>* skipped = nullptr);

// Maps a pnp wire type string onto an Orca ConfigOptionType. Returns false for
// a type this fork does not know how to build a control or a default for; the
// key is then skipped rather than registered as the wrong type.
bool map_wire_type(const std::string& wire_type, ConfigOptionType& out);

// Startup path: parse `schema_json` and register what it yields, excluding
// every key the curated translator already routes. Returns the number of keys
// registered. A malformed or empty document registers nothing.
size_t register_from_schema(const std::string& schema_json);

// Keys pnp declares on the wire but the host injects at slice time, so they
// must never render as a user-editable control on the generated PNP page
// (ticket 04; the prototype's skip list).
//
//   slice_has_paint — classic-perimeters.toml declares it; the host writes it
//   into every module config, so the def is real but the value is never the
//   user's to set. Pnp's `internal` wire flag is the delete-this answer (see
//   the map's fog entry); keep this aligned with pnp if a second name appears.
const std::vector<std::string>& pnp_host_injected_skip_keys();

}}} // namespace Slic3r::GUI::PnpConfigKeys
