#ifndef _STEP_MESH_DIALOG_H_
#define _STEP_MESH_DIALOG_H_

#include <thread>
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "libslic3r/Format/STEP.hpp"
#include "Widgets/Button.hpp"
class Button;

// [INTENT] Modal STEP tessellation settings dialog: it lets the user tune linear/angle deflection,
// toggle compound splitting, and preview the estimated triangle count before import proceeds.
// [STATE] The dialog keeps both raw text entry and last-valid numeric values so typed-but-unvalidated
// input survives until the worker-side preview and validation logic accept it.
// [THREAD] `m_task` is the background preview worker; the dialog must keep UI updates and worker
// teardown on the main thread to avoid races when the dialog closes or changes settings.
// [UNITY] Map this to a modal controller with text fields, validation state, and an async mesh
// estimate service; keep the preview result in a retained view-model instead of polling a thread.
// [PORTING_HAZARD:P2] The current flow assumes the dialog owns both validation and worker lifetime,
// so Unity should separate input validation from background estimation and explicit cancel/close.
class StepMeshDialog : public Slic3r::GUI::DPIDialog
{
public:
    StepMeshDialog(wxWindow* parent, Slic3r::Step& file, double linear_init, double angle_init);
    ~StepMeshDialog() override;
    void on_dpi_changed(const wxRect& suggested_rect) override;
    // [STATE] These accessors return the last parseable value, falling back to the cached default
    // so the dialog can keep a user's in-progress edits even when the raw text is temporarily invalid.
    inline double get_linear_defletion()
    {
        double value;
        if (m_linear_last.ToDouble(&value)) {
            return value;
        } else {
            return m_last_linear;
        }
    }
    inline double get_angle_defletion()
    {
        double value;
        if (m_angle_last.ToDouble(&value)) {
            return value;
        } else {
            return m_last_angle;
        }
    }
    inline bool get_split_compound_value() { return m_split_compound_checkbox->GetValue(); }

private:
    // [STATE] `m_file` is the live STEP source being configured; the dialog mutates import settings
    // against this reference rather than copying file contents into the UI layer.
    Slic3r::Step& m_file;
    wxCheckBox*   m_checkbox                = nullptr;
    wxCheckBox*   m_split_compound_checkbox = nullptr;
    wxString      m_linear_last;
    wxString      m_angle_last;
    // [STATE] `mesh_face_number_text` mirrors the async triangle estimate that informs the import cost.
    wxStaticText* mesh_face_number_text;
    double        m_last_linear = 0.003;
    double        m_last_angle  = 0.5;
    unsigned int  m_mesh_number = 0;
    // [THREAD] Worker thread ownership is manual here; `stop_task()` must join or cancel before close.
    boost::thread* m_task{nullptr};
    bool           validate_number_range(const wxString& value, double min, double max);
    void           update_mesh_number_text();
    void           on_task_done(wxCommandEvent& event);
    void           stop_task();
};

#endif // _STEP_MESH_DIALOG_H_
