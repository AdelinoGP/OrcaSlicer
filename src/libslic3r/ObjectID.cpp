// [INTENT] Implements the global ObjectBase ID counter and the two sentinel
// ObjectIDs for the wipe tower (object-level and instance-level).
// Every ObjectBase constructed atomically increments s_last_id, giving each
// model object a process-unique identity that survives undo/redo serialization.
// [STATE] s_last_id: process-global monotonically-increasing counter; NOT thread-safe
// (increment is not atomic). ObjectWithTimestamp::s_last_timestamp: similar counter
// for change-tracking timestamps, initialized to 1 (0 reserved as "unset").
// [HAZARD H1174] wipe_tower_object_id() and wipe_tower_instance_id() each declare a
// local `static ObjectBase mine` — two SEPARATE static instances, both named `mine`.
// Their IDs are assigned at first call from s_last_id and then permanently stable.
// Any port must ensure these two IDs remain distinct and stable within a session.
// [COUPLING] s_last_id is shared with every ObjectBase subclass across the codebase
// (Model, ModelObject, ModelInstance, etc.). Counter collisions would corrupt the
// undo/redo diff system.
#include "ObjectID.hpp"

namespace Slic3r {

size_t ObjectBase::s_last_id = 0;

// Unique object / instance ID for the wipe tower.
ObjectID wipe_tower_object_id()
{
    static ObjectBase mine;
    return mine.id();
}

ObjectID wipe_tower_instance_id()
{
    static ObjectBase mine;
    return mine.id();
}

ObjectWithTimestamp::Timestamp ObjectWithTimestamp::s_last_timestamp = 1;

} // namespace Slic3r

// CEREAL_REGISTER_TYPE(Slic3r::ObjectBase)
