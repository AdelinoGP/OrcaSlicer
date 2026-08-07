-- OrcaSlicer (pnp_gui) — Xmake build (see docs/adr/0001-xmake-conan-build-system.md)
-- Architecture:
--   Xmake  -> C++ target graph, source discovery, tests, staging, packaging
--   Conan  -> third-party C/C++ dependencies (add_requires("conan::..."))
--   Cargo  -> pinch_n_print_cli backend (invoked via on_build / task)
--
-- Migration status (per ADR-0001):
--   [x] wxWidgets 3.3.2 static proof (Windows x64, VS 2026, MSVC)
--   [x] repo conan profiles (C++17, static MSVC runtime) + lockfile (conan/)
--   [ ] remaining heavy deps via ConanCenter or repo recipes
--   [ ] full target graph port (libslic3r, libslic3r_gui, exe, shim, tests)
--   [ ] packaging parity (installers, portable dir)
-- CMake and deps/ remain authoritative until parity.

set_project("OrcaSlicer")
set_version("2.5.0-pnp")
set_languages("c++17")
set_default("libslic3r", "libslic3r_gui", "OrcaSlicer")

add_rules("mode.debug", "mode.release", "mode.releasedbg")

-- SLIC3R_STATIC=1 semantics on Windows: app and all deps share the static MSVC
-- runtime (/MT, /MTd in debug). Pending the full-app link test (handoff §8.2).
if is_plat("windows") then
    set_runtimes(is_mode("debug") and "MTd" or "MT")
end

-- ---------------------------------------------------------------- options

option("slic3r_static")
    set_default(true)
    set_description("Compile with static libraries (Boost, TBB, wxWidgets)")
option_end()

option("slic3r_gui")
    set_default(true)
    set_description("Compile with GUI components (OpenGL, wxWidgets)")
option_end()

option("pnp_bundle_cli")
    set_default(true)
    set_description("Stage and install the pnp_cli backend beside the OrcaSlicer executable")
option_end()

option("pnp_dist_dir")
    set_default("pinch_n_print_cli/target/dist")
    set_description("Staging directory produced by 'cargo xtask dist' (holds pnp_cli and modules/)")
option_end()

-- ------------------------------------------------------------ dependencies

-- ConanCenter-provided (proven or version-exact):
--   wxwidgets/3.3.2  -> proven end-to-end (static, webview, mediactrl, opengl, aui, html)
--   boost/1.84.0     -> exact match with deps/Boost
--   eigen/5.0.1      -> exact match with deps/Eigen
--   cereal/1.3.2     -> deps uses 1.3.0; 1.3.2 is the nearest upstream (accept per behavior gate)
--   draco/1.5.7      -> exact match with deps/Draco
--   qhull/8.0.2      -> exact match with deps/Qhull
--   glfw/3.4         -> exact match with deps/GLFW
--   cgal/5.6.3       -> exact match with deps/CGAL
--   libnoise/1.0.0   -> SoftFever fork tags 1.0; nearest upstream 1.0.0 (verify against fork)
--   zlib, libpng, libjpeg, expat -> transitively pulled by wxwidgets; explicit pins below

-- Custom recipes still required (in-repo, under conan/recipes/):
--   opencascade 7.6.0   (center has 7.9.1 only)
--   opencv 4.6.0        (center has 4.14.0 only)
--   openvdb 6.2.1-fork  (center has 12.1.1 only; project uses tamasmeszaros fork)
--   tbb 2021.5          (center tbb tops at 2020.3; onetbb has 2023.1.0)
--   openssl 1.1.1w      (center has 3.x only)
--   curl 7.75 / libcurl (center has 8.21.0)
--   freetype 2.12.1     (center has 2.13.2+)
--   opencsg 1.4.2       (no recipe)
--   openexr 2.5.5       (center has 2.5.7+)
--   nlopt 2.5.0         (center has 2.10.1+)

-- Shared Conan provisioning — KEEP IN SYNC with conan/profile_host.txt (that
-- profile is the same contract for direct conan workflows: lockfile, CI cache).
-- xmake generates its own per-package conan profiles (hardcoding cppstd=14 on
-- MSVC) and cannot consume external profiles/lockfiles, so the settings ride
-- along as CLI overrides on every require:
--   * compiler.cppstd=17       — the app is C++17; deps must match.
--   * runtimes MT/MTd (win)    — static MSVC runtime, SLIC3R_STATIC=1 semantics
--                                (xmake maps this to compiler.runtime=static).
--   * Ninja generator conf     — recipes pinning cmake<4 cannot drive the
--                                "Visual Studio 18 2026" generator (handoff §5.1).
local conan_base = {
    settings = {"compiler.cppstd=17"},
    conf = {"tools.cmake.cmaketoolchain:generator=Ninja"}
}
if is_plat("windows") then
    conan_base.runtimes = is_mode("debug") and "MTd" or "MT"
end

local function conan_configs(extra)
    local configs = {}
    for k, v in pairs(conan_base) do configs[k] = v end
    for k, v in pairs(extra or {}) do configs[k] = v end
    return configs
end

if is_plat("windows") then
    add_requires("conan::wxwidgets/3.3.2", {
        configs = conan_configs({
            options = {
                "shared=False",
                "webview=True", "mediactrl=True", "opengl=True",
                "aui=True", "html=True",
                "stc=False", "cairo=False",
                "custom_enables=wxUSE_PRIVATE_FONTS, wxUSE_GLCANVAS_EGL, wxUSE_WEBREQUEST, wxUSE_WEBVIEW_EDGE",
                "custom_disables=wxUSE_DETECT_SM, wxUSE_WEBVIEW_IE, wxUSE_LIBSDL, wxUSE_XTEST, wxUSE_LIBTIFF, wxUSE_NANOSVG, wxUSE_LIBWEBP"
            }
        })
    })
else
    add_requires("conan::wxwidgets/3.3.2", {
        configs = conan_configs({
            options = {
                "shared=False", "webview=True", "mediactrl=True", "opengl=True",
                "aui=True", "html=True", "stc=False"
            }
        })
    })
end

add_requires("conan::boost/1.84.0", {configs = conan_configs({options = {"header_only=True"}})})
add_requires("conan::eigen/5.0.1", {configs = conan_configs()})
add_requires("conan::cereal/1.3.2", {configs = conan_configs()})
add_requires("conan::draco/1.5.7", {configs = conan_configs()})
add_requires("conan::qhull/8.0.2", {configs = conan_configs()})
add_requires("conan::glfw/3.4", {configs = conan_configs()})
add_requires("conan::cgal/5.6.3", {configs = conan_configs()})
add_requires("conan::libnoise/1.0.0", {configs = conan_configs()})
add_requires("conan::zlib/1.3.1", {configs = conan_configs({options = {"shared=False"}})})
add_requires("conan::libpng/1.6.47", {configs = conan_configs({options = {"shared=False"}})})
add_requires("conan::libjpeg/9f", {configs = conan_configs({options = {"shared=False"}})})

-- TODO(custom recipes): opencascade/7.6.0, opencv/4.6.0, openvdb/6.2.1,
--   tbb/2021.5, openssl/1.1.1w, libcurl/7.75, freetype/2.12.1,
--   opencsg/1.4.2, openexr/2.5.5, nlopt/2.5.0
-- add_requires("conan::opencascade/7.6.0", {configs = {options = {"shared=False"}}})
-- add_requires("conan::opencv/4.6.0", {configs = {options = {"shared=False"}}})
-- add_requires("conan::openvdb/6.2.1", {configs = {options = {"shared=False"}}})
-- add_requires("conan::tbb/2021.5", {configs = {options = {"shared=False"}}})
-- add_requires("conan::openssl/1.1.1w", {configs = {options = {"shared=False"}}})
-- add_requires("conan::libcurl/7.75.0", {configs = {options = {"shared=False"}}})
-- add_requires("conan::freetype/2.12.1", {configs = {options = {"shared=False"}}})
-- add_requires("conan::opencsg/1.4.2", {configs = {options = {"shared=False"}}})
-- add_requires("conan::openexr/2.5.5", {configs = {options = {"shared=False"}}})
-- add_requires("conan::nlopt/2.5.0", {configs = {options = {"shared=False"}}})

-- ------------------------------------------------------- generated headers

-- libslic3r_version.h (mirrors src/libslic3r/CMakeLists.txt configure_file)
local version_vars = {
    SLIC3R_APP_NAME      = "OrcaSlicer",
    SLIC3R_APP_KEY       = "OrcaSlicer",
    SLIC3R_VERSION       = "02.06.00.51",
    ["SoftFever_VERSION"] = "2.5.0-pnp",
    SLIC3R_BUILD_ID      = os.getenv("SLIC3R_BUILD_ID") or "0",
    BBL_INTERNAL_TESTING = "0",
    ORCA_CHECK_GCODE_PLACEHOLDERS = "0"
}

-- GeneratedConfig.hpp (mirrors src/slic3r/CMakeLists.txt configure_file)
local generated_config_vars = {
    ORCA_UPDATER_SIG_KEY_B64 = os.getenv("ORCA_UPDATER_SIG_KEY") or "",
    ORCA_UPDATER_SIG_KEY_AVAILABLE = os.getenv("ORCA_UPDATER_SIG_KEY") and 1 or 0
}

-- GIT_COMMIT_HASH define (mirrors root CMakeLists.txt git detection).
-- Must run in script scope (rule on_load): description-scope Lua has no
-- os.iorunv/os.iorun, so command execution at root scope errors.
rule("git_commit_hash")
    on_load(function (target)
        local git_hash = os.getenv("git_commit_hash") or ""
        if git_hash == "" and os.isdir(".git") then
            local ok = try {function ()
                return os.iorunv("git", {"log", "-1", "--format=%h"})
            end}
            git_hash = (ok or ""):trim()
        end
        if git_hash ~= "" then
            target:add("defines", "GIT_COMMIT_HASH=\"" .. git_hash .. "\"")
        end
    end)
rule_end()
add_rules("git_commit_hash")

-- ---------------------------------------------------------------- helpers

-- add_files only takes compilable sources (headers error with "unknown source
-- file"); header visibility for IDEs would go through add_headerfiles.
-- The CMake build deliberately excludes a few orphan .cpp files that exist in
-- the tree (dead upstream code): ExPolygonCollection.cpp, JumpPointSearch.cpp,
-- Circle.cpp (top-level; Geometry/Circle.cpp is the live one) and
-- TryCatchSignalSEH.cpp (it is #included from TryCatchSignal.cpp on MSVC).
-- Keep the exclusion list (remove_files in the target) in sync with
-- src/libslic3r/CMakeLists.txt.

-- ------------------------------------------------------------- libslic3r

target("libslic3r")
    set_kind("static")
    set_warnings("all", "extra")
    set_pcxxheader("src/libslic3r/pchheader.hpp")
    add_files("src/libslic3r/**.cpp")
    remove_files(
        "src/libslic3r/Circle.cpp",
        "src/libslic3r/ExPolygonCollection.cpp",
        "src/libslic3r/JumpPointSearch.cpp",
        "src/libslic3r/TryCatchSignalSEH.cpp")
    add_includedirs("src", "src/libslic3r", {public = true})
    set_configdir("$(builddir)/config")
    add_configfiles("src/libslic3r/libslic3r_version.h.in",
        {filename = "libslic3r_version.h", pattern = "@(.-)@", variables = version_vars})
    add_includedirs("$(builddir)/config", {public = true})
    add_defines("USE_TBB", "TBB_USE_CAPTURED_EXCEPTION=0", {public = true})
    add_defines("SLIC3R_VERSION_IS_FORK", "PNP_FORK")

    -- heavy deps (custom recipes pending — see TODO above)
    add_packages("conan::boost/1.84.0", "conan::eigen/5.0.1", "conan::cereal/1.3.2",
        "conan::draco/1.5.7", "conan::qhull/8.0.2", "conan::cgal/5.6.3",
        "conan::zlib/1.3.1", "conan::libpng/1.6.47", "conan::libjpeg/9f")

    -- TODO: add_packages for opencascade, opencv, openvdb, tbb, openssl, libcurl,
    -- opencsg, openexr, nlopt once custom recipes land.

    if is_plat("windows") then
        add_syslinks("Psapi", "bcrypt", "ws2_32")
    elseif is_plat("macosx") then
        add_frameworks("Foundation", "ModelIO")
    end
target_end()

-- ----------------------------------------------------------- libslic3r_gui

target("libslic3r_gui")
    set_kind("static")
    set_warnings("all", "extra")
    set_options("slic3r_gui")
    set_pcxxheader("src/slic3r/pchheader.hpp")
    add_files(
        "src/slic3r/GUI/**.cpp",
        "src/slic3r/Config/**.cpp",
        "src/slic3r/Utils/**.cpp",
        "src/dev-utils/BaseException.cpp",
        "src/dev-utils/StackWalker.cpp")
    add_includedirs("src", "src/slic3r", "src/slic3r/GUI", {public = true})
    set_configdir("$(builddir)/config")
    add_configfiles("src/slic3r/GeneratedConfig.hpp.in",
        {filename = "GeneratedConfig.hpp", pattern = "@(.-)@", variables = generated_config_vars})
    add_includedirs("$(builddir)/config")
    add_defines("SLIC3R_CURRENTLY_COMPILING_GUI_MODULE", {private = true})
    add_defines("wxDEBUG_LEVEL=0", {public = true})
    add_packages("conan::wxwidgets/3.3.2", "conan::glfw/3.4")
    add_deps("libslic3r")
    if is_plat("windows") then
        add_includedirs("deps/WebView2/include", {public = true})
        add_syslinks("Advapi32", "Setupapi")
    end
target_end()

-- --------------------------------------------------------------- OrcaSlicer

target("OrcaSlicer")
    set_kind("binary")
    add_files("src/OrcaSlicer.cpp")
    add_deps("libslic3r", "libslic3r_gui")
    if is_plat("windows") then
        add_syslinks("ws2_32", "user32", "Setupapi")
        -- TODO(step 3): compile the generated OrcaSlicer.rc (icon/version
        -- resource) — add_files on the configfile output, not the .rc.in.
        set_configdir("$(builddir)/config")
        add_configfiles("src/dev-utils/platform/msw/OrcaSlicer.rc.in",
            {filename = "OrcaSlicer.rc", pattern = "@(.-)@",
             variables = table.join(version_vars,
                {SLIC3R_RESOURCES_DIR = path.join(os.projectdir(), "resources")})})
        add_ldflags("/MANIFEST:NO")
    elseif is_plat("macosx") then
        set_filename("OrcaSlicer")
        add_frameworks("OpenGL", "IOKit", "CoreFoundation", "AVFoundation", "AVKit", "CoreMedia", "VideoToolbox")
        add_ldflags("-liconv", "-lc++")
    else
        set_filename("orca-slicer")
        add_ldflags("-ldl")
    end
    if has_config("pnp_bundle_cli") then
        local dist_dir = get_config("pnp_dist_dir") or "pinch_n_print_cli/target/dist"
        local cli_name = is_plat("windows") and "pnp_cli.exe" or "pnp_cli"
        after_build(function (target)
            local bin_dir = path.directory(target:targetfile())
            os.cp(path.join(dist_dir, cli_name), path.join(bin_dir, cli_name))
            os.cp(path.join(dist_dir, "modules"), path.join(bin_dir, "modules"))
        end)
    end
target_end()

-- ---------------------------------------------------------- pnp backend task

-- xmake pnp -> cargo xtask dist in pinch_n_print_cli
task("pnp")
    on_run(function ()
        import("core.base.option")
        local workdir = "pinch_n_print_cli"
        local debug = option.get("debug") and " --debug" or ""
        os.cd(workdir)
        os.exec("cargo xtask dist" .. debug)
        os.cd("-")
    end)
    set_menu {
        usage = "xmake pnp [options]",
        description = "Build the PNP backend (cargo xtask dist)",
        options = {
            {'d', "debug", "k", nil, "Build the backend in debug mode"}
        }
    }
task_end()

-- ---------------------------------------------------------------- test task

-- xmake test -> ctest against the CMake test tree (until the Xmake test port lands)
task("test")
    on_run(function ()
        os.exec("ctest --output-on-failure")
    end)
task_end()
