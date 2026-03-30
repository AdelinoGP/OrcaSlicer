#ifndef slic3r_GUI_CalibrationWizardCaliPage_hpp_
#define slic3r_GUI_CalibrationWizardCaliPage_hpp_

#include "CalibrationWizardPage.hpp"
#include "StatusPanel.hpp"

namespace Slic3r { namespace GUI {

/*
[INTENT]
Declaration boundary for the active calibration step page.

[STATE]
- m_step_panel: Visual guide for calibration steps.
- m_printing_panel: Panel showing progress and control buttons.
- m_picture_panel: Area for calibration reference images.
- m_cur_extruder_id: Tracking for multi-extruder machines.

[UNITY]
- CalibrationCaliPage -> Sub-page/View within a larger Wizard MonoBehaviour.
- PrintingTaskPanel -> UI Toolkit composite component with ProgressBar.
*/

class CalibrationCaliPage : public CalibrationWizardPage
{
public:
    CalibrationCaliPage(wxWindow*      parent,
                        CalibMode      cali_mode,
                        CaliPageType   cali_type = CaliPageType::CALI_PAGE_CALI,
                        wxWindowID     id        = wxID_ANY,
                        const wxPoint& pos       = wxDefaultPosition,
                        const wxSize&  size      = wxDefaultSize,
                        long           style     = wxTAB_TRAVERSAL);
    ~CalibrationCaliPage();

    void create_page(wxWindow* parent);
    void on_subtask_pause_resume(wxCommandEvent& event);
    void on_subtask_abort(wxCommandEvent& event);
    void set_cali_img();
    void update(MachineObject* obj) override;
    void update_subtask(MachineObject* obj);
    void update_basic_print_data(bool def, float weight = 0.0, int prediction = 0);
    void reset_printing_values();
    void clear_last_job_status();
    void set_pa_cali_image(int stage);

    void on_device_connected(MachineObject* obj) override;

    void         set_cali_method(CalibrationMethod method) override;
    virtual bool Show(bool show = true) override;
    void         msw_rescale() override;

protected:
    float get_selected_calibration_nozzle_dia(MachineObject* obj);

    bool                  m_print_finish{false};
    bool                  m_is_between_start_and_running{false};
    wxBoxSizer*           m_top_sizer;
    CaliPageStepGuide*    m_step_panel{nullptr};
    PrintingTaskPanel*    m_printing_panel{nullptr};
    CaliPagePicture*      m_picture_panel;
    SecondaryCheckDialog* abort_dlg{nullptr};
    int                   m_cur_extruder_id{0};
};

}} // namespace Slic3r::GUI

#endif
