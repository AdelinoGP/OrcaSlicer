#include "libslic3r/libslic3r.h"
#include "GUI_Geometry.hpp"

// [INTENT] Provides a translation-unit anchor for the header-only geometry helpers so the linker sees a GUI-related object file even though
// all logic lives inline in `GUI_Geometry.hpp`. [STATE] There is no runtime state held here; every helper stores its own cache wherever it
// is instantiated, so this TU simply forces the inline definitions to compile into the GUI binary. [EVENT] No event wiring occurs in this
// file because the actual callbacks live next to the inline helpers, and nothing here subscribes to wx events. [OPENGL] Geometry updates
// invoked from the GUI code feed the GL viewport pipeline, even though this TU contains no GL calls itself. [THREAD] The inline helpers
// that end up here run on the UI thread alongside the rest of the GUI, so Unity's equivalent should execute inside the main thread's
// geometry preprocessing step. [UNITY] Equivalent Unity module would live entirely in a C# static class or assembly module; no separate
// source file is required since C# compiles everything together. [PORTING_HAZARD:P3] Ensure the Unity port still forces the GUI geometry
// helpers to compile even if they remain header-only, otherwise the linker could drop unused code just like this empty TU has no direct
// symbols.
namespace Slic3r { namespace GUI {

}} // namespace Slic3r::GUI
