// [INTENT] OBJ format loader/exporter. Converts Wavefront OBJ + optional MTL sidecar into
// an indexed triangle set (ITS) suitable for OrcaSlicer's TriangleMesh.
//
// Loading pipeline:
//   1. ObjParser::objparse()     — tokenises vertices, faces, usemtl directives
//   2. ObjParser::mtlparse()     — reads MTL sidecar file(s) for material colours
//   3. Face validation           — rejects polygons with >4 or <3 vertices
//   4. ITS construction          — copies vertex coords; quads are split into 2 triangles
//   5. Per-face colour mapping   — Ka+Kd blending or Kd fallback; UV map/PNG tracking
//   6. Volume check              — negative volume → flip_triangles() for correct winding
//
// [STATE] load_obj(path, meshptr, ...) fills *meshptr in place.
//         load_obj(path, model, ...)  calls the above then inserts into Model.
//
// [COUPLING] ObjInfo output struct carries per-face colours, UV coordinates, PNG references,
//            and vertex colours — all consumed by the GUI import pipeline.
//            ObjParser is a separate module (objparser.hpp); MTL parsing is also delegated there.
// [HAZARD] Named OBJ `o` / `g` sections are parsed by objparser but ignored here.
//          All faces are flattened into one TriangleMesh, so load_obj(path, model, ...) creates
//          exactly one ModelObject containing one merged mesh regardless of how many named objects
//          or groups were present in the source OBJ.
//
// [HAZARD] store_obj() always returns true even if the underlying WriteOBJFile() fails (known FIXME).
// [HAZARD] MTL lookup via unordered map — missing material silently records lost_material_name
//          but continues loading. Geometry is still imported with default colour.
// [HAZARD] Quad triangulation (fan split: 0-1-2 and 0-2-3) is only correct for convex quads.
//          Concave quads produce incorrect triangulation without warning.
#include "../libslic3r.h"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"

#include "OBJ.hpp"
#include "objparser.hpp"

#include <string>

#include <boost/log/trivial.hpp>

// [INTENT] Platform-specific path separator for basename extraction.
#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

// Translation
#include "I18N.hpp"
#define _L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

// [INTENT] Primary OBJ load function. Fills *meshptr with geometry parsed from `path`.
//          Also populates obj_info with material/colour/UV metadata for GUI display.
// [STATE] *meshptr is overwritten on success. On failure it may be partially written — caller
//         should not use *meshptr after a false return.
// [MEMORY] ObjParser::ObjData and MtlData are local temporaries; ITS is built inline and
//          move-assigned into *meshptr at the end (line ~205).
bool load_obj(const char* path, TriangleMesh* meshptr, ObjInfo& obj_info, std::string& message)
{
    if (meshptr == nullptr)
        return false;
    // Parse the OBJ file.
    ObjParser::ObjData data;
    ObjParser::MtlData mtl_data;
    if (!ObjParser::objparse(path, data)) {
        BOOST_LOG_TRIVIAL(error) << "load_obj: failed to parse " << path;
        message = _L("load_obj: failed to parse");
        return false;
    }
    // [INTENT] Load all MTL sidecar files referenced by 'mtllib' directives in the OBJ.
    // [COUPLING] MTL path resolution: try as absolute path first, then relative to OBJ dir.
    // [HAZARD] If an MTL file is missing, logs a warning but continues without colours.
    bool exist_mtl = false;
    if (data.mtllibs.size() > 0) { // read mtl
        for (auto mtl_name : data.mtllibs) {
            if (mtl_name.size() == 0) {
                continue;
            }
            exist_mtl                                = true;
            bool                    mtl_name_is_path = false;
            boost::filesystem::path mtl_abs_path(mtl_name);
            if (boost::filesystem::exists(mtl_abs_path)) {
                mtl_name_is_path = true;
            }
            boost::filesystem::path mtl_path;
            if (!mtl_name_is_path) {
                boost::filesystem::path full_path(path);
                std::string             dir      = full_path.parent_path().string();
                auto                    mtl_file = dir + "/" + mtl_name;
                boost::filesystem::path temp_mtl_path(mtl_file);
                mtl_path = temp_mtl_path;
            }
            auto _mtl_path = mtl_name_is_path ? mtl_abs_path.string().c_str() : mtl_path.string().c_str();
            if (boost::filesystem::exists(mtl_name_is_path ? mtl_abs_path : mtl_path)) {
                if (!ObjParser::mtlparse(_mtl_path, mtl_data)) {
                    BOOST_LOG_TRIVIAL(error) << "load_obj:load_mtl: failed to parse " << _mtl_path;
                    message = _L("load mtl in obj: failed to parse");
                    return false;
                }
            } else {
                BOOST_LOG_TRIVIAL(error) << "load_obj: failed to load mtl_path:" << _mtl_path;
            }
        }
    }
    // [INTENT] First pass: count faces and validate polygon vertex counts.
    //          data.vertices is a flat array of ObjVertex structs; coordIdx==-1 marks face boundaries.
    // [COUPLING] data.objects / data.groups are intentionally ignored here. Their metadata survives
    //            parsing but does not participate in ModelObject / ModelVolume partitioning.
    // [HAZARD] Polygons with 5+ vertices immediately abort loading with an error.
    //          Polygons with exactly 4 vertices (quads) are counted separately for index pre-allocation.
    size_t num_faces = 0;
    size_t num_quads = 0;
    for (size_t i = 0; i < data.vertices.size(); ++i) {
        // Find the end of face.
        size_t j = i;
        for (; j < data.vertices.size() && data.vertices[j].coordIdx != -1; ++j)
            ;
        if (size_t num_face_vertices = j - i; num_face_vertices > 0) {
            if (num_face_vertices > 4) {
                // Non-triangular and non-quad faces are not supported as of now.
                BOOST_LOG_TRIVIAL(error) << "load_obj: failed to parse " << path
                                         << ". The file contains polygons with more than 4 vertices.";
                message = _L("The file contains polygons with more than 4 vertices.");
                return false;
            } else if (num_face_vertices < 3) {
                // Non-triangular and non-quad faces are not supported as of now.
                BOOST_LOG_TRIVIAL(error) << "load_obj: failed to parse " << path
                                         << ". The file contains polygons with less than 2 vertices.";
                message = _L("The file contains polygons with less than 2 vertices.");
                return false;
            }
            if (num_face_vertices == 4)
                ++num_quads;
            ++num_faces;
            i = j;
        }
    }
    // [INTENT] Second pass: build indexed_triangle_set. Quads are fan-triangulated into 2 triangles.
    // [HAZARD] Fan triangulation (0-1-2, 0-2-3) is only valid for convex quads.
    //          Concave OBJ quads will produce incorrect geometry silently.
    indexed_triangle_set its;
    size_t               num_vertices = data.coordinates.size() / OBJ_VERTEX_LENGTH;
    its.vertices.reserve(num_vertices);
    its.indices.reserve(num_faces + num_quads);
    if (exist_mtl) {
        obj_info.is_single_mtl = data.usemtls.size() == 1 && mtl_data.new_mtl_unmap.size() == 1;
        obj_info.face_colors.reserve(num_faces + num_quads);
    }
    // [INTENT] Copy vertex positions. OBJ_VERTEX_LENGTH = 7 (x, y, z, r, g, b, a) to support
    //          per-vertex colours stored as extra coordinate fields in extended OBJ variants.
    bool has_color = data.has_vertex_color;
    for (size_t i = 0; i < num_vertices; ++i) {
        size_t j = i * OBJ_VERTEX_LENGTH;
        its.vertices.emplace_back(data.coordinates[j], data.coordinates[j + 1], data.coordinates[j + 2]);
        if (data.has_vertex_color) {
            RGBA color{std::clamp(data.coordinates[j + 3], 0.f, 1.f), std::clamp(data.coordinates[j + 4], 0.f, 1.f),
                       std::clamp(data.coordinates[j + 5], 0.f, 1.f), std::clamp(data.coordinates[j + 6], 0.f, 1.f)};
            obj_info.vertex_colors.emplace_back(color);
        }
    }
    int indices[ONE_FACE_SIZE];
    int uvs[ONE_FACE_SIZE];
    // [INTENT] Walk the flat data.vertices array, collecting vertex indices per face
    //          (terminated by coordIdx==-1 sentinel) and building triangle indices.
    for (size_t i = 0; i < data.vertices.size();)
        if (data.vertices[i].coordIdx == -1)
            ++i;
        else {
            int cnt = 0;
            while (i < data.vertices.size())
                if (const ObjParser::ObjVertex& vertex = data.vertices[i++]; vertex.coordIdx == -1) {
                    break;
                } else {
                    assert(cnt < OBJ_VERTEX_LENGTH);
                    if (vertex.coordIdx < 0 || vertex.coordIdx >= int(its.vertices.size())) {
                        BOOST_LOG_TRIVIAL(error) << "load_obj: failed to parse " << path << ". The file contains invalid vertex index.";
                        message = _L("The file contains invalid vertex index.");
                        return false;
                    }
                    indices[cnt] = vertex.coordIdx;
                    uvs[cnt]     = vertex.textureCoordIdx;
                    cnt++;
                }
            if (cnt) {
                assert(cnt == 3 || cnt == 4);
                // Insert one or two faces (triangulate a quad).
                its.indices.emplace_back(indices[0], indices[1], indices[2]);
                int  face_index = its.indices.size() - 1;
                RGBA face_color;
                // [INTENT] Resolve material colour for a face by name lookup.
                //          Colour = Ka+Kd if their sum stays ≤1 per channel, else Kd only.
                //          Alpha is taken from Tr (transparency) channel.
                // [COUPLING] Reads mtl_data.new_mtl_unmap (map<string, MaterialData*>) built during MTL parse.
                // [HAZARD] If material name is not in the map, face colour is silently skipped and
                //          lost_material_name records the first missing name for UI warning.
                auto set_face_color = [&uvs, &data, &mtl_data, &obj_info, &face_color](int face_index, const std::string mtl_name) {
                    if (mtl_data.new_mtl_unmap.find(mtl_name) != mtl_data.new_mtl_unmap.end()) {
                        bool is_merge_ka_kd = true;
                        for (size_t n = 0; n < 3; n++) {
                            if (float(mtl_data.new_mtl_unmap[mtl_name]->Ka[n] + mtl_data.new_mtl_unmap[mtl_name]->Kd[n]) > 1.0) {
                                is_merge_ka_kd = false;
                                break;
                            }
                        }
                        for (size_t n = 0; n < 3; n++) {
                            if (is_merge_ka_kd) {
                                face_color[n] = std::clamp(float(mtl_data.new_mtl_unmap[mtl_name]->Ka[n] +
                                                                 mtl_data.new_mtl_unmap[mtl_name]->Kd[n]),
                                                           0.f, 1.f);
                            } else {
                                face_color[n] = std::clamp(float(mtl_data.new_mtl_unmap[mtl_name]->Kd[n]), 0.f, 1.f);
                            }
                        }
                        face_color[3] = mtl_data.new_mtl_unmap[mtl_name]->Tr; // alpha
                        if (mtl_data.new_mtl_unmap[mtl_name]->map_Kd.size() > 0) {
                            auto png_name       = mtl_data.new_mtl_unmap[mtl_name]->map_Kd;
                            obj_info.has_uv_png = true;
                            if (obj_info.pngs.find(png_name) == obj_info.pngs.end()) {
                                obj_info.pngs[png_name] = false;
                            }
                            obj_info.uv_map_pngs[face_index] = png_name;
                        }
                        if (data.textureCoordinates.size() > 0) {
                            Vec2f                uv0(data.textureCoordinates[uvs[0] * 2], data.textureCoordinates[uvs[0] * 2 + 1]);
                            Vec2f                uv1(data.textureCoordinates[uvs[1] * 2], data.textureCoordinates[uvs[1] * 2 + 1]);
                            Vec2f                uv2(data.textureCoordinates[uvs[2] * 2], data.textureCoordinates[uvs[2] * 2 + 1]);
                            std::array<Vec2f, 3> uv_array{uv0, uv1, uv2};
                            obj_info.uvs.emplace_back(uv_array);
                        }
                        obj_info.face_colors.emplace_back(face_color);
                    } else {
                        if (obj_info.lost_material_name.empty()) {
                            obj_info.lost_material_name = mtl_name;
                        }
                    }
                };
                // [INTENT] Dispatch face colour assignment: single-material OBJ uses index 0;
                //          multi-material OBJ does linear scan over usemtl ranges to find the
                //          material active at face_index. O(materials) per face — not O(1).
                // [HAZARD] Linear scan over usemtl ranges is O(M) per face. For large meshes with
                //          many material sections this becomes O(F*M) overall — a performance hazard.
                auto set_face_color_by_mtl = [&data, &set_face_color](int face_index) {
                    if (data.usemtls.size() == 1) {
                        set_face_color(face_index, data.usemtls[0].name);
                    } else {
                        for (size_t k = 0; k < data.usemtls.size(); k++) {
                            auto mtl = data.usemtls[k];
                            if (face_index >= mtl.face_start && face_index <= mtl.face_end) {
                                set_face_color(face_index, data.usemtls[k].name);
                                break;
                            }
                        }
                    }
                };
                if (exist_mtl) {
                    set_face_color_by_mtl(face_index);
                }
                if (cnt == 4) {
                    // [INTENT] Second triangle of fan-triangulated quad: vertices 0, 2, 3.
                    its.indices.emplace_back(indices[0], indices[2], indices[3]);
                    int face_index = its.indices.size() - 1;
                    if (exist_mtl) {
                        set_face_color_by_mtl(face_index);
                    }
                }
            }
        }

    // [INTENT] Move-construct TriangleMesh from ITS. Avoids a deep copy of all vertex/index data.
    *meshptr = TriangleMesh(std::move(its));
    if (meshptr->empty()) {
        BOOST_LOG_TRIVIAL(error) << "load_obj: This OBJ file couldn't be read because it's empty. " << path;
        message = _L("This OBJ file couldn't be read because it's empty.");
        return false;
    }
    // [INTENT] Negative signed volume means face winding is CW (inside-out).
    //          Flip all triangles to restore CCW convention expected by the slicing engine.
    if (meshptr->volume() < 0)
        meshptr->flip_triangles();
    return true;
}

// [INTENT] Convenience overload: loads OBJ into a temporary mesh then inserts into Model.
// [STATE] Successful import appends exactly one ModelObject to `model`, even if the OBJ declared
//         multiple `o`/`g` sections; this adapter does not split them into separate volumes.
// [COUPLING] object_name_in follows same basename fallback as load_stl().
bool load_obj(const char* path, Model* model, ObjInfo& obj_info, std::string& message, const char* object_name_in)
{
    TriangleMesh mesh;

    bool ret = load_obj(path, &mesh, obj_info, message);

    if (ret) {
        std::string object_name;
        if (object_name_in == nullptr) {
            const char* last_slash = strrchr(path, DIR_SEPARATOR);
            object_name.assign((last_slash == nullptr) ? path : last_slash + 1);
        } else
            object_name.assign(object_name_in);
        model->add_object(object_name.c_str(), path, std::move(mesh));
    }

    return ret;
}

// [INTENT] Write a TriangleMesh to OBJ format.
// [HAZARD] Always returns true even if WriteOBJFile() fails (known FIXME).
bool store_obj(const char* path, TriangleMesh* mesh)
{
    // FIXME returning false even if write failed.
    mesh->WriteOBJFile(path);
    return true;
}

// [INTENT] Convenience overload: merges ModelObject volumes into a single mesh before writing.
bool store_obj(const char* path, ModelObject* model_object)
{
    TriangleMesh mesh = model_object->mesh();
    return store_obj(path, &mesh);
}

// [INTENT] Convenience overload: merges all Model objects into a single mesh before writing.
bool store_obj(const char* path, Model* model)
{
    TriangleMesh mesh = model->mesh();
    return store_obj(path, &mesh);
}

}; // namespace Slic3r
