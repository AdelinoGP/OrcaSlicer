#ifndef slic3r_Format_STL_hpp_
#define slic3r_Format_STL_hpp_

#include <admesh/stl.h>

namespace Slic3r {

class Model;
class TriangleMesh;
class ModelObject;

// [INTENT] Public STL import / export facade used by higher-level model I/O code.
// It presents STL as a Model / ModelObject / TriangleMesh conversion boundary while
// delegating actual parsing to the admesh-backed TriangleMesh implementation.
//
// [STATE] load_stl() mutates the provided Model by appending a new ModelObject on success.
//           store_stl() is side-effecting only through filesystem writes.
//
// [MEMORY] The API passes raw pointers for existing objects but does not take ownership of them.
//            load_stl() transfers the loaded mesh into Model via move semantics in STL.cpp.
//
// [COUPLING] Import progress reporting uses admesh's ImportstlProgressFn type directly, so callers
//            are coupled to the admesh ABI as well as OrcaSlicer's Model / TriangleMesh types.
//
// [HAZARD] store_stl() reports success optimistically; callers cannot trust the bool result alone
//          to detect write failures without extra filesystem validation.
// Load an STL file into a provided model.
extern bool load_stl(
    const char* path, Model* model, const char* object_name = nullptr, ImportstlProgressFn stlFn = nullptr, int custom_header_length = 80);

extern bool store_stl(const char* path, TriangleMesh* mesh, bool binary);
extern bool store_stl(const char* path, ModelObject* model_object, bool binary);
extern bool store_stl(const char* path, Model* model, bool binary);

}; // namespace Slic3r

#endif /* slic3r_Format_STL_hpp_ */
