# Orca(pnp_gui)

GUI frontend for the pnp_cli slicing backend: a fork of OrcaSlicer with native slicing removed, communicating with the external backend over a JSONL progress stream.

## Language

**Global plate layer**:
One Z stratum of the whole build plate, sliced by pnp for every object that intersects it. `layer_index` in the progress stream refers to this, never to a single object.
_Avoid_: object layer, layer (unqualified)

**Layer status**:
The per-global-plate-layer slice state shown during an active slice: `pending`, `in-progress`, `complete`, or `degraded` (completed with non-fatal module errors).
_Avoid_: done/not-done bool, layer progress

**Slice progress visualization**:
The coloring of model volumes on the plater by layer status while a slice runs, indexed by plate-absolute Z. Disappears when the slice ends; the finished result lives in the G-code Preview.
_Avoid_: progress overlay, layer shader

**Estimated layer count**:
The GUI's fallback total of global plate layers (model height / layer height), used only until the stream's real `layer_count` arrives. Deliberately never shown as exact.
_Avoid_: layer total (unqualified)
