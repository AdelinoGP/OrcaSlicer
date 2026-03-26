# ParamsPanel.hpp Annotation Plan

## 1. Analysis
`ParamsPanel` inherits `wxPanel` and manages a set of tabs (`Print`, `Filament`, `Printer`) and associated UI elements for parameter settings in the Slic3r GUI. `TipsDialog` is a utility dialog for displaying tips to the user.

## 2. Annotation Strategy
- [INTENT] Class-level annotation for `ParamsPanel` and `TipsDialog`.
- [STATE] Annotation of `ParamsPanel` members related to layout, active tabs, and mode.
- [EVENT] Event handling in `ParamsPanel` (`OnToggled`) and `TipsDialog` (`on_dpi_changed`).
- [UNITY] Mapping wxWidgets UI elements (`wxBoxSizer`, `wxPanel`, `ScalableButton`, `SwitchButton`) to Unity Toolkit `VisualElement` equivalents.
- [PORTING_HAZARD] Note the complexity of the current layout management and the reliance on `wxWidgets` specific sizers.

## 3. Plan
1. Start task T520.
2. Annotate `TipsDialog` and `ParamsPanel`.
3. Verify annotation.
4. Record completion.
5. Close task.
6. Commit.
