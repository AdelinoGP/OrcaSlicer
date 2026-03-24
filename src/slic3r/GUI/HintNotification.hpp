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

// [INTENT][STATE][THREAD][UNITY] Singleton that loads hint metadata, tracks which IDs were shown, and answers navigation calls on the UI
// thread; Unity can replace this with a `RuntimeInitializeOnLoadMethod` that populates a `ScriptableObject` `HintCatalog` and exposes
// UI-thread navigation helpers.
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
    // [EVENT][THREAD][PORTING_HAZARD:P2] Queries the singleton from the UI dispatcher before rendering so Unity can wrap this in its
    // main-thread notification routine.
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
    // [EVENT][THREAD][PORTING_HAZARD:P3][UNITY] Clears cached hint state when the user changes language/settings; Unity should expose a
    // `HintCatalog.Reset` method triggered from the Settings controller before rebuilding the notification panel.
    void uninit();
    // [EVENT][THREAD] Rebuilds the hint caches after a reload, so the UI is guaranteed to observe fresh data when `get_hint` runs again.
    void reinit();

private:
    void init();
    // [STATE][PORTING_HAZARD:P3] Seeds the random cursor via `rand()` so Unity should mimic this with `Random.state` to keep order stable.
    void init_random_hint_id();
    // [INTENT][PORTING_HAZARD:P2][UNITY] Parses hints.ini via boost::property_tree; Unity should load the equivalent TextAsset and
    // deserialize it into a ScriptableObject-backed catalog (JSON/XML) so the filtering code works the same.
    void load_hints_from_file(const boost::filesystem::path& path);
    bool is_used(const std::string& id);
    void set_used(const std::string& id);
    void clear_used();
    // Returns position in m_loaded_hints with next hint chosed randomly with weights
    size_t                   get_next_hint_id();
    size_t                   get_prev_hint_id();
    size_t                   get_random_next();
    size_t                   m_hint_id;            // [STATE] cursor pointing into m_loaded_hints so navigation updates stay predictable
    bool                     m_initialized{false}; // [STATE] marks whether the hints have been parsed so callers know caches are usable
    std::vector<HintData>    m_loaded_hints;
    bool                     m_sorted_hints{false}; // [STATE] ensures weight-sorted hints are only recomputed when the list changes
    std::vector<std::string> m_used_ids;
    bool                     m_used_ids_loaded{false}; // [STATE] signals the persisted "used" set is available before deduping
};
// [INTENT][STATE][THREAD][PORTING_HAZARD:P3][UNITY] Floating Did-You-Know notification managed by NotificationManager's wxTimer-driven
// queue; lives entirely on the UI thread so Unity must host it inside a VisualElement row synced by a DispatcherTimer/coroutine to prevent
// cross-thread updates.
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
    // [EVENT][UNITY] Right/left arrow control that flips hint navigation; keep Unity's `Button.onClick` bound to `open_next`/`open_prev`
    // and update the VisualElement text before the next frame.
    void render_right_arrow_button(
        ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y);
    // [EVENT][PORTING_HAZARD:P2] Documentation button launches a browser; Unity must show its own confirmation before calling `Application.OpenURL`.
    void render_documentation_button(
        ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y);
    // [OPENGL] Displays the notification icon texture; Unity can layer this onto a `VisualElement` background image.
    void render_logo(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y);
    // [STATE][EVENT][THREAD][UNITY][PORTING_HAZARD:P3] Queries `HintDatabase` to refresh the displayed `HintData` while NotificationManager's
    // wxTimer enqueues updates; Unity must replay this as a DispatcherTimer coroutine to keep VisualElements on the main thread.
    void retrieve_data(bool new_hint = true);
    // [PORTING_HAZARD:P2][EVENT][UNITY] Bridge to `wxGetApp()`'s browser warning dialog; Unity must dispatch through `MainThreadDispatcher`
    // to show a confirmation overlay before calling `Application.OpenURL` so the same warning gating exists.
    void open_documentation();

    bool                      m_has_hint_data{false}; // [STATE] tracks whether the latest hint has been fully cached for rendering
    std::function<void(void)> m_hypertext_callback;   // [EVENT] bound action triggered by hyperlink clicks
    std::string               m_disabled_tags;        // [STATE] filtering metadata copied from HintData
    std::string               m_enabled_tags;         // [STATE] hints only trigger when these tags pass
    bool                      m_runtime_disable;      // [STATE] mirrors HintData runtime_disable to gate tag reevaluation per click
    std::string               m_documentation_link;   // [STATE] stored so the documentation button knows whether to render
    float                     m_close_b_y{0}; // [STATE] caches close-button Y coordinate so hover detection uses consistent geometry
    float                     m_close_b_w{0}; // [STATE] caches width to throttle repeated close instructions while the button is hovered
    // hover of buttons
    long m_docu_hover_time{0};  // [STATE] tooltip hover timer for the documentation button
    long m_prefe_hover_time{0}; // [STATE] tooltip hover timer for the preferences button
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_HintNotification_hpp_
