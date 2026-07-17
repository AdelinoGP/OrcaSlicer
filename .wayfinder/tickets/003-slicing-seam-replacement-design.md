---
title: Slicing-seam replacement design
status: open
type: grilling
assignee:
blocked-by: [001]
---

## Question

What replaces `BackgroundSlicingProcess::process_fff`? Design the subprocess seam: pnp_cli process lifecycle, temp-file handoff (model + config in, gcode out), the per-plate loop for "Slice all", where in the existing state machine (`STATE_IDLE/STARTED/RUNNING/FINISHED`) the swap happens, and how much of `Plater`'s driving code stays unchanged vs. modified. A `/prototype` stub of the new process class may be warranted.
