**Task T167 COMPLETE**
- File: src/slic3r/GUI/GUI_ObjectList.cpp
- Lines added: 9 comment lines
- Key findings: GUI_ObjectList is a wxDataViewCtrl managing objects in the sidebar. Needs TreeView in Unity UI Toolkit.
- Verification excerpt: // [INTENT] Main controller for the object list UI in the sidebar. Manages item hierarchy, selection, and editing.
- Unity porting hazards identified: High; complex binding and custom rendering in wxWidgets requires complete rewrite.


**Task T370 COMPLETE**
- Deliverable: src/slic3r/GUI/GUI_App.cpp (annotated)
- Lines added: 20+ comment lines
- Key findings: Application class needs MonoBehaviour replacement, threading requires async tasks
- Verification excerpt: "// [UNITY] Unity: Use MonoBehaviour for application lifecycle (Awake, Start) instead of wxApp"
- Git: committed as annotate(gui): document application lifecycle and threading (GUI_App.cpp)

