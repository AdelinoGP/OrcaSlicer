#pragma once

// PnpBackend: discovery and version handshake for the external pnp_cli slicing backend.
//
// The pnp backend ships as `pnp_cli` (+ a `modules/` directory beside it) flat next to
// the orca-slicer executable (the drop of pnp's `cargo xtask dist`). An optional
// "PNP CLI directory" preference (AppConfig key "pnp_cli_directory") overrides the
// default location when it is valid.
//
// At startup (and whenever the preference changes) the backend is probed by spawning
// `pnp_cli module config-schema --module-dir <dir>` and parsing the top-level
// `schema_version` from its JSON stdout. Compatibility is gated on the semver MAJOR
// component only (pnp wire-contract rule). On failure, slicing stays disabled
// (available() == false) and one persistent error notification is shown; the rest of
// the GUI remains usable.

#include <string>

#include <boost/filesystem/path.hpp>

namespace Slic3r {
namespace GUI {

class PnpBackend
{
public:
    // The config-schema wire-contract MAJOR this GUI was built against.
    // Same-major = compatible (pnp CONFIG_SCHEMA_WIRE_VERSION rule).
    static constexpr int SUPPORTED_CONFIG_SCHEMA_MAJOR = 1;

    // AppConfig key for the optional Preferences override directory.
    static constexpr const char *CONFIG_KEY_CLI_DIR = "pnp_cli_directory";

    static PnpBackend& get();

    PnpBackend(const PnpBackend&) = delete;
    PnpBackend& operator=(const PnpBackend&) = delete;

    // Resolve the cli path (Preferences override wins when valid, else the directory of
    // the running executable), then run the `module config-schema` probe and update all
    // state below. Safe to call repeatedly (e.g. when the Preferences override changes).
    // Returns available().
    bool probe();

    // True when pnp_cli was found, ran successfully, and its config-schema major matches
    // SUPPORTED_CONFIG_SCHEMA_MAJOR. Consulted before any slice is started.
    bool available() const { return m_available; }

    // Full path to the resolved pnp_cli executable (may point at a non-existing file
    // when discovery failed; see failure_reason()).
    const boost::filesystem::path& cli_path() const { return m_cli_path; }

    // Module directory, always <dir containing pnp_cli>/modules.
    const boost::filesystem::path& module_dir() const { return m_module_dir; }

    // Full `schema_version` string reported by the probe (empty if the probe never ran
    // or its output could not be parsed).
    const std::string& schema_version() const { return m_schema_version; }

    // Raw `module config-schema` JSON document from the last successful probe
    // (empty when unavailable). Consumed by PnpConfigTranslator's schema guard.
    const std::string& schema_json() const { return m_schema_json; }

    // Human-readable reason available() is false (empty when available() is true).
    const std::string& failure_reason() const { return m_failure_reason; }

    // Push one persistent error notification describing failure_reason() when the
    // backend is unavailable; close a previously pushed one when it became available.
    // No-op outside failure/recovery. Requires the Plater to exist.
    void show_failure_notification();

private:
    PnpBackend() = default;

    // Resolve m_cli_path / m_module_dir from the AppConfig override or the exe dir.
    // Returns false (with m_failure_reason set) when no pnp_cli executable exists there.
    bool resolve_paths();

    // Spawn `pnp_cli module config-schema --module-dir <m_module_dir>` and parse the
    // top-level schema_version out of its stdout. Returns false with m_failure_reason
    // set on spawn failure, non-zero exit, unparsable output, or major mismatch.
    bool run_probe();

    bool                    m_available { false };
    boost::filesystem::path m_cli_path;
    boost::filesystem::path m_module_dir;
    std::string             m_schema_version;
    std::string             m_failure_reason;
    std::string             m_schema_json;
    bool                    m_notification_shown { false };
};

} // namespace GUI
} // namespace Slic3r
