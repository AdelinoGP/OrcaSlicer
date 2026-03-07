// [INTENT] ToolOrdering: Determines for each printed layer which filament/extruder
// is used, in what order, and coordinates with WipeTower2 to insert purge partitions.
// The central data structure is std::vector<LayerTools> (m_layer_tools), one entry per
// unique Z height across all PrintObjects. Each LayerTools holds the ordered list of
// extruder IDs needed on that layer.
//
// High-level algorithm:
//  1. initialize_layers()   - collect all Z heights, merge near-equal ones (EPSILON)
//  2. collect_extruders()   - for each layer, determine which extruders are required
//                             from region config (wall_filament, solid_infill_filament, etc.)
//  3. handle_dontcare_extruder() - replace extruder_id==0 ("don't care") placeholders
//                             with the most recently used extruder, to avoid unnecessary
//                             tool changes; also special-cases soluble first-layer and
//                             user-specified first_layer_print_sequence
//  4. reorder_extruders_for_minimum_flush_volume() - reorder extruders on each layer
//                             to minimize total purge volume (flush matrix optimization)
//  5. fill_wipe_tower_partitions() - count wipe tower segments needed per layer
//  6. collect_extruder_statistics() - per-extruder summary for G-code export
//
// [COUPLING] Tightly coupled to Print, PrintObject, PrintRegion, WipeTower2, GCode.
// [HAZARD] Extruder IDs switch between 0-based and 1-based throughout. Many functions
// receive/return 1-based but store 0-based internally. See assert(extruder_id > 0)
// before the -- decrement in handle_dontcare_extruder().

#include "ExtrusionEntity.hpp"
#include "Print.hpp"
#include "ToolOrdering.hpp"
#include "Layer.hpp"
#include "ClipperUtils.hpp"
#include "ParameterUtils.hpp"
#include "GCode/ToolOrderUtils.hpp"
#include "FilamentGroupUtils.hpp"
#include "I18N.hpp"

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
#endif

#include <cassert>
#include <limits>
#include <algorithm>
#include <unordered_map>

#include <libslic3r.h>

namespace Slic3r {

    //! macro used to mark string used at localization,
    //! return same string

#ifndef _L
#define _L(s) Slic3r::I18N::translate(s)
#endif

// [STATE] File-scope flag: "wipe into objects" feature disabled at compile time.
// [HAZARD] Hardcoded false — changing to true would enable infill wiping which is
// partially implemented but disabled, potentially producing incorrect G-code.
const static bool g_wipe_into_objects = false;

// [INTENT] Threshold for CIEDE2000 color distance. Filaments with ΔE < 20 are
// considered "similar color" and may share purge volume calculations.
constexpr double similar_color_threshold_de2000 = 20.0;

// [INTENT] Helper: filter the list of used filament IDs to only those matching a
// given type string (e.g. "PLA", "TPU"). Used in BBL-specific compatibility checks.
// [COUPLING] Calls print_config->filament_type.get_at() — relies on filament_type
// being a ConfigOptionStrings (indexed by filament slot, not by logical type).
static std::set<int>get_filament_by_type(const std::vector<unsigned int>& used_filaments, const PrintConfig* print_config, const std::string& type)
{
    std::set<int> target_filaments;
    for (unsigned int filament_id : used_filaments) {
        std::string filament_type = print_config->filament_type.get_at(filament_id);
        if (filament_type == type)
            target_filaments.insert(filament_id);
    }
    return target_filaments;
}


// Returns true in case that extruder a comes before b (b does not have to be present). False otherwise.
bool LayerTools::is_extruder_order(unsigned int a, unsigned int b) const
{
    if (a == b)
        return false;

    for (auto extruder : extruders) {
        if (extruder == a)
            return true;
        if (extruder == b)
            return false;
    }

    return false;
}

// [INTENT] BBL-specific compatibility check: verifies that each filament used in
// the print can physically be loaded into the extruder it has been assigned to.
// `filament_printable` is a bitmask — bit 0 = can print on extruder 0 (left), bit 1 = extruder 1 (right).
// [HAZARD] Throws RuntimeError with a localized string — caller must catch to report.
// Localization strings here make libslic3r depend on I18N.hpp.
bool check_filament_printable_after_group(const std::vector<unsigned int> &used_filaments, const std::vector<int> &filament_maps, const PrintConfig *print_config)
{
    for (unsigned int filament_id : used_filaments) {
        std::string filament_type = print_config->filament_type.get_at(filament_id);
        int printable_status = print_config->filament_printable.get_at(filament_id);
        int extruder_idx = filament_maps[filament_id];
        if (!(printable_status >> extruder_idx & 1)) {
            std::string extruder_name = extruder_idx == 0 ? _L("left") : _L("right");
            std::string error_msg     = _L("Grouping error: ") + filament_type + _L(" can not be placed in the ") + extruder_name + _L(" nozzle");
            throw Slic3r::RuntimeError(error_msg);
        }
    }
    return true;
}

// [INTENT] Query functions: return the 0-based extruder index to use for a given
// material role (wall, sparse infill, solid infill). All config values are 1-based
// so the function subtracts 1 before returning. If extruder_override is set (multi-
// material painting mode), all roles use the same override extruder.
// [HAZARD] The -1 conversion from 1-based config to 0-based index is done here.
// Any call site that forgets to call these accessors and reads config().wall_filament.value
// directly will be 1-based where 0-based is expected.
// Return a zero based extruder from the region, or extruder_override if overriden.
unsigned int LayerTools::wall_filament(const PrintRegion &region) const
{
	assert(region.config().wall_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().wall_filament.value : this->extruder_override) - 1;
}

unsigned int LayerTools::sparse_infill_filament(const PrintRegion &region) const
{
	assert(region.config().sparse_infill_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().sparse_infill_filament.value : this->extruder_override) - 1;
}

unsigned int LayerTools::solid_infill_filament(const PrintRegion &region) const
{
	assert(region.config().solid_infill_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().solid_infill_filament.value : this->extruder_override) - 1;
}

// [INTENT] Determine the 0-based extruder ID for an entire ExtrusionEntityCollection
// based on what roles it contains. Priority: solid infill > sparse infill > wall.
// If the collection is a wall (no infill roles), use wall_filament config.
// [HAZARD] Returns 0 when extruder config is 0 (meaning "inherit"), but 0 is also
// a valid extruder index. Callers must distinguish: extruder==0 returned when
// extruder config==0 means "unassigned"; extruder==0 returned when config==1 means
// "first extruder". This ambiguity exists in the original PrusaSlicer design.
// Returns a zero based extruder this eec should be printed with, according to PrintRegion config or extruder_override if overriden.
unsigned int LayerTools::extruder(const ExtrusionEntityCollection &extrusions, const PrintRegion &region) const
{
	assert(region.config().wall_filament.value > 0);
	assert(region.config().sparse_infill_filament.value > 0);
	assert(region.config().solid_infill_filament.value > 0);
	// 1 based extruder ID.
    unsigned int extruder = 1;
    if (this->extruder_override == 0) {
        if (extrusions.has_infill()) {
            if (extrusions.has_solid_infill())
                extruder = region.config().solid_infill_filament;
            else
                extruder = region.config().sparse_infill_filament;
        } else
            extruder = region.config().wall_filament.value;
    } else
        extruder = this->extruder_override;

    return (extruder == 0) ? 0 : extruder - 1;
}

static double calc_max_layer_height(const PrintConfig &config, double max_object_layer_height)
{
    double max_layer_height = std::numeric_limits<double>::max();
    for (size_t i = 0; i < config.nozzle_diameter.values.size(); ++ i) {
        double mlh = config.max_layer_height.values[i];
        if (mlh == 0.)
            mlh = 0.75 * config.nozzle_diameter.values[i];
        max_layer_height = std::min(max_layer_height, mlh);
    }
    // The Prusa3D Fast (0.35mm layer height) print profile sets a higher layer height than what is normally allowed
    // by the nozzle. This is a hack and it works by increasing extrusion width. See GH #3919.
    return std::max(max_layer_height, max_object_layer_height);
}

//calculate the flush weight (first value) and filament change count(second value)
static FilamentChangeStats calc_filament_change_info_by_toolorder(const PrintConfig* config, const std::vector<int>& filament_map, const std::vector<FlushMatrix>& flush_matrix, const std::vector<std::vector<unsigned int>>& layer_sequences)
{
    FilamentChangeStats ret;
    std::unordered_map<int, int> flush_volume_per_filament;
    int max_extruder_id = *std::max_element(filament_map.begin(), filament_map.end());
    assert(max_extruder_id >= 0);
    std::vector<unsigned int>last_filament_per_extruder(max_extruder_id + 1, -1);

    int total_filament_change_count = 0;
    float total_filament_flush_weight = 0;
    for (const auto& ls : layer_sequences) {
        for (const auto& item : ls) {
            int extruder_id = filament_map[item];
            int last_filament = last_filament_per_extruder[extruder_id];
            if (last_filament != -1 && last_filament != item) {
                int flush_volume = flush_matrix[extruder_id][last_filament][item];
                flush_volume_per_filament[item] += flush_volume;
                total_filament_change_count += 1;
            }
            last_filament_per_extruder[extruder_id] = item;
        }
    }

    for (auto& fv : flush_volume_per_filament) {
        float weight = config->filament_density.get_at(fv.first) * 0.001 * fv.second;
        total_filament_flush_weight += weight;
    }

    ret.filament_change_count = total_filament_change_count;
    ret.filament_flush_weight = (int)total_filament_flush_weight;

    return ret;
}

static void apply_first_layer_order(const DynamicPrintConfig* config, std::vector<unsigned int>& tool_order);

void ToolOrdering::handle_dontcare_extruder(const std::vector<unsigned int>& tool_order_layer0)
{
    if(m_layer_tools.empty() || tool_order_layer0.empty())
        return;

    // Reorder the extruders of first layer
    {
        LayerTools& lt = m_layer_tools[0];
        std::vector<unsigned int> layer0_extruders = lt.extruders;
        lt.extruders.clear();
        for (unsigned int extruder_id : tool_order_layer0) {
            auto iter = std::find(layer0_extruders.begin(), layer0_extruders.end(), extruder_id);
            if (iter != layer0_extruders.end()) {
                lt.extruders.push_back(extruder_id);
                *iter = (unsigned int)-1;
            }
        }

        for (unsigned int extruder_id : layer0_extruders) {
            if (extruder_id == 0)
                continue;

            if (extruder_id != (unsigned int)-1)
                lt.extruders.push_back(extruder_id);
        }

        // all extruders are zero
        if (lt.extruders.empty()) {
            lt.extruders.push_back(tool_order_layer0[0]);
        }
    }

    int last_extruder_id = m_layer_tools[0].extruders.back();
    for (int i = 1; i < m_layer_tools.size(); i++) {
        LayerTools& lt = m_layer_tools[i];

        if (lt.extruders.empty())
            continue;
        if (lt.extruders.size() == 1 && lt.extruders.front() == 0)
            lt.extruders.front() = last_extruder_id;
        else {
            if (lt.extruders.front() == 0)
                // Pop the "don't care" extruder, the "don't care" region will be merged with the next one.
                lt.extruders.erase(lt.extruders.begin());
            // Reorder the extruders to start with the last one.
            for (size_t i = 1; i < lt.extruders.size(); ++i)
                if (lt.extruders[i] == last_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(lt.extruders.data() + 1, lt.extruders.data(), i * sizeof(unsigned int));
                    lt.extruders.front() = last_extruder_id;
                    break;
                }
        }
        last_extruder_id = lt.extruders.back();
    }

    // Reindex the extruders, so they are zero based, not 1 based.
    for (LayerTools& lt : m_layer_tools){
        for (unsigned int& extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            --extruder_id;
        }
    }
}

void ToolOrdering::handle_dontcare_extruder(unsigned int last_extruder_id)
{
    if(m_layer_tools.empty())
        return;
    if(last_extruder_id == (unsigned int)-1){
        // The initial print extruder has not been decided yet.
        // Initialize the last_extruder_id with the first non-zero extruder id used for the print.
        last_extruder_id = 0;
        for (size_t i = 0; i < m_layer_tools.size() && last_extruder_id == 0; ++ i) {
            const LayerTools &lt = m_layer_tools[i];
            for (unsigned int extruder_id : lt.extruders)
                if (extruder_id > 0) {
                    last_extruder_id = extruder_id;
                    break;
                }
        }
        if (last_extruder_id == 0)
            // Nothing to extrude.
            return;
    }else{
        // 1 based idx
        ++ last_extruder_id;
    }

    for (LayerTools &lt : m_layer_tools) {
        if (lt.extruders.empty())
            continue;
        if (lt.extruders.size() == 1 && lt.extruders.front() == 0)
            lt.extruders.front() = last_extruder_id;
        else {
            if (lt.extruders.front() == 0)
                // Pop the "don't care" extruder, the "don't care" region will be merged with the next one.
                lt.extruders.erase(lt.extruders.begin());
            // Reorder the extruders to start with the last one.
            for (size_t i = 1; i < lt.extruders.size(); ++ i)
                if (lt.extruders[i] == last_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(lt.extruders.data() + 1, lt.extruders.data(), i * sizeof(unsigned int));
                    lt.extruders.front() = last_extruder_id;
                    break;
                }

            if (lt == m_layer_tools[0]) {
                // On first layer with wipe tower, prefer a soluble extruder
                // at the beginning, so it is not wiped on the first layer.
                if (m_print_config_ptr && m_print_config_ptr->enable_prime_tower) {
                    for (size_t i = 0; i<lt.extruders.size(); ++i)
                        if (m_print_config_ptr->filament_soluble.get_at(lt.extruders[i]-1)) { // 1-based...
                            std::swap(lt.extruders[i], lt.extruders.front());
                            break;
                        }
                }

                // Then, if we specified the tool order, apply it now
                apply_first_layer_order(m_print_full_config, lt.extruders);
            }

        }
        last_extruder_id = lt.extruders.back();
    }

    // Reindex the extruders, so they are zero based, not 1 based.
    for (LayerTools &lt : m_layer_tools){
        for (unsigned int &extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            -- extruder_id;
        }
    }
}

bool ToolOrdering::insert_wipe_tower_extruder()
{
    if (!m_print_config_ptr || !m_print_config_ptr->enable_prime_tower)
        return false;
    if (m_print_config_ptr->wipe_tower_filament == 0)
        return false;

    bool changed = false;
    const unsigned int wipe_extruder = (unsigned int)(m_print_config_ptr->wipe_tower_filament - 1);
    for (LayerTools &lt : m_layer_tools) {
        if (lt.wipe_tower_partitions > 0) {
            if (std::find(lt.extruders.begin(), lt.extruders.end(), wipe_extruder) == lt.extruders.end()) {
                lt.extruders.emplace_back(wipe_extruder);
                changed = true;
            }
        }
    }
    return changed;
}

void ToolOrdering::sort_and_build_data(const Print& print, unsigned int first_extruder, bool prime_multi_material)
{
    // if first extruder is -1, we can decide the first layer tool order before doing reorder function
    // so we shouldn't reorder first layer in reorder function
    bool reorder_first_layer = (first_extruder != (unsigned int)(-1));
    reorder_extruders_for_minimum_flush_volume(reorder_first_layer);
    m_sorted = true;

    double max_layer_height = 0.;
    double object_bottom_z = 0.;
    for (const auto& object : print.objects()) {
        for (const Layer* layer : object->layers()) {
            if (layer->has_extrusions()) {
                object_bottom_z = layer->print_z - layer->height;
                break;
            }
        }
        max_layer_height = std::max(max_layer_height, object->config().layer_height.value);
    }

    max_layer_height = calc_max_layer_height(print.config(), max_layer_height);

    this->fill_wipe_tower_partitions(print.config(), object_bottom_z, max_layer_height);
    if (this->insert_wipe_tower_extruder()) {
        reorder_extruders_for_minimum_flush_volume(reorder_first_layer);
        this->fill_wipe_tower_partitions(print.config(), object_bottom_z, max_layer_height);
    }

    this->collect_extruder_statistics(prime_multi_material);
}

void ToolOrdering::sort_and_build_data(const PrintObject& object , unsigned int first_extruder, bool prime_multi_material)
{
    // if first extruder is -1, we can decide the first layer tool order before doing reorder function
    // so we shouldn't reorder first layer in reorder function
    bool reorder_first_layer = (first_extruder != (unsigned int)(-1));
    reorder_extruders_for_minimum_flush_volume(reorder_first_layer);
    m_sorted = true;

    double max_layer_height = calc_max_layer_height(object.print()->config(), object.config().layer_height);

    this->fill_wipe_tower_partitions(object.print()->config(), object.layers().front()->print_z - object.layers().front()->height, max_layer_height);
    if (this->insert_wipe_tower_extruder()) {
        reorder_extruders_for_minimum_flush_volume(reorder_first_layer);
        this->fill_wipe_tower_partitions(object.print()->config(), object.layers().front()->print_z - object.layers().front()->height, max_layer_height);
    }

    this->collect_extruder_statistics(prime_multi_material);
}


// For the use case when each object is printed separately
// (print->config().print_sequence == PrintSequence::ByObject is true).
ToolOrdering::ToolOrdering(const PrintObject &object, unsigned int first_extruder, bool prime_multi_material)
{
    m_is_BBL_printer = object.print()->is_BBL_printer();
    m_print_full_config = &object.print()->full_print_config();
    m_print_object_ptr = &object;
    m_print = const_cast<Print*>(object.print());
    if (object.layers().empty())
        return;

    // Initialize the print layers for just a single object.
    {
        // construct layer tools by z height
        std::vector<coordf_t> zs;
        zs.reserve(zs.size() + object.layers().size() + object.support_layers().size());
        for (auto layer : object.layers())
            zs.emplace_back(layer->print_z);
        for (auto layer : object.support_layers())
            zs.emplace_back(layer->print_z);
        this->initialize_layers(zs);
    }

    // Collect extruders reuqired to print the layers. Add dontcare extruders
    this->collect_extruders(object, std::vector<std::pair<double, unsigned int>>());

    // BBS
    // Reorder the extruders to minimize tool switches.
    std::vector<unsigned int> first_layer_tool_order;
    if (first_extruder == (unsigned int) -1) {
        first_layer_tool_order = generate_first_layer_tool_order(object);
    }

    if (!first_layer_tool_order.empty()) {
        this->handle_dontcare_extruder(first_layer_tool_order);
    } else {
        this->handle_dontcare_extruder(first_extruder);
    }

    this->collect_extruder_statistics(prime_multi_material);

    double max_layer_height = calc_max_layer_height(object.print()->config(), object.config().layer_height);

    this->mark_skirt_layers(object.print()->config(), max_layer_height);
}

// For the use case when all objects are printed at once.
// (print->config().print_sequence == PrintSequence::ByObject is false).
ToolOrdering::ToolOrdering(const Print &print, unsigned int first_extruder, bool prime_multi_material)
{
    m_is_BBL_printer = print.is_BBL_printer();
    m_print_full_config = &print.full_print_config();
    m_print = const_cast<Print *>(&print);  // for update the context of print
    m_print_config_ptr = &print.config();

    // Initialize the print layers for all objects and all layers.
    coordf_t max_layer_height = 0.;
    {
        std::vector<coordf_t> zs;
        for (auto object : print.objects()) {
            zs.reserve(zs.size() + object->layers().size() + object->support_layers().size());
            for (auto layer : object->layers())
                zs.emplace_back(layer->print_z);
            for (auto layer : object->support_layers())
                zs.emplace_back(layer->print_z);

            max_layer_height = std::max(max_layer_height, object->config().layer_height.value);
        }
        this->initialize_layers(zs);
    }
    max_layer_height = calc_max_layer_height(print.config(), max_layer_height);

	// Use the extruder switches from Model::custom_gcode_per_print_z to override the extruder to print the object.
	// Do it only if all the objects were configured to be printed with a single extruder.
	std::vector<std::pair<double, unsigned int>> per_layer_extruder_switches;

    // BBS
	if (auto num_filaments = unsigned(print.config().filament_diameter.size());
		num_filaments > 1 && print.object_extruders().size() == 1 && // the current Print's configuration is CustomGCode::MultiAsSingle
        //BBS: replace model custom gcode with current plate custom gcode
        print.model().get_curr_plate_custom_gcodes().mode == CustomGCode::MultiAsSingle) {
		// Printing a single extruder platter on a printer with more than 1 extruder (or single-extruder multi-material).
		// There may be custom per-layer tool changes available at the model.
        per_layer_extruder_switches = custom_tool_changes(print.model().get_curr_plate_custom_gcodes(), num_filaments);
	}

    // Collect extruders reuqired to print the layers.
    for (auto object : print.objects())
        this->collect_extruders(*object, per_layer_extruder_switches);

    // Reorder the extruders to minimize tool switches.
    std::vector<unsigned int> first_layer_tool_order;
    if (first_extruder == (unsigned int)-1) {
        first_layer_tool_order = generate_first_layer_tool_order(print);
    }

    if(!first_layer_tool_order.empty())
        this->handle_dontcare_extruder(first_layer_tool_order);
    else
        this->handle_dontcare_extruder(first_extruder);

    this->collect_extruder_statistics(prime_multi_material);

    this->mark_skirt_layers(print.config(), max_layer_height);
}

static void apply_first_layer_order(const DynamicPrintConfig* config, std::vector<unsigned int>& tool_order) {
    const ConfigOptionInts* first_layer_print_sequence_op = config->option<ConfigOptionInts>("first_layer_print_sequence");
    if (first_layer_print_sequence_op) {
        const std::vector<int>& print_sequence_1st = first_layer_print_sequence_op->values;
        if (print_sequence_1st.size() >= tool_order.size()) {
            std::sort(tool_order.begin(), tool_order.end(), [&print_sequence_1st](int lh, int rh) {
                auto lh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), lh);
                auto rh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), rh);

                if (lh_it == print_sequence_1st.end() || rh_it == print_sequence_1st.end())
                    return false;

                return lh_it < rh_it;
            });
        }
    }
}

// [INTENT] BBL addition: determine the first-layer tool order to maximize bed adhesion.
// For each extruder used on the first layer, find the "minimum area" contour it would
// print (the smallest expolygon that is not too thin to survive offsetting by 0.2*line_width).
// Extruders assigned to larger minimum-area contours print first — this ensures the
// smallest, most delicate features (likely thin walls) are printed last, when the
// previous filament layers have warmed the bed enough for adhesion.
//
// [HAZARD] The area heuristic is a proxy for "most likely to detach" — it assumes
// small footprint = fragile. This can produce suboptimal ordering for tall thin objects
// with large footprints on subsequent layers.
// [COUPLING] Calls offset_ex() (Clipper) to test whether each contour survives a
// negative offset of 0.2*initial_layer_line_width — a geometry operation inside
// what is conceptually a "planning" function.
// [STATE] initial_extruder_id and max_minimal_area variables declared but unused — dead code.
// BBS
std::vector<unsigned int> ToolOrdering::generate_first_layer_tool_order(const Print& print)
{
    std::vector<unsigned int> tool_order;
    int initial_extruder_id = -1;
    std::map<int, double> min_areas_per_extruder;

    for (auto object : print.objects()) {
        const Layer* target_layer = nullptr;
        for(auto layer : object->layers()){
            for(auto layerm : layer->regions()){
                for(auto& expoly : layerm->raw_slices){
                    if (!offset_ex(expoly, -0.2 * scale_(print.config().initial_layer_line_width)).empty()) {
                        target_layer = layer;
                        break;
                    }
                }
                if(target_layer)
                    break;
            }
            if(target_layer)
                break;
        }

        if(!target_layer)
            return tool_order;

        for (auto layerm : target_layer->regions()) {
            int extruder_id = layerm->region().config().option("wall_filament")->getInt();

            for (auto expoly : layerm->raw_slices) {
                const double nozzle_diameter = print.config().nozzle_diameter.get_at(0);
                const coordf_t initial_layer_line_width = print.config().get_abs_value("initial_layer_line_width", nozzle_diameter);

                if (offset_ex(expoly, -0.2 * scale_(initial_layer_line_width)).empty())
                    continue;

                double contour_area = expoly.contour.area();
                auto iter = min_areas_per_extruder.find(extruder_id);
                if (iter == min_areas_per_extruder.end()) {
                    min_areas_per_extruder.insert({ extruder_id, contour_area });
                }
                else {
                    if (contour_area < min_areas_per_extruder.at(extruder_id)) {
                        min_areas_per_extruder[extruder_id] = contour_area;
                    }
                }
            }
        }
    }

    double max_minimal_area = 0.;
    for (auto ape : min_areas_per_extruder) {
        auto iter = tool_order.begin();
        for (; iter != tool_order.end(); iter++) {
            if (min_areas_per_extruder.at(*iter) < min_areas_per_extruder.at(ape.first))
                break;
        }

        tool_order.insert(iter, ape.first);
    }

    apply_first_layer_order(m_print_full_config, tool_order);

    return tool_order;
}

std::vector<unsigned int> ToolOrdering::generate_first_layer_tool_order(const PrintObject& object)
{
    std::vector<unsigned int> tool_order;
    int initial_extruder_id = -1;
    std::map<int, double> min_areas_per_extruder;
    const Layer* target_layer = nullptr;
    for(auto layer : object.layers()){
        for(auto layerm : layer->regions()){
            for(auto& expoly : layerm->raw_slices){
                if (!offset_ex(expoly, -0.2 * scale_(object.config().line_width)).empty()) {
                    target_layer = layer;
                    break;
                }
            }
            if(target_layer)
                break;
        }
        if(target_layer)
            break;
    }

    if(!target_layer)
        return tool_order;

    for (auto layerm : target_layer->regions()) {
        int extruder_id = layerm->region().config().option("wall_filament")->getInt();
        for (auto expoly : layerm->raw_slices) {
            const double nozzle_diameter = object.print()->config().nozzle_diameter.get_at(0);
            const coordf_t line_width = object.config().get_abs_value("line_width", nozzle_diameter);

            if (offset_ex(expoly, -0.2 * scale_(line_width)).empty())
                continue;

            double contour_area = expoly.contour.area();
            auto iter = min_areas_per_extruder.find(extruder_id);
            if (iter == min_areas_per_extruder.end()) {
                min_areas_per_extruder.insert({ extruder_id, contour_area });
            }
            else {
                if (contour_area < min_areas_per_extruder.at(extruder_id)) {
                    min_areas_per_extruder[extruder_id] = contour_area;
                }
            }
        }
    }

    double max_minimal_area = 0.;
    for (auto ape : min_areas_per_extruder) {
        auto iter = tool_order.begin();
        for (; iter != tool_order.end(); iter++) {
            if (min_areas_per_extruder.at(*iter) < min_areas_per_extruder.at(ape.first))
                break;
        }

        tool_order.insert(iter, ape.first);
    }

    apply_first_layer_order(m_print_full_config, tool_order);

    return tool_order;
}

// [INTENT] Merge all per-object Z heights into a single sorted, deduplicated list of
// LayerTools entries. Near-duplicate Z values within EPSILON are merged by averaging,
// preventing floating-point noise from creating phantom layers.
// [HAZARD] `sort_remove_duplicates` is a Slic3r helper that sorts + calls std::unique.
// If two Z values are within EPSILON but not exact duplicates, they are merged here.
// This merge changes the Z of all layers within the group to the average — a small
// coordinate shift applied to every subsequent computation on those layers.
void ToolOrdering::initialize_layers(std::vector<coordf_t> &zs)
{
    sort_remove_duplicates(zs);
    // Merge numerically very close Z values.
    for (size_t i = 0; i < zs.size();) {
        // Find the last layer with roughly the same print_z.
        size_t j = i + 1;
        coordf_t zmax = zs[i] + EPSILON;
        for (; j < zs.size() && zs[j] <= zmax; ++ j) ;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        m_layer_tools.emplace_back(LayerTools(0.5 * (zs[i] + zs[j-1])));
        i = j;
    }
}

// [INTENT] For each layer in the object, determine which extruders (filament slots) are
// required. The result populates LayerTools::extruders (the ordered extruder list for
// that layer). Three sources:
//   1. Per-layer extruder switches from CustomGCode (user-painted assignments)
//   2. Region config (wall_filament, solid_infill_filament, sparse_infill_filament)
//   3. Support extruders (support_filament, support_interface_filament)
//
// wiping_extrusions().is_overriddable_and_mark() marks which extrusion entities can be
// reassigned to a different extruder for in-object wiping (when wipe_into_objects is
// enabled). Only non-overriddable extrusions force an extruder change at the layer level.
//
// [STATE] Writes to `layer_tools.extruders` (mutates m_layer_tools).
// [STATE] Writes to `object.object_first_layer_wall_extruders` via const_cast — BBL addition
//         that mutates PrintObject from a function conceptually only reading it.
// [HAZARD] const_cast on a const& is undefined behavior if the actual object was declared
//          const. In practice PrintObject is never truly const-constructed, so this is
//          a bug with tolerated behavior rather than a formal UB.
// [COUPLING] Directly iterates layer->regions() and support_layer->support_fills —
//            tightly coupled to the Layer/LayerRegion/SupportLayer internal structure.
// Collect extruders reuqired to print layers.
void ToolOrdering::collect_extruders(const PrintObject &object, const std::vector<std::pair<double, unsigned int>> &per_layer_extruder_switches)
{
    // Extruder overrides are ordered by print_z.
    std::vector<std::pair<double, unsigned int>>::const_iterator it_per_layer_extruder_override;
	it_per_layer_extruder_override = per_layer_extruder_switches.begin();
    unsigned int extruder_override = 0;

    // BBS: collect first layer extruders of an object's wall, which will be used by brim generator
    int layerCount = 0;
    std::vector<int> firstLayerExtruders;
    firstLayerExtruders.clear();

    // Collect the object extruders.
    for (auto layer : object.layers()) {
        LayerTools &layer_tools = this->tools_for_layer(layer->print_z);

        // Override extruder with the next
    	for (; it_per_layer_extruder_override != per_layer_extruder_switches.end() && it_per_layer_extruder_override->first < layer->print_z + EPSILON; ++ it_per_layer_extruder_override)
    		extruder_override = (int)it_per_layer_extruder_override->second;

        // Store the current extruder override (set to zero if no overriden), so that layer_tools.wiping_extrusions().is_overridable_and_mark() will use it.
        layer_tools.extruder_override = extruder_override;

        // What extruders are required to print this object layer?
        for (const LayerRegion *layerm : layer->regions()) {
            const PrintRegion &region = layerm->region();

            if (! layerm->perimeters.entities.empty()) {
                bool something_nonoverriddable = true;

                if (m_print_config_ptr) { // in this case print->config().print_sequence != PrintSequence::ByObject (see ToolOrdering constructors)
                    something_nonoverriddable = false;
                    for (const auto& eec : layerm->perimeters.entities) // let's check if there are nonoverriddable entities
                        if (!layer_tools.wiping_extrusions().is_overriddable_and_mark(dynamic_cast<const ExtrusionEntityCollection&>(*eec), *m_print_config_ptr, object, region))
                            something_nonoverriddable = true;
                }

                if (something_nonoverriddable){
               		layer_tools.extruders.emplace_back((extruder_override == 0) ? region.config().wall_filament.value : extruder_override);
                    if (layerCount == 0) {
                        firstLayerExtruders.emplace_back((extruder_override == 0) ? region.config().wall_filament.value : extruder_override);
                    }
                }

                layer_tools.has_object = true;
            }

            bool has_infill       = false;
            bool has_solid_infill = false;
            bool something_nonoverriddable = false;
            for (const ExtrusionEntity *ee : layerm->fills.entities) {
                // fill represents infill extrusions of a single island.
                const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                ExtrusionRole role = fill->entities.empty() ? erNone : fill->entities.front()->role();
                if (is_solid_infill(role))
                    has_solid_infill = true;
                else if (role != erNone)
                    has_infill = true;

                if (m_print_config_ptr) {
                    if (! layer_tools.wiping_extrusions().is_overriddable_and_mark(*fill, *m_print_config_ptr, object, region))
                        something_nonoverriddable = true;
                }
            }

            if (something_nonoverriddable || !m_print_config_ptr) {
            	if (extruder_override == 0) {
	                if (has_solid_infill)
	                    layer_tools.extruders.emplace_back(region.config().solid_infill_filament);
	                if (has_infill)
	                    layer_tools.extruders.emplace_back(region.config().sparse_infill_filament);
            	} else if (has_solid_infill || has_infill)
            		layer_tools.extruders.emplace_back(extruder_override);
            }
            if (has_solid_infill || has_infill)
                layer_tools.has_object = true;
        }
        layerCount++;
    }

    sort_remove_duplicates(firstLayerExtruders);
    const_cast<PrintObject&>(object).object_first_layer_wall_extruders = firstLayerExtruders;

    // Collect the support extruders.
    for (auto support_layer : object.support_layers()) {
        LayerTools   &layer_tools   = this->tools_for_layer(support_layer->print_z);
        ExtrusionRole role          = support_layer->support_fills.role();
        bool          has_support   = false;
        bool          has_interface = false;
        for (const ExtrusionEntity *ee : support_layer->support_fills.entities) {
            ExtrusionRole er = ee->role();
            if (er == erSupportMaterial || er == erSupportTransition) has_support = true;
            if (er == erSupportMaterialInterface) has_interface = true;
            if (has_support && has_interface) break;
        }
        unsigned int extruder_support   = object.config().support_filament.value;
        unsigned int extruder_interface = object.config().support_interface_filament.value;
        if (has_support) {
            if (extruder_support > 0 || !has_interface || extruder_interface == 0 || layer_tools.has_object)
                layer_tools.extruders.push_back(extruder_support);
            else {
                auto all_extruders     = object.print()->extruders();
                auto get_next_extruder = [&](int current_extruder, const std::vector<unsigned int> &extruders) {
                    std::vector<float> flush_matrix(
                        cast<float>(get_flush_volumes_matrix(object.print()->config().flush_volumes_matrix.values, 0, object.print()->config().nozzle_diameter.values.size())));
                    const unsigned int number_of_extruders = (unsigned int) (sqrt(flush_matrix.size()) + EPSILON);
                    // Extract purging volumes for each extruder pair:
                    std::vector<std::vector<float>> wipe_volumes;
                    for (unsigned int i = 0; i < number_of_extruders; ++i)
                        wipe_volumes.push_back(std::vector<float>(flush_matrix.begin() + i * number_of_extruders, flush_matrix.begin() + (i + 1) * number_of_extruders));
                    int   next_extruder = current_extruder;
                    float min_flush     = std::numeric_limits<float>::max();
                    for (auto extruder_id : extruders) {
                        if (object.print()->config().filament_soluble.get_at(extruder_id) || extruder_id == current_extruder) continue;
                        if (wipe_volumes[extruder_interface - 1][extruder_id] < min_flush) {
                            next_extruder = extruder_id;
                            min_flush     = wipe_volumes[extruder_interface - 1][extruder_id];
                        }
                    }
                    return next_extruder;
                };
                bool interface_not_for_body = object.config().support_interface_not_for_body;
                layer_tools.extruders.push_back(get_next_extruder(interface_not_for_body ? extruder_interface - 1 : -1, all_extruders) + 1);
            }
        }
        if (has_interface) layer_tools.extruders.push_back(extruder_interface);
        if (has_support || has_interface) {
            layer_tools.has_support = true;
            layer_tools.wiping_extrusions().is_support_overriddable_and_mark(role, object);
        }
    }

    for (auto& layer : m_layer_tools) {
        // Sort and remove duplicates
        sort_remove_duplicates(layer.extruders);

        // make sure that there are some tools for each object layer (e.g. tall wiping object will result in empty extruders vector)
        if (layer.extruders.empty() && layer.has_object)
            layer.extruders.emplace_back(0); // 0="dontcare" extruder - it will be taken care of in reorder_extruders
    }
}


// [INTENT] Calculate how many wipe tower "partitions" (print segments) are needed per
// layer. A partition corresponds to one tool change plus its associated purge volume.
// The number of partitions is: count(extruders on layer) - 1 if first extruder matches
// the previously active extruder, otherwise count(extruders).
//
// Propagation logic: wipe tower partitions propagate downward — if layer N needs K
// partitions, all layers below also need at least K partitions. This ensures the wipe
// tower is printed to a sufficient height to support layers above.
//
// has_wipe_tower is also set for:
//  - Layers below raft (fills wipe tower during raft-to-object gap)
//  - Smooth timelapse mode (every layer with object extrusions)
//  - Wrapping detection layers (BBL: config.wrapping_detection_layers)
//
// [HAZARD] The comment "FIXME this is a hack to get the ball rolling" at line ~915 has
// persisted since PrusaSlicer days. The timelapse condition forces a wipe tower on every
// single layer with object content regardless of whether a tool change occurred — this
// can significantly increase print time for smooth timelapse mode.
// [HAZARD] A raft gap > max_layer_height triggers insertion of a new LayerTools entry
// into m_layer_tools mid-iteration. Insertions into std::vector invalidate all iterators
// but the outer loop uses indices, not iterators — correct here, but fragile if refactored.
void ToolOrdering::fill_wipe_tower_partitions(const PrintConfig &config, coordf_t object_bottom_z, coordf_t max_layer_height)
{
    if (m_layer_tools.empty())
        return;

    // Count the minimum number of tool changes per layer.
    size_t last_extruder = size_t(-1);
    for (LayerTools &lt : m_layer_tools) {
        lt.wipe_tower_partitions = lt.extruders.size();
        if (! lt.extruders.empty()) {
            if (last_extruder == size_t(-1) || last_extruder == lt.extruders.front())
                // The first extruder on this layer is equal to the current one, no need to do an initial tool change.
                -- lt.wipe_tower_partitions;
            last_extruder = lt.extruders.back();
        }
    }

    // Propagate the wipe tower partitions down to support the upper partitions by the lower partitions.
    for (int i = int(m_layer_tools.size()) - 2; i >= 0; -- i)
        m_layer_tools[i].wipe_tower_partitions = std::max(m_layer_tools[i + 1].wipe_tower_partitions, m_layer_tools[i].wipe_tower_partitions);


    int wrapping_layer_nums = config.wrapping_detection_layers;
    for (size_t i = 0; i < wrapping_layer_nums; ++i) {
        if (i >= m_layer_tools.size())
            break;
        LayerTools &lt    = m_layer_tools[i];
        lt.has_wipe_tower = config.enable_wrapping_detection;
    }

    //FIXME this is a hack to get the ball rolling.
    for (LayerTools &lt : m_layer_tools)
        lt.has_wipe_tower |= (lt.has_object && (config.timelapse_type == TimelapseType::tlSmooth || lt.wipe_tower_partitions > 0))
            || lt.print_z < object_bottom_z + EPSILON;

    // Test for a raft, insert additional wipe tower layer to fill in the raft separation gap.
    for (size_t i = 0; i + 1 < m_layer_tools.size(); ++ i) {
        const LayerTools &lt      = m_layer_tools[i];
        const LayerTools &lt_next = m_layer_tools[i + 1];
        if (lt.print_z < object_bottom_z + EPSILON && lt_next.print_z >= object_bottom_z + EPSILON) {
            // lt is the last raft layer. Find the 1st object layer.
            size_t j = i + 1;
            for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_wipe_tower; ++ j);
            if (j < m_layer_tools.size()) {
                const LayerTools &lt_object = m_layer_tools[j];
                coordf_t gap = lt_object.print_z - lt.print_z;
                assert(gap > 0.f);
                if (gap > max_layer_height + EPSILON) {
                    // Insert one additional wipe tower layer between lh.print_z and lt_object.print_z.
                    LayerTools lt_new(0.5f * (lt.print_z + lt_object.print_z));
                    // Find the 1st layer above lt_new.
                    for (j = i + 1; j < m_layer_tools.size() && m_layer_tools[j].print_z < lt_new.print_z - EPSILON; ++ j);
                    if (std::abs(m_layer_tools[j].print_z - lt_new.print_z) < EPSILON) {
						m_layer_tools[j].has_wipe_tower = true;
					} else {
						LayerTools &lt_extra = *m_layer_tools.insert(m_layer_tools.begin() + j, lt_new);
                        //LayerTools &lt_prev  = m_layer_tools[j];
                        LayerTools &lt_next  = m_layer_tools[j + 1];
                        assert(! m_layer_tools[j - 1].extruders.empty() && ! lt_next.extruders.empty());
                        // FIXME: Following assert tripped when running combine_infill.t. I decided to comment it out for now.
                        // If it is a bug, it's likely not critical, because this code is unchanged for a long time. It might
                        // still be worth looking into it more and decide if it is a bug or an obsolete assert.
                        //assert(lt_prev.extruders.back() == lt_next.extruders.front());
                        lt_extra.has_wipe_tower = true;
                        lt_extra.extruders.push_back(lt_next.extruders.front());
                        lt_extra.wipe_tower_partitions = lt_next.wipe_tower_partitions;
                    }
                }
            }
            break;
        }
    }

    // If the model contains empty layers (such as https://github.com/prusa3d/Slic3r/issues/1266), there might be layers
    // that were not marked as has_wipe_tower, even when they should have been. This produces a crash with soluble supports
    // and maybe other problems. We will therefore go through layer_tools and detect and fix this.
    // So, if there is a non-object layer starting with different extruder than the last one ended with (or containing more than one extruder),
    // we'll mark it with has_wipe tower.
    for (unsigned int i=0; i+1<m_layer_tools.size(); ++i) {
        LayerTools& lt = m_layer_tools[i];
        LayerTools& lt_next = m_layer_tools[i+1];
        if (lt.extruders.empty() || lt_next.extruders.empty())
            break;
        if (!lt_next.has_wipe_tower && (lt_next.extruders.front() != lt.extruders.back() || lt_next.extruders.size() > 1))
            lt_next.has_wipe_tower = true;
        // We should also check that the next wipe tower layer is no further than max_layer_height:
        unsigned int j = i+1;
        double last_wipe_tower_print_z = lt_next.print_z;
        while (++j < m_layer_tools.size()-1 && !m_layer_tools[j].has_wipe_tower)
            if (m_layer_tools[j+1].print_z - last_wipe_tower_print_z > max_layer_height + EPSILON) {
                if (!config.enable_wrapping_detection)
                    m_layer_tools[j].has_wipe_tower = true;
                last_wipe_tower_print_z = m_layer_tools[j].print_z;
            }
    }

    // Calculate the wipe_tower_layer_height values.
    coordf_t wipe_tower_print_z_last = 0.;
    for (LayerTools &lt : m_layer_tools)
        if (lt.has_wipe_tower) {
            lt.wipe_tower_layer_height = lt.print_z - wipe_tower_print_z_last;
            wipe_tower_print_z_last = lt.print_z;
        }
}

// [INTENT] Populate summary statistics: first/last extruder used across all layers,
// and the union of all extruders used (m_all_printing_extruders). Called after
// reorder_extruders_for_minimum_flush_volume so the order already reflects flush optimisation.
// [STATE] Writes m_first_printing_extruder, m_last_printing_extruder,
// m_all_printing_extruders — these are queried by GCode.cpp to emit initial tool-select
// and prime sequences.
// [HAZARD] If prime_multi_material is true the function reorders m_all_printing_extruders
// so that m_first_printing_extruder ends last, then re-assigns m_first_printing_extruder
// to the new front — this mutates the extruder sequence used for priming.
void ToolOrdering::collect_extruder_statistics(bool prime_multi_material)
{
    m_first_printing_extruder = (unsigned int)-1;
    for (const auto &lt : m_layer_tools)
        if (! lt.extruders.empty()) {
            m_first_printing_extruder = lt.extruders.front();
            break;
        }

    m_last_printing_extruder = (unsigned int)-1;
    for (auto lt_it = m_layer_tools.rbegin(); lt_it != m_layer_tools.rend(); ++ lt_it)
        if (! lt_it->extruders.empty()) {
            m_last_printing_extruder = lt_it->extruders.back();
            break;
        }

    m_all_printing_extruders.clear();
    for (const auto &lt : m_layer_tools) {
        append(m_all_printing_extruders, lt.extruders);
        sort_remove_duplicates(m_all_printing_extruders);
    }

    if (prime_multi_material && ! m_all_printing_extruders.empty()) {
        // Reorder m_all_printing_extruders in the sequence they will be primed, the last one will be m_first_printing_extruder.
        // Then set m_first_printing_extruder to the 1st extruder primed.
        m_all_printing_extruders.erase(
            std::remove_if(m_all_printing_extruders.begin(), m_all_printing_extruders.end(),
                [ this ](const unsigned int eid) { return eid == m_first_printing_extruder; }),
            m_all_printing_extruders.end());
        m_all_printing_extruders.emplace_back(m_first_printing_extruder);
        m_first_printing_extruder = m_all_printing_extruders.front();
    }
}

void ToolOrdering::cal_most_used_extruder(const PrintConfig &config)
{
    // record
    std::vector<int> extruder_count;
    extruder_count.resize(config.nozzle_diameter.size(), 0);
    for (LayerTools &layer_tools : m_layer_tools) {
        std::vector<unsigned int> filaments = layer_tools.extruders;
        std::set<int> layer_extruder_count;
        //count once only
        for (unsigned int &filament : filaments) {
            layer_extruder_count.insert(config.filament_map.values[filament] - 1);
        }

        //record
        for (int extruder_id : layer_extruder_count) {
            extruder_count[extruder_id]++;
        }
    }

    // set key for most used extruder
    // count most used extruder
    most_used_extruder = 0;
    for (int extruder_id = 1; extruder_id < extruder_count.size(); extruder_id++) {
        if (extruder_count[extruder_id] >= extruder_count[most_used_extruder])
            most_used_extruder = extruder_id;
    }
}

float ToolOrdering::cal_max_additional_fan(const PrintConfig &config)
{
    // record
    float max_fan = 0;
    for (LayerTools &layer_tools : m_layer_tools) {
        std::vector<unsigned int> filaments = layer_tools.extruders;
        std::set<int>             layer_extruder_count;
        // count once only
        for (unsigned int &filament : filaments)
            if (max_fan < config.additional_cooling_fan_speed.get_at(filament))
                max_fan = config.additional_cooling_fan_speed.get_at(filament);
    }
    return max_fan;
}


//BBS: find first non support filament
bool ToolOrdering::cal_non_support_filaments(const PrintConfig &config,
                                                         unsigned int &     first_non_support_filament,
                                                         std::vector<int> & initial_non_support_filaments,
                                                         std::vector<int> & initial_filaments)
{
    int find_count = 0;
    int find_first_filaments_count = 0;
    bool has_non_support = has_non_support_filament(config);
    for (const LayerTools &layer_tool : m_layer_tools) {
        for (const unsigned int &filament : layer_tool.extruders) {
            //check first filament
            if (!config.filament_map.values.empty() && initial_filaments[config.filament_map.values[filament] - 1] == -1) {
                initial_filaments[config.filament_map.values[filament] - 1] = filament;
                find_first_filaments_count++;
            }

            if (has_non_support) {
                // check first non support filaments
                if (config.filament_is_support.get_at(filament))
                    continue;

                if (first_non_support_filament == (unsigned int) -1) first_non_support_filament = filament;

                // params missing, add protection
                // filament map missing means single nozzle, no need to set initial_non_support_filaments
                if (config.filament_map.values.empty())
                    return true;

                if (initial_non_support_filaments[config.filament_map.values[filament] - 1] == -1) {
                    initial_non_support_filaments[config.filament_map.values[filament] - 1] = filament;
                    find_count++;
                }

                if (find_count == initial_non_support_filaments.size())
                    return true;
            } else if (find_first_filaments_count == initial_filaments.size() || config.filament_map.values.empty()){
                    return false;
            }

        }
    }

    return false;
}

bool ToolOrdering::has_non_support_filament(const PrintConfig &config) {
    for (const unsigned int &filament : m_all_printing_extruders) {
        if (!config.filament_is_support.get_at(filament)) {
            return true;
        }
    }

    return false;
}

// [INTENT] Helper: generate all unique bipartitions of an extruder set.
// Used by the flush-minimisation solver to enumerate how filaments can be split
// between two nozzles. Returns a set of (group1, group2) pairs where group1.size()
// <= group2.size() to avoid duplicates.
// [HAZARD] Complexity is O(2^n / 2 * n) — only practical for small n (BBL printers
// have at most ~16 filaments, so n <= 16 in practice).
std::set<std::pair<std::vector<unsigned int>, std::vector<unsigned int>>> generate_combinations(const std::vector<unsigned int> &extruders)
{
    int                                                                       n = extruders.size();
    std::vector<bool>                                                         flags(n);
    std::set<std::pair<std::vector<unsigned int>, std::vector<unsigned int>>> unique_combinations;

    if (extruders.empty())
        return unique_combinations;

    for (int i = 1; i <= n / 2; ++i) {
        std::fill(flags.begin(), flags.begin() + i, true);
        std::fill(flags.begin() + i, flags.end(), false);

        do {
            std::vector<unsigned int> group1, group2;
            for (int j = 0; j < n; ++j) {
                if (flags[j]) {
                    group1.push_back(extruders[j]);
                } else {
                    group2.push_back(extruders[j]);
                }
            }

            if (group1.size() > group2.size()) { std::swap(group1, group2); }

            unique_combinations.insert({group1, group2});

        } while (std::prev_permutation(flags.begin(), flags.end()));
    }

    return unique_combinations;
}

// [INTENT] Compute total flush volume for a single layer given a filament-to-nozzle map.
// Iterates each nozzle's filament sequence and sums consecutive transition costs from the
// flush matrix.  Used to score candidate filament orderings in the optimiser.
// [COUPLING] nozzle_flush_mtx is a vector-of-matrices indexed [nozzle][from][to].
float get_flush_volume(const std::vector<int> &filament_maps, const std::vector<unsigned int> &extruders, const std::vector<FlushMatrix> &matrix, size_t nozzle_nums)
{
    std::vector<std::vector<unsigned int>> nozzle_filaments;
    nozzle_filaments.resize(nozzle_nums);

    for (unsigned int filament_id : extruders) {
        nozzle_filaments[filament_maps[filament_id]].emplace_back(filament_id);
    }

    float flush_volume = 0;
    for (size_t nozzle_id = 0; nozzle_id < nozzle_nums; ++nozzle_id) {
        for (size_t i = 0; i + 1 < nozzle_filaments[nozzle_id].size(); ++i) {
            flush_volume += matrix[nozzle_id][nozzle_filaments[nozzle_id][i]][nozzle_filaments[nozzle_id][i+1]];
        }
    }

    return flush_volume;
}

// [INTENT] BBL-specific: Recommend a filament-to-nozzle assignment (filament map) to
// minimise flush waste across the full print.  Invoked only for BBL 2-nozzle printers;
// falls back to identity map (filament i → nozzle i) for non-BBL multi-extruder printers.
// [STATE] Does NOT mutate m_layer_tools — pure computation returning a vector<int> indexed
// by filament ID with 0-based nozzle IDs as values.
// [COUPLING] Delegates to FilamentGroup / FilamentGroupUtils subsystem which implements
// the combinatorial optimiser (FlushMode vs MatchMode strategies).
// [HAZARD] Calls get_recommended_filament_maps which may also call
// check_filament_printable_after_group — that throws RuntimeError on incompatible groupings.
// [HAZARD] master_extruder_id is stored 1-based in config but converted here with -1 to
// 0-based before passing to FilamentGroupContext — the same conversion hazard as elsewhere.
// [HAZARD] TPU filament special-case bypasses the normal FilamentGroup solver entirely
// and uses calc_filament_group_for_tpu instead — mixing TPU and non-TPU may fall through
// both branches and return master_extruder_id defaults.
std::vector<int> ToolOrdering::get_recommended_filament_maps(const std::vector<std::vector<unsigned int>>& layer_filaments, const Print* print,  const FilamentMapMode mode,const std::vector<std::set<int>>&physical_unprintables,const std::vector<std::set<int>>&geometric_unprintables)
{
    using namespace FilamentGroupUtils;
    if (!print || layer_filaments.empty())
        return std::vector<int>();

    const auto& print_config = print->config();
    const unsigned int filament_nums = (unsigned int)(print_config.filament_colour.values.size() + EPSILON);

    // get flush matrix
    std::vector<FlushMatrix> nozzle_flush_mtx;
    size_t extruder_nums = print_config.nozzle_diameter.values.size();
    for (size_t nozzle_id = 0; nozzle_id < extruder_nums; ++nozzle_id) {
        std::vector<float>              flush_matrix(cast<float>(get_flush_volumes_matrix(print_config.flush_volumes_matrix.values, nozzle_id, extruder_nums)));
        std::vector<std::vector<float>> wipe_volumes;
        for (unsigned int i = 0; i < filament_nums; ++i)
            wipe_volumes.push_back(std::vector<float>(flush_matrix.begin() + i * filament_nums, flush_matrix.begin() + (i + 1) * filament_nums));

        nozzle_flush_mtx.emplace_back(wipe_volumes);
    }
    auto flush_multiplies = print_config.flush_multiplier.values;
    flush_multiplies.resize(extruder_nums, 1);
    for (size_t nozzle_id = 0; nozzle_id < extruder_nums; ++nozzle_id) {
        for (auto& vec : nozzle_flush_mtx[nozzle_id]) {
            for (auto& v : vec)
                v *= flush_multiplies[nozzle_id];
        }
    }

    std::vector<LayerPrintSequence> other_layers_seqs = get_other_layers_print_sequence(print_config.other_layers_print_sequence_nums.value, print_config.other_layers_print_sequence.values);

    // other_layers_seq: the layer_idx and extruder_idx are base on 1
    auto get_custom_seq = [&other_layers_seqs](int layer_idx, std::vector<int>& out_seq) -> bool {
        for (size_t idx = other_layers_seqs.size() - 1; idx != size_t(-1); --idx) {
            const auto& other_layers_seq = other_layers_seqs[idx];
            if (layer_idx + 1 >= other_layers_seq.first.first && layer_idx + 1 <= other_layers_seq.first.second) {
                out_seq = other_layers_seq.second;
                return true;
            }
        }
        return false;
        };

    int master_extruder_id = print_config.master_extruder_id.value -1; // switch to 0 based idx
    std::vector<int>ret(filament_nums, master_extruder_id);
    bool ignore_ext_filament = false; // TODO: read from config
    // if mutli_extruder, calc group,otherwise set to 0
    if (extruder_nums == 2 && print->is_BBL_printer()) {
        std::vector<std::string> extruder_ams_count_str = print_config.extruder_ams_count.values;
        auto extruder_ams_counts = get_extruder_ams_count(extruder_ams_count_str);
        std::vector<int> group_size = calc_max_group_size(extruder_ams_counts, ignore_ext_filament);

        auto machine_filament_info = build_machine_filaments(print->get_extruder_filament_info(), extruder_ams_counts, ignore_ext_filament);

        std::vector<std::string> filament_types = print_config.filament_type.values;
        std::vector<std::string> filament_colours = print_config.filament_colour.values;
        std::vector<unsigned char> filament_is_support = print_config.filament_is_support.values;
        std::vector<std::string> filament_ids = print_config.filament_ids.values;
        // speacially handle tpu filaments
        auto used_filaments = collect_sorted_used_filaments(layer_filaments);
        auto tpu_filaments = get_filament_by_type(used_filaments, &print_config, "TPU");
        FGMode fg_mode = mode == FilamentMapMode::fmmAutoForMatch ? FGMode::MatchMode: FGMode::FlushMode;

        std::vector<std::set<int>> ext_unprintable_filaments;
        collect_unprintable_limits(physical_unprintables, geometric_unprintables, ext_unprintable_filaments);

        FilamentGroupContext context;
        {
            context.model_info.flush_matrix = std::move(nozzle_flush_mtx);
            context.model_info.unprintable_filaments = ext_unprintable_filaments;
            context.model_info.layer_filaments = layer_filaments;
            context.model_info.filament_ids = filament_ids;

            for (size_t idx = 0; idx < filament_types.size(); ++idx) {
                FilamentGroupUtils::FilamentInfo info;
                info.color = filament_colours[idx];
                info.type = filament_types[idx];
                info.is_support = filament_is_support[idx];
                context.model_info.filament_info.emplace_back(std::move(info));
            }

            context.machine_info.machine_filament_info = machine_filament_info;
            context.machine_info.max_group_size = std::move(group_size);
            context.machine_info.master_extruder_id = master_extruder_id;

            context.group_info.total_filament_num = (int)(filament_nums);
            context.group_info.max_gap_threshold = 0.01;
            context.group_info.strategy = FGStrategy::BestCost;
            context.group_info.mode = fg_mode;
            context.group_info.ignore_ext_filament = ignore_ext_filament;
        }


        if (!tpu_filaments.empty()) {
            ret = calc_filament_group_for_tpu(tpu_filaments, context.group_info.total_filament_num, context.machine_info.master_extruder_id);
        }
        else {
            FilamentGroup fg(context);
            fg.get_custom_seq = get_custom_seq;
            ret = fg.calc_filament_group();
        }
    } else if (extruder_nums > 1) {
        // For non-bbl multi-extruder printers we don't support filament group yet, and we use filament id as extruder id
        assert(extruder_nums == filament_nums);
        for (int i = 0; i < filament_nums; i++) {
            ret[i] = i;
        }
    }

    return ret;
}

FilamentChangeStats ToolOrdering::get_filament_change_stats(FilamentChangeMode mode)
{
    switch (mode)
    {
    case Slic3r::ToolOrdering::SingleExt:
        return m_stats_by_single_extruder;
    case Slic3r::ToolOrdering::MultiExtBest:
        return m_stats_by_multi_extruder_best;
    case Slic3r::ToolOrdering::MultiExtCurr:
        return m_stats_by_multi_extruder_curr;
    default:
        break;
    }
    return m_stats_by_single_extruder;
}

// [INTENT] Main flush-optimisation entry point.  Builds a per-nozzle flush cost matrix
// from config, determines which filaments are unprintable on each nozzle (geometric and
// physical constraints), computes or reads the filament-to-nozzle map, then calls
// reorder_filaments_for_minimum_flush_volume() to permute each layer's extruder list so
// that consecutive tool changes consume the least purge volume.  The reordered sequences
// are written back into m_layer_tools[i].extruders.
// [STATE] Mutates: m_layer_tools[*].extruders, m_stats_by_single_extruder,
// m_stats_by_multi_extruder_curr, m_stats_by_multi_extruder_best.
// [STATE] Side-effects on Print: may call m_print->update_filament_maps_to_config()
// to persist the computed filament map back into the live config.
// [COUPLING] Depends on: Print::get_filament_maps(), get_filament_map_mode(),
// get_geometric_unprintable_filaments(), get_physical_unprintable_filaments(),
// get_recommended_filament_maps(), reorder_filaments_for_minimum_flush_volume().
// [HAZARD] filament_maps values are toggled between 0-based and 1-based multiple times
// in the same function (transform +1, then transform -1).  A refactor that breaks this
// symmetry will silently mis-map filaments.
// [HAZARD] For non-BBL single extruder mode the filament map is all-zeros, so
// nozzle_flush_mtx[0] is the only matrix used — multi-nozzle path is skipped entirely.
// [HAZARD] When print_sequence == ByObject AND there is more than one object, filament
// map checking is skipped (the check is deferred to Print.cpp) — inconsistent split of
// responsibility.
void ToolOrdering::reorder_extruders_for_minimum_flush_volume(bool reorder_first_layer)
{
    const PrintConfig* print_config = m_print_config_ptr;
    if (!print_config && m_print_object_ptr) {
        print_config = &(m_print_object_ptr->print()->config());
    }

    if (!print_config || m_layer_tools.empty())
        return;

    const unsigned int number_of_extruders = (unsigned int)(print_config->filament_colour.values.size() + EPSILON);

    using FlushMatrix = std::vector<std::vector<float>>;
    size_t             nozzle_nums = print_config->nozzle_diameter.values.size();

    std::vector<FlushMatrix> nozzle_flush_mtx;
    for (size_t nozzle_id = 0; nozzle_id < nozzle_nums; ++nozzle_id) {
        std::vector<float> flush_matrix(cast<float>(get_flush_volumes_matrix(print_config->flush_volumes_matrix.values, nozzle_id, nozzle_nums)));
        std::vector<std::vector<float>> wipe_volumes;
        if ((print_config->purge_in_prime_tower && print_config->single_extruder_multi_material) || m_is_BBL_printer) {
            for (unsigned int i = 0; i < number_of_extruders; ++i)
                wipe_volumes.push_back(std::vector<float>(flush_matrix.begin() + i * number_of_extruders, flush_matrix.begin() + (i + 1) * number_of_extruders));
        } else {
            // populate wipe_volumes with prime_volume
            for (unsigned int i = 0; i < number_of_extruders; ++i)
                wipe_volumes.push_back(std::vector<float>(number_of_extruders, print_config->prime_volume));
        }
        nozzle_flush_mtx.emplace_back(wipe_volumes);
    }

    auto flush_multiplies = print_config->flush_multiplier.values;
    flush_multiplies.resize(nozzle_nums, 1);
    for (size_t nozzle_id = 0; nozzle_id < nozzle_nums; ++nozzle_id) {
        for (auto& vec : nozzle_flush_mtx[nozzle_id]) {
            for (auto& v : vec)
                v *= flush_multiplies[nozzle_id];
        }
    }

    std::vector<int>filament_maps(number_of_extruders, 0);
    FilamentMapMode map_mode = FilamentMapMode::fmmAutoForFlush;

    std::vector<std::vector<unsigned int>> layer_filaments;
    for (auto& lt : m_layer_tools) {
        layer_filaments.emplace_back(lt.extruders);
    }

    std::vector<unsigned int> used_filaments = collect_sorted_used_filaments(layer_filaments);

    std::vector<std::set<int>>geometric_unprintables = m_print->get_geometric_unprintable_filaments();
    std::vector<std::set<int>>physical_unprintables = m_print->get_physical_unprintable_filaments(used_filaments);

    filament_maps = m_print->get_filament_maps();
    map_mode = m_print->get_filament_map_mode();
    // only check and map in sequence mode, in by object mode, we check the map in print.cpp
    if (print_config->print_sequence != PrintSequence::ByObject || m_print->objects().size() == 1) {
        if (map_mode < FilamentMapMode::fmmManual) {
            const PrintConfig* print_config = m_print_config_ptr;
            if (!print_config && m_print_object_ptr) {
                print_config = &(m_print_object_ptr->print()->config());
            }

            filament_maps = ToolOrdering::get_recommended_filament_maps(layer_filaments, m_print, map_mode, physical_unprintables, geometric_unprintables);

            if (filament_maps.empty())
                return;
            std::transform(filament_maps.begin(), filament_maps.end(), filament_maps.begin(), [](int value) { return value + 1; });
            m_print->update_filament_maps_to_config(filament_maps);
        }
        std::transform(filament_maps.begin(), filament_maps.end(), filament_maps.begin(), [](int value) { return value - 1; });

        if (m_print->is_BBL_printer())
        check_filament_printable_after_group(used_filaments, filament_maps, print_config);
    }
    else {
        // we just need to change the map to 0 based
        std::transform(filament_maps.begin(), filament_maps.end(), filament_maps.begin(), [](int value) {return value - 1; });
    }

    std::vector<std::vector<unsigned int>>filament_sequences;
    std::vector<unsigned int>filament_lists(number_of_extruders);
    std::iota(filament_lists.begin(), filament_lists.end(), 0);

    std::vector<LayerPrintSequence> other_layers_seqs;
    const ConfigOptionInts* other_layers_print_sequence_op = print_config->option<ConfigOptionInts>("other_layers_print_sequence");
    const ConfigOptionInt* other_layers_print_sequence_nums_op = print_config->option<ConfigOptionInt>("other_layers_print_sequence_nums");
    if (other_layers_print_sequence_op && other_layers_print_sequence_nums_op) {
        const std::vector<int>& print_sequence = other_layers_print_sequence_op->values;
        int                     sequence_nums = other_layers_print_sequence_nums_op->value;
        other_layers_seqs = get_other_layers_print_sequence(sequence_nums, print_sequence);
    }

    std::vector<unsigned int>first_layer_filaments;
    if (!m_layer_tools.empty())
        first_layer_filaments = m_layer_tools[0].extruders;

    // other_layers_seq: the layer_idx and extruder_idx are base on 1
    auto get_custom_seq = [&other_layers_seqs, &reorder_first_layer, &first_layer_filaments](int layer_idx, std::vector<int>& out_seq) -> bool {
        if (!reorder_first_layer && layer_idx == 0) {
            out_seq.resize(first_layer_filaments.size());
            std::transform(first_layer_filaments.begin(), first_layer_filaments.end(), out_seq.begin(), [](auto item) {return item + 1; });
            return true;
        }
        for (size_t idx = other_layers_seqs.size() - 1; idx != size_t(-1); --idx) {
            const auto& other_layers_seq = other_layers_seqs[idx];
            if (layer_idx + 1 >= other_layers_seq.first.first && layer_idx + 1 <= other_layers_seq.first.second) {
                out_seq = other_layers_seq.second;
                return true;
            }
        }
        return false;
        };

    if (m_print->is_BBL_printer() || number_of_extruders == 1){
    reorder_filaments_for_minimum_flush_volume(
        filament_lists,
        filament_maps,
        layer_filaments,
        nozzle_flush_mtx,
        get_custom_seq,
        &filament_sequences
    );
    } else {
        // For non-bbl multi-extruder printers we don't support filament group yet, so we keep the layer sequence because we don't flush based on order
        filament_sequences = layer_filaments;
    }

    auto curr_flush_info = calc_filament_change_info_by_toolorder(print_config, filament_maps, nozzle_flush_mtx, filament_sequences);
    if (nozzle_nums <= 1)
        m_stats_by_single_extruder = curr_flush_info;
    else {
        m_stats_by_multi_extruder_curr = curr_flush_info;
        if (map_mode == fmmAutoForFlush)
            m_stats_by_multi_extruder_best = curr_flush_info;
    }

    // in multi extruder mode,collect data with other mode
    if (nozzle_nums > 1) {
        // always calculate the info by one extruder
        {
            std::vector<std::vector<unsigned int>>filament_sequences_one_extruder;
            auto maps_without_group = filament_maps;
            for (auto& item : maps_without_group)
                item = 0;
            reorder_filaments_for_minimum_flush_volume(
                filament_lists,
                maps_without_group,
                layer_filaments,
                nozzle_flush_mtx,
                get_custom_seq,
                &filament_sequences_one_extruder
            );
            m_stats_by_single_extruder = calc_filament_change_info_by_toolorder(print_config, maps_without_group, nozzle_flush_mtx, filament_sequences_one_extruder);
        }
        // if not in best for flush mode,also calculate the info by best for flush mode
        if (map_mode != fmmAutoForFlush)
        {
            std::vector<std::vector<unsigned int>>filament_sequences_one_extruder;
            std::vector<int>filament_maps_auto = get_recommended_filament_maps(layer_filaments, m_print, fmmAutoForFlush, physical_unprintables, geometric_unprintables);
            reorder_filaments_for_minimum_flush_volume(
                filament_lists,
                filament_maps_auto,
                layer_filaments,
                nozzle_flush_mtx,
                get_custom_seq,
                &filament_sequences_one_extruder
            );
            m_stats_by_multi_extruder_best = calc_filament_change_info_by_toolorder(print_config, filament_maps_auto, nozzle_flush_mtx, filament_sequences_one_extruder);
        }
    }

    for (size_t i = 0; i < filament_sequences.size(); ++i)
        m_layer_tools[i].extruders = std::move(filament_sequences[i]);
}
// [INTENT] Mark which layers need a skirt / draft-shield segment.  The first layer always
// gets one.  Intermediate layers (between consecutive object layers) are marked if the gap
// to the previous skirt layer exceeds max_layer_height, so the draft shield maintains
// structural continuity without leaving large unsupported jumps.
// [HAZARD] Empty layers (no extruders) are silently skipped — if the entire layer range
// is empty, the function exits without printing a skirt (with a "FIXME throw?" note left
// by the original author).
// Layers are marked for infinite skirt aka draft shield. Not all the layers have to be printed.
void ToolOrdering::mark_skirt_layers(const PrintConfig &config, coordf_t max_layer_height)
{
    if (m_layer_tools.empty())
        return;

    if (m_layer_tools.front().extruders.empty()) {
        // Empty first layer, no skirt will be printed.
        //FIXME throw an exception?
        return;
    }

    size_t i = 0;
    for (;;) {
        m_layer_tools[i].has_skirt = true;
        size_t j = i + 1;
        for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_object; ++ j);
        // i and j are two successive layers printing an object.
        if (j == m_layer_tools.size())
            // Don't print skirt above the last object layer.
            break;
        // Mark some printing intermediate layers as having skirt.
        double last_z = m_layer_tools[i].print_z;
        for (size_t k = i + 1; k < j; ++ k) {
            if (m_layer_tools[k + 1].print_z - last_z > max_layer_height + EPSILON) {
                // Layer k is the last one not violating the maximum layer height.
                // Don't extrude skirt on empty layers.
                while (m_layer_tools[k].extruders.empty())
                    -- k;
                if (m_layer_tools[k].has_skirt) {
                    // Skirt cannot be generated due to empty layers, there would be a missing layer in the skirt.
                    //FIXME throw an exception?
                    break;
                }
                m_layer_tools[k].has_skirt = true;
                last_z = m_layer_tools[k].print_z;
            }
        }
        i = j;
    }
}

// Assign a pointer to a custom G-code to the respective ToolOrdering::LayerTools.
// Ignore color changes, which are performed on a layer and for such an extruder, that the extruder will not be printing above that layer.
// If multiple events are planned over a span of a single layer, use the last one.

// [INTENT] Assigns user-defined custom G-code events (color change, pause, tool change,
// arbitrary G-code) to the correct LayerTools entry, walking layers in reverse to know
// which extruders are still printing above each point.
// [STATE] Writes LayerTools::custom_gcode pointer (non-owning, points into the static
// custom_gcode_per_print_z vector declared in this file).
// [HAZARD] custom_gcode_per_print_z is a static file-scope variable — it is overwritten
// every time assign_custom_gcodes() is called. If two plates are processed concurrently
// this would race.  Currently safe because slicing is single-threaded at the plate level.
// [HAZARD] Tool changes are ignored (filtered out by the iterator skip condition) when
// mode / model_mode mismatch (mm vs non-mm), so a file saved with multi-material tool
// changes will silently drop those events when re-opened in single-material mode.
// [COUPLING] Reads from print.model().get_curr_plate_custom_gcodes() — tightly coupled
// to the Model/Plate object hierarchy.
// BBS: replace model custom gcode with current plate custom gcode
static CustomGCode::Info custom_gcode_per_print_z;
void ToolOrdering::assign_custom_gcodes(const Print &print)
{
	// Only valid for non-sequential print.
	assert(print.config().print_sequence == PrintSequence::ByLayer);

    custom_gcode_per_print_z = print.model().get_curr_plate_custom_gcodes();
	if (custom_gcode_per_print_z.gcodes.empty())
		return;

    // BBS
	auto 						num_filaments = unsigned(print.config().filament_diameter.size());
	CustomGCode::Mode 			mode          =
		(num_filaments == 1) ? CustomGCode::SingleExtruder :
		print.object_extruders().size() == 1 ? CustomGCode::MultiAsSingle : CustomGCode::MultiExtruder;
    CustomGCode::Mode           model_mode    = print.model().get_curr_plate_custom_gcodes().mode;
	std::vector<unsigned char> 	extruder_printing_above(num_filaments, false);
	auto 						custom_gcode_it = custom_gcode_per_print_z.gcodes.rbegin();
	// Tool changes and color changes will be ignored, if the model's tool/color changes were entered in mm mode and the print is in non mm mode
	// or vice versa.
	bool 						ignore_tool_and_color_changes = (mode == CustomGCode::MultiExtruder) != (model_mode == CustomGCode::MultiExtruder);
	// If printing on a single extruder machine, make the tool changes trigger color change (M600) events.
	bool 						tool_changes_as_color_changes = mode == CustomGCode::SingleExtruder && model_mode == CustomGCode::MultiAsSingle;

	// From the last layer to the first one:
    coordf_t print_z_above = std::numeric_limits<coordf_t>::lowest();
	for (auto it_lt = m_layer_tools.rbegin(); it_lt != m_layer_tools.rend(); ++ it_lt) {
		LayerTools &lt = *it_lt;
		// Add the extruders of the current layer to the set of extruders printing at and above this print_z.
		for (unsigned int i : lt.extruders)
			extruder_printing_above[i] = true;
		// Skip all custom G-codes above this layer and skip all extruder switches.
		for (; custom_gcode_it != custom_gcode_per_print_z.gcodes.rend() && (
            (print_z_above > lt.print_z && custom_gcode_it->print_z > 0.5 * (lt.print_z + print_z_above))
            || custom_gcode_it->type == CustomGCode::ToolChange); ++ custom_gcode_it);
        print_z_above = lt.print_z;
		if (custom_gcode_it == custom_gcode_per_print_z.gcodes.rend())
			// Custom G-codes were processed.
			break;
		// Some custom G-code is configured for this layer or a layer below.
		const CustomGCode::Item &custom_gcode = *custom_gcode_it;
		// print_z of the layer below the current layer.
		coordf_t print_z_below = 0.;
		if (auto it_lt_below = it_lt; ++ it_lt_below != m_layer_tools.rend())
			print_z_below = it_lt_below->print_z;
        if (custom_gcode.print_z > 0.5 * (print_z_below + lt.print_z)) {
			// The custom G-code applies to the current layer.
			bool color_change = custom_gcode.type == CustomGCode::ColorChange;
			bool tool_change  = custom_gcode.type == CustomGCode::ToolChange;
			bool pause_or_custom_gcode = ! color_change && ! tool_change;
			bool apply_color_change = ! ignore_tool_and_color_changes &&
				// If it is color change, it will actually be useful as the exturder above will print.
                // BBS
				(color_change ? 
					mode == CustomGCode::SingleExtruder || 
						(custom_gcode.extruder <= int(num_filaments) && extruder_printing_above[unsigned(custom_gcode.extruder - 1)]) :
				 	tool_change && tool_changes_as_color_changes);
			if (pause_or_custom_gcode || apply_color_change)
        		lt.custom_gcode = &custom_gcode;
			// Consume that custom G-code event.
			++ custom_gcode_it;
		}
	}
}

const LayerTools& ToolOrdering::tools_for_layer(coordf_t print_z) const
{
    auto it_layer_tools = std::lower_bound(m_layer_tools.begin(), m_layer_tools.end(), LayerTools(print_z - EPSILON));
    assert(it_layer_tools != m_layer_tools.end());
    coordf_t dist_min = std::abs(it_layer_tools->print_z - print_z);
    for (++ it_layer_tools; it_layer_tools != m_layer_tools.end(); ++ it_layer_tools) {
        coordf_t d = std::abs(it_layer_tools->print_z - print_z);
        if (d >= dist_min)
            break;
        dist_min = d;
    }
    -- it_layer_tools;
    assert(dist_min < EPSILON);
    return *it_layer_tools;
}

// This function is called from Print::mark_wiping_extrusions and sets extruder this entity should be printed with (-1 .. as usual)
void WipingExtrusions::set_extruder_override(const ExtrusionEntity* entity, const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;

    auto entity_map_it = (entity_map.emplace(std::make_tuple(entity, object), ExtruderPerCopy())).first; // (add and) return iterator
    ExtruderPerCopy& copies_vector = entity_map_it->second;
    copies_vector.resize(num_of_copies, -1);

    if (copies_vector[copy_id] != -1)
        std::cout << "ERROR: Entity extruder overriden multiple times!!!\n";    // A debugging message - this must never happen.

    copies_vector[copy_id] = extruder;
}

// BBS
void WipingExtrusions::set_support_extruder_override(const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;
    support_map.emplace(object, extruder);
}

void WipingExtrusions::set_support_interface_extruder_override(const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;
    support_intf_map.emplace(object, extruder);
}

// Finds first non-soluble extruder on the layer
int WipingExtrusions::first_nonsoluble_extruder_on_layer(const PrintConfig& print_config) const
{
    const LayerTools& lt = *m_layer_tools;
    for (auto extruders_it = lt.extruders.begin(); extruders_it != lt.extruders.end(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it) && !print_config.filament_is_support.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Finds last non-soluble extruder on the layer
int WipingExtrusions::last_nonsoluble_extruder_on_layer(const PrintConfig& print_config) const
{
    const LayerTools& lt = *m_layer_tools;
    for (auto extruders_it = lt.extruders.rbegin(); extruders_it != lt.extruders.rend(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it) && !print_config.filament_is_support.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Decides whether this entity could be overridden
bool WipingExtrusions::is_overriddable(const ExtrusionEntityCollection& eec, const PrintConfig& print_config, const PrintObject& object, const PrintRegion& region) const
{
    if (print_config.filament_soluble.get_at(m_layer_tools->extruder(eec, region)))
        return false;

    if (object.config().flush_into_objects)
        return true;

    if (!object.config().flush_into_infill || eec.role() != erInternalInfill)
        return false;

    return true;
}

// BBS
bool WipingExtrusions::is_support_overriddable(const ExtrusionRole role, const PrintObject& object) const
{
    if (!object.config().flush_into_support)
        return false;

    if (role == erMixed) {
        return object.config().support_filament == 0 || object.config().support_interface_filament == 0;
    }
    else if (role == erSupportMaterial || role == erSupportTransition) {
        return object.config().support_filament == 0;
    }
    else if (role == erSupportMaterialInterface) {
        return object.config().support_interface_filament == 0;
    }

    return false;
}

// [INTENT] Mark extrusion entities (infill / perimeter / support) that should be printed
// using the new_extruder immediately after a tool change, consuming purge volume in the
// process.  This avoids depositing waste plastic in the wipe tower by instead routing the
// initial "dirty" extrusion of the new filament into regions that will be hidden inside
// the print (infill) or are accepted as discoloured (flush_into_objects).
// [STATE] Mutates entity_map, support_map, support_intf_map via set_extruder_override().
// Writes something_overridden = true as soon as any override is registered.
// [COUPLING] Reads lt.print_z to find matching Layer/SupportLayer in each PrintObject.
// Relies on object print order for spatial proximity — objects with flush_into_objects=true
// are sorted first so neighbouring infills minimise travel.
// [HAZARD] The loop variable `i` is reset to -1 inside the loop body (perimeters_done
// flip). This is intentional but hard to follow; refactoring to a state machine would
// reduce confusion.
// [HAZARD] volume_to_wipe can reach exactly 0.f inside the inner loop, causing an early
// return 0.f.  Floating-point accumulation means the caller sees "0 remaining" but
// actual purge may be slightly under.
// [HAZARD] Soluble filaments and support filaments are excluded from wiping (return
// volume_to_wipe unchanged), so prints using soluble supports still fully consume the
// wipe tower for those transitions.
// Following function iterates through all extrusions on the layer, remembers those that could be used for wiping after toolchange
// and returns volume that is left to be wiped on the wipe tower.
float WipingExtrusions::mark_wiping_extrusions(const Print& print, unsigned int old_extruder, unsigned int new_extruder, float volume_to_wipe)
{
    const LayerTools& lt = *m_layer_tools;
    const float min_infill_volume = 0.f; // ignore infill with smaller volume than this

    if (! this->something_overridable || volume_to_wipe <= 0. || print.config().filament_soluble.get_at(old_extruder) || print.config().filament_soluble.get_at(new_extruder))
        return std::max(0.f, volume_to_wipe); // Soluble filament cannot be wiped in a random infill, neither the filament after it

    // BBS
    if (print.config().filament_is_support.get_at(old_extruder) || print.config().filament_is_support.get_at(new_extruder))
        return std::max(0.f, volume_to_wipe); // Support filament cannot be used to print support, infill, wipe_tower, etc.

    // we will sort objects so that dedicated for wiping are at the beginning:
    ConstPrintObjectPtrs object_list = print.objects().vector();
    // BBS: fix the exception caused by not fixed order between different objects
    std::sort(object_list.begin(), object_list.end(), [object_list](const PrintObject* a, const PrintObject* b) {
        if (a->config().flush_into_objects != b->config().flush_into_objects) {
            return a->config().flush_into_objects.getBool();
        }
        else {
            return a->id() < b->id();
        }
    });

    // We will now iterate through
    //  - first the dedicated objects to mark perimeters or infills (depending on infill_first)
    //  - second through the dedicated ones again to mark infills or perimeters (depending on infill_first)
    //  - then all the others to mark infills (in case that !infill_first, we must also check that the perimeter is finished already
    // this is controlled by the following variable:
    bool perimeters_done = false;

    for (int i=0 ; i<(int)object_list.size() + (perimeters_done ? 0 : 1); ++i) {
        if (!perimeters_done && (i==(int)object_list.size() || !object_list[i]->config().flush_into_objects)) { // we passed the last dedicated object in list
            perimeters_done = true;
            i=-1;   // let's go from the start again
            continue;
        }

        const PrintObject* object = object_list[i];

        // Finds this layer:
        const Layer* this_layer = object->get_layer_at_printz(lt.print_z, EPSILON);
        if (this_layer == nullptr)
        	continue;

        size_t num_of_copies = object->instances().size();

        // iterate through copies (aka PrintObject instances) first, so that we mark neighbouring infills to minimize travel moves
        for (unsigned int copy = 0; copy < num_of_copies; ++copy) {
            for (const LayerRegion *layerm : this_layer->regions()) {
                const auto &region = layerm->region();

                if (!object->config().flush_into_infill && !object->config().flush_into_objects && !object->config().flush_into_support)
                    continue;
                bool wipe_into_infill_only = !object->config().flush_into_objects && object->config().flush_into_infill;
                bool is_infill_first = region.config().is_infill_first;
                if (is_infill_first != perimeters_done || wipe_into_infill_only) {
                    for (const ExtrusionEntity* ee : layerm->fills.entities) {                      // iterate through all infill Collections
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                        if (!is_overriddable(*fill, print.config(), *object, region))
                            continue;

                        if (wipe_into_infill_only && ! is_infill_first)
                            // In this case we must check that the original extruder is used on this layer before the one we are overridding
                            // (and the perimeters will be finished before the infill is printed):
                            if (!lt.is_extruder_order(lt.wall_filament(region), new_extruder))
                                continue;

                        if ((!is_entity_overridden(fill, object, copy) && fill->total_volume() > min_infill_volume))
                        {     // this infill will be used to wipe this extruder
                            set_extruder_override(fill, object, copy, new_extruder, num_of_copies);
                            if ((volume_to_wipe -= float(fill->total_volume())) <= 0.f)
                            	// More material was purged already than asked for.
	                            return 0.f;
                        }
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                if (object->config().flush_into_objects && is_infill_first == perimeters_done)
                {
                    for (const ExtrusionEntity* ee : layerm->perimeters.entities) {
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                        if (is_overriddable(*fill, print.config(), *object, region) && !is_entity_overridden(fill, object, copy) && fill->total_volume() > min_infill_volume) {
                            set_extruder_override(fill, object, copy, new_extruder, num_of_copies);
                            if ((volume_to_wipe -= float(fill->total_volume())) <= 0.f)
                            	// More material was purged already than asked for.
	                            return 0.f;
                        }
                    }
                }
            }

            // BBS
            if (object->config().flush_into_support) {
                auto& object_config = object->config();
                const SupportLayer* this_support_layer = object->get_support_layer_at_printz(lt.print_z, EPSILON);

                do {
                    if (this_support_layer == nullptr)
                        break;

                    bool support_overriddable = object_config.support_filament == 0;
                    bool support_intf_overriddable = object_config.support_interface_filament == 0;
                    if (!support_overriddable && !support_intf_overriddable)
                        break;

                    auto &entities = this_support_layer->support_fills.entities;
                    if (support_overriddable && !is_support_overridden(object) && !(object_config.support_interface_not_for_body.value && !support_intf_overriddable &&(new_extruder==object_config.support_interface_filament-1||old_extruder==object_config.support_interface_filament-1))) {
                        set_support_extruder_override(object, copy, new_extruder, num_of_copies);
                        for (const ExtrusionEntity* ee : entities) {
                            if (ee->role() == erSupportMaterial || ee->role() == erSupportTransition)
                                volume_to_wipe -= ee->total_volume();

                            if (volume_to_wipe <= 0.f)
                                return 0.f;
                        }
                    }

                    if (support_intf_overriddable && !is_support_interface_overridden(object)) {
                        set_support_interface_extruder_override(object, copy, new_extruder, num_of_copies);
                        for (const ExtrusionEntity* ee : entities) {
                            if (ee->role() == erSupportMaterialInterface)
                                volume_to_wipe -= ee->total_volume();

                            if (volume_to_wipe <= 0.f)
                                return 0.f;
                        }
                    }
                } while (0);
            }
        }
    }
	// Some purge remains to be done on the Wipe Tower.
    assert(volume_to_wipe > 0.);
    return volume_to_wipe;
}



// [INTENT] Post-processing pass after all toolchanges on a layer have been processed.
// Ensures that any overriddable entity that was *not* overridden by mark_wiping_extrusions
// is force-assigned to an extruder so that infill/perimeter print order invariants are
// maintained.  Without this, an entity might be printed with its original extruder at the
// wrong time (e.g. infill before perimeter when infill_first=false).
// [HAZARD] The force-override may violate infill_first ordering in edge cases (noted as
// FIXME in the code). This is a known limitation left from PrusaSlicer.
// Called after all toolchanges on a layer were mark_infill_overridden. There might still be overridable entities,
// that were not actually overridden. If they are part of a dedicated object, printing them with the extruder
// they were initially assigned to might mean violating the perimeter-infill order. We will therefore go through
// them again and make sure we override it.
void WipingExtrusions::ensure_perimeters_infills_order(const Print& print)
{
	if (! this->something_overridable)
		return;

    const LayerTools& lt = *m_layer_tools;
    unsigned int first_nonsoluble_extruder = first_nonsoluble_extruder_on_layer(print.config());
    unsigned int last_nonsoluble_extruder = last_nonsoluble_extruder_on_layer(print.config());

    for (const PrintObject* object : print.objects()) {
        // Finds this layer:
        const Layer* this_layer = object->get_layer_at_printz(lt.print_z, EPSILON);
        if (this_layer == nullptr)
        	continue;
        size_t num_of_copies = object->instances().size();

        for (size_t copy = 0; copy < num_of_copies; ++copy) {    // iterate through copies first, so that we mark neighbouring infills to minimize travel moves
            for (const LayerRegion *layerm : this_layer->regions()) {
                const auto &region = layerm->region();
                //BBS
                if (!object->config().flush_into_infill && !object->config().flush_into_objects)
                    continue;

                bool is_infill_first = region.config().is_infill_first;
                for (const ExtrusionEntity* ee : layerm->fills.entities) {                      // iterate through all infill Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                    if (!is_overriddable(*fill, print.config(), *object, region)
                     || is_entity_overridden(fill, object, copy) )
                        continue;

                    // This infill could have been overridden but was not - unless we do something, it could be
                    // printed before its perimeter, or not be printed at all (in case its original extruder has
                    // not been added to LayerTools
                    // Either way, we will now force-override it with something suitable:
                    //BBS
                    if (is_infill_first
                    //BBS
                    //|| object->config().flush_into_objects  // in this case the perimeter is overridden, so we can override by the last one safely
                    || lt.is_extruder_order(lt.wall_filament(region), last_nonsoluble_extruder    // !infill_first, but perimeter is already printed when last extruder prints
                    || ! lt.has_extruder(lt.sparse_infill_filament(region)))) // we have to force override - this could violate infill_first (FIXME)
                        set_extruder_override(fill, object, copy, (is_infill_first ? first_nonsoluble_extruder : last_nonsoluble_extruder), num_of_copies);
                    else {
                        // In this case we can (and should) leave it to be printed normally.
                        // Force overriding would mean it gets printed before its perimeter.
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                for (const ExtrusionEntity* ee : layerm->perimeters.entities) {                      // iterate through all perimeter Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                    if (is_overriddable(*fill, print.config(), *object, region) && ! is_entity_overridden(fill, object, copy))
                        set_extruder_override(fill, object, copy, (is_infill_first ? last_nonsoluble_extruder : first_nonsoluble_extruder), num_of_copies);
                }
            }
        }
    }
}

// [INTENT] Called from GCode::process_layer to retrieve the per-copy extruder override
// list for a specific extrusion entity. Returns nullptr if the entity has no override.
// When overrides exist, replaces all -1 sentinel values (meaning "print as usual") with
// the encoding -(correct_extruder_id+1) so the caller can distinguish overridden copies
// (non-negative) from regular copies (negative, encoded).
// [STATE] Mutates the override vector in entity_map in place — calling this function twice
// on the same entity changes the -1 sentinels a second time (double-encoding bug if called
// after correct_extruder_id changes).
// [HAZARD] The negative encoding -(n+1) means extruder 0 is stored as -1, which collides
// with the sentinel.  The replace() call resolves this by replacing all -1 first, so the
// sentinel is consumed before correct_extruder_id=-1 could be written. Nevertheless this
// encoding is fragile; any future change to the sentinel value must update all decode sites.
// Following function is called from GCode::process_layer and returns pointer to vector with information about which extruders should be used for given copy of this entity.
// If this extrusion does not have any override, nullptr is returned.
// Otherwise it modifies the vector in place and changes all -1 to correct_extruder_id (at the time the overrides were created, correct extruders were not known,
// so -1 was used as "print as usual").
// The resulting vector therefore keeps track of which extrusions are the ones that were overridden and which were not. If the extruder used is overridden,
// its number is saved as is (zero-based index). Regular extrusions are saved as -number-1 (unfortunately there is no negative zero).
const WipingExtrusions::ExtruderPerCopy* WipingExtrusions::get_extruder_overrides(const ExtrusionEntity* entity, const PrintObject* object, int correct_extruder_id, size_t num_of_copies)
{
	ExtruderPerCopy *overrides = nullptr;
    auto entity_map_it = entity_map.find(std::make_tuple(entity, object));
    if (entity_map_it != entity_map.end()) {
        overrides = &entity_map_it->second;
    	overrides->resize(num_of_copies, -1);
	    // Each -1 now means "print as usual" - we will replace it with actual extruder id (shifted it so we don't lose that information):
	    std::replace(overrides->begin(), overrides->end(), -1, -correct_extruder_id-1);
	}
    return overrides;
}

// BBS
int WipingExtrusions::get_support_extruder_overrides(const PrintObject* object)
{
    auto iter = support_map.find(object);
    if (iter != support_map.end())
        return iter->second;

    return -1;
}

int WipingExtrusions::get_support_interface_extruder_overrides(const PrintObject* object)
{
    auto iter = support_intf_map.find(object);
    if (iter != support_intf_map.end())
        return iter->second;

    return -1;
}


} // namespace Slic3r
