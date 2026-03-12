# Agent Prompt: Resolve Outstanding [UNCLEAR] Tags

## Context

This codebase (OrcaSlicer, C++17 3D slicer) has been annotated across 98 sessions.
During annotation, questions that could not be answered from local context were
marked `[UNCLEAR → ESCALATED]`. The previous session's journal overstated the
count at 51 — a grep of the source reveals the actual inventory is:

- 12 tags marked `[UNCLEAR → ESCALATED]` (explicitly deferred)
- 1 bare tag `[UNCLEAR: verify this]` (never given a disposition)

Your job is to resolve every one of these tags. Do not create any new files.
Do not modify any source code logic. Only update the annotation comments in place.

---

## Step 1: Find All Unresolved Tags

Before touching anything, run this search to get the authoritative current list:

```
grep -rn "UNCLEAR → ESCALATED\|UNCLEAR:" src/libslic3r/ src/slic3r/
```

Record every result. Do not rely on the agent_journal.md list — it is stale.
The canonical list is in the source files.

---

## Step 2: Resolution Protocol

For **each** tag found in Step 1, follow this procedure:

### 2a. Read the context window

Read ±50 lines around the tag in the file where it lives.
Then read the `.hpp` counterpart if the tag is in a `.cpp`, or vice versa.

### 2b. Trace call sites

Use grep to find callers of the function/method that contains the tag.
Read the top 2–3 call sites. Often the intent is clear from how the function
is used, even if the implementation is silent.

### 2c. Check the upstream lineage

OrcaSlicer is a fork of Bambu Studio → PrusaSlicer → Slic3r. For tags about
algorithmic choices or obscure magic constants, search the Git log or look for
equivalent code in reference files:
- `generated_documentation/pseudocode_arachne_straight_skeleton.md`
- `generated_documentation/03_algorithmic_complexities.md`
- Any existing `[INTENT]` comment in the same file that gives broader context

### 2d. Write the resolution

Replace the `[UNCLEAR → ESCALATED]` or `[UNCLEAR: ...]` text with one of:

**If you were able to determine the answer:**
```cpp
// [UNCLEAR → RESOLVED] <one to three sentences explaining the actual answer.
//   What is this constant / branch / behavior actually doing, and why.>
```

**If the resolution appears to be a hardcoded magic number:**
```cpp
// [UNCLEAR → MAGICNUMBER] <one to three sentences explaining the actual answer.
//   What is this constant / branch / behavior actually doing, and why.>
```

**If after Steps 2a–2c the answer is genuinely unknowable from static analysis:**
```cpp
// [UNCLEAR → UNRESOLVABLE] <explain specifically what information is missing.
//   E.g. "requires running the slicer under a profiler" or "requires reading
//   the original CuraEngine commit history". Do not leave a vague placeholder.>
```

Do **not** leave any tag as `[UNCLEAR → ESCALATED]` — that state means
"deferred to next session", which is now this session.

---

## Step 3: Process Each Tag

Here are the 13 tags found at the time of the last audit (line numbers may
have shifted; trust Step 1's grep over this list):

| # | File | ~Line | Summary of question |
|---|------|-------|---------------------|
| 1 | Emboss.hpp | 148 | Non-atomic `shared_ptr` field — is external synchronization guaranteed? |
| 2 | CutUtils.cpp | 29 | Does `PlaceOnCutLower \|\| FlipLower` coupling to `post_process()` reflect intentional design? |
| 3 | CutUtils.cpp | 336 | Why does `PlaceOnCutLower` imply flipping? |
| 4 | VariableWidth.cpp | 26 | Is the `length * a_width` trapezoid-approximation bias intentional? |
| 5 | VariableWidth.cpp | 153 | Is the last-segment potential under-extrusion deliberate? |
| 6 | FillTpmsD.cpp | 176 | Why are 16 segments chosen as the TPMS wave sampler seed count? |
| 7 | PressureEqualizer.cpp | 900 | Why did the cross-role propagator replace the commented-out role-specific clamping? |
| 8 | SkeletalTrapezoidation.cpp | 1913 | Is sampling toolpath locations past the midline actually safe? |
| 9 | Measure.cpp | 326 | Why are `err < 0.05` and `arc span > 81°` hardcoded as RANSAC thresholds? |
| 10 | SupportSpotsGenerator.cpp | 38 | Was the block-commented support-point logic intentionally disabled? |
| 11 | TriangleMesh.hpp | 220 | Is scaling cached volume by the affine determinant valid under shear? |
| 12 | TriangleMesh.cpp | 407 | Do any callers rely on accurate volume after a shear transform? |
| 13 | FillAdaptive.cpp | 1777 | Bare `[UNCLEAR: verify this]` — was the transform pre-applied to the mesh? |

### Approach hints by tag

**Tags 2–3 (CutUtils):** Read `CutUtils.hpp` for the `PlaceOnCutLower`/`FlipLower`
enum definition. Then grep for all callsites of `post_process()` and
`process_object_on_cut()` to understand whether the flip is structural
(for correct face orientation on the cut plane) or incidental.

**Tags 4–5 (VariableWidth):** Read VariableWidth.cpp fully. These are about
extrusion-width arithmetic for variable-width toolpaths. The `a_width` variable
and `length * a_width` expression may be intentional linear interpolation —
compare with the surrounding trapezoid-width loop above it.

**Tag 6 (TPMS):** Read `FillTpmsD.cpp:140–200`. The 16-segment count is a
sampling discretization. Check whether 16 corresponds to Nyquist for the
gyroid/D-surface spatial frequency at the print scale.

**Tags 11–12 (TriangleMesh shear volume):** Grep for callers of
`TriangleMesh::transform()` and `its_transform()`. Check whether any caller
passes a matrix with non-zero off-diagonal shear terms. If every caller is
scale/rotation only, the determinant formula is exact and the ESCALATED tag
can be downgraded to RESOLVED.

**Tag 10 (SupportSpotsGenerator):** Read the commented-out block. If it has
a TODO or a version comment, note it. Then check git log or grep for any
related issue/PR comment in the existing documentation.

---

## Step 4: Update agent_journal.md

After all tags are resolved, add a new session entry at the top of
agent_journal.md (do not create a new file):

```
## Session N — UNCLEAR Tag Resolution Pass

**Goal:** Resolve all remaining [UNCLEAR → ESCALATED] and bare [UNCLEAR] tags.

**Files modified:**
- List each .cpp/.hpp file where a tag was updated

**Resolution summary:**
- RESOLVED: N tags (list each with one-line answer)
- UNRESOLVABLE: N tags (list each with reason)

**Method used per tag:** (brief note per tag)
```

Then update the living status block at the very top of agent_journal.md:
```
Unresolved [UNCLEAR] tags: <new count — 0 if fully resolved>
```

---

## Constraints

- Do **not** alter any logic, variable names, or code structure.
- Do **not** create new `.md` files.
- Each modified source file must remain syntactically valid C++ after your edit.
- Commit each file individually with message:
  `annotate: resolve UNCLEAR tags in <filename> (UNCLEAR resolution pass)`
