#ifndef slic3r_GLShader_hpp_
#define slic3r_GLShader_hpp_

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "libslic3r/Point.hpp"

namespace Slic3r {

class ColorRGB;
class ColorRGBA;

// [INTENT] Encapsulate a single OpenGL shader program so renderers can bind it via a stable API.
// [UNITY] Map to a Unity `Shader`/`Material` pair with `ShaderVariantCollection` to mirror per-pass permutations.
// [PORTING_HAZARD:P2] Relies on legacy GL program binding; Unity needs explicit per-camera material setup.

class GLShaderProgram
{
public:
    enum class EShaderType
    {
        Vertex,
        Fragment,
        Geometry,
        TessEvaluation,
        TessControl,
        Compute,
        Count
    };

    typedef std::array<std::string, static_cast<size_t>(EShaderType::Count)> ShaderFilenames;
    typedef std::array<std::string, static_cast<size_t>(EShaderType::Count)> ShaderSources;

private:
    std::string m_name;
    unsigned int m_id{ 0 };
    // [STATE] Cache of attribute lookups to avoid repeated `glGetAttribLocation` during frame rendering.
    std::vector<std::pair<std::string, int>> m_attrib_location_cache;
    // [STATE] Cache of uniform locations so render workers can set properties without querying GL every time.
    std::vector<std::pair<std::string, int>> m_uniform_location_cache;

public:
    ~GLShaderProgram();

    bool init_from_files(const std::string& name, const ShaderFilenames& filenames, const std::initializer_list<std::string_view> &defines = {});
    bool init_from_texts(const std::string& name, const ShaderSources& sources);

    const std::string& get_name() const { return m_name; }
    unsigned int get_id() const { return m_id; }

    // [EVENT] Called around each render batch to bind/unbind this program on the GL context thread.
    // [THREAD] Must run on the OpenGL owner thread that created `m_id` to avoid context violations.
    void start_using() const;
    void stop_using() const;

    // [OPENGL] Helpers that resolve uniform locations once before delegating to setter overloads.
    // [UNITY] Bridge to `Shader.PropertyToID` + `Material.Set*` in Unity so property hashing happens once per shader.
    void set_uniform(const char* name, int value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, bool value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, float value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, double value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<int, 2>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<int, 3>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<int, 4>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<float, 2>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<float, 3>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<float, 4>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const std::array<double, 4>& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const float* value, size_t size) const { set_uniform(get_uniform_location(name), value, size); }
    void set_uniform(const char* name, const Transform3f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Transform3d& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Matrix3f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Matrix3d& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Matrix4f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Matrix4d& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Vec2f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Vec2d& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Vec3f& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const Vec3d& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const ColorRGB& value) const { set_uniform(get_uniform_location(name), value); }
    void set_uniform(const char* name, const ColorRGBA& value) const { set_uniform(get_uniform_location(name), value); }

    void set_uniform(int id, int value) const;
    void set_uniform(int id, bool value) const;
    void set_uniform(int id, float value) const;
    void set_uniform(int id, double value) const;
    void set_uniform(int id, const std::array<int, 2>& value) const;
    void set_uniform(int id, const std::array<int, 3>& value) const;
    void set_uniform(int id, const std::array<int, 4>& value) const;
    void set_uniform(int id, const std::array<float, 2>& value) const;
    void set_uniform(int id, const std::array<float, 3>& value) const;
    void set_uniform(int id, const std::array<float, 4>& value) const;
    void set_uniform(int id, const std::array<double, 4>& value) const;
    void set_uniform(int id, const float* value, size_t size) const;
    void set_uniform(int id, const Transform3f& value) const;
    void set_uniform(int id, const Transform3d& value) const;
    void set_uniform(int id, const Matrix3f& value) const;
    void set_uniform(int id, const Matrix3d& value) const;
    void set_uniform(int id, const Matrix4f& value) const;
    void set_uniform(int id, const Matrix4d& value) const;
    void set_uniform(int id, const Vec2f& value) const;
    void set_uniform(int id, const Vec2d& value) const;
    void set_uniform(int id, const Vec3f& value) const;
    void set_uniform(int id, const Vec3d& value) const;
    void set_uniform(int id, const ColorRGB& value) const;
    void set_uniform(int id, const ColorRGBA& value) const;

    // [OPENGL] Query helpers that can fail (return -1) when a shader lacks the symbol, so callers regretfully skip the draw.
    int get_attrib_location(const char* name) const;
    int get_uniform_location(const char* name) const;
};

} // namespace Slic3r

#endif /* slic3r_GLShader_hpp_ */
