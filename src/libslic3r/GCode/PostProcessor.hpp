// [INTENT] PostProcessor.hpp — interface for running user-defined post-processing
// scripts on the final G-code file, and for the BBS line-number insertion pass.
//
// [STATE] Stateless — all state is passed as arguments. The post-processing
// configuration (script list) is read from `DynamicPrintConfig::post_process`.
//
// [CONCURRENCY] Not thread-safe. Called once from the main slicing thread after
// G-code has been fully written to disk.
//
// run_post_process_scripts:
//   - If make_copy=true: creates a ".pp" copy of the G-code, runs scripts on the
//     copy, and updates src_path. The caller must delete the temp file.
//   - Sets print config as environment variables (SLIC3R_PP_*) before each script.
//   - Scripts communicate a renamed output path via a sidecar ".output_name" file.
//
// gcode_add_line_number:
//   - BBS extension: re-reads the entire G-code file and prepends "N<n>" line
//     numbers. Loads the full file into RAM as a std::string — [HAZARD H843].
//
// [HAZARD H843] gcode_add_line_number reads the entire G-code into `new_gcode`
// (std::string) before writing it back. For large prints (multi-GB G-code files)
// this will exhaust available RAM. Should use a streaming rename/replace instead.
//
// [HAZARD H844] run_post_process_scripts on Win32 uses CreateProcessW with
// INFINITE wait (WaitForSingleObject). If the script hangs, the slicer hangs with
// no timeout or cancellation path.
//
// [COUPLING] Called from GCode.cpp after export_gcode() completes. Modifies
// src_path in-place; callers must handle the case where src_path changed.

#ifndef slic3r_GCode_PostProcessor_hpp_
#define slic3r_GCode_PostProcessor_hpp_

#include <string>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"

namespace Slic3r {

// Run post processing script / scripts if defined.
// Returns true if a post-processing script was executed.
// Returns false if no post-processing script was defined.
// Throws an exception on error.
// host is one of "File", "PrusaLink", "Repetier", "SL1Host", "OctoPrint", "FlashAir", "Duet", "AstroBox" ...
// If make_copy, then a temp file will be created for src_path by adding a ".pp" suffix and src_path will be updated.
// In that case the caller is responsible to delete the temp file created.
// output_name is the final name of the G-code on SD card or when uploaded to PrusaLink or OctoPrint.
// If uploading to PrusaLink or OctoPrint, then the file will be renamed to output_name first on the target host.
// The post-processing script may change the output_name.
extern bool run_post_process_scripts(
    std::string& src_path, bool make_copy, const std::string& host, std::string& output_name, const DynamicPrintConfig& config);

inline bool run_post_process_scripts(std::string& src_path, const DynamicPrintConfig& config)
{
    std::string src_path_name = src_path;
    return run_post_process_scripts(src_path, false, "File", src_path_name, config);
}

// BBS
extern void gcode_add_line_number(const std::string& path, const DynamicPrintConfig& config);

} // namespace Slic3r

#endif /* slic3r_GCode_PostProcessor_hpp_ */
