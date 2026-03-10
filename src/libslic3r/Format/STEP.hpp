// [INTENT] STEP import bridge from OpenCASCADE's B-Rep document model into OrcaSlicer's
// internal triangle-mesh Model/ModelVolume representation.
//
// Pipeline overview:
//   1. StepPreProcessor optionally rewrites GBK-encoded STEP text into a UTF-8 temp file
//      so product / solid names survive into the UI.
//   2. Step::load() asks STEPCAFControl_Reader to parse the STEP assembly tree into an
//      XCAF document and enumerates free shapes / components as NamedSolid entries.
//   3. Step::mesh() tessellates each OpenCASCADE shape with BRepMesh_IncrementalMesh using
//      linear deflection (max chordal error) and angular deflection (max normal deviation).
//   4. The generated Poly_Triangulation faces are copied into admesh-style `stl_file` buffers,
//      then converted to TriangleMesh / ModelVolume so the rest of libslic3r sees standard mesh data.
//
// [COUPLING] This importer depends directly on OpenCASCADE document, shape, triangulation, and
//            progress APIs plus OrcaSlicer's Model / TriangleMesh ownership conventions.
// [MEMORY] OCCT handles use reference-counted `Handle(...)` smart handles, but this file also mixes
//          raw `Model*` mutation and a default `long&` argument bound to `new long(-1)`, which leaks.
// [CONCURRENCY] Parsing runs in a dedicated boost::thread for cancellable UI progress; tessellation
//               uses `tbb::parallel_for` over solids. OCCT thread-safety is assumed at the per-solid level.
// [HAZARD] STEP import is mesh-quality-sensitive: changing deflection defaults alters triangle count,
//          repair burden, and downstream slicing determinism.

#ifndef slic3r_Format_STEP_hpp_
#define slic3r_Format_STEP_hpp_
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFApp_Application.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>
#include <Message_ProgressIndicator.hxx>
#include <atomic>
#include <functional>
#include <iostream>
#include <string>

namespace fs = boost::filesystem;

namespace Slic3r {

class TriangleMesh;
class ModelObject;

// load step stage
const int LOAD_STEP_STAGE_READ_FILE = 0;
const int LOAD_STEP_STAGE_GET_SOLID = 1;
const int LOAD_STEP_STAGE_GET_MESH  = 2;
const int LOAD_STEP_STAGE_NUM       = 3;
const int LOAD_STEP_STAGE_UNIT_NUM  = 5;

typedef std::function<void(int load_stage, int current, int total, bool& cancel)> ImportStepProgressFn;
typedef std::function<void(bool isUtf8)>                                          StepIsUtf8Fn;

struct NamedSolid
{
    // [INTENT] Carry one OCCT shape plus its human-readable assembly name through the import pipeline.
    // `tri_face_cout` is a side channel used by get_triangle_num_tbb() to accumulate per-solid counts.
    NamedSolid(const TopoDS_Shape& s, const std::string& n) : solid{s}, name{n} {}
    const TopoDS_Shape solid;
    const std::string  name;
    int                tri_face_cout = 0;
};

// [INTENT] Convenience API that performs full STEP parse + tessellate + Model insertion in one call.
// [STATE] On success the caller's Model gains one ModelObject with one ModelVolume per surviving solid.
// [HAZARD] `mesh_face_num = *(new long(-1))` creates a leaked heap sentinel whenever the caller omits
//          the argument; translators should replace this with an optional / nullable out-parameter.
extern bool load_step(const char*          path,
                      Model*               model,
                      bool&                is_cancel,
                      double               linear_defletion = 0.003,
                      double               angle_defletion  = 0.5,
                      bool                 isSplitCompound  = false,
                      ImportStepProgressFn proFn            = nullptr,
                      StepIsUtf8Fn         isUtf8Fn         = nullptr,
                      long&                mesh_face_num    = *(new long(-1)));

// [INTENT] STEP names are plain text records, so OrcaSlicer peeks at the file encoding before OCCT reads it.
// If the file is UTF-8, the original path is passed through unchanged. If the file looks like GBK, a UTF-8
// temporary copy is generated so shape names display correctly. Unknown encodings are treated as UTF-8 and
// may surface as garbled names.
// [HAZARD] Detection is heuristic line-by-line byte inspection, not spec-level STEP string decoding.
class StepPreProcessor
{
    enum class EncodedType : unsigned char { UTF8, GBK, OTHER };

public:
    bool        preprocess(const char* path, std::string& output_path);
    static bool isUtf8File(const char* path);
    static bool isUtf8(const std::string str);

private:
    static bool isGBK(const std::string str);
    static int  preNum(const unsigned char byte);
    // BBS: default is UTF8 for most step file.
    EncodedType m_encode_type = EncodedType::UTF8;
};

class StepProgressIncdicator : public Message_ProgressIndicator
{
public:
    StepProgressIncdicator(std::atomic<bool>& stop_flag) : should_stop(stop_flag) {}

    // [CONCURRENCY] OCCT polls this flag from inside long-running parse / mesh operations. The flag is
    //               atomic because it is written by the UI/control thread and read by the worker thread.
    Standard_Boolean UserBreak() override { return should_stop.load(); }

    // [STATE] Progress is only surfaced by printing to stdout here; higher-level UI progress comes from
    //         Step::update_process(), so this callback is mostly diagnostic glue.
    void Show(const Message_ProgressScope&, const Standard_Boolean) override
    {
        std::cout << "Progress: " << GetPosition() << "%" << std::endl;
    }

private:
    std::atomic<bool>& should_stop;
};

class Step
{
public:
    enum class Step_Status { LOAD_SUCCESS, LOAD_ERROR, CANCEL, MESH_SUCCESS, MESH_ERROR };
    Step(fs::path path, ImportStepProgressFn stepFn = nullptr, StepIsUtf8Fn isUtf8Fn = nullptr);
    Step(std::string path, ImportStepProgressFn stepFn = nullptr, StepIsUtf8Fn isUtf8Fn = nullptr);
    ~Step();
    Step_Status  load();
    unsigned int get_triangle_num(double linear_defletion, double angle_defletion);
    unsigned int get_triangle_num_tbb(double linear_defletion, double angle_defletion);
    void         clean_mesh_data();
    // [INTENT] `linear_defletion` is OCCT's chordal tolerance in model units; smaller values generate more
    //          triangles. `angle_defletion` caps angular change between adjacent mesh facets.
    Step_Status mesh(Model* model, bool& is_cancel, bool isSplitCompound, double linear_defletion = 0.003, double angle_defletion = 0.5);

    std::atomic<bool> m_stop_mesh;
    void              update_process(int load_stage, int current, int total, bool& cancel);

private:
    // [STATE] `m_doc`, `m_shape_tool`, and `m_name_solids` retain importer state across the separate
    //         load() / mesh() / get_triangle_num() phases so the UI can inspect triangle counts before
    //         committing the tessellated result into the model.
    std::string          m_path;
    ImportStepProgressFn m_stepFn;
    StepIsUtf8Fn         m_utf8Fn;
    Handle(XCAFApp_Application) m_app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) m_doc;
    Handle(XCAFDoc_ShapeTool) m_shape_tool;
    std::vector<NamedSolid> m_name_solids;
};

}; // namespace Slic3r

#endif /* slic3r_Format_STEP_hpp_ */
