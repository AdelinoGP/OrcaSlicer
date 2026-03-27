# Scratchpad

- Iteration focus: T721 annotate `src/slic3r/GUI/wxMediaCtrl2.cpp`.
- Chosen because the file is a self-contained media wrapper with clear Unity port implications: platform backends, error signaling, and asynchronous event flow.
- Next step: insert durable `[INTENT]`, `[STATE]`, `[EVENT]`, `[THREAD]`, `[UNITY]`, and `[PORTING_HAZARD]` annotations in the cpp, then record handoff evidence and close the task.
- Completed T721 with cross-platform media wrapper annotations; the Windows registry/codec dependency is the main porting hazard.
