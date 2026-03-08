// [INTENT] Implementation of ExtrusionEntityCollection operations: deep-copy, clear/delete,
//          role-filtering, seam-ordered chaining (chained_path_from), and hierarchical
//          flattening. This is the central "container" implementation for the toolpath tree.
// [MEMORY] clear() iterates entities and calls `delete` on every pointer. This is the sole
//          deallocation point for owned entities. operator= calls clear() before re-cloning.
// [COUPLING] Depends on ShortestPath.hpp for chain_and_reorder_extrusion_entities().
//            chained_path_from() always clones the filtered entities — callers get a new
//            independent collection.
#include "ExtrusionEntityCollection.hpp"
#include "ShortestPath.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace Slic3r {

void filter_by_extrusion_role_in_place(ExtrusionEntitiesPtr& extrusion_entities, ExtrusionRole role)
{
    if (role != erMixed) {
        auto first = extrusion_entities.begin();
        auto last  = extrusion_entities.end();
        extrusion_entities.erase(std::remove_if(first, last, [&role](const ExtrusionEntity* ee) { return ee->role() != role; }), last);
    }
}

ExtrusionEntityCollection::ExtrusionEntityCollection(const ExtrusionPaths& paths) : no_sort(false) { this->append(paths); }

ExtrusionEntityCollection& ExtrusionEntityCollection::operator=(const ExtrusionEntityCollection& other)
{
    clear();
    this->entities = other.entities;
    for (size_t i = 0; i < this->entities.size(); ++i)
        this->entities[i] = this->entities[i]->clone();
    this->no_sort = other.no_sort;
    return *this;
}

void ExtrusionEntityCollection::swap(ExtrusionEntityCollection& c)
{
    std::swap(this->entities, c.entities);
    std::swap(this->no_sort, c.no_sort);
}

void ExtrusionEntityCollection::clear()
{
    for (size_t i = 0; i < this->entities.size(); ++i)
        delete this->entities[i];
    this->entities.clear();
}

ExtrusionEntityCollection::operator ExtrusionPaths() const
{
    ExtrusionPaths paths;
    for (const ExtrusionEntity* ptr : this->entities) {
        if (const ExtrusionPath* path = dynamic_cast<const ExtrusionPath*>(ptr))
            paths.push_back(*path);
    }
    return paths;
}

ExtrusionEntity* ExtrusionEntityCollection::clone() const { return new ExtrusionEntityCollection(*this); }

void ExtrusionEntityCollection::reverse()
{
    for (ExtrusionEntity* ptr : this->entities)
        // Don't reverse it if it's a loop, as it doesn't change anything in terms of elements ordering
        // and caller might rely on winding order
        if (!ptr->is_loop())
            ptr->reverse();
    std::reverse(this->entities.begin(), this->entities.end());
}

void ExtrusionEntityCollection::replace(size_t i, const ExtrusionEntity& entity)
{
    delete this->entities[i];
    this->entities[i] = entity.clone();
}

void ExtrusionEntityCollection::remove(size_t i)
{
    delete this->entities[i];
    this->entities.erase(this->entities.begin() + i);
}

// [INTENT] chained_path_from: filter entities by role, clone them, then greedily reorder
//          starting near `start_near` to minimise travel moves. Returns a fully independent
//          collection (all pointers are fresh clones — caller owns them).
// [STATE]  out.entities is built from a shallow filter_by_extrusion_role result, then
//          each pointer is replaced by its clone before reordering. No raw-pointer aliasing
//          remains after this function returns.
// [MEMORY] Every element in the returned collection is a heap-allocated clone. The caller
//          (or the returned object's destructor) is responsible for deletion.
// [CONCURRENCY] Stateless static method — thread-safe as long as `extrusion_entities`
//               elements are not concurrently modified.
// [COUPLING] Delegates ordering to chain_and_reorder_extrusion_entities() (ShortestPath.hpp),
//            which uses a greedy nearest-neighbour O(n²) algorithm.
// [HAZARD H737] filter_by_extrusion_role returns a vector of raw pointers into the original
//               collection. The subsequent clone-loop replaces each pointer, but if an
//               exception is thrown mid-loop the already-cloned objects leak and the
//               remaining slots still alias the originals. No RAII guard exists here.
ExtrusionEntityCollection ExtrusionEntityCollection::chained_path_from(const ExtrusionEntitiesPtr& extrusion_entities,
                                                                       const Point&                start_near,
                                                                       ExtrusionRole               role)
{
    // Return a filtered copy of the collection.
    ExtrusionEntityCollection out;
    out.entities = filter_by_extrusion_role(extrusion_entities, role);
    // Clone the extrusion entities.
    for (auto& ptr : out.entities)
        ptr = ptr->clone();
    chain_and_reorder_extrusion_entities(out.entities, &start_near);
    return out;
}

void ExtrusionEntityCollection::polygons_covered_by_width(Polygons& out, const float scaled_epsilon) const
{
    for (const ExtrusionEntity* entity : this->entities)
        entity->polygons_covered_by_width(out, scaled_epsilon);
}

void ExtrusionEntityCollection::polygons_covered_by_spacing(Polygons& out, const float scaled_epsilon) const
{
    for (const ExtrusionEntity* entity : this->entities)
        entity->polygons_covered_by_spacing(out, scaled_epsilon);
}

// Recursively count paths and loops contained in this collection.
size_t ExtrusionEntityCollection::items_count() const
{
    size_t count = 0;
    for (const ExtrusionEntity* entity : this->entities)
        if (entity->is_collection())
            count += static_cast<const ExtrusionEntityCollection*>(entity)->items_count();
        else
            ++count;
    return count;
}

// Returns a single vector of pointers to all non-collection items contained in this one.
ExtrusionEntityCollection ExtrusionEntityCollection::flatten(bool preserve_ordering) const
{
    struct Flatten
    {
        Flatten(bool preserve_ordering) : preserve_ordering(preserve_ordering) {}
        ExtrusionEntityCollection out;
        bool                      preserve_ordering;
        void                      recursive_do(const ExtrusionEntityCollection& collection)
        {
            if (collection.no_sort && preserve_ordering) {
                // Don't flatten whatever happens below this level.
                out.append(collection);
            } else {
                for (const ExtrusionEntity* entity : collection.entities)
                    if (entity->is_collection())
                        this->recursive_do(*static_cast<const ExtrusionEntityCollection*>(entity));
                    else
                        out.append(*entity);
            }
        }
    } flatten(preserve_ordering);

    flatten.recursive_do(*this);
    return flatten.out;
}

double ExtrusionEntityCollection::min_mm3_per_mm() const
{
    double min_mm3_per_mm = std::numeric_limits<double>::max();
    for (const ExtrusionEntity* entity : this->entities)
        min_mm3_per_mm = std::min(min_mm3_per_mm, entity->min_mm3_per_mm());
    return min_mm3_per_mm;
}

} // namespace Slic3r
