#ifndef slic3r_IconManager_hpp_
#define slic3r_IconManager_hpp_

#include <vector>
#include <memory>
#include "imgui/imgui.h"            // ImVec2
#include "slic3r/GUI/GLTexture.hpp" // texture storage

namespace Slic3r::GUI {

/// [INTENT] Owns the shared GPU atlas for all svgs/icons used by ImGui widgets.
/// Manages texture lifetime so callers can hold shared_ptr<Icon> without touching OpenGL directly.
class IconManager
{
public:
    /// <summary>
    /// Release texture
    /// Set shared pointers to invalid texture
    /// </summary>
    /// [THREAD] Destructor must run on the GL thread so m_icons_texture can delete GPU resources safely.
    ~IconManager();

    /// <summary>
    /// Define way to convert svg data to raster
    /// </summary>
    enum class RasterType : int {
        color           = 1 << 1,
        white_only_data = 1 << 2,
        gray_only_data  = 1 << 3,
        color_wite_gray = color | white_only_data | gray_only_data
        // TODO: add type with backgrounds
        // [UNITY] Map to Sprite/Tex2D variants; white/gray channels can become Unity shader keywords.
    };

    struct InitType
    {
        // path to file with image .. svg
        std::string filepath;

        // resolution of stored rasterized icon
        ImVec2 size; // float will be rounded

        // could contain more than one type
        RasterType type = RasterType::color;
        // together color, white and gray = color | white_only_data | gray_only_data
    };
    using InitTypes = std::vector<InitType>;
    // [STATE] The InitTypes list describes the atlas batch upload; retained only during init(), afterwards Icons keep the GPU data alive.

    /// <summary>
    /// Data for render texture with icon
    /// </summary>
    struct Icon
    {
        // [STATE] size actually uploaded to atlas; floats keep subpixel precision before ImGui render.
        ImVec2 size = ImVec2(-1, -1); // [in px] --> unsigned int values stored as float

        // [STATE] UV rect within the shared atlas texture; (tl, br) describe the sprite region.
        ImVec2 tl; // top left     -> uv0
        ImVec2 br; // bottom right -> uv1

        // [OPENGL] Atlas texture handle shared by every icon set.
        unsigned int tex_id = 0;
        bool         is_valid() const { return tex_id != 0; }
        // [UNITY] Can become a Texture2D + Rect (for Sprite) pair, with SpriteAtlas serving as the GPU cache.
        // && size.x > 0 && size.y > 0 && tl.x != br.x && tl.y != br.y;
    };
    using Icons = std::vector<std::shared_ptr<Icon>>;
    // Vector of icons, each vector contain multiple use of a SVG render
    using VIcons = std::vector<Icons>;

    /// <summary>
    /// Initialize raster texture on GPU with given images
    /// NOTE: Have to be called after OpenGL initialization
    /// </summary>
    /// <param name="input">Define files and its size with rasterization</param>
    /// <returns>Rasterized icons stored on GPU,
    /// Same size and order as input, each item of vector is set of texture in order by RasterType</returns>
    /// [THREAD] Uploads happen on the GL/context thread before these shared_ptrs are relayed to ImGui widgets.
    Icons init(const InitTypes& input);

    /// <summary>
    /// Initialize multiple icons with same settings for size and type
    /// NOTE: Have to be called after OpenGL initialization
    /// </summary>
    /// <param name="file_paths">Define files with icon</param>
    /// <param name="size">Size of stored texture[in px], float will be rounded</param>
    /// <param name="type">Define way to rasterize icon,
    /// together color, white and gray = RasterType::color | RasterType::white_only_data | RasterType::gray_only_data</param>
    /// <returns>Rasterized icons stored on GPU,
    /// Same size and order as file_paths, each item of vector is set of texture in order by RasterType</returns>
    /// [THREAD] Bulk variant reuses the same GLTexture and exposes staggered icon groups through Icons vectors.
    VIcons init(const std::vector<std::string>& file_paths, const ImVec2& size, RasterType type = RasterType::color);

    /// <summary>
    /// Release icons which are hold only by this manager
    /// May change texture and position of icons.
    /// </summary>
    /// [STATE] Clears m_icons and invalidates tex_id so any stale shared_ptr detects the texture is gone.
    void release();

private:
    // [OPENGL] GPU texture atlas where all icons share a single GLTexture resource.
    GLTexture m_icons_texture;

    // [STATE] Incremented whenever init/release reorganizes the atlas. Tracks atlas version for cache invalidation.
    unsigned int m_id{0}; // [UNCLEAR] Might flag texture rebinds; need implementation to confirm.
    // [STATE] Shared_ptr list returned to callers so that the atlas stays alive while icons are in use.
    Icons m_icons;
};

/// <summary>
/// Draw imgui image with icon
/// </summary>
/// <param name="icon">Place in texture</param>
/// <param name="size">[optional]Size of image, wen zero than use same size as stored texture</param>
/// <param name="tint_col">viz ImGui::Image </param>
/// <param name="border_col">viz ImGui::Image </param>
/// [PORTING_HAZARD:P2] Assumes ImGui immediate-mode draw calls; Unity port needs explicit mesh or SpriteRenderer.
/// [UNITY] Map to UI Toolkit Image or GameObject with Texture2D sampled via SpriteAtlas + UV rect.
void draw(const IconManager::Icon& icon,
          const ImVec2&            size       = ImVec2(0, 0),
          const ImVec4&            tint_col   = ImVec4(1, 1, 1, 1),
          const ImVec4&            border_col = ImVec4(0, 0, 0, 0));

/// <summary>
/// Draw icon which change on hover
/// </summary>
/// <param name="icon">Draw when no hover</param>
/// <param name="icon_hover">Draw when hover</param>
/// <returns>True when click, otherwise False</returns>
/// [EVENT] Provides ImGui hover/click semantics; port requires tracking pointer + hover state separately.
bool clickable(const IconManager::Icon& icon, const IconManager::Icon& icon_hover);

/// <summary>
/// Use icon as button with 3 states activ hover and disabled
/// </summary>
/// <param name="activ">Not disabled not hovered image</param>
/// <param name="hover">Hovered image</param>
/// <param name="disable">Disabled image</param>
/// <returns>True when click on enabled, otherwise False</returns>
/// [EVENT] Builds on clickable/draw semantics; Unity equivalent is Button with Sprite swap and interactable flag.
bool button(const IconManager::Icon& activ, const IconManager::Icon& hover, const IconManager::Icon& disable, bool disabled = false);

} // namespace Slic3r::GUI
#endif // slic3r_IconManager_hpp_
