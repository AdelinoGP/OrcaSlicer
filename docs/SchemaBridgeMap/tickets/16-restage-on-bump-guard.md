---
title: Keep the staged dist fresh across submodule bumps
status: closed
type: grilling
assignee: claude-code-agent
blocked-by: [08]
---

## Question

Ticket 08 deleted the stale flat dist and made CI stage its own, but the local
footgun remains: on a dev machine the bundled backend is only as fresh as the
last manual `xmake pnp`, and nothing compares it to the submodule commit.
Ticket 06 hit the real failure mode — a dist staged from a pre-wire-1.1.0
binary silently dropped 65 host keys from the fork's key universe.

Three candidate positions to decide between:

- **Build time.** The xmake bundle step records the submodule commit (or the
  dist's own provenance, if pnp emits one) into a stamp beside `pnp_cli.exe`
  and fails the build when it does not match `pinch_n_print_cli` HEAD. Loud,
  but breaks the build on every submodule bump until a restage; needs a skip
  valve for pnp-side iteration (build against an OLDER backend deliberately).
- **Startup.** `PnpBackend` compares the probed `schema_version` against the
  fork's floor (wire 1.1.0, the first wire carrying the `host` array) and
  raises the existing degradation notification naming an *old backend*, not a
  missing one. Makes the indistinguishable case ("backend declares less") a
  visible, named state.
- **Nowhere.** Accept it: CI stages fresh, humans run `xmake pnp`, and ticket
  08 deleted the only stale tree so the trap is currently empty.

Related, same decision: should `xmake pnp` run *automatically* as part of the
build when the dist is missing (it needs cargo, which CI now installs), or
stay a hard failure telling the human to run it — ticket 08 chose the hard
failure deliberately, but a `--pnp_dist_edition` build that could self-heal
the "forgot to stage" case is worth weighing against the surprise cost of a
C++ build silently spawning a cargo build.

Deliverable: a decision (any of the above, or a reasoned reject), and if
enforcement is chosen, the smallest implementation that satisfies it.
## Resolution (2026-09-03)

**Decision: a fork-only, build-time provenance stamp that self-heals.** The user
ruled out pnp-side changes entirely, which removes the two candidates that
needed them (a dist emitting its own provenance, a wire minor floor is
fork-side but was rejected on its own merits below).

### What was decided, and why

- **Guard point: build time, not startup.** The startup wire-minor floor was
  offered first with the strongest evidence behind it — `PnpBackend` checks only
  `SUPPORTED_CONFIG_SCHEMA_MAJOR` (`PnpBackend.hpp:34`) while the wire has moved
  1.0.0 -> 1.1.0 -> 1.2.0, so *every* drift ticket 06 and 19 actually hit was
  invisible to the existing check — and was declined. Build time catches any
  staleness, not only what the wire happens to express; it does nothing for a
  shipped install, which is accepted.
- **Stamp source: `xmake pnp` writes it.** The dist carries no provenance —
  `pnp_cli --version` reports `pnp_cli 0.1.0`, the crate version, and nothing
  else — so the only place that *knows* the commit is the stager. `xmake pnp`
  writes `<dist_root>/<edition>/.pnp-stamp` holding the submodule HEAD right
  after `cargo xtask dist` succeeds. Exact commit identity; no heuristics. The
  mtime-vs-commit-date alternative (pnp's own `staleness_reason` shape, pinned
  by `pnp_cli_freshness_tdd.rs`) was rejected as heuristic.
- **Missing stamp: silent pass.** A dist predating this guard, or staged by bare
  `cargo xtask dist`, has unknown freshness — not wrong freshness. It bundles
  with no message. **This makes the guard inert in CI**, which runs bare
  `cargo xtask dist` (`build_orca.yml:148`); acceptable, because CI checks out
  and stages in the same job, so its dist is fresh by construction.
- **Dirty submodule: warn, never fail.** HEAD equality cannot see uncommitted
  pnp work, and a pnp-side iteration session is exactly when that work exists.
  One warning line; the build proceeds.
- **Stale and missing both self-heal.** This **reverses ticket 08's deliberate
  hard failure**: the bundle step now runs `cargo xtask dist` itself rather than
  telling the human to. A C++ build can therefore start a long Rust build. The
  raise survives only for the case where a restage still produces no `pnp_cli`.
- **Escape valve: `--pnp_bundle_cli=n`.** No new option. Building deliberately
  against an older backend (what ticket 06 needed) means turning bundling off
  and staging by hand.

### Implementation

- New `xmake/modules/pnp/stage.lua`: `submodule_head()`, `submodule_dirty()`,
  `stamp_file()`, `read_stamp()`, `stage()`. `stage()` is the single staging
  path — it runs `cargo xtask dist` and writes the stamp, so the task and the
  self-heal cannot drift.
- `task("pnp")` now delegates to `stage.stage()`.
- The `OrcaSlicer` `after_build` closure resolves the dist through a local
  `resolve_dist()` (called twice: before and after a restage), decides
  restage-or-not, then bundles as before. The single-closure constraint from
  ticket 08 is untouched — this is still one `after_build`.
- The stamp lives in the dist tree, not beside the staged exe, so
  `xmake/modules/pnp/layout.lua` needs no change and no shipped tree carries it.

### Verification

- `xmake f -y -m release` re-parses `xmake.lua` clean.
- `stage.lua`'s helpers exercised directly via `xmake l`: `submodule_head()`
  returns the real HEAD, `submodule_dirty()` false on a clean submodule,
  `read_stamp()` nil on an unstamped dir and the trimmed commit on a stamped one.
- End-to-end: `xmake pnp` staged into the absent `target/dist` ticket 08 left
  deleted (1 binary + 23 modules, `.pnp-stamp` = `a63fd14e...`), then an
  incremental `xmake -b OrcaSlicer` over **five** states, each read out of the
  full build log (explicit `-b OrcaSlicer` per ticket 14's xmake finding):

  | state | setup | observed |
  |---|---|---|
  | 1 | stamp == HEAD | `staged backend from ...`, no restage, no warning |
  | 2 | stamp doctored to all-zero | `staged dist was built from 000000000000, pinch_n_print_cli is at a63fd14ea15a -- restaging`, cargo re-ran inside `after_build`, stamp rewritten to real HEAD, bundle proceeded |
  | 3 | stamp deleted | silent pass; no restage, no message, no stamp fabricated (only `stage()` writes one) |
  | 4 | dist dir moved aside + untracked file in submodule | `no pnp_cli in ... -- restaging`, cargo re-ran, dist and stamp restored |
  | 5 | dirty submodule, stamp == HEAD | the uncommitted-changes warning, then bundle |

  State 4 exposed a wart and it was fixed: the dirty warning fired *after* the
  restage, advising a restage that had just happened -- and wrongly, since
  `cargo xtask dist` builds the working tree, so a dist staged from a dirty
  submodule already carries the uncommitted work. The warning is now suppressed
  when a restage ran (`restage_reason == nil` guard); state 5 pins that it still
  fires in the case that matters.

- **No Catch2 coverage.** The whole guard is xmake Lua in the build description
  and the `after_build` sandbox; the fork's suites cannot reach it. The five
  states above are the verification, and they are manual.
