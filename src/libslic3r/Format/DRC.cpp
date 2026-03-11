// [INTENT] Draco mesh codec adapter. This file bridges compressed `.drc` payloads to OrcaSlicer's
// `indexed_triangle_set` / `TriangleMesh` types and back again.
//
// Load path:
//   1. Memory-map the whole file
//   2. Ask Draco whether the payload is a triangular mesh
//   3. Decode Draco's indexed mesh into Orca vertices + triangle indices
//   4. Normalize winding by checking signed volume and flipping if negative
//
// [STATE] `load_drc(path, meshptr)` overwrites the caller-provided mesh on success.
//         `load_drc(path, model, ...)` mutates `Model` by appending one object.
// [COUPLING] Depends directly on Draco's `Mesh`/`PointAttribute` API and on Orca's
//            `TriangleMesh` constructor from `indexed_triangle_set`.
// [HAZARD] Draco attributes can be quantized / dequantized during round-trip, so exported geometry is
//          not guaranteed bit-identical to the original mesh even when topology is preserved.
// [HAZARD] The writer only serializes vertex positions + triangle indices. Any Orca-side metadata
//          (materials, per-face colours, object partitioning) is lost by design.

#include <string>
#include <utility>
#include <cstring>

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/nowide/cstdio.hpp>

#include <draco/compression/encode.h>
#include <draco/compression/decode.h>
#include <draco/io/mesh_io.h>
#include <draco/mesh/mesh.h>
using namespace draco;

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "DRC.hpp"

// [INTENT] Keep basename extraction logic portable when synthesizing a `ModelObject` name from the path.
#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

namespace Slic3r {

bool load_drc(const char* path, TriangleMesh* meshptr)
{
    try {
        // [INTENT] Read the complete compressed stream through a memory map so Draco can decode directly
        // from contiguous bytes without building a second temporary buffer.
        // [MEMORY] `mapped_file_source` owns the file mapping for this scope only.
        boost::iostreams::mapped_file_source file(path);

        DecoderBuffer buffer;
        buffer.Init(file.data(), file.size());

        // [INTENT] Reject point clouds / other Draco geometry kinds early because Orca's import pipeline
        // can only materialize triangular surface meshes into TriangleMesh.
        auto geotype = Decoder::GetEncodedGeometryType(&buffer);
        if ((!geotype.ok()) || geotype.value() != TRIANGULAR_MESH) {
            return false;
        }

        Decoder decoder;
        Mesh    dracoMesh;
        Status  status = decoder.DecodeBufferToGeometry(&buffer, &dracoMesh);
        if (!status.ok()) {
            return false;
        }
        file.close();

        // [INTENT] Rebuild Orca's canonical mesh carrier so downstream repair, slicing, and Model insertion
        // can reuse the exact same code paths as STL / OBJ / 3MF imports.
        indexed_triangle_set its;

        const PointAttribute* const positions    = dracoMesh.GetNamedAttribute(GeometryAttribute::POSITION);
        size_t                      num_vertices = positions->size();
        its.vertices.reserve(num_vertices);
        for (AttributeValueIndex i(0); i < num_vertices; ++i) {
            float pos[3];
            positions->ConvertValue<float>(i, 3, pos);
            its.vertices.emplace_back(pos[0], pos[1], pos[2]);
        }

        size_t num_faces = dracoMesh.num_faces();
        its.indices.reserve(num_faces);
        for (FaceIndex i(0); i < num_faces; ++i) {
            Mesh::Face face = dracoMesh.face(i);

            // [HAZARD] Draco face indices reference point IDs, not necessarily the dense attribute order.
            // `mapped_index()` is the critical translation step back into the position attribute domain.
            its.indices.emplace_back(positions->mapped_index(face[0]).value(), positions->mapped_index(face[1]).value(),
                                     positions->mapped_index(face[2]).value());
        }

        // [MEMORY] Move-construct the TriangleMesh so large vertex/index arrays are transferred, not copied.
        *meshptr = TriangleMesh(std::move(its));
        // [INTENT] Draco does not encode Orca's winding convention; signed volume is used as a cheap global
        // orientation test before the mesh enters the normal repair pipeline.
        if (meshptr->volume() < 0)
            meshptr->flip_triangles();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "load_drc: " << e.what();
        return false;
    }
    return true;
}

bool load_drc(const char* path, Model* model, const char* object_name_in)
{
    TriangleMesh mesh;

    bool ret = load_drc(path, &mesh);

    if (ret) {
        std::string object_name;
        // [INTENT] Mirror STL / OBJ import behavior: default the logical object name to the source filename
        // so GUI scene lists have something human-readable even when Draco carries no object metadata.
        if (object_name_in == nullptr) {
            const char* last_slash = strrchr(path, DIR_SEPARATOR);
            object_name.assign((last_slash == nullptr) ? path : last_slash + 1);
        } else
            object_name.assign(object_name_in);

        // [STATE] Ownership of the decoded mesh transfers into a newly created ModelObject / ModelVolume.
        model->add_object(object_name.c_str(), path, std::move(mesh));
    }

    return ret;
}

bool store_drc(const char* path, TriangleMesh* mesh, int bits, int speed)
{
    try {
        const std::vector<stl_triangle_vertex_indices>* indices  = &(mesh->its.indices);
        const std::vector<stl_vertex>*                  vertices = &(mesh->its.vertices);

        Mesh dracoMesh;

        dracoMesh.set_num_points(vertices->size());

        // [INTENT] Encode only a POSITION attribute because Draco export here is used as a compact geometry
        // container, not as a full-fidelity project interchange format.
        GeometryAttribute gaPos;
        gaPos.Init(GeometryAttribute::POSITION, nullptr, 3, DT_FLOAT32, false, sizeof(float) * 3, 0);
        int32_t idPos = dracoMesh.AddAttribute(gaPos, true, indices->size() * 3);

        dracoMesh.attribute(idPos)->Resize(vertices->size());

        for (size_t i = 0; i < vertices->size(); ++i) {
            float vertex[3];
            vertex[0] = vertices->at(i)(0);
            vertex[1] = vertices->at(i)(1);
            vertex[2] = vertices->at(i)(2);
            dracoMesh.attribute(idPos)->SetAttributeValue(AttributeValueIndex(i), vertex);
        }

        dracoMesh.SetNumFaces(indices->size());
        for (size_t i = 0; i < indices->size(); ++i) {
            Mesh::Face face;
            face[0] = PointIndex(indices->at(i)[0]);
            face[1] = PointIndex(indices->at(i)[1]);
            face[2] = PointIndex(indices->at(i)[2]);
            dracoMesh.SetFace(FaceIndex(i), face);
        }

        Encoder encoder;
        // [INTENT] Caller controls the compression tradeoff explicitly:
        // `bits` tunes position quantization precision, `speed` tunes Draco encode/decode effort.
        encoder.SetSpeedOptions(speed, speed);
        encoder.SetAttributeQuantization(GeometryAttribute::POSITION, bits);

        EncoderBuffer buffer;
        encoder.EncodeMeshToBuffer(dracoMesh, &buffer);

        FILE* fp = boost::nowide::fopen(path, "wb");
        if (!fp)
            return false;
        size_t written = fwrite(buffer.data(), 1, buffer.size(), fp);
        fclose(fp);

        if (written != buffer.size())
            return false;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "store_drc: " << e.what();
        return false;
    }
    return true;
}

bool store_drc(const char* path, ModelObject* model_object, int bits, int speed)
{
    // [INTENT] Collapse multi-volume objects into one export mesh because `.drc` has no native Orca object graph.
    TriangleMesh mesh = model_object->mesh();
    return store_drc(path, &mesh, bits, speed);
}

bool store_drc(const char* path, Model* model, int bits, int speed)
{
    // [INTENT] Full-model export is likewise flattened to a single triangle soup before compression.
    TriangleMesh mesh = model->mesh();
    return store_drc(path, &mesh, bits, speed);
}

}; // namespace Slic3r
