#include "AuxiliaryDialog.hpp"
#include "I18N.hpp"
#include "GUI_AuxiliaryList.hpp"

#include "libslic3r/Utils.hpp"

#include <boost/property_tree/ptree.hpp>

namespace pt = boost::property_tree;
typedef pt::ptree JSON;

namespace Slic3r { namespace GUI {

AuxiliaryDialog::AuxiliaryDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Auxiliaryies"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    // [INTENT] Hosts the AuxiliaryList view inside a DPI-aware dialog so the list can focus/create/remove auxiliary objects without
    // touching other panels. [PORTING_HAZARD:P2] Unity must host this view inside a Canvas/Panel with CanvasScaler because wx's DPIDialog
    // has no direct analogue.
    m_aux_list = new AuxiliaryList(this);
    // [STATE]/[UNITY] The list view owns its own VisualElement-style sizer tree, so recreate it as a UI Toolkit ListView/ScrollView.
    // [STATE] Keep the same 80x50 em rhythm by driving RectTransform padding from the font metrics.

    SetSizerAndFit(m_aux_list->get_top_sizer());
    SetSize({80 * em_unit(), 50 * em_unit()});
    // [STATE] Dialog size is derived from em_unit so the font-based grid stays constant even when DPI changes.
    // [UNITY] Translate em_unit to texture-size-scaled RectTransform dimensions for the Unity panel.

    Layout();
    Center();
}

void AuxiliaryDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    // [EVENT]/[THREAD] DPIDialog fires this on the UI thread whenever the OS reports a DPI shift, so we re-fit/re-size immediately.
    // [PORTING_HAZARD:P3]/[UNITY] Unity lacks a direct DPI event, so replicate the behavior by listening to CanvasScaler.scaleFactor
    // changes or reacting to RenderTexture size changes on the main thread.
    Fit();
    SetSize({80 * em_unit(), 50 * em_unit()});
    // m_aux_list->msw_rescale();
    Refresh();
}

}} // namespace Slic3r::GUI
