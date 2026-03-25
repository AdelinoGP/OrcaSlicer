#ifndef __OAuthDialog_HPP__
#define __OAuthDialog_HPP__

#include "GUI_Utils.hpp"
#include "Jobs/OAuthJob.hpp"
#include "Jobs/Worker.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Defines the OAuthDialog class for orchestrating modal OAuth authorization flow and its background worker.
class OAuthDialog : public DPIDialog
{
private:
    // [STATE] Stores the parameters for authorization and the shared result populated by the OAuthJob.
    OAuthParams                  _params;
    std::shared_ptr<OAuthResult> _result;

    // [THREAD] Background worker for executing the asynchronous OAuthJob.
    std::unique_ptr<Worker> m_worker;

    void on_cancel(wxEvent& event);

protected:
    // [EVENT] Show override starts the authorization process.
    bool Show(bool show) override;
    // [UNITY] Unity's UI scaling and layout components handle window resizing automatically.
    void on_dpi_changed(const wxRect& suggested_rect) override;

public:
    OAuthDialog(wxWindow* parent, OAuthParams params);

    // [INTENT] Returns the final authorization result to the caller.
    OAuthResult get_result() { return *_result; }
};

}} // namespace Slic3r::GUI

#endif
