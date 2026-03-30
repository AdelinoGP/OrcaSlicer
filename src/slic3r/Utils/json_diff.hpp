// [ANNOTATED]
#ifndef __JSON_DIFF_HPP
#define __JSON_DIFF_HPP

#include <string>
#include <atomic>
#include <vector>

#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace std;

// [INTENT] Stateful JSON diff/patch codec used to minimize network bandwidth for printer
// status updates. It maintains a "last known full state" (base) and only transmits/receives
// the recursive delta between the new state and the base.
//
// [STATE] Managed members for the codec's synchronization window:
// - `printer_type/version`: Context for loading baseline settings from disk.
// - `settings_base`: Merged baseline from versioned JSON files in `data_dir()`.
// - `full_message`: Cached full payload for resync/reset.
// - `diff2all_base/all2diff_base`: The "last seen" states for decode and encode respectively.
// - `decode_error_count`: Tracks failure density for heuristic re-request trigger.
//
// [UNITY] Re-implement using `Newtonsoft.Json` (JObject/JToken) or `System.Text.Json`
// with a custom recursive merge/diff visitor. Ensure the `load_compatible_settings`
// path is mapped to Unity's `Application.persistentDataPath` or `StreamingAssets`.
//
// [PORTING_HAZARD:P2] The codec is strictly stateful and sequential. Message loss or
// out-of-order delivery will corrupt the local `diff2all_base` until a full-state
// reset is triggered by `decode_error_count`.
//
class json_diff
{
private:
    std::string printer_type;
    std::string printer_version = "00.00.00.00";
    json        settings_base;
    json        full_message;

    json diff2all_base;
    json all2diff_base;
    int  decode_error_count = 0;

    int  diff_objects(json const& in, json& out, json const& base);
    int  restore_objects(json const& in, json& out, json const& base);
    int  restore_append_objects(json const& in, json& out);
    void merge_objects(json const& in, json& out);

public:
    bool load_compatible_settings(std::string const& type, std::string const& version);
    int  all2diff(json const& in, json& out);
    int  diff2all(json const& in, json& out);
    int  all2diff_base_reset(json const& base);
    int  diff2all_base_reset(json& base);
    void compare_print(json& a, json& b);

    bool is_need_request();
};
#endif // __JSON_DIFF_HPP
