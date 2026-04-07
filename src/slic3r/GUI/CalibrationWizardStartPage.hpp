#ifndef slic3r_GUI_CalibrationWizardStartPage_hpp_
#define slic3r_GUI_CalibrationWizardStartPage_hpp_

#include "CalibrationWizardPage.hpp"

// [INTENT] CalibrationWizardStartPage.hpp declares the base and specialized "Start" page classes
// for various printer calibration wizards (PA, Flow Rate, Max Volumetric Speed).
// These pages present introductory content, images, and action buttons to begin each calibration type.
// [STATE] Base class owns UI widget pointers (labels, bitmaps, sizers) common to all start pages.
// [UNITY] Each page class maps to a MonoBehaviour-driven UI Screen or VisualElement Document.
//         Specialized pages become prefab variants with their own localized content and imagery.
// [PORTING_HAZARD:P2] The page hierarchy assumes wxWidgets sizer-based layout; Unity requires
//                      explicit anchor/constraint or flexbox-based layout alternatives.

namespace Slic3r { namespace GUI {

// [INTENT] Base class for calibration wizard start pages, providing common layout helpers
// [STATE] Owns sizers, labels, and bitmap widgets for "When", "About", and comparison imagery
// [UNITY] Base MonoBehaviour with serialized fields for TextMeshPro labels and RawImage/Sprites
class CalibrationStartPage : public CalibrationWizardPage
{
public:
    CalibrationStartPage(wxWindow*      parent,
                         wxWindowID     id    = wxID_ANY,
                         const wxPoint& pos   = wxDefaultPosition,
                         const wxSize&  size  = wxDefaultSize,
                         long           style = wxTAB_TRAVERSAL);

protected:
    CalibMode m_cali_mode; // [STATE] Which calibration mode this page belongs to

    wxBoxSizer*      m_top_sizer{nullptr};     // [STATE] Root vertical sizer
    wxBoxSizer*      m_images_sizer{nullptr};  // [STATE] Horizontal sizer for before/after images
    Label*           m_when_title{nullptr};    // [STATE] "When do you need..." title
    Label*           m_when_content{nullptr};  // [STATE] Descriptive text for when calibration is needed
    Label*           m_about_title{nullptr};   // [STATE] "About this calibration" title
    Label*           m_about_content{nullptr}; // [STATE] Detailed explanation of the calibration
    wxStaticBitmap*  m_before_bmp{nullptr};    // [STATE] "Before calibration" example image
    wxStaticBitmap*  m_after_bmp{nullptr};     // [STATE] "After calibration" example image
    wxStaticBitmap*  m_bmp_intro{nullptr};     // [STATE] Single introductory image (used when no before/after pair)
    PAPageHelpPanel* m_help_panel{nullptr};    // [STATE] Optional help panel (PA-specific)

    // [EVENT] UI construction methods called by derived classes during create_page()
    void create_when(wxWindow* parent, wxString title, wxString content);
    void create_about(wxWindow* parent, wxString title, wxString content);
    void create_bitmap(wxWindow* parent, const wxBitmap& before_img, const wxBitmap& after_img);
    void create_bitmap(wxWindow* parent, std::string before_img, std::string after_img);
    void create_bitmap(wxWindow* parent, std::string img);
};

// [INTENT] Start page for Pressure Advance (PA) calibration wizard
// [STATE] Includes PA-specific help panel and printer-series conditional UI
// [EVENT] on_device_connected() enables/shows/hides buttons based on printer capabilities
// [UNITY] PAStartPage MonoBehaviour with additional help panel and printer capability checks
class CalibrationPAStartPage : public CalibrationStartPage
{
public:
    CalibrationPAStartPage(wxWindow*      parent,
                           wxWindowID     id    = wxID_ANY,
                           const wxPoint& pos   = wxDefaultPosition,
                           const wxSize&  size  = wxDefaultSize,
                           long           style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);

    void on_reset_page();
    void on_device_connected(MachineObject* obj); // [EVENT] Updates UI when printer connects
    void msw_rescale() override;                  // [EVENT] DPI/resolution scaling
};

// [INTENT] Start page for Flow Rate calibration wizard
// [STATE] Includes extra explanatory text about foaming materials (LW-PLA) and auto-calibration limitations
// [EVENT] on_device_connected() shows/hides auto-calibration button based on printer support
// [UNITY] FlowRateStartPage MonoBehaviour with specialized content for flow calibration
class CalibrationFlowRateStartPage : public CalibrationStartPage
{
public:
    CalibrationFlowRateStartPage(wxWindow*      parent,
                                 wxWindowID     id    = wxID_ANY,
                                 const wxPoint& pos   = wxDefaultPosition,
                                 const wxSize&  size  = wxDefaultSize,
                                 long           style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);
    void on_reset_page();
    void on_device_connected(MachineObject* obj); // [EVENT] Updates UI based on printer capabilities
    void msw_rescale() override;                  // [EVENT] DPI/resolution scaling
};

// [INTENT] Start page for Max Volumetric Speed calibration wizard
// [STATE] Simplified page with just "When" section and recommendations list
// [UNITY] MaxVolumetricSpeedStartPage MonoBehaviour with minimal content layout
class CalibrationMaxVolumetricSpeedStartPage : public CalibrationStartPage
{
public:
    CalibrationMaxVolumetricSpeedStartPage(wxWindow*      parent,
                                           wxWindowID     id    = wxID_ANY,
                                           const wxPoint& pos   = wxDefaultPosition,
                                           const wxSize&  size  = wxDefaultSize,
                                           long           style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);
    void msw_rescale() override; // [EVENT] DPI/resolution scaling
};

}} // namespace Slic3r::GUI

#endif // [ANNOTATED]