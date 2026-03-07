// [INTENT] Thin adapter layer between the STL file format and OrcaSlicer's internal Model.
// All real STL parsing logic lives in TriangleMesh::ReadSTLFile() (libstl / admesh).
// This file is intentionally minimal — it only handles:
//   1. Delegating binary/ASCII STL reading to TriangleMesh
//   2. Deriving the object name from the file path when none is provided
//   3. Inserting the resulting TriangleMesh into a Model as a new ModelObject
//   4. Providing three overloaded store_stl() helpers (mesh / object / full model)
//
// [COUPLING] Direct dependency on Model::add_object() and TriangleMesh ownership transfer (std::move).
//            Callers own the Model* lifetime; this file does not manage it.
//
// [HAZARD] store_stl() always returns true even if the underlying write fails.
//          This is a known bug (FIXME comments in source). Do not rely on the return value
//          for error detection when writing STL files.

#include "../libslic3r.h"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"

#include "STL.hpp"

#include <string>

// [INTENT] Platform-specific path separator for strrchr-based basename extraction.
#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

namespace Slic3r {

// [INTENT] Load a single STL file into the given Model as a new ModelObject.
// [STATE] On success: model gains a new ModelObject containing one ModelVolume with the loaded mesh.
//         On failure: model is unchanged; returns false.
// [MEMORY] Mesh is constructed on the stack then std::move'd into the model — no heap copy.
// [COUPLING] stlFn is an optional progress callback (ImportstlProgressFn); may be nullptr.
//            custom_header_length allows reading non-standard binary STL headers (e.g., BambuLab files).
bool load_stl(const char *path, Model *model, const char *object_name_in, ImportstlProgressFn stlFn, int custom_header_length)
{
    TriangleMesh mesh;
    std::string design_id;

    // [INTENT] ReadSTLFile dispatches to binary or ASCII parser based on file magic bytes.
    // [HAZARD] Returns false for empty mesh AND for parse failure — caller cannot distinguish.
    if (!mesh.ReadSTLFile(path, true, stlFn, custom_header_length)) {
        //    die "Failed to open $file\n" if !-e $path;
        return false;
    }
    if (mesh.empty()) {
        // die "This STL file couldn't be read because it's empty.\n"
        return false;
    }

    // [INTENT] Derive object name from file path basename if no explicit name is given.
    std::string object_name;
    if (object_name_in == nullptr) {
        const char *last_slash = strrchr(path, DIR_SEPARATOR);
        object_name.assign((last_slash == nullptr) ? path : last_slash + 1);
    } else
       object_name.assign(object_name_in);

    // [INTENT] Transfer mesh ownership to Model; model is now responsible for lifetime.
    // [MEMORY] std::move avoids deep copy of potentially large triangle arrays.
    model->add_object(object_name.c_str(), path, std::move(mesh));
    return true;
}

// [INTENT] Write a TriangleMesh directly to a binary or ASCII STL file.
// [HAZARD] Always returns true even if the write operation fails (known FIXME).
bool store_stl(const char *path, TriangleMesh *mesh, bool binary)
{
    if (binary)
        mesh->write_binary(path);
    else
        mesh->write_ascii(path);
    //FIXME returning false even if write failed.
    return true;
}

// [INTENT] Convenience overload: merges all volumes of a ModelObject into a single mesh before writing.
// [MEMORY] model_object->mesh() allocates a temporary merged TriangleMesh on the stack.
bool store_stl(const char *path, ModelObject *model_object, bool binary)
{
    TriangleMesh mesh = model_object->mesh();
    return store_stl(path, &mesh, binary);
}

// [INTENT] Convenience overload: merges all objects in a Model into a single mesh before writing.
// [MEMORY] model->mesh() allocates a temporary merged TriangleMesh on the stack.
bool store_stl(const char *path, Model *model, bool binary)
{
    TriangleMesh mesh = model->mesh();
    return store_stl(path, &mesh, binary);
}

}; // namespace Slic3r