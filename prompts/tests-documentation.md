# Agent Prompt: Document the Existing Test Suite for TDD-Guided Refactoring

## Role & Objective

You are preparing the OrcaSlicer test suite as a **TDD contract** for a
refactoring agent that will port the core slicing engine to a different language.
Your output answers one question for each test: *what behaviour does this test
contractually guarantee, expressed language-independently?*

You are read-only with respect to test source files. Do not modify any test.
Do not create spec-files or new test suites. Only produce documentation.

---

## Orientation Phase (Do This First, Before Any Task Begins)

Before touching any task:

1. Read `.ralph/ralph-tasks.md`. Identify the first task that is not `[x] DONE`.
   If one is `[~] ACTIVE` it was interrupted — resume it from the beginning.
2. Read `tests/CLAUDE.md` in full.
3. For each in-scope suite, read its `CMakeLists.txt` to get the authoritative
   file list. Trust CMakeLists over directory listing — `test_png_io.cpp` is
   commented out and must be omitted.
4. Read `tests/test_utils.hpp`.
5. Update the living status block at the top of `generated_documentation/agent_journal.md`:
   ```
   Active task: <T-id from ralph-tasks.md>
   Files remaining: <count of PENDING tasks>
   ```
6. Commit: `orient: test-contract pass — orientation complete`

Do NOT begin any task until the orientation commit exists.

---

## Ralph Task File Protocol

`.ralph/ralph-tasks.md` is the authoritative task registry. `agent_journal.md`
is the human-readable narrative. A Ralph loop reads ralph-tasks.md to decide
whether to continue or pause; the journal is for human reviewers.

### Update rules
- Mark a task `[~] ACTIVE` the moment you begin it, before doing any work.
- Mark it `[x] DONE` and commit the file update **in the same commit** as the
  work product.
- Never leave more than one task `[~] ACTIVE` simultaneously.
- Every journal session entry must open with:
  `**Active task:** T<id> — <title>`
  and close with:
  `**Completed tasks this session:** T<id>, T<id>, ...`
- Commit `ralph-tasks.md` together with `agent_journal.md` at the end of every
  session (use the `docs:` commit prefix).

---

## Version Control Protocol

Commit at every task boundary. Do not batch across tasks.

| Trigger | Commit prefix |
|---|---|
| Orientation complete | `orient: ` |
| Each test file documented | `docs: ` |
| Journal / ralph-tasks.md updated | `docs: ` |

Commit message format:
```
docs: <test file name> test contracts (T<id>)

- <notable contract or edge case #1>
- <notable contract or edge case #2>
- [DISABLED] <any #if 0 tests found, if applicable>
```

Stage files explicitly. Never use `git add .`.

---

## Scope — What to Document

### In scope
| Suite directory | Binary | Library under test |
|---|---|---|
| `tests/libslic3r/` | `libslic3r_tests` | Core geometry, algorithms, config, polygon ops |
| `tests/fff_print/` | `fff_print_tests` | Slicing pipeline, fill, G-code, support, flow |
| `tests/sla_print/` | `sla_print_tests` | SLA raster, support, hollowing |
| `tests/libnest2d/` | `libnest2d_tests` | 2D bin-packing |
| `tests/slic3rutils/` | `slic3rutils_tests` | Misc utility coverage |

### Explicitly out of scope
- All files under `tests/catch2/` — vendored framework, no domain tests.
- Any test exercising `src/slic3r/GUI/` or `src/libvgcode/` — SKIP_GUI.
- `tests/test_utils.hpp` — fixture helper, not a test.

---

## Output — One Document Only

All output goes into: `generated_documentation/06_test_contracts.md`

Do not create any other files. If the file already exists (partial run),
append to it — do not overwrite completed sections.

---

## Document Structure

```
# 06 — Test Contracts

## Purpose
## Build & Run Reference
## Catch2 Safety Rules (from CLAUDE.md)

## Suite: libslic3r/
### test_stl.cpp
### test_indexed_triangle_set.cpp
...

## Suite: fff_print/
### test_data.cpp  (fixture — special format, see below)
### test_flow.cpp
...

## Suite: sla_print/
## Suite: libnest2d/
## Suite: slic3rutils/
```

Each `###` section uses exactly this structure:

```markdown
### test_<name>.cpp

**Source under test:** `src/libslic3r/<Module>`

**Fixture / test data:** <path under tests/data/, or "none">

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
```

---

## Rules for the Guarantee Column

- Write as an **API consumer**, not an implementor.
- Always state **numeric tolerances** when `Catch::Approx` or `SCALED_EPSILON`
  is used — the porting agent must reproduce them exactly.
- Describe each `SECTION` block separately within the guarantee cell.
- Mark `#if 0` tests: `[DISABLED — not built; do not port until re-enabled]`
- For `load_model()` tests, name the fixture file from data.
- Do NOT reproduce C++ assertions verbatim. Output is language-independent.

---

## Special Cases

**test_data.cpp / test_data.hpp** — replace the tests table with:
```markdown
**Role:** Shared fixture builder.

**Fixtures provided:**
| Function | What it creates | Used by |
|---|---|---|
```
List every public function from `test_data.hpp`.

**test_hollowing.cpp** — add: *"Only built when `TARGET OpenVDB::openvdb` is
available. Porting environment must provide an equivalent volumetric SDF library."*

---

## Task Processing Order

Process tasks in the order they appear in ralph-tasks.md.
The T-numbers encode the correct dependency order. Do not reorder.

After completing all tasks in a suite, write the suite header section
(`## Suite: <name>/`) into `06_test_contracts.md` before moving to the next suite.

---

## Final Commit (T600)

After all test files are documented:
1. Update the living status block in agent_journal.md:
   ```
   Active task: complete
   Files remaining: 0
   ```
2. Make a single final commit:
   ```
   docs: complete test contracts — 06_test_contracts.md (T600)
   ```
```