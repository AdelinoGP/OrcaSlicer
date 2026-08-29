---
title: Restore pnp_cli bundling after the dist layout change, and land the submodule bump
status: closed
type: task
assignee: Adelino Penedo
blocked-by: [01]
---

## Question

At `dbf3449c`, `cargo xtask dist` stages to `pinch_n_print_cli/target/dist/<edition>/` (e.g.
`target/dist/developer/`) instead of the flat `target/dist/`. The fork's `--pnp_dist_dir=`
default and the xmake bundling step still point at the flat path, which on a machine that has
ever built the old layout holds a stale `pnp_cli.exe` + `modules/` — silently, with no error.
Ticket 01 hit exactly this: the inventory ran against `1238ef02`'s schema while the submodule
worktree was at `dbf3449c`.

Also unfinished: the superproject still records `1238ef02` (`git ls-tree HEAD
pinch_n_print_cli`); `dbf3449c` is checked out but uncommitted.

Work:

- Decide how the fork picks an edition — hardcode one, add a `--pnp_dist_edition=` option, or
  probe for the newest/only subdirectory. Editions are a pnp concept the fork has not modelled.
- Update `--pnp_dist_dir=`'s default and the xmake bundling rule in `xmake.lua` /
  `xmake/modules/pnp/layout.lua` so the build tree and the shipped tree both get the real
  `dbf3449c` binary and its 23 modules (up from 21).
- Fail loudly, not silently, when the dist dir is absent or empty.
- Commit the submodule bump to `dbf3449c` on `pnp/main`.

Deliverable: bundling works from a clean `cargo xtask dist`, verified by checking that the
bundled `pnp_cli module config-schema` reports 23 modules.

## Amended by ticket 02 (2026-08-28)

**The submodule bump is landed.** Ticket 02 needed a pnp-side commit
(`config-schema` wire 1.1.0: the `host` array and per-field `scope`), so the
superproject now records `1343489f` — which is `dbf3449c` plus that commit —
instead of `1238ef02`. The "land the submodule bump" half of this ticket is done.

What is **not** done, and is still the whole point of this ticket: the dist layout.
`cargo xtask dist` still stages to `target/dist/<edition>/`, and the fork's
`--pnp_dist_dir=` default and the xmake bundling step still point at the flat
`target/dist/`. Nothing in ticket 02 touched either, and ticket 02's verification
used a hand-built dist rather than the bundled one — so a built GUI still cannot
find `pnp_cli`, and the schema probe it now depends on at startup will fail on a
clean build. That failure is non-fatal by design (the GUI runs with the stock key
set and raises a notification), but it means **no registered pnp key reaches a real
build until this ticket lands**.

## Amended by ticket 06

`pinch_n_print_cli/target/dist/pnp_cli.exe` **and** `target/dist/developer/pnp_cli.exe` both answer
`module config-schema` with `schema_version 1.0.0` and no `host` array, while the submodule working
tree is at `a50bfc28`, which emits wire 1.1.0 (93 host entries). The staged binaries predated
ticket 02's pnp-side commit; `cargo xtask dist` fixed it locally in that session, but nothing in the
fork noticed. Whatever this ticket does about the layout must also leave the dist actually restaged —
a correct `--pnp_dist_dir` pointing at a stale binary silently drops 65 host keys from the fork's
key universe and disables ticket 06's drift reconciliation.

## Resolved (2026-08-28)

**Resolution summary:**

- xmake resolves `<pnp_dist_dir>/<pnp_dist_edition>/` by default. New option `--pnp_dist_edition=`
  (default `developer`); `--pnp_dist_dir` keeps its default (the dist root) and is documented as a
  root, not a dist. A root that holds no `pnp_cli` directly and no `<edition>/pnp_cli` fails the
  build loudly, listing which editions it found. A root that holds `pnp_cli` but no edition layer
  (e.g. a checkout-external tree staged before the edition change) still bundles, with a loud
  warning naming the flat layout.
- `xmake pnp` passes `--pnp_dist_edition` through to `cargo xtask dist`, so the staging step and
  the bundling step can never disagree about editions.
- Bundling is a **mirror**, not a merge: the copy step wipes `modules/` first, so a module deleted
  upstream cannot survive a rebuild beside the executable (verified live with a decoy module).
- The bundle and resources staging share **one** `after_build` closure on the `OrcaSlicer` target.
  Two load-bearing xmake-3.0.9 behaviours, both measured:
  1. **Only the last registered `after_build` closure on a target runs** — earlier ones are
     silently dropped. The bundling rule sat *before* the resources rule's `after_build`, so
     bundling never executed under this build system: the build tree's `pnp_cli.exe` beside the
     exe was a leftover from 03:54 today, not a copy, and xmake reported nothing. The loud
     bundle-failure path could never have fired either — the whole rule was dead, not merely
     wrong.
  2. **The description scope's `os` has no destructive verbs** — `os.rm`/`os.tryrm` are nil there
     (both errored when called from a closure defined at description scope), while `after_build`'s
     action sandbox has the full set. The rule therefore keeps only option *values* at description
     scope and does its work inside the single `after_build` closure.
- The shipped `xmake package` tree inherits all of this via the build dir; verified below.
- CI (`build_orca.yml`) gained: a Rust toolchain (needed for `cargo xtask dist`), a wasm-tools
  install step, a Rust-build cache, and a "Stage PNP backend" step. The new loud failure makes
  this mandatory — C++-only CI would fail at the bundle step rather than ship a backend-less
  installer silently.
- Submodule pointer moved `1343489f` → `a50bfc28`: code-identical twins (same patch content;
  only the docs-closure commit `fc8272d5` differs), but `1343489f` sat on no branch — a
  `submodules: recursive` checkout on GitHub would fail to fetch it, breaking CI at checkout
  before the new dist stage could even run.
- The stale flat dist (`target/dist/pnp_cli.exe` + 21 modules, wire 1.0.0) was deleted after the
  restage; `target/dist/developer/` is the only dist in the tree. Measured evidence in
  [08-restaged-dist.md](../assets/08-restaged-dist.md).

**Post-restage probes, measured:**

| probe | result |
|---|---|
| `pnp_cli.exe module config-schema` (staged dist) | wire **1.1.0**, 23 modules, **93** host keys |
| `pnp_cli.exe module config-schema` (build tree, after build) | wire **1.1.0**, 23 modules, **93** host keys |
| `pnp_cli.exe module config-schema` (`xmake package` tree) | wire **1.1.0**, 23 modules, **93** host keys |
| missing-dist build (sandbox repro of the resolution logic) | build **fails** naming both probed paths and the editions found |
| flat-dist build (sandbox repro) | bundles with a loud warning naming the legacy layout |
| mirror check (decoy module planted beside the exe, rebuild) | decoy wiped; exactly the 23 staged modules remain |
| `PNP_LIVE_SCHEMA=… pnp_config_translator_tests "[live-schema]"` | runs to the drift diff; reports exactly ticket 06's six dead rows (ticket 09 owns the repairs) |
| `pnp_config_translator_tests` + `pnp_runtime_tests` | all pass (917 + 282 assertions) |
| `cargo xtask build-guests --check` before dist | exit 0 (all guests fresh) |
