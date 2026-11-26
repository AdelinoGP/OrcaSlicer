---
title: Plugin host (Wasm re-qualification)
status: open
type: implementation
assignee: ""
blocked-by: [12]
---

# Plugin host (Wasm re-qualification)

## Question

Port upstream's plugin host — 168 commits, 57 files, +23,083 lines — into a fork whose slicing
pipeline is an external process and whose modularity story is Wasm, not Python.

Upstream's host is built around two things this fork does not have:

1. **A Python runtime.** `PluginManager.cpp:13` embeds pybind11; neither `conan/conanfile.py` nor
   `xmake.lua` mentions Python or pybind11 today. Porting the host means adding an interpreter to
   an AGPL GUI application and shipping it to users.
2. **A slicing hook.** `slicingPipeline` is wired to `SlicingPipelineStepPlugin` (`Print.hpp:132-137`
   upstream) and fires from `Print.cpp:3654-4148`. Here `Print::process` is an empty stub
   (`src/libslic3r/Print.cpp:2238`), so the flagship capability **cannot fire**.

Therefore this ticket is not "port the plugin host". It is: decide and build what the fork's
plugin surface *is*, on top of pnp's module system, and take from upstream only what survives.

## What may port

| Piece | Files | Condition |
|---|---|---|
| Plugin discovery, packaging, audit | `PluginFsUtils.cpp` (2,648), `PluginAuditManager.cpp` (4,643) | Only if the fork keeps a plugin UI at all; these are the least Python-coupled |
| Pages / script capabilities | `PluginManager.cpp` (3,123) | These are Python-bound. Porting them means embedding pybind11 |
| Host UI surfaces | 18 existing files reference `PluginManager`, hottest `GUI_App.cpp` (30), `Tab.cpp` (23), `Plater.cpp` (20) | Permanent fork-side surface across the three most divergent GUI files |

## What does not port

- `slicingPipeline` in its upstream form — the hooks are in `Print::process`, which is a stub.
  Ticket 12's P3.2 replaces it with a Wasm ABI.
- `PostProcessor.cpp`'s post-process step. There is no GUI post-processor here; the only
  post-processing seam is `libslic3r/GCode/PostProcessor.cpp`, and `GCode/` is deleted.

## Sequencing

1. Ticket 12 closes with P3.1–P3.4 decided, in particular **P3.1**: does the fork embed Python at
   all? If the answer is no, this ticket becomes "Wasm plugin surface", most of the +23,083 lines
   are dropped, and the remaining work is a new ABI rather than a port.
2. If Python stays excluded (the likely answer, matching "our slicer is fully modular with Wasm"),
   port only discovery/packaging/audit and the UI shell, and write the Wasm binding against
   `pnp_cli` modules.
3. Submodule changes under the ticket 12 waiver land as their own commit with a `pinch_n_print_cli/`
   bump.

## Verification

1. Build and `xmake test` green, with the Python runtime either absent (best) or explicitly
   recorded as a new shipped dependency with its licence and size.
2. Walkthrough, screenshots:
   - The plugin/preferences surface lists installed plugins (or the Wasm equivalent).
   - One plugin step runs inside a `pnp_cli` slice and its effect appears in the output — the P3.3
     acceptance test. Without this, the ticket does not close.
   - A malformed or quarantined plugin is refused, and the refusal is visible.
3. Record which upstream capabilities were dropped and why, per capa (pages, script,
   printerAgent, slicingPipeline).

## Risks

- This is the largest single liability in the range (+23,083 lines, 18 existing files touched).
  If P3.1 says no Python, most of it is dead weight and should never land.
- Embedding CPython in a wxWidgets GUI on Windows is a packaging problem (DLLs, interpreter init,
  GIL versus the GUI thread) that the fork has never carried. That cost is independent of who
  writes the port, and it is why the map lists ticket 11 as a possible new shipped dependency.
- A plugin surface that cannot slice is a surface that lies; the Wasm ABI is what makes it
  truthful, and it does not exist yet. **Merge rule:** the GUI half does not merge until P3.3
  passes — the same rule tickets 09 and 10 carry.

## Done when

P3.1 answered, the surviving subset ported, the Wasm step demonstrated inside a real slice, and
the dropped capabilities listed in the ledger with reasons.
