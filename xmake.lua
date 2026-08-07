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
--   [x] full dependency set: ConanCenter + one repo recipe (opencascade
--       minus tcl/Draw); dead SLA-era deps dropped (openvdb, opencsg,
--       openexr, glew)
--   [ ] full target graph port (libslic3r, libslic3r_gui, exe, shim, tests)
--   [ ] packaging parity (installers, portable dir)
-- CMake and deps/ remain authoritative until parity.

set_project("OrcaSlicer")
set_version("2.5.0-pnp")
set_languages("c++17")
set_default("libslic3r", "libslic3r_gui", "OrcaSlicer")

add_rules("mode.debug", "mode.release", "mode.releasedbg")

-- Dynamic MSVC runtime (/MD, /MDd in debug) — parity with the authoritative
-- CMake build (it never overrides the runtime; SLIC3R_STATIC means static
-- *libraries*, not static CRT). Keep in sync with conan/profile_host.txt.
if is_plat("windows") then
    set_runtimes(is_mode("debug") and "MDd" or "MD")
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
        "src/libslic3r/TryCatchSignalSEH.cpp",
        -- OpenVDB is not provisioned: its only consumer (VoxelizeCSGMesh.hpp,
        -- SLA-era) has no callers in this fork; CMake compiles OpenVDBUtils.cpp
        -- only `if (TARGET OpenVDB::openvdb)`.
        "src/libslic3r/OpenVDBUtils.cpp")
    add_includedirs("src", "src/libslic3r", {public = true})
    set_configdir("$(builddir)/config")
    add_configfiles("src/libslic3r/libslic3r_version.h.in",
        {filename = "libslic3r_version.h", pattern = "@(.-)@", variables = version_vars})
    add_includedirs("$(builddir)/config", {public = true})
    add_defines("USE_TBB", "TBB_USE_CAPTURED_EXCEPTION=0", {public = true})
    add_defines("SLIC3R_VERSION_IS_FORK", "PNP_FORK")

    -- direct conan deps (mirrors src/libslic3r/CMakeLists.txt link list;
    -- freetype/tcl arrive via the opencascade closure)
    add_rules("pnp.conan")
    set_values("pnp.conan.packages",
        "boost", "eigen", "cereal", "draco", "qhull", "cgal", "libnoise",
        "zlib", "libpng", "libjpeg", "expat", "nanosvg",
        "opencascade", "opencv", "onetbb", "nlopt", "openssl")

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
    -- direct conan deps (libslic3r's public set propagates via add_deps)
    add_rules("pnp.conan")
    set_values("pnp.conan.packages",
        "wxwidgets", "glfw", "libcurl", "opencv", "onetbb", "boost", "expat", "nanosvg")
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
    add_rules("pnp.conan", "pnp.conan.dlls")
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
