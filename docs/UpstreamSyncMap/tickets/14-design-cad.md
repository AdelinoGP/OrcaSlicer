---
title: Design / CAD tab
status: open
type: implementation
assignee: ""
blocked-by: []
---

# Design / CAD tab

## Question

Port upstream's Design/CAD tab — 265 commits touching the CAD paths (289 across the wider Design
series), 35 files, **+45,608 lines** — which is both the largest feature in the range and the one
that survives this fork's removals cleanly.

It is bigger than every other feature ticket combined, and it is the only substantial subsystem
with **zero** references to `Print::process`, `GCode`, or `slicingPipeline`. It builds on `Model`,
`TriangleMesh`, `Format/OBJ.hpp` and `Format/bbs_3mf.hpp`, all present. It needs OCCT, which the
fork already pins (`opencascade/7.6.0`), so the geometry kernel is not new work.

The one genuinely new dependency is **SLVS**, the constraint solver: GPLv3, combined with this
AGPL-3.0 fork (permitted; the combined work stays AGPL). Upstream fetches and builds it from
`deps/SLVS/SLVS.cmake`, which does not exist here because CMake does not exist here.

## Scope

| Piece | Lines | Notes |
|---|---|---|
| `GUI/CAD/` sketcher + tools | ~25,000 across `DesignSketchTool.cpp` (12,375) and siblings | Port |
| `libslic3r/CAD/CadDocument.cpp` | 4,764 | 268 BRep/TopoDS references; needs OCCT, already pinned |
| `DesignPanel.cpp` | 12,677 | The tab itself; `MainFrame.cpp:1250-1252,1352-1353,1488-1548` is the seam |
| `McpControl.cpp` | 2,304 | **Keep in the first pass.** `DesignPanel.cpp/.hpp` and `MainFrame.cpp` reference it, so "just drop it" is not free — it means editing three upstream files that would otherwise land verbatim. Port it as-is, and strip the IPC surface only if a later ticket decides it is unwanted |
| Text tool | — | Optional; check coupling before deciding |

Permanent fork-side surface, measured by upstream commit touches: `GLCanvas3D.cpp` (30),
`MainFrame.cpp` (22), `Plater.cpp` (21), `Preferences.cpp` (11), `StatusPanel.cpp` (9). The fork
already diverges on the first three (`GLCanvas3D` +255/−341, `Plater` +221/−1172,
`MainFrame` +2/−15), so those hunks are hand-ports.

## Build integration

Per the decided order: **try a Conan recipe first; if that fails, vendor into `deps_src/`**.

The solver is not official SolveSpace: upstream pins the `JacobStoren/SolveSpaceLib` fork at
commit `4d8704523e4bf212fadf5189f92484244f670fea`, archive hash
`SHA256=1c4bdde9c3c6ef20ea4b50b73601de56769f2eb131b36927d7c6489f102e6c30`
(`refs/remotes/main/main:deps/SLVS/SLVS.cmake`). Name that exact source in whatever recipe or
vendored copy is used; "SLVS 7.x" is not an identity.

1. Look for a Conan recipe for that fork and that commit — check Conan Center for `solvespace` and
   `slvs`, then for a `solverspace`-style package. If a recipe exists but tracks a different
   upstream, it is not a match: the fork pins a commit, and the pin is what the CAD code was tested
   against.
2. If no matching recipe exists (the likely outcome), vendor that commit under `deps_src/SLVS` and
   add an xmake target, matching how `deps_src/admesh` is built. Record the version, the GPLv3
   licence file, and the commit vendored.
3. Upstream's `deps/SLVS/SLVS.cmake` is a CMake module and cannot be taken: it is the thing being
   replaced, not a dependency to adopt.

## Sequencing

1. SLVS buildable in isolation, with a trivial unit test that solves one constraint — prove the
   dependency before adding 45k lines that depend on it.
2. `libslic3r/CAD` document model.
3. `GUI/CAD` sketcher, then `DesignPanel`, then the `MainFrame`/`GLCanvas3D` seams.
4. Drop `McpControl.cpp` and say so in the ledger.

## Verification

1. Build and `xmake test` green, including the new SLVS smoke test.
2. Walkthrough, screenshots:
   - Design tab opens; a sketch can be created, constrained, and extruded.
   - A constraint that over-constrains is refused with a message, not silently ignored.
   - Save a project containing a CAD document, reopen it, and confirm the document survives the
     `.3mf` round trip — this is the compatibility case that matters for a fork that must keep
     reading and writing project files.
   - Export the result to a mesh and confirm it appears in the plate as a normal object.
3. Record the binary size delta: SLVS plus CAD code is a substantial addition to a shipped app.

## Risks

- **Scale.** +45,608 lines is more than the entire rest of this map's adopted work combined, which
  is why the map plans it as its own PR alongside the profiles PR and the fix-picks PR.
- **Licence file hygiene.** SLVS is GPLv3; the vendored copy must carry its licence and the
  notices must stay in the shipped tree, or the AGPL distribution is wrong.
- **Maintenance.** Even though it does not need the slicer, CAD is a living subsystem; every
  future upstream CAD fix will land in files this fork now owns.
- The MCP control surface rides along because `DesignPanel` references it. If it is later judged
  unwanted, stripping it is a deliberate fork-side edit to three files, not a free deletion.

## Done when

SLVS built and tested in isolation, CAD document and sketcher ported, tab wired, `McpControl`
ported with its `DesignPanel` coupling intact, project round-trip verified, binary-size delta
recorded, ledger row updated.
