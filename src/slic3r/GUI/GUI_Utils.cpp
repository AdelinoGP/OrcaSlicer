#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "GUI_App.hpp"

#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>

#ifdef _WIN32
#include <Windows.h>
#include "libslic3r/AppConfig.hpp"
#include <wx/msw/registry.h>
#endif // _WIN32

#include <wx/toplevel.h>
#include <wx/sizer.h>
#include <wx/checkbox.h>
#include <wx/dcclient.h>
#include <wx/font.h>
#include <wx/fontutil.h>
#include <wx/display.h>

#include "libslic3r/Config.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Centralize shared helpers for dialog plumbing, DPI helpers, and ancillary file utilities that the GUI layer consumes.
// [STATE] The module owns global caches such as `dialogStack` and Win32 handles so the rest of the UI can reuse consistent metric data.
// [UNITY] Unity should split these helpers into dedicated `MonoBehaviour` utilities backed by `ScriptableObject` state containers while
// keeping UI work on the main thread.

#ifdef _WIN32
// [INTENT] Publish HID and volume attach/detach wxEvents so the GUI can rebuild device lists when Windows hardware changes occur.
// [THREAD] These events run on the wx main thread; the Unity port must marshal equivalent FileSystemWatcher/InputSystem callbacks via the
// synchronization context. [UNITY] Raise `UnityEvent`s from `FileSystemWatcher` or `InputSystem` observers and refresh the device-list
// controller MonoBehaviour. [PORTING_HAZARD:P2] Win32 event IDs do not exist under Unity or wxWidgets on other platforms, so treat this
// block as documentation for custom hardware observers.
wxDEFINE_EVENT(EVT_HID_DEVICE_ATTACHED, HIDDeviceAttachedEvent);
wxDEFINE_EVENT(EVT_HID_DEVICE_DETACHED, HIDDeviceDetachedEvent);
wxDEFINE_EVENT(EVT_VOLUME_ATTACHED, VolumeAttachedEvent);
wxDEFINE_EVENT(EVT_VOLUME_DETACHED, VolumeDetachedEvent);
#endif // _WIN32

CopyFileResult copy_file_gui(const std::string& from, const std::string& to, std::string& error_message, const bool with_check)
{
    // [INTENT] Keep GUI copy flows deterministic by handling Win32 copy errors without relying on `CopyFile`'s exception path.
    // [STATE] `ret` carries the copy outcome so dialogs can present consistent error messaging without querying Win32 again.
    // [THREAD] Runs on the UI thread; Unity should call the async copy from a background `Task.Run` and marshal completion to the main
    // thread. [UNITY] Replace with `System.IO.File.Copy` plus `UnityMainThreadDispatcher` or `async`/`await` to avoid blocking Unity's
    // render thread. [PORTING_HAZARD:P2] This Win32 handle juggling does not translate directly; treat it as documentation for the
    // reliability checks the port must re-implement.
#ifdef WIN32
    // still has exceptions
    /*wxString src = from_u8(from);
    wxString dest = from_u8(to);

    bool result = CopyFile(src.wc_str(), dest.wc_str(), false);
    if (!result) {
        DWORD errCode = GetLastError();
        error_message = "Error: " + errCode;
        return FAIL_COPY_FILE;
    }
    return SUCCESS;*/

    wxString       src  = from_u8(from);
    wxString       dest = from_u8(to);
    BOOL           result;
    char*          buff      = nullptr;
    HANDLE         handlesrc = nullptr;
    HANDLE         handledst = nullptr;
    CopyFileResult ret       = SUCCESS;

    handlesrc = CreateFile(src.wc_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, 0);
    if (handlesrc == INVALID_HANDLE_VALUE) {
        error_message = "Error: open src file";
        ret           = FAIL_COPY_FILE;
        goto __finished;
    }

    handledst = CreateFile(dest.wc_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, 0);
    if (handledst == INVALID_HANDLE_VALUE) {
        error_message = "Error: create dest file";
        ret           = FAIL_COPY_FILE;
        goto __finished;
    }

    DWORD size   = GetFileSize(handlesrc, NULL);
    buff         = new char[size + 1];
    DWORD dwRead = 0, dwWrite;
    result       = ReadFile(handlesrc, buff, size, &dwRead, NULL);
    if (!result) {
        DWORD errCode = GetLastError();
        error_message = "Error: " + errCode;
        ret           = FAIL_COPY_FILE;
        goto __finished;
    }
    buff[size] = 0;
    result     = WriteFile(handledst, buff, size, &dwWrite, NULL);
    if (!result) {
        DWORD errCode = GetLastError();
        error_message = "Error: " + errCode;
        ret           = FAIL_COPY_FILE;
        goto __finished;
    }

__finished:
    if (handlesrc)
        CloseHandle(handlesrc);
    if (handledst)
        CloseHandle(handledst);
    if (buff)
        delete[] buff;

    return ret;
#else
    return copy_file(from, to, error_message, with_check);
#endif
}

// [INTENT] Walk the parent chain to find the owning top-level window for geometry and modality helpers.
// [UNITY] Equivalent to traversing `transform.parent` until a `Canvas` or custom `WindowController` is reached.
// [THREAD] Walks the wx hierarchy on the UI thread; Unity must mirror this on the main thread before rescheduling dialog work.
wxTopLevelWindow* find_toplevel_parent(wxWindow* window)
{
    for (; window != nullptr; window = window->GetParent()) {
        if (window->IsTopLevel()) {
            return dynamic_cast<wxTopLevelWindow*>(window);
        }
    }

    return nullptr;
}

// [INTENT] Wait until the top-level window geometry settles before running `callback`, since different platforms deliver `wxEVT_SHOW` at
// different times. [EVENT] Binds to `wxEVT_SHOW` and `CallAfter` when necessary to guarantee the callback runs on a stable frame, mimicking
// Unity's `OnEnable` + layout cycle. [STATE] Ensures geometry-related state (size/position) is stable before the callback mutates layout.
// [PORTING_HAZARD:P3] Linux defers geometry via `CallAfter`, so Unity should guard against asynchronous layout updates as well.
void on_window_geometry(wxTopLevelWindow* tlw, std::function<void()> callback)
{
#ifdef _WIN32
    // On windows, the wxEVT_SHOW is not received if the window is created maximized
    // cf. https://groups.google.com/forum/#!topic/wx-users/c7ntMt6piRI
    // OTOH the geometry is available very soon, so we can call the callback right away
    callback();
#elif defined __linux__
    tlw->Bind(wxEVT_SHOW, [=](wxShowEvent& evt) {
        // On Linux, the geometry is only available after wxEVT_SHOW + CallAfter
        // cf. https://groups.google.com/forum/?pli=1#!topic/wx-users/fERSXdpVwAI
        tlw->CallAfter([=]() { callback(); });
        evt.Skip();
    });
#elif defined __APPLE__
    tlw->Bind(wxEVT_SHOW, [=](wxShowEvent& evt) {
        callback();
        evt.Skip();
    });
#endif
}

#if !wxVERSION_EQUAL_OR_GREATER_THAN(3, 1, 3)
wxDEFINE_EVENT(EVT_DPI_CHANGED_SLICER, DpiChangedEvent);
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN

#ifdef _WIN32
// [STATE] Caches loaded DLL handles so repeated DPI queries reuse the same modules without reloading, limiting Win32 dependencies.
template<class F> typename F::FN winapi_get_function(const wchar_t* dll, const char* fn_name)
{
    static HINSTANCE dll_handle = LoadLibraryExW(dll, nullptr, 0);

    if (dll_handle == nullptr) {
        return nullptr;
    }
    return (typename F::FN) GetProcAddress(dll_handle, fn_name);
}
#endif

// [INTENT] Query per-window DPI across Windows versions so fonts and geometry scale correctly for each monitor.
// [PORTING_HAZARD:P2] This path relies on Windows APIs such as `GetDpiForWindow`, so Unity should rely on `Screen.dpi`/`Display` info
// instead of replicating these Win32 calls. [UNITY] Use `Display.main.systemWidth`/`systemHeight` plus `Screen.dpi`, or surface native OS
// DPI through a native plugin for precise control. If called with nullptr, a DPI for the primary monitor is returned.
int get_dpi_for_window(const wxWindow* window)
{
#ifdef _WIN32
    enum MONITOR_DPI_TYPE_ {
        // This enum is inlined here to avoid build-time dependency
        MDT_EFFECTIVE_DPI_ = 0,
        MDT_ANGULAR_DPI_   = 1,
        MDT_RAW_DPI_       = 2,
        MDT_DEFAULT_       = MDT_EFFECTIVE_DPI_,
    };

    // Need strong types for winapi_get_function() to work
    struct GetDpiForWindow_t
    {
        typedef HRESULT(WINAPI* FN)(HWND hwnd);
    };
    struct GetDpiForMonitor_t
    {
        typedef HRESULT(WINAPI* FN)(HMONITOR hmonitor, MONITOR_DPI_TYPE_ dpiType, UINT* dpiX, UINT* dpiY);
    };

    static auto GetDpiForWindow_fn  = winapi_get_function<GetDpiForWindow_t>(L"User32.dll", "GetDpiForWindow");
    static auto GetDpiForMonitor_fn = winapi_get_function<GetDpiForMonitor_t>(L"Shcore.dll", "GetDpiForMonitor");

    // Desktop Window is the window of the primary monitor.
    const HWND hwnd = (window == nullptr) ? ::GetDesktopWindow() : window->GetHandle();

    if (GetDpiForWindow_fn != nullptr) {
        // We're on Windows 10, we have per-screen DPI settings
        return GetDpiForWindow_fn(hwnd);
    } else if (GetDpiForMonitor_fn != nullptr) {
        // We're on Windows 8.1, we have per-system DPI
        // Note: MonitorFromWindow() is available on all Windows.

        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        UINT           dpiX;
        UINT           dpiY;
        return GetDpiForMonitor_fn(monitor, MDT_EFFECTIVE_DPI_, &dpiX, &dpiY) == S_OK ? dpiX : DPI_DEFAULT;
    } else {
        // We're on Windows earlier than 8.1, use DC

        const HDC hdc = GetDC(hwnd);
        if (hdc == NULL) {
            return DPI_DEFAULT;
        }
        return GetDeviceCaps(hdc, LOGPIXELSX);
    }
#elif defined __linux__
    // TODO
    return DPI_DEFAULT;
#elif defined __APPLE__
    // TODO
    return DPI_DEFAULT;
#else // freebsd and others
    // TODO
    return DPI_DEFAULT;
#endif
}

// [INTENT] Match fonts to the requested DPI by first trying `SystemParametersInfoForDpi` and then rescaling the system font.
// [UNITY] Unity should adjust font size via `Screen.dpi` and custom theme settings instead of replicating Win32 metrics.
// [PORTING_HAZARD:P2] This Win32-specific path must be replaced by a platform-neutral scaling helper when porting to Unity.
wxFont get_default_font_for_dpi(const wxWindow* window, int dpi)
{
#ifdef _WIN32
    // First try to load the font with the Windows 10 specific way.
    struct SystemParametersInfoForDpi_t
    {
        typedef BOOL(WINAPI* FN)(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, UINT dpi);
    };
    static auto SystemParametersInfoForDpi_fn = winapi_get_function<SystemParametersInfoForDpi_t>(L"User32.dll",
                                                                                                  "SystemParametersInfoForDpi");
    if (SystemParametersInfoForDpi_fn != nullptr) {
        NONCLIENTMETRICS nm;
        memset(&nm, 0, sizeof(NONCLIENTMETRICS));
        nm.cbSize = sizeof(NONCLIENTMETRICS);
        if (SystemParametersInfoForDpi_fn(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &nm, 0, dpi))
            return wxFont(wxNativeFontInfo(nm.lfMessageFont, window));
    }
    // Then try to guesstimate the font DPI scaling on Windows 8.
    // Let's hope that the font returned by the SystemParametersInfo(), which is used by wxWidgets internally, makes sense.
    int dpi_primary = get_dpi_for_window(nullptr);
    if (dpi_primary != dpi) {
        // Rescale the font.
        return wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Scaled(float(dpi) / float(dpi_primary));
    }
#endif
    return wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
}

// [STATE] Determines whether dark-mode colors should be used by inspecting system appearance or fallback heuristics.
// [INTENT] Keep a single source of truth for dark-mode detection so downstream panels update consistently.
// [UNITY] Mirror this via a `DarkModeSettings` ScriptableObject or by querying `SystemInfo.operatingSystem` + user preferences.
// [PORTING_HAZARD:P3] OS-specific registry lookups are disabled here, so the Unity port should rely on cross-platform heuristics instead of
// Win32 APIs.
bool check_dark_mode()
{
#if 0 // #ifdef _WIN32  // #ysDarkMSW - Allow it when we deside to support the sustem colors for application
    wxRegKey rk(wxRegKey::HKCU,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
    if (rk.Exists() && rk.HasValue("AppsUseLightTheme")) {
        long value = -1;
        rk.QueryValue("AppsUseLightTheme", &value);
        return value <= 0;
    }
#endif
#if wxCHECK_VERSION(3, 1, 3)
    return wxSystemSettings::GetAppearance().IsDark();
#else
    const unsigned luma = wxGetApp().get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    return luma < 128;
#endif
}

#ifdef _WIN32
// [INTENT] Toggle window colors when dark mode is enabled so dialogs blend with the system palette (currently stubbed out).
// [UNITY] Replace with a ScriptableObject-driven palette witch that propagates to every `VisualElement` or `Canvas` component.
// [PORTING_HAZARD:P3] The Windows-only `app_config` flag here is optional; Unity should rely on its own theme store instead of Win32 APIs.
void update_dark_ui(wxWindow* window)
{
#ifdef SUPPORT_DARK_MODE
    bool is_dark = wxGetApp().app_config->get("dark_color_mode") == "1";
#else
    bool is_dark = false;
#endif
    // window->SetBackgroundColour(is_dark ? wxColour(43,  43,  43)  : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    // window->SetForegroundColour(is_dark ? wxColour(250, 250, 250) : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
}
#endif

// [EVENT] Triggered when the host appearance changes so the config flag stays in sync with the theme selector.
// [UNITY] Unity should write the preference to a `DarkModeSettings` asset and broadcast it through UI controllers instead of `app_config`.
void update_dark_config()
{
    wxSystemAppearance app = wxSystemSettings::GetAppearance();
    GUI::wxGetApp().app_config->set("dark_color_mode", app.IsDark() ? "1" : "0");
    wxGetApp().Update_dark_mode_flag();
}

// [INTENT] Inject a checkbox into file dialogs so callers can add extra toggles without reworking every dialog definition.
// [STATE] Stores `checkbox_label` and defaults the toggle to `true` so callers can read the value after the panel is constructed.
// [UNITY] Unity would instantiate a `Toggle` prefab inside a dialog `VisualElement` or UGUI canvas to achieve the same UX.
CheckboxFileDialog::ExtraPanel::ExtraPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
    // WARN: wxMSW does some extra shenanigans to calc the extra control size.
    // It first calls the create function with a dummy empty wxDialog parent and saves its size.
    // Afterwards, the create function is called again with the real parent.
    // Additionally there's no way to pass any extra data to the create function (no closure),
    // which is why we have to this stuff here. Grrr!
    auto*          dlg = dynamic_cast<CheckboxFileDialog*>(parent);
    const wxString checkbox_label(dlg != nullptr ? dlg->checkbox_label : wxString("String long enough to contain dlg->checkbox_label"));

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    cbox        = new wxCheckBox(this, wxID_ANY, checkbox_label);
    cbox->SetValue(true);
    sizer->AddSpacer(5);
    sizer->Add(this->cbox, 0, wxEXPAND | wxALL, 5);
    SetSizer(sizer);
    sizer->SetSizeHints(this);
}

wxWindow* CheckboxFileDialog::ExtraPanel::ctor(wxWindow* parent) { return new ExtraPanel(parent); }

// [INTENT] Wrap `wxFileDialog` with an optional checkbox so callers can expose extra consent toggles without duplicating UI wiring.
// [STATE] Remembers the label and default value, letting `get_checkbox_value` read it once the dialog closes.
// [UNITY] Unity should inject a `Toggle` prefab into the dialog prefab and expose the value via a `DialogContext` ScriptableObject.
// [PORTING_HAZARD:P3] wxFileDialog extra controls are platform-specific; the port must support injecting arbitrary toggles into the shared
// dialog template.
CheckboxFileDialog::CheckboxFileDialog(wxWindow*       parent,
                                       const wxString& checkbox_label,
                                       bool            checkbox_value,
                                       const wxString& message,
                                       const wxString& default_dir,
                                       const wxString& default_file,
                                       const wxString& wildcard,
                                       long            style,
                                       const wxPoint&  pos,
                                       const wxSize&   size,
                                       const wxString& name)
    : wxFileDialog(parent, message, default_dir, default_file, wildcard, style, pos, size, name), checkbox_label(checkbox_label)
{
    if (checkbox_label.IsEmpty()) {
        return;
    }

    SetExtraControlCreator(ExtraPanel::ctor);
}

// [STATE] Reads the extra checkbox state so callers know if the user opted into the additional toggle.
// [UNITY] Expose this via a `DialogResponse` scriptable object and bind a `Toggle` to the dialog lifecycle.
bool CheckboxFileDialog::get_checkbox_value() const
{
    auto* extra_panel = dynamic_cast<ExtraPanel*>(GetExtraControl());
    return extra_panel != nullptr ? extra_panel->cbox->GetValue() : false;
}

// [INTENT] Snapshot window rect/maximized state for config persistence and restore flows.
// [STATE] Stores `rect` and `maximized` in the `WindowMetrics` object so callers can rehydrate `SetSize`/`Maximize` on restore.
// [UNITY] Unity should store similar data in a serialized `WindowMetrics` ScriptableObject or `PlayerPrefs` entry.
WindowMetrics WindowMetrics::from_window(wxTopLevelWindow* window)
{
    WindowMetrics res;
    res.rect      = window->GetScreenRect();
    res.maximized = window->IsMaximized();
    return res;
}

// [INTENT] Parse serialized geometry so restored windows honor saved layout while ignoring corrupted strings.
// [PORTING_HAZARD:P3] The semicolon-separated metrics format must be kept in sync with Unity's serializer if both read the same config key.
boost::optional<WindowMetrics> WindowMetrics::deserialize(const std::string& str)
{
    std::vector<std::string> metrics_str;
    metrics_str.reserve(5);

    if (!unescape_strings_cstyle(str, metrics_str) || metrics_str.size() != 5) {
        return boost::none;
    }

    int metrics[5];
    try {
        for (size_t i = 0; i < 5; i++) {
            metrics[i] = boost::lexical_cast<int>(metrics_str[i]);
        }
    } catch (const boost::bad_lexical_cast&) {
        return boost::none;
    }

    if ((metrics[4] & ~1) != 0) { // Checks if the maximized flag is 1 or 0
        metrics[4] = 0;
    }

    WindowMetrics res;
    res.rect      = wxRect(metrics[0], metrics[1], metrics[2], metrics[3]);
    res.maximized = metrics[4] != 0;

    return res;
}

// [INTENT] Clamp the stored rect so a restored window never drifts off-screen, adjusting thresholds for modern displays.
// [UNITY] Unity should clamp `RectTransform` positions using screen bounds before applying them.
void WindowMetrics::sanitize_for_display(const wxRect& screen_rect)
{
    rect = rect.Intersect(screen_rect);

    // Prevent the window from going too far towards the right and/or bottom edge
    // It's hardcoded here that the threshold is 80% of the screen size
    rect.x = std::min(rect.x, screen_rect.x + 4 * screen_rect.width / 5);
    rect.y = std::min(rect.y, screen_rect.y + 4 * screen_rect.height / 5);
}

// [INTENT] Re-center the window before showing it so it opens close to the display midpoint when restoring defaults.
// [UNITY] Unity should set normalized `RectTransform` pivots and positions around the screen center when spawning new windows.
void WindowMetrics::center_for_display(const wxRect& screen_rect)
{
    rect.x = std::max(0, (screen_rect.GetWidth() - rect.GetWidth()) / 2);
    rect.y = std::max(0, (screen_rect.GetHeight() - rect.GetHeight()) / 2);
}

// [INTENT] Emit geometry as a semicolon-delimited string so config storage stays human-readable.
// [UNITY] Unity can use JSON or `PlayerPrefs` plus a serializer function to mirror this string if interoperability is required.
std::string WindowMetrics::serialize() const
{
    return (boost::format("%1%; %2%; %3%; %4%; %5%") % rect.x % rect.y % rect.width % rect.height % static_cast<int>(maximized)).str();
}

std::ostream& operator<<(std::ostream& os, const WindowMetrics& metrics) { return os << '(' << metrics.serialize() << ')'; }

// [INTENT] RAII timer that prints task durations to stdout and `OutputDebugString` so long-running UI work can be profiled manually.
// [STATE] Records the `task_name` and capture time to compute elapsed milliseconds in the destructor.
// [THREAD] Destruction logs run on the same thread that owns the timer so Unity's port should marshal logging to the main thread if
// profiling UI operations. [UNITY] Map to `UnityEngine.Profiling.Profiler.BeginSample`/`EndSample` or `Debug.Log` if instrumentalizing UI
// jobs in the Unity port.
TaskTimer::TaskTimer(std::string task_name) : task_name(task_name.empty() ? "task" : task_name)
{
    start_timer = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
}

TaskTimer::~TaskTimer()
{
    std::chrono::milliseconds stop_timer = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    auto        process_duration = std::chrono::milliseconds(stop_timer - start_timer).count();
    std::string out              = (boost::format("\n!!! %1% duration = %2% ms \n\n") % task_name % process_duration).str();
    printf("%s", out.c_str());
#ifdef __WXMSW__
    std::wstring stemp = std::wstring(out.begin(), out.end());
    OutputDebugString(stemp.c_str());
#endif
}

/* Image Generator */
// [INTENT] Load PNG/BMP/JPG textures via `wxImage` so GUI panes can render thumbnails and icons.
// [THREAD] Should run on a worker thread to avoid blocking the UI; Unity should use async texture loading helpers instead of blocking the
// GameThread. [UNITY] Unity should use `Texture2D.LoadImage` or `Resources.Load` once the bytes are available. [PORTING_HAZARD:P3] Depends
// on wxImage handlers; replace with cross-platform Texture2D helpers in Unity.
bool load_image(const std::string& filename, wxImage& image)
{
    bool result = true;
    if (boost::algorithm::iends_with(filename, ".png")) {
        result = image.LoadFile(wxString::FromUTF8(filename.c_str()), wxBITMAP_TYPE_PNG);
    } else if (boost::algorithm::iends_with(filename, ".bmp")) {
        result = image.LoadFile(wxString::FromUTF8(filename.c_str()), wxBITMAP_TYPE_BMP);
    } else if (boost::algorithm::iends_with(filename, ".jpg")) {
        result = image.LoadFile(wxString::FromUTF8(filename.c_str()), wxBITMAP_TYPE_JPEG);
    } else if (boost::algorithm::iends_with(filename, ".jpeg")) {
        result = image.LoadFile(wxString::FromUTF8(filename.c_str()), wxBITMAP_TYPE_JPEG);
    } else {
        return false;
    }
    return result;
}

// [INTENT] Resize/crop provided images to the requested size for thumbnails and textures used by the GUI.
// [OPENGL] Prepares PNG RGBA data so GL textures can be updated without blocking render frames.
// [THREAD] Runs on worker threads in wxWidgets; Unity should offload the conversion to background tasks before touching the render loop.
// [UNITY] Unity should convert these into `Texture2D` assets via `Resize`, `Crop`, or `RenderTexture` helpers instead of this wxImage path.
// [PORTING_HAZARD:P3] Relies on wxImage handlers; porters must reimplement the resizing logic with Unity `Texture2D` operations.
bool generate_image(const std::string& filename, wxImage& image, wxSize img_size, int method)
{
    wxInitAllImageHandlers();

    bool    result = true;
    wxImage img;
    result = load_image(filename, img);
    if (!result)
        return result;

    image = wxImage(img_size);
    image.SetType(wxBITMAP_TYPE_PNG);
    if (!image.HasAlpha()) {
        image.InitAlpha();
    }

    // image.Clear(0);
    // unsigned char *alpha = image.GetAlpha();
    unsigned char* alpha = new unsigned char[image.GetWidth() * image.GetHeight()];
    if (alpha) {
        ::memset(alpha, wxIMAGE_ALPHA_TRANSPARENT, image.GetWidth() * image.GetHeight());
    }
    if (method == GERNERATE_IMAGE_RESIZE) {
        float h_factor   = img.GetHeight() / (float) image.GetHeight();
        float w_factor   = img.GetWidth() / (float) image.GetWidth();
        float factor     = std::min(h_factor, w_factor);
        int   tar_height = (int) ((float) img.GetHeight() / factor);
        int   tar_width  = (int) ((float) img.GetWidth() / factor);
        img              = img.Rescale(tar_width, tar_height);
        image.Paste(img, (image.GetWidth() - tar_width) / 2, (image.GetHeight() - tar_height) / 2);
    } else if (method == GERNERATE_IMAGE_CROP_VERTICAL) {
        float w_factor   = img.GetWidth() / (float) image.GetWidth();
        int   tar_height = (int) ((float) img.GetHeight() / w_factor);
        int   tar_width  = (int) ((float) img.GetWidth() / w_factor);
        img              = img.Rescale(tar_width, tar_height);
        image.Paste(img, (image.GetWidth() - tar_width) / 2, (image.GetHeight() - tar_height) / 2);
    } else {
        return false;
    }

    // image.ConvertAlphaToMask(image.GetMaskRed(), image.GetMaskGreen(), image.GetMaskBlue());
    return true;
}

// [STATE] Tracks the stack of modal dialogs so the GUI can navigate the chain when closing or re-focusing windows.
// [EVENT] Push/pop operations for this stack live inside `DPIAware::ShowModal` inside `GUI_Utils.hpp`, providing a single point to gate ESC/Cancel
// handlers and prevent stale modal events. [THREAD] Muted from the UI thread; Unity's dialog manager should serialize stack operations on
// the main thread as well. [UNITY] Unity should replicate this via a `DialogManager` that queues GameObjects and restores focus order.
std::deque<wxDialog*> dialogStack;

// [INTENT] Clamp dialog size so it never exceeds 80% of the current display, preventing off-screen windows.
// [UNITY] Unity should clamp `RectTransform` size using `Display.main` bounds before presenting new dialogs.
void fit_in_display(wxTopLevelWindow& window, wxSize desired_size)
{
    const auto display_size = wxDisplay(window.GetParent()).GetClientArea();
    if (desired_size.GetWidth() > display_size.GetWidth()) {
        desired_size.SetWidth(display_size.GetWidth() * 4 / 5);
    }
    if (desired_size.GetHeight() > display_size.GetHeight()) {
        desired_size.SetHeight(display_size.GetHeight() * 4 / 5);
    }

    window.SetSize(desired_size);
}

#ifdef __linux__
// [INTENT] Detect a debugger via `/proc/self/status` so UI flows can adjust logging or timeouts when attached.
// [PORTING_HAZARD:P3] This Linux-only `/proc` check should be replaced with `System.Diagnostics.Debugger.IsAttached` or similar when
// targeting Unity. https://stackoverflow.com/a/69842462/3289421
bool is_debugger_present()
{
    std::ifstream sf("/proc/self/status");
    std::string   s;
    while (sf >> s) {
        if (s == "TracerPid:") {
            int pid;
            sf >> pid;
            return pid != 0;
        }
        std::getline(sf, s);
    }

    return false;
}
#endif

}} // namespace Slic3r::GUI
