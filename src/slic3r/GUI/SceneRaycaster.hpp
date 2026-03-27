#ifndef slic3r_SceneRaycaster_hpp_
#define slic3r_SceneRaycaster_hpp_

#include "MeshUtils.hpp"
#include "GLModel.hpp"
#include <vector>
#include <string>
#include <optional>

namespace Slic3r { namespace GUI {

struct Camera;

// [INTENT] Lightweight ownership wrapper for one selectable mesh source.
// [STATE] Keeps the stable picking id, active flag, back-face policy, and local transform used by the scene query.
// [UNITY] Map to a raycastable scene-entity record owned by a centralized picking service; keep the transform and id data in a shared model.
class SceneRaycasterItem
{
    int                  m_id{-1};
    bool                 m_active{true};
    bool                 m_use_back_faces{false};
    const MeshRaycaster* m_raycaster;
    Transform3d          m_trafo;

public:
    SceneRaycasterItem(int id, const MeshRaycaster& raycaster)
        : m_id(id), m_raycaster(&raycaster), m_trafo(Transform3d::Identity()), m_use_back_faces(false)
    {}
    SceneRaycasterItem(int id, const MeshRaycaster& raycaster, const Transform3d& trafo, bool use_back_faces = false)
        : m_id(id), m_raycaster(&raycaster), m_trafo(trafo), m_use_back_faces(use_back_faces)
    {}

    int                  get_id() const { return m_id; }
    bool                 is_active() const { return m_active; }
    void                 set_active(bool active) { m_active = active; }
    bool                 use_back_faces() const { return m_use_back_faces; }
    const MeshRaycaster* get_raycaster() const { return m_raycaster; }
    const Transform3d&   get_transform() const { return m_trafo; }
    void                 set_transform(const Transform3d& trafo) { m_trafo = trafo; }
};

// [INTENT] Maintain the GUI picking registry and resolve mouse hits across beds, volumes, gizmos, and fallback grabbers.
// [STATE] The four vectors encode hit-priority buckets; m_gizmos_on_top is a policy flag that short-circuits lower-priority searches.
// [EVENT] add/remove methods mutate the registry in response to scene lifecycle changes; hit() is the query entry point used by input
// handlers. [OPENGL] Debug-only members cache GL primitives for hit visualization, but the production path is CPU-side ray/triangle
// selection. [UNITY] Replace with a scene-picking service plus explicit priority buckets, fed by the input bridge and queried from the main
// thread. [PORTING_HAZARD:P2] The decoded-id scheme is tightly coupled to legacy picking ranges; Unity should preserve that mapping in one
// adapter, not scatter magic offsets. [UNCLEAR] Fallback gizmos are a second-order priority bucket; hypothesis: they exist to keep grab
// handles selectable after normal gizmo hits fail.
class SceneRaycaster
{
public:
    // [STATE] Type tags define the hit-priority class that drives bucket selection and encoded id ranges.
    enum class EType {
        None,
        Bed,
        Volume,
        Gizmo,
        FallbackGizmo // Is used for gizmo grabbers which will be hit after all grabbers of Gizmo type
    };

    // [STATE] Base ids reserve disjoint numeric ranges for each bucket so the decoded hit can be mapped back to its source.
    enum class EIdBase {
        Bed           = 0,
        Volume        = 1000, // Must be greater than PartPlateList::MAX_PLATES_COUNT * PartPlate::GRABBER_COUNT
        Gizmo         = 1000000,
        FallbackGizmo = 2000000
    };

    struct HitResult
    {
        // [STATE] Hit metadata carries the resolved bucket, local picking id, and world-space contact data.
        EType type{EType::None};
        int   raycaster_id{-1};
        Vec3f position{Vec3f::Zero()};
        Vec3f normal{Vec3f::Zero()};

        bool is_valid() const { return raycaster_id != -1; }
    };

private:
    // [STATE] Separate containers preserve deterministic priority between bed, volume, gizmo, and fallback-gizmo queries.
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_bed;
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_volumes;
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_gizmos;
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_fallback_gizmos;

    // When set to true, if checking gizmos returns a valid hit,
    // the search is not performed on other types
    bool m_gizmos_on_top{false};

#if ENABLE_RAYCAST_PICKING_DEBUG
    // [OPENGL] Debug overlay geometry visualizes the ray-hit sphere and normal line for inspection.
    GLModel                  m_sphere;
    GLModel                  m_line;
    std::optional<HitResult> m_last_hit;
#endif // ENABLE_RAYCAST_PICKING_DEBUG

public:
    // [INTENT] Start with empty picking buckets and default policy flags.
    SceneRaycaster();

    // [EVENT] Registry mutation API used by scene setup/teardown code to keep picking data in sync with live objects.
    std::shared_ptr<SceneRaycasterItem> add_raycaster(
        EType type, int picking_id, const MeshRaycaster& raycaster, const Transform3d& trafo, bool use_back_faces = false);
    void remove_raycasters(EType type, int id);
    void remove_raycasters(EType type);
    void remove_raycaster(std::shared_ptr<SceneRaycasterItem> item);

    // [STATE] Direct accessors expose the bucket ownership model to callers that need to inspect or prune items in place.
    std::vector<std::shared_ptr<SceneRaycasterItem>>*       get_raycasters(EType type);
    const std::vector<std::shared_ptr<SceneRaycasterItem>>* get_raycasters(EType type) const;

    void set_gizmos_on_top(bool value) { m_gizmos_on_top = value; }

    // [INTENT] Perform prioritized hit-testing from a 2D mouse position through the camera into the registry buckets.
    HitResult hit(const Vec2d& mouse_pos, const Camera& camera, const ClippingPlane* clipping_plane = nullptr) const;

#if ENABLE_RAYCAST_PICKING_DEBUG
    // [OPENGL] Render the last resolved hit state for debugging and ray-picking validation.
    void render_hit(const Camera& camera);

    size_t beds_count() const { return m_bed.size(); }
    size_t volumes_count() const { return m_volumes.size(); }
    size_t gizmos_count() const { return m_gizmos.size(); }
    size_t fallback_gizmos_count() const { return m_fallback_gizmos.size(); }
    size_t active_beds_count() const;
    size_t active_volumes_count() const;
    size_t active_gizmos_count() const;
    size_t active_fallback_gizmos_count() const;
#endif // ENABLE_RAYCAST_PICKING_DEBUG

    // [INTENT] Encode/decode the legacy picking ids so the caller can identify both bucket and local object id.
    static int decode_id(EType type, int id);

private:
    // [INTENT] Reserve bucket-specific numeric ranges to avoid collisions between beds, volumes, and gizmo handles.
    static int encode_id(EType type, int id);
    static int base_id(EType type);
};

}} // namespace Slic3r::GUI

#endif // slic3r_SceneRaycaster_hpp_
