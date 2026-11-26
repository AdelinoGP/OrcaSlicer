#include <catch2/catch_all.hpp>

#include "libslic3r/Utils.hpp"
#include "test_utils.hpp"

#include <fstream>

#ifndef _WIN32
#include <unistd.h>     // getuid
#endif

using namespace Slic3r;

TEST_CASE("per_user_temp_dir composes a per-user temp root", "[utils]") {
    const std::string base = "/tmp";

    SECTION("an empty id returns base unchanged") {
        REQUIRE(per_user_temp_dir(base, "") == base);
    }
    SECTION("a non-empty id is appended at the top level") {
        REQUIRE(per_user_temp_dir(base, "1000") == base + "/orcaslicer_1000");
    }
    SECTION("distinct ids produce distinct roots") {
        REQUIRE(per_user_temp_dir(base, "1000") != per_user_temp_dir(base, "1001"));
    }
}

TEST_CASE("per_user_temp_id follows the platform contract", "[utils]") {
    const std::string id = per_user_temp_id();

    SECTION("stable across calls") {
        REQUIRE(per_user_temp_id() == id);
    }
#ifdef _WIN32
    SECTION("empty on Windows (its temp dir is already per-user)") {
        REQUIRE(id.empty());
    }
#else
    SECTION("the current uid on Linux/macOS") {
        REQUIRE_FALSE(id.empty());
        REQUIRE(id == std::to_string(static_cast<unsigned long>(::getuid())));
    }
#endif
}

// The end-to-end contract callers depend on: the temp root is left alone on
// Windows and isolated per user on Linux/macOS.
TEST_CASE("per-user temp root is unchanged on Windows, isolated elsewhere", "[utils]") {
    const std::string base = "/tmp";
    const std::string root = per_user_temp_dir(base, per_user_temp_id());
#ifdef _WIN32
    REQUIRE(root == base);
#else
    REQUIRE(root != base);
    REQUIRE_THAT(root, Catch::Matchers::StartsWith(base + "/orcaslicer_"));
#endif
}

TEST_CASE("sanitize_file_basename keeps only a plain file name from an untrusted name", "[Utils]") {
    const std::string unicode = "\xe6\xa8\xa1\xe5\x9e\x8b \xc3\xa9t\xc3\xa9.3mf"; // UTF-8 CJK and accented Latin
    const auto [input, expected] = GENERATE_COPY(table<std::string, std::string>({
        {"normal.3mf", "normal.3mf"},
        {"../../x.3mf", "x.3mf"},
        {"..\\..\\x.3mf", "x.3mf"},
        {"C:\\x.3mf", "x.3mf"},
        {"C:x.3mf", "C_x.3mf"},
        {"/etc/x", "x"},
        {"a/b\\c.gcode", "c.gcode"},
        {"x:stream", "x_stream"}, // no NTFS alternate data stream
        {"x.", "x."},
        {".3mf", ".3mf"},
        {unicode, unicode},
    }));
    CAPTURE(input);
    CHECK(sanitize_file_basename(input) == expected);
}

TEST_CASE("sanitize_file_basename rejects names that do not name a file", "[Utils]") {
    const std::string input = GENERATE(as<std::string>{}, "", ".", "..", "../..", "dir/", "..\\", " ", ". .", "...");
    CAPTURE(input);
    CHECK(sanitize_file_basename(input).empty());
}

namespace {
void touch(const boost::filesystem::path &path) { std::ofstream(path.string()) << "existing"; }
std::string file_contents(const boost::filesystem::path &path)
{
    std::ifstream file(path.string());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
} // namespace

TEST_CASE("find_unused_filename keeps a name nothing uses", "[Utils]") {
    ScopedTemporaryDir dir;
    std::string name;
    REQUIRE(find_unused_filename(dir.path(), "model.3mf", {}, name));
    CHECK(name == "model.3mf");
}

TEST_CASE("find_unused_filename versions a name an existing file uses", "[Utils]") {
    ScopedTemporaryDir dir;
    touch(dir.path() / "model.3mf");
    std::string name;
    REQUIRE(find_unused_filename(dir.path(), "model.3mf", {}, name));
    CHECK(name == "model(1).3mf");
}

TEST_CASE("find_unused_filename versions a name that maps onto an existing file once sanitized", "[Utils]") {
    ScopedTemporaryDir dir;
    touch(dir.path() / "my_model.3mf");
    const std::string input = GENERATE(as<std::string>{}, "my?model.3mf", "my:model.3mf", "my*model.3mf");
    CAPTURE(input);
    std::string name;
    REQUIRE(find_unused_filename(dir.path(), input, {}, name));
    CHECK(name == "my_model(1).3mf");
    CHECK(file_contents(dir.path() / "my_model.3mf") == "existing");
}

TEST_CASE("find_unused_filename treats the marker of another download as used", "[Utils]") {
    ScopedTemporaryDir dir;
    touch(download_marker_path(dir.path(), "model.3mf"));
    std::string name;
    REQUIRE(find_unused_filename(dir.path(), "model.3mf", {}, name));
    CHECK(name == "model(1).3mf");
}

TEST_CASE("find_unused_filename ignores the marker of the download asking", "[Utils]") {
    ScopedTemporaryDir dir;
    const boost::filesystem::path own_marker = download_marker_path(dir.path(), "model.3mf");
    touch(own_marker);
    std::string name;
    REQUIRE(find_unused_filename(dir.path(), "model.3mf", own_marker, name));
    CHECK(name == "model.3mf");
}

TEST_CASE("find_unused_filename gives up after 999 versions", "[Utils]") {
    ScopedTemporaryDir dir;
    touch(dir.path() / "model.3mf");
    for (int version = 1; version < 999; ++version)
        touch(dir.path() / ("model(" + std::to_string(version) + ").3mf"));
    std::string name;
    REQUIRE(find_unused_filename(dir.path(), "model.3mf", {}, name));
    CHECK(name == "model(999).3mf");

    touch(dir.path() / name);
    REQUIRE_FALSE(find_unused_filename(dir.path(), "model.3mf", {}, name));
    CHECK(name == "model(999).3mf");
}
