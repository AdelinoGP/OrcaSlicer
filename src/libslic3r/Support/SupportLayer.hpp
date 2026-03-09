// [INTENT] Declares the shared data types used throughout the normal and tree support
// generation pipelines:
//   - SupporLayerType enum (generation-time layer classification)
//   - SupportGeneratorLayer (mutable working layer holding geometry + metadata)
//   - SupportGeneratorLayerStorage (mutex-guarded arena allocator backed by std::deque)
//   - SupportGeneratorLayersPtr (std::vector of raw pointers into the arena)
//
// [MEMORY] All SupportGeneratorLayer objects are arena-allocated from
// SupportGeneratorLayerStorage (backed by Slic3r::deque with TBB scalable allocator).
// Because std::deque (and Slic3r::deque) never invalidates references or pointers to
// existing elements on push_back, raw pointers into the deque are stable for the
// lifetime of the storage object.
//
// [CONCURRENCY] Two allocation paths exist:
//   - allocate_unguarded(): no lock — caller must ensure serial access
//   - allocate(): acquires tbb::spin_mutex — safe for concurrent TBB workers
// [HAZARD] allocate() uses manual lock()/unlock() with NO RAII guard (H892).
//   An exception in emplace_back() (e.g. std::bad_alloc) leaves the mutex permanently
//   locked, deadlocking all subsequent allocate() callers.
//
// [HAZARD] The enum name "SupporLayerType" is a typo (missing 't'). This typo is the
//   canonical definition used across the entire support subsystem (H865, H891).
#ifndef slic3r_SupportLayer_hpp_
#define slic3r_SupportLayer_hpp_

#include <oneapi/tbb/scalable_allocator.h>
#include <oneapi/tbb/spin_mutex.h>
// for Slic3r::deque
#include "../libslic3r.h"
#include "../ClipperUtils.hpp"
#include "../Polygon.hpp"

namespace Slic3r {

// [INTENT] Fine-grained layer type enum used during support generation to distinguish
// raft, contact, interface, base, and intermediate layers. This is distinct from the
// final SupportLayer type stored in PrintObject after generation is complete.
// [HAZARD] Typo in enum name: "SupporLayerType" should be "SupportLayerType" — the
//   missing 't' is propagated through every support file that references this type (H891).
enum class SupporLayerType {
    Unknown = 0,
    // Ratft base layer, to be printed with the support material.
    RaftBase,
    // Raft interface layer, to be printed with the support interface material.
    RaftInterface,
    // Bottom contact layer placed over a top surface of an object. To be printed with a support interface material.
    BottomContact,
    // Dense interface layer, to be printed with the support interface material.
    // This layer is separated from an object by an BottomContact layer.
    BottomInterface,
    // Sparse base support layer, to be printed with a support material.
    Base,
    // Dense interface layer, to be printed with the support interface material.
    // This layer is separated from an object with TopContact layer.
    TopInterface,
    // Top contact layer directly supporting an overhang. To be printed with a support interface material.
    TopContact,
    // Some undecided type yet. It will turn into Base first, then it may turn into BottomInterface or TopInterface.
    Intermediate,
};

// [INTENT] Mutable working layer object used during support generation. Holds all geometry
// and metadata for a single support layer slice at a specific Z height. After generation
// completes, these objects are consumed to build the final SupportLayer structures stored
// in PrintObject.
//
// [STATE] Fields are written in multiple phases:
//   Phase 1 (allocate):      layer_type, print_z, bottom_z, height set
//   Phase 2 (fill contact):  polygons, contact_polygons, overhang_polygons, enforcer_polygons set
//   Phase 3 (trim / merge):  polygons may be further modified by diff/trim passes
//
// [HAZARD] operator== only compares print_z, height, and bridging — ignores all polygon
//   fields and layer_type (H893). Two layers with identical Z but different geometry are
//   considered equal.
class SupportGeneratorLayer
{
public:
    void reset() { *this = SupportGeneratorLayer(); }

    // [HAZARD] Partial equality — only print_z, height, bridging are compared.
    // layer_type, polygon fields, and idx_object_layer_* are ignored (H893).
    bool operator==(const SupportGeneratorLayer& layer2) const
    {
        return print_z == layer2.print_z && height == layer2.height && bridging == layer2.bridging;
    }

    // Order the layers by lexicographically by an increasing print_z and a decreasing layer height.
    bool operator<(const SupportGeneratorLayer& layer2) const
    {
        if (print_z < layer2.print_z) {
            return true;
        } else if (print_z == layer2.print_z) {
            if (height > layer2.height)
                return true;
            else if (height == layer2.height) {
                // Bridging layers first.
                return bridging && !layer2.bridging;
            } else
                return false;
        } else
            return false;
    }

    void merge(SupportGeneratorLayer&& rhs)
    {
        // The union_() does not support move semantic yet, but maybe one day it will.
        this->polygons = union_(this->polygons, std::move(rhs.polygons));
        auto merge     = [](std::unique_ptr<Polygons>& dst, std::unique_ptr<Polygons>& src) {
            if (!dst || dst->empty())
                dst = std::move(src);
            else if (src && !src->empty())
                *dst = union_(*dst, std::move(*src));
        };
        merge(this->contact_polygons, rhs.contact_polygons);
        merge(this->overhang_polygons, rhs.overhang_polygons);
        merge(this->enforcer_polygons, rhs.enforcer_polygons);
        rhs.reset();
    }

    // For the bridging flow, bottom_print_z will be above bottom_z to account for the vertical separation.
    // For the non-bridging flow, bottom_print_z will be equal to bottom_z.
    coordf_t bottom_print_z() const { return print_z - height; }

    // To sort the extremes of top / bottom interface layers.
    coordf_t extreme_z() const { return (this->layer_type == SupporLayerType::TopContact) ? this->bottom_z : this->print_z; }

    SupporLayerType layer_type{SupporLayerType::Unknown};
    // Z used for printing, in unscaled coordinates.
    coordf_t print_z{0};
    // Bottom Z of this layer. For soluble layers, bottom_z + height = print_z,
    // otherwise bottom_z + gap + height = print_z.
    coordf_t bottom_z{0};
    // Layer height in unscaled coordinates.
    coordf_t height{0};
    // Index of a PrintObject layer_id supported by this layer. This will be set for top contact layers.
    // If this is not a contact layer, it will be set to size_t(-1).
    size_t idx_object_layer_above{size_t(-1)};
    // Index of a PrintObject layer_id, which supports this layer. This will be set for bottom contact layers.
    // If this is not a contact layer, it will be set to size_t(-1).
    size_t idx_object_layer_below{size_t(-1)};
    // Use a bridging flow when printing this support layer.
    bool bridging{false};

    // Polygons to be filled by the support pattern.
    Polygons polygons;
    // Currently for the contact layers only.
    std::unique_ptr<Polygons> contact_polygons;
    std::unique_ptr<Polygons> overhang_polygons;
    // Enforcers need to be propagated independently in case the "support on build plate only" option is enabled.
    std::unique_ptr<Polygons> enforcer_polygons;
};

// [INTENT] Thread-safe arena allocator for SupportGeneratorLayer objects.
// Backed by a Slic3r::deque (itself backed by TBB scalable_allocator) so that
// push_back never invalidates existing pointers — raw pointers into the deque
// remain valid for the lifetime of this storage object.
//
// [CONCURRENCY] Two allocation methods:
//   allocate_unguarded() — no mutex, caller is responsible for single-thread access
//   allocate()           — acquires tbb::spin_mutex for TBB parallel use
//
// [HAZARD] allocate() uses manual lock()/unlock() — no RAII guard (H892).
//   If emplace_back() throws std::bad_alloc, the spin_mutex is never released
//   and the storage object becomes permanently deadlocked.
class SupportGeneratorLayerStorage
{
public:
    // [INTENT] Allocate without locking. Safe only from a single thread.
    SupportGeneratorLayer& allocate_unguarded(SupporLayerType layer_type)
    {
        m_storage.emplace_back();
        m_storage.back().layer_type = layer_type;
        return m_storage.back();
    }

    // [INTENT] Allocate under mutex protection. Safe for TBB worker threads.
    // [HAZARD] No RAII guard — exception in emplace_back() permanently
    //   deadlocks this storage object (H892).
    SupportGeneratorLayer& allocate(SupporLayerType layer_type)
    {
        m_mutex.lock();
        m_storage.emplace_back();
        SupportGeneratorLayer* layer_new = &m_storage.back();
        m_mutex.unlock();
        layer_new->layer_type = layer_type;
        return *layer_new;
    }

private:
    template<typename BaseType> using Allocator = tbb::scalable_allocator<BaseType>;
    // [MEMORY] deque guarantees pointer stability on push_back.
    Slic3r::deque<SupportGeneratorLayer, Allocator<SupportGeneratorLayer>> m_storage;
    tbb::spin_mutex                                                        m_mutex;
};
// [INTENT] Vector of raw non-owning pointers into a SupportGeneratorLayerStorage arena.
// Pointer stability guaranteed by deque backing.
using SupportGeneratorLayersPtr = std::vector<SupportGeneratorLayer*>;

} // namespace Slic3r

#endif /* slic3r_SupportLayer_hpp_ */
