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
