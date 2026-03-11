#pragma once

#include <string>
#include <vector>

namespace Slic3r {

struct MaterialTypeInfo
{
    // [INTENT] Normalized material envelope used by calibration/safety heuristics and
    //          profile validation to reason about temperature/mechanical bounds.
    std::string name;
    int         min_temp;
    int         max_temp;
    int         chamber_min_temp;
    int         chamber_max_temp;
    double      adhesion_coefficient;
    double      yield_strength;
    double      thermal_length;
};

class MaterialType
{
public:
    // [STATE] Returns process-wide static registry populated in MaterialType.cpp.
    static const std::vector<MaterialTypeInfo>& all();

    // [INTENT] Name-key lookup helper for configuration strings in presets and UI.
    static const MaterialTypeInfo* find(const std::string& name);

    // [STATE] Out-parameters are mutated only on successful lookup.
    // [COUPLING] Callers typically feed these values into PrintConfig validation paths.
    static bool get_temperature_range(const std::string& type, int& min_temp, int& max_temp);
    static bool get_chamber_temperature_range(const std::string& type, int& chamber_min_temp, int& chamber_max_temp);
    static bool get_adhesion_coefficient(const std::string& type, double& adhesion_coefficient);
    static bool get_yield_strength(const std::string& type, double& yield_strength);
    static bool get_thermal_length(const std::string& type, double& thermal_length);
};

} // namespace Slic3r
