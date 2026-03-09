// [INTENT] Bridge between OrcaSlicer's TriangleMesh world and OpenCASCADE (OCCT) text/font
//          rendering. Converts a UTF-8 text string + font name into a 3D TriangleMesh by:
//          1. Using OCCT Font_BRepFont + Font_BRepTextBuilder to produce B-Rep 2D glyph profiles
//          2. Extruding them with BRepPrimAPI_MakePrism to a solid
//          3. Tessellating via BRepMesh_IncrementalMesh into stl_file then TriangleMesh
// [COUPLING] Depends on: OCCT (Font_*, BRep*, TopoDS_*), admesh (stl_file / stl_allocate),
//            Slic3r::TriangleMesh, Slic3r::resources_dir(), boost::log
// [STATE] Module-level mutable state: g_occt_fonts_maps (static std::map). Written once by
//         init_occt_fonts() at startup from main thread; read by load_text_shape() on job threads.
//         No synchronisation — safe only because writes precede all reads in practice.

#include "../libslic3r.h"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"

#include "TextShape.hpp"

#include <string>
#include <vector>

#include "Standard_TypeDef.hxx"
#include "STEPCAFControl_Reader.hxx"
#include "BRepMesh_IncrementalMesh.hxx"
#include "Interface_Static.hxx"
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include "XCAFApp_Application.hxx"
#include "TopoDS_Solid.hxx"
#include "TopoDS_Compound.hxx"
#include "TopoDS_Builder.hxx"
#include "TopoDS.hxx"
#include "TDataStd_Name.hxx"
#include "BRepBuilderAPI_Transform.hxx"
#include "TopExp_Explorer.hxx"
#include "TopExp_Explorer.hxx"
#include "BRep_Tool.hxx"
#include "Font_BRepFont.hxx"
#include "Font_BRepTextBuilder.hxx"
#include "BRepPrimAPI_MakePrism.hxx"
#include "Font_FontMgr.hxx"

#include <boost/log/trivial.hpp>

namespace Slic3r {

// [H1034 P1/High] g_occt_fonts_maps: module-level mutable global map<font_name, font_path>.
// Written by init_occt_fonts() (called once from main thread at startup) and read by
// load_text_shape() (called from GUI/job threads). No mutex. Safe only because the single write
// fully completes before any reads; if the call order ever changes this becomes a data race.
// Refactoring target: pass an immutable snapshot by value to each job, or protect with std::shared_mutex.
static std::map<std::string, std::string> g_occt_fonts_maps; // map<font_name, font_path>

// [H1037 P2/Medium] fonts_suffix: 30-element exclusion list applied via SearchFromEnd().
// Any font whose name *ends* with one of these strings is silently dropped from the font list.
// "Ultra", "Extra", "Extended", and digit suffixes ("1"–"9") can match legitimate base names
// (e.g., a font literally named "Ultra" or "Song1") causing it to be permanently hidden.
// "ExtraBold" appears twice — the duplicate is harmless but indicates copy-paste origin.
static const std::vector<Standard_CString> fonts_suffix{"Bold",      "Medium",     "Heavy",      "Italic",     "Oblique",   "Inclined",
                                                        "Light",     "Thin",       "Semibold",   "ExtraBold",  "ExtraBold", "Semilight",
                                                        "SemiLight", "ExtraLight", "Extralight", "Ultralight", "Condensed", "Ultra",
                                                        "Extra",     "Expanded",   "Extended",   "1",          "2",         "3",
                                                        "4",         "5",          "6",          "7",          "8",         "9",
                                                        "Al Tarikh"};

// [INTENT] Accessor for the cached font map; used by GUI code to present font choices
//          that correspond to what OCCT can actually render.
std::map<std::string, std::string> get_occt_fonts_maps() { return g_occt_fonts_maps; }

// [INTENT] Populate g_occt_fonts_maps by querying OCCT's Font_FontMgr for all available
//          system fonts, filtering out emoji, variant-weight faces, and non-TTF/OTF/TTC formats.
//          Returns an ordered list of displayable font names.
// [STATE] Writes g_occt_fonts_maps — must be called before any job thread reads it.
// [CONCURRENCY] Not thread-safe (writes global map without lock). Designed for single call
//               from main thread at app startup.
std::vector<std::string> init_occt_fonts()
{
    std::vector<std::string> stdFontNames;

    Handle(Font_FontMgr) aFontMgr = Font_FontMgr::GetInstance();
    aFontMgr->InitFontDataBase();

    TColStd_SequenceOfHAsciiString availFontNames;
    aFontMgr->GetAvailableFontsNames(availFontNames);
    stdFontNames.reserve(availFontNames.Size());

    g_occt_fonts_maps.clear();

    BOOST_LOG_TRIVIAL(info) << "init_occt_fonts start";
#ifdef __APPLE__
    // [INTENT] Inject bundled HarmonyOS Sans SC font for macOS before querying system fonts,
    //          so it always appears in the list regardless of what the OS has installed.
    stdFontNames.push_back("HarmonyOS Sans SC");
    // [H1038 P2/Medium] Typo in map key: "HarmoneyOS Sans SC" (extra 'e') vs the display name
    // "HarmonyOS Sans SC" pushed above. Any lookup by the correct name "HarmonyOS Sans SC"
    // will miss this entry. The font path is correctly resolved; only the map key is wrong.
    // Fix: change key to "HarmonyOS Sans SC" to match the display name.
    g_occt_fonts_maps.insert(std::make_pair("HarmoneyOS Sans SC", Slic3r::resources_dir() + "/fonts/" + "HarmonyOS_Sans_SC_Regular.ttf"));
#endif
    for (auto afn : availFontNames) {
#ifdef __APPLE__
        // Skip hidden macOS system fonts (names starting with '.')
        if (afn->String().StartsWith("."))
            continue;
#endif
        // Skip emoji fonts — they cause rendering issues in the B-Rep pipeline
        if (afn->Search("Emoji") != -1 || afn->Search("emoji") != -1)
            continue;

        // [H1037 P2/Medium] SearchFromEnd checks if the font name *ends* with any suffix.
        // This filters variant-weight faces (e.g. "Roboto Bold") but also silently excludes
        // any legitimately named font ending with a digit or the word "Ultra", "Extra", etc.
        bool repeat = false;
        for (size_t i = 0; i < fonts_suffix.size(); i++) {
            if (afn->SearchFromEnd(fonts_suffix[i]) != -1) {
                repeat = true;
                break;
            }
        }
        if (repeat)
            continue;

        Handle(Font_SystemFont) sys_font  = aFontMgr->GetFont(afn->ToCString());
        TCollection_AsciiString font_path = sys_font->FontPath(Font_FontAspect::Font_FontAspect_Regular);
        if (!font_path.IsEmpty() && font_path.SearchFromEnd(".") != -1) {
            auto file_type = font_path.SubString(font_path.SearchFromEnd(".") + 1, font_path.Length());
            file_type.LowerCase();
            if (file_type == "ttf" || file_type == "otf" || file_type == "ttc") {
                g_occt_fonts_maps.insert(std::make_pair(afn->ToCString(), decode_path(font_path.ToCString())));
            }
        }
    }
    BOOST_LOG_TRIVIAL(info) << "init_occt_fonts end";
    // in order
    for (auto occt_font : g_occt_fonts_maps) {
        stdFontNames.push_back(occt_font.first);
    }
    return stdFontNames;
}

// [INTENT] Convert UTF-8 text + font name + height to an OCCT B-Rep compound shape (2D profile).
//          Also computes total advance width of the string using per-glyph AdvanceX queries.
// [COUPLING] Directly drives Font_BRepFont + Font_BRepTextBuilder + Font_TextFormatter from OCCT.
//            Output TopoDS_Shape is a wire/face compound; caller must extrude it.
// [MEMORY] All OCCT objects are stack/Handle-managed; no explicit heap allocation here.
static bool TextToBRep(const char*      text,
                       const char*      font,
                       const float      theTextHeight,
                       Font_FontAspect& theFontAspect,
                       TopoDS_Shape&    theShape,
                       double&          text_width)
{
    Standard_Integer anArgIt = 1;
    Standard_CString aName   = "text_shape";
    Standard_CString aText   = text;

    Font_BRepFont aFont;
    // TCollection_AsciiString aFontName("Courier");
    TCollection_AsciiString aFontName(font);
    Standard_Real           aTextHeight        = theTextHeight;
    Font_FontAspect         aFontAspect        = theFontAspect;
    Standard_Boolean        anIsCompositeCurve = Standard_False;
    gp_Ax3                  aPenAx3(gp::XOY());
    gp_Dir                  aNormal(0.0, 0.0, 1.0);
    gp_Dir                  aDirection(1.0, 0.0, 0.0);
    gp_Pnt                  aPenLoc;

    Graphic3d_HorizontalTextAlignment aHJustification = Graphic3d_HTA_LEFT;
    Graphic3d_VerticalTextAlignment   aVJustification = Graphic3d_VTA_BOTTOM;
    Font_StrictLevel                  aStrictLevel    = Font_StrictLevel_Any;

    aFont.SetCompositeCurveMode(anIsCompositeCurve);
    if (!aFont.FindAndInit(aFontName.ToCString(), aFontAspect, aTextHeight, aStrictLevel))
        return false;

    aPenAx3 = gp_Ax3(aPenLoc, aNormal, aDirection);

    Handle(Font_TextFormatter) aFormatter = new Font_TextFormatter();
    aFormatter->Reset();
    aFormatter->SetupAlignment(aHJustification, aVJustification);
    aFormatter->Append(aText, *aFont.FTFont());
    aFormatter->Format();

    // [INTENT] Compute text width by summing per-glyph advance values (includes kerning pairs).
    text_width                  = 0;
    NCollection_String coll_str = aText;
    for (NCollection_Utf8Iter anIter = coll_str.Iterator(); *anIter != 0;) {
        const Standard_Utf32Char aCharThis = *anIter;
        const Standard_Utf32Char aCharNext = *++anIter;
        double                   width     = aFont.AdvanceX(aCharThis, aCharNext);
        text_width += width;
    }

    Font_BRepTextBuilder aBuilder;
    theShape = aBuilder.Perform(aFont, aFormatter, aPenAx3);
    return true;
}

// [INTENT] Extrude a 2D B-Rep base shape into a 3D solid by sweeping along +Z by `thickness`.
// [H1035 P1/High] MEMORY LEAK: `BRepPrimAPI_MakePrism` is allocated with `new` and stored in a
//   raw pointer named `Prism`. The extruded shape is extracted via `Prism->Shape()` into the
//   output `theSolid`, but `delete Prism` is never called. Every call to load_text_shape() leaks
//   one BRepPrimAPI_MakePrism heap allocation (~KB range). Over repeated emboss operations this
//   accumulates. Fix: use a stack-allocated or unique_ptr-managed MakePrism, or call delete.
static bool Prism(const TopoDS_Shape& theBase, const float thickness, TopoDS_Shape& theSolid)
{
    if (theBase.IsNull())
        return false;

    gp_Vec                 V(0.f, 0.f, thickness);
    BRepPrimAPI_MakePrism* Prism = new BRepPrimAPI_MakePrism(theBase, V, Standard_False);

    theSolid = Prism->Shape();
    return true;
}

// [INTENT] Tessellate an OCCT solid into a Slic3r TriangleMesh via an admesh stl_file intermediate.
//          Traverses all TopAbs_FACE surfaces, accumulates nodes and triangles, respects face
//          orientation (swaps winding for reversed faces), then calls TriangleMesh::from_stl().
// [H1036 P2/Medium] TWO-COPY PATH: OCCT triangulation → intermediate stl_file (C struct, heap
//   allocated by stl_allocate) → TriangleMesh::from_stl() (another copy). A direct push into
//   indexed_triangle_set vectors would avoid the admesh intermediate and halve peak memory.
// [MEMORY] stl_allocate() uses malloc(); from_stl() presumably takes ownership or copies again.
//          The local `stl` is stack-declared but its internal arrays are C-heap allocated —
//          they must be freed. Verify that from_stl() calls stl_close() or equivalent.
// [COUPLING] Admesh C API (stl_file, stl_allocate, stl_facet, stl_calculate_normal,
//            stl_normalize_vector), OCCT BRepMesh + TopExp_Explorer + BRep_Tool, Slic3r TriangleMesh.
static void MakeMesh(TopoDS_Shape& theSolid, TriangleMesh& theMesh)
{
    const double STEP_TRANS_CHORD_ERROR = 0.005;
    const double STEP_TRANS_ANGLE_RES   = 1;

    // [INTENT] Tessellate solid to triangles with chord error 0.005 mm and angular resolution 1°.
    BRepMesh_IncrementalMesh mesh(theSolid, STEP_TRANS_CHORD_ERROR, false, STEP_TRANS_ANGLE_RES, true);
    int                      aNbNodes     = 0;
    int                      aNbTriangles = 0;
    for (TopExp_Explorer anExpSF(theSolid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
        TopLoc_Location aLoc;
        Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(anExpSF.Current()), aLoc);
        if (!aTriangulation.IsNull()) {
            aNbNodes += aTriangulation->NbNodes();
            aNbTriangles += aTriangulation->NbTriangles();
        }
    }

    stl_file stl;
    stl.stats.type                = inmemory;
    stl.stats.number_of_facets    = (uint32_t) aNbTriangles;
    stl.stats.original_num_facets = stl.stats.number_of_facets;
    stl_allocate(&stl);

    std::vector<Vec3f> points;
    points.reserve(aNbNodes);
    // BBS: count faces missing triangulation
    Standard_Integer aNbFacesNoTri = 0;
    // BBS: fill temporary triangulation
    Standard_Integer aNodeOffset    = 0;
    Standard_Integer aTriangleOffet = 0;
    for (TopExp_Explorer anExpSF(theSolid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
        const TopoDS_Shape& aFace = anExpSF.Current();
        TopLoc_Location     aLoc;
        Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(aFace), aLoc);
        if (aTriangulation.IsNull()) {
            ++aNbFacesNoTri;
            continue;
        }
        // BBS: copy nodes
        gp_Trsf aTrsf = aLoc.Transformation();
        for (Standard_Integer aNodeIter = 1; aNodeIter <= aTriangulation->NbNodes(); ++aNodeIter) {
            gp_Pnt aPnt = aTriangulation->Node(aNodeIter);
            aPnt.Transform(aTrsf);
            points.emplace_back(std::move(Vec3f(aPnt.X(), aPnt.Y(), aPnt.Z())));
        }
        // BBS: copy triangles
        const TopAbs_Orientation anOrientation = anExpSF.Current().Orientation();
        for (Standard_Integer aTriIter = 1; aTriIter <= aTriangulation->NbTriangles(); ++aTriIter) {
            Poly_Triangle aTri = aTriangulation->Triangle(aTriIter);

            Standard_Integer anId[3];
            aTri.Get(anId[0], anId[1], anId[2]);
            if (anOrientation == TopAbs_REVERSED) {
                // BBS: swap 1, 2.
                Standard_Integer aTmpIdx = anId[1];
                anId[1]                  = anId[2];
                anId[2]                  = aTmpIdx;
            }
            // BBS: Update nodes according to the offset.
            anId[0] += aNodeOffset;
            anId[1] += aNodeOffset;
            anId[2] += aNodeOffset;
            // BBS: save triangles facets
            stl_facet facet;
            facet.vertex[0] = points[anId[0] - 1].cast<float>();
            facet.vertex[1] = points[anId[1] - 1].cast<float>();
            facet.vertex[2] = points[anId[2] - 1].cast<float>();
            facet.extra[0]  = 0;
            facet.extra[1]  = 0;
            stl_normal normal;
            stl_calculate_normal(normal, &facet);
            stl_normalize_vector(normal);
            facet.normal                                   = normal;
            stl.facet_start[aTriangleOffet + aTriIter - 1] = facet;
        }

        aNodeOffset += aTriangulation->NbNodes();
        aTriangleOffet += aTriangulation->NbTriangles();
    }

    theMesh.from_stl(stl);
}

// [INTENT] Public entry point: given text + font metadata, produce a TextResult containing
//          text_width (mm) and text_mesh (3D TriangleMesh). Called from GUI/job context.
// [STATE] Reads g_occt_fonts_maps implicitly (OCCT queries system font manager for the given name).
// [HAZARD] H1035 (leak in Prism()), H1036 (double-copy in MakeMesh()), H1037 (font name exclusions),
//          H1038 (typo in HarmonyOS map key) all flow through this call path.
void load_text_shape(const char* text,
                     const char* font,
                     const float text_height,
                     const float thickness,
                     bool        is_bold,
                     bool        is_italic,
                     TextResult& text_result)
{
    if (thickness <= 0)
        return;

    Handle(Font_FontMgr) aFontMgr = Font_FontMgr::GetInstance();
    if (aFontMgr->GetAvailableFonts().IsEmpty())
        aFontMgr->InitFontDataBase();

    TopoDS_Shape    aTextBase;
    Font_FontAspect aFontAspect = Font_FontAspect_UNDEFINED;
    if (is_bold && is_italic)
        aFontAspect = Font_FontAspect_BoldItalic;
    else if (is_bold)
        aFontAspect = Font_FontAspect_Bold;
    else if (is_italic)
        aFontAspect = Font_FontAspect_Italic;
    else
        aFontAspect = Font_FontAspect_Regular;

    if (!TextToBRep(text, font, text_height, aFontAspect, aTextBase, text_result.text_width))
        return;

    TopoDS_Shape aTextShape;
    if (!Prism(aTextBase, thickness, aTextShape)) // [H1035] leaks MakePrism object on every call
        return;

    MakeMesh(aTextShape, text_result.text_mesh); // [H1036] two-copy tessellation path
}

}; // namespace Slic3r
