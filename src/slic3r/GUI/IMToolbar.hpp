#ifndef slic3r_IMToolbar_hpp_
#define slic3r_IMToolbar_hpp_

#include <functional>
#include <string>
#include <vector>

#include "GLTexture.hpp"
#include "Event.hpp"
#include <imgui/imgui.h>

#define DEFAULT_TOOLBAR_BUTTON_WIDTH 80.0f
#define DEFAULT_TOOLBAR_BUTTON_HEIGHT 80.0f

namespace Slic3r { namespace GUI {

// [INTENT] Packs the icon textures and slice-progress flag for one toolbar entry so the ImGui render pass can show state + progress.
// [UNITY] Map to a `ScriptableObject`-backed toolbar data model driving a `Canvas`/`GraphicRaycaster` button that swaps `RawImage` textures
// when slicing states change.
class IMToolbarItem
{
public:
    enum SliceState {
        UNSLICED     = 0,
        SLICING      = 1,
        SLICED       = 2,
        SLICE_FAILED = 3,
    };

    bool       selected{false}; // [STATE] Marks whichever command is current so the toolbar can highlight it.
    float      percent;         // [STATE] Tracks slicing progress for the button that shows the current job.
    GLTexture  image_stats;     // [OPENGL] Hosts the stats icon GPU texture used while the slice is idle.
    GLTexture  image_slicing;   // [OPENGL] Streams active job thumbnails during slicing.
    GLTexture  image_idle;      // [OPENGL] Idle icon.
    GLTexture  image_failed;    // [OPENGL] Swap texture when a slice fails.
    SliceState slice_state; // [STATE] Current enum used by the render pass to decide which texture to show and whether to animate progress.

    ImTextureID texture_id{0}; // [OPENGL][PORTING_HAZARD:P2] ImGui-specific texture handle; Unity needs to keep `Texture2D` references and
                               // avoid manual GL binding.
    std::vector<unsigned char> image_data;   // [STATE] Raw RGBA data used to rebuild the GPU texture when necessary.
    unsigned int               image_width;  // [STATE] Width/height used when generating the GLTexture.
    unsigned int               image_height; // [STATE] Same for height.

    bool generate_texture(); // [OPENGL][THREAD] Called on the UI thread to load the latest image bytes into the GLTexture before ImGui
                             // draws the toolbar.
    ~IMToolbarItem();        // [INTENT][THREAD] Destroys GL textures on the UI thread so ImGui/Unity texture handles stay valid.
};

// [INTENT] Coordinates the ImGui toolbar layout, sizing, and enable/disable toggles for the rendering loop.
// [UNITY] Mirror this with a controller MonoBehaviour that feeds `List<ScriptableObject>` toolbar items into a `HorizontalLayoutGroup`.
class IMToolbar
{
private:
    bool m_enabled{false}; // [STATE] Enables/disables toolbar rendering so the ImGui pass can skip drawing when no data is ready.

public:
    float icon_width;
    float icon_height;             // [STATE] Controls the ImGui button size and translates to Unity's `RectTransform` height.
    bool  is_display_scrollbar;    // [STATE] Toggles the scrollbar so the layout can shrink to the available space.
    bool  show_stats_item{false};  // [STATE] Decides whether to keep an always-visible stats entry.
    bool  is_render_finish{false}; // [STATE] Signals that the last rendering pass completed so Unity can align `Canvas` repaint scheduling.
    IMToolbar()
    {
        icon_width  = DEFAULT_TOOLBAR_BUTTON_WIDTH;
        icon_height = DEFAULT_TOOLBAR_BUTTON_HEIGHT;
    }

    void del_all_item();   // [INTENT][THREAD] Clears every toolbar entry on the UI thread so the next machine/project can repopulate icons
                           // without stale state.
    void del_stats_item(); // [INTENT][THREAD] Removes the always-on stats entry when the stats overlay is hidden by user prefs.

    IMToolbarItem* m_all_plates_stats_item =
        nullptr; // [STATE] Optional summary entry that exposes the shared stats texture for the toolbar.
    std::vector<IMToolbarItem*> m_items =
        {}; // [STATE] Owner list of shared items; Unity would mirror this with `List<ScriptableObject>` plus `UI Toolkit ListView`.
    float fontScale; // [STATE][PORTING_HAZARD:P3] Tracks the scaling factor applied to the ImGui font so Unity can keep its `Text`/`TMP`
                     // scaling aligned.

    bool is_enabled() const { return m_enabled; }
    void set_enabled(
        bool enable); // [EVENT][THREAD] Called from UI panels on the main thread to gate toolbar visibility when switching views.

    void set_icon_size(
        float width,
        float height) // [EVENT][STATE] Adjusts the ImGui button sizing so Unity's RectTransform height/width matches the toolbar layout.
    {
        icon_width  = width;
        icon_height = height;
    }

    int get_items_count() { return m_items.size(); }
};

// [INTENT] Keeps the return button icon ready so the user can go back to the main toolbar view.
// [UNITY] Replace with a dedicated `Button` GameObject hosting a `RawImage` that swaps textures without ImGui hooks.
class IMReturnToolbar
{
private:
    bool m_enabled{false}; // [STATE] Tracks whether the return button is active so the toolbar can reappear on demand.
    ImTextureID
        texture_id; // [OPENGL][PORTING_HAZARD:P2] ImGui texture handle; Unity should keep a `Texture2D` and expose it to the button renderer.
    GLTexture return_textrue; // [OPENGL][PORTING_HAZARD:P3] Manual GPU texture lifecycle tied to ImGui, so Unity must own the Texture2D
                              // lifecycle on the C# side.

public:
    IMReturnToolbar() {}

    bool init(); // [INTENT][THREAD] Loads the return icon on the UI thread so the ImGui texture can be registered safely.
    bool is_enabled() const { return m_enabled; }
    void set_enabled(bool enable)
    {
        // [EVENT] Reacts to navigation state changes so the return button hides/shows as needed.
        m_enabled = enable;
    }
    ImTextureID get_return_texture_id() // [OPENGL] Exposes the ImGui texture handle to render the button icon.
    {
        return texture_id;
    }
};

}} // namespace Slic3r::GUI

#endif // slic3r_IMToolbar_hpp_
