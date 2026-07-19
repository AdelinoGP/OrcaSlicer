#include "calib.hpp"

// F11 (fork ticket, native-slicing rip-out §2): the calibration G-code
// generators (CalibPressureAdvance / CalibPressureAdvanceLine /
// CalibPressureAdvancePattern and their helpers) were deleted along with the
// native slicing pipeline. The calibration UI survives as a mock; the type
// block it depends on lives in calib.hpp. This translation unit is retained
// (still listed in libslic3r/CMakeLists.txt) but is intentionally empty of
// generator code.

namespace Slic3r {

} // namespace Slic3r
