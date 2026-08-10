---
title: Upstream sync strategy
status: closed
type: grilling
assignee: Analysis Agent
blocked-by: []
---

## Question

The fork modifies this checkout in place and is long-lived. What is the branch model, how (and whether) do we take upstream OrcaSlicer updates going forward, and what "canonical porting source" status does this checkout lose for pinch_n_print porting work — does pnp porting need a separate pristine OrcaSlicer reference?

## Resolution

Decided with the user (grilling session, 2026-07-16):

- **Branch model:** create a long-lived `pnp/main` branch off current local `main` as the fork's trunk, pushed to the AdelinoGP fork. Feature branches merge into `pnp/main`. Local `main` remains an untouched upstream mirror (remote `main` = OrcaSlicer/OrcaSlicer).
- **Fork point:** tag it `pnp-fork-base` at the commit `pnp/main` branches from — fixed base for cherry-picks and a permanent answer to "what upstream revision did we fork at".
- **Upstream sync:** cherry-pick only, no merges, no scheduled cadence. Freeze at the fork point; opportunistically cherry-pick printer-profile updates, GUI fixes, and security fixes. Rationale: after the slicing rip-out, upstream merges conflict heavily in `libslic3r`, and the fork's lifespan (bridge until pnp_studio matures) never amortizes merge debt.
- **Porting reference:** this checkout **loses canonical porting-source status** once the rip-out lands. The user maintains a separate pristine OrcaSlicer folder on this machine for pinch_n_print porting work — out of this repo's hands. In-repo, pristine `main` + the `pnp-fork-base` tag remain available for `git show`-style reference and cherry-pick bases.
