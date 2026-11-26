#include <catch2/catch_all.hpp>

#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <map>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/AppConfig.hpp"

using namespace Slic3r;

namespace {

namespace fs = boost::filesystem;

struct TempPresetDir {
    fs::path path;

    TempPresetDir()
    {
        path = fs::temp_directory_path() / fs::unique_path("orcaslicer-preset-%%%%-%%%%-%%%%");
        fs::create_directories(path);
    }

    ~TempPresetDir()
    {
        boost::system::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write_print_preset(const DynamicPrintConfig &default_config, const fs::path &file, const std::string &name, const std::string &inherits = {})
{
    DynamicPrintConfig config(default_config);
    config.option<ConfigOptionString>("print_settings_id", true)->value = name;
    config.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = inherits;

    fs::create_directories(file.parent_path());
    config.save_to_json(file.string(), name, "User", "1.0.0");
}

// Write a preset json carrying a name and an "inherits" value, using the given collection's
// default config so it loads back into that collection. Works for any preset type.
void write_preset_with_inherits(const DynamicPrintConfig &default_config, const fs::path &file,
                                const std::string &name, const std::string &inherits)
{
    DynamicPrintConfig config(default_config);
    config.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = inherits;

    fs::create_directories(file.parent_path());
    config.save_to_json(file.string(), name, "User", "1.0.0");
}

// Add an in-memory preset (no file) with the given inherits value (empty => root preset).
Preset &add_inmemory_preset(PresetCollection &coll, const std::string &name, const std::string &inherits = {})
{
    DynamicPrintConfig config(coll.default_preset().config);
    config.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS, true)->value = inherits;
    return coll.load_preset(std::string(), name, config, /*select=*/false);
}

// Mark an already-loaded preset as renamed from one or more former names.
void set_renamed_from(PresetCollection &coll, const std::string &preset_name, std::vector<std::string> old_names)
{
    for (auto it = coll.begin(); it != coll.end(); ++it)
        if (it->name == preset_name)
            it->renamed_from = std::move(old_names);
}

// Compare a float vector element-wise with the absolute tolerance upstream uses.
void check_double_vector(const std::vector<double> &actual, std::initializer_list<double> expected)
{
    REQUIRE(actual.size() == expected.size());
    size_t index = 0;
    for (double value : expected)
        REQUIRE_THAT(actual[index++], Catch::Matchers::WithinAbs(value, 1e-6));
}

// A standalone print preset collection that exposes the protected rename-map builder, so a
// renamed_from scenario can be set up without the full system-profile load pipeline.
// (PresetCollection is non-copyable - it holds a mutex - so it is constructed directly with
// the same type/keys/defaults PresetBundle uses for its print collection.)
struct RenameTestCollection : public PresetCollection
{
    RenameTestCollection()
        : PresetCollection(Preset::TYPE_PRINT, Preset::print_options(),
                           static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()))
    {}
    using PresetCollection::update_map_system_profile_renamed;
};

} // namespace

TEST_CASE("Preset identity is canonicalized from load path", "[Preset][Identity]")
{
    TempPresetDir              temp_dir;
    PresetBundle               bundle;
    PresetsConfigSubstitutions substitutions;

    write_print_preset(bundle.prints.default_preset().config, temp_dir.path / PRESET_PRINT_NAME / "User.json", "User");
    write_print_preset(bundle.prints.default_preset().config, temp_dir.path / PRESET_LOCAL_DIR / "bundle-1" / PRESET_PRINT_NAME / "LocalBundle.json", "LocalBundle");
    write_print_preset(bundle.prints.default_preset().config, temp_dir.path / PRESET_SUBSCRIBED_DIR / "remote-1" / PRESET_PRINT_NAME / "Subscribed.json", "Subscribed");

    bundle.prints.load_presets(temp_dir.path.string(), PRESET_PRINT_NAME, substitutions, ForwardCompatibilitySubstitutionRule::Disable);
    bundle.prints.load_presets((temp_dir.path / PRESET_LOCAL_DIR / "bundle-1").string(), PRESET_PRINT_NAME, substitutions, ForwardCompatibilitySubstitutionRule::Disable);
    bundle.prints.load_presets((temp_dir.path / PRESET_SUBSCRIBED_DIR / "remote-1").string(), PRESET_PRINT_NAME, substitutions, ForwardCompatibilitySubstitutionRule::Disable);

    const Preset *root_user = bundle.prints.find_preset("User");
    REQUIRE(root_user != nullptr);
    CHECK(root_user->name == "User");
    CHECK_FALSE(root_user->is_from_bundle());

    const Preset *local_bundle = bundle.prints.find_preset("_local/bundle-1/LocalBundle");
    REQUIRE(local_bundle != nullptr);
    CHECK(local_bundle->name == "_local/bundle-1/LocalBundle");
    CHECK(local_bundle->is_from_bundle());

    const Preset *subscribed = bundle.prints.find_preset("_subscribed/remote-1/Subscribed");
    REQUIRE(subscribed != nullptr);
    CHECK(subscribed->name == "_subscribed/remote-1/Subscribed");
    CHECK(subscribed->is_from_bundle());
}

TEST_CASE("Legacy bundle import without bundle metadata stays in the user preset directory", "[Preset][Identity]")
{
    TempPresetDir temp_dir;
    PresetBundle  bundle;

    PresetsConfigSubstitutions substitutions;
    std::vector<std::string>   result;
    int                        overwrite = 0;
    std::string                file      = (temp_dir.path / "legacy-bundle" / "Imported.json").string();
    const fs::path             user_root = temp_dir.path / "user";

    write_print_preset(bundle.prints.default_preset().config, file, "Imported");
    fs::create_directories(user_root);
    bundle.prints.update_user_presets_directory(user_root.string(), PRESET_PRINT_NAME);

    REQUIRE(bundle.import_json_presets(
        substitutions,
        file,
        [](std::string const &) { return 1; },
        ForwardCompatibilitySubstitutionRule::Disable,
        overwrite,
        result));

    const Preset *imported = bundle.prints.find_preset("Imported");
    REQUIRE(imported != nullptr);
    CHECK(imported->name == "Imported");
    CHECK(imported->bundle_id.empty());
    CHECK_FALSE(imported->is_from_bundle());
    // Detached user presets (no inherits) are saved in the "base" subfolder of the user preset root.
    CHECK(fs::equivalent(fs::path(imported->file).parent_path().parent_path(), user_root / PRESET_PRINT_NAME));
}

TEST_CASE("Current vendor type tolerates missing printer model", "[Preset][Bundle]")
{
    PresetBundle bundle;

    VendorProfile orca_vendor("ORCA");
    VendorProfile::PrinterModel model;
    model.name = "Orca Test";
    orca_vendor.models.emplace_back(model);
    bundle.vendors.emplace("ORCA", std::move(orca_vendor));

    bundle.printers.get_edited_preset().config.erase("printer_model");

    CHECK(bundle.get_current_vendor_type() == VendorType::Unknown);
}

TEST_CASE("Printer extruder count tolerates missing nozzle diameter", "[Preset][Bundle]")
{
    PresetBundle bundle;
    DynamicPrintConfig& config = bundle.printers.get_edited_preset().config;

    config.erase("nozzle_diameter");
    CHECK(bundle.get_printer_extruder_count() == 1);

    config.set_key_value("nozzle_diameter", new ConfigOptionFloats());
    CHECK(bundle.get_printer_extruder_count() == 1);

    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({ 0.4, 0.6 }));
    CHECK(bundle.get_printer_extruder_count() == 2);
}

TEST_CASE("find_preset resolves a system preset's renamed_from", "[Preset][Rename]")
{
    RenameTestCollection coll;

    // "New Process" is the current preset; it was renamed from "Old Process".
    add_inmemory_preset(coll, "New Process");
    set_renamed_from(coll, "New Process", { "Old Process" });
    coll.update_map_system_profile_renamed();

    // The rename map knows the old name...
    const std::string *renamed = coll.get_preset_name_renamed("Old Process");
    REQUIRE(renamed != nullptr);
    CHECK(*renamed == "New Process");

    // ...and plain find_preset() now follows it (the core of this PR; previously this
    // resolution lived only in find_preset2 and a few call sites).
    const Preset *resolved = coll.find_preset("Old Process");
    REQUIRE(resolved != nullptr);
    CHECK(resolved->name == "New Process");

    // A genuinely unknown name still returns null (no spurious match).
    CHECK(coll.find_preset("Totally Unknown") == nullptr);

    // A child that still inherits the OLD name resolves through the runtime walker,
    // which uses plain find_preset().
    Preset       &child  = add_inmemory_preset(coll, "Child Process", "Old Process");
    const Preset *parent = coll.get_preset_parent(child);
    REQUIRE(parent != nullptr);
    CHECK(parent->name == "New Process");
}

TEST_CASE("find_preset resolves a preset renamed more than once", "[Preset][Rename]")
{
    RenameTestCollection coll;

    // "New Process" was renamed twice, so it carries both former names in renamed_from.
    add_inmemory_preset(coll, "New Process");
    set_renamed_from(coll, "New Process", { "Original Process", "Old Process" });
    coll.update_map_system_profile_renamed();

    // Each historical name resolves to the current preset.
    for (const char *old_name : { "Original Process", "Old Process" }) {
        INFO("resolving old name: " << old_name);
        const std::string *renamed = coll.get_preset_name_renamed(old_name);
        REQUIRE(renamed != nullptr);
        CHECK(*renamed == "New Process");

        const Preset *resolved = coll.find_preset(old_name);
        REQUIRE(resolved != nullptr);
        CHECK(resolved->name == "New Process");
    }

    // A child inheriting either former name resolves through the runtime walker.
    Preset &child = add_inmemory_preset(coll, "Child Process", "Original Process");
    REQUIRE(coll.get_preset_parent(child) != nullptr);
    CHECK(coll.get_preset_parent(child)->name == "New Process");
}

TEST_CASE("find_preset2 auto-matches removed Generic vendor profiles to the library", "[Preset][Rename]")
{
    PresetBundle bundle;

    // The OrcaFilamentLibrary replacement that removed empty "<vendor> Generic" profiles map to.
    add_inmemory_preset(bundle.filaments, "Generic PLA @System");

    // Plain lookups do NOT fuzzy-match a removed vendor profile.
    CHECK(bundle.filaments.find_preset("Voron Generic PLA") == nullptr);
    CHECK(bundle.filaments.find_preset2("Voron Generic PLA", /*auto_match=*/false) == nullptr);

    // With auto_match, the removed "Voron Generic PLA" resolves to "Generic PLA @System".
    const Preset *matched = bundle.filaments.find_preset2("Voron Generic PLA", /*auto_match=*/true);
    REQUIRE(matched != nullptr);
    CHECK(matched->name == "Generic PLA @System");

    // No library preset exists for an unrelated material => still no match.
    CHECK(bundle.filaments.find_preset2("BrandX Generic PETG", /*auto_match=*/true) == nullptr);
}

TEST_CASE("Renamed parent is normalized into a loaded preset's inherits", "[Preset][Rename]")
{
    TempPresetDir        temp_dir;
    RenameTestCollection coll;

    // Current parent, renamed from "Old Process".
    add_inmemory_preset(coll, "New Process");
    set_renamed_from(coll, "New Process", { "Old Process" });
    coll.update_map_system_profile_renamed();

    // A user preset on disk that still inherits the OLD name.
    write_preset_with_inherits(coll.default_preset().config,
                               temp_dir.path / PRESET_PRINT_NAME / "Child.json", "Child", "Old Process");

    PresetsConfigSubstitutions substitutions;
    coll.load_presets(temp_dir.path.string(), PRESET_PRINT_NAME, substitutions,
                      ForwardCompatibilitySubstitutionRule::Disable);

    const Preset *child = coll.find_preset("Child");
    REQUIRE(child != nullptr);
    // The dangling "Old Process" was rewritten to the resolved parent name at load time,
    // so the runtime walker (plain find_preset) can resolve the chain.
    CHECK(child->inherits() == "New Process");
    REQUIRE(coll.get_preset_parent(*child) != nullptr);
    CHECK(coll.get_preset_parent(*child)->name == "New Process");
}

TEST_CASE("Removed Generic parent is normalized into a loaded filament's inherits", "[Preset][Rename]")
{
    TempPresetDir temp_dir;
    PresetBundle  bundle;

    add_inmemory_preset(bundle.filaments, "Generic PLA @System");

    // A user filament that still inherits a removed "<vendor> Generic PLA" profile.
    write_preset_with_inherits(bundle.filaments.default_preset().config,
                               temp_dir.path / PRESET_FILAMENT_NAME / "MyPLA.json", "MyPLA", "Voron Generic PLA");

    PresetsConfigSubstitutions substitutions;
    bundle.filaments.load_presets(temp_dir.path.string(), PRESET_FILAMENT_NAME, substitutions,
                                  ForwardCompatibilitySubstitutionRule::Disable);

    const Preset *child = bundle.filaments.find_preset("MyPLA");
    REQUIRE(child != nullptr);
    CHECK(child->inherits() == "Generic PLA @System");
    REQUIRE(bundle.filaments.get_preset_parent(*child) != nullptr);
    CHECK(bundle.filaments.get_preset_parent(*child)->name == "Generic PLA @System");
}

namespace {

// A live reference to a preset's compatible_printers / compatible_prints list. Fetches the *stored*
// preset (real=true) so writes and reads hit the same object; creates the option if absent.
std::vector<std::string> &compatible_list(PresetCollection &coll, const std::string &preset_name, const char *field_key)
{
    Preset *preset = coll.find_preset(preset_name, /*first_visible_if_not_found=*/false, /*real=*/true);
    REQUIRE(preset != nullptr);
    return preset->config.option<ConfigOptionStrings>(field_key, true)->values;
}

} // namespace

TEST_CASE("Renamed printer/process names are normalized into compatible lists on load", "[Preset][Rename]")
{
    PresetBundle bundle;

    // Current printer + process, each renamed from an older name.
    add_inmemory_preset(bundle.printers, "New Printer");
    set_renamed_from(bundle.printers, "New Printer", { "Old Printer" });
    add_inmemory_preset(bundle.prints, "New Process");
    set_renamed_from(bundle.prints, "New Process", { "Old Process" });

    // A user process still compatible with the OLD printer name.
    add_inmemory_preset(bundle.prints, "My Process");
    compatible_list(bundle.prints, "My Process", "compatible_printers") = { "Old Printer" };

    // A user filament referencing the OLD printer AND OLD process names, plus an unknown printer.
    add_inmemory_preset(bundle.filaments, "My Filament");
    compatible_list(bundle.filaments, "My Filament", "compatible_printers") = { "Old Printer", "Unknown Printer" };
    compatible_list(bundle.filaments, "My Filament", "compatible_prints")   = { "Old Process" };

    // Build the rename maps (done during system load in the real pipeline), then normalize.
    AppConfig app_config;
    bundle.load_installed_printers(app_config); // rebuilds every collection's rename map
    bundle.normalize_compatible_presets();

    // The stale printer name in a process' compatible_printers is rewritten to the current name.
    CHECK(compatible_list(bundle.prints, "My Process", "compatible_printers") == std::vector<std::string>{ "New Printer" });

    // The stale process name in a filament's compatible_prints is rewritten (this field has no
    // runtime rename fallback, so load-time normalization is the only fix).
    CHECK(compatible_list(bundle.filaments, "My Filament", "compatible_prints") == std::vector<std::string>{ "New Process" });

    // The renamed printer is rewritten while the unknown/deleted name is preserved as-is.
    CHECK(compatible_list(bundle.filaments, "My Filament", "compatible_printers") ==
          (std::vector<std::string>{ "New Printer", "Unknown Printer" }));

    // Normalizing rewrites config in place without flagging the preset dirty.
    CHECK_FALSE(bundle.prints.find_preset("My Process", false, true)->is_dirty);

    // A system preset that already references the current name is left untouched (idempotent no-op).
    bundle.normalize_compatible_presets();
    CHECK(compatible_list(bundle.prints, "My Process", "compatible_printers") == std::vector<std::string>{ "New Printer" });
}

TEST_CASE("Renamed names are normalized into a SYSTEM preset's compatible lists", "[Preset][Rename]")
{
    PresetBundle bundle;

    // Current printer + process, each renamed from an older name.
    add_inmemory_preset(bundle.printers, "New Printer");
    set_renamed_from(bundle.printers, "New Printer", { "Old Printer" });
    add_inmemory_preset(bundle.prints, "New Process");
    set_renamed_from(bundle.prints, "New Process", { "Old Process" });

    // A *system* (vendor) filament whose own compatible lists still reference the OLD names. A vendor
    // profile can point at a sibling preset that was later renamed, so system presets must be
    // normalized too (they are skipped by neither collection walk).
    add_inmemory_preset(bundle.filaments, "System Filament").is_system = true;
    compatible_list(bundle.filaments, "System Filament", "compatible_printers") = { "Old Printer" };
    compatible_list(bundle.filaments, "System Filament", "compatible_prints")   = { "Old Process" };

    AppConfig app_config;
    bundle.load_installed_printers(app_config); // build the rename maps
    bundle.normalize_compatible_presets();

    // The stale references in the system preset are rewritten to the current names.
    CHECK(compatible_list(bundle.filaments, "System Filament", "compatible_printers") ==
          std::vector<std::string>{ "New Printer" });
    CHECK(compatible_list(bundle.filaments, "System Filament", "compatible_prints") ==
          std::vector<std::string>{ "New Process" });

    // The rewrite does not flag the system preset dirty, and is idempotent.
    CHECK_FALSE(bundle.filaments.find_preset("System Filament", false, true)->is_dirty);
    bundle.normalize_compatible_presets();
    CHECK(compatible_list(bundle.filaments, "System Filament", "compatible_printers") ==
          std::vector<std::string>{ "New Printer" });
}

// PNP fork (F12): removed the "compatible_prints on SLA materials" case — sla_prints/sla_materials
// collections were deleted with the SLA cut.

TEST_CASE("Shipped renamed_from entries are unambiguous", "[Preset][Rename]")
{
    // update_map_system_profile_renamed() builds one old-name -> preset map per collection and
    // counts an error when two presets claim the same former name: the bundle then reports
    // "presets have errors" at startup, and find_preset() picks whichever loaded first. The
    // profile sync (UpstreamSyncMap ticket 03) rewrote every vendor tree, so assert the shipped
    // data still satisfies that invariant before relying on it to keep user presets attached.
    const fs::path profiles_dir(PROFILES_DIR);
    REQUIRE(fs::is_directory(profiles_dir));

    std::map<std::string, std::map<std::string, std::string>> renamed; // type -> old name -> new name
    size_t entries = 0;

    for (const fs::directory_entry &vendor_dir : fs::directory_iterator(profiles_dir)) {
        if (!fs::is_directory(vendor_dir.path()))
            continue;
        for (const fs::directory_entry &entry : fs::recursive_directory_iterator(vendor_dir.path())) {
            if (!fs::is_regular_file(entry.path()) || entry.path().extension() != ".json")
                continue;
            std::ifstream stream(entry.path().string(), std::ios::binary);
            REQUIRE(stream.good());
            const nlohmann::json doc = nlohmann::json::parse(stream);
            if (!doc.is_object())
                continue;
            const auto type = doc.find("type");
            const auto name = doc.find("name");
            const auto from = doc.find("renamed_from");
            if (type == doc.end() || !type->is_string() || name == doc.end() || !name->is_string() ||
                from == doc.end() || !from->is_string())
                continue;
            const std::string &t = type->get_ref<const std::string &>();
            if (t != "machine" && t != "process" && t != "filament")
                continue;
            const std::string preset_name = name->get<std::string>();

            std::string rest = from->get<std::string>();
            size_t      cut  = 0;
            while (cut != std::string::npos && !rest.empty()) {
                cut = rest.find(';');
                const std::string old_name = boost::algorithm::trim_copy(rest.substr(0, cut));
                rest = (cut == std::string::npos) ? std::string() : rest.substr(cut + 1);
                if (old_name.empty())
                    continue;
                INFO("preset " << preset_name << " renamed_from " << old_name);
                CHECK(old_name != preset_name);
                const auto [it, inserted] =
                    renamed[t].insert(std::make_pair(old_name, preset_name));
                if (!inserted)
                    CHECK(it->second == preset_name);
                ++entries;
            }
        }
    }

    // Guards against passing vacuously if the tree is not where PROFILES_DIR points.
    CHECK(entries > 1000);
}

TEST_CASE("Profile validator flags dangling and renamed preset references", "[Preset][Validate]")
{
    PresetBundle bundle;

    // Current printers: a real one, and a renamed one (its old name resolves via renamed_from).
    add_inmemory_preset(bundle.printers, "Real Printer");
    add_inmemory_preset(bundle.printers, "New Printer");
    set_renamed_from(bundle.printers, "New Printer", { "Old Printer" });

    // A real process, referenced from a filament's compatible_prints.
    add_inmemory_preset(bundle.prints, "Real Process").is_system = true;

    // A fully valid system filament: references only current names.
    add_inmemory_preset(bundle.filaments, "Good Filament").is_system = true;
    compatible_list(bundle.filaments, "Good Filament", "compatible_printers") = { "Real Printer" };
    compatible_list(bundle.filaments, "Good Filament", "compatible_prints")   = { "Real Process" };

    AppConfig app_config;
    bundle.load_installed_printers(app_config); // build the rename maps

    // With only valid references, the validator is clean.
    CHECK_FALSE(bundle.check_preset_references());

    SECTION("deleted compatible_printers is flagged") {
        add_inmemory_preset(bundle.filaments, "Ghost Ref Filament").is_system = true;
        compatible_list(bundle.filaments, "Ghost Ref Filament", "compatible_printers") = { "Ghost Printer" };
        CHECK(bundle.check_preset_references());
    }

    SECTION("renamed compatible_printers (old name) is flagged") {
        add_inmemory_preset(bundle.filaments, "Old Ref Filament").is_system = true;
        compatible_list(bundle.filaments, "Old Ref Filament", "compatible_printers") = { "Old Printer" };
        CHECK(bundle.check_preset_references());
    }

    SECTION("deleted compatible_prints is flagged") {
        add_inmemory_preset(bundle.filaments, "Bad Process Ref").is_system = true;
        compatible_list(bundle.filaments, "Bad Process Ref", "compatible_prints") = { "Ghost Process" };
        CHECK(bundle.check_preset_references());
    }

    SECTION("deleted inherits parent is flagged") {
        add_inmemory_preset(bundle.filaments, "Orphan Filament", "Ghost Parent").is_system = true;
        CHECK(bundle.check_preset_references());
    }

    SECTION("non-system preset with a dangling reference is ignored") {
        add_inmemory_preset(bundle.filaments, "User Filament"); // is_system stays false
        compatible_list(bundle.filaments, "User Filament", "compatible_printers") = { "Ghost Printer" };
        CHECK_FALSE(bundle.check_preset_references());
    }
}


// PNP fork (F13) regression. SLA preset collections were removed in F12, so the
// preset-type -> collection lookup must return nullptr for SLA types. A stale GUI caller
// (DiffPresetDialog::create_presets_sizer) still iterated the SLA types and dereferenced
// that null, crashing the app on startup with an ACCESS_VIOLATION in get_selected_idx.
// The mapping is now centralised as PresetBundle::get_preset_collection; guard the
// contract so callers can rely on null for SLA / unknown types.
TEST_CASE("PresetBundle::get_preset_collection returns null for SLA types", "[Preset][Bundle]")
{
    PresetBundle bundle;

    // FFF types resolve to their collections.
    REQUIRE(bundle.get_preset_collection(Preset::TYPE_PRINTER)  != nullptr);
    REQUIRE(bundle.get_preset_collection(Preset::TYPE_FILAMENT) == &bundle.filaments);
    REQUIRE(bundle.get_preset_collection(Preset::TYPE_PRINT)    == &bundle.prints);

    // SLA types have no collection in this fork -> nullptr (must never be dereferenced).
    REQUIRE(bundle.get_preset_collection(Preset::TYPE_SLA_PRINT)    == nullptr);
    REQUIRE(bundle.get_preset_collection(Preset::TYPE_SLA_MATERIAL) == nullptr);
    REQUIRE(bundle.get_preset_collection(Preset::TYPE_INVALID)      == nullptr);

    // const overload agrees.
    const PresetBundle& cbundle = bundle;
    REQUIRE(cbundle.get_preset_collection(Preset::TYPE_SLA_MATERIAL) == nullptr);
    REQUIRE(cbundle.get_preset_collection(Preset::TYPE_PRINT)        == &cbundle.prints);
}

// Upstream 9b2fcaa53a. A 2.4-era project can embed a filament that stores one value for a
// per-variant key while the parent declares two variants. update_diff_values_to_child_config
// indexes the child list by variant position, so without padding the config to the parent's
// length it read past the end and threw "bad allocation" (or worse).
TEST_CASE("A project filament storing one value for a per-variant key applies it to every variant", "[Preset][Bundle][Regression]")
{
    PresetCollection filaments(Preset::TYPE_FILAMENT, Preset::filament_options(),
                               static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()));
    Preset &parent = add_inmemory_preset(filaments, "Parent Filament");
    parent.config.set_key_value("filament_extruder_variant", new ConfigOptionStrings({"Direct Drive Standard", "Direct Drive High Flow"}));
    parent.config.set_deserialize_strict("filament_flow_ratio", "0.98,0.95");
    parent.config.set_deserialize_strict("filament_max_volumetric_speed", "15,12");
    parent.is_system = true;

    // A filament saved before these keys became per-variant lists both variants but stores each key once.
    DynamicPrintConfig config;
    config.set_key_value(BBL_JSON_KEY_INHERITS, new ConfigOptionString("Parent Filament"));
    config.set_key_value("filament_extruder_variant", new ConfigOptionStrings({"Direct Drive Standard", "Direct Drive High Flow"}));
    config.set_deserialize_strict("filament_flow_ratio", "0.97");
    config.set_deserialize_strict("filament_max_volumetric_speed", "11");
    auto embedded                 = std::make_unique<Preset>(Preset::TYPE_FILAMENT, "Project Filament", false);
    embedded->config              = std::move(config);
    embedded->is_project_embedded = true;

    std::vector<Preset *>      project_presets{embedded.get()};
    PresetsConfigSubstitutions substitutions;
    filaments.load_project_embedded_presets(project_presets, PRESET_FILAMENT_NAME, substitutions, ForwardCompatibilitySubstitutionRule::Disable);

    const Preset *loaded = filaments.find_preset("Project Filament");
    REQUIRE(loaded != nullptr);
    check_double_vector(loaded->config.option<ConfigOptionFloats>("filament_flow_ratio")->values, {0.97, 0.97});
    check_double_vector(loaded->config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->values, {11, 11});
}

TEST_CASE("A project's edited filament keeps the preset's compatible printers and processes", "[Preset][Bundle][Regression]")
{
    enum class Load { FirstSlotSelected, FirstSlotParentSelected, LaterSlotEmbedded, LaterSlotCopied };
    const Load load = GENERATE(Load::FirstSlotSelected, Load::FirstSlotParentSelected, Load::LaterSlotEmbedded, Load::LaterSlotCopied);
    CAPTURE(load);
    const bool first_slot = load == Load::FirstSlotSelected || load == Load::FirstSlotParentSelected;
    // For FirstSlotParentSelected only the parent the project inherits from is loaded, so it is the one selected.
    const std::string preset_name = load == Load::FirstSlotParentSelected ? "Parent PLA" : "My PLA";
    PresetCollection filaments(Preset::TYPE_FILAMENT, Preset::filament_options(),
                               static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()));
    Preset &user = add_inmemory_preset(filaments, preset_name);
    user.is_project_embedded = load == Load::LaterSlotEmbedded;
    user.config.option<ConfigOptionStrings>("compatible_printers", true)->values = {"My Printer"};
    user.config.option<ConfigOptionStrings>("compatible_prints", true)->values   = {"My Process"};
    user.config.option<ConfigOptionInts>("nozzle_temperature", true)->values     = {210};

    DynamicPrintConfig project(user.config);
    project.option<ConfigOptionStrings>("compatible_printers")->values.clear();
    project.option<ConfigOptionStrings>("compatible_prints")->values.clear();
    project.option<ConfigOptionInts>("nozzle_temperature")->values = {225};
    if (load == Load::FirstSlotParentSelected)
        project.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS)->value = preset_name;

    const Preset *loaded = filaments.load_external_preset("test.3mf", "test.3mf", "My PLA", project, {},
                                                          first_slot ? PresetCollection::LoadAndSelect::Always :
                                                                       PresetCollection::LoadAndSelect::OnlyIfModified).first;
    REQUIRE(loaded != nullptr);

    const Preset &result = first_slot ? filaments.get_edited_preset() : *loaded;
    REQUIRE(result.name == (load == Load::LaterSlotCopied ? "My PLA(test.3mf)" : preset_name));
    CHECK(result.config.option<ConfigOptionInts>("nozzle_temperature")->values == std::vector<int>{225});
    CHECK(result.config.option<ConfigOptionStrings>("compatible_printers")->values == std::vector<std::string>{"My Printer"});
    CHECK(result.config.option<ConfigOptionStrings>("compatible_prints")->values == std::vector<std::string>{"My Process"});
}

// Upstream 4a20168742. A config loaded over the full defaults already carries a one-entry
// filament_self_index, so the old "key missing" test never fired and a multi-filament project
// authored without the key kept the single entry. Size the index against the filament count.
TEST_CASE("A multi-toolhead project saved without filament self indices loads every filament", "[Preset][Bundle]")
{
    const std::vector<std::string> colors = { "#FF0000", "#000000", "#FFFFFF", "#FFFF00" };
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionStrings>("filament_colour")->values = colors;
    config.opt<ConfigOptionFloats>("nozzle_diameter")->values = std::vector<double>(colors.size(), 0.4);
    config.option<ConfigOptionBool>("single_extruder_multi_material")->value = false;
    Preset::normalize(config);

    PresetBundle bundle;
    REQUIRE_NOTHROW(bundle.load_config_model("test.3mf", std::move(config)));
    CHECK(bundle.filament_presets.size() == colors.size());
    CHECK(bundle.project_config.opt<ConfigOptionStrings>("filament_colour")->values == colors);
}
