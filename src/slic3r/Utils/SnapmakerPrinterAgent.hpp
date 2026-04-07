#pragma once

#include "MoonrakerPrinterAgent.hpp"

#include <string>

namespace Slic3r {

class SnapmakerPrinterAgent final : public MoonrakerPrinterAgent
{
public:
    // [UNITY] Snapmaker support stays on the Moonraker base agent and customizes filament normalization only.
    explicit SnapmakerPrinterAgent(std::string log_dir);
    ~SnapmakerPrinterAgent() override = default;

    static AgentInfo get_agent_info_static();
    AgentInfo        get_agent_info() override { return get_agent_info_static(); }

    bool fetch_filament_info(std::string dev_id) override;

private:
    // [INTENT] Combine firmware-reported type fragments into the single material key used by Orca profiles.
    static std::string combine_filament_type(const std::string& type, const std::string& sub_type);
};

} // namespace Slic3r
