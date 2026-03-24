#ifndef _
#define _(s) Slic3r::GUI::I18N::translate((s))
#define _L(s) Slic3r::GUI::I18N::translate((s))
#define _devL(s) wxString((s))
#define _omitL(s) ("")
#define _utf8(s) Slic3r::GUI::I18N::translate_utf8((s))
#define _u8L(s) Slic3r::GUI::I18N::translate_utf8((s))
#endif /* _ */

#ifndef _CTX
#define _CTX(s, ctx) Slic3r::GUI::I18N::translate((s), (ctx))
#define _CTX_utf8(s, ctx) Slic3r::GUI::I18N::translate_utf8((s), (ctx))
#endif /* _ */

#ifndef L
// !!! If you needed to translate some wxString,
// !!! please use _L(string)
// !!! _() - is a standard wxWidgets macro to translate
// !!! L() is used only for marking localizable string
// !!! It will be used in "xgettext" to create a Locating Message Catalog.
#define L(s) s
#endif /* L */

#ifndef L_CONTEXT
#define L_CONTEXT(s, context) s
#endif /* L */

#ifndef _CHB
//! macro used to localization, return wxScopedCharBuffer
//! With wxConvUTF8 explicitly specify that the source string is already in UTF-8 encoding
#define _CHB(s) wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str()
#endif /* _CHB */

// [THREAD][STATE][EVENT][PORTING_HAZARD:P2] These macros and helpers reference the global `wxLocale` catalog that lives on the main UI
// thread; locale-change `EVT_MENU`/`EVT_UPDATE_UI` handlers rebuild these strings, so Unity must marshal locale swaps through a
// `LocalizationSettings` controller and avoid touching the `CultureInfo` cache from background workers.

// [INTENT] Keep `_`/`_L`/`_CTX` macros routed through the same I18N helpers so every call-site hits the catalog-aware translation pipeline.
// [UNITY] Replace these with `LocalizedString` instances backed by a ScriptableObject-based `StringTable` cache and pulled through
// `LocalizationSettings.StringDatabase`. [PORTING_HAZARD:P2] wxWidgets uses `xgettext` plus `wxLocale` and `.po` catalogs; Unity migration
// will need a manual pipeline to extract strings and switch locales without `wxLocale`.

#ifndef slic3r_GUI_I18N_hpp_
#define slic3r_GUI_I18N_hpp_

#include <wx/intl.h>
#include <wx/version.h>

namespace Slic3r { namespace GUI {

// [INTENT] Centralize `wxGetTranslation` overloads so GUI code never duplicates encoding, plural, or context handling.
// [STATE] All helpers read from the global `wxLocale` catalog bound to the application, so changing the locale updates every translation
// automatically. [UNITY] In Unity this corresponds to a `LocalizationSettings.StringDatabase` helper that caches `StringTable` entries
// inside a ScriptableObject. [PORTING_HAZARD:P2] Porting must recreate the catalog tooling: extracting strings in edit-time, storing them
// in Unity assets, and controlling locale switching outside of `wxLocale`.
namespace I18N {
// [EVENT] UI panels re-run these helpers inside `EVT_MENU`, `EVT_UPDATE_UI`, and `wxCommandEvent` paint paths, so the wrappers must stay
// stateless and safe for repeated translation calls triggered by menu/toolbar redraws.
// [INTENT] Ensure `_()` overloads route through `wxGetTranslation` with consistent UTF-8 handling so every string literal gets localized
// regardless of encoding. [STATE] Each call reflects the global `wxLocale` catalog, making locale switches transparent to UI components.
// [UNITY] Implement this via `LocalizedString` lookups against `StringTable` assets plus a `LocalizationController` MonoBehaviour that
// caches the current `CultureInfo`.
inline wxString translate(const char* s) { return wxGetTranslation(wxString(s, wxConvUTF8)); }
inline wxString translate(const wchar_t* s) { return wxGetTranslation(s); }
inline wxString translate(const std::string& s) { return wxGetTranslation(wxString(s.c_str(), wxConvUTF8)); }
inline wxString translate(const std::wstring& s) { return wxGetTranslation(s.c_str()); }
inline wxString translate(const wxString& s) { return wxGetTranslation(s); }

// [INTENT] Plural-aware wrappers forward singular+plural variants and the count so translators can control grammatical forms inside the
// catalog. [PORTING_HAZARD:P2] Unity has no built-in plural support, so you must either pre-generate separate string entries per plurality
// or implement a localization helper that applies grammar rules before calling the StringTable.
inline wxString translate(const char* s, const char* plural, unsigned int n)
{
    return wxGetTranslation(wxString(s, wxConvUTF8), wxString(plural, wxConvUTF8), n);
}
inline wxString translate(const wchar_t* s, const wchar_t* plural, unsigned int n) { return wxGetTranslation(s, plural, n); }
inline wxString translate(const std::string& s, const std::string& plural, unsigned int n)
{
    return wxGetTranslation(wxString(s.c_str(), wxConvUTF8), wxString(plural.c_str(), wxConvUTF8), n);
}
inline wxString translate(const std::wstring& s, const std::wstring& plural, unsigned int n)
{
    return wxGetTranslation(s.c_str(), plural.c_str(), n);
}
inline wxString translate(const wxString& s, const wxString& plural, unsigned int n) { return wxGetTranslation(s, plural, n); }

// [INTENT] Offer UTF-8 `std::string` counterparts so code that needs narrow strings can reuse the same catalog state without repeating
// conversion logic. [UNITY] Equivalent to calling `LocalizedString.GetLocalizedString` and reading the `LocalizedString.Value` from a
// `StringTable` asset that feeds `LocalizationSettings`.
// [STATE][THREAD][PORTING_HAZARD:P2] The UTF-8 helpers mirror the same `wxLocale` cache but drop to narrow strings; they assume the global
// locale is stable, so Unity must publish the same `CultureInfo` change via the main thread and keep cached `StringTable` lookups in sync.
inline std::string translate_utf8(const char* s) { return wxGetTranslation(wxString(s, wxConvUTF8)).ToUTF8().data(); }
inline std::string translate_utf8(const wchar_t* s) { return wxGetTranslation(s).ToUTF8().data(); }
inline std::string translate_utf8(const std::string& s) { return wxGetTranslation(wxString(s.c_str(), wxConvUTF8)).ToUTF8().data(); }
inline std::string translate_utf8(const std::wstring& s) { return wxGetTranslation(s.c_str()).ToUTF8().data(); }
inline std::string translate_utf8(const wxString& s) { return wxGetTranslation(s).ToUTF8().data(); }

inline std::string translate_utf8(const char* s, const char* plural, unsigned int n) { return translate(s, plural, n).ToUTF8().data(); }
inline std::string translate_utf8(const wchar_t* s, const wchar_t* plural, unsigned int n)
{
    return translate(s, plural, n).ToUTF8().data();
}
inline std::string translate_utf8(const std::string& s, const std::string& plural, unsigned int n)
{
    return translate(s, plural, n).ToUTF8().data();
}
inline std::string translate_utf8(const std::wstring& s, const std::wstring& plural, unsigned int n)
{
    return translate(s, plural, n).ToUTF8().data();
}
inline std::string translate_utf8(const wxString& s, const wxString& plural, unsigned int n)
{
    return translate(s, plural, n).ToUTF8().data();
}

#if wxCHECK_VERSION(3, 1, 1)
#define _wxGetTranslation_ctx(S, CTX) wxGetTranslation((S), wxEmptyString, (CTX))
#else
#define _wxGetTranslation_ctx(S, CTX) ((void) (CTX), wxGetTranslation((S)))
#endif

// [INTENT] Provide context-aware wrappers so identical source strings in different dialogs produce the right translation via `wxLocale` and
// context. [STATE] Older wx versions ignore the context, so `_wxGetTranslation_ctx` gracefully drops it while newer versions pass it along
// to the catalog. [EVENT] Context helpers are bound to menu/menu-bar and dialog assembly events so they can override global string ids when
// the same literal repeats, and Unity should bake the context name into the `StringTable` key or maintain per-dialog tables.
// [PORTING_HAZARD:P3] Unity keys are usually unique, so encode the context inside the key (for example `Preview.Actions.Zoom`) or maintain
// separate tables per context.

inline wxString translate(const char* s, const char* ctx) { return _wxGetTranslation_ctx(wxString(s, wxConvUTF8), ctx); }
inline wxString translate(const wchar_t* s, const char* ctx) { return _wxGetTranslation_ctx(s, ctx); }
inline wxString translate(const std::string& s, const char* ctx) { return _wxGetTranslation_ctx(wxString(s.c_str(), wxConvUTF8), ctx); }
inline wxString translate(const std::wstring& s, const char* ctx) { return _wxGetTranslation_ctx(s.c_str(), ctx); }
inline wxString translate(const wxString& s, const char* ctx) { return _wxGetTranslation_ctx(s, ctx); }

inline std::string translate_utf8(const char* s, const char* ctx)
{
    return _wxGetTranslation_ctx(wxString(s, wxConvUTF8), ctx).ToUTF8().data();
}
inline std::string translate_utf8(const wchar_t* s, const char* ctx) { return _wxGetTranslation_ctx(s, ctx).ToUTF8().data(); }
inline std::string translate_utf8(const std::string& s, const char* ctx)
{
    return _wxGetTranslation_ctx(wxString(s.c_str(), wxConvUTF8), ctx).ToUTF8().data();
}
inline std::string translate_utf8(const std::wstring& s, const char* ctx) { return _wxGetTranslation_ctx(s.c_str(), ctx).ToUTF8().data(); }
inline std::string translate_utf8(const wxString& s, const char* ctx) { return _wxGetTranslation_ctx(s, ctx).ToUTF8().data(); }

#undef _wxGetTranslation_ctx
} // namespace I18N

// [INTENT] Convert a translated `std::string` to `wxString` so callers can keep using wxWidgets APIs without duplicating encoding logic.
// [STATE] This helper reflects whichever locale is currently bound to `wxLocale`, so downstream code can treat it as transient bridging
// state. [THREAD] `wxString` creation must run on the UI thread because it relies on `wxLocale` lifetime and needs `wxWidgets` event loop
// guarantees. [UNITY] The Unity counterpart would take a `LocalizedString.Value` and feed it to UI Toolkit or TMP controls as a plain
// `string`.
wxString L_str(const std::string& str);

} // namespace GUI
} // namespace Slic3r

// Macro to function both as a marker for xgettext and to actually perform the translation.
#ifndef _L_PLURAL
#define _L_PLURAL(s, plural, n) Slic3r::GUI::I18N::translate(s, plural, n)
#endif /* L */

#endif /* slic3r_GUI_I18N_hpp_ */
