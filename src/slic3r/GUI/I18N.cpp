#include "I18N.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Central wxWidgets translation helper that insists on UTF-8 input so every call goes through the catalog with strict encoding.
// [STATE] Relies on the global `wxLocale` instance configured later in the GUI startup so the returned text follows the current locale.
// [STATE] `wxTranslations` caches the catalog lookup per locale, so callers must re-run `L_str` after a language-change event to refresh widget labels.
// [THREAD] wxGetTranslation uses the thread-local locale, so the helper must live on the GUI thread and mirror Unity's main-thread lookups.
// [EVENT] The Preferences language-change handler fires `wxLocale::AddCatalog` and a refresh event, so this function is the boundary where translated strings are re-fetched after that event.
// [PORTING_HAZARD:P3] Unity localization prefers string tables and async locale switching, so a LocalizationService should cache translations instead.
// [UNITY] Map to `LocalizationSettings.StringDatabase.GetLocalizedString(localeIdentifier, key, args)` or a dedicated `LocalizationManager.Translate` call.
wxString L_str(const std::string &str)
{
    //! Explicitly specify that the source string is already in UTF-8 encoding
    return wxGetTranslation(wxString(str.c_str(), wxConvUTF8));
}

} }
