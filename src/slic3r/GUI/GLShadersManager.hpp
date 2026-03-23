#ifndef slic3r_GLShadersManager_hpp_
#define slic3r_GLShadersManager_hpp_

#include "GLShader.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Slic3r {

class GLShadersManager
{
    // [STATE] Cache of compiled shader programs that must live for the lifetime of the GL context.
    // [THREAD] Access is restricted to the GL thread that owns the context; no locking is provided.
    std::vector<std::unique_ptr<GLShaderProgram>> m_shaders;

public:
    // [INTENT] Warm every known shader so viewport rendering can bind programs without per-frame compilation.
    // [EVENT] Invoked during the GUI_App::OnInit() sequence before creating the render canvas.
    // [OPENGL][THREAD] Must run on the GL thread after context creation; failure to do so leaves programs uncompiled.
    // [PORTING_HAZARD:P2] Unity must mirror this with a RenderPipeline-ready ShaderVariantCollection that preloads Shader assets before any draw calls.
    // [UNITY] Equivalent to a ScriptableObject/MonoBehaviour that holds Shader assets, warms them via ShaderVariantCollection.WarmUp(), and exposes Material instances.
    std::pair<bool, std::string> init();

    // call this method before to release the OpenGL context
    // [INTENT] Destroy GLShaderProgram instances before the context release to avoid dangling GPU resources.
    // [OPENGL][THREAD] Must run on the GL thread prior to context destruction; invoking it after context loss is invalid.
    // [PORTING_HAZARD:P2] Unity must explicitly unload or dispose the corresponding Materials when tearing down the custom RenderPipeline camera.
    void shutdown();

    // returns nullptr if not found
    // [INTENT] Return the cached shader program so drawers can bind it and set uniforms.
    // [STATE] Looks up the cache and signals failure when the shader name was never registered.
    // [UNITY] Mirrors a Dictionary<string, Shader> that hands out cached Shader/Material pairs inside a MonoBehaviour controller.
    GLShaderProgram* get_shader(const std::string& shader_name);

    // returns currently active shader, nullptr if none
    // [INTENT] Provide the program the manager believes is currently bound so callers can reuse it instead of rebinding.
    // [STATE] Evaluates to nullptr when no shader is active.
    // [UNITY] Plays the same role as storing the last Material/Shader index on a MaterialPropertyBlock before issuing Graphics.DrawMesh.
    GLShaderProgram* get_current_shader();
};

} // namespace Slic3r

#endif //  slic3r_GLShadersManager_hpp_
