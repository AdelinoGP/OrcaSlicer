// PNP fork: config-warnings sink implementation. See PnpConfigWarningsLog.hpp.

#include "PnpConfigWarningsLog.hpp"

#include <fstream>
#include <set>
#include <string>

#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r { namespace GUI {

void log_pnp_config_warnings(const DynamicPrintConfig&      full,
                             std::vector<PnpConfigWarning>&& warnings,
                             int                             plate)
{
    // Keys whose value differs from the Orca defaults. full_config() returns
    // every key with defaults filled in, so without this filter every one of
    // the ~830 not-yet-mapped keys would be recorded on every slice
    // (ticket 013). The filter is Tier-D-only: lossy-fallback records are
    // only emitted when the translator actually took a fallback branch (and
    // Orca's default seam_position=spAligned is itself the loss), and
    // unsupported-feature / no-op records always pass.
    // FullPrintConfig is a StaticConfig; DynamicConfig::diff(const DynamicConfig&)
    // shadows the ConfigBase overload, so qualify to reach it.
    const t_config_option_keys changed = full.ConfigBase::diff(FullPrintConfig::defaults());
    const std::set<std::string> changed_keys(changed.begin(), changed.end());

    const std::string ts = Utils::utc_timestamp();

    size_t count_unsupported = 0, count_not_mapped = 0, count_no_op = 0, count_lossy = 0;
    size_t dropped = 0;

    std::string       path = data_dir() + "/pnp-config-warnings.jsonl";
    std::ofstream     out(path, std::ios::app);
    if (!out) {
        BOOST_LOG_TRIVIAL(error) << "pnp-config-warnings: cannot open " << path << " for append";
        return;
    }

    for (const PnpConfigWarning& w : warnings) {
        if (w.warn_class == PnpWarningClass::NotYetMapped && changed_keys.count(w.key) == 0) {
            ++dropped;
            continue;
        }
        switch (w.warn_class) {
        case PnpWarningClass::UnsupportedFeature: ++count_unsupported; break;
        case PnpWarningClass::NotYetMapped:       ++count_not_mapped;  break;
        case PnpWarningClass::NoOp:               ++count_no_op;       break;
        case PnpWarningClass::LossyFallback:      ++count_lossy;       break;
        }

        nlohmann::json rec;
        rec["ts"]         = ts;
        rec["plate"]      = plate;
        rec["key"]        = w.key;
        rec["class"]      = to_string(w.warn_class);
        rec["orca_value"] = w.orca_value;
        if (w.warn_class == PnpWarningClass::LossyFallback)
            rec["sent_value"] = w.sent_value;
        out << rec.dump() << "\n";
    }

    BOOST_LOG_TRIVIAL(info) << "pnp-config-warnings: plate " << plate
                            << " — unsupported-feature " << count_unsupported
                            << ", not-yet-mapped " << count_not_mapped
                            << ", no-op " << count_no_op
                            << ", lossy-fallback " << count_lossy
                            << " (dropped " << dropped << " not-yet-mapped at default); appended to "
                            << path;
}

}} // namespace Slic3r::GUI
