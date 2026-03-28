#ifndef SKIPPARTCANVAS_H
#define SKIPPARTCANVAS_H
#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <opencv2/opencv.hpp>
#include <wx/textctrl.h>
#include <vector>
#include <expat.h>
#include <libslic3r/Color.hpp>
#include <boost/thread/mutex.hpp>
#include "PartSkipCommon.hpp"

wxDECLARE_EVENT(EVT_ZOOM_PERCENT, wxCommandEvent);
wxDECLARE_EVENT(EVT_CANVAS_PART, wxCommandEvent);

namespace Slic3r { namespace GUI {

using Coord      = float;
using FloatPoint = std::array<Coord, 2>;

struct ObjectInfo
{
    std::string name{""};
    int         identify_id{-1};
    PartState   state{psUnCheck};
};

// [INTENT] Interactive GL canvas for part-skipping: it loads a color-coded pick image,
// tracks per-object selection state, and translates mouse/zoom gestures into custom wx events.
// [STATE] The canvas owns the current pick image, the part-id-to-triangle/pick lookup tables,
// zoom/offset/drag state, hover id, and the parent background tint used while rendering.
// [OPENGL] wxGLCanvas + wxGLContext are the retained rendering boundary; Render() is expected
// to rebuild the immediate-mode view and keep the pick-image hit test aligned with the frame.
// [UNITY] Port this as a custom controller over a RenderTexture-backed image view with explicit
// hit-test data, a scroll/zoom model, and a separate input bridge for drag and wheel gestures.
// [PORTING_HAZARD:P2] Color-based picking and manual OpenGL canvas lifetime assumptions do not
// map 1:1 to Unity UI; keep hit testing and draw sequencing in a dedicated view-model/service.
class SkipPartCanvas : public wxGLCanvas
{
    union SkipIdHelper {
        uint32_t value = 0;

        struct
        {
            uint8_t b;
            uint8_t g;
            uint8_t r;
            uint8_t _padding;
        };

        uint8_t bytes[4];

        SkipIdHelper() = default;

        SkipIdHelper(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue), _padding(0) {}

        SkipIdHelper(uint32_t val) : value(val) {}
        void reverse()
        {
            uint8_t tmp{r};
            r = b;
            b = tmp;
        }
    };

public:
    SkipPartCanvas(wxWindow* parent, const wxGLAttributes& dispAttrs);
    ~SkipPartCanvas() = default;

    void SetParentBackground(const ColorRGB& color) { parent_color_ = color; }

    void LoadPickImage(const std::string& path);
    void ZoomIn(const int zoom_percent);
    void ZoomOut(const int zoom_percent);
    void SwitchDrag(const bool drag_on);
    void UpdatePartsInfo(const PartsInfo& parts);
    void SetZoomPercent(const int value);
    void SetOffset(const wxPoint& value);

    wxTextCtrl* log_ctrl;

protected:
    // [EVENT] wx paint/mouse callbacks feed hit-testing, drag state changes, and zoom updates;
    // Unity should route these through a UI input bridge rather than relying on canvas events.
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnMouseRightDown(wxMouseEvent& event);
    void OnMouseRightUp(wxMouseEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);

private:
    wxGLContext*                                                       context_;
    cv::Mat                                                            pick_image_;
    std::unordered_map<uint32_t, std::vector<std::vector<FloatPoint>>> parts_triangles_;
    std::unordered_map<uint32_t, std::vector<std::vector<cv::Point>>>  pick_parts_;
    std::unordered_map<uint32_t, PartState>                            parts_state_;
    bool                                                               gl_inited_{false};
    int                                                                zoom_percent_{100};
    wxPoint                                                            offset_{0, 0};
    wxPoint                                                            drag_start_offset_{0, 0};
    wxPoint                                                            drag_start_pt_{0, 0};
    bool                                                               is_draging_{false};
    bool                                                               fixed_draging_{false};
    bool                                                               left_down_{false};
    ColorRGB                                                           parent_color_ = ColorRGB();
    int                                                                hover_id_{-1};
    double                                                             image_view_scale_{1};

    // [EVENT] These helpers emit selection and zoom notifications back to the owning dialog.
    void SendSelectEvent(int id, PartState state);
    void SendZoomEvent(int zoom_percent);

    inline double   Zoom() const;
    inline wxPoint  ViewPtToImagePt(const wxPoint& view_pt) const;
    uint32_t        GetIdAtImagePt(const wxPoint& image_pt) const;
    inline uint32_t GetIdAtViewPt(const wxPoint& view_pt) const;

    void ProcessHover(const wxPoint& mouse_pt);
    void AutoSetCursor();
    void StartDrag(const wxPoint& mouse_pt);
    void ProcessDrag(const wxPoint& mouse_pt);
    void EndDrag();

    void Render();

    void DebugLogLine(std::string str);
};

// [INTENT] Thread-safe error accumulator shared by the 3MF parsing helpers below.
// [STATE] The mutex guards a mutable error list so parser callbacks can collect failures
// before a later UI-facing log dump.
class _BBS_3MF_Base
{
    mutable boost::mutex             mutex;
    mutable std::vector<std::string> m_errors;

protected:
    void add_error(const std::string& error) const;
    void clear_errors();

public:
    void log_errors();
};

struct PlateInfo
{
    int                     index{-1};
    std::vector<ObjectInfo> objects;
    bool                    label_object_enabled = false;
};

// [INTENT] Parse 3MF model metadata into plate/object state that SkipPartCanvas consumes.
// [STATE] ParseContext stores the current plate, temporary object, and in-plate flag while
// Expat walks the XML stream.
// [THREAD] The parser is isolated from the canvas itself, but the shared error store is mutexed,
// so the Unity port should treat this as a background import/service boundary rather than UI code.
// [UNITY] Move this to a data import service that produces plain plate/object DTOs for the view.
// [PORTING_HAZARD:P3] UI and import concerns are mixed in one header today; split them cleanly in
// Unity so parsing can run off-thread without any canvas dependency.
class ModelSettingHelper : public _BBS_3MF_Base
{
    struct ParseContext
    {
        std::vector<PlateInfo> plates;
        PlateInfo              current_plate;
        ObjectInfo             temp_object;
        bool                   in_plate = false;
    };

public:
    ModelSettingHelper(const std::string& path);

    bool                    Parse();
    std::vector<ObjectInfo> GetPlateObjects(int plate_idx);
    bool                    GetLabelObjectEnabled(int plate_idx);

private:
    std::string  path_;
    ParseContext context_;

    static void XMLCALL StartElementHandler(void* userData, const XML_Char* name, const XML_Char** atts);
    static void XMLCALL EndElementHandler(void* userData, const XML_Char* name);
    // [UNCLEAR] Hypothesis: Expat callbacks build the plate/object graph synchronously from a
    // 3MF sidecar payload; keep the parse context local so Unity can mirror this as a service.
    void DataHandler(const XML_Char* s, int len);
};

}} // namespace Slic3r::GUI
#endif // SKIPPARTCANVAS_H
