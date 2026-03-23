#ifndef slic3r_ASCIIFolding_hpp_
#define slic3r_ASCIIFolding_hpp_

#include <string>

namespace Slic3r {

// [INTENT] Wrap every accent-removal helper so consumer code can serialize UI strings into firmware-safe ASCII.
// [UNITY] Would map to a `StringNormalization` helper (e.g., `RuntimeTextSanitizer.NormalizeToASCII`) that runs on the
// main thread before file/path creation.
// [PORTING_HAZARD:P3] Removing diacritics in Unity must mirror this table-driven map or risk renaming mismatches in legacy
// printer scripts when running under `System.IO`.
// If possible, remove accents from accented latin characters.
// This function is useful for generating file names to be processed by legacy firmwares.
// [INTENT][STATE] `is_convert_for_filename` flips on stricter replacement of whitespace and symbols so the caller keeps
// deterministic names even when fonts use multi-byte glyphs.
// [UNITY][THREAD] Equivalent to running `StringBuilder` rewrites on the main thread with `String.Normalize(NormalizationForm.FormD)`
// plus filtering out `UnicodeCategory.NonSpacingMark`. Be mindful that `.NET` `char` is UTF-16, so surrogate pairs must match this logic.
extern std::string fold_utf8_to_ascii(const std::string& src, bool is_convert_for_filename = false);

// Convert the input UNICODE character to a string of maximum 4 output ASCII characters.
// Return the end of the string written to the output.
// The output buffer must be at least 4 characters long.
// [INTENT][THREAD] Low-level converter that writes up to four ASCII `wchar_t`s per Unicode code point to help the caller
// build the final sanitized string incrementally.
// [UNITY][PORTING_HAZARD:P2] Unity's `char` is UTF-16 while `wchar_t` size varies per platform; porters need to treat the
// buffer as a `Span<char>`/`StringBuilder` and guard against surrogate pairs that may expand into two `wchar_t`s on Windows.
extern wchar_t* fold_to_ascii(wchar_t c, wchar_t* out);

} // namespace Slic3r

#endif /* slic3r_ASCIIFolding_hpp_ */
