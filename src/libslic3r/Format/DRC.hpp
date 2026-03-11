#ifndef slic3r_Format_DRC_hpp_
#define slic3r_Format_DRC_hpp_

namespace Slic3r {

// [INTENT] Public tuning bounds for Draco position quantization.
// `0` means "leave precision choice to the encoder defaults / caller policy" in current UI flows.
#define DRC_BITS_MIN 8
#define DRC_BITS_MAX 30
#define DRC_BITS_DEFAULT 0
#define DRC_BITS_DEFAULT_STR "0"
#define DRC_SPEED_DEFAULT 0

class TriangleMesh;
class ModelObject;
class Model;

// [INTENT] Import / export surface meshes through the Draco compressed mesh codec.
// [COUPLING] These overloads intentionally mirror STL/OBJ helpers so higher-level import code can switch on
//            extension and call a uniform `(path, TriangleMesh*)` or `(path, Model*)` signature.
extern bool load_drc(const char* path, TriangleMesh* meshptr);
extern bool load_drc(const char* path, Model* model, const char* object_name = nullptr);

extern bool store_drc(const char* path, TriangleMesh* mesh, int bits, int speed = DRC_SPEED_DEFAULT);
extern bool store_drc(const char* path, ModelObject* model_object, int bits, int speed = DRC_SPEED_DEFAULT);
extern bool store_drc(const char* path, Model* model, int bits, int speed = DRC_SPEED_DEFAULT);

}; // namespace Slic3r

#endif /* slic3r_Format_DRC_hpp_ */
