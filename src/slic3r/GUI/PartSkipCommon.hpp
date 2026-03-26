// [INTENT] Definitions for part skipping state tracking in the GUI
// [UNITY] Use enum for part state, and List/Dictionary for parts info
// [SKIP_TRIVIAL] Very small file, essentially just a definition enum and typedef
#ifndef PARTSKIPCOMMON_H
#define PARTSKIPCOMMON_H

#include <vector>

namespace Slic3r { namespace GUI {

enum PartState { psUnCheck, psChecked, psSkipped };

typedef std::vector<std::pair<int, PartState>> PartsInfo;

}} // namespace Slic3r::GUI

#endif // PARTSKIPCOMMON_H
