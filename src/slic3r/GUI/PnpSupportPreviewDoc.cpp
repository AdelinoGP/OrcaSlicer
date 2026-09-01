// PNP fork: support-preview document parsing and mesh building (see header).
//
// Deliberately free of GUI, process and backend types so tests/pnp can compile
// it into a GUI-free binary; the runner that spawns pnp_cli lives in
// PnpSupportPreview.cpp.

#include "PnpSupportPreview.hpp"

#include "libslic3r/Tesselate.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Slic3r { namespace GUI {

size_t PnpSupportPreviewDoc::expolygon_count() const
{
	size_t count = 0;
	for (const PnpSupportPreviewLayer &layer : this->layers) {
		count += layer.support.size();
		count += layer.support_interface.size();
	}
	return count;
}

namespace {

// Read the leading integer of a semver string. Returns -1 when absent or
// unparseable, which the caller reports as an incompatible document rather
// than silently accepting.
int semver_major(const std::string &version)
{
	size_t i = 0;
	while (i < version.size() && version[i] >= '0' && version[i] <= '9')
		++i;
	if (i == 0)
		return -1;
	try {
		return std::stoi(version.substr(0, i));
	} catch (const std::exception &) {
		return -1;
	}
}

// pnp emits rings as [[x, y], ...] in millimetres; Orca polygons are in
// scaled integer coordinates.
bool ring_from_json(const json &node, Polygon &out)
{
	if (!node.is_array())
		return false;
	out.points.clear();
	out.points.reserve(node.size());
	for (const json &pt : node) {
		if (!pt.is_array() || pt.size() < 2 || !pt[0].is_number() || !pt[1].is_number())
			return false;
		out.points.emplace_back(scale_(pt[0].get<double>()), scale_(pt[1].get<double>()));
	}
	// A ring with fewer than three points encloses no area; drop it rather
	// than feed a degenerate contour to the tesselator.
	return out.points.size() >= 3;
}

// Parse a wire polygon array (contour + holes, millimetres) into Orca
// expolygons. Malformed entries are dropped, never fatal.
void expolygons_from_json(const json &node, ExPolygons &out)
{
	if (!node.is_array())
		return;
	for (const json &poly_node : node) {
		if (!poly_node.is_object())
			continue;
		ExPolygon ex;
		const auto contour_it = poly_node.find("contour");
		if (contour_it == poly_node.end() || !ring_from_json(*contour_it, ex.contour))
			continue;
		if (const auto holes_it = poly_node.find("holes");
		    holes_it != poly_node.end() && holes_it->is_array()) {
			for (const json &hole_node : *holes_it) {
				Polygon hole;
				if (ring_from_json(hole_node, hole))
					ex.holes.push_back(std::move(hole));
			}
		}
		// The tesselator relies on Orca's orientation convention;
		// pnp makes no promise about ring winding.
		ex.contour.make_counter_clockwise();
		for (Polygon &hole : ex.holes)
			hole.make_clockwise();
		out.push_back(std::move(ex));
	}
}

// Weld one layer's expolygons into a prism spanning `z_bottom..z_top` and
// append it to `its`. Shared by the body and interface buckets.
void append_layer_prism(indexed_triangle_set &its, const ExPolygons &expolygons,
                        double z_bottom, double z_top)
{
	if (expolygons.empty())
		return;

	// Caps: the tesselator returns loose triangles as vertex triples.
	const std::vector<Vec3d> top    = triangulate_expolygons_3d(expolygons, z_top, false);
	const std::vector<Vec3d> bottom = triangulate_expolygons_3d(expolygons, z_bottom, true);
	for (const std::vector<Vec3d> *cap : {&top, &bottom}) {
		for (size_t v = 0; v + 2 < cap->size(); v += 3) {
			const int base = int(its.vertices.size());
			its.vertices.emplace_back((*cap)[v].cast<float>());
			its.vertices.emplace_back((*cap)[v + 1].cast<float>());
			its.vertices.emplace_back((*cap)[v + 2].cast<float>());
			its.indices.emplace_back(base, base + 1, base + 2);
		}
	}

	// Side walls: one quad per ring edge, split into two triangles.
	for (const ExPolygon &ex : expolygons) {
		std::vector<const Polygon *> rings { &ex.contour };
		for (const Polygon &hole : ex.holes)
			rings.push_back(&hole);
		for (const Polygon *ring : rings) {
			const Points &pts = ring->points;
			for (size_t p = 0; p < pts.size(); ++p) {
				const Point &a = pts[p];
				const Point &b = pts[(p + 1) % pts.size()];
				const auto   ax = float(unscale<double>(a.x()));
				const auto   ay = float(unscale<double>(a.y()));
				const auto   bx = float(unscale<double>(b.x()));
				const auto   by = float(unscale<double>(b.y()));
				const int    base = int(its.vertices.size());
				its.vertices.emplace_back(ax, ay, float(z_bottom));
				its.vertices.emplace_back(bx, by, float(z_bottom));
				its.vertices.emplace_back(bx, by, float(z_top));
				its.vertices.emplace_back(ax, ay, float(z_top));
				its.indices.emplace_back(base, base + 1, base + 2);
				its.indices.emplace_back(base, base + 2, base + 3);
			}
		}
	}
}

} // anonymous namespace

PnpSupportPreviewParse parse_support_preview(const std::string &json_text)
{
	PnpSupportPreviewParse result;

	json root = json::parse(json_text, nullptr, false);
	if (root.is_discarded()) {
		result.error = "support-preview output is not valid JSON";
		return result;
	}
	if (!root.is_object()) {
		result.error = "support-preview output is not a JSON object";
		return result;
	}

	result.doc.schema_version = root.value("schema_version", std::string());
	const int major           = semver_major(result.doc.schema_version);
	if (major < 0) {
		result.error = "support-preview document has no readable schema_version";
		return result;
	}
	if (major != PNP_SUPPORT_PREVIEW_SCHEMA_MAJOR) {
		result.error = "support-preview schema major " + std::to_string(major)
		             + " is not supported by this build (expected "
		             + std::to_string(PNP_SUPPORT_PREVIEW_SCHEMA_MAJOR) + ")";
		return result;
	}

	// The geometry is consumed as millimetres; a future unit change must not
	// be read as mm and silently scaled wrong.
	result.doc.units = root.value("units", std::string("mm"));
	if (result.doc.units != "mm") {
		result.error = "support-preview units \"" + result.doc.units + "\" are not millimetres";
		return result;
	}

	result.doc.layer_count = root.value("layer_count", 0);

	if (const auto it = root.find("layers"); it != root.end() && it->is_array()) {
		result.doc.layers.reserve(it->size());
		for (const json &layer_node : *it) {
			if (!layer_node.is_object())
				continue;
			PnpSupportPreviewLayer layer;
			layer.layer_index = layer_node.value("layer_index", 0);
			layer.z_mm        = layer_node.value("z_mm", 0.);

			// Schema 1.1.0 adds `support_body` — the actual support
			// structures (SupportPlanIR SupportBody role regions). Prefer it
			// over the 1.0.0 `support` field, which carries the model's own
			// cross-sections at support layers and renders as a green copy
			// of the model. An empty `support_body` is authoritative (no
			// supports); only a document without the field falls back.
			const json* support_src = nullptr;
			if (const auto body_it = layer_node.find("support_body");
			    body_it != layer_node.end() && body_it->is_array())
				support_src = &*body_it;
			else if (const auto support_it = layer_node.find("support");
			         support_it != layer_node.end() && support_it->is_array())
				support_src = &*support_it;
			if (support_src != nullptr)
				expolygons_from_json(*support_src, layer.support);

			// Schema 1.2.0 adds `support_interface` — the interface role
			// regions (where the support meets the model and the bed),
			// rendered as a distinct band. Absent in older documents.
			if (const auto interface_it = layer_node.find("support_interface");
			    interface_it != layer_node.end())
				expolygons_from_json(*interface_it, layer.support_interface);

			result.doc.layers.push_back(std::move(layer));
		}
	}

	result.ok = true;
	return result;
}

PnpSupportPreviewMeshes build_support_preview_meshes(const PnpSupportPreviewDoc &doc,
                                                     double fallback_layer_height_mm)
{
	if (fallback_layer_height_mm <= 0.)
		fallback_layer_height_mm = 0.2;

	indexed_triangle_set body_its;
	indexed_triangle_set interface_its;
	double               previous_z = 0.;

	for (size_t i = 0; i < doc.layers.size(); ++i) {
		const PnpSupportPreviewLayer &layer = doc.layers[i];
		// Thickness from the gap to the previous layer, so variable layer
		// height comes out right. A non-ascending z (which pnp should never
		// emit, but we do not have to trust) falls back to the nominal height.
		double thickness = (i == 0) ? layer.z_mm : layer.z_mm - previous_z;
		if (thickness <= 0.)
			thickness = fallback_layer_height_mm;
		previous_z = layer.z_mm;

		if (layer.support.empty() && layer.support_interface.empty())
			continue;

		const double z_top    = layer.z_mm;
		const double z_bottom = layer.z_mm - thickness;

		append_layer_prism(body_its, layer.support, z_bottom, z_top);
		append_layer_prism(interface_its, layer.support_interface, z_bottom, z_top);
	}

	PnpSupportPreviewMeshes meshes;
	meshes.body           = TriangleMesh(std::move(body_its));
	meshes.interface_mesh = TriangleMesh(std::move(interface_its));
	return meshes;
}

PnpSupportFamily pnp_support_family_from_type(const std::string &support_type)
{
	// Mirror pnp's `canonical_support_family` (crates/slicer-ir/src/slice_ir.rs):
	// `tree*`/`hybrid*` -> tree, everything else -> traditional. Orca's four
	// enum values (stNormalAuto/stTreeAuto/stNormal/stTree) all land on the
	// intended family.
	if (support_type.rfind("stTree", 0) == 0 || support_type.rfind("hybrid", 0) == 0)
		return PnpSupportFamily::Tree;
	return PnpSupportFamily::Traditional;
}

} } // namespace Slic3r::GUI
