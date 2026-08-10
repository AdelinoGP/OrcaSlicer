// PNP fork: support-preview overlay for the FDM-supports gizmo.
//
// F13 deleted PrintObject::generate_support_preview() along with the native
// support pipeline and left GLGizmoFdmSupports paint-only, noting that
// restoring the overlay via a CLI geometry query was "pnp handoff item 13".
// pnp_cli grew that query (`support-preview`), so this is the fork half:
// parse its JSON document and turn the per-layer 2D slices back into a mesh
// the gizmo can render.
//
// The parse/mesh half is deliberately free of GUI and process types so it can
// be unit-tested from a JSON literal; PnpSupportPreview.cpp holds the runner
// that actually spawns pnp_cli.

#ifndef slic3r_GUI_PnpSupportPreview_hpp_
#define slic3r_GUI_PnpSupportPreview_hpp_

#include <atomic>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r {
class DynamicPrintConfig;
}

namespace Slic3r { namespace GUI {

// pnp_cli's support-preview document version this build understands. The
// document carries a semver string; we gate on the major only, matching the
// config-schema and progress-event gates on PnpBackend.
static constexpr int PNP_SUPPORT_PREVIEW_SCHEMA_MAJOR = 1;

struct PnpSupportPreviewLayer
{
	int        layer_index = 0;
	// Top of the layer, in millimetres, as reported by pnp.
	double     z_mm        = 0.;
	ExPolygons support;
};

struct PnpSupportPreviewDoc
{
	std::string                         schema_version;
	std::string                         units;
	int                                 layer_count = 0;
	std::vector<PnpSupportPreviewLayer> layers;

	// Total expolygons across every layer; 0 means "no supports here", which
	// is a legitimate result (supports disabled, or nothing needs them).
	size_t expolygon_count() const;
};

struct PnpSupportPreviewParse
{
	bool                 ok = false;
	// Empty when ok; a human-readable reason otherwise.
	std::string          error;
	PnpSupportPreviewDoc doc;
};

// Parse a pnp_cli support-preview document. Rejects a schema major this build
// does not know, a non-millimetre unit, and malformed JSON; tolerates missing
// optional fields so a future minor can add them.
PnpSupportPreviewParse parse_support_preview(const std::string& json_text);

// Extrude each layer's expolygons into a prism spanning that layer's
// thickness, and weld the layers into one mesh.
//
// Layer thickness is taken from the gap to the previous layer's z, so variable
// layer height comes out right; the first layer falls back to its own z, and a
// non-positive gap falls back to `fallback_layer_height_mm` (a layer list that
// is not strictly ascending would otherwise produce inverted prisms).
TriangleMesh build_support_preview_mesh(const PnpSupportPreviewDoc& doc,
                                        double                      fallback_layer_height_mm);

struct PnpSupportPreviewRun
{
	bool         ok = false;
	std::string  error;
	TriangleMesh mesh;
	// Diagnostics for the log; also what the gizmo reports when the run
	// succeeds but yields nothing to draw.
	size_t layer_count     = 0;
	size_t expolygon_count = 0;
};

// Run `pnp_cli support-preview` over an already-exported 3MF (the translated
// config rides in the 3MF's project_settings.config sidecar — no separate
// config file), then parse and build the mesh. Blocking: call it from a
// worker thread.
//
// `cancel` is polled while waiting; when it turns true the child is terminated
// and the result comes back not-ok with an empty error (a cancel is not a
// failure and must not raise a notification).
PnpSupportPreviewRun run_support_preview(const boost::filesystem::path& model_3mf,
                                         double                         fallback_layer_height_mm,
                                         const std::atomic<bool>&       cancel);

} } // namespace Slic3r::GUI

#endif // slic3r_GUI_PnpSupportPreview_hpp_
