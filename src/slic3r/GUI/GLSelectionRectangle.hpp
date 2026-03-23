#ifndef slic3r_GLSelectionRectangle_hpp_
#define slic3r_GLSelectionRectangle_hpp_

#include "libslic3r/Point.hpp"
#include "GLModel.hpp"

namespace Slic3r { namespace GUI {

struct Camera;
class GLCanvas3D;

// [INTENT] Model the viewport drag-selection rectangle so the GUI can make bulk selection/deselection decisions quickly.
// [UNITY] Mirror this as a dedicated overlay control (Canvas + GraphicRaycaster) that drives a `LineRenderer` or `UI Toolkit` VisualElement
// border plus pointer hit testing while using the Input System for drag events.
class GLSelectionRectangle
{
public:
    enum EState { Off, Select, Deselect };

    // Initiates the rectangle.
    // [EVENT] Mouse-down sequence that records the start corner and desired select/deselect intent; [STATE] flips `m_state` to make
    // `is_dragging()` true; [THREAD] must run on the UI thread.
    void start_dragging(const Vec2d& mouse_position, EState state);

    // To be called on mouse move.
    // [EVENT] Mouse-move updates the opposite corner while keeping the rectangle in sync with the cursor.
    void dragging(const Vec2d& mouse_position);

    // Given a vector of points in world coordinates, the function returns indices of those
    // that are in the rectangle.
    // [INTENT] Project world-space points into the current screen rectangle to produce selection indices; [STATE] expects `m_state` to
    // reflect Select or Deselect and `m_rectangle` to match screen bounds. [PORTING_HAZARD:P2] Relies on GLModel canvas transforms, which
    // Unity must remap via camera.ScreenToWorldPoint + mesh colliders.
    std::vector<unsigned int> contains(const std::vector<Vec3d>& points) const;

    // Disables the rectangle.
    // [EVENT] Mouse-up or cancel semantics reset the drag state and release GL resources so subsequent renders stay clean.
    void stop_dragging();

    // [OPENGL] Draws the selection rectangle using a cached GLModel mesh; [THREAD] called from the GLCanvas3D render loop, so it is
    // confined to the GL context thread. [UNITY] Equivalent to rendering the rectangle via a `LineRenderer`/`GL.LINES` mesh on an overlay
    // camera targeting a `RenderTexture` or dedicated UI layer. [PORTING_HAZARD:P3] Relies on legacy GLModel state and immediate-mode
    // assumptions that Unity must rewrite as buffered geometry updates.
    void render(const GLCanvas3D& canvas);

    bool   is_dragging() const { return m_state != Off; }
    EState get_state() const { return m_state; }

    float get_width() const { return std::abs(m_start_corner.x() - m_end_corner.x()); }
    float get_height() const { return std::abs(m_start_corner.y() - m_end_corner.y()); }
    float get_left() const { return std::min(m_start_corner.x(), m_end_corner.x()); }
    float get_right() const { return std::max(m_start_corner.x(), m_end_corner.x()); }
    float get_top() const { return std::max(m_start_corner.y(), m_end_corner.y()); }
    float get_bottom() const { return std::min(m_start_corner.y(), m_end_corner.y()); }

private:
    // [STATE] Tracks whether the rectangle is idle or performing a select/deselect drag so callers can gate `contains()`.
    EState m_state{Off};
    // [STATE] Screen-space start/end corners that drive width/height queries and selection logic.
    Vec2d m_start_corner{Vec2d::Zero()};
    Vec2d m_end_corner{Vec2d::Zero()};
    // [OPENGL] Cached mesh representing the dashed rectangle and fill; reused across renders to avoid rebuilding GL buffers each frame.
    // [UNITY] This maps to a cached `Mesh` shared by the overlay LineRenderer or `UI Toolkit` border geometry.
    // [PORTING_HAZARD:P2] GLModel is tightly coupled with wxWidgets GLUT context; Unity requires a separate render pipeline for overlays.
    GLModel m_rectangle;
    // [STATE] Previous corners used to detect when the rectangle changed and update the mesh lazily.
    Vec2d m_old_start_corner{Vec2d::Zero()};
    Vec2d m_old_end_corner{Vec2d::Zero()};
};

}} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoSlaSupports_hpp_
