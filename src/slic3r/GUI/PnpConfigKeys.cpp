#include "PnpConfigKeys.hpp"

#include <set>

#include "PnpConfigTranslator.hpp"
#include "libslic3r/Config.hpp"

namespace Slic3r { namespace GUI { namespace PnpConfigKeys {

namespace {

std::string json_string(const nlohmann::json& obj, const char* field)
{
    auto it = obj.find(field);
    if (it == obj.end() || it->is_null())
        return {};
    if (it->is_string())
        return it->get<std::string>();
    return it->dump();
}

bool json_number(const nlohmann::json& obj, const char* field, double& out)
{
    auto it = obj.find(field);
    if (it == obj.end() || ! it->is_number())
        return false;
    out = it->get<double>();
    return true;
}

PnpPresetScope parse_scope(const std::string& scope)
{
    if (scope == "filament")
        return PnpPresetScope::Filament;
    if (scope == "printer")
        return PnpPresetScope::Printer;
    // Absent or unrecognised means print, which is right for every module key
    // today and is what wire versions before 1.1.0 implied by saying nothing.
    return PnpPresetScope::Print;
}

// Build one def from a schema field object. `field` must carry at least "key".
bool field_to_def(const nlohmann::json& field, PnpConfigKeyDef& out, std::string& reason)
{
    out.key = json_string(field, "key");
    if (out.key.empty()) {
        reason = "field carries no key";
        return false;
    }

    const std::string wire_type = json_string(field, "type");
    if (! map_wire_type(wire_type, out.type)) {
        reason = "unmapped wire type '" + wire_type + "'";
        return false;
    }

    out.scope         = parse_scope(json_string(field, "scope"));
    out.default_value = json_string(field, "default");
    if (out.type == coBool || out.type == coBools) {
        // pnp renders bools as JSON literals; Orca's deserializer wants 1/0.
        if (out.default_value == "true")  out.default_value = "1";
        if (out.default_value == "false") out.default_value = "0";
    }
    out.label         = json_string(field, "display");
    out.category      = json_string(field, "group");
    out.tooltip       = json_string(field, "description");
    out.sidetext      = json_string(field, "unit");

    out.has_min = json_number(field, "min", out.min);
    out.has_max = json_number(field, "max", out.max);

    if (out.type == coEnum) {
        auto values = field.find("values");
        if (values != field.end() && values->is_array())
            for (const auto& v : *values)
                if (v.is_string())
                    out.enum_values.emplace_back(v.get<std::string>());
        if (out.enum_values.empty()) {
            // An enum with no domain cannot be deserialized or presented; a
            // free-text string is the honest fallback.
            reason = "enum declares no values";
            return false;
        }
    }
    return true;
}

} // namespace

bool map_wire_type(const std::string& wire_type, ConfigOptionType& out)
{
    // pnp's vocabulary is documented on ConfigFieldEntry::field_type
    // (crates/slicer-scheduler/src/manifest.rs).
    if (wire_type == "bool")             { out = coBool;           return true; }
    if (wire_type == "int")              { out = coInt;            return true; }
    if (wire_type == "float")            { out = coFloat;          return true; }
    if (wire_type == "string")           { out = coString;         return true; }
    if (wire_type == "enum")             { out = coEnum;           return true; }
    if (wire_type == "percent")          { out = coPercent;        return true; }
    if (wire_type == "float_or_percent") { out = coFloatOrPercent; return true; }
    if (wire_type == "float-list")       { out = coFloats;         return true; }
    if (wire_type == "string-list")      { out = coStrings;        return true; }
    return false;
}

std::vector<PnpConfigKeyDef> parse_schema(const nlohmann::json&                          schema_doc,
                                          const std::function<bool(const std::string&)>& already_bound,
                                          std::vector<SkippedKey>*                       skipped)
{
    std::vector<PnpConfigKeyDef> out;
    if (! schema_doc.is_object())
        return out;

    std::set<std::string> seen;
    auto consider = [&](const nlohmann::json& field) {
        PnpConfigKeyDef def;
        std::string     reason;
        if (! field.is_object())
            return;
        const std::string key = json_string(field, "key");
        if (key.empty() || ! seen.insert(key).second)
            // A key declared by several modules is one key; first wins.
            return;
        if (already_bound && already_bound(key)) {
            if (skipped)
                skipped->push_back({key, "already routed by name identity or the curated table"});
            return;
        }
        if (! field_to_def(field, def, reason)) {
            if (skipped)
                skipped->push_back({key, reason});
            return;
        }
        out.emplace_back(std::move(def));
    };

    auto schema = schema_doc.find("schema");
    if (schema != schema_doc.end() && schema->is_array())
        for (const auto& module : *schema) {
            auto fields = module.find("fields");
            if (fields != module.end() && fields->is_array())
                for (const auto& field : *fields)
                    consider(field);
        }

    // Host keys (wire 1.1.0+). Absent from older backends, in which case the
    // 62 host keys stay unregistered exactly as they were before this ticket.
    auto host = schema_doc.find("host");
    if (host != schema_doc.end() && host->is_array())
        for (const auto& field : *host)
            consider(field);

    return out;
}

size_t register_from_schema(const std::string& schema_json)
{
    if (schema_json.empty())
        return 0;

    const nlohmann::json doc = nlohmann::json::parse(schema_json, nullptr, false);
    if (doc.is_discarded())
        return 0;

    // The pnp key names the curated table already writes. Derived by running
    // the translator over a stock default config rather than restated, so the
    // exclusion set follows the table automatically as rows are repaired
    // (ticket 09) or retired.
    //
    // This runs before registration, so the config it translates is the stock
    // Orca key set — which is exactly the input the table was written against.
    std::set<std::string> curated_targets;
    {
        const DynamicPrintConfig defaults = DynamicPrintConfig::full_print_config();
        // Explicitly the *unprobed* translation (ticket 05): with a universe
        // installed the identity pass copies every declared key under its own
        // name, so the output would be the whole universe and nothing would be
        // left to register. What this needs is the curated table's own targets.
        const PnpTranslationResult routed = PnpConfigTranslator::translate(defaults, nullptr);
        if (routed.json.is_object())
            for (auto it = routed.json.begin(); it != routed.json.end(); ++ it)
                curated_targets.insert(it.key());
    }

    const auto already_bound = [&curated_targets](const std::string& key) {
        return print_config_def.has(key) || curated_targets.count(key) > 0;
    };

    return pnp_register_config_keys(parse_schema(doc, already_bound));
}

}}} // namespace Slic3r::GUI::PnpConfigKeys
