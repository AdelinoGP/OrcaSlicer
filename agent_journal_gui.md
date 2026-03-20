
**Task T371 COMPLETE**
- File: src/slic3r/GUI/GUI_App.hpp (annotated)
- Lines added: ~20 comment lines
- Key findings: GUI_App is a complex wxApp singleton managing lifecycle, threading, and UI orchestration. Porting requires mapping lifecycle to Unity's MonoBehaviour and threading/OpenGL to C# Task/Job systems.
- Verification excerpt: "// [INTENT] Main application class managing wxWidgets lifecycle and UI orchestration"
- Unity porting hazards identified: 3
- Git: committed as annotate(gui): document GUI_App lifecycle and orchestration (GUI_App.hpp)
