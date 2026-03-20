#ifndef __part_plate_hpp_
#define __part_plate_hpp_

#include <vector>
#include <set>
#include <array>
#include <thread>
#include <mutex>

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Arrange.hpp"
#include "Plater.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "GLCanvas3D.hpp"
#include "GLTexture.hpp"
#include "3DScene.hpp"
#include "GLModel.hpp"
#include "3DBed.hpp"
#include "MeshUtils.hpp"
#include "libslic3r/ParameterUtils.hpp"

class GLUquadric;
typedef class GLUquadric GLUquadricObject;

// use PLATE_CURRENT_IDX stands for using current plate
// and use PLATE_ALL_IDX
#define PLATE_CURRENT_IDX -1
#define PLATE_ALL_IDX -2

#define MAX_PLATE_COUNT 36

inline int compute_colum_count(int count)
{
    float value       = sqrt((float) count);
    float round_value = round(value);
    int   cols;

    if (value > round_value)
        cols = round_value + 1;
    else
        cols = round_value;

    return cols;
}

extern const float WIPE_TOWER_DEFAULT_X_POS;
extern const float WIPE_TOWER_DEFAULT_Y_POS; // Max y

extern const float I3_WIPE_TOWER_DEFAULT_X_POS;
extern const float I3_WIPE_TOWER_DEFAULT_Y_POS; // Max y

namespace Slic3r {

class Model;
class ModelObject;
class ModelInstance;
class Print;
class SLAPrint;

namespace GUI {
class Plater;
class GLCanvas3D;
struct Camera;
class PartPlateList;

using GCodeResult = GCodeProcessorResult;

// [INTENT] Manages a single build plate, its geometry, objects, and rendering state in the 3D workspace
// [STATE] Tracks plate dimensions, origin, locked state, slice validity, and object instances
// [UNITY] Replace with MonoBehaviour managing plate representation or ScriptableObject for data
// [PORTING_HAZARD:P1] ObjectBase inheritance needs Unity-compatible base class or interface
// [PORTING_HAZARD:P2] GL rendering requires Unity's Mesh/Renderer system
class PartPlate : public ObjectBase
{
public:
    enum HeightLimitMode { HEIGHT_LIMIT_NONE, HEIGHT_LIMIT_BOTTOM, HEIGHT_LIMIT_TOP, HEIGHT_LIMIT_BOTH };

private:
    // [STATE] References to parent container, UI, and data model
    // [UNITY] Use Unity's scene references (PlateManager, PlaterController, ModelData)
    // [PORTING_HAZARD:P1] Weak references - Unity's reference system handles lifetime
    PartPlateList*    m_partplate_list{nullptr};
    Plater*           m_plater; // Plater reference, not own it
    Model*            m_model;  // Model reference, not own it
    PrinterTechnology printer_technology;

    // [STATE] Object-instance mappings tracking what's on this plate
    // [UNITY] Use Dictionary<int, HashSet<int>> or PlateInstanceData struct
    std::set<std::pair<int, int>> obj_to_instance_set;
    std::set<std::pair<int, int>> instance_outside_set;

    // [STATE] Plate metadata: index, origin, dimensions, and operational flags
    // [UNITY] Use int index, Vector3 origin, Vector2 size, bool flags
    // [PORTING_HAZARD:P1] Coordinate system: Blender/OpenGL (Y-up) vs Unity (Y-up, right-handed)
    int   m_plate_index;
    Vec3d m_origin;
    int   m_width;
    int   m_depth;
    int   m_height;
    float m_height_to_lid;
    float m_height_to_rod;
    bool  m_printable;
    bool  m_locked;
    bool  m_ready_for_slice;
    bool  m_slice_result_valid;
    bool  m_apply_invalid{false};
    float m_slice_percent;

    // [STATE] Slicing results and filament information
    // [UNITY] Use ScriptableObject for print data, List<FilamentData> for filaments
    // [THREAD] Slicing runs in background thread - unity needs coroutines or Job System
    Print*                    m_print; // Print reference, not own it, no need to serialize
    GCodeProcessorResult*     m_gcode_result;
    std::vector<FilamentInfo> slice_filaments_info;
    int                       m_print_index;

    // [STATE] Temporary file paths for gcode and config storage
    // [UNITY] Use Application.persistentDataPath or temporary cache
    std::string m_tmp_gcode_path;       // use a temp path to store the gcode
    std::string m_temp_config_3mf_path; // use a temp path to store the config 3mf
    std::string m_gcode_path_from_3mf;  // use a path to store the gcode loaded from 3mf

    friend class PartPlateList;

    // [STATE] Plate geometry and spatial data for collision, rendering, and raycasting
    // [UNITY] Use MeshCollider for collision, Mesh for rendering, Vector3 arrays for vertices
    // [OPENGL] GLVertexBuffer equivalent for m_triangles and other GLModels
    // [PORTING_HAZARD:P1] Transform3d -> Matrix4x4, verify coordinate space (Y-up vs Y-up)
    // [PORTING_HAZARD:P2] PickingModel -> Unity's picking/raycasting system
    Pointfs                            m_shape;
    Pointfs                            m_exclude_area;
    std::vector<Pointfs>               m_extruder_areas;
    std::vector<double>                m_extruder_heights;
    BoundingBoxf3                      m_bounding_box;
    BoundingBoxf3                      m_extended_bounding_box;
    mutable std::vector<BoundingBoxf3> m_exclude_bounding_box;
    mutable BoundingBoxf3              m_grabber_box;
    Transform3d                        m_grabber_trans_matrix;
    Slic3r::Geometry::Transformation   position;
    std::vector<Vec3f>                 positions;
    ExPolygon                          m_print_polygon;
    PickingModel                       m_triangles;
    // [OPENGL] Render models for plate visualization (excludes, logo, grid, height limits)
    // [UNITY] Use Mesh/MeshFilter/MeshRenderer components, convert GLModel to Mesh
    GLModel m_exclude_triangles;
    GLModel m_wrapping_detection_triangles;
    GLModel m_logo_triangles;
    GLModel m_gridlines;
    GLModel m_gridlines_bolder;
    GLModel m_height_limit_common;
    GLModel m_height_limit_bottom;
    GLModel m_height_limit_top;

    // [OPENGL] Interactive icons for plate controls (delete, arrange, orient, lock, etc.)
    // [UNITY] Use UI.Image components with Button or custom pickable meshes
    PickingModel m_del_icon;
    PickingModel m_arrange_icon;
    PickingModel m_orient_icon;
    PickingModel m_lock_icon;
    PickingModel m_plate_settings_icon;
    PickingModel m_plate_filament_map_icon;
    PickingModel m_plate_name_edit_icon;
    PickingModel m_move_front_icon;
    GLModel      m_plate_idx_icon;
    GLTexture    m_texture;

    // [STATE] UI interaction state and rendering properties
    // [UNITY] Use MonoBehaviour fields, raycast target flags, selection state in manager
    float             m_scale_factor{1.0f};
    GLUquadricObject* m_quadric;
    int               m_hover_id;
    bool              m_selected;
    int               m_timelapse_warning_code = 0;

    // [STATE] Runtime configuration for this plate (temp, speed, etc.)
    // [UNITY] Use ScriptableObject or Serializable config class
    // [CONFIG] DynamicPrintConfig bridges Slic3r config system to Unity
    DynamicPrintConfig m_config;

    // [STATE] User-facing plate name and display texture
    // [UNITY] Use string field + TextMeshPro for name, Texture2D for icon
    // [PORTING_HAZARD:P1] wxCoord -> Unity UI units
    std::string m_name;
    GLModel     m_plate_name_icon;
    GLTexture   m_name_texture;
    wxCoord     m_name_texture_width;
    wxCoord     m_name_texture_height;

    void init();
    bool valid_instance(int obj_id, int instance_id);
    void generate_print_polygon(ExPolygon& print_polygon);
    void generate_exclude_polygon(ExPolygon& exclude_polygon);
    void generate_logo_polygon(ExPolygon& logo_polygon);
    void calc_bounding_boxes() const;
    void calc_triangles(const ExPolygon& poly);
    void calc_exclude_triangles(const ExPolygon& poly);
    void calc_triangles_from_polygon(const ExPolygon& poly, GLModel& render_model);
    void calc_gridlines(const ExPolygon& poly, const BoundingBox& pp_bbox);
    void calc_height_limit();
    void calc_vertex_for_number(int index, bool one_number, GLModel& buffer);
    void calc_vertex_for_plate_name_edit_icon(GLTexture* texture, int index, PickingModel& model);
    void calc_vertex_for_icons(int index, PickingModel& model);
    // void calc_vertex_for_icons_background(int icon_count, GLModel &buffer);
    void render_background(bool force_default_color = false);
    void render_logo(bool bottom, bool render_cali = true);
    void render_logo_texture(GLTexture& logo_texture, GLModel& logo_buffer, bool bottom);
    void render_exclude_area(bool force_default_color);
    // void render_background_for_picking(const ColorRGBA render_color) const;
    void render_grid(bool bottom);
    void render_wrapping_detection_area(bool force_default_color);
    void render_height_limit(PartPlate::HeightLimitMode mode = HEIGHT_LIMIT_BOTH);
    // void render_label(GLCanvas3D& canvas) const;
    // void render_grabber(const ColorRGBA render_color, bool use_lighting) const;
    // void render_face(float x_size, float y_size) const;
    // void render_arrows(const ColorRGBA render_color, bool use_lighting) const;
    // void render_left_arrow(const ColorRGBA render_color, bool use_lighting) const;
    // void render_right_arrow(const ColorRGBA render_color, bool use_lighting) const;
    void render_icon_texture(GLModel& buffer, GLTexture& texture);
    void show_tooltip(const std::string tooltip);
    void render_icons(bool bottom, bool only_name = false, int hover_id = -1);
    void render_only_numbers(bool bottom);
    void render_plate_name_texture();
    void register_raycasters_for_picking(GLCanvas3D& canvas);
    int  picking_id_component(int idx) const;

    void on_filament_map_mode_change();

public:
    // [STATE] Constants for UI interaction (hover IDs, grabber count)
    // [UNITY] Use const int or enum in PlateUIManager
    static constexpr unsigned int PLATE_NAME_HOVER_ID   = 6;
    static constexpr unsigned int PLATE_FILAMENT_MAP_ID = 8;
    static constexpr unsigned int GRABBER_COUNT         = 9;

    // [STATE] Render color scheme for plate states
    // [UNITY] Use MaterialPropertyBlock or separate materials per state
    static ColorRGBA SELECT_COLOR;
    static ColorRGBA UNSELECT_COLOR;
    static ColorRGBA UNSELECT_DARK_COLOR;
    static ColorRGBA DEFAULT_COLOR;
    static ColorRGBA LINE_BOTTOM_COLOR;
    static ColorRGBA LINE_TOP_COLOR;
    static ColorRGBA LINE_TOP_DARK_COLOR;
    static ColorRGBA LINE_TOP_SEL_COLOR;
    static ColorRGBA LINE_TOP_SEL_DARK_COLOR;
    static ColorRGBA HEIGHT_LIMIT_BOTTOM_COLOR;
    static ColorRGBA HEIGHT_LIMIT_TOP_COLOR;

    // [EVENT] Initialize color scheme on startup
    // [UNITY] Call on PlateManager Awake() or before first render
    static void update_render_colors();
    static void load_render_colors();

    // [EVENT] Default constructor - creates uninitialized plate
    // [PORTING_HAZARD:P2] Unity requires Awake/Start initialization pattern
    PartPlate();

    // [INTENT] Main constructor - establishes plate geometry and references
    // [EVENT] Called when creating new plates or loading from file
    // [PORTING_HAZARD:P1] Verify parameter order matches Unity constructor patterns
    PartPlate(PartPlateList*    partplate_list,
              Vec3d             origin,
              int               width,
              int               depth,
              int               height,
              Plater*           platerObj,
              Model*            modelObj,
              bool              printable = true,
              PrinterTechnology tech      = ptFFF);
    // [EVENT] Destructor - cleans up GL resources and references
    // [UNITY] Use OnDisable() or OnDestroy() for cleanup
    ~PartPlate();

    bool operator<(PartPlate&) const;

    // [EVENT] Clear - removes all objects from plate, optionally clears slice data
    // [UNITY] Use Clear() method on PlateManager, clear instance list and game objects
    void clear(bool clear_sliced_result = true);

    // [STATE] Bed type management for compatibility checking
    // [UNITY] Use enum field mapped to material presets
    BedType get_bed_type(bool load_from_project = false) const;
    void    set_bed_type(BedType bed_type);
    void    reset_bed_type();

    // [EVENT] Reset skirt start angle for print configuration
    // [CONFIG] Modulates print config parameter
    void reset_skirt_start_angle();

    // [CONFIG] Access plate-specific configuration
    // [UNITY] Use getter returning SerializableConfig or ScriptableObject
    DynamicPrintConfig* config() { return &m_config; }

    // set print sequence per plate
    // bool print_seq_same_global = true;
    void          set_print_seq(PrintSequence print_seq = PrintSequence::ByDefault);
    PrintSequence get_print_seq() const;
    // Get the real effective print sequence of current plate.
    // If curr_plate's print_seq is ByDefault, use the global sequence
    // @return PrintSequence::{ByLayer,ByObject}
    PrintSequence get_real_print_seq(bool* plate_same_as_global = nullptr) const;

    std::vector<int> get_real_filament_maps(const DynamicConfig& g_config, bool* use_global_param = nullptr) const;
    FilamentMapMode  get_real_filament_map_mode(const DynamicConfig& g_config, bool* use_global_param = nullptr) const;

    FilamentMapMode get_filament_map_mode() const;
    void            set_filament_map_mode(const FilamentMapMode& mode);

    // get filament map, 0 based filament ids, 1 based extruder ids
    std::vector<int> get_filament_maps() const;
    void             set_filament_maps(const std::vector<int>& f_maps);

    void clear_filament_map();
    void clear_filament_map_mode();

    bool has_spiral_mode_config() const;
    bool get_spiral_vase_mode() const;
    void set_spiral_vase_mode(bool spiral_mode, bool as_global);

    std::vector<Vec2d> get_plate_wrapping_detection_area() const;

    // static const int plate_x_offset = 20; //mm
    // static const double plate_x_gap = 0.2;
    ThumbnailData    thumbnail_data;
    ThumbnailData    no_light_thumbnail_data;
    ThumbnailData    obj_preview_thumbnail_data;
    static const int plate_thumbnail_width  = 512;
    static const int plate_thumbnail_height = 512;

    ThumbnailData top_thumbnail_data;
    ThumbnailData pick_thumbnail_data;

    // ThumbnailData cali_thumbnail_data;
    PlateBBoxData cali_bboxes_data;
    // static const int cali_thumbnail_width = 2560;
    // static const int cali_thumbnail_height = 2560;

    // set the plate's index
    void set_index(int index);

    // get the plate's index
    int get_index() { return m_plate_index; }

    // SoftFever
    // get the plate's name
    std::string get_plate_name() const { return m_name; }
    void        generate_plate_name_texture();
    // set the plate's name
    void set_plate_name(const std::string& name);

    void set_timelapse_warning_code(int code) { m_timelapse_warning_code = code; }
    int  timelapse_warning_code() { return m_timelapse_warning_code; }

    // get the print's object, result and index
    void get_print(PrintBase** print, GCodeResult** result, int* index);

    // set the print object, result and it's index
    void set_print(PrintBase* print, GCodeResult* result = nullptr, int index = -1);

    // get gcode filename
    std::string get_gcode_filename();

    bool is_valid_gcode_file();

    // get the plate's center point origin
    Vec3d get_center_origin();
    /* size and position related functions*/
    // set position and size
    void set_pos_and_size(Vec3d& origin, int width, int depth, int height, bool with_instance_move, bool do_clear = true);

    // BBS
    Vec2d           get_size() const { return Vec2d(m_width, m_depth); }
    ModelObjectPtrs get_objects() { return m_model->objects; }
    ModelObjectPtrs get_objects_on_this_plate();
    ModelInstance*  get_instance(int obj_id, int instance_id);
    BoundingBoxf3   get_objects_bounding_box();

    Vec3d get_origin() { return m_origin; }
    // Vec3d calculate_wipe_tower_size(const DynamicPrintConfig &config, const double w, const double wipe_volume, int plate_extruder_size =
    // 0, bool use_global_objects = false) const;
    Vec3d                       estimate_wipe_tower_size(const DynamicPrintConfig& config,
                                                         const double              w,
                                                         const double              wipe_volume,
                                                         int                       extruder_count            = 1,
                                                         int                       plate_extruder_size       = 0,
                                                         bool                      use_global_objects        = false,
                                                         bool                      enable_wrapping_detection = false) const;
    arrangement::ArrangePolygon estimate_wipe_tower_polygon(const DynamicPrintConfig& config,
                                                            int                       plate_index,
                                                            Vec3d&                    wt_pos,
                                                            Vec3d&                    wt_size,
                                                            int                       extruder_count      = 1,
                                                            int                       plate_extruder_size = 0,
                                                            bool                      use_global_objects  = false) const;
    bool                        check_objects_empty_and_gcode3mf(std::vector<int>& result) const;
    // get used filaments from config, 1 based idx
    std::vector<int> get_extruders(bool conside_custom_gcode = false) const;
    std::vector<int> get_extruders_under_cli(bool conside_custom_gcode, DynamicPrintConfig& full_config) const;
    std::vector<int> get_extruders_without_support(bool conside_custom_gcode = false) const;
    // get used filaments from gcode result, 1 based idx
    std::vector<int> get_used_filaments();
    int              get_physical_extruder_by_filament_id(const DynamicConfig& g_config, int idx) const;
    bool             check_filament_printable(const DynamicPrintConfig& config, wxString& error_message);
    bool             check_tpu_printable_status(const DynamicPrintConfig& config, const std::vector<int>& tpu_filaments);
    bool             check_mixture_of_pla_and_petg(const DynamicPrintConfig& config);
    bool             check_mixture_filament_compatible(const DynamicPrintConfig& config, std::string& error_msg);
    bool             check_compatible_of_nozzle_and_filament(const DynamicPrintConfig&       config,
                                                             const std::vector<std::string>& filament_presets,
                                                             std::string&                    error_msg);

    /* [INTENT] Instance related operations - plate membership and containment
       [STATE] Maintains obj_to_instance_set and instance_outside_set
       [UNITY] Use Dictionary<int, HashSet<int>> for instance tracking */

    // [EVENT] Check if instance is bound to plate (intersects or contained)
    bool contain_instance(int obj_id, int instance_id);

    // [EVENT] Check if instance is fully contained within plate boundaries
    // [PORTING_HAZARD:P1] Manifold collision detection system needed
    bool contain_instance_totally(ModelObject* object, int instance_id) const;
    bool contain_instance_totally(int obj_id, int instance_id) const;

    // judge whether the plate's origin is at the left of instance or not
    bool is_left_top_of(int obj_id, int instance_id);

    // check whether instance is outside the plate or not
    bool check_outside(int obj_id, int instance_id, BoundingBoxf3* bounding_box = nullptr);

    // judge whether instance is intesected with plate or not
    bool intersect_instance(int obj_id, int instance_id, BoundingBoxf3* bounding_box = nullptr);

    // add an instance into plate
    int add_instance(int obj_id, int instance_id, bool move_position, BoundingBoxf3* bounding_box = nullptr);

    // remove instance from plate
    int remove_instance(int obj_id, int instance_id);

    // translate instance on the plate
    void translate_all_instance(Vec3d position);

    // duplicate all instance for count
    void duplicate_all_instance(unsigned int dup_count, bool need_skip, std::map<int, bool>& skip_objects);

    // update instance exclude state
    void update_instance_exclude_status(int obj_id, int instance_id, BoundingBoxf3* bounding_box = nullptr);

    // update object's index caused by original object deleted
    void update_object_index(int obj_idx_removed, int obj_idx_max);

    // set objects configs when enabling spiral vase mode.
    void set_vase_mode_related_object_config(int obj_id = -1);

    // whether it is empty
    bool empty() { return obj_to_instance_set.empty(); }

    int printable_instance_size();

    // whether it is has printable instances
    bool has_printable_instances();
    bool is_all_instances_unprintable();

    // move instances to left or right PartPlate
    void move_instances_to(PartPlate& left_plate, PartPlate& right_plate, BoundingBoxf3* bounding_box = nullptr);

    /* [INTENT] Rendering related functions - visualization of plate geometry
       [OPENGL] Direct GL rendering with transforms
       [UNITY] Use MeshRenderer, MaterialPropertyBlock, Camera.Render() */

    // [EVENT/STATE] Get plate geometry shape
    const Pointfs& get_shape() const { return m_shape; }

    // [EVENT] Set/update plate geometry, exclude areas, and parameters
    // [UNITY] Update mesh colliders and render meshes
    bool                        set_shape(const Pointfs&              shape,
                                          const Pointfs&              exclude_areas,
                                          const std::vector<Pointfs>& extruder_areas,
                                          const std::vector<double>&  extruder_heights,
                                          Vec2d                       position,
                                          float                       height_to_lid,
                                          float                       height_to_rod);
    const std::vector<Pointfs>& get_extruder_areas() const { return m_extruder_areas; }
    const std::vector<double>&  get_extruder_heights() const { return m_extruder_heights; }

    // [EVENT] Spatial containment and intersection queries
    // [PORTING_HAZARD:P1] Bounding box logic may differ in Unity's coordinate space
    bool contains(const Vec3d& point) const;
    bool contains(const GLVolume& v) const;
    bool contains(const BoundingBoxf3& bb) const;
    bool intersects(const BoundingBoxf3& bb) const;

    // [OPENGL] Main render method - draws plate and all visual elements
    // [EVENT] Called every frame during scene render
    // [PORTING_HAZARD:P2] Requires conversion to Unity's rendering pipeline (URP/HDRP)
    void render(const Transform3d& view_matrix,
                const Transform3d& projection_matrix,
                bool               bottom,
                bool               only_body              = false,
                bool               force_background_color = false,
                HeightLimitMode    mode                   = HEIGHT_LIMIT_NONE,
                int                hover_id               = -1,
                bool               render_cali            = false,
                bool               show_grid              = true);

    void                 set_selected();
    void                 set_unselected();
    void                 set_hover_id(int id) { m_hover_id = id; }
    const BoundingBoxf3& get_bounding_box(bool extended = false) { return extended ? m_extended_bounding_box : m_bounding_box; }
    const BoundingBox    get_bounding_box_crd();
    BoundingBoxf3        get_plate_box() { return get_build_volume(); }
    BoundingBoxf3        get_build_volume(bool use_share = false);

    const std::vector<BoundingBoxf3>& get_exclude_areas() { return m_exclude_bounding_box; }

    /*status related functions*/
    // update status
    void update_states();

    // is locked or not
    bool is_locked() const { return m_locked; }
    void lock(bool state) { m_locked = state; }

    // is a printable plate or not
    bool is_printable() const { return m_printable; }

    // can be sliced or not
    bool can_slice() const { return m_ready_for_slice && !m_apply_invalid; }
    void update_slice_ready_status(bool ready_slice) { m_ready_for_slice = ready_slice; }

    // bedtype mismatch or not
    bool is_apply_result_invalid() const { return m_apply_invalid; }
    void update_apply_result_invalid(bool invalid) { m_apply_invalid = invalid; }

    // is slice result valid or not
    bool is_slice_result_valid() const { return m_slice_result_valid; }

    // is slice result ready for print
    bool is_slice_result_ready_for_print() const
    {
        bool result = m_slice_result_valid;
        if (result)
            result = m_gcode_result ? (!m_gcode_result->toolpath_outside && m_gcode_result->gcode_check_result.error_code == 0 &&
                                       !m_gcode_result->filament_printable_reuslt.has_value()) :
                                      false; // && !m_gcode_result->conflict_result.has_value()  gcode conflict can also print
        return result;
    }

    // check whether plate's slice result valid for export to file
    bool is_slice_result_ready_for_export() { return is_slice_result_ready_for_print() && has_printable_instances(); }

    // invalid sliced result
    void update_slice_result_valid_state(bool valid = false);

    void update_slicing_percent(float percent) { m_slice_percent = percent; }

    float get_slicing_percent() { return m_slice_percent; }

    /* [INTENT] Slicing related functions - background processing
       [THREAD] Slicing runs in background worker thread
       [STATE] Manages print context and results */

    // [EVENT] Update slice context to background process
    // [THREAD] Called from main thread to pass context to worker
    // [PORTING_HAZARD:P1] Unity needs Job System or async/await pattern
    void update_slice_context(BackgroundSlicingProcess& process);

    // [STATE] Access print object and slice results
    // [UNITY] Use references to ScriptableObject or generated data
    Print*                fff_print() { return m_print; }
    GCodeProcessorResult* get_slice_result() { return m_gcode_result; }

    std::string get_tmp_gcode_path();
    std::string get_temp_config_3mf_path();
    // this API should only be used for command line usage
    void set_tmp_gcode_path(std::string new_path) { m_tmp_gcode_path = new_path; }
    // load gcode from file
    int load_gcode_from_file(const std::string& filename);
    // load thumbnail data from file
    int load_thumbnail_data(std::string filename, ThumbnailData& thumb_data);
    // load pattern thumbnail data from file
    int load_pattern_thumbnail_data(std::string filename);
    // load pattern box data from file
    int load_pattern_box_data(std::string filename);

    std::vector<int>                get_first_layer_print_sequence() const;
    std::vector<LayerPrintSequence> get_other_layers_print_sequence() const;
    void                            set_first_layer_print_sequence(const std::vector<int>& sorted_filaments);
    void                            set_other_layers_print_sequence(const std::vector<LayerPrintSequence>& layer_seq_list);
    void                            update_first_layer_print_sequence(size_t filament_nums);
    void                            update_first_layer_print_sequence_when_delete_filament(size_t filamen_id);

    void print() const;

    void on_extruder_count_changed(int extruder_count);
    void set_filament_count(int filament_count);
    void on_filament_added();
    void on_filament_deleted(int filament_count, int filament_id);

    // [PORTING_HAZARD:P1] Cereal serialization - Unity uses JsonUtility, ScriptableObjects, or PlayerPrefs
    friend class cereal::access;
    friend class UndoRedo::StackImpl;

    // [EVENT].Deserialize plate state from archive
    // [PORTING_HAZARD:P1] Requires porting to Unity serialization (JSON, binary, or asset persistence)
    template<class Archive> void load(Archive& ar)
    {
        std::vector<std::pair<int, int>> objects_and_instances;
        std::vector<std::pair<int, int>> instances_outside;

        ar(m_plate_index, m_name, m_print_index, m_origin, m_width, m_depth, m_height, m_locked, m_selected, m_ready_for_slice,
           m_slice_result_valid, m_apply_invalid, m_printable, m_tmp_gcode_path, objects_and_instances, instances_outside, m_config);

        for (std::vector<std::pair<int, int>>::iterator it = objects_and_instances.begin(); it != objects_and_instances.end(); ++it)
            obj_to_instance_set.insert(std::pair(it->first, it->second));

        for (std::vector<std::pair<int, int>>::iterator it = instances_outside.begin(); it != instances_outside.end(); ++it)
            instance_outside_set.insert(std::pair(it->first, it->second));
    }

    // [EVENT] Serialize plate state to archive
    // [PORTING_HAZARD:P1] Requires porting to Unity serialization
    template<class Archive> void save(Archive& ar) const
    {
        std::vector<std::pair<int, int>> objects_and_instances;
        std::vector<std::pair<int, int>> instances_outside;

        for (std::set<std::pair<int, int>>::iterator it = instance_outside_set.begin(); it != instance_outside_set.end(); ++it)
            instance_outside.emplace_back(it->first, it->second);

        for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
            objects_and_instances.emplace_back(it->first, it->second);

        ar(m_plate_index, m_name, m_print_index, m_origin, m_width, m_depth, m_height, m_locked, m_selected, m_ready_for_slice,
           m_slice_result_valid, m_apply_invalid, m_printable, m_tmp_gcode_path, objects_and_instances, instances_outside, m_config);
    }
    /*template<class Archive> void serialize(Archive& ar)
    {
        std::vector<std::pair<int, int>> objects_and_instances;
        for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
            objects_and_instances.emplace_back(it->first, it->second);
        ar(m_plate_index, m_origin, m_width, m_depth, m_height, m_locked, m_ready_for_slice, m_printable, objects_and_instances);
    }*/
};

class PartPlateList : public ObjectBase
{
    // [INTENT] Manages all build plates in the workspace, handles creation, deletion, selection
    // [STATE] Maintains ordered list of plates, current selection, global geometry
    // [UNITY] Use PlateManager singleton or GameObject with PlateList component
    // [PORTING_HAZARD:P1] ObjectBase inheritance needs Unity-compatible base
    // [PORTING_HAZARD:P2] Requires Scene or ScriptableObject to persist plates

    // [STATE] References to global systems
    // [UNITY] Use Unity scene references or singleton pattern
    Plater*           m_plater; // Plater reference, not own it
    Model*            m_model;  // Model reference, not own it
    PrinterTechnology printer_technology;

    // [STATE] Collection of all plates and their print results
    // [UNITY] Use List<PartPlateMonoBehaviour> and Dictionary<int, PrintData>
    // [THREAD] Mutex protects concurrent access during async operations
    std::vector<PartPlate*>     m_plate_list;
    std::map<int, PrintBase*>   m_print_list;
    std::map<int, GCodeResult*> m_gcode_result_list;
    std::mutex                  m_plates_mutex; // [THREAD] Guards plate list access
    int                         m_plate_count;
    int                         m_plate_cols; // Grid layout columns
    int                         m_current_plate;
    int                         m_print_index;

    // [STATE] Global plate dimensions
    // [UNITY] MaterialPropertyBlock or ScriptableObject for dimensions
    int m_plate_width;
    int m_plate_depth;
    int m_plate_height;

    // [STATE] Height limit parameters for collision avoidance
    float                      m_height_to_lid;
    float                      m_height_to_rod;
    PartPlate::HeightLimitMode m_height_limit_mode{PartPlate::HEIGHT_LIMIT_BOTH};

    // [STATE] Special unprintable plate for overflow/out-of-bounds objects
    // [UNITY] Use separate GameObject or flag in plate system
    PartPlate unprintable_plate;

    // [STATE] Global geometry configuration (shape, exclude areas, extruder zones)
    // [UNITY] Use shared MeshCollider for zones, serialized exclude areas
    Pointfs              m_shape;
    Pointfs              m_exclude_areas;
    Pointfs              m_wrapping_exclude_areas;
    std::vector<Pointfs> m_extruder_areas;
    std::vector<double>  m_extruder_heights;
    BoundingBoxf3        m_bounding_box;

    // [STATE] Initialization flag and shared texture resources
    // [UNITY] Use AssetBundle or Resources for textures
    bool        m_intialized;
    std::string m_logo_texture_filename;
    GLTexture   m_logo_texture; // Bed type logo
    GLTexture   m_del_texture;  // Delete icon
    GLTexture   m_del_hovered_texture;
    GLTexture   m_move_front_hovered_texture;
    GLTexture   m_move_front_texture;
    GLTexture   m_arrange_texture; // Auto-arrange icon
    GLTexture   m_arrange_hovered_texture;
    GLTexture   m_orient_texture; // Orientation icon
    GLTexture   m_orient_hovered_texture;
    GLTexture   m_locked_texture;
    GLTexture   m_locked_hovered_texture;
    GLTexture   m_lockopen_texture;
    GLTexture   m_lockopen_hovered_texture;
    GLTexture   m_plate_settings_texture; // Settings icon
    GLTexture   m_plate_settings_changed_texture;
    GLTexture   m_plate_settings_hovered_texture;
    GLTexture   m_plate_settings_changed_hovered_texture;
    GLTexture   m_plate_set_filament_map_texture; // Filament mapping icon
    GLTexture   m_plate_set_filament_map_hovered_texture;
    GLTexture   m_plate_name_edit_texture; // Name edit icon
    GLTexture   m_plate_name_edit_hovered_texture;
    GLTexture   m_idx_textures[MAX_PLATE_COUNT]; // Plate number labels

    // [STATE] Render options for UI toggles
    // [UNITY] Use boolean fields in PlateUIManager or settings
    bool render_bedtype_logo   = true;
    bool render_plate_settings = true;
    bool render_cali_logo      = true;

    // [STATE] Theme and UI state
    // [UNITY] Material themes or UI style manager
    bool m_is_dark = false;

    // [STATE] Filament count for UI updates
    // [UNITY] Reacts to configuration changes
    int m_filament_count = 1;

    // [EVENT] Initialize plates, textures, and global state
    // [UNITY] Call in Awake() or Start()
    void init();

    // [MATH] Compute plate origins for grid layout
    // [PORTING_HAZARD:P1] Coordinate calculations may differ in Unity's 2D/UI space
    Vec3d compute_origin(int index, int column_count);
    Vec3d compute_origin_for_unprintable();
    Vec2d compute_shape_position(int index, int cols);

    // [EVENT] Generate and release UI icon textures
    // [UNITY] Use Texture2D generation or load from sprites
    void generate_icon_textures();
    void release_icon_textures(); // [UNITY] Texture2D.Dispose()

    // [STATE] Position wipe tower on specific plate
    // [PORTING_HAZARD:P1] Port wipe tower logic to Unity
    void set_default_wipe_tower_pos_for_plate(int plate_idx);

    friend class cereal::access;
    friend class UndoRedo::StackImpl;
    friend class PartPlate;

public:
    // [INTENT] Helper class for managing bed/texture data
    // [UNITY] Use ScriptableObject for texture atlases or Serializabe bed data
    class BedTextureInfo
    {
    public:
        // [STATE] Texture sub-region definition
        // [UNITY] Use Texture2D with UV rect
        class TexturePart
        {
        public:
            // position in texture atlas
            float       x;
            float       y;
            float       w;
            float       h;
            std::string filename;
            GLTexture*  texture{nullptr};
            Vec2d       offset;
            GLModel*    buffer{nullptr};

            // [EVENT] Constructor taking UV coordinates
            TexturePart(float xx, float yy, float ww, float hh, std::string file)
            {
                x        = xx;
                y        = yy;
                w        = ww;
                h        = hh;
                filename = file;
                texture  = nullptr;
                buffer   = nullptr;
                offset   = Vec2d(0, 0);
            }

            // [PORTING_HAZARD:P1] Copy constructor - verify Unity reference handling
            TexturePart(const TexturePart& part)
            {
                this->x        = part.x;
                this->y        = part.y;
                this->w        = part.w;
                this->h        = part.h;
                this->offset   = part.offset;
                this->buffer   = part.buffer;
                this->filename = part.filename;
                this->texture  = part.texture;
            }
            void update_file(std::string file) { filename = file; }

            void update_buffer();
            void reset();
        };
        std::vector<TexturePart> parts;
        void                     reset();
    };

    // [STATE] Global constants and static texture resources
    // [UNITY] Use LoadingManager or Addressables for texture loading
    static constexpr unsigned int MAX_PLATES_COUNT = MAX_PLATE_COUNT;
    static GLTexture              bed_textures[(unsigned int) btCount];
    static bool                   is_load_bedtype_textures;
    static bool                   is_load_cali_texture;
    static bool                   is_load_extruder_only_area_textures;

    // [EVENT] Main constructor with full parameters
    // [PORTING_HAZARD:P1] Requires Unity Awake/Start pattern for initialization
    PartPlateList(int width, int depth, int height, Plater* platerObj, Model* modelObj, PrinterTechnology tech = ptFFF);

    // [EVENT] Simplified constructor
    PartPlateList(Plater* platerObj, Model* modelObj, PrinterTechnology tech = ptFFF);

    // [EVENT] Destructor - cleans up plate list and textures
    // [UNITY] Use OnDestroy() to release resources
    ~PartPlateList();

    // this may be happened after machine changed
    void reset_size(int width, int depth, int height, bool reload_objects = true, bool update_shapes = false);
    // clear all the instances in the plate, but keep the plates
    void clear(bool delete_plates = false, bool release_print_list = false, bool except_locked = false, int plate_index = -1);
    // clear all the instances in the plate, and delete the plates, only keep the first default plate
    void reset(bool do_init);
    // compute the origin for printable plate with index i using new width
    Vec3d compute_origin_using_new_size(int i, int new_width, int new_depth);

    // reset partplate to init states
    void reinit();

    // get the plate stride
    double plate_stride_x();
    double plate_stride_y();
    void   get_plate_size(int& width, int& depth, int& height)
    {
        width  = m_plate_width;
        depth  = m_plate_depth;
        height = m_plate_height;
    }

    // Pantheon: update plates after moving plate to the front
    void update_plates();

    /* [INTENT] Basic plate lifecycle operations
       [EVENT] Creation, duplication, deletion plates
       [UNITY] Use PlateManager methods to spawn/destroy GameObjects */

    // [EVENT] Create new empty plate, return index
    // [UNITY] Instantiate plate GameObject, add to list
    int create_plate(bool adjust_position = true);

    // [EVENT] Duplicate existing plate and its configuration
    // [PORTING_HAZARD:P1] Deep copy objects and instances
    int duplicate_plate(int index);

    // [EVENT] Destroy print associated with index
    // [THREAD] May involve async cleanup
    int destroy_print(int print_index);

    // [EVENT] Remove plate by index
    // [UNITY] Destroy GameObject and cleanup references
    int delete_plate(int index);

    // [EVENT] Remove currently selected plate
    void delete_selected_plate();

    // [STATE] Check bed type compatibility across plates
    bool check_all_plate_local_bed_type(const std::vector<BedType>& cur_bed_types);

    // [ACCESS] Get plate reference
    PartPlate* get_plate(int index);

    void get_height_limits(float& height_to_lid, float& height_to_rod)
    {
        height_to_lid = m_height_to_lid;
        height_to_rod = m_height_to_rod;
    }

    void set_height_limits_mode(PartPlate::HeightLimitMode mode) { m_height_limit_mode = mode; }

    int              get_curr_plate_index() const { return m_current_plate; }
    PartPlate*       get_curr_plate() { return m_plate_list[m_current_plate]; }
    const PartPlate* get_curr_plate() const { return m_plate_list[m_current_plate]; }

    std::vector<PartPlate*>& get_plate_list() { return m_plate_list; };

    PartPlate* get_selected_plate();

    std::vector<PartPlate*> get_nonempty_plate_list();

    std::vector<const GCodeProcessorResult*> get_nonempty_plates_slice_results();

    // compute the origin for printable plate with index i
    Vec3d   get_current_plate_origin() { return compute_origin(m_current_plate, m_plate_cols); }
    Vec2d   get_current_shape_position() { return compute_shape_position(m_current_plate, m_plate_cols); }
    Pointfs get_exclude_area() { return m_exclude_areas; }
    Pointfs get_wrapping_exclude_area() const { return m_wrapping_exclude_areas; }

    std::set<int> get_extruders(bool conside_custom_gcode = false) const;

    // select plate
    int select_plate(int index);

    // get the plate counts, not including the invalid plate
    int get_plate_count() const;

    // update the plate cols due to plate count change
    void update_plate_cols();

    void update_all_plates_pos_and_size(bool adjust_position       = true,
                                        bool with_unprintable_move = true,
                                        bool switch_plate_type     = false,
                                        bool do_clear              = true);

    // get the plate cols
    int get_plate_cols() { return m_plate_cols; }

    // move the plate to position index
    int move_plate_to_index(int old_index, int new_index);

    // lock plate
    int lock_plate(int index, bool state);

    // is locked
    bool is_locked(int index) { return m_plate_list[index]->is_locked(); }

    // find plate by print index, return -1 if not found
    int find_plate_by_print_index(int index);

    /* [INTENT] Instance cross-plate management
       [STATE] Tracks where instances are, moves them between plates
       [PORTING_HAZARD:P1] All spatial queries need Unity Bounds/Colliders */

    // [EVENT] Find which plate contains instance (partial intersection)
    int find_instance(int obj_id, int instance_id);
    int find_instance(BoundingBoxf3& bounding_box);

    // [EVENT] Find plate that fully contains instance
    // [PORTING_HAZARD:P2] Requires manifold containment test
    int find_instance_belongs(int obj_id, int instance_id);

    // [EVENT] Refresh instance after transformation changes
    // [UNITY] Called when objects move/rotate
    int notify_instance_update(int obj_id, int instance_id, bool is_new = false);

    // [EVENT] Remove instance from plate tracking
    int notify_instance_removed(int obj_id, int instance_id);

    // [EVENT] Explicitly move instance to target plate
    int add_to_plate(int obj_id, int instance_id, int plate_id);

    // [EVENT] Re-scan scene and rebuild plate content
    // [PORTING_HAZARD:P1] Unity requires SceneObject enumeration
    int reload_all_objects(bool except_locked = false, int plate_index = -1);

    // [EVENT] Populate plate after creation
    int construct_objects_list_for_new_plate(int plate_index);

    /* [INTENT] Arrangement and packing logic for auto-layout
       [STATE] Compute plate assignments, collision avoidance, constraints
       [PORTING_HAZARD:P1] Slic3r's Arrange -> Unity's packing algorithms */

    // [MATH] Compute target plate index for object based on geometry
    int compute_plate_index(arrangement::ArrangePolygon& arrange_polygon);

    // [EVENT] Pre-process single object for arrangement with lock constraints
    bool preprocess_arrange_polygon(int obj_index, int instance_index, arrangement::ArrangePolygon& arrange_polygon, bool selected);

    // [EVENT] Check locked plate constraints
    bool preprocess_arrange_polygon_other_locked(int                          obj_index,
                                                 int                          instance_index,
                                                 arrangement::ArrangePolygon& arrange_polygon,
                                                 bool                         selected);

    // [MATH] Exclude areas inflation and wrapping detection
    // [PORTING_HAZARD:P2] Complex geometry algorithms
    bool preprocess_exclude_areas(arrangement::ArrangePolygons& unselected,
                                  bool                          enable_wrapping_detect,
                                  int                           num_plates = 16,
                                  float                         inflation  = 0);
    bool preprocess_nonprefered_areas(arrangement::ArrangePolygons& regions, int num_plates = 1, float inflation = 0);

    // [EVENT] Post-process assigned plate index into arrange polygon
    void postprocess_bed_index_for_selected(arrangement::ArrangePolygon& arrange_polygon);
    void postprocess_bed_index_for_unselected(arrangement::ArrangePolygon& arrange_polygon);
    void postprocess_bed_index_for_current_plate(arrangement::ArrangePolygon& arrange_polygon);

    // [EVENT] Finalize arrangement assignment
    void postprocess_arrange_polygon(arrangement::ArrangePolygon& arrange_polygon, bool selected);

    /* [INTENT] Rendering all plates and global UI
       [OPENGL] Orchestrates individual plate renders
       [UNITY] Use Scene rendering, Camera.Render(), or URP ScriptableRenderPass */

    // [EVENT] Theme change notification
    void on_change_color_mode(bool is_dark) { m_is_dark = is_dark; }

    // [OPENGL] Main render orchestration for all plates
    // [EVENT] Called every frame
    void render(const Transform3d& view_matrix,
                const Transform3d& projection_matrix,
                bool               bottom,
                bool               only_current = false,
                bool               only_body    = false,
                int                hover_id     = -1,
                bool               render_cali  = false,
                bool               show_grid    = true);

    // [STATE] Set render feature toggles
    void set_render_option(bool bedtype_texture, bool plate_settings);
    void set_render_cali(bool value = true) { render_cali_logo = value; }

    // [EVENT] Setup raycast/picking for UI interaction
    // [UNITY] Register colliders with EventSystem or Physics.Raycast
    void register_raycasters_for_picking(GLCanvas3D& canvas)
    {
        for (auto plate : m_plate_list)
            plate->register_raycasters_for_picking(canvas);
    }

    // [STATE] Get global bounding box
    BoundingBoxf3& get_bounding_box() { return m_bounding_box; }

    // [EVENT] Select plate by object reference
    int select_plate_by_obj(int obj_index, int instance_index);

    // [EVENT] Update all plate bounding boxes
    void calc_bounding_boxes();

    // [EVENT] Visual focus/center on current plate
    // [UNITY] Camera control or UI highlight
    void select_plate_view();

    // [EVENT] Update global plate geometry - affects all plates
    // [PORTING_HAZARD:P2] Requires mesh regeneration cascade
    bool set_shapes(const Pointfs&              shape,
                    const Pointfs&              exclude_areas,
                    const Pointfs&              wrapping_exclude_areas,
                    const std::vector<Pointfs>& extruder_areas,
                    const std::vector<double>&  extruder_heights,
                    const std::string&          custom_texture,
                    float                       height_to_lid,
                    float                       height_to_rod);

    // [STATE] Hover state management
    void set_hover_id(int id);
    void reset_hover_id();

    // [EVENT] Spatial queries for global bounding box
    bool intersects(const BoundingBoxf3& bb);
    bool contains(const BoundingBoxf3& bb);

    const std::string& get_logo_texture_filename() { return m_logo_texture_filename; }
    void               update_logo_texture_filename(const std::string& texture_filename);
    /* [INTENT] Global slicing management - orchestrates slicing per plate
       [THREAD] Background slicing operation coordination
       [STATE] Track slice validity across all plates */

    // [EVENT] Pass context to current plate for background slicing
    // [PORTING_HAZARD:P1] Unity: Jobs or async/await for background work
    void update_slice_context_to_current_plate(BackgroundSlicingProcess& process);

    // [STATE] Access current plate's print and results
    Print&                get_current_fff_print() const;
    GCodeProcessorResult* get_current_slice_result() const;

    // [EVENT] Import gcode as new plate
    // [PORTING_HAZARD:P2] File I/O, marshalling
    int create_plate_from_gcode_file(const std::string& filename);

    // [EVENT] Invalidate all cached slice results (e.g., after config change)
    void invalid_all_slice_result();

    // [EVENT] Update single plate slice valid state
    void update_current_slice_result_state(bool valid) { m_plate_list[m_current_plate]->update_slice_result_valid_state(valid); }

    // [STATE] Aggregate slice validity checks
    bool is_all_slice_results_valid() const;
    bool is_all_slice_results_ready_for_print() const;
    bool is_all_plates_ready_for_slice() const;
    bool is_all_slice_result_ready_for_export() const;

    // [DEBUG] Print state to console
    void print() const;

    // [EVENT] Collect slice results from all plates
    void get_sliced_result(std::vector<bool>& sliced_result, std::vector<std::string>& gcode_paths);

    // [EVENT] Restore plates after deserialization
    // [PORTING_HAZARD:P1] Unity: JsonUtility.FromJsonOverwrite or asset restoration
    int rebuild_plates_after_deserialize(std::vector<bool>& previous_sliced_result, std::vector<std::string>& previous_gcode_paths);

    // [EVENT] Rebuild plate layout after auto-arrangement
    // [PORTING_HAZARD:P2] Recycle GameObjects by destroying old plates and creating new ones
    int rebuild_plates_after_arrangement(bool recycle_plates = true, bool except_locked = false, int plate_idx = -1);

    /* [INTENT] 3MF file serialization - import/export workspaces
       [PORTING_HAZARD:P1] Port 3MF parser/serializer or use Unity's 3MF libraries */

    // [EVENT] Export to 3MF structure
    // [UNITY] Use MeshExport or custom 3MF writer
    int store_to_3mf_structure(PlateDataPtrs& plate_data_list, bool with_slice_info = true, int plate_idx = -1);

    // [EVENT] Import from 3MF structure
    int load_from_3mf_structure(PlateDataPtrs& plate_data_list, int filament_count = 1);

    // [EVENT] Load gcode files after import
    int load_gcode_files();

    // [PORTING_HAZARD:P1] Cereal serialization requires Unity replacement
    template<class Archive> void serialize(Archive& ar)
    {
        // ar(cereal::base_class<ObjectBase>(this));
        // Cancel undo/redo for m_shape ,Because the printing area of different models is different, currently if the grid changes, it
        // cannot correspond to the model on the left ui
        ar(m_plate_width, m_plate_depth, m_plate_height, m_height_to_lid, m_height_to_rod, m_height_limit_mode, m_plate_count,
           m_current_plate, m_plate_list, unprintable_plate);
        // ar(m_plate_width, m_plate_depth, m_plate_height, m_plate_count, m_current_plate);
    }
    // [INTENT] Helper structures and texture management
    struct Rect
    {
        int x;
        int y;
        int w;
        int h;
    };

    // [MATH] Calculate extruder-only safe areas for dual nozzle
    bool calc_extruder_only_area(Rect& left_only_rect, Rect& right_only_rect);

    // [EVENT] Initialize bed type texture metadata
    void init_bed_type_info();

    // [EVENT] Initialize extruder-only area texture metadata
    bool init_extruder_only_area_info();

    // [EVENT] Load bed type texture assets
    // [UNITY] Use Resources.Load<Texture2D> or AssetBundle
    void load_bedtype_textures();
    void load_extruder_only_area_textures();

    // [EVENT] Toggle calibration texture visibility
    void show_cali_texture(bool show = true);

    // [EVENT] Initialize calibration texture metadata
    void init_cali_texture_info();

    // [EVENT] Load calibration texture assets
    void load_cali_textures();

    // [EVENT] Respond to extruder count changes
    // [UNITY] Recalculate display and compatibility
    void on_extruder_count_changed(int extruder_count);

    // [STATE] Update filament count for UI
    void set_filament_count(int filament_count);

    // [EVENT] Handle filament lifecycle in plates
    void on_filament_deleted(int filament_count, int filament_id);
    void on_filament_added(int filament_count);

    // [STATE] Per-plate compatibility flags for dual nozzle setups
    // [UNITY] Use Dictionary<int, bool>
    std::map<int, bool> m_allow_bed_type_in_double_nozzle;

    // [STATE/RESOURCE] Texture atlases for bed types, calibration, areas
    BedTextureInfo bed_texture_info[btCount];
    BedTextureInfo cali_texture_info;
    BedTextureInfo extruder_only_area_info[(unsigned char) Slic3r::ExtruderOnlyAreaType::btAreaCount];
};

} // namespace GUI
} // namespace Slic3r

namespace cereal {
template<class Archive> struct specialize<Archive, Slic3r::GUI::PartPlate, cereal::specialization::member_load_save>
{};
} // namespace cereal
#endif //__part_plate_hpp_
