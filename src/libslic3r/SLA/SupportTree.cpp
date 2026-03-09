/**
 * [INTENT] Implements SupportTree::slice() and SupportTree::create().
 * slice() merges support and pad mesh slices for each Z level.
 * create() instantiates a SupportTreeBuilder and runs SupportTreeBuildsteps::execute().
 *
 * [HAZARD H935] In SupportTree::slice(), the `cap` variable is computed as
 * the number of grid levels ABOVE the pad bounding box max Z:
 *   auto cap = grid.end() - maxzit;
 * then used to reserve padgrid which is filled by copying from grid.BEGIN to
 * maxzit (the levels BELOW the pad top). This reserves the TAIL size for a
 * HEAD-copy. The reserve is therefore wrong: it under-reserves by (total -
 * cap) = maxzit - grid.begin() items. std::copy via back_inserter will still
 * work (it grows the vector), but the reserve hint is misleading and imposes
 * extra allocations for large grids. Severity: P3/Low.
 *
 * In this file we will implement the automatic SLA support tree generation.
 */

#include <numeric>
#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/SpatIndex.hpp>
#include <libslic3r/SLA/SupportTreeBuilder.hpp>
#include <libslic3r/SLA/SupportTreeBuildsteps.hpp>

#include <libslic3r/MTUtils.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>

#include <libnest2d/optimizers/nlopt/genetic.hpp>
#include <libnest2d/optimizers/nlopt/subplex.hpp>
#include <boost/log/trivial.hpp>
#include <libslic3r/I18N.hpp>

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r { namespace sla {

void SupportTree::retrieve_full_mesh(indexed_triangle_set& outmesh) const
{
    its_merge(outmesh, retrieve_mesh(MeshType::Support));
    its_merge(outmesh, retrieve_mesh(MeshType::Pad));
}

std::vector<ExPolygons> SupportTree::slice(const std::vector<float>& grid, float cr) const
{
    const indexed_triangle_set& sup_mesh = retrieve_mesh(MeshType::Support);
    const indexed_triangle_set& pad_mesh = retrieve_mesh(MeshType::Pad);

    using Slices = std::vector<ExPolygons>;
    auto slices  = reserve_vector<Slices>(2);

    if (!sup_mesh.empty()) {
        slices.emplace_back();
        slices.back() = slice_mesh_ex(sup_mesh, grid, cr, ctl().cancelfn);
    }

    if (!pad_mesh.empty()) {
        slices.emplace_back();

        auto bb     = bounding_box(pad_mesh);
        auto maxzit = std::upper_bound(grid.begin(), grid.end(), bb.max.z());

        auto cap     = grid.end() - maxzit;
        auto padgrid = reserve_vector<float>(size_t(cap > 0 ? cap : 0));
        std::copy(grid.begin(), maxzit, std::back_inserter(padgrid));

        slices.back() = slice_mesh_ex(pad_mesh, padgrid, cr, ctl().cancelfn);
    }

    size_t len = grid.size();
    for (const Slices& slv : slices) {
        len = std::min(len, slv.size());
    }

    // Either the support or the pad or both has to be non empty
    if (slices.empty())
        return {};

    Slices& mrg = slices.front();

    for (auto it = std::next(slices.begin()); it != slices.end(); ++it) {
        for (size_t i = 0; i < len; ++i) {
            Slices& slv = *it;
            std::copy(slv[i].begin(), slv[i].end(), std::back_inserter(mrg[i]));
            slv[i] = {}; // clear and delete
        }
    }

    return mrg;
}

SupportTree::UPtr SupportTree::create(const SupportableMesh& sm, const JobController& ctl)
{
    auto builder   = make_unique<SupportTreeBuilder>();
    builder->m_ctl = ctl;

    if (sm.cfg.enabled) {
        // Execute takes care about the ground_level
        SupportTreeBuildsteps::execute(*builder, sm);
        builder->merge_and_cleanup(); // clean metadata, leave only the meshes.
    } else {
        // If a pad gets added later, it will be in the right Z level
        builder->ground_level = sm.emesh.ground_level();
    }

    return std::move(builder);
}

}} // namespace Slic3r::sla
