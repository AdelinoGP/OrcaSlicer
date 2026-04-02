// [ANNOTATED]
// [INTENT] Control commands for the AMS (Automatic Material System) unit.
// [EVENT] Dispatches the "ams_reset" JSON command to the network layer via MachineObject.
// [UNITY] Map to a C# async method in the AMSService or PrinterService.

#include <nlohmann/json.hpp>
#include "DevFilaSystem.h"

#include "slic3r/GUI/DeviceManager.hpp" // TODO: remove this include
#include "DevUtil.h"

using namespace nlohmann;
namespace Slic3r {

int DevFilaSystem::CtrlAmsReset() const
{
    json jj_command;
    jj_command["print"]["command"]     = "ams_reset";
    jj_command["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    return m_owner->publish_json(jj_command);
}

} // namespace Slic3r