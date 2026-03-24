#ifndef slic3r_EmbossJob_hpp_
#define slic3r_EmbossJob_hpp_

#include <atomic>
#include <memory>
#include <string>
#include <libslic3r/Emboss.hpp>
#include <libslic3r/EmbossShape.hpp> // ExPolygonsWithIds
#include "libslic3r/Point.hpp"       // Transform3d
#include "libslic3r/ObjectID.hpp"

#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/TextLines.hpp"

#include "Job.hpp"

// forward declarations
namespace Slic3r {
class TriangleMesh;
class ModelVolume;
enum class ModelVolumeType : int;
class BuildVolume;
namespace GUI {
class RaycastManager;
class Plater;
class GLCanvas3D;
class Worker;
class Selection;
} // namespace GUI
} // namespace Slic3r

namespace Slic3r::GUI::Emboss {

/// [INTENT] Group emboss creation/update helpers so GUI commands can stash geometry+name payloads before the background job runs.
/// [PORTING_HAZARD:P2] `libslic3r::EmbossShape` and `Transform3d` live deep in the slicer math stack; a Unity rewrite must replace them
/// with Vector3/PolygonBatch equivalents and keep syncing with the physics mesh.
/// <summary>
/// Base data hold data for create emboss shape
/// </summary>
/// [INTENT] Base class for embossing parameters (e.g., text, SVG, height).
/// [UNITY] Replace with a ScriptableObject class that stores geometry and emboss settings, and can be serialized and passed to worker jobs.
class DataBase
{
public:
    DataBase(const std::string& volume_name, std::shared_ptr<std::atomic<bool>> cancel)
        : volume_name(volume_name), cancel(std::move(cancel))
    {}
    DataBase(const std::string& volume_name, std::shared_ptr<std::atomic<bool>> cancel, EmbossShape&& shape)
        : volume_name(volume_name), cancel(std::move(cancel)), shape(std::move(shape))
    {}
    DataBase(DataBase&&) = default;
    virtual ~DataBase()  = default;

    /// <summary>
    /// Create shape
    /// e.g. Text extract glyphs from font
    /// Not 'const' function because it could modify shape
    /// </summary>
    virtual EmbossShape& create_shape() { return shape; };

    /// <summary>
    /// Write data how to reconstruct shape to volume
    /// </summary>
    /// <param name="volume">Data object for store emboss params</param>
    virtual void write(ModelVolume& volume) const;

    // Define projection move
    // True (raised) .. move outside from surface (MODEL_PART)
    // False (engraved).. move into object (NEGATIVE_VOLUME)
    // [STATE] Tracks whether the new geometry protrudes or cuts into the parent surface when the job runs.
    bool is_outside = true;

    // Define per letter projection on one text line
    // [optional] It is not used when empty
    // [STATE] Optional per-letter offsets assembled from UI text-line controls.
    Slic3r::Emboss::TextLines text_lines = {};

    // [optional] Define distance for surface
    // It is used only for flat surface (not cutted)
    // Position of Zero(not set value) differ for MODEL_PART and NEGATIVE_VOLUME
    // [STATE] Overrides the default Z offset when embossing on flat or cylindrical faces.
    std::optional<float> from_surface;

    // new volume name
    // [STATE] Human-readable alias for the generated volume so downstream UI can label it.
    std::string volume_name;

    // flag that job is canceled
    // for time after process.
    // [THREAD] Shared cancel flag that the UI holds to interrupt the worker thread safely.
    std::shared_ptr<std::atomic<bool>> cancel;

    // shape to emboss
    // [STATE] Cached EmbossShape result ready for serialization back into ModelVolume.
    EmbossShape shape;
};

/// <summary>
/// Hold neccessary data to create ModelVolume in job
/// Volume is created on the surface of existing volume in object.
/// NOTE: EmbossDataBase::font_file doesn't have to be valid !!!
/// </summary>
/// [INTENT] Capture the surface projection, selection ID, and transform so the worker can instantiate a volume on the target object without
/// UI reachbacks. [UNITY] Treat this as a data-only MonoBehaviour component with a Transform reference and selection GUID; Unity can
/// deserialize it from a ScriptableObject before enqueuing the job.
struct DataCreateVolume : public DataBase
{
    // define embossed volume type
    // [STATE] Indicates whether we will create a MODEL_PART or NEGATIVE_VOLUME emboss.
    ModelVolumeType volume_type;

    // parent ModelObject index where to create volume
    // [STATE] Tracks which ObjectID the new volume should attach to so the finalize step can find the right owner.
    ObjectID object_id;

    // new created volume transformation
    // [STATE] Transformation to place the new volume; follows the GL gizmo context.
    Transform3d trmat;
};
using DataBasePtr = std::unique_ptr<DataBase>;

/// <summary>
/// Hold neccessary data to update embossed text object in job
/// </summary>
/// [INTENT] Keep track of the existing volume handle plus undo state so updates can be replayed on the same slice-model entry.
/// [UNITY] Represents the DTO for an update operation, linking the new Emboss shape definition to the existing volume GUID in the Unity scene.
struct DataUpdate
{
    // Hold data about shape
    DataBasePtr base;

    // unique identifier of volume to change
    ObjectID volume_id;

    // Used for prevent flooding Undo/Redo stack on slider.
    // [STATE] Flag gate from slider events that coalesce successive updates before pushing undo entries.
    bool make_snapshot;
};

/// <summary>
/// Update text shape in existing text volume
/// Predict that there is only one runnig(not canceled) instance of it
/// </summary>
/// [INTENT] Recompute emboss geometry on a worker thread and stash the result before handing it back to wxWidgets.
class UpdateJob : public Job
{
    DataUpdate   m_input;
    TriangleMesh m_result;

public:
    // move params to private variable
    explicit UpdateJob(DataUpdate&& input);

    /// <summary>
    /// Create new embossed volume by m_input data and store to m_result
    /// </summary>
    /// <param name="ctl">Control containing cancel flag</param>
    /// [THREAD] This executes on the Job worker thread so it must keep all wxWidgets interaction out of the hot path.
    void process(Ctl& ctl) override;

    /// <summary>
    /// Update volume - change object_id
    /// </summary>
    /// <param name="canceled">Was process canceled.
    /// NOTE: Be carefull it doesn't care about
    /// time between finished process and started finalize part.</param>
    /// <param name="">unused</param>
    /// [THREAD] Finalize runs back on the UI thread so it may touch Selection/Undo stacks.
    void finalize(bool canceled, std::exception_ptr& eptr) override;

    /// <summary>
    /// Update text volume
    /// </summary>
    /// <param name="volume">Volume to be updated</param>
    /// <param name="mesh">New Triangle mesh for volume</param>
    /// <param name="base">Data to write into volume</param>
    /// [INTENT] Persist the rebuilt mesh and metadata into ModelVolume without opening GL contexts.
    /// [PORTING_HAZARD:P3] Updating ModelVolume is tied to libslic3r's object graph; Unity needs a replacement data model before rendering.
    static void update_volume(ModelVolume* volume, TriangleMesh&& mesh, const DataBase& base);
};

struct SurfaceVolumeData
{
    // Transformation of volume inside of object
    // [STATE] Local transform describing how the copied surface sits inside the host object.
    Transform3d transform;

    struct ModelSource
    {
        // source volumes
        // [STATE] Shared_ptr keeps the TriangleMesh alive while this helper runs.
        std::shared_ptr<const TriangleMesh> mesh;
        // Transformation of volume inside of object
        // [STATE] Per-source transform so cut surfaces can be mapped back to world space.
        Transform3d tr;
    };
    using ModelSources = std::vector<ModelSource>;
    ModelSources sources;
};

/// <summary>
/// Hold neccessary data to update embossed text object in job
/// </summary>
/// [UNITY] Similar to DataUpdate but for surface-based operations; requires mapping to Unity surface mesh generation jobs.
struct UpdateSurfaceVolumeData : public DataUpdate, public SurfaceVolumeData
{};

/// <summary>
/// Update text volume to use surface from object
/// </summary>
class UpdateSurfaceVolumeJob : public Job
{
    UpdateSurfaceVolumeData m_input;
    TriangleMesh            m_result;

public:
    // move params to private variable
    explicit UpdateSurfaceVolumeJob(UpdateSurfaceVolumeData&& input);
    void process(Ctl& ctl) override;
    void finalize(bool canceled, std::exception_ptr& eptr) override;
};

/// <summary>
/// Copied triangles from object to be able create mesh for cut surface from
/// </summary>
/// <param name="volume">Define embossed volume</param>
/// <returns>Source data for cut surface from</returns>
/// [INTENT] Mirror the object surface triangles so future emboss geometry can align to the host mesh when slicing.
/// [UNITY] Store this as a compact list of mesh references so the Unity job can replay them without libslic3r types.
SurfaceVolumeData::ModelSources create_volume_sources(const ModelVolume& volume);

/// <summary>
/// shorten params for start_crate_volume functions
/// </summary>
/// [INTENT] Bundle the live render/camera context plus raycaster so the event handler remains small.
/// [UNITY] Map this struct to a DTO consumed by a MonoBehaviour that translates mouse input into Unity physics raycasts + job enqueues.
struct CreateVolumeParams
{
    GLCanvas3D& canvas;
    // [OPENGL] GLCanvas3D owns the OpenGL render surface so a Unity port needs to replicate this with a camera or RenderTexture input.

    // Direction of ray into scene
    const Camera& camera;

    // To put new object on the build volume
    const BuildVolume& build_volume;

    // used to emplace job for execution
    Worker& worker;

    // New created volume type
    ModelVolumeType volume_type;

    // Contain AABB trees from scene
    RaycastManager& raycaster;
    // [UNITY] Use Unity PhysicsScene/Physics.Raycast to replicate this when migrating.

    // Define which gizmo open on the success
    unsigned char gizmo; // GLGizmosManager::EType
    // [STATE] Controls which GL gizmo will open after emboss creation (move/rotate/scale).

    // Volume define object to add new volume
    const GLVolume* gl_volume;

    // Wanted additionl move in Z(emboss) direction of new created volume
    // [STATE] Optional height offset applied when snapping the new volume to the surface.
    std::optional<float> distance = {};

    // Wanted additionl rotation around Z of new created volume
    // [STATE] Optional twist around the Z axis when placing the emboss.
    std::optional<float> angle = {};
};

/// <summary>
/// Create new volume on position of mouse cursor
/// </summary>
/// <param name="plater_ptr">canvas + camera + bed shape + </param>
/// <param name="data">Shape of emboss</param>
/// <param name="volume_type">New created volume type</param>
/// <param name="raycaster">Knows object in scene</param>
/// <param name="gizmo">Define which gizmo open on the success - enum GLGizmosManager::EType</param>
/// <param name="mouse_pos">Define position where to create volume</param>
/// <param name="distance">Wanted additionl move in Z(emboss) direction of new created volume</param>
/// <param name="angle">Wanted additionl rotation around Z of new created volume</param>
/// <returns>True on success otherwise False</returns>
/// [EVENT] Invoked by the emboss gizmo once the user clicks into the scene; enqueues a worker job carrying the mouse raycast results.
/// [PORTING_HAZARD:P3] Tightly coupled to `GLGizmosManager` and `RaycastManager`; Unity needs an InputSystem bridge + PhysicsScene to
/// emulate this flow. [UNITY] In Unity this could map to an async `JobHandle` spawned from a MonoBehaviour that uses `Physics.Raycast` and
/// feeds data into a `ScriptableObject` job descriptor.
bool start_create_volume(CreateVolumeParams& input, DataBasePtr data, const Vec2d& mouse_pos);

/// <summary>
/// Same as previous function but without mouse position
/// Need to suggest position or put near the selection
/// </summary>
/// [EVENT] Fired when emboss creation is triggered from menus/context actions instead of direct click; it will infer placement from the selection.
bool start_create_volume_without_position(CreateVolumeParams& input, DataBasePtr data);

/// <summary>
/// Start job for update embossed volume
/// </summary>
/// <param name="data">define update data</param>
/// <param name="volume">Volume to be updated</param>
/// <param name="selection">Keep model and gl_volumes - when start use surface volume must be selected</param>
/// <param name="raycaster">Could cast ray to scene</param>
/// <returns>True when start job otherwise false</returns>
/// [EVENT] Called when UI sliders or text edits mutate the emboss shape so a worker job can regenerate the mesh.
/// [UNITY] Map Selection to a serialized GUID and use `MainThreadDispatcher` to marshal the result back to the mono layer.
bool start_update_volume(DataUpdate&& data, const ModelVolume& volume, const Selection& selection, RaycastManager& raycaster);

} // namespace Slic3r::GUI::Emboss

#endif // slic3r_EmbossJob_hpp_
