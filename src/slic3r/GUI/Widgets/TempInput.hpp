#ifndef slic3r_GUI_TempInput_hpp_
#define slic3r_GUI_TempInput_hpp_

#include "../wxExtensions.hpp"
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include "StaticBox.hpp"

#include <unordered_set>

wxDECLARE_EVENT(wxCUSTOMEVT_SET_TEMP_FINISH, wxCommandEvent);

enum TempInputType { TEMP_OF_MAIN_NOZZLE_TYPE, TEMP_OF_DEPUTY_NOZZLE_TYPE, TEMP_OF_NORMAL_TYPE };

// [INTENT] TempInput is a skinned temperature editor row: it combines icon chrome, a current-value label, a numeric entry,
// and a lazily-created warning popup so range checks and visual feedback stay self-contained.
// [STATE] Read-only mode, edit-progress gating, temperature limits, allow-listed values, and the current temperature type all
// live on the control because paint, validation, and commit behavior are driven from the same retained state.
// [EVENT] The row emits a custom completion command when input is committed; focus, key, wheel, hover, and paint hooks are
// handled by the implementation to manage validation and popup lifetimes.
// [UNITY] Port as a retained row prefab with icon slots, a text input, and an anchored validation tooltip/overlay controller.
// [PORTING_HAZARD:P2] wx focus navigation and transient popup dismissal are part of the commit contract, so Unity needs an
// explicit validation state machine instead of relying on stock input focus behavior.
class TempInput : public wxNavigationEnabled<StaticBox>
{
    // [STATE] Per-row visual state stays inside the widget so hover and edit highlighting can be derived during custom paint.
    bool hover;

    bool           m_read_only{false};
    bool           m_on_changing{false};
    wxSize         labelSize;
    ScalableBitmap normal_icon;
    ScalableBitmap actice_icon;
    ScalableBitmap degree_icon;
    ScalableBitmap round_scale_hint_icon; /*the size hint of icon, use to compute size*/

    StateColor label_color;
    StateColor text_color;

    // [STATE] Child controls are owned here; the popup is created lazily and reused until validation clears it.
    wxTextCtrl*   text_ctrl;
    wxStaticText* warning_text;

    int                     max_temp = 0;
    int                     min_temp = 0;
    std::unordered_set<int> additional_temps;
    bool                    warning_mode = false;
    TempInputType           m_input_type;

    // [STATE] Manual layout uses these cached dimensions and paddings to keep the row aligned across rescale/relayout.
    int              padding_left    = 0;
    static const int TempInputWidth  = 200;
    static const int TempInputHeight = 50;

public:
    // [INTENT] WarningType selects the validation message path for too-high, too-low, or unknown temperature violations.
    enum WarningType {
        WARNING_TOO_HIGH,
        WARNING_TOO_LOW,
        WARNING_UNKNOWN,
    };

    TempInput();

    TempInput(wxWindow*      parent,
              int            type,
              wxString       text,
              TempInputType  input_type,
              wxString       label       = "",
              wxString       normal_icon = "",
              wxString       actice_icon = "",
              const wxPoint& pos         = wxDefaultPosition,
              const wxSize&  size        = wxDefaultSize,
              long           style       = 0);

public:
    // [INTENT] Create assembles the child text field, loads the icons, and wires the validation/event surface.
    void Create(wxWindow*      parent,
                wxString       text,
                wxString       label       = "",
                wxString       normal_icon = "",
                wxString       actice_icon = "",
                const wxPoint& pos         = wxDefaultPosition,
                const wxSize&  size        = wxDefaultSize,
                long           style       = 0);

    wxPopupTransientWindow* wdialog{nullptr};
    int                     temp_type;
    bool                    actice = false;
    wxString                currentTemp;

    wxString erasePending(wxString& str);

    void SetTagTemp(int temp);
    void SetTagTemp(wxString temp);

    void          SetCurrTemp(int temp);
    void          SetCurrTemp(wxString temp);
    void          SetCurrType(TempInputType type);
    TempInputType GetCurrType() { return m_input_type; };

    bool AllisNum(std::string str);
    void SetFinish();
    void Warning(bool warn, WarningType type = WARNING_UNKNOWN);
    void SetIconActive();
    void SetIconNormal();

    void SetReadOnly(bool ro) { m_read_only = ro; }

    void SetMaxTemp(int temp);
    void SetMinTemp(int temp);
    void AddTemp(int temp) { additional_temps.insert(temp); };

    int GetType() { return temp_type; }

    wxString GetTagTemp() { return text_ctrl->GetValue(); }
    wxString GetCurrTemp() { return GetLabel(); }
    int      get_max_temp() { return max_temp; }
    void     SetLabel(const wxString& label);

    void SetTextColor(StateColor const& color);

    void SetLabelColor(StateColor const& color);

    virtual void Rescale();

    virtual bool Enable(bool enable = true) override;

    virtual void SetMinSize(const wxSize& size) override;

    wxTextCtrl* GetTextCtrl() { return text_ctrl; }

    wxTextCtrl const* GetTextCtrl() const { return text_ctrl; }

    // [STATE] SetOnChanging guards against recursive commit/validation flows while the control is navigating away from edit mode.
    bool IsOnChanging() const { return m_on_changing; }
    void SetOnChanging() { m_on_changing = true; }
    void ReSetOnChanging() { m_on_changing = false; }

protected:
    virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);

    void DoSetToolTipText(wxString const& tip) override;

private:
    void ResetWaringDlg();
    bool CheckIsValidVal(bool show_warning);

    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void messureMiniSize();
    void messureSize();

    // some useful events
    void mouseMoved(wxMouseEvent& event);
    void mouseWheelMoved(wxMouseEvent& event);
    void mouseEnterWindow(wxMouseEvent& event);
    void mouseLeaveWindow(wxMouseEvent& event);
    void keyPressed(wxKeyEvent& event);
    void keyReleased(wxKeyEvent& event);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_TempInput_hpp_
