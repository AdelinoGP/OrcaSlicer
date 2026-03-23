# Memories

## Patterns

### mem-1773981659-cd27
> SKIP_TRIVIAL: src/slic3r/GUI/AboutDialog.hpp
<!-- tags: gui, annotation, skip | created: 2026-03-20 -->

### mem-1773981636-e5ed
> SKIP_TRIVIAL: src/slic3r/GUI/AboutDialog.cpp
<!-- tags: gui, annotation, skip | created: 2026-03-20 -->

### mem-1773981616-d20e
> SKIP_TRIVIAL: src/slic3r/GUI/3DBed.cpp
<!-- tags: gui, annotation, skip | created: 2026-03-20 -->

### mem-1773981590-bd5c
> SKIP_TRIVIAL: src/slic3r/GUI/2DBed.hpp
<!-- tags: gui, annotation, skip | created: 2026-03-20 -->

### mem-1773981566-14a6
> SKIP_TRIVIAL: src/slic3r/GUI/2DBed.cpp
<!-- tags: gui, annotation, skip | created: 2026-03-20 -->

### mem-1773855069-f68b
> MainFrame.cpp annotated: maps to MainUIController managing the main application panels (Prepare, Preview, Monitor, etc.). Includes complex configuration change propagation and parallel thumbnail loading.
<!-- tags: gui, unity, mainframe, threading | created: 2026-03-18 -->

### mem-1773854191-51bd
> MainFrame.hpp annotated: Maps to MainUIController MonoBehaviour managing various UI Panels.
<!-- tags: gui, unity, mainframe | created: 2026-03-18 -->

### mem-1773803554-99cc
> wxWidgets to Unity porting: Use [UNITY] tag with specific component names (e.g., MonoBehaviour, UnityWebRequest, Job System)
<!-- tags: unity, porting, wxwidgets | created: 2026-03-18 -->

### mem-1773637307-3ea2
> Voronoi offset operations require careful handling of distance parameters and polygon counts
<!-- tags: voronoi, offset, polygons | created: 2026-03-16 -->

### mem-1773637302-2f9e
> Voronoi tests use Boost Polygon library with specific issue tickets (#12067, #12707, #12903, #12139)
<!-- tags: voronoi, boost, issues | created: 2026-03-16 -->

## Decisions

## Fixes

### mem-1774268687-da18
> failure: cmd=cmake -S . -B build -DCMAKE_BUILD_TYPE=Release, error=source directory does not contain CMakeLists.txt, next=verify build instructions or skip build for this repo
<!-- tags: tooling, build | created: 2026-03-23 -->

### mem-1774054791-9e67
> failure: cmd=read src/slic3r/GUI/Files/SVG.hpp, error=File not found, next=confirm manifest entry is stale or remove task
<!-- tags: gui, missing-file | created: 2026-03-21 -->

### mem-1774054728-14ba
> failure: cmd=read src/slic3r/GUI/Files/SVG.cpp, error=File not found, next=confirm manifest or mark gui:T201 blocked
<!-- tags: gui, missing-file | created: 2026-03-21 -->

### mem-1774054659-a328
> failure: cmd=read src/slic3r/GUI/Jobs/SVGFileJob.hpp, error=File not found, next=confirm manifest or mark gui:T200 blocked
<!-- tags: gui, missing-file | created: 2026-03-21 -->

### mem-1774053407-7791
> failure: cmd=read src/slic3r/GUI/Jobs/SVGFileJob.cpp, error=File not found, next=verify actual SVG job files or mark gui:T199 blocked
<!-- tags: gui, missing-file | created: 2026-03-21 -->

### mem-1774050430-757b
> failure: cmd=read src/slic3r/GUI/Jobs/RotoptJob.hpp, error=File not found, next=align tasks to RotoptimizeJob files
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774050410-47df
> failure: cmd=read src/slic3r/GUI/Jobs/RotoptJob.cpp, error=File not found, next=confirm whether the job renamed to Rotoptimize or document blocker
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774050367-c43a
> failure: cmd=read src/slic3r/GUI/Jobs/MedialAxisJob.hpp, error=File not found, next=check if header exists elsewhere or mark T194 blocked
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774047878-3f83
> failure: cmd=read src/slic3r/GUI/Jobs/LightJob.hpp, error=File not found, next=check for renamed LightJob classes or mark gui:T192 blocked
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774047667-757c
> failure: cmd=read src/slic3r/GUI/Jobs/MedialAxisJob.cpp, error=File not found, next=confirm path or mark gui:T193 as blocked
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774047571-33c9
> failure: cmd=read src/slic3r/GUI/Jobs/LightJob.cpp, error=File not found, next=confirm actual path or mark gui:T191 as blocked
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774047463-8c02
> failure: cmd=read src/slic3r/GUI/Jobs/JobList.hpp, error=File not found, next=confirm correct path or raise blocker
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774045921-8109
> failure: cmd=read src/slic3r/GUI/Jobs/JobList.cpp, error=File not found, next=confirm if JobList exists elsewhere or mark gui JobList task as failed
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774045479-639a
> failure: cmd=read src/slic3r/GUI/Jobs/Job.cpp, error=File not found, next=confirm whether Job.cpp exists elsewhere or adjust tasks
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774045265-c455
> failure: cmd=read src/slic3r/GUI/Jobs/EmbossUpdateJob.hpp, error=File not found, next=check if header exists in repo or update manifest
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774045174-d236
> failure: cmd=read src/slic3r/GUI/Jobs/EmbossUpdateJob.cpp, error=File not found, next=confirm file path or task manifest and either create skip entry or locate actual file
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1773994151-0897
> failure: cmd=read src/slic3r/GUI/PalmTree.hpp, error=File not found. task-1773880086-94d5 failed.
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1773988211-13e6
> failure: cmd=read src/slic3r/GUI/PalmTree.cpp, error=File not found and no PalmTree match under src, next=fail stale runtime task and reconcile runtime queue with .ralph/ralph-tasks.md before selecting next annotation target
<!-- tags: gui, missing-file, tasking | created: 2026-03-20 -->

### mem-1773981545-7bab
> failure: cmd=edit, error=LSP compilation errors (inconsistent file state or header removal/missing includes), next=annotate in smaller, more surgical edits
<!-- tags: gui, tooling, edit | created: 2026-03-20 -->

### mem-1773963015-6a1e
> failure: cmd=edit, error=LSP compilation errors (inconsistent file state or header removal), next=annotate in smaller, more surgical edits
<!-- tags: gui, tooling, edit | created: 2026-03-19 -->

### mem-1773962783-01f5
> File src/slic3r/GUI/DPIFrame.cpp listed in task manifest does not exist in src/slic3r/GUI/.
<!-- tags: gui, missing-file | created: 2026-03-19 -->

### mem-1773799068-f3c8
> failure: cmd=git add agent_journal_gui.md .ralph/agent/scratchpad.md && git diff --cached -- agent_journal_gui.md .ralph/agent/scratchpad.md, exit=128, error=Unable to create .git/index.lock because a lock file already exists, next=inspect whether the lock is stale before retrying git staging
<!-- tags: git, error-handling, tooling | created: 2026-03-18 -->

### mem-1773637309-e58b
> Missing Voronoi vertices can be repaired via rotation-based mechanism with angles π/6, π/5, π/7, π/11
<!-- tags: voronoi, repair, rotation | created: 2026-03-16 -->

## Context
