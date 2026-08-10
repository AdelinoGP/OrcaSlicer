// PNP fork: regression test for the temp-dir base of Model::get_backup_path().
//
// Model::get_backup_path() (and through it the plate's temp G-code path and
// store_bbs_3mf's _temp_3.config staging) resolves against temporary_dir().
// When no explicit temp dir was set, the empty parent produced
// "/orcaslicer_model/..." which Windows resolves to the root of the current
// drive (F:\orcaslicer_model\...) — visible after running the test binaries,
// which never call set_temporary_dir(). The GUI sets the temp dir at startup,
// so this only bites consumers that do not, which is exactly what this test
// binary is. The seam under test is temporary_dir() itself: it must never
// hand out an empty parent.
//
// (Deliberately does not construct a Model: Model's transitive link closure
// pulls EmbossShape -> NSVGUtils, whose nanosvg implementation is emitted in
// the GUI target only, so any Model-referencing test cannot link against
// plain libslic3r.)

#include <catch2/catch_test_macros.hpp>

#include <boost/filesystem.hpp>

#include "libslic3r/Utils.hpp"

using namespace Slic3r;

TEST_CASE("temporary_dir falls back to the system temp dir when unset", "[pnp][backup-path]")
{
    // Make the test hermetic: clear whatever temp dir earlier tests may have set.
    set_temporary_dir("");

    const std::string temp = boost::filesystem::temp_directory_path().string();
    REQUIRE_FALSE(temp.empty());

    // The buggy behaviour returned an empty parent, so get_backup_path()
    // built a drive-root-relative "/orcaslicer_model/..." path.
    REQUIRE(temporary_dir() == temp);

    // And a subsequent explicit set still wins (the GUI's startup path).
    set_temporary_dir(temp + "/orcaslicer_custom");
    REQUIRE(temporary_dir() == temp + "/orcaslicer_custom");

    set_temporary_dir("");
    REQUIRE(temporary_dir() == temp);
}
