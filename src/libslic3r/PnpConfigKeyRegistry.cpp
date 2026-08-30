///|/ PNP fork — SchemaBridgeMap ticket 02.
///|/
///|/ Runtime registration of the pnp backend's config keys into Orca's config
///|/ core. Lives in its own translation unit because it is the one place that
///|/ needs to see both the config definition (PrintConfig.hpp) and the preset
///|/ option lists (Preset.hpp).
#include "PrintConfig.hpp"
#include "Preset.hpp"

#include <map>
#include <memory>
#include <set>

#include <boost/log/trivial.hpp>

namespace Slic3r {

namespace {

// True once pnp_register_config_keys() has run. The seal is what makes the
// mutable global safe: every consumer that assumes a fixed key set (the
// undo/redo stack's serialization ordinals, the preset option lists, the
// settings tabs' layout) is built after this flips.
bool s_pnp_keys_sealed = false;
std::vector<std::string> s_pnp_registered_keys;

// Enum domains for dynamically-registered coEnum keys.
//
// ConfigOptionEnumGeneric holds a bare `const t_config_enum_values*`, so the
// map must outlive every option built from the def. These are never erased —
// the seal means the set is fixed for the process — so a raw pointer into them
// stays valid.
std::vector<std::unique_ptr<t_config_enum_values>> s_pnp_enum_maps;

} // namespace

bool pnp_config_keys_sealed() { return s_pnp_keys_sealed; }

const std::vector<std::string>& pnp_registered_config_keys() { return s_pnp_registered_keys; }

size_t pnp_register_config_keys(const std::vector<PnpConfigKeyDef>& keys)
{
    if (s_pnp_keys_sealed) {
        // Re-registration would hand the undo/redo stack an inconsistent
        // ordinal space and leave presets built from the older key set. Loud
        // but non-fatal: refusing is always the safe outcome, and aborting a
        // release build over a startup-ordering bug is not.
        BOOST_LOG_TRIVIAL(error) << "pnp: config keys already registered; ignoring second registration";
        return 0;
    }

    std::vector<std::string> added_print, added_filament, added_printer;

    for (const PnpConfigKeyDef& k : keys) {
        if (k.key.empty())
            continue;
        if (print_config_def.has(k.key))
            // An Orca key of the same name already binds this setting — that is
            // the identity-routing case, and Orca's own definition wins.
            continue;

        ConfigOptionDef* def = print_config_def.add_pnp_key(k.key, k.type);
        def->label           = k.label.empty() ? k.key : k.label;
        def->category        = k.category;
        def->tooltip         = k.tooltip;
        def->sidetext        = k.sidetext;
        // The schema's `advanced` flag picks the generated PNP page's mode tier
        // (ticket 04): the flag defaults to false, so unannotated host keys land
        // in Advanced, not Expert.
        def->mode = k.advanced ? comExpert : comAdvanced;
        if (k.has_min)
            def->min = float(k.min);
        if (k.has_max)
            def->max = float(k.max);

        if (k.type == coEnum || k.type == coEnums) {
            auto map = std::make_unique<t_config_enum_values>();
            for (size_t i = 0; i < k.enum_values.size(); ++i)
                (*map)[k.enum_values[i]] = int(i);
            def->enum_values   = k.enum_values;
            def->enum_labels   = k.enum_values;
            def->enum_keys_map = map.get();
            s_pnp_enum_maps.emplace_back(std::move(map));
        }

        // Build the default by deserializing pnp's own textual default through
        // Orca's deserializer, so the two sides cannot disagree on the value.
        std::unique_ptr<ConfigOption> opt(def->create_empty_option());
        if (!k.default_value.empty() && !opt->deserialize(k.default_value)) {
            BOOST_LOG_TRIVIAL(warning) << "pnp: config key '" << k.key << "' declares default '" << k.default_value
                                       << "' which does not parse as " << int(k.type) << "; using the zero value";
            opt.reset(def->create_empty_option());
        }
        def->set_default_value(opt.release());

        s_pnp_registered_keys.emplace_back(k.key);
        switch (k.scope) {
        case PnpPresetScope::Filament: added_filament.emplace_back(k.key); break;
        case PnpPresetScope::Printer: added_printer.emplace_back(k.key); break;
        case PnpPresetScope::Print:
        default: added_print.emplace_back(k.key); break;
        }
    }

    Preset::append_pnp_options(PnpPresetScope::Print, added_print);
    Preset::append_pnp_options(PnpPresetScope::Filament, added_filament);
    Preset::append_pnp_options(PnpPresetScope::Printer, added_printer);

    s_pnp_keys_sealed = true;
    BOOST_LOG_TRIVIAL(info) << "pnp: registered " << s_pnp_registered_keys.size() << " backend config keys (" << added_print.size()
                            << " print, " << added_filament.size() << " filament, " << added_printer.size() << " printer)";
    return s_pnp_registered_keys.size();
}

std::vector<PnpPageGroup> pnp_page_groups(const std::vector<std::string>& skip_keys)
{
    std::set<std::string> skipped(skip_keys.begin(), skip_keys.end());
    // Bucket by def->category (the schema `group`), per the ordering the
    // prototype locked: descending key count, ties alphabetical. Stable without
    // any fork-side group list.
    std::map<std::string, std::vector<std::string>, std::less<>> buckets;
    for (const std::string& key : pnp_registered_config_keys()) {
        if (skipped.count(key) != 0)
            continue;
        const ConfigOptionDef* def = print_config_def.get(key);
        if (def == nullptr)
            continue; // cannot happen while the seal holds; guarded anyway
        buckets[def->category].emplace_back(key);
    }

    std::vector<PnpPageGroup> groups;
    groups.reserve(buckets.size());
    for (auto& [category, keys] : buckets) {
        std::sort(keys.begin(), keys.end());
        groups.push_back({category, std::move(keys)});
    }
    std::stable_sort(groups.begin(), groups.end(), [](const PnpPageGroup& a, const PnpPageGroup& b) {
        if (a.keys.size() != b.keys.size())
            return a.keys.size() > b.keys.size();
        return a.category < b.category;
    });

    // Names a group that Orca's own settings pages already use. The group does
    // NOT merge into that page (the prototype: the PNP page is the one place
    // backend-declared settings live); this exists so the ticket's review can
    // see the overlap and the banner copy can name it.
    static const char* const orca_page_names[] = {"Support", "Quality", "Speed", "Walls"};
    for (PnpPageGroup& g : groups)
        for (const char* name : orca_page_names)
            if (g.category == name)
                g.matches_orca_page = true;
    return groups;
}

} // namespace Slic3r
