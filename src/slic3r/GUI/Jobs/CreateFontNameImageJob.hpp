#ifndef slic3r_CreateFontNameImageJob_hpp_
#define slic3r_CreateFontNameImageJob_hpp_

#include <vector>
#include <string>
#include <GL/glew.h>
#include <wx/string.h>
#include <wx/fontenc.h>
#include "Job.hpp"
#include "libslic3r/Point.hpp" // Vec2i32

namespace Slic3r::GUI {

/// <summary>
/// Keep data for rasterization of text by font face
/// </summary>
struct FontImageData
{
    // [INTENT] Describe what string the caller needs rasterized for the atlas slot.
    std::string text;
    // [STATE] Chosen face name and encoding pair; the Unity port will map this to a FontAsset + Localization key.
    wxString       font_name;
    wxFontEncoding encoding;
    // [OPENGL][UNITY] Target GL texture ID / atlas slot; in Unity this becomes a Texture2D/RenderTexture slot that must be written on the
    // main thread.
    GLuint texture_id;
    // [STATE] Atlas offset index used to place this face in the shared texture array.
    size_t index;
    // [STATE] Expected pixel size limit for the rasterized glyph block.
    Vec2i32 size; // in px

    // [STATE] Contrast control for the generated glyph; higher value darkens the raster (divided by 255).
    unsigned char gray_level = 5;

    // [OPENGL] Meta data describing the texture format when finalizing the upload.
    GLenum format = GL_ALPHA, type = GL_UNSIGNED_BYTE;
    GLint  level = 0;

    // [THREAD] Guard that throttles the number of open font files; decremented from the main thread when finalize runs.
    unsigned int* count_opened_font_files = nullptr;

    // [THREAD] Cancel token shared with the worker queue so Unity's Task cancellation or a coroutine can stop processing.
    std::shared_ptr<std::atomic<bool>> cancel = nullptr;
    // [STATE] Flag that notes when the texture data has been created for UI refresh.
    std::shared_ptr<bool> is_created = nullptr;
};

/// <summary>
/// Create image for face name
/// </summary>
class CreateFontImageJob : public Job
{
    // [STATE] Immutable request blob captured at job creation (text/font combo, texture slot, cancel tokens).
    FontImageData m_input;
    // [STATE] Buffered raster bytes produced during process() before the GL upload.
    std::vector<unsigned char> m_result;
    // [STATE]/[OPENGL] Pixel dimensions of the generated image to validate atlas placement.
    Point m_tex_size;

public:
    // [INTENT] Construct the job from caller-provided request metadata so the worker can run without touching UI state.
    CreateFontImageJob(FontImageData&& input);
    /// <summary>
    /// Rasterize text into image (result)
    /// </summary>
    /// <param name="ctl">Check for cancelation</param>
    // [THREAD] Runs on the JobManager's background worker thread; must check ctl/cancel and avoid touching wxWidgets.
    void process(Ctl& ctl) override;

    /// <summary>
    /// Copy image data into OpenGL texture
    /// </summary>
    /// <param name="canceled"></param>
    /// <param name=""></param>
    // [THREAD] Invoked on the main/UI thread to upload the texture data and adjust shared state.
    // [UNITY] Replace with `UnityMainThreadDispatcher.Instance().Enqueue(() => { ... Texture2D.Apply(); })` if porting.
    void finalize(bool canceled, std::exception_ptr&) override;

    /// <summary>
    /// Text used for generate preview for empty text
    /// and when no glyph for given m_input.text
    /// </summary>
    static const std::string default_text;
};

} // namespace Slic3r::GUI

#endif // slic3r_CreateFontNameImageJob_hpp_
