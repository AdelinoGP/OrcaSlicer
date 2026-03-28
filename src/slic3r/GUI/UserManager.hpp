#ifndef slic3r_UserManager_hpp_
#define slic3r_UserManager_hpp_

#include <map>
#include <mutex>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <boost/thread.hpp>
#include "nlohmann/json.hpp"
#include "slic3r/Utils/json_diff.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

using namespace nlohmann;

namespace Slic3r {

class NetworkAgent;

// [INTENT] Thin network-auth adapter that parses callback payloads and forwards successful bind results into GUI-owned printer state.
// [STATE] Stores only a non-owning NetworkAgent*; lifetime and thread affinity are external, so calls must assume the agent outlives each
// parse. [THREAD] parse_json may run on a network callback path; any UI mutation should be marshaled back to the main thread in the Unity
// port. [UNITY] Replace with a typed auth-result message handler plus a main-thread completion bridge; keep transport parsing out of view
// code. [PORTING_HAZARD:P2] The implicit coupling to NetworkAgent and GUI singletons means the current ownership model does not map 1:1 to
// Unity components.
class UserManager
{
private:
    NetworkAgent* m_agent{nullptr};

public:
    UserManager(NetworkAgent* agent = nullptr);
    ~UserManager();

    // [INTENT] Swap the active agent without taking ownership; parse_json uses this handle to publish the decoded result.
    void set_agent(NetworkAgent* agent);

    // [INTENT] Decode the backend payload and react only to the successful bind/auth case; malformed or negative responses should be
    // treated as no-ops.
    int parse_json(std::string payload);
};
} // namespace Slic3r

#endif //  slic3r_UserManager_hpp_
