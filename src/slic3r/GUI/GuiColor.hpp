#ifndef slic3r_GUI_Color_hpp_
#define slic3r_GUI_Color_hpp_
#include <wx/colour.h>
#include "libslic3r/Color.hpp"
#include "slic3r/Utils/ColorSpaceConvert.hpp"

// [INTENT][STATE] Keeps the palette entry id and the last computed distance from the currently inspected color so UI matching dialogs can
// rank candidates. [PORTING_HAZARD:P3] Built assuming wxColour uses 0-255 components and linear distance; Unity must align its gamma space
// or reapply ColorSpaceConvert helpers.
struct ColorDistValue
{
    int   id;
    float distance;
};

namespace Slic3r { namespace GUI {
// [INTENT][THREAD] Convert the latest RGBA theme or user-selected color into a wxWidgets color for repaint steps running on the UI thread
// during selection events. [UNITY] Mirror with a ScriptableObject-backed palette and runtime GraphicRaycaster controller that keeps Unity
// `Color` conversions in sync.
wxColour convert_to_wxColour(const RGBA& color);

// [INTENT][THREAD] Mirror wx rendering state back into renderer-neutral RGBA so backend timing logic can persist colors without wxWidgets
// types. [UNITY] Map to `ColorUtility.ToHtmlStringRGBA` plus a shared settings `Color` ScriptableObject.
RGBA convert_to_rgba(const wxColour& color);

// [INTENT][EVENT] Provide a symmetric distance metric triggered while reacting to hover/keyboard events so palette entries can be ranked.
// [STATE] Used when evaluating a target color versus palette entries; the closest match drives UI highlights.
// [OPENGL] Invoked before redraw to re-triangulate palette chips with the highlight shader.
// [PORTING_HAZARD:P2] Depends on legacy wxColour gamma, so Unity `Color` distances must run in the same space or results shift noticeably.
float calc_color_distance(wxColour c1, wxColour c2);

// [INTENT][EVENT] Reveal the same distance calculation to codepaths that already consume raw RGBA values (serialization inspectors,
// presets, etc.). [THREAD] Safe to call from worker threads since it operates purely on value types.
float calc_color_distance(RGBA c1, RGBA c2);
} // namespace GUI
} // namespace Slic3r

#endif /* slic3r_GUI_Color_hpp_ */
