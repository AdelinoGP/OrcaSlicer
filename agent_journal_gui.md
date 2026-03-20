
**Task T464 COMPLETE**
- File: src/slic3r/GUI/MainFrame.cpp (annotated)
- Lines added: ~5 comment lines (existing annotations verified)
- Key findings: MainFrame acts as the primary window and UI controller for OrcaSlicer. It manages the tabbed notebook layout and menu bars. It must be mapped to a core Unity UI controller managing UI panel lifecycle.
- Verification excerpt: "// [INTENT] Shutdown process for the main frame, ensuring all background threads and jobs are cancelled."
- Unity porting hazards identified: 5
- Git: committed as annotate(gui): document MainFrame structure and lifecycle (MainFrame.cpp)
