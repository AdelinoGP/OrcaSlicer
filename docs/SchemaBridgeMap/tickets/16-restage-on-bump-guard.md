---
title: Keep the staged dist fresh across submodule bumps
status: open
type: grilling
assignee:
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