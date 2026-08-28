#include "PnpBackend.hpp"

#include "GUI_App.hpp"
#include "Plater.hpp"
#include "NotificationManager.hpp"
#include "I18N.hpp"
#include "PnpConfigTranslator.hpp"
#include "PnpConfigWarningsLog.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/format.hpp"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/process.hpp>
#ifdef _WIN32
#include <boost/process/windows.hpp>
#endif

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

namespace {

const char *cli_executable_name()
{
#ifdef _WIN32
    return "pnp_cli.exe";
#else
    return "pnp_cli";
#endif
}

} // anonymous namespace

PnpBackend& PnpBackend::get()
{
    static PnpBackend instance;
    return instance;
}

bool PnpBackend::probe()
{
    m_available      = false;
    m_schema_version.clear();
    m_failure_reason.clear();
    m_schema_json.clear();

    if (resolve_paths() && run_probe())
        m_available = true;

    if (m_available)
        BOOST_LOG_TRIVIAL(info) << "PnpBackend: probe OK, cli=" << m_cli_path.string()
                                << ", schema_version=" << m_schema_version;
    else
        BOOST_LOG_TRIVIAL(error) << "PnpBackend: probe failed: " << m_failure_reason;

    return m_available;
}

bool PnpBackend::resolve_paths()
{
    namespace fs = boost::filesystem;

    fs::path dir;

    // Preferences override wins when it is valid (contains the pnp_cli executable).
    const AppConfig *app_config = wxGetApp().app_config;
    std::string override_dir = app_config != nullptr ? app_config->get(CONFIG_KEY_CLI_DIR) : std::string();
    if (!override_dir.empty()) {
        fs::path candidate = fs::path(override_dir) / cli_executable_name();
        boost::system::error_code ec;
        if (fs::exists(candidate, ec) && !ec) {
            dir = fs::path(override_dir);
        } else {
            BOOST_LOG_TRIVIAL(warning) << "PnpBackend: preferences override \"" << override_dir
                                       << "\" does not contain " << cli_executable_name()
                                       << ", falling back to the application directory";
        }
    }

    // Default: pnp_cli ships flat next to the orca-slicer executable.
    if (dir.empty()) {
        boost::system::error_code ec;
        dir = boost::dll::program_location(ec).parent_path();
        if (ec) {
            m_failure_reason = _u8L("Could not determine the application directory to locate pnp_cli.");
            return false;
        }
    }

    m_cli_path   = dir / cli_executable_name();
    m_module_dir = dir / "modules";

    boost::system::error_code ec;
    if (!fs::exists(m_cli_path, ec) || ec) {
        m_failure_reason = Slic3r::format(_u8L("pnp_cli not found at %1%."), m_cli_path.string());
        return false;
    }
    return true;
}

bool PnpBackend::run_probe()
{
    namespace bp = boost::process;

    std::string output;
    int         exit_code = -1;
    try {
        bp::ipstream out_stream;
        std::vector<std::string> args { "module", "config-schema", "--module-dir", m_module_dir.string() };
        bp::child    child(m_cli_path.string(), bp::args(args),
                           bp::std_out > out_stream, bp::std_err > bp::null, bp::std_in < bp::null,
#ifdef _WIN32
                           bp::windows::create_no_window,
#endif
                           bp::limit_handles);
        std::string line;
        while (out_stream && std::getline(out_stream, line))
            output.append(line).append("\n");
        child.wait();
        exit_code = child.exit_code();
    } catch (const std::exception &ex) {
        m_failure_reason = Slic3r::format(_u8L("Failed to run %1%: %2%"), m_cli_path.string(), ex.what());
        return false;
    }

    if (exit_code != 0) {
        m_failure_reason = Slic3r::format(_u8L("%1% exited with code %2% while probing the module config schema."),
                                  m_cli_path.string(), exit_code);
        return false;
    }

    // Parse the top-level schema_version out of the config-schema JSON.
    try {
        nlohmann::json j = nlohmann::json::parse(output);
        m_schema_version = j.at("schema_version").get<std::string>();
        // Retain the full document for the translator's schema guard.
        m_schema_json = std::move(output);
    } catch (const std::exception &) {
        m_failure_reason = Slic3r::format(_u8L("Could not parse the config schema reported by %1%."), m_cli_path.string());
        return false;
    }

    // Gate on the semver MAJOR component only (pnp wire-contract compatibility rule).
    boost::optional<Semver> ver = Semver::parse(m_schema_version);
    if (!ver) {
        m_failure_reason = Slic3r::format(_u8L("%1% reported an invalid schema version \"%2%\"."),
                                  m_cli_path.string(), m_schema_version);
        return false;
    }
    if (ver->maj() != SUPPORTED_CONFIG_SCHEMA_MAJOR) {
        m_failure_reason = Slic3r::format(_u8L("%1% uses config schema version %2%, but this build supports major version %3%."),
                                  m_cli_path.string(), m_schema_version, SUPPORTED_CONFIG_SCHEMA_MAJOR);
        return false;
    }
    return true;
}

// PNP fork (SchemaBridgeMap ticket 06): reconcile the curated translator table
// against the live schema and report the rows that no longer land.
//
// A curated rename/remap row keeps running after pnp retires the key it writes;
// pnp ignores a key it does not declare, so the Orca setting silently stops
// arriving and nothing says so. The submodule bump produced exactly that with
// `support_density`, retired for `support_base_pattern_spacing`.
//
// Called after each successful probe once a notification manager exists: from
// GUI_App::post_init() at startup (the probe itself runs far earlier, before the
// PresetBundle) and again from the Preferences re-probe. The diff is recomputed
// from the raw document each time rather than stashed at probe time, so pointing
// the fork at a different pnp_cli re-answers the question.
//
// Only the curated rows are diffed. A schema key that is neither an Orca key by
// name nor a table target is, by the map's routing rule, a PNP-page key -- the
// expected case, not drift -- so the inverse direction is deliberately silent.
void report_pnp_schema_drift()
{
    const std::string &schema_json = PnpBackend::get().schema_json();
    if (schema_json.empty())
        return; // no probe: nothing to reconcile against.

    const nlohmann::json doc = nlohmann::json::parse(schema_json, nullptr, false);
    if (doc.is_discarded())
        return;

    // Against a wire-1.0.0 backend the universe is the module half only, and
    // ticket 01 measured 14 live rows that resolve through host keys. Diffing
    // there would report all 14 dead, so it is not a diff worth running.
    if (!PnpConfigTranslator::pnp_schema_reports_host_keys(doc)) {
        BOOST_LOG_TRIVIAL(info)
            << "pnp: config-schema reports no host keys (wire < 1.1.0); skipping drift reconciliation";
        return;
    }

    const std::vector<PnpDeadTarget> dead =
        PnpConfigTranslator::dead_curated_targets(PnpConfigTranslator::pnp_key_universe_from_schema(doc));
    if (dead.empty())
        return;

    log_pnp_dead_curated_targets(dead);

    Plater *plater = wxGetApp().plater();
    if (plater == nullptr || plater->get_notification_manager() == nullptr)
        return;
    // Developer-facing: the user cannot fix a stale translation table, but the
    // consequence is theirs -- these settings do not reach the backend. Important
    // rather than Warning: it stays up long enough to be read without claiming the
    // print is unsafe.
    plater->get_notification_manager()->push_notification(
        NotificationType::CustomNotification,
        NotificationManager::NotificationLevel::ImportantNotificationLevel,
        format_pnp_dead_targets_message(
            dead, _u8L("This build's PNP translation table is out of date with pnp_cli: these settings no longer reach the backend:")));
}

void PnpBackend::show_failure_notification()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    NotificationManager *notification_manager = plater->get_notification_manager();
    if (notification_manager == nullptr)
        return;

    if (m_available) {
        // A re-probe (e.g. after fixing the Preferences override) succeeded; retire the
        // stale failure notification if one is showing.
        if (m_notification_shown) {
            notification_manager->close_notification_of_type(NotificationType::CustomNotification);
            m_notification_shown = false;
        }
        return;
    }

    // ErrorNotificationLevel notifications never fade out, which makes this persistent.
    notification_manager->push_notification(
        NotificationType::CustomNotification,
        NotificationManager::NotificationLevel::ErrorNotificationLevel,
        _u8L("The PNP slicing backend is unavailable; slicing is disabled.") + "\n" + m_failure_reason + "\n" +
            _u8L("Set the \"PNP CLI directory\" in Preferences to the folder containing pnp_cli."));
    m_notification_shown = true;
}

} // namespace GUI
} // namespace Slic3r
