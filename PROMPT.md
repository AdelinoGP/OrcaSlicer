# PROMPT - Phase 2: GUI Documentation Package for Unity Reimplementation

## Phase Boundary

This prompt governs **Phase 2 only**.

Assume Phase 1 has already finished successfully.

- Do not redo Phase 1 annotation work except for a minimal corrective edit when a document would otherwise be false.
- Do not begin Phase 3 review/finalization work here.
- Do not emit the configured completion token until every Phase 2 completion gate is satisfied.

## Mission

You are producing the documentation package that future AI agents and human engineers will use to reimplement the OrcaSlicer GUI layer in Unity/C#.

This phase turns source-level annotations into durable, structured engineering documentation.

The output must be specific, source-grounded, and implementation-oriented.

## Authoritative Working State

Use these as the source of truth for progress and resumption:

1. `.ralph/ralph-tasks.md`
2. `.ralph/agent/handoff.md`
3. Ralph task tooling state
4. The annotated source tree from Phase 1

If these disagree, reconcile the discrepancy first and record it in `.ralph/agent/handoff.md`.

## Runtime Behavior

- Completing one document task is **not** a reason to stop.
- After every completed document task, immediately select the next eligible Phase 2 task and continue.
- If one task is blocked, record the blocker and continue with the next unblocked Phase 2 task.
- The only valid reasons to end the run are:
  1. every Phase 2 completion gate is satisfied, then emit the configured completion token once on its own line, or
  2. every remaining Phase 2 task is blocked by a hard external dependency and that blocker is explicitly recorded.

Never print the configured completion token in examples, notes, handoff entries, or commit messages.

## Output Location

Write all Phase 2 deliverables under:

`generated_documentation/gui/`

## Required Phase 2 Task Naming

Use task titles that make auditing easy:

- `T201 docs: gui_01_architecture_overview.md`
- `T202 docs: gui_02_screen_and_widget_inventory.md`
- `T203 docs: gui_03_state_management.md`
- `T204 docs: gui_04_opengl_viewport_pipeline.md`
- `T205 docs: gui_05_event_and_callback_model.md`
- `T206 docs: gui_06_background_process_and_threading.md`
- `T207 docs: gui_07_unity_porting_hazards.md`
- `T208 docs: gui_08_external_gui_dependencies.md`
- `T209+ docs: flow_<name>.md` for pseudocode / flow documents

If a title does not follow this format, normalize it when you touch it.

## Documentation Quality Bar

Every document must be useful to a Unity implementation effort.

### Mandatory quality rules

1. No stub files.
2. No headers-only placeholders.
3. No padding to satisfy length.
4. Every major claim should be tied to source evidence.
5. Every document must include Unity-specific recommendations, not just C++ descriptions.
6. Every unresolved ambiguity must be called out explicitly.
7. Prefer durable structure: tables, numbered flows, state diagrams, dependency lists, and subsystem breakdowns.

### Required source-reference format

Use repo-relative textual anchors in this exact style when citing source code:

- `src/slic3r/GUI/Foo.cpp:L120-L184`
- `src/libvgcode/Bar.hpp:L33-L79`

Use one or more such anchors wherever they materially improve trustworthiness.

Do **not** rely on host-specific web URLs.

### Acceptable diagram forms

At least one of these should appear where useful:

- Mermaid
- ASCII block diagram
- numbered sequence flow
- pseudocode listing
- lifecycle table

## Standard Work Loop

For each Phase 2 task:

1. Read `.ralph/ralph-tasks.md`, `.ralph/agent/handoff.md`, and the relevant Phase 1 annotations.
2. Select the next highest-priority incomplete Phase 2 task.
3. Mark it `[~] ACTIVE` in `.ralph/ralph-tasks.md`.
4. Gather the primary source files and anchors you will rely on.
5. Write or expand the document until it is materially complete.
6. Self-verify that the document is not a stub and contains concrete Unity guidance.
7. Append a completion-evidence block to `.ralph/agent/handoff.md`.
8. Mark the task `[x] DONE` in `.ralph/ralph-tasks.md`.
9. Commit the atomic change.
10. Immediately continue to the next eligible Phase 2 task.

## Required Completion Evidence Block

After each document task, append this to `.ralph/agent/handoff.md`:

```md
## Phase 2 - Task T2xx complete
- Deliverable: generated_documentation/gui/<filename>
- Scope covered: <subsystems or flows>
- Source anchors referenced: <count>
- Key Unity decisions captured: <2-5 bullets>
- Verification excerpt: <one meaningful line from the document>
- Remaining follow-up if any: <none or short note>
- Git: <commit hash or commit subject>
- Next recommended Phase 2 task: <task id>
```

A document task is not complete until this evidence exists.

## Required Deliverables

### T201 - `gui_01_architecture_overview.md`

Purpose: a top-down map of the GUI system.

Required sections:

- system boundary and module map
- startup path and lifetime overview
- major GUI subsystems and their responsibilities
- cross-cutting concerns: undo/redo, i18n, theming, settings, background work
- Unity migration summary by subsystem
- recommended port order

### T202 - `gui_02_screen_and_widget_inventory.md`

Purpose: a screen-by-screen and widget-by-widget inventory.

Required sections:

- top-level windows, tabs, panes, and dialogs
- ownership/lifecycle notes
- Unity UI equivalent for each major screen or widget
- complexity notes and migration hotspots
- inventory table keyed by source anchors

### T203 - `gui_03_state_management.md`

Purpose: explain how UI state is modeled and moves through the system.

Required sections:

- state taxonomy: ephemeral, session, persistent, domain-backed
- ownership and mutation patterns
- synchronization points and invalidation patterns
- persistence/settings interactions
- Unity recommendations: MonoBehaviour state, ScriptableObject, serialized settings, async state, etc.

### T204 - `gui_04_opengl_viewport_pipeline.md`

Purpose: deep explanation of the rendering and interaction pipeline.

This is one of the two highest-priority documents.

Required sections:

- render loop trace
- viewport scene composition
- GL resource lifetime and ownership
- user interaction model
- shader/material considerations
- g-code visualization behavior
- at least three Unity strategy options with trade-offs
- preferred strategy and why

### T205 - `gui_05_event_and_callback_model.md`

Purpose: explain wxWidgets events, callbacks, and higher-level event flows.

Required sections:

- event model primer for this codebase
- important bind sites and handlers
- critical user flows as numbered sequences
- custom events or app-specific dispatch patterns
- Unity equivalents: EventSystem, UnityEvent, delegates, observables, custom bus, etc.

### T206 - `gui_06_background_process_and_threading.md`

Purpose: explain how background work interacts with the GUI.

This is the other highest-priority document.

Required sections:

- thread/process inventory
- background slicing and job orchestration
- main-thread marshaling patterns
- UI thread safety constraints
- async cancellation/progress behavior
- Unity equivalents using async/await, coroutines, Job System, or custom dispatching

### T207 - `gui_07_unity_porting_hazards.md`

Purpose: a concentrated list of migration risks.

Required sections:

- critical blockers
- hazard catalog grouped by subsystem
- severity, impact, evidence, and likely mitigation
- dependencies between hazards
- recommended order for burning down risk

### T208 - `gui_08_external_gui_dependencies.md`

Purpose: catalog external dependencies that affect the GUI port.

Required sections:

- dependency inventory
- how each dependency is used by the GUI layer
- whether Unity has a package or native replacement path
- keep/adapt/replace recommendation for each
- notable licensing or integration concerns if visible from source context

## Pseudocode / Flow Documents (T209 and above)

Create flow documents for complex flows that deserve implementation recipes.

Use names like:

- `flow_app_startup.md`
- `flow_viewport_input_and_render.md`
- `flow_background_slicing.md`
- `flow_project_load_save.md`
- `flow_printer_connection_or_upload.md`

Create one when a flow has at least one of the following:

- multiple event handlers
- cross-thread behavior
- GPU/render interaction
- substantial state transitions
- non-obvious Unity migration implications

Each flow document must include:

- flow purpose
- participating source files and anchors
- numbered steps
- explicit state/thread/render markers where applicable
- Unity implementation notes

## Self-Check Before Marking a Document Done

Before marking any Phase 2 task done, verify that the document:

- has real content, not scaffolding
- names the relevant source files and anchors
- contains concrete Unity mappings
- contains at least one table, flow, or diagram where useful
- calls out unresolved ambiguity explicitly
- would help a new engineer implement the subsystem in Unity without rereading all source files first

## Phase 2 Completion Summary Block

When you think Phase 2 is complete, append this block to `.ralph/agent/handoff.md`:

```md
## Phase 2 documentation coverage summary
- Core docs present: <list T201-T208 files>
- Flow docs present: <list or `none`>
- Highest-priority docs completed: T204 yes/no, T206 yes/no
- Source anchor convention used consistently: yes/no
- Remaining documentation gaps: <list or `none`>
- Result: PASS | FAIL
```

If the result is `FAIL`, continue Phase 2 work immediately.

## Phase 2 Completion Gate

Phase 2 is complete only when **all** of the following are true:

1. `generated_documentation/gui/gui_01_architecture_overview.md` exists and is substantive.
2. `generated_documentation/gui/gui_02_screen_and_widget_inventory.md` exists and is substantive.
3. `generated_documentation/gui/gui_03_state_management.md` exists and is substantive.
4. `generated_documentation/gui/gui_04_opengl_viewport_pipeline.md` exists and is substantive.
5. `generated_documentation/gui/gui_05_event_and_callback_model.md` exists and is substantive.
6. `generated_documentation/gui/gui_06_background_process_and_threading.md` exists and is substantive.
7. `generated_documentation/gui/gui_07_unity_porting_hazards.md` exists and is substantive.
8. `generated_documentation/gui/gui_08_external_gui_dependencies.md` exists and is substantive.
9. T204 and T206 are among the deepest and most evidence-backed docs in the set.
10. At least two flow documents exist if the codebase contains at least two qualifying complex flows; otherwise the reason is documented in `.ralph/agent/handoff.md`.
11. `.ralph/agent/handoff.md` contains the final Phase 2 documentation coverage summary with `PASS`.
12. No Phase 2 task remains `[~] ACTIVE`.
13. All Phase 2 work is committed.

Only then emit the configured completion token once, on its own line, and nothing else after it.
