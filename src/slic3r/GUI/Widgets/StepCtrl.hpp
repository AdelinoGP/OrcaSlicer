#ifndef slic3r_GUI_StepCtrlBase_hpp_
#define slic3r_GUI_StepCtrlBase_hpp_

#include "StaticBox.hpp"

// [INTENT] StepCtrlBase owns the shared step-model, labels, and selection bookkeeping for the stepper-style widgets below.
// [STATE] The active step index, drag offsets, cached thumb position, and per-step hint/tip strings are the retained model.
// [EVENT] EVT_STEP_CHANGING / EVT_STEP_CHANGED provide the vetoable selection contract used by the mouse-driven controller.
// [UNITY] Port as a retained stepper controller with a data model plus separate visual rows/thumb indicator and explicit selection
// callbacks. [PORTING_HAZARD:P2] wxCommandEvent veto flow and mouse-capture drag semantics do not map 1:1 to a stock Unity control.
wxDECLARE_EVENT(EVT_STEP_CHANGING, wxCommandEvent);
wxDECLARE_EVENT(EVT_STEP_CHANGED, wxCommandEvent);

class StepCtrlBase : public StaticBox
{
protected:
    wxFont font_tip;
    // [STATE] Per-state palette entries are stored as StateColor objects so paint code can react to hover/press/disabled state.
    StateColor clr_bar;
    StateColor clr_step;
    StateColor clr_text;
    StateColor clr_tip;
    int        radius    = 7;
    int        bar_width = 4;

    std::vector<wxString> steps;
    std::vector<wxString> tips;
    wxString              hint;

    int step = -1;

    // [STATE] Drag gesture bookkeeping keeps the thumb anchored during press-and-drag selection updates.
    wxPoint drag_offset;
    wxPoint pos_thumb;

public:
    StepCtrlBase(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0);

    ~StepCtrlBase();

public:
    // [INTENT] Update the descriptive hint rendered alongside the step list.
    void SetHint(wxString hint);

    // [INTENT] Change the font used for tip text; the widget rescales its layout when this succeeds.
    bool SetTipFont(wxFont const& font);

public:
    // [INTENT] Append a step label and optional tip text to the retained model.
    int AppendItem(const wxString& item, wxString const& tip = {});

    // [INTENT] Clear the model and reset selection state.
    void DeleteAllItems();

    // [INTENT] Expose the current model size and selection to callers.
    unsigned int GetCount() const;

    int GetSelection() const;

    // [EVENT] SelectItem is the public selection entry point used by user input and programmatic navigation.
    void SelectItem(int item);
    // [EVENT] Idle advances deferred hover/drag state in the custom controller loop.
    void Idle();

    wxString GetItemText(unsigned int item) const;
    int      GetItemUseText(wxString txt) const;
    void     SetItemText(unsigned int item, wxString const& value);

private:
    // some useful events
    // [EVENT] Emits the vetoable changing/changed command event pair and keeps selection transitions centralized.
    bool sendStepCtrlEvent(bool changing = false);
};

// [INTENT] StepCtrl is the interactive stepper view: it renders a thumb, tracks mouse capture, and drives selection changes.
// [UNITY] Use a retained list/slider hybrid with custom hit-testing and a drag controller rather than a stock Stepper.
class StepCtrl : public StepCtrlBase
{
    ScalableBitmap bmp_thumb;

public:
    StepCtrl(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0);

    virtual void Rescale();

private:
    void mouseDown(wxMouseEvent& event);
    void mouseMove(wxMouseEvent& event);
    void mouseUp(wxMouseEvent& event);
    void mouseCaptureLost(wxMouseCaptureLostEvent& event);

    // [INTENT] Paint the interactive step chrome through wxDC; Unity should reimplement it as a custom UI render pass.
    void doRender(wxDC& dc) override;

    DECLARE_EVENT_TABLE()
};

// [INTENT] StepIndicator is the read-only progression view: it advances through steps and shows completion state with an OK icon.
// [UNITY] Port as a retained progress/step list with a completed-state badge per row and no direct drag interaction.
class StepIndicator : public StepCtrlBase
{
    ScalableBitmap bmp_ok;

public:
    StepIndicator(
        wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0);

    virtual void Rescale();

    void SelectNext();

private:
    void doRender(wxDC& dc) override;
};

// [INTENT] FilamentStepIndicator is the step indicator variant specialized for filament workflows and slot labeling.
// [STATE] It adds the slot-information label on top of the shared step model to drive workflow-specific rendering.
// [UNITY] Model this as the same retained step list plus an injected subtitle/status string for the active filament slot.
class FilamentStepIndicator : public StepCtrlBase

{
    ScalableBitmap bmp_ok;
    // wxBitmap bmp_extruder;
    // [STATE] Slot text is cached separately because the renderer composes it into the indicator chrome.
    wxString m_slot_information = "";

public:
    FilamentStepIndicator(
        wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0);

    virtual void Rescale();

    void SelectNext();
    void SetSlotInformation(wxString slot);

private:
    void doRender(wxDC& dc) override;
};

#endif // !slic3r_GUI_StepCtrlBase_hpp_
