#ifndef slic3r_GUI_TickCode_hpp_
#define slic3r_GUI_TickCode_hpp_

#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Color.hpp"
#include <set>

namespace Slic3r {
using namespace CustomGCode;
namespace GUI {

// [INTENT] Lightweight value object for one tick-level custom G-code marker.
// [STATE] The strict ordering by tick lets std::set hold a unique, timeline-sorted marker list.
// [UNITY] Model this as a serializable marker struct inside a retained annotation/service layer, not as a view object.
struct TickCode
{
    bool operator<(const TickCode& other) const { return other.tick > this->tick; }
    bool operator>(const TickCode& other) const { return other.tick < this->tick; }

    int         tick     = 0;
    Type        type     = ColorChange;
    int         extruder = 0;
    std::string color;
    std::string extra;
};

// [INTENT] Tracks the editable collection of tick markers plus the UI toggles that affect how markers are generated and colored.
// [STATE] Owns the ordered tick set, pause-message text, suppression flags, and the optional extruder-color palette pointer.
// [PORTING_HAZARD:P2] The class mixes marker-model mutations with palette resolution and UI suppression rules, so a Unity port should split
// editing from color lookup. [UNITY] Use a ScriptableObject-backed marker model with a separate color-resolution service and a main-thread
// edit controller.
class TickCodeInfo
{
    std::string pause_print_msg;
    bool        m_suppress_plus      = false;
    bool        m_suppress_minus     = false;
    bool        m_use_default_colors = false;

    // [STATE] Non-owning palette pointer; lifetime is external, likely tied to the slider or other extrusion-color source.
    // [PORTING_HAZARD:P3] This aliasing assumption is easy to break when the palette becomes a Unity-managed list.
    std::vector<std::string>* m_colors{nullptr}; // reference to IMSlider::m_extruder_colors

    std::string get_color_for_tick(TickCode tick, Type type, const int extruder);

public:
    std::set<TickCode> ticks{};
    Mode               mode = Undef;

    bool empty() const { return ticks.empty(); }
    void set_pause_print_msg(const std::string& message) { pause_print_msg = message; }

    // [INTENT] Mutate the marker set in response to UI edits and G-code mode changes.
    // [UNITY] These operations should become controller/service methods that emit model-change notifications to the view layer.
    bool add_tick(const int tick, Type type, int extruder, double print_z);
    bool edit_tick(std::set<TickCode>::iterator it, double print_z);
    void switch_code(Type type_from, Type type_to);
    bool switch_code_for_tick(std::set<TickCode>::iterator it, Type type_to, const int extruder);
    void erase_all_ticks_with_code(Type type);

    bool has_tick_with_code(Type type);
    bool has_tick(int tick);

    void suppress_plus(bool suppress) { m_suppress_plus = suppress; }
    void suppress_minus(bool suppress) { m_suppress_minus = suppress; }
    bool suppressed_plus() { return m_suppress_plus; }
    bool suppressed_minus() { return m_suppress_minus; }
    void set_default_colors(bool default_colors_on) { m_use_default_colors = default_colors_on; }

    void set_extruder_colors(std::vector<std::string>* extruder_colors) { m_colors = extruder_colors; }
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_TickCode_hpp_
