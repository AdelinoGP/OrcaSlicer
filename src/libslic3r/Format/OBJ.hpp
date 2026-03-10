#ifndef slic3r_Format_OBJ_hpp_
#define slic3r_Format_OBJ_hpp_
#include "libslic3r/Color.hpp"
#include <unordered_map>
namespace Slic3r {

class TriangleMesh;
class Model;
class ModelObject;

// [INTENT] Side-channel metadata produced during OBJ import. Geometry itself is returned through
//          TriangleMesh / Model, while this struct preserves colour, UV, and texture references
//          needed by higher-level UI import flows.
// [STATE] Filled by load_obj() as an output parameter; callers are expected to treat existing
//         contents as disposable because the loader appends and overwrites fields opportunistically.
// [COUPLING] Mirrors parser-side concepts from objparser.hpp (materials, texture coordinates,
//            mtllib sidecars), so GUI import code can reconstruct material choices after the core
//            mesh has been collapsed into one TriangleMesh.
struct ObjInfo
{
    std::vector<RGBA>                    vertex_colors;
    std::vector<RGBA>                    face_colors;
    bool                                 is_single_mtl{false};
    std::string                          lost_material_name{""};
    std::vector<std::array<Vec2f, 3>>    uvs;
    std::string                          obj_dircetory;
    std::map<std::string, bool>          pngs;
    std::unordered_map<int, std::string> uv_map_pngs;
    bool                                 has_uv_png{false};
};

// [INTENT] Callback payload used by the GUI to map imported OBJ colours/materials onto extruders.
// [STATE] Acts as mutable input/output state for ObjImportColorFn: caller seeds input_colors and
//         model, callback fills filament_ids / first_extruder_id / deal_vertex_color decisions.
// [MEMORY] Stores only borrowed Model*; callback must not assume ownership or lifetime extension.
struct ObjDialogInOut
{ // input:colors array
    std::vector<RGBA> input_colors;
    bool              is_single_color{false};
    // colors array output:
    std::vector<unsigned char> filament_ids;
    unsigned char              first_extruder_id;
    bool                       deal_vertex_color;
    Model*                     model{nullptr};
    std::string                lost_material_name{""};
};
typedef std::function<void(ObjDialogInOut& in_out)> ObjImportColorFn;

// [INTENT] Low-level OBJ loader: parse one OBJ into one TriangleMesh plus ObjInfo metadata.
// [HAZARD] OBJ `o` and `g` partitions do not survive this boundary; the output mesh is a single
//          merged geometry payload even when the source file declared multiple named objects.
extern bool load_obj(const char* path, TriangleMesh* mesh, ObjInfo& vertex_colors, std::string& message);

// [INTENT] Higher-level convenience overload: wrap the merged mesh in exactly one ModelObject.
// [STATE] Appends to `model` on success; does not create one ModelVolume per named OBJ object.
extern bool load_obj(const char* path, Model* model, ObjInfo& vertex_colors, std::string& message, const char* object_name = nullptr);

extern bool store_obj(const char* path, TriangleMesh* mesh);
extern bool store_obj(const char* path, ModelObject* model);
extern bool store_obj(const char* path, Model* model);

}; // namespace Slic3r

#endif /* slic3r_Format_OBJ_hpp_ */
