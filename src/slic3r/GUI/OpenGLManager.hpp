#ifndef slic3r_OpenGLManager_hpp_
#define slic3r_OpenGLManager_hpp_

// [INTENT] OpenGLManager: Singleton manager for OpenGL context initialization, shader management,
// and capability detection. Provides GL canvas creation, shader program access, and static queries
// for OpenGL extensions (compressed textures, multisampling, framebuffers).
// [UNITY] Replace with a C# MonoBehaviour that initializes Unity's shader system (Shader.Find),
// checks Graphics capabilities (SystemInfo), and manages shader compilation from Unity ShaderLab files.
// [PORTING_HAZARD:P2] Unity's shader system is fundamentally different from raw OpenGL; GLSL shaders must be
// converted to Unity's ShaderLab/HLSL syntax, and the manager's context creation logic is irrelevant in Unity.

#include "GLShadersManager.hpp"

class wxWindow;
class wxGLCanvas;
class wxGLContext;

namespace Slic3r { namespace GUI {

class OpenGLManager
{
public:
    // [INTENT] Enumerates supported framebuffer types: unknown, ARB, or EXT.
    // [STATE] Used to track framebuffer extension availability for offscreen rendering.
    enum class EFramebufferType : unsigned char { Unknown, Arb, Ext };

    // [INTENT] Encapsulates detected OpenGL capabilities (version, extensions, limits).
    // [STATE] Holds cached GL info after detection; used for shader compatibility checks and feature toggles.
    // [UNITY] Map to a plain C# class populated via Unity's SystemInfo and GPU capabilities APIs.
    class GLInfo
    {
        bool  m_detected{false};
        bool  m_core_profile{false};
        int   m_max_tex_size{0};
        float m_max_anisotropy{0.0f};
#if ENABLE_OPENGL_AUTO_AA_SAMPLES
        int m_samples{0};
#endif // ENABLE_OPENGL_AUTO_AA_SAMPLES

        std::string m_version;
        std::string m_glsl_version;
        std::string m_vendor;
        std::string m_renderer;

    public:
        GLInfo() = default;

        const std::string& get_version() const;
        const std::string& get_glsl_version() const;
        const std::string& get_vendor() const;
        const std::string& get_renderer() const;

        bool is_core_profile() const { return m_core_profile; }

        bool is_mesa() const;

        int   get_max_tex_size() const;
        float get_max_anisotropy() const;

        bool is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const;
        bool is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const;

        // If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
        // Otherwise HTML formatted for the system info dialog.
        std::string to_string(bool for_github) const;

    private:
        // [INTENT] Populates GLInfo fields by querying the current OpenGL context.
        // [UNITY] Not needed; Unity's SystemInfo provides similar data.
        void detect() const;
    };

#ifdef __APPLE__
    // [INTENT] macOS version info used for a crash workaround when closing the app.
    // [STATE] Stores OS version numbers; populated once.
    // [UNITY] Not needed; Unity handles platform-specific crash reporting.
    struct OSInfo
    {
        int major{0};
        int minor{0};
        int micro{0};
    };
#endif //__APPLE__

private:
    // [INTENT] Tracks multisampling state for anti-aliasing.
    // [STATE] Unknown until detected; Enabled or Disabled based on hardware support.
    enum class EMultisampleState : unsigned char { Unknown, Enabled, Disabled };

    // [STATE] OpenGL initialization flag; ensures one-time context setup.
    bool m_gl_initialized{false};
    // [STATE] Current GL context; owned and managed by OpenGLManager.
    wxGLContext* m_context{nullptr};
    // [STATE] Shader manager instance; owns compiled shader programs.
    GLShadersManager m_shaders_manager;
    // [STATE] Cached GL capabilities; populated once on first access.
    static GLInfo s_gl_info;
#ifdef __APPLE__
    // [STATE] macOS version info for crash workaround.
    static OSInfo s_os_info;
#endif //__APPLE__
    // [STATE] Whether compressed textures (e.g., S3TC) are supported.
    static bool s_compressed_textures_supported;
    // [STATE] Whether to force power-of-two texture dimensions (legacy requirement).
    static bool s_force_power_of_two_textures;

    // [STATE] Multisampling state for anti-aliasing.
    static EMultisampleState s_multisample;
    // [STATE] Detected framebuffer extension type.
    static EFramebufferType s_framebuffers_type;

public:
    // [INTENT] Default constructor; initialization is deferred to init_gl().
    OpenGLManager() = default;
    // [INTENT] Destructor ensures GL context is properly released.
    ~OpenGLManager();

    // [INTENT] Initialize OpenGL extensions via GLEW and set up global GL state (framebuffer, multisampling).
    // [EVENT] Called once during application startup; errors may trigger a popup dialog.
    // [UNITY] Not needed in Unity; Graphics device is already initialized.
    bool init_gl(bool popup_error = true);
    // [INTENT] Create a wxGLContext for a specific canvas, with version and profile requirements.
    // [EVENT] Called when a GL canvas is created; returns a context to be used for rendering.
    // [UNITY] Not applicable; Unity manages its own rendering context per camera.
    wxGLContext* init_glcontext(wxGLCanvas&                canvas,
                                const std::pair<int, int>& required_opengl_version,
                                bool                       enable_compatibility_profile,
                                bool                       enable_debug);

    // [INTENT] Retrieve a named shader program from the shader manager.
    // [UNITY] Replace with Shader.Find() or a dictionary mapping names to Unity Shader objects.
    GLShaderProgram* get_shader(const std::string& shader_name) { return m_shaders_manager.get_shader(shader_name); }
    // [INTENT] Get the currently active shader program (for querying uniform locations).
    // [UNITY] Not needed; Unity's Shader.SetGlobal... APIs operate on the global shader state.
    GLShaderProgram* get_current_shader() { return m_shaders_manager.get_current_shader(); }

    // [INTENT] Static queries for OpenGL capabilities, used to adapt rendering pipeline.
    // [UNITY] Map to Unity's SystemInfo properties (e.g., SystemInfo.SupportsRenderTextureFormat).
    static bool             are_compressed_textures_supported() { return s_compressed_textures_supported; }
    static bool             can_multisample() { return s_multisample == EMultisampleState::Enabled; }
    static bool             are_framebuffers_supported() { return (s_framebuffers_type != EFramebufferType::Unknown); }
    static EFramebufferType get_framebuffers_type() { return s_framebuffers_type; }
    // [INTENT] Create a wxGLCanvas suitable for the current OpenGL capabilities.
    // [UNITY] Not applicable; Unity provides its own canvas (RawImage or RenderTexture).
    static wxGLCanvas*   create_wxglcanvas(wxWindow& parent);
    static const GLInfo& get_gl_info() { return s_gl_info; }
    static bool          force_power_of_two_textures() { return s_force_power_of_two_textures; }

private:
    // [INTENT] Detects multisampling support and sets attribute list accordingly.
    // [UNITY] Not needed; Unity's QualitySettings.antiAliasing handles multisampling.
    static void detect_multisample(int* attribList);
};

}} // namespace Slic3r::GUI

#endif // slic3r_OpenGLManager_hpp_
