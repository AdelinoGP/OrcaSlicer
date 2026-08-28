---
title: Restore pnp_cli bundling after the dist layout change, and land the submodule bump
status: open
type: task
assignee:
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
