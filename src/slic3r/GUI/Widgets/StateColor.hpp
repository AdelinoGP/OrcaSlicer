#ifndef slic3r_GUI_StateColor_hpp_
#define slic3r_GUI_StateColor_hpp_

#include <wx/colour.h>

#include <map>

class StateColor
{
    // [INTENT] Centralize the shared palettes that widgets use for hover/focus/pressed/disabled color variations so renderers stay
    // consistent. [UNITY] Model this as a ScriptableObject or UI Toolkit style asset with a Dictionary<StateMask, Color> consumed by UI
    // callbacks. [PORTING_HAZARD:P3] wxColour-dependent LAB math and tuple returns rely on precise color-space conversions that Unity must
    // mirror. [THREAD] Designed for the GUI thread only; internal vectors mutate without synchronization and should not be touched from
    // worker threads.
public:
    // [STATE] Low bits encode affirmative widget state (hover, pressed, focus) while high bits encode explicit `Not*` states used by the matcher.
    enum State {
        Normal     = 0,
        Enabled    = 1,
        Checked    = 2,
        Focused    = 4,
        Hovered    = 8,
        Pressed    = 16,
        Disabled   = 1 << 16,
        NotChecked = 2 << 16,
        NotFocused = 4 << 16,
        NotHovered = 8 << 16,
        NotPressed = 16 << 16,
    };

public:
    // [INTENT] Provide LAB/lightness math so dark/light adjustments hold perceptual consistency when coloring widgets.
    // [UNITY] Unity can achieve the same by converting to linear Color via ColorUtility and using Mathf.Max/Color.Lerp to adjust brightness.
    static std::tuple<double, double, double> GetLAB(const wxColour& color);
    static double                             GetLightness(const wxColour& color);
    static wxColour                           SetLightness(const wxColour& color, double lightness);
    static wxColour                           LightenDarkenColor(const wxColour& color, int amount);
    static double                             GetColorDifference(const wxColour& c1, const wxColour& c2);
    static double                             LAB_Delta_E(const wxColour& c1, const wxColour& c2);

    // [STATE] Global flag that switches the helper map between light and dark palettes; affects future color lookups only.
    // [PORTING_HAZARD:P2] Porting needs a central setting (ScriptableObject bool) so all palettes flip atomically, matching wxWidgets globals.
    static void SetDarkMode(bool dark);

    // [STATE] Cache of computed dark-mode translations.
    // [UNITY] Mirror this with a Dictionary<Color, Color> inside a serialized theme ScriptableObject so the palette flips when dark mode toggles.
    static std::map<wxColour, wxColour> const& GetDarkMap();
    static wxColour                            darkModeColorFor(wxColour const& color);
    static wxColour                            lightModeColorFor(wxColour const& color);

public:
    // [INTENT] Constructors accept one or more (color, state mask) pairs so callers can declaratively define color per state.
    // [UNITY] In Unity this maps to a helper that registers (Color, SelectableState) tuples inside a serialized object or runtime dictionary.
    template<typename... Colors> StateColor(std::pair<Colors, int>... colors) { fill(colors...); }

    // single color
    StateColor(wxColour const& color);

    // single color
    StateColor(wxString const& color);

    // single color
    StateColor(unsigned long color);

    // operator==
    bool operator==(StateColor const& other) const
    {
        return statesList_ == other.statesList_ && colors_ == other.colors_ && takeFocusedAsHovered_ == other.takeFocusedAsHovered_;
    };

    // operator!=
    bool operator!=(StateColor const& other) const { return !(*this == other); };

public:
    // [STATE] Populate the ordered palette so the matcher can iterate states/colors in a deterministic order during paint.
    // [UNITY] This corresponds to filling a List<StateColorEntry> on a MonoBehaviour that binds to Selectable transitions.
    void append(wxColour const& color, int states);

    void append(wxString const& color, int states);

    void append(unsigned long color, int states);

    void clear();

public:
    int count() const { return statesList_.size(); }

    int states() const;

public:
    // [STATE] Queries that resolve the most appropriate color based on the provided state mask; defaults to index 0 when nothing matches.
    // [PORTING_HAZARD:P3] The fallback order assumes the high-bit `Not*` flags exist; Unity needs the same mask semantics or the lookup
    // will diverge. [UNITY] Equivalent to a ColorPalette service that maps SelectableState combinations to Colors via a serialized list.
    wxColour defaultColor();

    wxColour colorForStates(int states);

    wxColour colorForStatesNoDark(int states);

    int colorIndexForStates(int states);

    // [THREAD] Mutations happen only on the UI thread while constructing custom palettes for controls.
    bool setColorForStates(wxColour const& color, int states);

    void setTakeFocusedAsHovered(bool set);

private:
    template<typename Color, typename... Colors> void fill(std::pair<Color, int> color, std::pair<Colors, int>... colors)
    {
        fillOne(color);
        fill(colors...);
    }

    template<typename Color> void fillOne(std::pair<Color, int> color) { append(color.first, color.second); }

    void fill() {}

private:
    // [STATE] Parallel arrays couple each mask with its color so `colorForStates` can scan the palette in index order.
    std::vector<int>      statesList_;
    std::vector<wxColour> colors_;
    // [STATE] Widget helpers can flip this to treat focus as hover when the same highlight is desired for both cues.
    bool takeFocusedAsHovered_ = true;
};

#endif // !slic3r_GUI_StateColor_hpp_
