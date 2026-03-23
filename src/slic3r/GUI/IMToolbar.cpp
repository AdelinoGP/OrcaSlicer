#include "IMToolbar.hpp"

#include "3DScene.hpp"
#include <GL/glew.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui.h>

#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "ImGuiWrapper.hpp"

namespace Slic3r {
namespace GUI {
IMToolbarItem::~IMToolbarItem()
{
    // [INTENT] Tear down the GPU handle when the toolbar item owner is deleted so the GL context stays clean.
    // [THREAD] Runs on the GL/UI thread because OpenGL resources are bound to that context.
    // [OPENGL] Deletes the texture created with `glGenTextures`.
    // [PORTING_HAZARD:P2] Unity would take care of `Texture2D` lifetime via managed destroy calls instead of raw GL handles.
    GLuint id = (GLuint) (int64_t) texture_id;
    if (id != 0)
        glsafe(::glDeleteTextures(1, &id));
}

bool IMToolbarItem::generate_texture()
{
    // [INTENT] Upload the cached RGBA bytes into a freshly created GL texture for ImGui to render.
    // [STATE] Updates `texture_id` so render passes know which texture to sample.
    // [OPENGL] Creates/binds a 2D texture, sets linear filtering, and pushes the pixel data into GPU memory.
    // [THREAD] Must execute on the UI/GL thread because it touches the bound texture unit.
    // [UNITY] Replace this with a `Texture2D` creation + `Graphics.CopyTexture` on the main thread or allocate at import time.
    // [PORTING_HAZARD:P3] Unity does not allow raw GL handles, so the port must wrap this logic with a managed texture cache.
    GLint          last_texture;
    unsigned       m_image_texture{0};
    unsigned char* pixels = (unsigned char*) (&image_data[0]);

    glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    glsafe(::glGenTextures(1, &m_image_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_image_texture));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    texture_id = (ImTextureID) (intptr_t) m_image_texture;

    // Restore state
    glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));

    return true;
}

void IMToolbar::del_all_item()
{
    // [INTENT] Explicitly destroy every toolbar item so switching configurations doesn’t leak icon textures.
    // [STATE] Clears the owning vector and resets pointers, mirroring a reset of the toolbar model in Unity.
    // [THREAD] Must run on the UI thread while no render pass is sampling the items array.
    // [PORTING_HAZARD:P3] Unity will rely on GC-managed collections rather than manual `delete`.
    for (int i = 0; i < m_items.size(); i++) {
        delete m_items[i];
        m_items[i] = nullptr;
    }
    m_items.clear();
}

void IMToolbar::del_stats_item()
{
    // [INTENT] Dispose of the shared stats entry when the stats overlay is hidden from the toolbar model.
    // [STATE] Nulls the pointer so subsequent checks know the stats texture is unavailable.
    // [THREAD] Should reside on the UI thread because GL texture cleanup happens below when the item is destroyed.
    delete m_all_plates_stats_item;
    m_all_plates_stats_item = nullptr;
}

void IMToolbar::set_enabled(bool enable)
{
    // [EVENT] Invoked by GUI panels to gate whether the toolbar should participate in the render pass.
    // [STATE] Tracks whether toolbar drawing remains active and resets the `is_render_finish` flag when we disable.
    // [UNITY] Equivalent to enabling/disabling the toolbar GameObject or CanvasGroup in Unity.
    m_enabled = enable;
    if (!m_enabled)
        is_render_finish = false;
}

bool IMReturnToolbar::init()
{
    // [INTENT] Loads the return-arrow SVG asset into a GL texture so the ImGui button can display it.
    // [STATE] Stores the texture handle in `texture_id` for later retrieval via `get_return_texture_id`.
    // [OPENGL] Same pattern as `IMToolbarItem::generate_texture` but for the dedicated return button icon.
    // [THREAD] Runs on the UI thread because it manipulates OpenGL state and uses synchronous disk IO (`get_data_from_svg`).
    // [UNITY] Unity should load the SVG or PNG via `Resources.Load<Texture2D>` or `UnityWebRequestTexture`, then assign to a UI `Button`'s
    // `RawImage`. [PORTING_HAZARD:P3] On Unity the heavy SVG rasterization should happen once (possibly offline), not during each toolbar refresh.
    bool     compress = false;
    GLint    last_texture;
    unsigned m_image_texture{0};

    std::string path = resources_dir() + "/images/";
    std::string file_name;

    file_name = path + "assemble_return.svg";

    ThumbnailData data;
    if (!get_data_from_svg(file_name, 20, data))
        return false;

    unsigned char* pixels = (unsigned char*) (&data.pixels[0]);
    glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    glsafe(::glGenTextures(1, &m_image_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_image_texture));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    if (compress && GLEW_EXT_texture_compression_s3tc)
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, data.width, data.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                              pixels));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, data.width, data.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    texture_id = (ImTextureID) (intptr_t) m_image_texture;

    // Restore state
    glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));

    return true;
}

} // namespace GUI
} // namespace Slic3r
