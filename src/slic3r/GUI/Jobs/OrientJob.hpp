// [INTENT]
// This header declares the `OrientJob` class, a background job responsible for
// automatically orienting objects on the print bed to find a stable printing
// orientation. It uses algorithms from `libslic3r/Orient.hpp` to analyze the
// geometry of the objects and determine the best way to place them.
//
// The job separates objects into selected, unselected, and unprintable groups,
// processes them in a worker thread, and then applies the new orientations in
// the main UI thread.
//
// [THREAD]
// - This job runs as a background task. The `process` method is executed in a
//   separate thread, and the `finalize` method is called on the main UI thread
//   to update the UI/scene state.
//
// [UNITY]
// In a Unity port, this functionality would be handled by a C# script that
// triggers an async Task or a C# Job.
// - The `prepare_...` methods would collect the `GameObject`s and their `Mesh`
//   data from the scene.
// - The `process` method logic, which calls the core orientation algorithm, would
//   be ported to C# and run in a background job. The underlying geometry
//   analysis would need to be ported or replaced with a Unity-compatible library.
// - The `finalize` method would be a callback on the main thread that applies
//   the resulting rotation to the `Transform` of each `GameObject`.
//
// [PORTING_HAZARD:P2]
// - The C++ orientation algorithms (`libslic3r/Orient.hpp`) must be completely
//   replaced or re-implemented in C# to run within Unity, as they depend on
//   libslic3r data structures that aren't natively supported in Unity.

#ifndef ORIENTJOB_HPP
#define ORIENTJOB_HPP

#include "Job.hpp"
#include "libslic3r/Orient.hpp"

namespace Slic3r {

class ModelObject;

namespace GUI {

class Plater;

class OrientJob : public Job
{
    using OrientMesh  = orientation::OrientMesh;
    using OrientMeshs = orientation::OrientMeshs;

    // [STATE] The sets of meshes to be processed, separated into selected,
    // unselected, and unprintable groups.
    OrientMeshs m_selected, m_unselected, m_unprintable;
    // [STATE] A pointer to the Plater, providing access to the model and UI.
    Plater* m_plater;

    // [INTENT] Clears the input mesh groups to prepare for a new orientation operation.
    void clear_input();

    // [INTENT] Prepares the selection of objects to be oriented.
    // [PARAM] obj_sel: A boolean vector indicating which objects are selected.
    // [PARAM] only_one_plate: If true, only orient objects on the current plate.
    void prepare_selection(std::vector<bool> obj_sel, bool only_one_plate);

    // [INTENT] Prepares the selected and unselected items separately. If nothing is
    // selected, it treats all printable objects as selected.
    void prepare_selected();

    // [INTENT] Prepares the items from the currently selected part plate for orientation.
    void prepare_partplate();

public:
    // [INTENT] Prepares the data for the orientation job. This is the main entry
    // point for the preparation phase.
    void prepare();

    void process(Ctl& ctl) override;

    // [INTENT] Constructs a new OrientJob.
    OrientJob();

    void finalize(bool canceled, std::exception_ptr& e) override;
#if 0
    static
    orientation::OrientMesh get_orient_mesh(ModelObject* obj, const Plater* plater)
    {
        using OrientMesh = orientation::OrientMesh;
        OrientMesh om;
        om.name = obj->name;
        om.mesh = obj->mesh(); // don't know the difference to obj->raw_mesh(). Both seem OK
        om.setter = [obj, plater](const OrientMesh& p) {
            obj->rotate(p.angle, p.axis);
            obj->ensure_on_bed();
        };
        return om;
    }
#endif
    // [INTENT] A helper function to create an `OrientMesh` from a `ModelInstance`.
    // It extracts the necessary mesh data and creates a setter lambda to apply
    // the orientation result back to the instance.
    static orientation::OrientMesh get_orient_mesh(ModelInstance* instance);
};

} // namespace GUI
} // namespace Slic3r

#endif // ORIENTJOB_HPP
