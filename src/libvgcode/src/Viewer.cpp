///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#include "../include/Viewer.hpp"
#include "ViewerImpl.hpp"

namespace libvgcode {
// [INTENT] Facade for G-code visualization, delegating to ViewerImpl (PIMPL pattern)
// Provides the public API for accessing G-code preview rendering capabilities
// [STATE] Wraps ViewerImpl instance containing all visualization state, colors, settings
// [EVENT] Methods trigger state updates in underlying ViewerImpl that affect rendering
// [THREAD] GUI thread calls; ViewerImpl handles any internal worker thread synchronization
// [UNITY] Remove facade pattern; expose methods directly on MonoBehaviour component
// Store visualization state in ScriptableObject-backed ViewModel
// [PORTING_HAZARD:P2] PIMPL pattern overhead can be simplified by direct MonoBehaviour usage

Viewer::Viewer() { m_impl = new ViewerImpl(); }

Viewer::~Viewer() { delete m_impl; }

// [EVENT] Initialization flow: called once at component start, sets up GL resources and shader programs
// [OPENGL] Creates GL buffers, compiles shaders, initializes vertex arrays for gcode path visualization
// [UNITY] Use MonoBehaviour.Awake() or Start() for init; WebGL/glTF-based rendering pipeline
void Viewer::init(const std::string& opengl_context_version) { m_impl->init(opengl_context_version); }

// [EVENT] Shutdown flow: called when component is destroyed or application exits
// [OPENGL] Releases all GL resources (buffers, textures, shaders) to prevent memory leaks
// [UNITY] Use MonoBehaviour.OnDestroy(); Unity automatically manages resources with proper cleanup
void Viewer::shutdown() { m_impl->shutdown(); }

// [EVENT] Reset flow: clears current visualization state, prepares for new data load
// [STATE] Resets view ranges, selections, colors to defaults; maintains instance alive
// [UNITY] Clear Unity scene objects, reset ScriptableObject state, reinitialize view state
void Viewer::reset() { m_impl->reset(); }

// [EVENT] Data loading flow: parses G-code input, builds vertex buffer for visualization
// [STATE] Transient data mode; builds comprehensive path data structure with all metadata
// [THREAD] Potentially heavy operation; should run on worker thread with main thread marshaling
// [UNITY] UnityWebRequest + Job System; parse async, then marshal vertex data to main thread for rendering
// [PORTING_HAZARD:P1] GCodeInputData structure must be mapped to Unity-compatible data containers
void Viewer::load(GCodeInputData&& gcode_data) { m_impl->load(std::move(gcode_data)); }

// [OPENGL] Primary render loop: submits vertex buffers to GPU with view/projection matrices
// Uses instanced rendering for performance; handles layer filtering, visibility toggles
// [STATE] Reads current view state (visible layers, extrusion roles, option visibility) to filter rendering
// [UNITY] Use Unity's Camera.Render() with custom command buffer; MaterialPropertyBlock for dynamic colors
// [PORTING_HAZARD:P2] Mat4x4 must be converted to Matrix4x4; GL instancing needs Unity DrawMeshInstanced
void Viewer::render(const Mat4x4& view_matrix, const Mat4x4& projection_matrix) { m_impl->render(view_matrix, projection_matrix); }

EViewType Viewer::get_view_type() const { return m_impl->get_view_type(); }

void Viewer::set_view_type(EViewType type) { m_impl->set_view_type(type); }

ETimeMode Viewer::get_time_mode() const { return m_impl->get_time_mode(); }

void Viewer::set_time_mode(ETimeMode mode) { m_impl->set_time_mode(mode); }

bool Viewer::is_top_layer_only_view_range() const { return m_impl->is_top_layer_only_view_range(); }

void Viewer::toggle_top_layer_only_view_range() { m_impl->toggle_top_layer_only_view_range(); }

// [STATE] Visibility toggle states for visualization options (travels, wipes, etc.)
// [EVENT] Clicked checkbox in UI toggles this state
// [UNITY] Boolean toggle visualization feature; use LayerMask or GameObject active state
bool Viewer::is_option_visible(EOptionType type) const { return m_impl->is_option_visible(type); }

// [EVENT] Checkbox toggle in UI - toggles visibility of specific option type
// [PORTING_HAZARD:P3] L273-285 lines need mapping to Unity UI Toggle -> visibility state
void Viewer::toggle_option_visibility(EOptionType type) { m_impl->toggle_option_visibility(type); }

// [STATE] Visibility state for extrusion role categories
// [EVENT] Toggle in Extrusion Role visibility panel
// [UNITY] DAG-layer mask filtering; per-role GameObject layer toggling
bool Viewer::is_extrusion_role_visible(EGCodeExtrusionRole role) const { return m_impl->is_extrusion_role_visible(role); }

void Viewer::toggle_extrusion_role_visibility(EGCodeExtrusionRole role) { m_impl->toggle_extrusion_role_visibility(role); }

// [STATE] Color scheme for different extrusion roles (perimeters, infill, supports, etc.)
// [EVENT] Color picker changes in UI update this state
// [UNITY] Use Unity MaterialPropertyBlock or per-vertex colors; store colors in ScriptableObject theme
// Map EGCodeExtrusionRole enum to Data-Driven Color Palette SO
const Color& Viewer::get_extrusion_role_color(EGCodeExtrusionRole role) const { return m_impl->get_extrusion_role_color(role); }

// [EVENT] UI color picker callback - user customized color scheme
// [PORTING_HAZARD:P2] Color format conversion needed; wxWidgets Color likely needs conversion to Unity Color32/Color
void Viewer::set_extrusion_role_color(EGCodeExtrusionRole role, const Color& color) { m_impl->set_extrusion_role_color(role, color); }

// [EVENT] Reset button action - restore factory defaults
// [STATE] Resets internal color map to hardcoded defaults
// [UNITY] Reset ScriptableObject to default asset values or reload from Resources
void Viewer::reset_default_extrusion_roles_colors() { m_impl->reset_default_extrusion_roles_colors(); }

const Color& Viewer::get_option_color(EOptionType type) const { return m_impl->get_option_color(type); }

void Viewer::set_option_color(EOptionType type, const Color& color) { m_impl->set_option_color(type, color); }

void Viewer::reset_default_options_colors() { m_impl->reset_default_options_colors(); }

size_t Viewer::get_tool_colors_count() const { return m_impl->get_tool_colors_count(); }

const Palette& Viewer::get_tool_colors() const { return m_impl->get_tool_colors(); }

void Viewer::set_tool_colors(const Palette& colors) { m_impl->set_tool_colors(colors); }

size_t Viewer::get_color_print_colors_count() const { return m_impl->get_color_print_colors_count(); }

const Palette& Viewer::get_color_print_colors() const { return m_impl->get_color_print_colors(); }

void Viewer::set_color_print_colors(const Palette& colors) { m_impl->set_color_print_colors(colors); }

const ColorRange& Viewer::get_color_range(EViewType type) const { return m_impl->get_color_range(type); }

void Viewer::set_color_range_palette(EViewType type, const Palette& palette) { m_impl->set_color_range_palette(type, palette); }

float Viewer::get_travels_radius() const { return m_impl->get_travels_radius(); }

void Viewer::set_travels_radius(float radius) { m_impl->set_travels_radius(radius); }

float Viewer::get_wipes_radius() const { return m_impl->get_wipes_radius(); }

void Viewer::set_wipes_radius(float radius) { m_impl->set_wipes_radius(radius); }

// [STATE] Returns total layer count from loaded G-code data
// [UNITY] Map to array/list count; UI uses this for slider max value
size_t Viewer::get_layers_count() const { return m_impl->get_layers_count(); }

// [STATE] Current visible layer range (subset of full model)
// [EVENT] Updates on user slider interaction or programmatic view changes
// [UNITY] Two-float range stored in ScriptableObject; UI Slider controls this state
const Interval& Viewer::get_layers_view_range() const { return m_impl->get_layers_view_range(); }

// [EVENT] User interaction: layer range slider changed
// [STATE] Updates visible_layers state, triggers re-render with filtered vertices
// [UNITY] Called from UI Slider onValueChanged; triggers visual update in renderer
void Viewer::set_layers_view_range(const Interval& range) { m_impl->set_layers_view_range(range); }

// [EVENT] Programmatic layer selection
// Convenience method for [min, max] range
// [UNITY] Anti-aliased slider or direct input fields calling into state
void Viewer::set_layers_view_range(Interval::value_type min, Interval::value_type max) { m_impl->set_layers_view_range(min, max); }

const Interval& Viewer::get_view_visible_range() const { return m_impl->get_view_visible_range(); }

void Viewer::set_view_visible_range(Interval::value_type min, Interval::value_type max) { m_impl->set_view_visible_range(min, max); }

const Interval& Viewer::get_view_full_range() const { return m_impl->get_view_full_range(); }

const Interval& Viewer::get_view_enabled_range() const { return m_impl->get_view_enabled_range(); }

bool Viewer::is_spiral_vase_mode() const { return m_impl->is_spiral_vase_mode(); }

float Viewer::get_layer_z(size_t layer_id) const { return m_impl->get_layer_z(layer_id); }

std::vector<float> Viewer::get_layers_zs() const { return m_impl->get_layers_zs(); }

size_t Viewer::get_layer_id_at(float z) const { return m_impl->get_layer_id_at(z); }

size_t Viewer::get_used_extruders_count() const { return m_impl->get_used_extruders_count(); }

std::vector<uint8_t> Viewer::get_used_extruders_ids() const { return m_impl->get_used_extruders_ids(); }

std::vector<ETimeMode> Viewer::get_time_modes() const { return m_impl->get_time_modes(); }

size_t Viewer::get_vertices_count() const { return m_impl->get_vertices_count(); }

const PathVertex& Viewer::get_current_vertex() const { return m_impl->get_current_vertex(); }

size_t Viewer::get_current_vertex_id() const { return m_impl->get_current_vertex_id(); }

const PathVertex& Viewer::get_vertex_at(size_t id) const { return m_impl->get_vertex_at(id); }

float Viewer::get_estimated_time() const { return m_impl->get_estimated_time(); }

float Viewer::get_estimated_time_at(size_t id) const { return m_impl->get_estimated_time_at(id); }

Color Viewer::get_vertex_color(const PathVertex& vertex) const { return m_impl->get_vertex_color(vertex); }

size_t Viewer::get_extrusion_roles_count() const { return m_impl->get_extrusion_roles_count(); }

std::vector<EGCodeExtrusionRole> Viewer::get_extrusion_roles() const { return m_impl->get_extrusion_roles(); }

size_t Viewer::get_options_count() const { return m_impl->get_options_count(); }

const std::vector<EOptionType>& Viewer::get_options() const { return m_impl->get_options(); }

size_t Viewer::get_color_prints_count(uint8_t extruder_id) const { return m_impl->get_color_prints_count(extruder_id); }

std::vector<ColorPrint> Viewer::get_color_prints(uint8_t extruder_id) const { return m_impl->get_color_prints(extruder_id); }

float Viewer::get_extrusion_role_estimated_time(EGCodeExtrusionRole role) const { return m_impl->get_extrusion_role_estimated_time(role); }

float Viewer::get_travels_estimated_time() const { return m_impl->get_travels_estimated_time(); }

std::vector<float> Viewer::get_layers_estimated_times() const { return m_impl->get_layers_estimated_times(); }

// [STATE] Bounding box calculation for computing zoom/fit bounds
// [EVENT] Called when user clicks "Fit View" or after data load
// [OPENGL] Used to calculate camera orthographic/perspective scaling
// [UNITY] Compute Bounds using Unity's Mesh.bounds or manually; use for Camera.main.orthographicSize
AABox Viewer::get_bounding_box(const std::vector<EMoveType>& types) const { return m_impl->get_bounding_box(types); }

// [STATE] Sub-selection bounding box for extrusion roles only
// [EVENT] Use case: Fit view to current visible extrusion types (e.g., supports only)
// [UNITY] Similar to Fit View but filtered; recalculate bounds from subset of mesh
AABox Viewer::get_extrusion_bounding_box(const std::vector<EGCodeExtrusionRole>& roles) const
{
    return m_impl->get_extrusion_bounding_box(roles);
}

// [STATE] Diagnostic metric tracking CPU-side memory footprint (vertex buffers, metadata)
// [EVENT] Displayed in debug/performance panel
// [UNITY] Calculate manually or use Profiler; display in UI via ScriptableObject stats
size_t Viewer::get_used_cpu_memory() const { return m_impl->get_used_cpu_memory(); }

// [STATE] Diagnostic metric tracking GPU-side memory usage (textures, vertex buffers)
// [INTENT] Performance monitoring for large builds
// [UNITY] Use Profiler.GetAllocatedMemoryForGraphicsDriver(); expose to analytics system
size_t Viewer::get_used_gpu_memory() const { return m_impl->get_used_gpu_memory(); }

// [END OF FILE - T136] All methods documented with Unity mapping

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
Vec3 Viewer::get_cog_position() const { return m_impl->get_cog_marker_position(); }

float Viewer::get_cog_marker_scale_factor() const { return m_impl->get_cog_marker_scale_factor(); }

void Viewer::set_cog_marker_scale_factor(float factor) { m_impl->set_cog_marker_scale_factor(factor); }

const Vec3& Viewer::get_tool_marker_position() const { return m_impl->get_tool_marker_position(); }

float Viewer::get_tool_marker_offset_z() const { return m_impl->get_tool_marker_offset_z(); }

void Viewer::set_tool_marker_offset_z(float offset_z) { m_impl->set_tool_marker_offset_z(offset_z); }

float Viewer::get_tool_marker_scale_factor() const { return m_impl->get_tool_marker_scale_factor(); }

void Viewer::set_tool_marker_scale_factor(float factor) { m_impl->set_tool_marker_scale_factor(factor); }

const Color& Viewer::get_tool_marker_color() const { return m_impl->get_tool_marker_color(); }

void Viewer::set_tool_marker_color(const Color& color) { m_impl->set_tool_marker_color(color); }

float Viewer::get_tool_marker_alpha() const { return m_impl->get_tool_marker_alpha(); }

void Viewer::set_tool_marker_alpha(float alpha) { m_impl->set_tool_marker_alpha(alpha); }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

} // namespace libvgcode
