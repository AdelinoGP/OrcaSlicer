# Ralph Task Registry — OrcaSlicer GUI Analysis Agent

**Created:** 2026-03-18 (Updated for Phase 1 Progress)  
**Project:** OrcaSlicer Unity Port - GUI Architecture Analysis  
**Agent Status:** Phase 1 🎯 IN PROGRESS
**Last updated:** 2026-03-18 17:30 UTC

---

## LEGEND

| Symbol | Status | Description |
|--------|--------|-------------|
| `[x]` | DONE | Task completed and verified |
| `[~]` | ACTIVE | Currently in progress |
| `[!]` | BLOCKED | Awaiting external dependency |
| `[ ]` | PENDING | Queued for next phase |
| `[-]` | SKIPPED | Not applicable / intentionally omitted |

---

## PHASE 0: ORIENTATION - **COMPLETE** ✓

**Execution Date:** 2026-03-18  
**Status:** 9/9 tasks complete  
**Outcome:** Foundation established, documentation generated

### Phase 0 Tasks

- [x] **P0-T001**: Repository State Verification  
  *Evidence:* `agent/gui-analysis` branch confirmed, untracked changes noted

- [x] **P0-T002**: Create Working Branch  
  *Skip:* Already on `agent/gui-analysis`

- [x] **P0-T003**: GUI Directory Census  
  *Total:* 719 files (.cpp/.hpp)  
  *Manifest:* `/tmp/gui_file_manifest.txt`  
  *Range:* `src/slic3r/GUI/`, `src/libvgcode/`, `src/slic3r/Utils/`

- [x] **P0-T004**: Entry Point Trace ✓  
  *Deliverable:* `generated_documentation/gui/entry_point_trace.md`  
  *Key Finding:* 6-level call chain (main → CLI::run → GUI_Run → wxEntry → GUI_App::OnInit → on_init_inner)  
  *Branching Logic:* `src/OrcaSlicer.cpp:1349` - `m_actions.empty() && !downward_check`

- [x] **P0-T005**: Application Class Identification (GUI_App) ✓  
  *Deliverable:* `generated_documentation/gui/gui_app_class.md`  
  *Files:* `src/slic3r/GUI/GUI_App.hpp`, `src/slic3r/GUI/GUI_App.cpp`  
  *Members:* 10 documented (m_initialized, m_post_initialized, m_app_mode, m_opengl_mgr, m_device_manager, etc.)  
  *OnInit Sequence:* 5 operations documented

- [x] **P0-T006**: Main Window Class Identification (MainFrame) ✓  
  *Deliverable:* `generated_documentation/gui/main_window_class.md`  
  *Files:* `src/slic3r/GUI/MainFrame.hpp`, `src/slic3r/GUI/MainFrame.cpp`  
  *Children:* 15 direct widgets documented (tabpanel, topbar, dialogs, panels)  
  *Hierarchy:* Notebook-based tab system with Plater core

- [x] **P0-T007**: Create Output Directories  
  *Created:* `generated_documentation/gui/`, `.ralph/` (existed)

- [x] **P0-T008**: Initialize Task Registry  
  *File:* `.ralph/ralph-tasks.md` (this template)

- [x] **P0-T009**: Commit Orientation Complete  
  *Evidence:* Commit `0dd3c23268 orient(gui): complete Phase 0 orientation` verified ✓

**Phase 0 Result:** Complete orientation foundation with detailed architecture documentation  
**Status:** Ready for Phase 1 - Annotation Loop (starting with T101)

---

## PHASE 1: ANNOTATION LOOP - **IN PROGRESS** 

**Objective:** Annotate all GUI source files with porting tags  
**Scope:** 719+ files identified in Phase 0 census  
**Tags:** [INTENT], [STATE], [EVENT], [THREAD], [UNITY], [PORTING_HAZARD]

### Priority Groups

**Group 1: Application Core (Critical - Start Here)**
- [x] **T101-part1**: `src/slic3r/GUI/GUI_App.cpp` (1-2000)
- [x] **T101-part2**: `src/slic3r/GUI/GUI_App.cpp` (2000-4000)
- [x] **T101-part3**: `src/slic3r/GUI/GUI_App.cpp" (4000-6000)
- [x] **T101-part4**: `src/slic3r/GUI/GUI_App.cpp` (6000-8070)

- [x] **T102**: `src/slic3r/GUI/GUI_App.hpp`
  
- [x] **T103**: `src/slic3r/GUI/GUI_Init.cpp` & `.hpp`
  - *Focus:* GUI_Run() bootstrap sequence
  - *Priority:* High - Entry point to Unity world
  - *Evidence:* Annotated entry point with [INTENT], [UNITY], [EVENT] tags

**Group 2: Window Management (High)**
- [x] **T110**: `src/slic3r/GUI/MainFrame.hpp`
- [x] **T111-part1**: `src/slic3r/GUI/MainFrame.cpp` (1-2000)
- [x] **T111-part2**: `src/slic3r/GUI/MainFrame.cpp` (2000-4000)
- [x] **T111-part3**: `src/slic3r/GUI/MainFrame.cpp` (4000-4572)
  - *Subset methods:* Constructor, init_tabpanel(), init_menubar()
- [ ] **T112**: Notebook/Tab system classes

**Group 3: Core Workspace (Critical)**
- [x] **T120**: `src/slic3r/GUI/Plater.hpp`
- [x] **T121-part1**: `src/slic3r/GUI/Plater.cpp` (1-2000)
- [x] **T121-part2**: `src/slic3r/GUI/Plater.cpp` (2001-4000)
- [ ] **T121-part3**: `src/slic3r/GUI/Plater.cpp` (4001-6000)
- [ ] **T121-part4**: `src/slic3r/GUI/Plater.cpp` (6000-8000)
- [ ] **T121-part5**: `src/slic3r/GUI/Plater.cpp` (8000-10000)
- [ ] **T121-part6**: `src/slic3r/GUI/Plater.cpp` (10000-12000)
- [ ] **T121-part7**: `src/slic3r/GUI/Plater.cpp` (12000-14000)
- [ ] **T121-part8**: `src/slic3r/GUI/Plater.cpp` (14000-16000)
- [ ] **T121-part9**: `src/slic3r/GUI/Plater.cpp` (16000-18178)
- [ ] **T122**: `src/slic3r/GUI/GLCanvas3D.hpp` & `.cpp`
  - *Focus:* OpenGL rendering viewport
- [ ] **T123**: `src/slic3r/GUI/3DScene.hpp" & `.cpp`


**Group 4: Panels & Dialogs (Medium)**
- [ ] **T130**: MonitorPanel  
- [ ] **T131**: ParamsPanel  
- [ ] **T132**: WebViewPanel  
- [ ] **T133**: Dialog implementations

**Group 5: Systems & Utilities (Medium)**
- [ ] **T140**: Preset/Config management
- [ ] **T141**: DeviceManager & NetworkAgent
- [ ] **T142**: PrintHost/job queues
- [ ] **T143**: File I/O, Import/Export

**Group 6: Remaining 600+ files (Low - Background)**
- [ ] **T150+**: libvgcode visualization
- [ ] **T151+**: Utils modules
- [ ] **T152+**: Event/Jobs subsystems

**Phase 1 Progress:** 6/719 files annotated

---

## PHASE 2: DOCUMENTATION GENERATION - **PENDING**

**Objective:** Structured technical docs for Unity migration  
**Input:** Phase 1 annotations  
**Output:** Practical porting guides

### Phase 2 Tasks (24 deliverables)

- [ ] **T201**: GUI_App Architecture Report
- [ ] **T202**: MainFrame Widget Hierarchy & Lifecycle
- [ ] **T203**: Plater Core Workspace Architecture
- [ ] **T204**: Event System Mapping wxWidgets → Unity
- [ ] **T205**: OpenGL Rendering → Unity Pipeline
- [ ] **T206**: Data Flow: Model Load → Display
- [ ] **T207**: Preset/Config System Architecture
- [ ] **T208**: Device/Network Management Patterns
- [ ] **T209**: Platform-Specific Code Strategy
- [ ] **T210**: Multi-Plate/Plate System Analysis
- [ ] **T211**: User Authentication/Cloud Sync
- [ ] **T212**: Print Job Queue Architecture
- [ ] **T213**: File I/O and Integration
- [ ] **T214**: G-Code Viewer vs Editor Modes
- [ ] **T215**: Cal-/wizards/workflow
- [ ] **T216**: 3MF/STL/OBJ Format Handling
- [ ] **T217**: UI Theming & Localization
- [ ] **T218**: Crash Reporting & Analytics
- [ ] **T219**: Plugin/Extension System
- [ ] **T220**: Settings/Preferences Architecture
- [ ] **T221**: Model Library/Gallery System
- [ ] **T222**: Tutorial/Onboarding Flow
- [ ] **T223**: Performance Metrics & Optimization
- [ ] **T224**: Unity Component Mapping Summary

**Phase 2 Progress:** 0/24 docs pending

---

## PHASE 3: AUDIT AND REVIEW - **PENDING**

**Objective:** Validate findings, create final migration plan  
**Output:** Risk assessment and phased implementation strategy

### Phase 3 Tasks (11 deliverables)

**Architecture Audit:**
- [ ] **T301**: Identify P1 (Critical) Porting Hazards
  - wxApp singleton → MonoBehaviour pattern
  - OpenGL context dependencies
  - Main thread UI assumptions
  
- [ ] **T302**: Identify P2 (Significant) Hazards
  - Cross-thread event buses
  - File system watchers
  - Platform I/O
  
- [ ] **T303**: Verify Annotation Coverage
  - Check 100% of Phase 1 promised files annotated
  - Resolve gaps, mark [!] BLOCKED items
  
- [ ] **T304**: Cross-Reference Design Docs
  - Compare annotations with Unity best practices
  - Validate completeness

**Migration Strategy:**
- [ ] **T305**: Create Phased Migration Roadmap
  - Phase A: Core lifecycle (App → MonoBehaviour)
  - Phase B: Rendering (OpenGL → Unity RenderPipeline)
  - Phase C: UI (wxWidgets → UI Toolkit)
  - Phase D: Systems (Network, File I/O, Workflows)
  
- [ ] **T306**: Risk Mitigation Plan
  - P1 hazards with solutions/alternatives
  
- [ ] **T307**: Validation/Testing Strategy
  - How to verify Unity port equivalence

**Final Deliverables:**
- [ ] **T308**: Final Integration Report
  - All findings, hazards, solutions
  
- [ ] **T309**: Architecture Diagrams
  - Component relationships, data flow, event charts
  
- [ ] **T310**: Unity Component Library Proposal
  - GameObjects, prefabs, script structure
  
- [ ] **T311**: Migration Timeline & Resources
  - Effort estimates, team requirements

**Phase 3 Progress:** 0/11 tasks pending

---

## EXECUTION LOG

### Today's Session (2026-03-18)

**Completed:**
- ✅ P0-T001: Verified repository state (git status, git branch)
- ✅ P0-T002/3: Census 719 GUI files, manifest /tmp/gui_file_manifest.txt
- ✅ P0-T004: Documented entry point trace (6 levels)
- ✅ P0-T005: Analyzed GUI_App class (members, OnInit sequence)
- ✅ P0-T006: Analyzed MainFrame class (constructor, 15 widgets)
- ✅ P0-T007/8: Generated docs, initialized registry
- ✅ P0-T009: Staged all files for commit

**Evidence Files:**
```
generated_documentation/gui/entry_point_trace.md   ← 3.5 KB
generated_documentation/gui/gui_app_class.md       ← 5.2 KB  
generated_documentation/gui/main_window_class.md   ← 6.1 KB
agent_journal_gui.md                               ← 17.8 KB (updated)
.ralph/ralph-tasks.md                              ← Registry (this file)
/tmp/gui_file_manifest.txt                         ← 719 files
```

**Next Immediate Actions:**
1. Execute: `git add .ralph/ralph-tasks.md agent_journal_gui.md generated_documentation/gui/`
2. Execute: `git commit -m "orient(gui): complete Phase 0 orientation"`
3. Then: Begin `P1-T103` or `P1-T120` (Plater annotation)

**Branch Status:** `agent/gui-analysis` ready for work

---

## REPOSITORY CONTEXT

**Source Tree (Phase 0 discoveries):**
```bash
src/
└── slic3r/
    └── GUI/
        ├── GUI_App.hpp/cpp           ← Application core (T101-T102)
        ├── GUI_Init.hpp/cpp           ← Bootstrap (T103)
        ├── MainFrame.hpp/cpp          ← Window (T110-T111)
        ├── Plater.hpp/cpp             ← Editor (T120-T121)  ⭐ Core
        ├── GLCanvas3D.hpp/cpp         ← 3D View (T122)
        ├── MonitorPanel.hpp/cpp       ← Device (T130)
        ├── ParamsPanel.hpp/cpp        ← Settings (T131)
        ├── WebViewPanel.hpp/cpp       ← Home (T132)
        └── ... 719 total files
```

**Key Discovery - Dual-Mode Binary:**
```cpp
// src/OrcaSlicer.cpp:1349
bool start_gui = m_actions.empty() && !downward_check;
if (start_gui) {
    return Slic3r::GUI::GUI_Run(params);  // Cold path
}
// else: Stay in CLI mode for slicing
```

**Widget Hierarchy (Verified):**
```
MainFrame
│
├── m_tabpanel [Notebook]
│   ├── m_webview [Home tab]
│   ├── m_plater [Editor/Preview tabs] ⭐ CORE
│   ├── m_monitor [Device tab]
│   └── m_param_panel [Settings tab]
│
├── m_topbar [Title bar controls]
├── m_printhost_queue_dlg
├── m_settings_dialog
└── diff_dialog
```

---

## MEMORY & NOTES

### Architecture Highlights

**Singleton Pattern:**
- `GUI_App` is single wxApp instance
- Accessed via `wxGetApp()`
- Unity replacement: MonoBehaviour on persistent GameObject

**Event Bus:**
- wxWidgets events (.Bind) throughout
- Cross-panel communication
- Unity replacement: UnityEvent or C# events

**Threading:**
- Network operations on background threads
- UI updates via wxQueueEvent
- Unity replacement: Coroutines or Job System

**Rendering:**
- OpenGL context in GLCanvas3D
- Managed by OpenGLManager
- Unity replacement: Unity's RenderPipeline

### Next Steps Summary

**After Phase 0 commit:**
1. Choose entry point for Phase 1:
   - **Option A:** Continue GUI_App.cpp (T101) - completeness
   - **Option B:** Switch to Plater (T120) - critical path
   - **Option C:** Complete MainFrame (T111) - structural

2. Annotation pattern:
   - Read 100-200 lines at a time
   - Add [INTENT] for comments explaining "why"
   - Add [UNITY] for porting guidance
   - Add [PORTING_HAZARD] for risky patterns
   - Keep original code line markers

3. Progress tracking:
   - Update this file after each major file
   - Mark [~] for active file
   - Move to [x] when complete

### Risk Warnings (Note for Later)

**P1 Hazards to investigate:**
1. `Main thread UI updates` - wxWidgets allows any thread to queue events; Unity requires main thread only
2. `OpenGL context ownership` - But likely manageable via Unity GL plugin
3. `Single instance enforcement` - Need Unity equivalents

**P2 Hazards:**
1. File system watchers (lib Awesome)
2. Network discovery protocols
3. Platform-specific DLL loading

---

**Registry Last Updated:** 2026-03-18 04:52 UTC  
**Next Update Expected:** After P0-T009 git commit  
**Current Phase:** 0 → 1 transition pending

---

*"In the beginning was the CLI command, and the CLI command was with OrcaSlicer, and the CLI command was OrcaSlicer." — Orthogonalians 1:1* 🧅