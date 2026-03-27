#ifndef slic3r_calib_dlg_hpp_
#define slic3r_calib_dlg_hpp_

#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/LabeledStaticBox.hpp"
#include "Widgets/RadioGroup.hpp"
#include "GUI_App.hpp"
#include "wx/hyperlink.h"
#include <wx/radiobox.h>
#include "libslic3r/calib.hpp"

// [INTENT] This header defines a family of modal calibration dialogs that collect numeric ranges and dispatch them to Plater-driven
// calibration flows. [STATE] Each dialog owns wxWidgets controls plus a transient Calib_Params snapshot; Plater* is non-owning and must
// stay valid for the modal lifetime. [EVENT] Start buttons and option selectors in the cpp funnel through on_start/on_*_changed handlers to
// recompute defaults and launch the chosen calibration job. [UNITY] Recreate these as controller-backed modal UI Toolkit pages with
// validated inputs, explicit confirm actions, and a shared calibration view-model. [PORTING_HAZARD:P2] The dialogs are small but tightly
// coupled to printer-side side effects, so a Unity port should separate shared calibration templates from per-test dispatch logic.
namespace Slic3r { namespace GUI {

// [INTENT] Pressure-advance calibration dialog for tuning extrusion compensation across extruder types and methods.
// [STATE] Stores the selected extruder/method radios, start/end/step inputs, optional print-count flag, and model-specific speed/accel
// fields; m_bDDE appears legacy or platform-specific and needs verification. [EVENT] on_show and the radio-box change handlers reset
// defaults before on_start forwards the validated parameters to Plater. [UNITY] Use a modal wizard page with radio groups, numeric fields,
// and a single start action bound to a calibration controller. [PORTING_HAZARD:P2] Reset-on-change behavior means the UI is not passive;
// Unity needs explicit model recomputation whenever the selection changes.
class PA_Calibration_Dlg : public DPIDialog
{
public:
    PA_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~PA_Calibration_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_show(wxShowEvent& event);

protected:
    void         reset_params();
    virtual void on_start(wxCommandEvent& event);
    virtual void on_extruder_type_changed(wxCommandEvent& event);
    virtual void on_method_changed(wxCommandEvent& event);

protected:
    bool         m_bDDE;
    Calib_Params m_params;

    RadioGroup* m_rbExtruderType;
    RadioGroup* m_rbMethod;
    TextInput*  m_tiStartPA;
    TextInput*  m_tiEndPA;
    TextInput*  m_tiPAStep;
    CheckBox*   m_cbPrintNum;
    TextInput*  m_tiBMAccels;
    TextInput*  m_tiBMSpeeds;

    Plater* m_plater; // non-owning callback target; the dialog assumes the parent Plater outlives the modal session
};

// [INTENT] Filament temperature calibration dialog for probing start/end temperatures and step size by filament type.
// [STATE] Holds the selected filament type, temperature range inputs, and a transient calibration snapshot for the current run.
// [EVENT] The filament-type selector refreshes the suggested defaults; on_start dispatches the built calibration request back to Plater.
// [UNITY] Map to a modal form with a dropdown/radio selector and validated numeric inputs bound to a shared calibration controller.
// [PORTING_HAZARD:P2] The dialog assumes the current selection can rewrite defaults, so Unity needs a clear data-binding loop rather than
// one-shot form submission.
class Temp_Calibration_Dlg : public DPIDialog
{
public:
    Temp_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~Temp_Calibration_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent& event);
    virtual void on_filament_type_changed(wxCommandEvent& event);
    Calib_Params m_params;

    RadioGroup* m_rbFilamentType;
    TextInput*  m_tiStart;
    TextInput*  m_tiEnd;
    TextInput*  m_tiStep;
    Plater*     m_plater; // non-owning; used only to launch the calibration action from the dialog
};

// [INTENT] Maximum volumetric speed test dialog for exploring safe extrusion throughput across a numeric range.
// [STATE] Keeps start/end/step inputs plus a temporary Calib_Params instance for the selected test envelope.
// [EVENT] on_start packages the chosen range and hands it to Plater.
// [UNITY] Model as a lightweight modal panel with constrained numeric fields and a single confirm path.
// [PORTING_HAZARD:P3] Simple UI, but still coupled to printer-calibration side effects and validation rules that need parity in Unity.
class MaxVolumetricSpeed_Test_Dlg : public DPIDialog
{
public:
    MaxVolumetricSpeed_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~MaxVolumetricSpeed_Test_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    TextInput* m_tiStart;
    TextInput* m_tiEnd;
    TextInput* m_tiStep;
    Plater*    m_plater; // caller-owned Plater used as the execution sink for the test
};

// [INTENT] Vertical fine artifact (VFA) test dialog for tuning motion/artifact behavior across a numeric range.
// [STATE] Maintains the calibration range inputs and a temporary parameter snapshot.
// [EVENT] The OK/start path submits the selected values to Plater.
// [UNITY] Use a compact modal wizard or inspector-style panel with validated numeric fields.
// [PORTING_HAZARD:P3] Mostly straightforward, but the dialog's meaning is domain-specific and should not be flattened into a generic range form.
class VFA_Test_Dlg : public DPIDialog
{
public:
    VFA_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~VFA_Test_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    TextInput* m_tiStart;
    TextInput* m_tiEnd;
    TextInput* m_tiStep;
    Plater*    m_plater; // non-owning execution target
};

// [INTENT] Retraction calibration dialog for measuring start/end/step values used to tune filament retraction behavior.
// [STATE] Tracks the numeric sweep and transient calibration parameters; the owning Plater handles the actual print job launch.
// [EVENT] on_start turns the entered sweep into a calibration run.
// [UNITY] Recreate as a modal calibration page with inline validation and a single dispatch action.
// [PORTING_HAZARD:P3] Behavior is simple, but it still depends on the exact Plater contract for calibration job creation.
class Retraction_Test_Dlg : public DPIDialog
{
public:
    Retraction_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~Retraction_Test_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    TextInput* m_tiStart;
    TextInput* m_tiEnd;
    TextInput* m_tiStep;
    Plater*    m_plater; // non-owning callback target
};

// [INTENT] Input shaping frequency sweep dialog for probing X/Y frequency ranges and damping assumptions.
// [STATE] Stores model/type radio selections, X/Y frequency bounds, and a damping factor used to seed the calibration run.
// [EVENT] Selector changes likely reshape the available defaults; on_start submits the chosen sweep to Plater.
// [UNITY] Use a modal panel with grouped radio controls and paired range fields, backed by a calibration view-model.
// [PORTING_HAZARD:P2] This dialog mixes multiple coupled parameters, so Unity binding needs to keep the X/Y and damping inputs synchronized
// with model choice.
class Input_Shaping_Freq_Test_Dlg : public DPIDialog
{
public:
    Input_Shaping_Freq_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~Input_Shaping_Freq_Test_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    RadioGroup* m_rbModel;
    RadioGroup* m_rbType;
    TextInput*  m_tiFreqStartX;
    TextInput*  m_tiFreqEndX;
    TextInput*  m_tiFreqStartY;
    TextInput*  m_tiFreqEndY;
    TextInput*  m_tiDampingFactor;
    Plater*     m_plater; // non-owning execution target
};

// [INTENT] Input shaping damping sweep dialog for tuning damping factor ranges around a chosen motion model.
// [STATE] Tracks the model/type selectors, fixed X/Y frequencies, and a start/end damping range used to drive the test.
// [EVENT] on_start dispatches the selected damping sweep to Plater.
// [UNITY] Represent as a modal calibration form with radio groups plus numeric range inputs.
// [PORTING_HAZARD:P2] Similar to the frequency dialog, the parameter coupling is domain-specific and should be preserved in the Unity controller.
class Input_Shaping_Damp_Test_Dlg : public DPIDialog
{
public:
    Input_Shaping_Damp_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~Input_Shaping_Damp_Test_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    RadioGroup* m_rbModel;
    RadioGroup* m_rbType;
    TextInput*  m_tiFreqX;
    TextInput*  m_tiFreqY;
    TextInput*  m_tiDampingFactorStart;
    TextInput*  m_tiDampingFactorEnd;
    Plater*     m_plater; // non-owning execution target
};

// [INTENT] Cornering calibration dialog for tuning junction deviation / cornering behavior through model-specific ranges.
// [STATE] Keeps the selected model and junction-deviation sweep endpoints alongside transient calibration parameters.
// [EVENT] on_start submits the chosen cornering test to Plater.
// [UNITY] Use a modal settings sheet with a model selector and validated numeric inputs.
// [PORTING_HAZARD:P2] The UX is simple, but the naming and parameter semantics are printer-firmware specific and need explicit mapping in Unity.
class Cornering_Test_Dlg : public DPIDialog
{
public:
    Cornering_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~Cornering_Test_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    RadioGroup* m_rbModel;
    TextInput*  m_tiJDStart;
    TextInput*  m_tiJDEnd;
    Plater*     m_plater; // non-owning execution target
};
}} // namespace Slic3r::GUI
#endif
