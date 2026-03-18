# P0-T004: Entry Point Trace

## Entry Point: OrcaSlicer.cpp → GUI_App::OnInit()

### Call Chain Documentation

**Entry Point:** `src/OrcaSlicer.cpp:7567` (main) or `src/OrcaSlicer.cpp:7536` (Win32 exported entry point)

### Call Sequence (GUI Mode)

1. **main()** (src/OrcaSlicer.cpp:7567)
   - Calls: `CLI().run(argc, argv)`
   - Returns: int (exit code)

2. **CLI::run()** (src/OrcaSlicer.cpp:1232)
   - Performs setup and argument parsing
   - Line 1349: `bool start_gui = m_actions.empty() && !downward_check;`
   - Line 1350-1394: If `start_gui == true`, enters GUI path
   - Line 1394: Returns `Slic3r::GUI::GUI_Run(params)`
   - **Entry condition**: No CLI actions specified, not in downward compatibility check mode

3. **GUI_Run()** (src/slic3r/GUI/GUI_Init.cpp:28)
   - Line 44: Creates `GUI::GUI_App* gui = new GUI::GUI_App()`
   - Line 56: Sets `gui->init_params = &params`
   - Line 64/66: Calls `wxEntry(argc, argv)` to initialize wxWidgets application

4. **wxEntry()** → **GUI_App::OnInit()** (src/slic3r/GUI/GUI_App.cpp:2598)
   - Line 2601: Delegates to `on_init_inner()`

5. **GUI_App::on_init_inner()** (src/slic3r/GUI/GUI_App.cpp:2701)
   - Performs initialization in sequence:
     - Logging setup (line 2705)
     - Font initialization (line 2712)
     - Image handlers (line 2716)
     - Color/font setup (line 2792-2794)
     - TLS/SSL initialization (line 2832)
     - **Line 3114**: Creates `mainframe = new MainFrame()`
     - **Line 3125**: Sets top window `SetTopWindow(mainframe)`
     - **Line 3126**: Shows window via `mainframe->Show()`

6. **MainFrame Constructor** (src/slic3r/GUI/MainFrame.cpp:322)
   - Creates UI components:
     - Line 371: Creates topbar (`m_topbar = new BBLTopbar(this)`)
     - Line 429: Calls `init_tabpanel()` → creates Notebook with tabs
     - Line 433: Calls `init_menubar_as_editor()` or `init_menubar_as_gcodeviewer()`

### Summary

**Total Call Chain (6 levels):**

```
main() → CLI::run() → GUI_Run() → wxEntry() → GUI_App::OnInit() → GUI_App::on_init_inner() → MainFrame() → Show()
```

### Key Decision Point

The **critical branching** occurs in `CLI::run()` at line 1349:
- `start_gui = m_actions.empty() && !downward_check`
- If `start_gui == true`: Application launches GUI mode
- If `start_gui == false`: Application stays in CLI mode for slicing

### Files Involved

- `src/OrcaSlicer.cpp` - Main entry point and CLI dispatch
- `src/slic3r/GUI/GUI_Init.cpp` - GUI wrapper and wxEntry bridge
- `src/slic3r/GUI/GUI_App.cpp` - Application initialization
- `src/slic3r/GUI/MainFrame.cpp` - Main window creation