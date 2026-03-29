#ifndef slic3r_GUI_TextInput_hpp_
#define slic3r_GUI_TextInput_hpp_

#include <wx/textctrl.h>
#include "StaticBox.hpp"

// [INTENT] A skinned text-input wrapper that combines a standard wxTextCtrl with custom-painted chrome:
// optional icons, a prefix label, hover/focus outlines, and static tips.
// [STATE] Caches metrics for label and tip text, and stores StateColor palettes for label/text.
// [UNITY] Use a standard Unity UI Toolkit TextField with a custom structural wrapper (label, icons) and USS styles for hover/focus outlines.
// [PORTING_HAZARD:P3] The implementation manually overrides DoSetSize to position the internal wxTextCtrl within its painted borders. Unity's layout engine should handle this structurally.

class TextInput : public wxNavigationEnabled<StaticBox>
{

    wxSize labelSize;
    ScalableBitmap icon;
    ScalableBitmap icon_1;
    StateColor     label_color;
    StateColor     text_color;
    wxTextCtrl * text_ctrl;

    wxString  static_tips;
    wxSize    static_tips_size;
    wxBitmap  static_tips_icon;

    static const int TextInputWidth = 200;
    static const int TextInputHeight = 50;

public:
    TextInput();

    TextInput(wxWindow *     parent,
              wxString       text,
              wxString       label = "",
              wxString       icon  = "",
              const wxPoint &pos   = wxDefaultPosition,
              const wxSize & size  = wxDefaultSize,
              long           style = 0);

public:
    void Create(wxWindow *     parent,
              wxString       text,
              wxString       label = "",
              wxString       icon  = "",
              const wxPoint &pos   = wxDefaultPosition,
              const wxSize & size  = wxDefaultSize,
              long           style = 0);

    void SetCornerRadius(double radius);

    void SetLabel(const wxString& label);

    void SetStaticTips(const wxString& tips, const wxBitmap& bitmap);

    void SetIcon(const wxBitmap & icon);
    void SetIcon(const wxString & icon);

    void SetIcon_1(const wxString &icon);

    void SetLabelColor(StateColor const &color);

    void SetTextColor(StateColor const &color);

    virtual void Rescale();

    virtual bool Enable(bool enable = true) override;

    virtual void SetMinSize(const wxSize& size) override;

    wxTextCtrl *GetTextCtrl() { return text_ctrl; }

    wxTextCtrl const *GetTextCtrl() const { return text_ctrl; }

protected:
    virtual void OnEdit() {}

    virtual void DoSetSize(
        int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);

    void DoSetToolTipText(wxString const &tip) override;

private:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void messureSize();

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_TextInput_hpp_
