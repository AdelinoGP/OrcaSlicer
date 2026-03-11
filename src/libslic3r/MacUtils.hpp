#ifndef __MAC_UTILS_H
#define __MAC_UTILS_H

namespace Slic3r {

// [INTENT] Capability probe for legacy Boost.Log file appender behavior on macOS.
// [COUPLING] Allows platform adapters to gate logging setup without pulling Objective-C++
//            checks into cross-platform modules.
bool is_macos_support_boost_add_file_log();

// [INTENT] Runtime guard for macOS 15 specific behavior toggles.
// [HAZARD] Returns int instead of bool for historical ABI compatibility with callers.
int is_mac_version_15();
} // namespace Slic3r

#endif
