# P0-T006: Main Window Class Identification

## MainFrame Class Analysis

### File Location
- **Header:** `src/slic3r/GUI/MainFrame.hpp`
- **Implementation:** `src/slic3r/GUI/MainFrame.cpp`

### Class Overview
`MainFrame` is the primary application window class, inheriting from `DPIFrame` (line 92 in MainFrame.hpp). It manages the main UI layout, menu system, tabs, and orchestrates the plater, monitor, and parameter panels.

### Constructor Signature

```cpp
MainFrame::MainFrame()
    : DPIFrame(NULL, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, BORDERLESS_FRAME_STYLE, "mainframe")
    , m_printhost_queue_dlg(new PrintHostQueueDialog(this))
    , m_recent_projects(18)
    , m_settings_dialog(this)
    , diff_dialog(this)
```

**Location:** `src/slic3r/GUI/MainFrame.cpp:322`

### Constructor Parameters

The MainFrame constructor takes **no explicit parameters** but inherits from DPIFrame with these arguments:
- `parent` = `NULL` (top-level window)
- `id` = `wxID_ANY` (automatic ID)
- `title` = `""` (empty, set later)
- `pos` = `wxDefaultPosition`
- `size` = `wxDefaultSize`
- `style` = `BORDERLESS_FRAME_STYLE`
- `name` = `"mainframe"`

### Direct Child Widgets (Container Hierarchy)

From `init_tabpanel()` and constructor analysis:

#### 1. **m_tabpanel** (Notebook) - *Primary Container*
- **Type:** `Notebook*` (wxNotebook derivative)
- **Location:** `init_tabpanel()` line 1218
- **Purpose:** Tabbed interface container for all main views
- **Config:** `wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME`

#### 2. **m_topbar** (BBLTopbar)
- **Type:** `BBLTopbar*` (line 371, non-Apple) or `wxPanel*` (Apple)
- **Purpose:** Window title bar with controls (undo/redo, close, minimize)
- **Platform-dependent:** Different implementation on macOS

#### 3. **m_printhost_queue_dlg** (PrintHostQueueDialog)
- **Type:** `PrintHostQueueDialog*`
- **Initialized in:** Constructor member initializer (line 324)
- **Purpose:** Dialog for managing print queue (sending to network printers)

#### 4. **m_settings_dialog** (PreferencesDialog)
- **Type:** `PreferencesDialog`
- **Initialized in:** Constructor member initializer (line 327)
- **Purpose:** Settings/preferences dialog

#### 5. **diff_dialog** (PublishDialog)
- **Type:** `PublishDialog`
- **Initialized in:** Constructor member initializer (line 328)
- **Purpose:** Model comparison/publishing dialog

#### 6. **m_recent_projects** (File History)
- **Type:** `FileHistory` (nested class)
- **Initialized in:** Constructor member initializer (line 326)
- **Purpose:** Tracks recently opened projects for quick access

### Children Created in init_tabpanel()

From `init_tabpanel()` (lines 1287-1312):

#### 7. **m_webview** (WebViewPanel) - Tab Page 0: Home
- **Type:** `WebViewPanel*`
- **Location:** Line 1288
- **Added to notebook:** Tab position `tpHome` (0)
- **Purpose:** Home tab with web content/dashboard

#### 8. **m_param_panel** (ParamsPanel) - Not directly a notebook page
- **Type:** `ParamsPanel*`
- **Location:** Line 1295
- **Purpose:** Parameter editing sidebar (associated with plater)

#### 9. **m_plater** (Plater) - Tab Page 1: 3D Editor & Tab Page 2: Preview
- **Type:** `Plater*`
- **Location:** Line 1298
- **Note:** Added as notebook page(s) later during plater initialization
- **Purpose:** Core 3D view editor and preview interface
- **Parent link:** `wxGetApp().plater_ = m_plater` (line 3127 in GUI_App.cpp)

#### 10. **m_monitor** (MonitorPanel) - Tab Page 3: Device Monitor
- **Type:** `MonitorPanel*`
- **Location:** Line 1307
- **Added to notebook:** Line 1309 with "Device" tab label
- **Purpose:** Printer monitoring and control interface

#### 11. **m_printer_view** (PrinterWebView)
- **Type:** `PrinterWebView*`
- **Location:** Line 1311
- **Purpose:** Web-based printer interface (camera view, etc.)

#### 12. **m_calib_menu** (wxMenu)
- **Type:** `wxMenu*`
- **Location:** MainFrame.hpp line 106
- **Initialized in:** Menubar creation
- **Purpose:** Calibration menu

### Additional UI Components

#### 13. **m_menubar** (wxMenuBar)
- **Type:** `wxMenuBar*`
- **Initialized in:** `init_menubar_as_editor()` or `init_menubar_as_gcodeviewer()`
- **Location:** Line 104 in header, created after tabpanel

#### 14. **m_main_sizer** (wxSizer)
- **Type:** `wxSizer*`
- **Location:** Line 113 in header
- **Purpose:** Main layout sizer (not explicitly shown in snippet but used)

#### 15. **m_reset_title_text_colour_timer** (wxTimer)
- **Type:** `wxTimer*`
- **Location:** Line 98 in header
- **Purpose:** macOS-specific title bar color reset timer

### Widget Hierarchy Summary

```
MainFrame (DPIFrame)
├── m_tabpanel (Notebook) [PRIMARY TAB CONTAINER]
│   ├── m_webview [tpHome (0)]
│   ├── m_plater [tp3DEditor (1), tpPreview (2)]
│   ├── m_monitor [tpMonitor (3)]
│   └── [Other tabs created dynamically]
├── m_topbar (BBLTopbar/Panel) [TITLE BAR]
├── m_printhost_queue_dlg (PrintHostQueueDialog) [DIALOG]
├── m_settings_dialog (PreferencesDialog) [DIALOG]
├── diff_dialog (PublishDialog) [DIALOG]
├── m_menubar (wxMenuBar) [MENU SYSTEM]
└── [Supporting managers and timers]
```

### Key Relationships

- **Plater:** The most complex child, created in MainFrame but managed by GUI_App (via `plater_` global)
- **Parameter Panel:** Created in MainFrame but operates alongside Plater in the editor view
- **Notebook:** Central container that switches between Home, Editor, Preview, Monitor, and other views

### Usage Pattern

```
User Action Flow:
Main Button → MainFrame → Open File → Plater → Load Model
          ↓            ↓
      MenuBar    TabPanel