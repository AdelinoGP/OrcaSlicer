#include "I18N.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Central wxWidgets translation helper that insists on UTF-8 input so every call goes through the catalog with strict encoding.
// [STATE] Relies on the global `wxLocale` instance configured later in the GUI startup so the returned text follows the current locale.
// [THREAD] wxGetTranslation uses the thread-local locale, so the helper must live on the GUI thread and mirror Unity's main-thread lookups.
// [PORTING_HAZARD:P3] Unity localization prefers string tables and async locale switching, so a LocalizationService should cache translations instead.
// [UNITY] Map to `LocalizationSettings.StringDatabase.GetLocalizedString(localeIdentifier, key, args)` or a dedicated `LocalizationManager.Translate` call.
wxString L_str(const std::string &str)
{
    //! Explicitly specify that the source string is already in UTF-8 encoding
    return wxGetTranslation(wxString(str.c_str(), wxConvUTF8));
}

} }
