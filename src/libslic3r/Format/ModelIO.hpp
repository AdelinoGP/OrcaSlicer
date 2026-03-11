#include <string>

namespace Slic3r {
// [INTENT] Apple-only compatibility shim for CAD / mesh formats that ModelIO can decode but Orca's core
// loaders cannot. The bridge converts them to a temporary STL so the rest of the pipeline stays unchanged.
// [COUPLING] `Model.cpp` owns the policy of when to fall back to this bridge and is responsible for deleting
// the temporary artifact after the regular STL loader consumes it.
/**
 * Uses ModelIO to convert supported model types to a temporary STL
 * that can then be consumed by the existing STL loader
 * @param input_file The File to load
 * @return Path to the temporary file, or an empty string if conversion failed
 */
std::string make_temp_stl_with_modelio(const std::string& input_file);

/**
 * Convenience function to delete the file.
 * No return value since success isn't required
 * @param temp_file File path to delete
 */
void delete_temp_file(const std::string& temp_file);
} // namespace Slic3r
