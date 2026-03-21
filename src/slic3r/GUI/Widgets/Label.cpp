#include "libslic3r/Utils.hpp"
#include "Label.hpp"
#include "StaticBox.hpp"
#include <wx/intl.h> // For wxLocale
#include <wx/dcclient.h>
#include <wx/settings.h>
#include <boost/log/trivial.hpp>
/* [INTENT] Provide a consistent HarmonyOS/NanumGothic label font with system-aware scaling so every Label can request the same head/body
 * weights. [STATE] Chooses the face per locale and scales by platform before we cache the font for reuse. [PORTING_HAZARD:P3] Unity must
 * carry over the HarmonyOS/NanumGothic FontAssets (e.g., Addressable TMP_FontAssets) plus the `size * 4 / 5` scaling logic because `wxFont`
 * APIs are not available. [UNITY] Mirror this via a singleton `FontCatalog : ScriptableObject` that feeds TextMeshPro/UIToolkit fontSize
 * and FontAsset references, applying per-language fallbacks before binding to UI elements.
 */
wxFont Label::sysFont(int size, bool bold)
{
// #ifdef __linux__
//     return wxFont{};
// #endif
#ifndef __APPLE__
    size = size * 4 / 5;
#endif

    wxString face = "HarmonyOS Sans SC";

    // Check if the current locale is Korean
    if (wxLocale::GetSystemLanguage() == wxLANGUAGE_KOREAN) {
        face = "NanumGothic";
    }

    wxFont font{size, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL, false, face};
    font.SetFaceName(face);
    if (!font.IsOk()) {
        BOOST_LOG_TRIVIAL(warning) << boost::format("Can't find %1% font") % face;
        font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        BOOST_LOG_TRIVIAL(warning) << boost::format("Use system font instead: %1%") % font.GetFaceName();
        if (bold)
            font.MakeBold();
        font.SetPointSize(size);
    }
    return font;
}
wxFont Label::Head_48;
wxFont Label::Head_32;
wxFont Label::Head_24;
wxFont Label::Head_20;
wxFont Label::Head_18;
wxFont Label::Head_16;
wxFont Label::Head_15;
wxFont Label::Head_14;
wxFont Label::Head_13;
wxFont Label::Head_12;
wxFont Label::Head_11;
wxFont Label::Head_10;

wxFont Label::Body_16;
wxFont Label::Body_15;
wxFont Label::Body_14;
wxFont Label::Body_13;
wxFont Label::Body_12;
wxFont Label::Body_11;
wxFont Label::Body_10;
wxFont Label::Body_9;
wxFont Label::Body_8;

/* [STATE] Head_* and Body_* static wxFont instances cache the same sized fonts across the UI so repeated font creation is fast and
 * consistent. [UNITY] Equivalent to centralized TextMeshPro `GUIStyle`s or `FontCatalog` entries that reuse the same TMP_FontAsset for
 * multiple components.
 */

/* [INTENT] Preloads HarmonyOS and NanumGothic font resources on Linux/Windows and primes the shared font cache before any Label exists.
 * [STATE] `wxFont::AddPrivateFont` keeps the face names alive so `sysFont` always returns the expected typeface.
 * [PORTING_HAZARD:P2] Unity must load and hold TMP_FontAsset references (Addressables or ScriptableObjects) ahead of GUI usage instead of
 * calling `wxFont::AddPrivateFont`. [UNITY] Perform this work inside a singleton `FontCatalog` (ScriptableObject or runtime initializer)
 * that registers TMP_FontAssets before UI layout runs.
 */
void Label::initSysFont()
{
#if defined(__linux__) || defined(_WIN32)
    const std::string& resource_path = Slic3r::resources_dir();
    wxString           font_path     = wxString::FromUTF8(resource_path + "/fonts/HarmonyOS_Sans_SC_Bold.ttf");
    bool               result        = wxFont::AddPrivateFont(font_path);
    // BOOST_LOG_TRIVIAL(info) << boost::format("add font of HarmonyOS_Sans_SC_Bold returns %1%")%result;
    // printf("add font of HarmonyOS_Sans_SC_Bold returns %d\n", result);
    font_path = wxString::FromUTF8(resource_path + "/fonts/HarmonyOS_Sans_SC_Regular.ttf");
    result    = wxFont::AddPrivateFont(font_path);
    // BOOST_LOG_TRIVIAL(info) << boost::format("add font of HarmonyOS_Sans_SC_Regular returns %1%")%result;
    // printf("add font of HarmonyOS_Sans_SC_Regular returns %d\n", result);
    // Adding NanumGothic Regular and Bold
    font_path = wxString::FromUTF8(resource_path + "/fonts/NanumGothic-Regular.ttf");
    result    = wxFont::AddPrivateFont(font_path);
    // BOOST_LOG_TRIVIAL(info) << boost::format("add font of NanumGothic-Regular returns %1%")%result;
    // printf("add font of NanumGothic-Regular returns %d\n", result);
    font_path = wxString::FromUTF8(resource_path + "/fonts/NanumGothic-Bold.ttf");
    result    = wxFont::AddPrivateFont(font_path);
    // BOOST_LOG_TRIVIAL(info) << boost::format("add font of NanumGothic-Bold returns %1%")%result;
    // printf("add font of NanumGothic-Bold returns %d\n", result);
    Head_48 = Label::sysFont(48, true);
    Head_32 = Label::sysFont(32, true);
    Head_24 = Label::sysFont(24, true);
    Head_20 = Label::sysFont(20, true);
    Head_18 = Label::sysFont(18, true);
    Head_16 = Label::sysFont(16, true);
    Head_15 = Label::sysFont(15, true);
    Head_14 = Label::sysFont(14, true);
    Head_13 = Label::sysFont(13, true);
    Head_12 = Label::sysFont(12, true);
    Head_11 = Label::sysFont(11, true);
    Head_10 = Label::sysFont(10, true);

    Body_16 = Label::sysFont(16, false);
    Body_15 = Label::sysFont(15, false);
    Body_14 = Label::sysFont(14, false);
    Body_13 = Label::sysFont(13, false);
    Body_12 = Label::sysFont(12, false);
    Body_11 = Label::sysFont(11, false);
    Body_10 = Label::sysFont(10, false);
    Body_9  = Label::sysFont(9, false);
    Body_8  = Label::sysFont(8, false);
}

/* [INTENT] Wrap text width manually so labels can control line breaks without relying on platform auto-wrap. [STATE] Tracks the new-line
 * flag while generating text segments. [EVENT] This helper is invoked from `Label::Wrap` (called on resize or when auto-wrap is enabled) so
 * the event flow stays on the UI thread. [OPENGL] There is no GL state touched here; all wrapping happens before the widget renders, unlike
 * future Unity renderers. [UNITY] Replace with TMP_Text.GetPreferredValues + `enableWordWrapping` and cache the resulting string for layout
 * reuse via a helper MonoBehaviour. [PORTING_HAZARD:P3] The CJK aware wrap logic depends on `wxDC` metrics; Unity may need custom
 * word-wrapping rules if TMP/TextMeshPro gives different widths.
 */
class WXDLLIMPEXP_CORE wxTextWrapper2
{
public:
    wxTextWrapper2() { m_eol = false; }

    // win is used for getting the font, text is the text to wrap, width is the
    // max line width or -1 to disable wrapping
    void Wrap(wxWindow* win, const wxString& text, int widthMax)
    {
        const wxClientDC dc(win);
        Wrap(dc, text, widthMax);
    }

    void Wrap(wxDC const& dc, const wxString& text, int widthMax, int maxCount = 0)
    {
        const wxArrayString ls = wxSplit(text, '\n', '\0');
        for (wxArrayString::const_iterator i = ls.begin(); i != ls.end(); ++i) {
            wxString line  = *i;
            int      count = 0;

            if (i != ls.begin()) {
                // Do this even if the line is empty, except if it's the first one.
                OnNewLine();
            }

            // Is this a special case when wrapping is disabled?
            if (widthMax < 0) {
                DoOutputLine(line);
                continue;
            }

            for (bool newLine = false; !line.empty(); newLine = true) {
                if (newLine)
                    OnNewLine();

                if (1 == line.length()) {
                    DoOutputLine(line);
                    break;
                }

                wxArrayInt widths;
                dc.GetPartialTextExtents(line, widths);

                const size_t posEnd = std::lower_bound(widths.begin(), widths.end(), widthMax) - widths.begin();

                // Does the entire remaining line fit?
                if (posEnd == line.length()) {
                    DoOutputLine(line);
                    break;
                }

                // Find the last word to chop off.
                size_t lastSpace = posEnd;
                while (lastSpace > 0) {
                    auto c = line[lastSpace];
                    if (c == ' ')
                        break;
                    if (c > 0x4E00) {
                        if (lastSpace != posEnd)
                            ++lastSpace;
                        break;
                    }
                    --lastSpace;
                }
                if (lastSpace == 0) {
                    // No spaces, so can't wrap.
                    lastSpace = posEnd;
                }
                if (lastSpace == 0) {
                    // Break at least one char
                    lastSpace = 1;
                }

                // Output the part that fits.
                DoOutputLine(line.substr(0, lastSpace));

                // And redo the layout with the rest.
                if (line[lastSpace] == ' ')
                    ++lastSpace;
                line = line.substr(lastSpace);

                if (maxCount > 0 && ++count == maxCount - 1) {
                    OnNewLine();
                    DoOutputLine(line);
                    break;
                }
            }
        }
    }

    // we don't need it, but just to avoid compiler warnings
    virtual ~wxTextWrapper2() {}

protected:
    // line may be empty
    virtual void OnOutputLine(const wxString& line) = 0;

    // called at the start of every new line (except the very first one)
    virtual void OnNewLine() {}

private:
    // call OnOutputLine() and set m_eol to true
    void DoOutputLine(const wxString& line)
    {
        OnOutputLine(line);

        m_eol = true;
    }

    // this function is a destructive inspector: when it returns true it also
    // resets the flag to false so calling it again wouldn't return true any
    // more
    bool IsStartOfNewLine()
    {
        if (!m_eol)
            return false;

        m_eol = false;

        return true;
    }

    bool m_eol;
};

/* [INTENT] Stores the wrapped output so Label::Wrap can read the resulting multi-line string without re-measuring.
 * [STATE] `m_text` mirrors the formatted label text for reuse after wrapping events.
 * [UNITY] Equivalent to caching TMP_Text.text after calling `GetPreferredValues`, then reusing that string for layout updates.
 */
class wxLabelWrapper2 : public wxTextWrapper2
{
public:
    void WrapLabel(wxDC const& dc, wxString const& label, int widthMax)
    {
        m_text.clear();
        Wrap(dc, label, widthMax);
    }

    void WrapLabel(wxWindow* text, wxString const& label, int widthMax)
    {
        m_text.clear();
        Wrap(text, label, widthMax);
    }

    wxString GetText() const { return m_text; }

protected:
    virtual void OnOutputLine(const wxString& line) wxOVERRIDE { m_text += line; }

    virtual void OnNewLine() wxOVERRIDE { m_text += wxT('\n'); }

private:
    wxString m_text;
};

wxSize Label::split_lines(wxDC& dc, int width, const wxString& text, wxString& multiline_text, int max_count)
{
    wxLabelWrapper2 wrap;
    wrap.Wrap(dc, text, width, max_count);
    multiline_text = wrap.GetText();
    return dc.GetMultiLineTextExtent(multiline_text);
}

/* [INTENT] Initialize Label state (font, color, text, propagation flags) so every instance starts with consistent visuals.
 * [STATE] `m_font`, `m_text`, and `m_color` describe the current content that SetWindowStyleFlag and Wrap subsequently mutate.
 * [THREAD] Construction runs on the UI thread since wxWidgets emits events before the window shows.
 * [UNITY] Replace Label with a TextMeshProUGUI component whose state is controlled by a binder MonoBehaviour that mirrors these fields.
 */
Label::Label(wxWindow* parent, wxString const& text, long style, wxSize size) : Label(parent, Body_14, text, style, size) {}

Label::Label(wxWindow* parent, wxFont const& font, wxString const& text, long style, wxSize size)
    : wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, size, style)
{
    this->m_font = font;
    this->m_text = text;
    SetFont(font);
    SetForegroundColour(*wxBLACK);
    SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
    SetForegroundColour("#262E30");
    if (style & LB_PROPAGATE_MOUSE_EVENT) {
        // [EVENT] Forward left-clicks and releases to the parent handler so the label behaves like part of the containing panel.
        // [UNITY] Unity can call `ExecuteEvents.ExecuteHierarchy` on the parent GameObject when Pointer events arrive.
        for (auto evt : {wxEVT_LEFT_UP, wxEVT_LEFT_DOWN})
            Bind(evt, [this](auto& e) { GetParent()->GetEventHandler()->ProcessEventLocally(e); });
    };
    if (style & LB_AUTO_WRAP) {
        // [STATE] Auto-wrap keeps `m_text` aligned to the current width, using `m_skip_size_evt` to avoid recursion.
        // [EVENT] `wxEVT_SIZE` drives `Wrap`, so this binding must remain on the UI thread; Unity should mirror via
        // `OnRectTransformDimensionsChange`.
        Bind(wxEVT_SIZE, &Label::OnSize, this);
        Wrap(GetSize().x);
    }
}

/* [STATE] Keep `m_text` as the canonical string so SetWindowStyleFlag and wrapping only reflow when the text actually changes.
 * [EVENT] Auto-wrap calls `Wrap`, while macOS hyperlink labels rely on `SetLabelMarkup`, so the caller must signal the label when either
 * condition changes. [UNITY] Synchronize TMP_Text.text with this setter and reapply rich text markup via a helper function when a hyperlink
 * style is active.
 */
void Label::SetLabel(const wxString& label)
{
    if (m_text == label)
        return;
    m_text = label;
    if ((GetWindowStyle() & LB_AUTO_WRAP)) {
        Wrap(GetSize().x);
    } else {
        wxStaticText::SetLabel(label);
    }
#ifdef __WXOSX__
    if ((GetWindowStyle() & LB_HYPERLINK)) {
        SetLabelMarkup(label);
        return;
    }
#endif
}

/* [INTENT] Flip between the default label style and hyperlink presentation while caching the original color and font.
 * [STATE] `m_color` stores the base color so removing hyperlink state restores the same look.
 * [EVENT] Refresh is triggered so the wxStaticText redraws with the new cursor and underline; Unity should toggle PointerEnter/Exit
 * handlers accordingly. [UNITY] Toggle TMP_Text color/underline via a controller MonoBehaviour and push pointer cursor updates through an
 * attached `EventTrigger`. [PORTING_HAZARD:P3] macOS `SetLabelMarkup` has no direct analog, so hyperlink rendering must be implemented
 * through TMP rich text.
 */
void Label::SetWindowStyleFlag(long style)
{
    if (style == GetWindowStyle())
        return;
    wxStaticText::SetWindowStyleFlag(style);
    if (style & LB_HYPERLINK) {
        this->m_color = GetForegroundColour();
        static wxColor clr_url("#009688");
        SetFont(this->m_font.Underlined());
        SetForegroundColour(clr_url);
        SetCursor(wxCURSOR_HAND);
#ifdef __WXOSX__
        SetLabelMarkup(m_text);
#endif
    } else {
        SetForegroundColour(this->m_color);
        SetFont(this->m_font);
        SetCursor(wxCURSOR_ARROW);
#ifdef __WXOSX__
        wxStaticText::SetLabel({});
        SetLabel(m_text);
#endif
    }
    Refresh();
}

/* [INTENT] Measure wrapped text manually so binary wrapping decisions are consistent even without relying on native controls.
 * [STATE] `m_skip_size_evt` prevents re-entry while we rewrite the label text during the wrap pass.
 * [EVENT] Called from `wxEVT_SIZE`, so Unity should instead hook into `OnRectTransformDimensionsChange` or `LayoutRebuilder`.
 * [OPENGL] Text lives in the CPU path; no GPU textures are touched here, but Unity will need to update the TMP mesh after recalculating.
 * [PORTING_HAZARD:P3] This logic leans on `wxDC` metrics, so Unity must replicate it with TextMeshPro's `GetPreferredValues` to avoid
 * different line breaks. [UNITY] Use `TMP_Text.ForceMeshUpdate` and `GetPreferredValues(width)` on a temporary TMP object to emulate the
 * same wrapping.
 */
void Label::Wrap(int width)
{
    wxLabelWrapper2 wrapper;
    wrapper.Wrap(this, m_text, width);
    m_skip_size_evt = true;
    wxStaticText::SetLabel(wrapper.GetText());
    m_skip_size_evt = false;
}

/* [EVENT] Handle `wxEVT_SIZE` so auto-wrapped labels recalc when the rect changes, but skip if Wrap already fired.
 * [THREAD] Runs on the main UI thread; Unity counterparts should use `OnRectTransformDimensionsChange` to trigger re-wraps.
 * [STATE] `m_skip_size_evt` guards the handler from re-entry while we mutate the label text.
 * [UNITY] Mirror this with a TMP_Text wrapper controller that rewraps the string when `RectTransform.rect.width` changes.
 */
void Label::OnSize(wxSizeEvent& evt)
{
    evt.Skip();
    if (m_skip_size_evt)
        return;
    Wrap(evt.GetSize().x);
}
