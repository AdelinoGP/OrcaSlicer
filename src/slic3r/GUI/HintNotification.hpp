#ifndef slic3r_GUI_HintNotification_hpp_
#define slic3r_GUI_HintNotification_hpp_

#include "NotificationManager.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Persistent metadata record for hint text, weights, filters, and callbacks so the notification generator can describe and
// gate hints across sessions.
struct HintData
{
    std::string id_string;
    std::string text;
    size_t      weight;
    bool        was_displayed; // [STATE] marks hints already rendered so cycles skip repeats without clearing the cache
    std::string hypertext;
    std::string follow_text;
    std::string disabled_tags;
    std::string enabled_tags;
    bool        runtime_disable; // [STATE] toggles tag evaluation before every hyperlink click when hints run in runtime-only mode
    std::string documentation_link;
    std::string image_url;
    std::function<void(void)> callback{nullptr};
};

enum class HintDataNavigation {
    Curr,
    Prev,
    Next,
    Random,
};

// [EVENT] Commands that drive the hint carousel from buttons/hooks so Unity can mirror the same verbs in its UI Toolkit overlay.

// [INTENT][STATE][THREAD] Singleton that loads hint metadata, tracks which IDs were shown, and answers navigation calls on the UI thread.
class HintDatabase
{
public:
    static HintDatabase& get_instance()
    {
        static HintDatabase instance; // Guaranteed to be destroyed.
                                      // Instantiated on first use.
        return instance;
    }

private:
    HintDatabase() : m_hint_id(0) {}

public:
    ~HintDatabase();
    HintDatabase(HintDatabase const&)   = delete;
    void operator=(HintDatabase const&) = delete;

    // return true if HintData filled;
    // [EVENT][PORTING_HAZARD:P2] Queries the singleton on the UI dispatcher so Unity can wrap this in its main-thread notification routine.
    HintData* get_hint(HintDataNavigation nav);
    size_t    get_index() { return m_hint_id; }
    size_t    get_count()
    {
        if (!m_initialized)
            return 0;
        return m_loaded_hints.size();
    }
    // resets m_initiailized to false and writes used if was initialized
    // used when reloading in runtime - like change language
    void uninit();
    void reinit();

private:
    void init();
    // [STATE][PORTING_HAZARD:P3] Seeds the random cursor via `rand()` so Unity should mimic this with `Random.state` to keep order stable.
    void init_random_hint_id();
    // [INTENT][PORTING_HAZARD:P2] Parses hints.ini via boost::property_tree; Unity must mirror the file format in managed code (JSON/XML).
    void load_hints_from_file(const boost::filesystem::path& path);
    bool is_used(const std::string& id);
    void set_used(const std::string& id);
    void clear_used();
    // Returns position in m_loaded_hints with next hint chosed randomly with weights
    size_t                   get_next_hint_id();
    size_t                   get_prev_hint_id();
    size_t                   get_random_next();
    size_t                   m_hint_id; // [STATE] cursor pointing into m_loaded_hints so navigation updates stay predictable
    bool                     m_initialized{false};
    std::vector<HintData>    m_loaded_hints;
    bool                     m_sorted_hints{false};
    std::vector<std::string> m_used_ids;
    bool                     m_used_ids_loaded{false};
};
// [INTENT][UNITY] Floating Did-You-Know notification that draws via ImGui and can be replaced in Unity with a Canvas overlay + GraphicRaycaster
class NotificationManager::HintNotification : public NotificationManager::PopNotification
{
public:
    HintNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, bool new_hint)
        : PopNotification(n, id_provider, evt_handler)
    {
        retrieve_data(new_hint);
    }
    // [EVENT][STATE] Subscribe to the hint lifecycle so the notification UI gets a hint on init and knows how to move through the catalog.
    virtual void init() override;
    // [EVENT] User-facing "next" arrow fires open_next() which re-queries the singleton with a navigation command.
    void open_next() { retrieve_data(); }

protected:
    // [OPENGL][UNITY] Keeps the ImGui window height in sync with the calculated line count so Unity can adjust `VisualElement.style.height`.
    virtual void set_next_window_size(ImGuiWrapper& imgui) override;
    // [OPENGL] Measures whitespace and icon padding before layout, a detail the Unity overlay must mirror for consistent spacing.
    virtual void count_spaces() override;
    // [STATE][OPENGL] Splits multi-line text so `render_text` knows how to paint each wrap, similar to TextMeshPro layout passes.
    virtual void count_lines() override;
    // [EVENT][STATE] Hypertext clicks go through this method, gating on `m_runtime_disable` and tag checks.
    virtual bool on_text_click() override;
    // [OPENGL][UNITY] Renders all hint strings, hypertext, and follow-up text inside an ImGui window; Unity should replicate with UI
    // Toolkit text blocks, TextMeshPro formatting, and fade adjustments.
    virtual void render_text(
        ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y) override;
    // [EVENT][OPENGL] Paints the close arrow and forwards to arrow/Preferences/documentation buttons so Unity can expose the same hit areas.
    virtual void render_close_button(
        ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y) override;
    virtual void render_minimize_button(ImGuiWrapper& imgui, const float win_pos_x, const float win_pos_y) override {}

    // [EVENT][PORTING_HAZARD:P3][UNITY] Preferences button proxies the minimize slot, so Unity should drive its Settings panel through a
    // `Command` router.
    void render_preferences_button(ImGuiWrapper& imgui, const float win_pos_x, const float win_pos_y);
    // [EVENT] Right/left arrow control that flips hint navigation; keep Unity's buttons bound to `open_next`/`open_prev`.
    void render_right_arrow_button(
        ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y);
    // [EVENT][PORTING_HAZARD:P2] Documentation button launches a browser; Unity must show its own confirmation before calling `Application.OpenURL`.
    void render_documentation_button(
        ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y);
    // [OPENGL] Displays the notification icon texture; Unity can layer this onto a `VisualElement` background image.
    void render_logo(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y);
    // [STATE][EVENT] Queries `HintDatabase` to refresh the displayed `HintData` and respects `new_hint` to honor navigation.
    void retrieve_data(bool new_hint = true);
    // [PORTING_HAZARD:P2][EVENT] Bridge to `wxGetApp()`'s browser warning dialog; Unity must not call `Application.OpenURL` without a
    // confirmation overlay.
    void open_documentation();

    bool                      m_has_hint_data{false}; // [STATE] tracks whether the latest hint has been fully cached for rendering
    std::function<void(void)> m_hypertext_callback;   // [EVENT] bound action triggered by hyperlink clicks
    std::string               m_disabled_tags;        // [STATE] filtering metadata copied from HintData
    std::string               m_enabled_tags;         // [STATE] hints only trigger when these tags pass
    bool                      m_runtime_disable;      // [STATE] mirrors HintData runtime_disable to gate tag reevaluation per click
    std::string               m_documentation_link;   // [STATE] stored so the documentation button knows whether to render
    float                     m_close_b_y{0};
    float                     m_close_b_w{0};
    // hover of buttons
    long m_docu_hover_time{0};  // [STATE] tooltip hover timer for the documentation button
    long m_prefe_hover_time{0}; // [STATE] tooltip hover timer for the preferences button
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_HintNotification_hpp_
