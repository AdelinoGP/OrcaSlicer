# Scratchpad

- Selected T573 (`src/slic3r/GUI/Search.cpp`) as the next ready Phase 1 task.
- The file combines two popup search controllers: preset options and object search.
- Main Unity migration note: model the current wxPopupWindow + custom-painted rows as a floating controller with a persistent filtered list view and explicit dismiss/focus handling.
- Completed the Search.cpp pass; next up is the header so the popup/controller boundary and state ownership are visible to downstream agents.
