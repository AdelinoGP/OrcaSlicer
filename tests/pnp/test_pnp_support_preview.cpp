// PNP fork: tests for the support-preview document parser and mesh builder
// (pnp handoff item 13).
//
// These cover the half of PnpSupportPreview that is free of GUI and process
// types. The runner that spawns pnp_cli is exercised by hand against the live
// binary; what is worth pinning here is the contract with pnp's JSON document,
// because that is what silently rots when the two repos move apart.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>

#include "slic3r/GUI/PnpSupportPreview.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

namespace {

// One 10x10 mm square of support on each of two 0.2 mm layers, shaped exactly
// like pnp_cli's real output (schema 1.0.0, mm, z at the layer top).
std::string two_layer_doc()
{
    return R"({
      "schema_version": "1.0.0",
      "units": "mm",
      "layer_count": 2,
      "skipped_intermediate_entries": 0,
      "layers": [
        {"layer_index": 0, "z_mm": 0.2, "support": [
          {"contour": [[0,0],[10,0],[10,10],[0,10]], "holes": []}
        ]},
        {"layer_index": 1, "z_mm": 0.4, "support": [
          {"contour": [[0,0],[10,0],[10,10],[0,10]], "holes": []}
        ]}
      ]
    })";
}

} // namespace

TEST_CASE("support-preview document parsing", "[pnp][support_preview]")
{
    SECTION("a well-formed document yields layers, z values and polygons")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(two_layer_doc());
        REQUIRE(parsed.ok);
        REQUIRE(parsed.error.empty());
        REQUIRE(parsed.doc.schema_version == "1.0.0");
        REQUIRE(parsed.doc.units == "mm");
        REQUIRE(parsed.doc.layers.size() == 2);
        REQUIRE(parsed.doc.expolygon_count() == 2);
        REQUIRE_THAT(parsed.doc.layers[1].z_mm, Catch::Matchers::WithinAbs(0.4, 1e-9));
        REQUIRE(parsed.doc.layers[0].support.front().contour.points.size() == 4);
    }

    SECTION("millimetres are converted to Orca's scaled coordinates")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(two_layer_doc());
        REQUIRE(parsed.ok);
        // 10 mm must land on scale_(10), not on 10 — getting this wrong puts
        // the overlay four orders of magnitude off and is invisible in a unit
        // test that only checks point counts.
        const BoundingBox bb = get_extents(parsed.doc.layers[0].support.front().contour);
        REQUIRE(bb.max.x() - bb.min.x() == coord_t(scale_(10.)));
    }

    SECTION("an unsupported schema major is refused, not guessed at")
    {
        std::string doc = two_layer_doc();
        doc.replace(doc.find("1.0.0"), 5, "2.0.0");
        const PnpSupportPreviewParse parsed = parse_support_preview(doc);
        REQUIRE_FALSE(parsed.ok);
        REQUIRE(parsed.error.find("schema major 2") != std::string::npos);
    }

    SECTION("a non-millimetre unit is refused rather than scaled wrong")
    {
        std::string doc = two_layer_doc();
        doc.replace(doc.find("\"mm\""), 4, "\"in\"");
        const PnpSupportPreviewParse parsed = parse_support_preview(doc);
        REQUIRE_FALSE(parsed.ok);
        REQUIRE(parsed.error.find("millimetres") != std::string::npos);
    }

    SECTION("malformed JSON fails cleanly instead of throwing")
    {
        REQUIRE_NOTHROW(parse_support_preview("{not json"));
        const PnpSupportPreviewParse parsed = parse_support_preview("{not json");
        REQUIRE_FALSE(parsed.ok);
        REQUIRE_FALSE(parsed.error.empty());
    }

    SECTION("an empty support list is a valid answer, not an error")
    {
        // pnp emits this when supports are disabled or nothing needs them;
        // the gizmo must be able to tell it apart from a failed run.
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.0.0","units":"mm","layer_count":5,"layers":[]})");
        REQUIRE(parsed.ok);
        REQUIRE(parsed.doc.expolygon_count() == 0);
    }

    SECTION("degenerate rings are dropped, not fed to the tesselator")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.0.0","units":"mm","layer_count":1,"layers":[
                 {"layer_index":0,"z_mm":0.2,"support":[
                   {"contour":[[0,0],[1,0]],"holes":[]}
                 ]}]})");
        REQUIRE(parsed.ok);
        REQUIRE(parsed.doc.expolygon_count() == 0);
    }

    SECTION("schema 1.1.0 support_body wins over the coarse support field")
    {
        // The 1.1.0 `support_body` field carries the actual support
        // structures; the 1.0.0 `support` field carries the model's own
        // cross-sections. The overlay must be built from support_body.
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.1.0","units":"mm","layer_count":1,"layers":[
                 {"layer_index":0,"z_mm":0.2,
                  "support":[{"contour":[[0,0],[100,0],[100,100],[0,100]],"holes":[]}],
                  "support_body":[{"contour":[[40,40],[60,40],[60,60],[40,60]],"holes":[]}]}
               ]})");
        REQUIRE(parsed.ok);
        REQUIRE(parsed.doc.layers.size() == 1);
        REQUIRE(parsed.doc.layers[0].support.size() == 1);
        const BoundingBox bb = get_extents(parsed.doc.layers[0].support.front().contour);
        // 20 mm body, not the 100 mm coarse outline.
        REQUIRE(bb.max.x() - bb.min.x() == coord_t(scale_(20.)));
    }

    SECTION("an empty support_body is authoritative, not a fallback trigger")
    {
        // A 1.1.0 document with an empty support_body means "no supports";
        // falling back to the coarse `support` field would re-show the model.
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.1.0","units":"mm","layer_count":1,"layers":[
                 {"layer_index":0,"z_mm":0.2,
                  "support":[{"contour":[[0,0],[100,0],[100,100],[0,100]],"holes":[]}],
                  "support_body":[]}
               ]})");
        REQUIRE(parsed.ok);
        REQUIRE(parsed.doc.expolygon_count() == 0);
    }

    SECTION("a 1.0.0 document without support_body falls back to support")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(two_layer_doc());
        REQUIRE(parsed.ok);
        REQUIRE(parsed.doc.layers[0].support.size() == 1);
        const BoundingBox bb = get_extents(parsed.doc.layers[0].support.front().contour);
        REQUIRE(bb.max.x() - bb.min.x() == coord_t(scale_(10.)));
    }

    SECTION("schema 1.2.0 support_interface lands in its own bucket")
    {
        // The interface band (where the support meets the model and the bed)
        // must not leak into the body bucket the overlay is built from.
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.2.0","units":"mm","layer_count":1,"layers":[
                 {"layer_index":0,"z_mm":0.2,
                  "support_body":[{"contour":[[0,0],[10,0],[10,10],[0,10]],"holes":[]}],
                  "support_interface":[{"contour":[[2,2],[8,2],[8,8],[2,8]],"holes":[]}]}
               ]})");
        REQUIRE(parsed.ok);
        REQUIRE(parsed.doc.layers.size() == 1);
        REQUIRE(parsed.doc.layers[0].support.size() == 1);
        REQUIRE(parsed.doc.layers[0].support_interface.size() == 1);
        const BoundingBox bb = get_extents(parsed.doc.layers[0].support_interface.front().contour);
        REQUIRE(bb.max.x() - bb.min.x() == coord_t(scale_(6.)));
        REQUIRE(parsed.doc.expolygon_count() == 2);
    }

    SECTION("a 1.1.0 document without support_interface carries an empty band")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.1.0","units":"mm","layer_count":1,"layers":[
                 {"layer_index":0,"z_mm":0.2,
                  "support_body":[{"contour":[[0,0],[10,0],[10,10],[0,10]],"holes":[]}]}
               ]})");
        REQUIRE(parsed.ok);
        REQUIRE(parsed.doc.layers[0].support.size() == 1);
        REQUIRE(parsed.doc.layers[0].support_interface.empty());
    }
}

TEST_CASE("support-preview mesh building", "[pnp][support_preview]")
{
    SECTION("layers become prisms spanning the gap to the previous layer")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(two_layer_doc());
        REQUIRE(parsed.ok);
        const PnpSupportPreviewMeshes meshes = build_support_preview_meshes(parsed.doc, 0.2);
        REQUIRE_FALSE(meshes.body.empty());

        // Two stacked 0.2 mm layers of a 10x10 square: the solid must span
        // z 0..0.4 and x/y 0..10.
        const BoundingBoxf3 bb = meshes.body.bounding_box();
        REQUIRE_THAT(bb.min.z(), Catch::Matchers::WithinAbs(0.0, 1e-5));
        REQUIRE_THAT(bb.max.z(), Catch::Matchers::WithinAbs(0.4, 1e-5));
        REQUIRE_THAT(bb.min.x(), Catch::Matchers::WithinAbs(0.0, 1e-5));
        REQUIRE_THAT(bb.max.x(), Catch::Matchers::WithinAbs(10.0, 1e-5));
    }

    SECTION("variable layer height is honoured, not assumed constant")
    {
        // Layer tops at 0.2 and 1.2: the second prism is 1.0 mm thick. Using a
        // fixed layer height here would leave a 0.8 mm gap in the overlay.
        std::string doc = two_layer_doc();
        doc.replace(doc.find("\"z_mm\": 0.4"), std::string("\"z_mm\": 0.4").size(), "\"z_mm\": 1.2");
        const PnpSupportPreviewParse parsed = parse_support_preview(doc);
        REQUIRE(parsed.ok);
        const PnpSupportPreviewMeshes meshes = build_support_preview_meshes(parsed.doc, 0.2);
        const BoundingBoxf3 bb  = meshes.body.bounding_box();
        REQUIRE_THAT(bb.min.z(), Catch::Matchers::WithinAbs(0.0, 1e-5));
        REQUIRE_THAT(bb.max.z(), Catch::Matchers::WithinAbs(1.2, 1e-5));
    }

    SECTION("a document with no support geometry yields an empty mesh")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.0.0","units":"mm","layer_count":5,"layers":[]})");
        REQUIRE(parsed.ok);
        REQUIRE(build_support_preview_meshes(parsed.doc, 0.2).empty());
    }

    SECTION("holes are carried through into the mesh")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.0.0","units":"mm","layer_count":1,"layers":[
                 {"layer_index":0,"z_mm":0.2,"support":[
                   {"contour":[[0,0],[10,0],[10,10],[0,10]],
                    "holes":[[[3,3],[3,7],[7,7],[7,3]]]}
                 ]}]})");
        REQUIRE(parsed.ok);
        REQUIRE(parsed.doc.layers[0].support.front().holes.size() == 1);
        const PnpSupportPreviewMeshes meshes = build_support_preview_meshes(parsed.doc, 0.2);
        REQUIRE_FALSE(meshes.body.empty());
        // A ring wall is generated for the hole as well as the contour, so the
        // hole cannot silently vanish into a solid slab.
        REQUIRE(meshes.body.facets_count() > 8);
    }

    SECTION("interface polygons build the interface mesh, not the body")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.2.0","units":"mm","layer_count":1,"layers":[
                 {"layer_index":0,"z_mm":0.2,
                  "support_body":[{"contour":[[0,0],[10,0],[10,10],[0,10]],"holes":[]}],
                  "support_interface":[{"contour":[[2,2],[8,2],[8,8],[2,8]],"holes":[]}]}
               ]})");
        REQUIRE(parsed.ok);
        const PnpSupportPreviewMeshes meshes = build_support_preview_meshes(parsed.doc, 0.2);
        REQUIRE_FALSE(meshes.body.empty());
        REQUIRE_FALSE(meshes.interface_mesh.empty());
        // The band is the 6x6 square, not the 10x10 body.
        const BoundingBoxf3 bb = meshes.interface_mesh.bounding_box();
        REQUIRE_THAT(bb.min.x(), Catch::Matchers::WithinAbs(2.0, 1e-5));
        REQUIRE_THAT(bb.max.x(), Catch::Matchers::WithinAbs(8.0, 1e-5));
    }

    SECTION("an interface-only document leaves the body mesh empty")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.2.0","units":"mm","layer_count":1,"layers":[
                 {"layer_index":0,"z_mm":0.2,
                  "support_interface":[{"contour":[[2,2],[8,2],[8,8],[2,8]],"holes":[]}]}
               ]})");
        REQUIRE(parsed.ok);
        const PnpSupportPreviewMeshes meshes = build_support_preview_meshes(parsed.doc, 0.2);
        REQUIRE(meshes.body.empty());
        REQUIRE_FALSE(meshes.interface_mesh.empty());
        REQUIRE_FALSE(meshes.empty());
    }
}

TEST_CASE("support-preview family mapping", "[pnp][support_preview]")
{
    SECTION("Orca's tree values map to the tree family")
    {
        REQUIRE(pnp_support_family_from_type("stTreeAuto") == PnpSupportFamily::Tree);
        REQUIRE(pnp_support_family_from_type("stTree") == PnpSupportFamily::Tree);
    }

    SECTION("normal values map to the traditional family")
    {
        REQUIRE(pnp_support_family_from_type("stNormalAuto") == PnpSupportFamily::Traditional);
        REQUIRE(pnp_support_family_from_type("stNormal") == PnpSupportFamily::Traditional);
    }

    SECTION("an absent or unknown value defaults to traditional, like pnp")
    {
        // pnp's canonical_support_family: tree*/hybrid* -> tree, everything
        // else -> traditional. The fork must not invent a third family.
        REQUIRE(pnp_support_family_from_type("") == PnpSupportFamily::Traditional);
        REQUIRE(pnp_support_family_from_type("hybrid") == PnpSupportFamily::Tree);
        REQUIRE(pnp_support_family_from_type("bogus") == PnpSupportFamily::Traditional);
    }
}
