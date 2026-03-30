// [ANNOTATED]
#ifndef slic3r_MacDarkMode_hpp_
#define slic3r_MacDarkMode_hpp_

#include <wx/event.h>

namespace Slic3r { namespace GUI {

// [INTENT] Platform-specific shims for macOS native GUI behaviors. It bridges wxWidgets
// to macOS-specific features like Dark Mode state, WKWebView script execution,
// window miniaturization, and title bar coloring.
//
// [STATE] These functions primarily query or mutate underlying macOS native
// window/view state (NSWindow, NSView) via void pointers.
//
// [UNITY] Unity's cross-platform abstraction layer (PlayerSettings, SystemInfo)
// and the UI Toolkit handle most of these behaviors natively.
// - `mac_dark_mode`: Replaced by `SystemInfo` or custom theme providers.
// - `WKWebView`: Replaced by Unity's `WebView` package or third-party browser plugins.
// - `initGestures`: Replaced by Unity's Input System.
//
// [PORTING_HAZARD:P3] Platform-specific "hacks" (like `set_title_colour_after_set_title`)
// are workarounds for wxWidgets/macOS theme sync issues and should be replaced
// by standard Unity styling in a port.
//
#if __APPLE__
extern bool   mac_dark_mode();
extern double mac_max_scaling_factor();
extern void   set_miniaturizable(void* window);
void          WKWebView_evaluateJavaScript(void* web, wxString const& script, void (*callback)(wxString const&));
void          WKWebView_setTransparentBackground(void* web);
void          set_tag_when_enter_full_screen(bool isfullscreen);
void          set_title_colour_after_set_title(void* window);
void          initGestures(void* view, wxEvtHandler* handler);
void          openFolderForFile(wxString const& file);
void          StaticGroup_layoutBadge(void* group, void* badge);
#endif

}} // namespace Slic3r::GUI

#endif // MacDarkMode_h
