#include "libslic3r/libslic3r.h"
#include "UserManager.hpp"
#include "DeviceManager.hpp"
#include "NetworkAgent.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"

#include "DeviceCore/DevManager.h"

namespace Slic3r {

// [INTENT] Thin adapter between network-auth callbacks and GUI/device state.
// [STATE] Keeps only a non-owning NetworkAgent pointer; all real session state lives in GUI/App singletons and DeviceManager.
// [UNITY] Replace this with a main-thread auth event handler that updates a shared session model and then drives dialog/UI state.
// [PORTING_HAZARD:P2] The parser directly mutates modal GUI state and selected-machine state, so transport and presentation are tightly coupled.
UserManager::UserManager(NetworkAgent* agent) { m_agent = agent; }

UserManager::~UserManager() {}

void UserManager::set_agent(NetworkAgent* agent)
{
    // [STATE] Dependency injection for the active transport; ownership stays external.
    m_agent = agent;
}

// [INTENT] Decode a server payload and handle the narrow bind-success path used
// for login / machine binding.
// [THREAD] This likely runs from a network callback path, so wx dialog and
// DeviceManager calls must be marshaled onto the UI thread in a Unity port.
// [UNCLEAR] The code ignores most payload variants and swallows all
// parse/runtime errors; hypothesis: only bind success is meant to surface here.
// [UNITY] Model this as a typed auth-result message plus a UI-thread
// completion callback rather than parsing JSON in the view layer.
int UserManager::parse_json(std::string payload)
{
    bool restored_json = false;
    json j;
    json j_pre = json::parse(payload);
    if (j_pre.empty()) {
        return -1;
    }

    // bind/unbind

    try {
        if (j_pre.contains("bind")) {
            if (j_pre["bind"].contains("command")) {
                // bind
                if (j_pre["bind"]["command"].get<std::string>() == "bind") {
                    std::string dev_id;
                    std::string result;

                    if (j_pre["bind"].contains("dev_id")) {
                        dev_id = j_pre["bind"]["dev_id"].get<std::string>();
                    }

                    if (j_pre["bind"].contains("result")) {
                        result = j_pre["bind"]["result"].get<std::string>();
                    }

                    if (result == "success") {
                        DeviceManager* dev = GUI::wxGetApp().getDeviceManager();
                        if (!dev) {
                            return -1;
                        }

                        if (GUI::wxGetApp().m_ping_code_binding_dialog && GUI::wxGetApp().m_ping_code_binding_dialog->IsShown()) {
                            GUI::wxGetApp().m_ping_code_binding_dialog->EndModal(wxCLOSE);
                            GUI::MessageDialog msgdialog(nullptr, _L("Log in successful."), "", wxAPPLY | wxOK);
                            msgdialog.ShowModal();
                        }
                        dev->update_user_machine_list_info();
                        dev->set_selected_machine(dev_id);
                        return 0;
                    }
                }
            }
        }
    } catch (...) {}

    return -1;
}

} // namespace Slic3r
