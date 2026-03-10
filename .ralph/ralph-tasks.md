## Phase BOOTSTRAP — Housekeeping (Complete before resuming Phase 1/2)

- [ ] TB001  BOOTSTRAP · Annotate `\.ralph\ralph-tasks.md` with Phase 1/2 tasks and
             backfill all already-completed tasks as [x] DONE,
             using the session log in agent_journal.md as the source of truth.
             Assign T1xx IDs to every annotated file. Commit with prefix `docs:`.

- [ ] TB002  BOOTSTRAP · Add the CURRENT STATUS living block to the very top of
             `agent_journal.md` (above Session 1). Populate it from Session 84
             state: annotation pass complete, Phase 2 docs in progress, H1191 is
             next hazard ID, open questions from Session 1 items 1–4 — verify
             which are still unresolved and list them. Commit with prefix `docs:`.

- [ ] TB003  BOOTSTRAP · Compact `agent_journal.md`. The file is ~6000 lines
             covering 84 sessions. Archive Sessions 1–83 into a single new file
             `agent_journal_archive_s01_s83.md` (verbatim, no summarization),
             then replace those sessions in agent_journal.md with a single
             two-line entry:
               `## Sessions 1–83 — Archived`
               `Full log: agent_journal_archive_s01_s83.md`
             Keep Session 84 and the new CURRENT STATUS block intact.
             Commit with prefix `docs:`.

- [ ] TB004  BOOTSTRAP · Add the `## Critical Blockers (P1/High — Read First)`
             section to `04_refactoring_hazards.md`, immediately after the Table
             of Contents. Scan the entire file for every entry marked `P1/High`
             and list each as a single line:
               `H<id> · <10-word description> · <file:line>`
             Do not paraphrase existing entries — extract directly.
             Commit with prefix `docs:`.

- [ ] TB004b  BOOTSTRAP · Audit `04_refactoring_hazards.md` for misplaced content:
              find any multi-paragraph algorithm explanations that duplicate
              `03_algorithmic_complexities.md` and replace them with a single
              cross-reference line pointing to the relevant section of that file.
              Do NOT remove any H-numbered hazard entries. Do NOT summarize
              individual entries. Only remove prose that belongs in a different
              document. Commit with prefix `docs:`.

- [ ] TB005  BOOTSTRAP · Resolve or explicitly escalate the four open questions
             recorded in Session 1 of agent_journal.md:
             (1) Does slice_mesh use the admesh adjacency table or rebuild it?
             (2) Exact memory ownership of Layer objects (raw vs unique_ptr)?
             (3) Are Arachne and classic PerimeterGenerator mutually exclusive?
             (4) What is the purpose of ClipperZUtils Z-metadata tagging?
             For each: read the relevant source file, write a one-paragraph
             resolution into the CURRENT STATUS open questions block, and add
             an [UNCLEAR] → [RESOLVED] annotation at the original call site.
             Commit with prefix `annotate:`.

## Phase 1 - Documentation

- [ ] T101 

## Phase 2 — Documentation Gaps (Resume after Bootstrap is complete)

- [ ] T204  Create `05_external_dependencies.md`. Follow the structure in the
            prompt exactly. Priority order: Clipper/Clipper2 → TBB → Eigen →
            admesh → OpenVDB → remaining 16 libraries. For each: API surface
            used, algorithmic role, porting strategy (REPLICATE / FIND_EQUIVALENT
            / PORT_REQUIRED / GUI_ONLY), and hazards. Commit with prefix `docs:`.

- [ ] T210  Add missing data structures to `02_core_data_structures.md`:
            SupportLayer + SupportLayerPtrs, FillAdaptive::Octree,
            FillLightning::Generator (note raw pointer hazard from Fill.cpp),
            UndoRedo::StackImpl (cereal stack + friend class coupling),
            TriangleSelector (lazy raw ptr on ModelVolume, paint-on operations),
            PlaceholderParser runtime state (Spirit X3 AST, expression evaluator).
            Use the same format as existing entries: fields, invariants,
            lifecycle, consumers. Commit with prefix `docs:`.

- [ ] T211  Add missing algorithm sections to `03_algorithmic_complexities.md`:
            (a) ArcFitter.cpp — segment-to-arc conversion (G2/G3), including
                the chord-error threshold and the minimum-arc-length guard.
            (b) MultiMaterialSegmentation.cpp — the painting-based filament
                region assignment algorithm (BBS addition; entirely absent
                despite being noted in Session 2 journal).
            (c) Arachne straight skeleton — explain the medial axis computation
                that drives variable-width perimeters; currently named but not
                explained in Section 2.
            Commit with prefix `docs:`.

- [ ] T205A  Create `pseudocode_triangle_mesh_slicer_chaining.md` — Phase 2
             line-chaining algorithm (hash map assembly → greedy chain walk →
             T-junction handling → degenerate discard). This is the phase most
             obscured by the C++ implementation. Include Translation Notes
             flagging: implicit endpoint ID encoding, the NO_SEED/SKIP flag
             state machine, and the per-layer mutex scheme.
             Register as ACTIVE in ralph-tasks.md before starting.
             Commit with prefix `docs:`.

- [ ] T205B  Create `pseudocode_fill_lightning.md` — the branch extension loop
             (unlit-point coverage → branch growth → merging heuristic). Flag
             the raw Generator pointer lifecycle in Translation Notes.
             Commit with prefix `docs:`.

- [ ] T205C  Create `pseudocode_multimaterial_segmentation.md` — only after
             T211(b) is complete, since the algorithm section must exist first.
             Commit with prefix `docs:`.

- [ ] T205D  Create `pseudocode_seam_placer.md` — visibility scoring
             (hemisphere raycasting → score accumulation) and B-spline alignment
             loop. Flag the monotonic-Z assumption in Translation Notes.
             Commit with prefix `docs:`.
