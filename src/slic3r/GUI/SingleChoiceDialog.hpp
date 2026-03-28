#ifndef slic3r_GUI_SingleChoice_hpp_
#define slic3r_GUI_SingleChoice_hpp_

#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"

// [INTENT] Small modal selector dialog: the caller supplies a caption/message and a fixed choice list,
// then reads back the chosen index after dismissal.
// [STATE] The selected item lives only in the embedded ComboBox; this header exposes that widget pointer
// because the cpp keeps the dialog's transient selection state inside the control rather than a view model.
// [UNITY] Map to a modal controller with a dropdown-backed selection field and explicit OK/Cancel actions;
// keep the selection model owned by the controller, not by arbitrary callers.
// [PORTING_HAZARD:P2] The public raw ComboBox accessor and the constructor's choice-list assumption make
// validation and ownership boundaries implicit; a Unity port should normalize empty lists before opening.
namespace Slic3r { namespace GUI {

class SingleChoiceDialog : public DPIDialog
{
public:
    SingleChoiceDialog(
        const wxString& message, const wxString& caption, const wxArrayString& choices, int initialSelectionwx, wxWindow* parent = nullptr);
    ~SingleChoiceDialog();

    int       GetSingleChoiceIndex();
    ComboBox* GetTypeComboBox() { return type_comboBox; };

    // [EVENT] DPI changes are forwarded from the dialog base so the popup can rescale its fixed layout.
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    // [STATE] Widget-owned combo box; lifetime is bound to the dialog instance.
    ComboBox* type_comboBox = nullptr;
};

}} // namespace Slic3r::GUI

#endif
