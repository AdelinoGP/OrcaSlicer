// [INTENT]
// This header declares the `FillBedJob` class, a background job for
// automatically duplicating a selected object to fill the print bed. It defines
// the data structures used to hold the state of the arrangement process and
// the interface for the job's three main stages: prepare, process, and finalize.
//
// [UNITY]
// In a Unity port, this class would not have a direct equivalent. Its role would
// be fulfilled by a C# script that manages the state of the fill-bed operation
// and launches the corresponding C# Job or async Task. The member variables
// would become fields in that C# class.

#ifndef FILLBEDJOB_HPP
#define FILLBEDJOB_HPP

#include "ArrangeJob.hpp"

namespace Slic3r { namespace GUI {

class Plater;

class FillBedJob : public Job
{
    // [STATE] Index of the selected object in the model.
    int m_object_idx = -1;

    using ArrangePolygon  = arrangement::ArrangePolygon;
    using ArrangePolygons = arrangement::ArrangePolygons;

    // [STATE] Footprints of the objects to be duplicated and arranged.
    ArrangePolygons m_selected;
    // [STATE] Footprints of other movable objects on the bed.
    ArrangePolygons m_unselected;
    // BBS: add partplate related logic
    //  [STATE] Footprints of locked objects that cannot be moved.
    ArrangePolygons m_locked;
    ;

    // [STATE] The 2D contour of the print bed.
    Points m_bedpts;

    // [STATE] Parameters for the arrangement algorithm (e.g., object spacing).
    arrangement::ArrangeParams params;

    // [STATE] The total number of items to be arranged, used for progress reporting.
    int m_status_range = 0;
    // [STATE] A pointer to the Plater, providing access to the model, config, etc.
    Plater* m_plater;

    // [STATE] If true, create new instances of the same object. If false, create new objects.
    bool m_instances;

public:
    void prepare();
    void process(Ctl& ctl) override;

    // [INTENT] Constructs a new FillBedJob.
    // [PARAM] instances: If true, new instances of the selected object will be
    // created. If false, new objects will be created.
    FillBedJob(bool instances = false);

    int status_range() const { return m_status_range; }

    void finalize(bool canceled, std::exception_ptr& e) override;
};

}} // namespace Slic3r::GUI

#endif // FILLBEDJOB_HPP
