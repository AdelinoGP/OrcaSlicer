#ifndef slic3r_GLGizmos_hpp_
#define slic3r_GLGizmos_hpp_

// [INTENT][EVENT][STATE][OPENGL][UNITY][PORTING_HAZARD:P2] Capture the cross-gizmo event vocabulary so the GL canvas -> gizmo dispatcher
// can make deterministic choices and carry modifier-state signals; Unity ports will re-express this as a shared C# enum consumed by a
// `GizmosInputBridge` MonoBehaviour that mirrors the GL canvas input pipeline and drag/selection state.
namespace Slic3r { namespace GUI {

enum class SLAGizmoEventType : unsigned char {
    LeftDown = 1,
    LeftUp,
    RightDown,
    Dragging,
    Delete,
    SelectAll,
    ShiftUp,
    AltUp,
    ApplyChanges,
    DiscardChanges,
    AutomaticGeneration,
    ManualEditing,
    MouseWheelUp,
    MouseWheelDown,
    ResetClippingPlane
};

}} // namespace Slic3r::GUI

// [INTENT][PORTING_HAZARD:P3][UNITY] This aggregator header forces every gizmo implementation along the SlaSupport/AdvancedCut path to
// compile at once; in Unity the equivalent is a registry or `ScriptableObject` list referenced by the `GizmosManager` MonoBehaviour so
// deferred assembly scanning keeps runtime declaration order intact.
#include "slic3r/GUI/Gizmos/GLGizmoMoveScale.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoRotate.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFlatten.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFdmSupports.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFuzzySkin.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.hpp"
// BBS
#include "slic3r/GUI/Gizmos/GLGizmoAdvancedCut.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoHollow.hpp"

#endif // slic3r_GLGizmos_hpp_
