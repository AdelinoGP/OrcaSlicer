#ifndef slic3r_GUI_Geometry_hpp_
#define slic3r_GUI_Geometry_hpp_

namespace Slic3r {
namespace GUI {

// [INTENT] Classify the coordinate basis that child manipulators and gizmo maths interpret (world vs object vs local) so cursor deltas know
// how to map into Transform semantics. [UNITY] Map to Unity's `Transform` component by translating `World` to `transform` matrices +
// `TransformPoint`, `Instance` to parent-object space, and `Local` to `transform.localPosition`/`localRotation` usage.
enum class ECoordinatesType : unsigned char { World = 0, Instance, Local };

// [INTENT] Maintain a transformation-mode bitmask describing coordinate space, relative/absolute toggles, and group behavior so UI
// manipulators share the same context. [UNITY] Mirror this mask with a MonoBehaviour that tracks `TransformSpace` (World/Local),
// `isRelative`, and `groupIndependent` flags. [THREAD] Manipulators mutate this bitmask through wxWidgets event callbacks on the UI thread
// while the render loop reads it so we avoid cross-thread races.
class TransformationType
{
public:
    enum Enum {
        // Transforming in a world coordinate system
        World = 0,
        // Transforming in a instance coordinate system
        Instance = 1,
        // Transforming in a local coordinate system
        Local = 2,
        // Absolute transformations, allowed in local coordinate system only.
        // [PORTING_HAZARD:P3] Shares the zero value with `World`, so detecting 'absolute' relies on Relative being cleared instead of a
        // dedicated flag.
        Absolute = 0,
        // Relative transformations, allowed in both local and world coordinate system.
        Relative = 4,
        // For group selection, the transformation is performed as if the group made a single solid body.
        Joint = 0,
        // For group selection, the transformation is performed on each object independently.
        Independent = 8,

        World_Relative_Joint          = World | Relative | Joint,
        World_Relative_Independent    = World | Relative | Independent,
        Instance_Absolute_Joint       = Instance | Absolute | Joint,
        Instance_Absolute_Independent = Instance | Absolute | Independent,
        Instance_Relative_Joint       = Instance | Relative | Joint,
        Instance_Relative_Independent = Instance | Relative | Independent,
        Local_Absolute_Joint          = Local | Absolute | Joint,
        Local_Absolute_Independent    = Local | Absolute | Independent,
        Local_Relative_Joint          = Local | Relative | Joint,
        Local_Relative_Independent    = Local | Relative | Independent,
    };

    TransformationType() : m_value(World) {}
    TransformationType(Enum value) : m_value(value) {}
    TransformationType& operator=(Enum value)
    {
        m_value = value;
        return *this;
    }

    Enum operator()() const { return m_value; }
    bool has(Enum v) const { return ((unsigned int) m_value & (unsigned int) v) != 0; }

    void set_world()
    {
        this->remove(Instance);
        this->remove(Local);
    }
    void set_instance()
    {
        this->remove(Local);
        this->add(Instance);
    }
    void set_local()
    {
        this->remove(Instance);
        this->add(Local);
    }
    void set_absolute() { this->remove(Relative); }
    void set_relative() { this->add(Relative); }
    void set_joint() { this->remove(Independent); }
    void set_independent() { this->add(Independent); }

    // [STATE] These setters keep the bitmask consistent so downstream rendering/selection logic reads the same coordinate and group flags.
    // [EVENT] Toggle events (toolbar buttons, hotkeys, context menu picks) call these helpers so the change propagates to gizmo/selection
    // controllers and the render pipeline together.
    // [UNITY] Wire the toggles to UI Toolkit `Toggle` callbacks inside a shared MonoBehaviour before issuing `Transform` adjustments.

    // [STATE] Accessors that let the canvas controller branch on the active coordinate, relativity, and group modes.
    bool world() const { return !this->has(Instance) && !this->has(Local); }
    bool instance() const { return this->has(Instance); }
    bool local() const { return this->has(Local); }
    bool absolute() const { return !this->has(Relative); }
    bool relative() const { return this->has(Relative); }
    bool joint() const { return !this->has(Independent); }
    bool independent() const { return this->has(Independent); }

private:
    void add(Enum v) { m_value = Enum((unsigned int) m_value | (unsigned int) v); }
    void remove(Enum v) { m_value = Enum((unsigned int) m_value & (~(unsigned int) v)); }

    // [STATE] Current flag combination; [PORTING_HAZARD:P3] coordinate/relativity/group bits share a single mask, so Unity must keep all
    // bits aligned. [OPENGL] The GL render loop reads this mask to configure which matrix stack and axis the drawing code uses for gizmos.
    Enum m_value;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Geometry_hpp_
