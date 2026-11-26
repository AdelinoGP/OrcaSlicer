---
title: Security hardening — hand-ported
status: open
type: implementation
assignee: ""
blocked-by: []
---

# Security hardening — hand-ported

## Question

The four hardening commits that leave conflict markers at this base are the ones with the widest
blast radius: 3MF component/plate parsing, the `TriangleSelector` paint path, the Project page's
metadata rendering, and the LAN-printer JSON paths. Adopt them by hand-porting, as the standing
rule requires, and record exactly what the port changed.

Conflict is not a reason to skip a security fix. It is a reason to write down the diff.

## Picks

| SHA | Conflict site (measured) | Port shape |
|---|---|---|
| `a124b7d2df` | `Format/bbs_3mf.cpp` | Add the cyclic-component rejection to the fork's loader. Small, but it is the OOM vector: a crafted component graph otherwise recurses without bound |
| `490d134507` | **`src/libslic3r/TriangleSelector.cpp`**, plus `test_3mf.cpp`. `bbs_3mf.cpp` and `Model.cpp` apply cleanly | The conflict is inside the selector itself, not at "moved call sites" — port the validation into the fork's selector and keep its call sites. Test half dropped |
| `203bc63f35` | `Utils.hpp`, `utils.cpp`; `GUI/Project.cpp` and the web asset apply cleanly | Port the escaping helpers, then the Project-page use. Verify `resources/web/model/model.js` at head matches upstream before taking that file wholesale |
| `e40030cf81` | **`PrintHost.cpp` conflicts**; the other driver files apply, and `UltiMaker.*` do not exist here | **Omit `Utils/UltiMaker.cpp|hpp`** (new upstream; ticket 08 owns them later). 11 source files + 3 test/CMake files upstream; the test/CMake half is dropped |

## Method

1. `git show <sha> -- <paths>` into a patch, apply per file by hand where markers appear.
2. Commit as `cherry-pick(partial): <sha> <subject>`, body naming the omitted hunks and why.
3. `git range-diff` the result against upstream and record the divergence in the ledger.

## Verification

1. Build and `xmake test` green.
2. Hostile-input walkthrough, screenshots:
   - 3MF with a cyclic component graph → refused, no OOM.
   - 3MF with malformed plate IDs and malformed paint data → refused; painting on a normal model
     still works (this is the `TriangleSelector` regression risk).
   - Project whose metadata contains a script payload → rendered as text in the Project page, not
     executed; accessory-open path restricted.
   - A malformed response from each driver (Duet, ESP3D, Flashforge, MKS, PrintHost, UserManager),
     served by a local stub → no crash.

## Risks

- `490d134507` is the largest port (296+/63−) and touches the paint tool's geometry helper. A
  subtle porting error corrupts painting rather than crashing, so the walkthrough must exercise
  painting, not just loading.
- `e40030cf81`'s UltiMaker hunks will conflict again when ticket 08 lands. Take them from upstream
  there, not from this ticket.
- Four hand-ports is four permanent divergences; the ledger must name each conflicting file.

## Done when

Four hand-ported commits, each `range-diff`ed and documented, build and tests green, hostile-input
screenshots attached, ledger evidence filled including the omitted hunks and dropped tests.
