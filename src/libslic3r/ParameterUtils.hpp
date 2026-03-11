#ifndef slic3r_Parameter_Utils_hpp_
#define slic3r_Parameter_Utils_hpp_

#include <vector>
#include <map>
#include "PrintConfig.hpp"

namespace Slic3r {
// [INTENT] Encodes one user-defined layer-sequence rule as ((start_layer, end_layer), extruder_order).
// [COUPLING] This compact tuple form is shared with PrintConfig serialization/UI plumbing, so field order is contract-sensitive.
using LayerPrintSequence = std::pair<std::pair<int, int>, std::vector<int>>;
// [INTENT] Expand serialized/custom rules into normalized per-layer print sequence data.
std::vector<LayerPrintSequence> get_other_layers_print_sequence(int sequence_nums, const std::vector<int>& sequence);
// [INTENT] Collapse normalized rule objects back to legacy scalar/vector parameters used by profile storage.
void get_other_layers_print_sequence(const std::vector<LayerPrintSequence>& customize_sequences,
                                     int&                                   sequence_nums,
                                     std::vector<int>&                      sequence);

// [COUPLING] Reads DynamicPrintConfig option arrays through PrintConfig option naming conventions and extruder-type enums.
// [HAZARD] Wrong opt_key/ExtruderType combination can silently select unintended index if config shape differs across profiles.
extern int get_index_for_extruder_parameter(const DynamicPrintConfig& config,
                                            const std::string&        opt_key,
                                            int                       cur_extruder_id,
                                            ExtruderType              extruder_type,
                                            NozzleVolumeType          nozzle_volume_type);
} // namespace Slic3r

#endif // slic3r_Parameter_Utils_hpp_
