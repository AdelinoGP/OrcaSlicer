#ifndef slic3r_CameraUtils_hpp_
#define slic3r_CameraUtils_hpp_

#include "Camera.hpp"
#include "libslic3r/Point.hpp"
namespace Slic3r {
class GLVolume;
}

namespace Slic3r::GUI {

/*
 [INTENT]
 CameraUtils provides static math helpers for coordinate space transformations (3D to 2D Screen, Screen to 3D Ray).
 It decouples the raw Camera data from the geometric operations used for selection, hit-testing, and HUD placement.

 [STATE]
 Stateless; all functions are static and operate on the provided Camera instance.

 [UNITY]
 Most of these functions map directly to Unity's Camera API:
 - project() -> Camera.WorldToScreenPoint()
 - ray_from_screen_pos() -> Camera.ScreenPointToRay()
 - get_z0_position() -> Plane.Raycast(new Plane(Vector3.up, 0))

 [PORTING_HAZARD:P2]
 OrcaSlicer uses a right-handed coordinate system (OpenGL-style) where Z is up (in some contexts) or out.
 Unity uses a left-handed coordinate system where Y is up.
 Coordinate transformations must be carefully audited during porting to ensure consistent world-space orientation.
*/
class CameraUtils
{
public:
    CameraUtils() = delete; // only static functions

    /// <summary>
    /// Project point throw camera to 2d coordinate into imgui window
    /// </summary>
    /// <param name="camera">Projection params</param>
    /// <param name="points">Point to project.</param>
    /// <returns>projected points by camera into coordinate of camera.
    /// x(from left to right), y(from top to bottom)</returns>
    static Points        project(const Camera& camera, const std::vector<Vec3d>& points);
    static Slic3r::Point project(const Camera& camera, const Vec3d& point);

    /// <summary>
    /// Create hull around GLVolume in 2d space of camera
    /// </summary>
    /// <param name="camera">Projection params</param>
    /// <param name="volume">Outline by 3d object</param>
    /// <returns>Polygon around object</returns>
    static Polygon create_hull2d(const Camera& camera, const GLVolume& volume);

    /// <summary>
    /// Create ray(point and direction) for screen coordinate
    /// </summary>
    /// <param name="camera">Definition of camera</param>
    /// <param name="position">Position on screen(aka mouse position) </param>
    /// <param name="point">OUT start of ray</param>
    /// <param name="direction">OUT direction of ray</param>
    static void ray_from_screen_pos(const Camera& camera, const Vec2d& position, Vec3d& point, Vec3d& direction);
    static void ray_from_ortho_screen_pos(const Camera& camera, const Vec2d& position, Vec3d& point, Vec3d& direction);
    static void ray_from_persp_screen_pos(const Camera& camera, const Vec2d& position, Vec3d& point, Vec3d& direction);

    /// <summary>
    /// Unproject mouse coordinate to get position in space where z coor is zero
    /// Platter surface should be in z == 0
    /// </summary>
    /// <param name="camera">Projection params</param>
    /// <param name="coor">Mouse position</param>
    /// <returns>Position on platter under mouse</returns>
    static Vec2d get_z0_position(const Camera& camera, const Vec2d& coor);

    /// <summary>
    /// Create 3d screen point from 2d position
    /// </summary>
    /// <param name="camera">Define camera viewport</param>
    /// <param name="position">Position on screen(aka mouse position)</param>
    /// <returns>Point represented screen coor in 3d</returns>
    static Vec3d screen_point(const Camera& camera, const Vec2d& position);
};
} // namespace Slic3r::GUI

#endif /* slic3r_CameraUtils_hpp_ */
