///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#include "Settings.hpp"

namespace libvgcode {
// [INTENT][STATE] Empty namespace - Settings struct is header-only with inline default initializations
// [PORTING_HAZARD:Px] All member defaults defined in header - ensure Unity/C# mirrors default values
// [UNCLEAR] Settings struct has no methods, only data - unclear if runtime validation needed
} // namespace libvgcode
