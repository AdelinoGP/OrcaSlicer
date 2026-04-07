# PROMPT - Phase 3: Final Review, Validation, and Readiness Package

## Phase Boundary

This prompt governs **Phase 3 only**.

Assume Phases 1 and 2 have already completed.

- Do not redo earlier phases wholesale.
- Do not create new major Phase 2 documents beyond what is required to fix audit failures.
- Do not emit the configured completion token until every Phase 3 completion gate is satisfied.

## Mission

You are performing the final validation pass over the GUI analysis package for the future Unity/C# port.

This phase exists to catch drift, stale references, unresolved ambiguity, and missing readiness guidance before the package is handed off to implementation agents.

## Authoritative Working State

Use these as the source of truth:

1. `.ralph/ralph-tasks.md`
2. `.ralph/agent/handoff.md`
3. Ralph task tooling state
4. Annotated source files from Phase 1
5. Documentation under `generated_documentation/gui/` from Phase 2

If these disagree, reconcile the discrepancy first and record the resolution in `.ralph/agent/handoff.md`.

## Runtime Behavior

- Completing one review task is **not** a reason to stop.
- After every completed review task, immediately select the next eligible Phase 3 task and continue.
- If you find a defect, fix it or create a precise follow-up task, then continue.
- The only valid reasons to end the run are:
  1. every Phase 3 completion gate is satisfied, then emit the configured completion token once on its own line, or
  2. every remaining Phase 3 task is blocked by a hard external dependency and that blocker is explicitly recorded.

Never print the configured completion token in notes, examples, handoff entries, or commit messages.

## Required Phase 3 Task Naming

Use these canonical tasks:

- `T301 audit: unclear_inventory`
- `T302 audit: source_reference_validation`
- `T303 audit: documentation_consistency`
- `T304 review: final_readiness_package`
- `T305 finalize: phase3_commit`

If your current task list differs, normalize it when you touch it.

## Required Deliverables

Phase 3 must produce or update these artifacts:

- `generated_documentation/gui/gui_99_final_review.md`
- updates to earlier docs if reference or consistency fixes are needed
- `.ralph/agent/handoff.md` final review summary
- `.ralph/ralph-tasks.md` closed Phase 3 state

## Standard Work Loop

1. Read `.ralph/ralph-tasks.md`, `.ralph/agent/handoff.md`, and the Phase 2 documents.
2. Select the next incomplete Phase 3 task.
3. Mark it `[~] ACTIVE`.
4. Perform the audit or fix.
5. Record evidence in `.ralph/agent/handoff.md`.
6. Mark the task `[x] DONE`.
7. Commit the atomic change.
8. Immediately continue to the next eligible Phase 3 task.

## T301 - UNCLEAR Inventory

Search all in-scope source files for `[UNCLEAR]` tags and produce a structured inventory.

Recommended search scope:

- `src/slic3r/GUI/`
- `src/libvgcode/`
- `src/slic3r/Utils/`
- `src/slic3r/Config/` if present
- `src/OrcaSlicer.cpp` if it contains Phase 1 annotations

For each `[UNCLEAR]` item, classify it as one of:

- resolved during later work
- still unresolved but low-risk
- still unresolved and porting-relevant

Record each item in `generated_documentation/gui/gui_99_final_review.md` with:

- source anchor
- short description
- current best hypothesis
- expected Unity impact
- recommendation for the implementation phase

## T302 - Source Reference Validation

Phase 2 documents should use textual anchors like:

- `path/to/file.cpp:L120-L184`

Validate every such reference in `generated_documentation/gui/`.

The audit must check that:

1. the referenced file exists
2. the start line exists
3. the end line exists if present
4. the referenced passage still matches the claim closely enough to remain trustworthy

If a reference is broken or stale, fix the document immediately.

You may use a temporary script under `/tmp` to automate validation if helpful, but do not commit temporary tooling unless it adds durable value.

## T303 - Documentation Consistency Audit

Audit the documentation package for internal consistency.

Check at least the following:

- every required Phase 2 core document exists
- the same subsystem is described consistently across docs
- hazards in `gui_07_unity_porting_hazards.md` are reflected in the architecture and threading docs where relevant
- major viewport claims align with the annotations in the source
- screen inventory and state-management docs do not contradict each other
- any corrective source edits made during Phase 2 are reflected in the docs

If you find a material inconsistency, fix it rather than merely reporting it.

## T304 - Final Readiness Package

Create or update `generated_documentation/gui/gui_99_final_review.md` so it becomes the executive handoff for the future Unity implementation effort.

Required sections:

- artifact inventory
- unresolved ambiguities
- source reference validation summary
- documentation consistency summary
- critical blockers
- recommended implementation order
- estimated effort by subsystem
- suggested first Unity milestones
- known assumptions and what should be verified first during implementation

This document should be concise compared with the main docs, but specific and actionable.

## T305 - Finalization Commit

After all review and fixes are complete:

- ensure no Phase 3 task remains `[~] ACTIVE`
- commit the final Phase 3 changes atomically
- record the final commit hash and summary in `.ralph/agent/handoff.md`

## Required Completion Evidence Block

After each Phase 3 task, append a block like this to `.ralph/agent/handoff.md`:

```md
## Phase 3 - Task T3xx complete
- Deliverables: <files changed>
- Audit scope: <what was checked>
- Problems found: <count and short summary>
- Fixes applied: <short list or `none`>
- Verification excerpt: <one meaningful line from a doc, audit summary, or fix>
- Git: <commit hash or commit subject>
- Next recommended Phase 3 task: <task id or `none`>
```

A Phase 3 task is not complete until this evidence exists.

## Final Review Summary Block

When you believe Phase 3 is complete, append this to `.ralph/agent/handoff.md`:

```md
## Phase 3 final review summary
- UNCLEAR inventory complete: yes/no
- Source references validated: yes/no
- Consistency audit passed: yes/no
- Final review document present: yes/no
- Remaining blockers: <list or `none`>
- Implementation readiness: READY | NOT_READY
```

If the result is `NOT_READY`, continue Phase 3 work immediately.

## Phase 3 Completion Gate

Phase 3 is complete only when **all** of the following are true:

1. `generated_documentation/gui/gui_99_final_review.md` exists and contains all required sections.
2. Every Phase 2 core document still exists and any broken/stale references have been fixed.
3. Every discovered `[UNCLEAR]` item has been inventoried and classified.
4. `.ralph/agent/handoff.md` contains the final review summary with `Implementation readiness: READY`.
5. No Phase 3 task remains `[~] ACTIVE`.
6. All Phase 3 work is committed.

Only then emit the configured completion token once, on its own line, and nothing else after it.
