// [ANNOTATED]
// [INTENT] Public interface for the procedural filament icon generation service.
// [STATE] Enumerates supported rendering modes for material swatches.
// [UNITY] Map to a static C# FilamentIconGenerator class. Return Texture2D objects instead of bitmaps.
// [PORTING_HAZARD:P3] Interface uses wxWidgets types (wxBitmap, wxSize) which must be replaced by Unity equivalents.

#ifndef slic3r_GUI_FilamentBitmapUtils_hpp_
#define slic3r_GUI_FilamentBitmapUtils_hpp_

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <vector>

namespace Slic3r { namespace GUI {

enum class FilamentRenderMode { Single, Dual, Triple, Quadruple, Gradient };

// Create a colour swatch bitmap. The render mode is chosen automatically from the
// number of colours unless force_gradient is true.
wxBitmap create_filament_bitmap(const std::vector<wxColour>& colors, const wxSize& size, bool force_gradient = false);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_FilamentBitmapUtils_hpp_