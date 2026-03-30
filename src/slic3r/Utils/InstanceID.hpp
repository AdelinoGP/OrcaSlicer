#pragma once

#include <string>

namespace Slic3r {

class AppConfig;

// [INTENT] Public interface for machine-unique identifier (IID) resolution.
// This identifier is stable across updates and config resets, and is primarily
// used by cloud synchronization, telemetry, and update-checking services.
//
// [STATE] The implementation manages process-global singleton state for memoization
// (see InstanceID.cpp).
//
// [UNITY] Map the `ensure()` call to a Unity ScriptableObject provider or a
// static `InstanceIDProvider.Instance.ID` property.
//
// [PORTING_HAZARD:P3] The interface depends on `AppConfig` for the first-run resolution;
// in Unity, this should be decoupled into a standalone service or an injected
// ScriptableObject-backed settings model.
//
namespace instance_id {

// Returns the canonical IID, generating and storing one when missing.
std::string ensure(AppConfig& config);

// Clears the cached IID (primarily for tests).
void reset_cache_for_tests();

} // namespace instance_id

} // namespace Slic3r
