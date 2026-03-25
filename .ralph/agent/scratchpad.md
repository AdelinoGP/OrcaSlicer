# Phase 1 GUI Annotation - Scratchpad

## Current Understanding
- Phase 1 annotation is ongoing, with many tasks already completed (T101-T378 done, some pending)
- I see from handoff.md that tasks up to T510 have been completed (T502, T505-T510)
- However, ralph-tasks.md shows many tasks before T500 are still pending (e.g., T177, T180, etc.)
- The ready-tasks list includes tasks starting from T511 onward, but there are gaps in numbering
- Need to reconcile the state: determine which tasks are truly pending vs. already done
- The objective is to annotate every in-scope GUI file for Unity port preparation
- Must follow the standard work loop: pick next eligible Phase 1 task, mark active, read file, annotate/classify, add evidence, commit, continue

## Plan
1. First, read the full ralph-tasks.md to understand the current state
2. Identify the next eligible Phase 1 task (likely one of the pending tasks in the list)
3. Check if there are any blocked tasks and record blockers
4. Follow the priority order: app lifecycle, viewport, main windows, configuration, dialogs, background-process, utilities
5. Use the ready-tasks list as a guide but also look at earlier pending tasks
6. Start with a task that is unblocked and has a file that exists (avoid known missing files)
7. Annotate the file according to the standard
8. Add evidence to handoff.md, commit, and continue

## Notes from Memories
- Several files are missing (PalmTree, DPIFrame, etc.) - skip those if they don't exist
- There are patterns for annotation (mem-1774420576-b412)
- Some files are corrupted (OAuthJob.cpp) - need to handle carefully
- Use [UNITY] tags with concrete Unity component names
- Skip trivial files if they are under 50 lines and semantically inert

## Next Steps
- Completed T177: verified existing annotations in BedShapeDialog.cpp, marked task done, committed.
- T180 (BindDialog.hpp) is next pending task, but we stopped after one task as per prompt.
- Leave T180 as started for next iteration.
- Other pending tasks: T180, T184, T185, etc. need annotation.
- Ensure each file is accounted for in audit.