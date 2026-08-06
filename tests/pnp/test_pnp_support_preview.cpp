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
}

TEST_CASE("support-preview mesh building", "[pnp][support_preview]")
{
    SECTION("layers become prisms spanning the gap to the previous layer")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(two_layer_doc());
        REQUIRE(parsed.ok);
        const TriangleMesh mesh = build_support_preview_mesh(parsed.doc, 0.2);
        REQUIRE_FALSE(mesh.empty());

        // Two stacked 0.2 mm layers of a 10x10 square: the solid must span
        // z 0..0.4 and x/y 0..10.
        const BoundingBoxf3 bb = mesh.bounding_box();
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
        const TriangleMesh mesh = build_support_preview_mesh(parsed.doc, 0.2);
        const BoundingBoxf3 bb  = mesh.bounding_box();
        REQUIRE_THAT(bb.min.z(), Catch::Matchers::WithinAbs(0.0, 1e-5));
        REQUIRE_THAT(bb.max.z(), Catch::Matchers::WithinAbs(1.2, 1e-5));
    }

    SECTION("a document with no support geometry yields an empty mesh")
    {
        const PnpSupportPreviewParse parsed = parse_support_preview(
            R"({"schema_version":"1.0.0","units":"mm","layer_count":5,"layers":[]})");
        REQUIRE(parsed.ok);
        REQUIRE(build_support_preview_mesh(parsed.doc, 0.2).empty());
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
        const TriangleMesh mesh = build_support_preview_mesh(parsed.doc, 0.2);
        REQUIRE_FALSE(mesh.empty());
        // A ring wall is generated for the hole as well as the contour, so the
        // hole cannot silently vanish into a solid slab.
        REQUIRE(mesh.facets_count() > 8);
    }
}
