// [ANNOTATED]
// [INTENT] Control commands for extruder toolhead switching (retry/quit).
// [EVENT] Dispatches AMS control commands ("resume", "abort") to the network layer via MachineObject.
// [UNITY] Map to C# async methods in the ExtruderService or PrinterService.

#include <nlohmann/json.hpp>
#include "DevExtruderSystem.h"

#include "slic3r/GUI/DeviceManager.hpp"

using namespace nlohmann;

namespace Slic3r {
int DevExtderSystem::CtrlRetrySwitching()
{
    if (m_owner) {
        return m_owner->command_ams_control("resume");
    }
    return -1;
}

int DevExtderSystem::CtrlQuitSwitching()
{
    if (m_owner) {
        return m_owner->command_ams_control("abort");
    }
    return -1;
}
} // namespace Slic3r