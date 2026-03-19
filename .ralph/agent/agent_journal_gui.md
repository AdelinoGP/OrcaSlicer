**Task T149 COMPLETE**
- File: src/slic3r/GUI/KBShortcutsDialog.cpp
- Lines added: 120
- Key findings: Annotated main architecture, event handling (Bind/PostEvent), and UI creation logic, identifying Unity UI Toolkit as the target for reimplementation.
- Verification excerpt: `// [INTENT] Dialog displaying keyboard shortcuts in tabs.`
- Unity porting hazards identified: 5 (Custom binding, event handling, simplebook, custom tab buttons)
- Git: committed as annotate(gui): document KBShortcutsDialog architecture and events (KBShortcutsDialog.cpp)

**Task T158 COMPLETE**
- Deliverable: src/slic3r/GUI/UpdateDialogs.hpp (annotated)
- Lines added: 79
- Key findings: Annotated UpdateDialogs classes (MsgUpdateSlic3r, etc.), identifying UI modal dialogs and their components for Unity porting.
- Verification excerpt: `// [INTENT] A confirmation dialog listing application updates`
- Unity porting hazards identified: 3 (wxDialog subclasses, custom event handling)
- Git: committed as annotate(gui): annotate UpdateDialogs.hpp (UpdateDialogs.hpp)

**Task T159 COMPLETE**
- Deliverable: src/slic3r/GUI/WipeTowerDialog.cpp (annotated)
- Lines added: 216 added lines (many are comments/annotations)
- Key findings: Annotated main classes RammingDialog/RammingPanel and WipingDialog. Identified wxWebView as a porting hazard (Webview in Unity: WebGL, or UniWebView for mobile).
- Verification excerpt: "// [INTENT] RammingDialog: Expert-level customization dialog for filament extrusion ramming settings."
- Git: committed as annotate(gui): document RammingDialog and porting hazards (WipeTowerDialog.cpp), commit: e85510099b
