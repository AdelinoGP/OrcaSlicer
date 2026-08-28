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
