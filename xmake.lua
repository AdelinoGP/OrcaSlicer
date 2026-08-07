-- OrcaSlicer (pnp_gui) — Xmake build (see docs/adr/0001-xmake-conan-build-system.md)
-- Architecture:
--   Xmake  -> C++ target graph, source discovery, tests, staging, packaging
--   Conan  -> third-party C/C++ deps, ONE consolidated lockfile-enforced graph
--             (conan/conanfile.py + rule "pnp.conan" below; ADR-0001 amendment)
--   Cargo  -> pinch_n_print_cli backend (invoked via on_build / task)
--
-- Migration status (per ADR-0001):
--   [x] wxWidgets 3.3.2 static proof (Windows x64, VS 2026, MSVC)
--   [x] repo conan profiles (C++17, /MD parity) + enforced lockfile (conan/)
--   [x] full dependency set: ConanCenter + two repo recipes (opencascade
--       minus tcl/Draw, wxwidgets deltas); dead SLA-era deps dropped
--       (openvdb, opencsg, openexr, glew)
--   [x] target graph port: libslic3r, libslic3r_gui, OrcaSlicer.dll +
--       orca-slicer.exe launcher — GUI launches on Windows x64
--   [x] tests: 7 Catch2 suites via `xmake test` (all passing)
--   [ ] macOS/Linux ports
--   [x] portable package dir (`xmake package`)
--   [x] Windows NSIS installer (`xmake pack -f nsis`) — verified: silent
--       install, 64-bit registry view, launch, upgrade-in-place, uninstall
--   [ ] macOS/Linux installers, then CMake cutover
-- CMake and deps/ remain authoritative until parity.
--
-- Build prerequisite for `xmake pack -f nsis`: NSIS *with the UAC plugin*.
-- xmake's makensis probe (plugins/pack/nsis/main.lua:31-56) compiles a script
-- that !includes UAC.nsh, so a stock NSIS install is rejected even though the
-- specfile here deliberately does not use UAC. See docs/BUILD_SYSTEM_HANDOFF.md.

-- xpack() (installer packaging) is a builtin include, not a core interface
includes("@builtin/xpack")
-- shared shipping-layout module, imported as pnp.layout by the package task
-- and the installer
add_moduledirs("xmake/modules")

set_project("OrcaSlicer")
set_version("2.5.0-pnp")
-- NOTE: not c99 — xmake compiles .c files as C++ (-TP) on MSVC for c99
-- (no /std:c99 exists), which breaks K&R sources like mcut's shewchuk.c.
set_languages("c11", "c++17")
-- Windows builds the launcher (which pulls OrcaSlicer.dll via add_deps)
if is_plat("windows") then
    set_default("OrcaSlicer_app_gui")
else
    set_default("libslic3r", "libslic3r_gui", "OrcaSlicer")
end

add_rules("mode.debug", "mode.release", "mode.releasedbg")

-- Dynamic MSVC runtime (/MD, /MDd in debug) — parity with the authoritative
-- CMake build (it never overrides the runtime; SLIC3R_STATIC means static
-- *libraries*, not static CRT). Keep in sync with conan/profile_host.txt.
if is_plat("windows") then
    set_runtimes(is_mode("debug") and "MDd" or "MD")
end

-- Global compile definitions (mirrors root CMakeLists.txt lines 81-560)
add_defines("BBL_RELEASE_TO_PUBLIC=" .. (is_mode("debug") and "0" or "1"))
add_defines("wxUSE_UNICODE", "_UNICODE", "UNICODE", "WXINTL_NO_GETTEXT_MACRO",
    "wxNO_UNSAFE_WXSTRING_CONV")
if is_plat("windows") then
    -- WIN32/_WINDOWS: CMake's MSVC default flags define them; sources use
    -- `#if WIN32` (C1017 when the macro is missing/empty)
    add_defines("WIN32", "_WINDOWS",
        "_USE_MATH_DEFINES", "_CRT_SECURE_NO_WARNINGS", "_SCL_SECURE_NO_WARNINGS",
        "BOOST_ALL_NO_LIB", "BOOST_USE_WINAPI_VERSION=0x602", "BOOST_SYSTEM_USE_UTF8")
    -- force UTF-8 source/exec charset (upstream GH PR #5583); /bigobj for
    -- heavy template TUs (MeshBoolean, GUI) — upstream sets it per-target,
    -- global here is harmless
    add_cxflags("/utf-8")
    add_cxxflags("/bigobj")
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

if has_config("slic3r_gui") then
    add_defines("SLIC3R_GUI")
end

-- ------------------------------------------------------------ dependencies
--
-- All third-party C/C++ dependencies come from ONE consolidated conan install
-- of conan/conanfile.py (single coherent graph, lockfile-enforced, profile
-- settings from conan/profile_host.txt). This replaces xmake's stock
-- per-package `add_requires("conan::...")` integration, whose isolated graphs
-- resolved conflicting transitive versions (occt→freetype/2.13.2 beside the
-- project's 2.12.1, cgal→boost/1.83.0+eigen/3.4.0, libcurl→openssl/3.x) and
-- could not consume the lockfile. See ADR-0001 amendment + conan/README.md.
--
-- Targets declare their direct conan packages via
--     set_values("pnp.conan.packages", "boost", "wxwidgets", ...)
-- and the rule injects flags for the transitive closure (public, so they
-- propagate through add_deps to the final binary).

rule("pnp.conan")
    on_load(function (target)
        import("core.project.config")

        local projectdir = os.projectdir()
        local key = (config.plat() or os.host()) .. "_" .. (config.arch() or os.arch())
            .. "_" .. (config.mode() or "release")
        local outdir = path.join(config.builddir(), "conan", key)
        local deps_file = path.join(outdir, "pnp_deps.lua")

        -- The checked-in profiles are Release; a debug dependency graph needs
        -- its own host profile (MDd deps) before xmake debug mode is usable.
        if config.mode() == "debug" then
            wprint("pnp.conan: conan/profile_host.txt builds Release deps; " ..
                   "debug app + release deps will fail to link on MSVC (TODO: debug profile)")
        end

        -- rerun the install when its inputs changed (cheap no-op otherwise)
        local inputs = {
            path.join(projectdir, "conan", "conanfile.py"),
            path.join(projectdir, "conan", "profile_host.txt"),
            path.join(projectdir, "conan", "profile_build.txt"),
            path.join(projectdir, "conan", "conan.lock")
        }
        local recipe_dirs = os.dirs(path.join(projectdir, "conan", "recipes", "*"))
        for _, dir in ipairs(recipe_dirs) do
            table.insert(inputs, path.join(dir, "conanfile.py"))
        end
        local stale = not os.isfile(deps_file)
        if not stale then
            local outmtime = os.mtime(deps_file)
            for _, f in ipairs(inputs) do
                if os.isfile(f) and os.mtime(f) > outmtime then
                    stale = true
                    break
                end
            end
        end
        if stale and not _g.pnp_conan_installed then
            _g.pnp_conan_installed = true
            -- repo-owned recipes (versions pinned in-recipe) must be exported
            -- into the conan cache before the graph resolves
            for _, dir in ipairs(recipe_dirs) do
                print("pnp.conan: conan export %s ...", path.filename(dir))
                os.vrunv("conan", {"export", dir})
            end
            print("pnp.conan: conan install (consolidated graph, --build=missing) ...")
            os.vrunv("conan", {"install", path.join(projectdir, "conan", "conanfile.py"),
                "--profile:host=" .. path.join(projectdir, "conan", "profile_host.txt"),
                "--profile:build=" .. path.join(projectdir, "conan", "profile_build.txt"),
                "--lockfile=" .. path.join(projectdir, "conan", "conan.lock"),
                "--build=missing",
                "-of", outdir})
        end

        local wanted = target:values("pnp.conan.packages")
        if wanted == nil then
            return
        end
        local graph = assert(io.load(deps_file), "pnp.conan: cannot load %s", deps_file)

        -- transitive closure, deps-first; injected in reverse (dependents
        -- before dependencies) for GNU-ld-friendly link order
        local visited, order = {}, {}
        local function visit(name)
            if visited[name] then
                return
            end
            visited[name] = true
            local pkg = graph[name]
            assert(pkg, "pnp.conan: package '%s' not in the conan graph (%s)", name, deps_file)
            for _, dep in ipairs(pkg.deps or {}) do
                visit(dep)
            end
            table.insert(order, name)
        end
        for _, name in ipairs(table.wrap(wanted)) do
            visit(name)
        end
        for i = #order, 1, -1 do
            local pkg = graph[order[i]]
            for _, dir in ipairs(pkg.includedirs) do
                target:add("sysincludedirs", dir, {public = true})
            end
            for _, dir in ipairs(pkg.linkdirs) do
                target:add("linkdirs", dir, {public = true})
            end
            for _, lib in ipairs(pkg.links) do
                target:add("links", lib, {public = true})
            end
            for _, lib in ipairs(pkg.syslinks) do
                target:add("syslinks", lib, {public = true})
            end
            for _, def in ipairs(pkg.defines) do
                target:add("defines", def, {public = true})
            end
            for _, flag in ipairs(pkg.cxxflags) do
                target:add("cxxflags", flag)
            end
            for _, flag in ipairs(pkg.ldflags) do
                target:add("ldflags", flag, {public = true})
            end
            if target:is_plat("macosx") then
                for _, dir in ipairs(pkg.frameworkdirs) do
                    target:add("frameworkdirs", dir, {public = true})
                end
                for _, fw in ipairs(pkg.frameworks) do
                    target:add("frameworks", fw, {public = true})
                end
            end
        end
    end)
rule_end()

-- copies runtime DLLs of shared conan packages (onetbb, hwloc) beside the exe
rule("pnp.conan.dlls")
    after_build(function (target)
        import("core.project.config")
        if not target:is_plat("windows") then
            return
        end
        local key = config.plat() .. "_" .. config.arch() .. "_" .. (config.mode() or "release")
        local deps_file = path.join(config.builddir(), "conan", key, "pnp_deps.lua")
        local graph = io.load(deps_file)
        if not graph then
            return
        end
        local bin_dir = path.directory(target:targetfile())
        for name, pkg in pairs(graph) do
            if not name:startswith("__") then
                for _, dir in ipairs(pkg.bindirs or {}) do
                    for _, dll in ipairs(os.files(path.join(dir, "*.dll"))) do
                        os.cp(dll, path.join(bin_dir, path.filename(dll)))
                    end
                end
            end
        end
    end)
rule_end()

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

-- ------------------------------------------- vendored libraries (deps_src/)
-- Static ports of deps_src/<lib>/CMakeLists.txt. Public include dirs mirror
-- each library's exported interface; deps_src itself is exported by admesh
-- (dir-qualified includes like "admesh/stl.h", "clipper/clipper_z.hpp").

target("admesh")
    set_kind("static")
    add_files("deps_src/admesh/*.cpp")
    add_sysincludedirs("deps_src/admesh", "deps_src", {public = true})
    -- admesh sources include libslic3r/ headers (LocalesUtils.hpp)
    add_includedirs("src")
    add_rules("pnp.conan")
    set_values("pnp.conan.packages", "boost", "eigen")
target_end()

target("clipper")
    set_kind("static")
    -- clipper.cpp is deliberately absent: ClipperLib is compiled as part of
    -- libslic3r using Slic3r::Point as its base type (see deps_src/clipper)
    add_files("deps_src/clipper/clipper_z.cpp")
    add_sysincludedirs("deps_src/clipper", {public = true})
    -- clipper_z.cpp pulls in clipper.cpp, which includes libslic3r/Int128.hpp
    add_includedirs("src")
    add_rules("pnp.conan")
    set_values("pnp.conan.packages", "eigen", "onetbb")
target_end()

target("Clipper2")
    set_kind("static")
    add_files("deps_src/clipper2/Clipper2Lib/src/*.cpp")
    add_sysincludedirs("deps_src/clipper2/Clipper2Lib/include", {public = true})
target_end()

target("glu-libtess")
    set_kind("static")
    add_files("deps_src/glu-libtess/src/*.c")
    add_includedirs("deps_src/glu-libtess/src")
    add_sysincludedirs("deps_src/glu-libtess/include", {public = true})
target_end()

target("mcut")
    set_kind("static")
    add_files("deps_src/mcut/source/*.cpp", "deps_src/mcut/source/*.c")
    add_sysincludedirs("deps_src/mcut/include", {public = true})
    add_defines("MCUT_WITH_COMPUTE_HELPER_THREADPOOL=1")
    if is_plat("windows") then
        add_defines("_CRT_SECURE_NO_WARNINGS")
        add_cxxflags("/bigobj")
    end
target_end()

target("miniz")
    set_kind("static")
    add_files("deps_src/miniz/miniz.c")
    add_sysincludedirs("deps_src/miniz", {public = true})
target_end()

target("qoi")
    set_kind("static")
    add_files("deps_src/qoi/qoilib.c")
    add_sysincludedirs("deps_src/qoi", {public = true})
target_end()

target("semver")
    set_kind("static")
    add_files("deps_src/semver/semver.c")
    add_sysincludedirs("deps_src/semver", {public = true})
target_end()

target("glad")
    set_kind("static")
    add_files("src/glad/src/gl.c")
    add_sysincludedirs("src/glad/include", {public = true})
target_end()

target("libvgcode")
    set_kind("static")
    add_files("src/libvgcode/src/**.cpp")
    add_includedirs("src/libvgcode/src")
    add_sysincludedirs("src/libvgcode/include", "src/libvgcode", "src", {public = true})
    add_deps("glad")
target_end()

target("imgui")
    set_kind("static")
    add_files("deps_src/imgui/*.cpp")
    -- both "imgui.h" and "imgui/imgui.h" include styles are used
    add_sysincludedirs("deps_src/imgui", "deps_src", {public = true})
    add_rules("pnp.conan")
    set_values("pnp.conan.packages", "boost")
target_end()

target("imguizmo")
    set_kind("static")
    add_files("deps_src/imguizmo/ImGuizmo.cpp")
    add_sysincludedirs("deps_src/imguizmo", {public = true})
    add_deps("imgui")
target_end()

target("hidapi")
    set_kind("static")
    if is_plat("windows") then
        add_files("deps_src/hidapi/win/hid.c")
    elseif is_plat("macosx") then
        add_files("deps_src/hidapi/mac/hid.c")
    else
        add_files("deps_src/hidapi/linux/hid.c")
    end
    add_includedirs("deps_src/hidapi")
    add_sysincludedirs("deps_src/hidapi/include", {public = true})
target_end()

target("mdns")
    set_kind("static")
    add_files("deps_src/mdns/mdns.c", "deps_src/mdns/cxmdns.cpp")
    add_sysincludedirs("deps_src/mdns", {public = true})
    if is_plat("windows") then
        add_syslinks("Iphlpapi", "Ws2_32", {public = true})
    end
target_end()

target("minilzo")
    set_kind("static")
    add_files("deps_src/minilzo/*.c")
    add_sysincludedirs("deps_src/minilzo", {public = true})
target_end()

target("md4c")
    set_kind("static")
    add_files("deps_src/md4c/src/*.c")
    add_sysincludedirs("deps_src/md4c/src", {public = true})
target_end()

-- ------------------------------------------------------- libslic3r_cgal
-- CGAL-using compilation units isolated so CGAL's rounding-math requirements
-- do not propagate to the rest of libslic3r (mirrors src/libslic3r/CMakeLists
-- lines 343-371).

target("libslic3r_cgal")
    set_kind("static")
    add_files(
        "src/libslic3r/CutSurface.cpp",
        "src/libslic3r/IntersectionPoints.cpp",
        "src/libslic3r/MeshBoolean.cpp",
        "src/libslic3r/TryCatchSignal.cpp",
        "src/libslic3r/Triangulation.cpp")
    add_includedirs("src", "src/libslic3r")
    add_includedirs("$(builddir)/config")
    add_sysincludedirs("deps_src", "deps_src/libigl", "deps_src/mcut/include")
    add_defines("USE_TBB", "TBB_USE_CAPTURED_EXCEPTION=0", "NOMINMAX")
    add_rules("pnp.conan")
    -- same package surface as libslic3r: its headers (EmbossShape, Point, ...)
    -- pull cereal etc. transitively
    set_values("pnp.conan.packages",
        "cgal", "boost", "eigen", "cereal", "onetbb",
        "zlib", "libpng", "opencascade", "opencv", "nlopt", "openssl")
    if not is_plat("windows") then
        add_cxxflags("-frounding-math")
    end
    -- NOTE (divergence from upstream): CGAL_DO_NOT_USE_MPZF is NOT defined on
    -- MSVC. With it, CGAL's exact type becomes Quotient<Gmpzf>, whose
    -- boost::operators mixed comparisons hit the cl>=19.40 C2666 regression
    -- (boost 1.84). Without it CGAL uses Mpzf, which compiles fine on
    -- VS 18 2026. Behavior gate: exact-arithmetic results identical by
    -- construction; watch mesh-boolean tests.
target_end()

-- ------------------------------------------------------------- libslic3r

target("libslic3r")
    set_kind("static")
    set_warnings("all", "extra")
    set_pcxxheader("src/libslic3r/pchheader.hpp")
    add_files("src/libslic3r/**.cpp")
    remove_files(
        -- NOTE: src/libslic3r/Circle.cpp is LIVE (CMakeLists.txt:52), even
        -- though Geometry/Circle.cpp also exists (line 148). It defines
        -- Slic3r::ArcSegment. Do not exclude it.
        "src/libslic3r/ExPolygonCollection.cpp",
        "src/libslic3r/JumpPointSearch.cpp",
        "src/libslic3r/TryCatchSignalSEH.cpp",
        -- OpenVDB is not provisioned: its only consumer (VoxelizeCSGMesh.hpp,
        -- SLA-era) has no callers in this fork; CMake compiles OpenVDBUtils.cpp
        -- only `if (TARGET OpenVDB::openvdb)`.
        "src/libslic3r/OpenVDBUtils.cpp",
        -- commented out in src/libslic3r/CMakeLists.txt (pre-boost-1.70 asio)
        "src/libslic3r/GCodeSender.cpp",
        -- compiled in libslic3r_cgal instead (rounding-math isolation)
        "src/libslic3r/CutSurface.cpp",
        "src/libslic3r/IntersectionPoints.cpp",
        "src/libslic3r/MeshBoolean.cpp",
        "src/libslic3r/TryCatchSignal.cpp",
        "src/libslic3r/Triangulation.cpp")
    -- libnest2d is a single translation unit whose CMake target links back to
    -- libslic3r (cyclic for xmake add_deps); compile it into libslic3r instead
    add_files("deps_src/libnest2d/src/libnest2d.cpp")
    add_sysincludedirs("deps_src/libnest2d/include", {public = true})
    add_defines("LIBNEST2D_THREADING_tbb", "LIBNEST2D_STATIC",
        "LIBNEST2D_OPTIMIZER_nlopt", "LIBNEST2D_GEOMETRIES_libslic3r", {public = true})
    add_includedirs("src", "src/libslic3r", {public = true})
    -- header-only vendored interfaces (libigl exports NOMINMAX on MSVC)
    add_sysincludedirs("deps_src/libigl", {public = true})
    if is_plat("windows") then
        add_defines("NOMINMAX", {public = true})
    end
    set_configdir("$(builddir)/config")
    add_configfiles("src/libslic3r/libslic3r_version.h.in",
        {filename = "libslic3r_version.h", pattern = "@(.-)@", variables = version_vars})
    add_includedirs("$(builddir)/config", {public = true})
    add_defines("USE_TBB", "TBB_USE_CAPTURED_EXCEPTION=0", {public = true})
    add_defines("SLIC3R_VERSION_IS_FORK", "PNP_FORK")

    -- vendored static deps (public include dirs propagate from each target)
    add_deps("admesh", "clipper", "Clipper2", "glu-libtess", "mcut",
        "miniz", "qoi", "semver", "libslic3r_cgal")

    -- direct conan deps (mirrors src/libslic3r/CMakeLists.txt link list;
    -- freetype/tcl arrive via the opencascade closure)
    add_rules("pnp.conan")
    set_values("pnp.conan.packages",
        "boost", "eigen", "cereal", "draco", "qhull", "cgal", "libnoise",
        "zlib", "libpng", "libjpeg-turbo", "expat",
        "opencascade", "opencv", "onetbb", "nlopt", "openssl")

    if is_plat("windows") then
        -- Synchronization: WaitOnAddress/WakeByAddressSingle, used by
        -- boost::log's atomic_based_event under BOOST_USE_WINAPI_VERSION=0x602.
        -- Conan's boost recipe does not declare it in system_libs.
        add_syslinks("Psapi", "bcrypt", "ws2_32", "Synchronization")
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
    -- Dead upstream code: in the tree but in no CMakeLists. GLGizmoFaceDetector
    -- and GLGizmoText are explicitly commented out in src/slic3r/CMakeLists.txt
    -- (lines 141, 179); the other three appear nowhere.
    -- NOTE: GUI/DeviceCore/** and GUI/DeviceTab/** look absent from
    -- src/slic3r/CMakeLists.txt but are LIVE — their own CMakeLists append to
    -- SLIC3R_GUI_SOURCES via add_subdirectory (lines 699-700). Do not exclude.
    remove_files(
        "src/slic3r/GUI/Gizmos/GLGizmoAdvancedCut.cpp",
        "src/slic3r/GUI/Gizmos/GLGizmoFaceDetector.cpp",
        "src/slic3r/GUI/Gizmos/GLGizmoText.cpp",
        "src/slic3r/GUI/SysInfoDialog.cpp",
        "src/slic3r/GUI/WebUpdatePlugin.cpp")
    add_includedirs("src", "src/slic3r", "src/slic3r/GUI", {public = true})
    add_includedirs("src/slic3r/Utils")  -- CMake: target_include_directories(... PRIVATE Utils)
    set_configdir("$(builddir)/config")
    add_configfiles("src/slic3r/GeneratedConfig.hpp.in",
        {filename = "GeneratedConfig.hpp", pattern = "@(.-)@", variables = generated_config_vars})
    add_includedirs("$(builddir)/config")
    add_defines("SLIC3R_CURRENTLY_COMPILING_GUI_MODULE", {private = true})
    add_defines("wxDEBUG_LEVEL=0", {public = true})
    -- header-only vendored interfaces (deps_src root comes via libslic3r).
    -- NOT deps_src/agg: its VERSION file shadows the C++20 <version> header
    -- on case-insensitive filesystems, and agg's only user is removed SLA code.
    add_sysincludedirs("deps_src/nlohmann", "deps_src/earcut",
        "deps_src/fast_float", "deps_src/ankerl", "deps_src/stb_dxt",
        "deps_src/hints")
    -- direct conan deps (libslic3r's public set propagates via add_deps)
    add_rules("pnp.conan")
    set_values("pnp.conan.packages",
        "wxwidgets", "glfw", "libcurl", "opencv", "onetbb", "boost", "expat")
    add_deps("libslic3r")
    add_deps("glad", "libvgcode", "imgui", "imguizmo", "hidapi", "mdns",
        "minilzo", "md4c")
    if is_plat("windows") then
        add_includedirs("deps/WebView2/include", {public = true})
        add_syslinks("Advapi32", "Setupapi", "opengl32")
    end
target_end()

-- --------------------------------------------------------------- OrcaSlicer

-- On Windows the app is split exactly as src/CMakeLists.txt does it:
--   OrcaSlicer        -> OrcaSlicer.dll (SHARED), exports slic3r_main
--   OrcaSlicer_app_gui-> orca-slicer.exe (WIN32 subsystem), LoadLibrary's the
--                        dll after an OpenGL version probe
-- Everywhere else OrcaSlicer is the executable itself.
target("OrcaSlicer")
    set_kind(is_plat("windows") and "shared" or "binary")
    add_files("src/OrcaSlicer.cpp")
    add_deps("libslic3r", "libslic3r_gui")
    add_rules("pnp.conan", "pnp.conan.dlls")
    -- unix/fhs.hpp: CMake configure_file from dev-utils/platform/unix/fhs.hpp.in
    -- (#cmakedefine template, which add_configfiles cannot render). Portable
    -- layout everywhere for now: SLIC3R_FHS undefined, resources path empty.
    -- TODO(linux packaging): honor an fhs option here.
    on_load(function (target)
        import("core.project.config")
        local out = path.join(config.builddir(), "config", "unix", "fhs.hpp")
        if not os.isfile(out) then
            io.writefile(out,
                "/* generated by xmake; portable layout (no FHS) */\n" ..
                "#define SLIC3R_FHS_RESOURCES \"\"\n")
        end
    end)
    if is_plat("windows") then
        add_syslinks("ws2_32", "user32", "Setupapi")
        add_ldflags("/MANIFEST:NO")  -- the manifest ships via OrcaSlicer.rc
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

    -- The GUI resolves resources/ relative to the executable, so the build
    -- tree must reproduce it (CMake makes a junction/symlink;
    -- src/CMakeLists.txt:180-260). A directory symlink needs no elevation
    -- when Developer Mode is on; fall back to a copy.
    after_build(function (target)
        local bin_dir = path.directory(target:targetfile())
        local dest = path.join(bin_dir, "resources")
        if os.exists(dest) then
            return
        end
        local src = path.join(os.projectdir(), "resources")
        local ok = try {function () os.ln(src, dest); return true end}
        if not ok then
            os.cp(src, dest)
        end
    end)
target_end()

-- ------------------------------------------------- OrcaSlicer_app_gui (win)

-- WIN32 launcher: probes the OpenGL version, then LoadLibrary's OrcaSlicer.dll
-- and calls slic3r_main (mirrors src/CMakeLists.txt:161-178).
if is_plat("windows") then
target("OrcaSlicer_app_gui")
    set_kind("binary")
    set_basename("orca-slicer")
    add_files("src/OrcaSlicer_app_msvc.cpp")
    add_deps("OrcaSlicer")
    add_rules("pnp.conan")
    set_values("pnp.conan.packages", "boost")
    -- set_values keeps a flat string list, not a map, so pass KEY=VALUE
    -- pairs and reassemble them in on_load
    for k, v in pairs(version_vars) do
        add_values("pnp.rc.vars", k .. "=" .. tostring(v))
    end
    -- The .rc carries the version block, icon and the manifest reference
    -- (`1 24 "OrcaSlicer.manifest"`), so both must land in the same directory.
    --
    -- Rendered in on_load (script scope) rather than with add_configfiles
    -- because add_files() resolves at load time: a generated file that does
    -- not exist yet is silently skipped, and the exe then links with no
    -- manifest (wx pops a "Common Controls v6" warning at startup) and no
    -- icon. Script scope also gives us os.mkdir, which description scope
    -- lacks. target:add("files", ...) registers the .rc after rendering it.
    on_load(function (target)
        import("core.project.config")
        local rc_dir = path.join(config.builddir(), "config")
        os.mkdir(rc_dir)
        local vars = {
            -- forward slashes: the RC compiler treats a backslash in a string
            -- literal as an escape, so a native path silently corrupts it
            -- (CMake's SLIC3R_RESOURCES_DIR is slash-separated for the same reason)
            SLIC3R_RESOURCES_DIR = (path.join(os.projectdir(), "resources"):gsub("\\", "/"))
        }
        for _, kv in ipairs(table.wrap(target:values("pnp.rc.vars"))) do
            local k, v = kv:match("^([^=]+)=(.*)$")
            if k then
                vars[k] = v
            end
        end
        local function render(src, dst)
            local text = io.readfile(path.join(os.projectdir(), src))
            if text then
                io.writefile(dst, (text:gsub("@(.-)@", function (k) return vars[k] or "" end)))
            end
        end
        render("src/dev-utils/platform/msw/OrcaSlicer.manifest.in",
               path.join(rc_dir, "OrcaSlicer.manifest"))
        local rc_file = path.join(rc_dir, "OrcaSlicer.rc")
        render("src/dev-utils/platform/msw/OrcaSlicer.rc.in", rc_file)
        target:add("files", rc_file)
    end)
    -- releasedbg keeps a console (CMake: WIN32_EXECUTABLE off for RelWithDebInfo)
    if not is_mode("releasedbg") then
        add_defines("SLIC3R_WRAPPER_NOCONSOLE")
        add_ldflags("/SUBSYSTEM:WINDOWS")
    end
    if is_mode("release") then
        add_ldflags("/DEBUG")  -- debug symbols even in release
    end
    add_ldflags("/MANIFEST:NO")
    add_syslinks("user32", "opengl32", "shell32")
target_end()
end

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

-- ------------------------------------------------------------- packaging

-- xmake package -> self-contained portable directory (ADR-0001 step 5).
-- The layout itself lives in xmake/modules/pnp/layout.lua so the installer
-- ships exactly the same tree; see that file for the CMake parity mapping.
task("package")
    on_run(function ()
        import("pnp.layout")
        print("package: %s", layout.assemble())
    end)
    set_menu {
        usage = "xmake package",
        description = "Assemble a self-contained portable directory"
    }
task_end()

-- xmake pack -f nsis -> Windows installer, at parity with the CPack/NSIS
-- installer that CMakeLists.txt:978-1012 produced.
--
-- Driven through xmake's nsis backend but with a PROJECT-OWNED specfile: the
-- stock template (xmake/scripts/xpack/nsis/makensis.nsi) cannot express this
-- installer — no shortcuts, an unconditional PATH section CPack deliberately
-- disabled, per-user UAC via a plugin stock NSIS does not ship, and a
-- VIProductVersion that makensis rejects for a non-numeric version. The
-- divergences are enumerated at the top of installer/OrcaSlicer.nsi.
xpack("OrcaSlicer")
    set_formats("nsis")
    set_title("OrcaSlicer")
    set_description("Orca Slicer is an open source slicer for FDM printers")
    set_homepage("https://github.com/OrcaSlicer/OrcaSlicer")
    -- CPACK_PACKAGE_VENDOR; the specfile writes this as the registry Publisher
    set_company("SoftFever")
    -- Split version: xpack feeds PACKAGE_VERSION_{MAJOR,MINOR,ALTER} to the
    -- numeric NSIS version resources and PACKAGE_VERSION_BUILD ("pnp") to the
    -- display string. set_version("2.5.0-pnp") would not parse as semver here.
    set_version("2.5.0", {build = "pnp"})
    set_specfile("installer/OrcaSlicer.nsi")
    set_iconfile("resources/images/OrcaSlicer.ico")
    set_licensefile("LICENSE.txt")
    -- CPACK_NSIS_INSTALLED_ICON_NAME "$INSTDIR\orca-slicer.exe"
    set_nsis_displayicon("orca-slicer.exe")

    on_load(function (package)
        import("pnp.layout")
        -- CPACK_PACKAGE_FILE_NAME + the _x64/_arm64 suffix from
        -- CMakeLists.txt:987-993
        package:set("basename", "OrcaSlicer_Windows_Installer_V" ..
            layout.version_full() .. "_" .. package:arch())
    end)

    before_package(function (package)
        import("pnp.layout")
        -- Stage the same tree `xmake package` produces, so the portable
        -- directory and the installed directory cannot diverge.
        layout.assemble()
        -- The nsis backend only copies the template when the generated .nsi is
        -- absent (plugins/pack/nsis/main.lua:272-275), so a stale substituted
        -- copy from a previous run would silently win. Drop it.
        os.tryrm(path.join(package:builddir(), package:basename() .. ".nsi"))
    end)

    -- Emit directory-level File /r commands rather than one File per staged
    -- file: add_installfiles would expand resources/ into thousands of
    -- individual File/Delete lines in the .nsi.
    on_installcmd(function (package, batchcmds)
        import("pnp.layout")
        batchcmds:cp(path.join(layout.staging_dir(), "*"), package:installdir(),
            {rootdir = layout.staging_dir()})
    end)

    on_uninstallcmd(function (package, batchcmds)
        import("pnp.layout")
        for _, item in ipairs(os.filedirs(path.join(layout.staging_dir(), "*"))) do
            local dst = path.join(package:installdir(), path.filename(item))
            if os.isdir(item) then
                batchcmds:rmdir(dst)
            else
                batchcmds:rm(dst)
            end
        end
    end)

    after_package(function (package)
        -- CPACK_PACKAGE_CHECKSUM SHA256: CPack emitted a sidecar digest next to
        -- the installer; xmake's nsis backend does not.
        -- `hash` is a sandbox global here, not an importable module.
        local outputfile = package:outputfile()
        if os.isfile(outputfile) then
            local digest = hash.sha256(outputfile)
            io.writefile(outputfile .. ".sha256",
                digest .. " *" .. path.filename(outputfile) .. "\n")
            print("checksum: %s.sha256", path.filename(outputfile))
        end
    end)
xpack_end()

-- ------------------------------------------------------------------- tests
-- Catch2 suites ported from tests/CMakeLists.txt. Each suite is a binary;
-- `xmake test` runs them all (xmake's built-in test runner via add_tests).

-- Bundled Catch2 v3 (tests/catch2). Its catch_user_config.hpp is normally
-- produced by CMake configure_file from a #cmakedefine template; every option
-- defaults to off, so we render it by dropping the #cmakedefine lines and
-- substituting the two real values (CatchConfigOptions.cmake:83-84).
target("Catch2WithMain")
    set_kind("static")
    add_files("tests/catch2/src/catch2/**.cpp")
    add_includedirs("tests/catch2/src", "$(builddir)/catch2/generated-includes", {public = true})
    on_load(function (target)
        import("core.project.config")
        local outdir = path.join(config.builddir(), "catch2", "generated-includes", "catch2")
        os.mkdir(outdir)
        local text = io.readfile(path.join(os.projectdir(),
            "tests/catch2/src/catch2/catch_user_config.hpp.in"))
        local out = {}
        for _, line in ipairs(text:split("\n", {strict = true})) do
            if not line:startswith("#cmakedefine") then
                line = line:gsub("@CATCH_CONFIG_DEFAULT_REPORTER@", "console")
                line = line:gsub("@CATCH_CONFIG_CONSOLE_WIDTH@", "80")
                table.insert(out, line)
            end
        end
        io.writefile(path.join(outdir, "catch_user_config.hpp"), table.concat(out, "\n"))
    end)
    if is_plat("windows") then
        -- The project defines _UNICODE globally, which makes Catch2's bundled
        -- main emit wmain; the test exes use the default console entry point
        -- (mainCRTStartup -> main). Mirrors tests/CMakeLists.txt:9-11.
        add_defines("DO_NOT_USE_WMAIN")
    end
target_end()

-- Shared test settings (CMake's test_common INTERFACE target).
rule("pnp.test")
    on_load(function (target)
        local projectdir = os.projectdir()
        target:add("defines",
            "TEST_DATA_DIR=R\"(" .. path.join(projectdir, "tests", "data") .. ")\"",
            "PROFILES_DIR=R\"(" .. path.join(projectdir, "resources", "profiles") .. ")\"",
            "CATCH_CONFIG_FAST_COMPILE")
        target:add("includedirs", path.join(projectdir, "tests"))
        if target:is_plat("windows") then
            -- Catch2's main() lives in Catch2WithMain.lib. Without an explicit
            -- subsystem MSVC infers the entry point from object files only,
            -- never scans libs, and fails with LNK1561. CMake always passes
            -- /subsystem:console for executables, which makes the linker use
            -- mainCRTStartup and resolve main from the library.
            target:add("ldflags", "/SUBSYSTEM:CONSOLE", {force = true})
        end
    end)
rule_end()

-- suite -> extra source files beyond <suite>_tests.cpp (from each
-- tests/<suite>/CMakeLists.txt)
local test_suites = {
    -- libslic3r's suite also compiles ../libnest2d/printer_parts.cpp
    -- (tests/libslic3r/CMakeLists.txt:31) for PRINTER_PART_POLYGONS
    libslic3r = {"tests/libslic3r/*.cpp", "tests/libnest2d/printer_parts.cpp"},
    fff_print = {"tests/fff_print/*.cpp"},
    libnest2d = {"tests/libnest2d/*.cpp"},
    filament_group = {"tests/filament_group/*.cpp"}
}

for suite, files in pairs(test_suites) do
target(suite .. "_tests")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_files(files)
    if suite == "libslic3r" then
        -- commented out in tests/libslic3r/CMakeLists.txt:29 — it exercises
        -- sla::RasterBase, and SLA is removed from this fork
        remove_files("tests/libslic3r/test_png_io.cpp")
    end
    add_rules("pnp.test", "pnp.conan.dlls")
    add_deps("libslic3r", "Catch2WithMain")
    add_includedirs("src", "deps_src")
    add_tests("default")
    if suite == "filament_group" then
        add_defines("FG_TEST_GOLDEN_DIR=R\"(" ..
            path.join(os.projectdir(), "tests", "filament_group", "golden") .. ")\"")
    end
target_end()
end

-- slic3rutils additionally needs the GUI library
target("slic3rutils_tests")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_files("tests/slic3rutils/*.cpp")
    add_rules("pnp.test", "pnp.conan.dlls")
    add_deps("libslic3r", "libslic3r_gui", "Catch2WithMain")
    add_includedirs("src")
    add_tests("default")
    if is_plat("windows") then
        add_syslinks("Setupapi")
    end
target_end()

-- PNP fork suites: GUI-free units compiled straight into the test binary
-- (tests/pnp/CMakeLists.txt). Kept as two targets because the warnings-log
-- cases redirect the global data_dir().
target("pnp_config_translator_tests")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_files("tests/pnp/test_pnp_config_translator.cpp",
              "src/slic3r/GUI/PnpConfigTranslator.cpp")
    add_rules("pnp.test", "pnp.conan.dlls")
    add_deps("libslic3r", "Catch2WithMain")
    add_includedirs("src", "src/slic3r/GUI", "deps_src")
    add_tests("default")
target_end()

target("pnp_runtime_tests")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_files("tests/pnp/test_pnp_progress.cpp",
              "tests/pnp/test_pnp_config_warnings_log.cpp",
              "tests/pnp/test_pnp_support_preview.cpp",
              "src/slic3r/GUI/PnpProgress.cpp",
              "src/slic3r/GUI/PnpConfigWarningsLog.cpp",
              -- document half only; PnpSupportPreview.cpp (the pnp_cli runner)
              -- needs the GUI backend and is deliberately not linked here
              "src/slic3r/GUI/PnpSupportPreviewDoc.cpp")
    add_rules("pnp.test", "pnp.conan.dlls")
    add_deps("libslic3r", "Catch2WithMain")
    add_includedirs("src", "src/slic3r/GUI", "deps_src")
    add_tests("default")
target_end()

