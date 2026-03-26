# Scratchpad: GUI File Annotation

- [ ] PrintHostDialogs.cpp (T545)
- [ ] PrintHostDialogs.hpp (T546)

## Understanding
- `PrintHostSendDialog`: wxWidgets Dialog for G-code upload settings (filename, group, storage). Needs `[UNITY]` equivalent (Modal UI Toolkit dialog).
- `PrintHostQueueDialog`: wxWidgets Dialog for upload queue monitoring. Needs `[UNITY]` equivalent (Window with ListView/TableView, monitoring).
- `ElegooPrintHostSendDialog`: Specialized dialog for Elegoo printer host with extra options (time-lapse, leveling, plate type). Needs `[UNITY]` equivalent (Specialized variant of PrintHostSendDialog).

## Plan
1. Add annotations to `PrintHostDialogs.cpp`.
2. Add annotations to `PrintHostDialogs.hpp`.
3. Track progress in handoff.md.
