#ifndef slic3r_3DBed_hpp_
#define slic3r_3DBed_hpp_

#include "GLTexture.hpp"
#include "3DScene.hpp"
#include "GLModel.hpp"

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/ExPolygon.hpp"

#include <tuple>
#include <array>

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

/*
class GeometryBuffer
{
    struct Vertex
    {
        Vec3f position{ Vec3f::Zero() };
        Vec2f tex_coords{ Vec2f::Zero() };
    };

    std::vector<Vertex> m_vertices;

public:
    bool set_from_triangles(const std::vector<Vec2f> &triangles, float z);
    bool set_from_lines(const Lines& lines, float z);
    //BBS: add APi to set from 3d lines
    bool set_from_3d_Lines(const Lines3& lines);

    const float* get_vertices_data() const;
    unsigned int get_vertices_data_size() const { return (unsigned int)m_vertices.size() * get_vertex_data_size(); }
    unsigned int get_vertex_data_size() const { return (unsigned int)(5 * sizeof(float)); }
    size_t get_position_offset() const { return 0; }
    size_t get_tex_coords_offset() const { return (size_t)(3 * sizeof(float)); }
    unsigned int get_vertices_count() const { return (unsigned int)m_vertices.size(); }
};
*/

// [INTENT][PORTING_HAZARD:P2] translate 2D bed outlines into the GLModel structure that OpenGL renders; Unity should treat this as
// mesh-generation helper feeding MeshFilter/MeshCollider bundles.
bool init_model_from_poly(GLModel& model, const ExPolygon& poly, float z);

class Bed3D
{
    // [INTENT][SCOPED THREAD:MAIN] owns the UI-visible 3D bed mesh, axes, and rendering cache shared with the GL scene manager.
public:
    // ORCA make bed colors accessable for 2D bed
    // [STATE][UNITY:ScriptableObject theme palette + shared color asset] these statics cache the bed/axis colors so the 3D bed mirrors the
    // 2D theme; Unity must sync this palette with the 2D overlay via a ScriptableObject singleton. [PORTING_HAZARD:P3] global statics mean
    // the palette is application-global and must be updated before canvases reuse a shared material instance.
    static ColorRGBA DEFAULT_MODEL_COLOR;
    static ColorRGBA DEFAULT_MODEL_COLOR_DARK;
    static ColorRGBA DEFAULT_SOLID_GRID_COLOR;
    static ColorRGBA DEFAULT_TRANSPARENT_GRID_COLOR;

    static ColorRGBA AXIS_X_COLOR;
    static ColorRGBA AXIS_Y_COLOR;
    static ColorRGBA AXIS_Z_COLOR;

    // [EVENT][STATE] fires when the theme changes so shaders reuse the updated palette; Unity should expose a theme change event (e.g., C#
    // event on ScriptableObject) to refresh shared materials.
    static void update_render_colors();
    static void load_render_colors();

    // [INTENT][STATE][OPENGL] draws the XYZ arrows that surround the bed when the axes mode is enabled.
    // [UNITY:LineRenderer + MeshFilter arrow helper] Unity should use LineRenderer/mesh primitives parented to the bed mesh for localized axes.
    class Axes
    {
    public:
        static const float DefaultStemRadius;
        static const float DefaultStemLength;
        static const float DefaultTipRadius;
        static const float DefaultTipLength;

    private:
        // [STATE][THREAD:MAIN] stored axis origin in model space; updated by main thread before render passes.
        Vec3d m_origin{Vec3d::Zero()};
        // [STATE] controls the arrow stem length; mutating it resets the GL mesh so new geometry is generated.
        float m_stem_length{DefaultStemLength};
        // [OPENGL][PORTING_HAZARD:P3] arrow geometry is cached in GLModel for direct OpenGL draws; Unity needs an equivalent MeshFilter primitive.
        GLModel m_arrow;

    public:
        const Vec3d& get_origin() const { return m_origin; }
        void         set_origin(const Vec3d& origin) { m_origin = origin; }
        void         set_stem_length(float length)
        {
            m_stem_length = length;
            m_arrow.reset();
        }
        float get_total_length() const { return m_stem_length; } // + DefaultTipLength; } // ORCA axis without arrow
        // [OPENGL][UNITY:MeshRenderer arrow helper] draws the cached arrow mesh; Unity should reuse a MeshRenderer/LineRenderer combo.
        void render();
    };

public:
    // [STATE][UNITY:enum + ScriptableObject flag] distinguishes printer-provided meshes from procedurally generated ones so the renderer
    // selects the right workflow.
    enum class Type : unsigned char {
        // The print bed model and texture are available from some printer preset.
        System,
        // The print bed model is unknown, thus it is rendered procedurally.
        Custom
    };

private:
    // [STATE] the build volume defines printer extents; Unity should keep the BuildVolume struct (or equivalent bounds) in sync with the
    // PrintVolume asset.
    BuildVolume m_build_volume;
    // [STATE] tracks whether we rely on the preset model or procedural fallback.
    Type m_type{Type::System};
    // std::string m_texture_filename;
    std::string m_model_filename;
    // Print volume bounding box exteded with axes and model.
    // [STATE][OPENGL] caches for frustum culling; keep in sync with render_mesh updates.
    BoundingBoxf3 m_extended_bounding_box;
    BoundingBoxf3 m_printable_bounding_box;
    // Slightly expanded print bed polygon, for collision detection.
    // Polygon m_polygon;
    GLModel m_triangles;
    // [STATE][OPENGL] procedural triangle mesh for the grid/collision outline.
    // GLModel m_gridlines;
    //  GLTexture m_texture;
    //  temporary texture shown until the main texture has still no levels compressed
    // GLTexture m_temp_texture;
    GLModel m_model;
    // [STATE][OPENGL] actual bed model that gets rendered; Unity should swap MeshFilter.sharedMesh instead of GLModel.
    Vec3d m_model_offset{Vec3d::Zero()};
    // [STATE] translation to align the model inside the build volume.
    Axes m_axes;

    float m_scale_factor{1.0f};
    // [STATE] scale applied by UI controls; Unity should expose a slider/ScriptableObject-bound property.
    // BBS: add part plate related logic
    Vec2d m_position{Vec2d::Zero()};
    // [STATE] base offset when composing multiple part plates on the bed.
    std::vector<Vec2d> m_bed_shape;
    // [STATE] outline used for collision detection and grid rendering.
    std::vector<std::vector<Vec2d>> m_extruder_shapes;
    // [STATE] per-extruder outlines to display multiple toolheads.
    std::vector<double> m_extruder_heights;
    // [STATE] keeps track of the current Z heights for each extruder footprint.
    bool m_is_dark = false;
    // [STATE][EVENT] toggles when dark mode is requested so render colors switch to the dark palette.

public:
    Bed3D()  = default;
    ~Bed3D() = default;

    // Update print bed model from configuration.
    // [EVENT][THREAD:MAIN][UNITY:MeshGenerator job] invoked when the printer preset or bed shape changes so Unity can refresh the
    // MeshFilter and collision mesh on the main thread. Return true if the bed shape changed, so the calee will update the UI.
    // FIXME if the build volume max print height is updated, this function still returns zero
    // as this class does not use it, thus there is no need to update the UI.
    // BBS
    bool set_shape(const Pointfs&       printable_area,
                   const double         printable_height,
                   std::vector<Pointfs> extruder_areas,
                   std::vector<double>  extruder_heights,
                   const std::string&   custom_model,
                   bool                 force_as_custom = false,
                   const Vec2d          position        = Vec2d::Zero(),
                   bool                 with_reset      = true);

    void set_position(Vec2d& position);
    // [EVENT][STATE] called when the user drags part plates so the bed offset updates before the next render.
    void set_axes_mode(bool origin);
    // [STATE][EVENT] toggles whether the axes origin is shown or hidden.
    const Vec2d& get_position() const { return m_position; }

    // Build volume geometry for various collision detection tasks.
    // [STATE][UNITY:ScriptableObject BuildVolume asset] expose the same bounds so Unity physics/query helpers can reuse the data.
    const BuildVolume& build_volume() const { return m_build_volume; }

    // Was the model provided, or was it generated procedurally?
    // [STATE] simple getter used by UI helpers to pick the right mesh source.
    Type get_type() const { return m_type; }
    // Was the model generated procedurally?
    bool is_custom() const { return m_type == Type::Custom; }

    // get the bed shape type
    // [STATE] expose whether the build volume is rectangular, circular, etc., so Unity overlay components can match behavior.
    BuildVolume_Type get_build_volume_type() const { return m_build_volume.type(); }

    // Bounding box around the print bed, axes and model, for rendering.
    // [STATE][OPENGL][UNITY:Bounds struct] used by renderers/collision helpers; Unity should convert these to Bounds for cameras and colliders.
    const BoundingBoxf3& extended_bounding_box() const { return m_extended_bounding_box; }
    const BoundingBoxf3& printable_bounding_box() const { return m_printable_bounding_box; }

    // Check against an expanded 2d bounding box.
    // FIXME shall one check against the real build volume?
    // [STATE][EVENT] used by picking helpers; Unity should borrow the same bounds when converting screen raycasts.
    bool contains(const Point& point) const;
    // [STATE] projects 3D points down to the 2D plane for UI overlays.
    Point point_projection(const Point& point) const;

    // [OPENGL][THREAD:MAIN][UNITY:RenderTexture + dedicated camera] draws the bed and axes; Unity should replay this via a
    // RenderTexture/composed mesh render job on the main render loop.
    void render(GLCanvas3D&        canvas,
                const Transform3d& view_matrix,
                const Transform3d& projection_matrix,
                bool               bottom,
                float              scale_factor,
                bool               show_axes);

    // [EVENT][STATE] called when dark mode toggles so render colors and grid palettes update.
    void on_change_color_mode(bool is_dark);

private:
    // BBS: add partplate related logic
    //  Calculate an extended bounding box from axes and current model for visualization purposes.
    // [STATE][OPENGL] recalculates caches when the model or axes shift.
    BoundingBoxf3 calc_printable_bounding_box() const;
    // [STATE][OPENGL] includes axes and bed meshes to tell the renderer the full visual extent.
    BoundingBoxf3 calc_extended_bounding_box() const;
    // [STATE][OPENGL] recalculates offsets when the model or print volume changes.
    void update_model_offset();
    // BBS: with offset
    // [OPENGL][STATE] rebuilds the procedural triangle mesh when the bed shape moves or extruders change.
    void update_bed_triangles();
    // [PORTING_HAZARD:P3][UNCLEAR] heuristics infer whether a model exists or the bed should be procedural; Unity needs to replicate this
    // detection or expose an explicit flag.
    static std::tuple<Type, std::string, std::string> detect_type(const Pointfs& shape);
    // [OPENGL][UNITY:RenderTexture + MeshFilter] splits bed render phases so Unity can sequence mesh draws before/after axes.
    void render_internal(GLCanvas3D&        canvas,
                         const Transform3d& view_matrix,
                         const Transform3d& projection_matrix,
                         bool               bottom,
                         float              scale_factor,
                         bool               show_axes);
    // [OPENGL][UNITY:LineRenderer arrow helper] draws the axes overlay.
    void render_axes();
    // [OPENGL] renders the system bed model (preset mesh) before custom overlays.
    void render_system(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom);
    // void render_texture(bool bottom, GLCanvas3D& canvas);
    // [OPENGL] builds the model-level draw call before custom overrides.
    void render_model(const Transform3d& view_matrix, const Transform3d& projection_matrix);
    // [OPENGL][UNITY] draws custom procedural beds; Unity must regenerate meshes for custom profiles and feed them to MeshFilter + MeshRenderer.
    void render_custom(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom);
    // [PORTING_HAZARD:P2] default render path blends axes/model with programmatic color resets; document this when translating to a Unity
    // shader stack.
    void render_default(bool bottom, const Transform3d& view_matrix, const Transform3d& projection_matrix);

    // BBS: remove the bed picking logic
    // void register_raycasters_for_picking(const GLModel::Geometry& geometry, const Transform3d& trafo);
};

} // GUI
} // Slic3r

#endif // slic3r_3DBed_hpp_
