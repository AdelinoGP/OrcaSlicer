// [INTENT] Thin adapter that exposes OBJ color quantization to callers without
// exposing QuantKMeans internals. Performs K-means color clustering on RGBA
// inputs to reduce object color count to a manageable palette.
// [COUPLING] Entirely delegates to QuantKMeans (ObjColorUtils.hpp); no independent logic.
// [STATE] Stateless — all state lives inside QuantKMeans.
// [MEMORY] cluster_colors_from_algo and cluster_labels_from_algo are output-only;
// inputs are passed by reference but not modified beyond QuantKMeans::apply.
#include "ObjColorUtils.hpp"

bool obj_color_deal_algo(std::vector<Slic3r::RGBA>& input_colors,
                         std::vector<Slic3r::RGBA>& cluster_colors_from_algo,
                         std::vector<int>&          cluster_labels_from_algo,
                         char&                      cluster_number,
                         int                        max_cluster)
{
    QuantKMeans quant(10);
    quant.apply(input_colors, cluster_colors_from_algo, cluster_labels_from_algo, (int) cluster_number, max_cluster);
    if (cluster_number == -1) {
        return false;
    }
    return true;
}