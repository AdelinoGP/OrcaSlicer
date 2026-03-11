#pragma once
namespace Slic3r {
class Model;

// [INTENT] Import an SVG drawing as one `ModelObject` whose volumes correspond to extruded top-level shapes.
// [STATE] On success, `model` is mutated in place and `message` may carry warnings / failures for the UI.
extern bool load_svg(const char* path, Model* model, std::string& message);

}; // namespace Slic3r
