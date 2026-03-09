// [INTENT] OpenCASCADE BRep-based text generation pipeline: alternative to the
// stb_truetype path in Emboss.hpp. Uses Font_BRepFont + Font_BRepTextBuilder to
// produce a BRep solid, then triangulates to TriangleMesh via BRepMesh_IncrementalMesh.
// [COUPLING] Depends heavily on OpenCASCADE (Font_FontMgr, Font_BRepFont, Font_BRepTextBuilder,
//            BRepMesh_IncrementalMesh, admesh stl_file). Cannot be used without OCCT.
// [STATE] g_occt_fonts_maps is a module-level static in TextShape.cpp, populated once
//         by init_occt_fonts() at startup. See H1034 for thread-safety notes.
// [CONCURRENCY] [HAZARD H1034 P1/High] g_occt_fonts_maps (static std::map in TextShape.cpp)
//               is written by init_occt_fonts() and read by get_occt_fonts_maps() / load_text_shape().
//               If init_occt_fonts() is ever called concurrently (e.g. from multiple plater jobs
//               that all trigger lazy init), the map write is a data race. Currently init is
//               called once from the main thread at startup, so safe in practice — but there is
//               no synchronization enforcing that contract.
#ifndef slic3r_Text_Shape_hpp_
#define slic3r_Text_Shape_hpp_

#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r {
class TriangleMesh;

// [INTENT] Return type for load_text_shape(): carries the generated mesh and total text width.
struct TextResult
{
    TriangleMesh text_mesh;
    double       text_width;
};

// [INTENT] Populate g_occt_fonts_maps from the OS font manager via OCCT Font_FontMgr.
// Must be called once from the main thread before any call to load_text_shape().
// Returns the sorted list of base font names (without bold/italic variants).
extern std::vector<std::string> init_occt_fonts();

// [INTENT] Generate a 3D TriangleMesh for a text string using the OCCT BRep pipeline.
// Pipeline: text string → Font_BRepFont glyph outlines → Font_BRepTextBuilder 2D BRep
//           → BRepPrimAPI_MakePrism 3D solid → BRepMesh_IncrementalMesh → stl_file → TriangleMesh.
// [HAZARD H1035] BRepPrimAPI_MakePrism is heap-allocated in Prism() without deletion — memory leak
//               per call. See TextShape.cpp for details.
// [HAZARD H1036] MakeMesh() performs OCCT triangulation → stl_file (admesh C struct) → TriangleMesh
//               — a two-copy path with extra allocation cost per text generation call.
extern void load_text_shape(const char* text,
                            const char* font,
                            const float text_height,
                            const float thickness,
                            bool        is_bold,
                            bool        is_italic,
                            TextResult& text_result);

// [INTENT] Accessor for the populated font map (font_name → font_path) built by init_occt_fonts().
std::map<std::string, std::string> get_occt_fonts_maps();

}; // namespace Slic3r

#endif // slic3r_Text_Shape_hpp_
