#include "libslic3r/libslic3r.h"
#include "GUI_Geometry.hpp"

// [INTENT] Provides a translation-unit anchor for the header-only geometry helpers so the linker sees a GUI-related object file even though
// all logic lives inline in `GUI_Geometry.hpp`. [UNITY] Equivalent Unity module would live entirely in a C# static class or assembly
// module; no separate source file is required since C# compiles everything together. [PORTING_HAZARD:P3] Ensure the Unity port still forces
// the GUI geometry helpers to compile even if they remain header-only, otherwise the linker could drop unused code just like this empty TU
// has no direct symbols.
namespace Slic3r { namespace GUI {

}} // namespace Slic3r::GUI
