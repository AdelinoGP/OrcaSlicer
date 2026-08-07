--!Shipping layout for OrcaSlicer (pnp_gui)
--
-- Single definition of "what a shipped OrcaSlicer tree looks like", imported by
-- every consumer so they can never drift:
--   * task("package")     -> the portable directory / .app bundle / FHS tree
--   * xpack("OrcaSlicer") -> the NSIS installer payload
--   * task("appimage") / task("dmg") / task("flatpak")
--
-- CMakeLists.txt:958-964 makes the same point about its own install() rules:
-- keep the layout in ONE guarded block so the platform variants cannot diverge.
--
-- PLATFORM STATUS
--   windows  VERIFIED — builds, launches, tests, packages, installs.
--   macosx   UNVERIFIED — never configured or built on a Mac. The bundle
--            layout below is derived from src/CMakeLists.txt:244-256.
--   linux    UNVERIFIED — never configured or built. Portable and FHS layouts
--            are derived from CMakeLists.txt:939-955 and 965-973.

import("core.project.config")

-- Full fork version including the -pnp suffix, read from version.inc — the
-- single source of version truth shared with xmake.lua and the flatpak/msix
-- scripts. Parsed textually; version.inc is CMake syntax but is never executed.
function version_full()
    local content = io.readfile(path.join(os.projectdir(), "version.inc"))
    assert(content, "version.inc not found — it is the single source of version truth")
    local version = content:match('set%s*%(%s*SoftFever_VERSION%s+"([^"]*)"')
    return assert(version, "version.inc: SoftFever_VERSION not found")
end

-- major, minor, patch with the fork suffix stripped. NSIS version resources
-- and CFBundleVersion-adjacent fields must be strictly numeric.
function version_triple()
    local major, minor, patch = version_full():match("^(%d+)%.(%d+)%.(%d+)")
    return assert(major, "version.inc: SoftFever_VERSION is not major.minor.patch"),
        minor, patch
end

-- the part after major.minor.patch, e.g. "pnp" from "2.5.0-pnp" ("" if none)
function version_build()
    return (version_full():gsub("^%d+%.%d+%.%d+%-?", ""))
end

-- directory holding freshly built binaries
function bindir()
    config.load()
    return path.join(config.builddir(), config.plat(), config.arch(),
        config.mode() or "release")
end

-- name of the staged tree, e.g. OrcaSlicer_2.5.0-pnp_windows_x64
function staging_name()
    config.load()
    return "OrcaSlicer_" .. version_full() .. "_" .. config.plat() .. "_" .. config.arch()
end

-- absolute path of the staged tree
function staging_dir()
    config.load()
    return path.join(config.builddir(), "package", staging_name())
end

-- the executable's basename per platform (src/CMakeLists.txt:117-123: unix-like
-- systems rename the target to orca-slicer; macOS keeps OrcaSlicer so the
-- binary name matches the application name)
function exe_name()
    config.load()
    local plat = config.plat()
    if plat == "windows" then
        return "orca-slicer.exe"
    elseif plat == "macosx" then
        return "OrcaSlicer"
    end
    return "orca-slicer"
end

function cli_name()
    config.load()
    return config.plat() == "windows" and "pnp_cli.exe" or "pnp_cli"
end

-- shared-library glob for the platform
local function _libpat()
    local plat = config.plat()
    if plat == "windows" then
        return "*.dll"
    elseif plat == "macosx" then
        return "*.dylib"
    end
    return "*.so*"
end

-- copy every runtime library from the build dir into dest
local function _copy_libs(bin_dir, dest)
    for _, file in ipairs(os.files(path.join(bin_dir, _libpat()))) do
        os.vcp(file, path.join(dest, path.filename(file)))
    end
end

-- copy the pnp backend (CLI + modules). PnpBackend::resolve_paths derives both
-- from the running binary's own directory, so they must land beside the
-- executable, not beside the resources.
local function _copy_backend(bin_dir, dest)
    local cli = path.join(bin_dir, cli_name())
    if os.isfile(cli) then
        os.vcp(cli, path.join(dest, cli_name()))
    end
    local modules = path.join(bin_dir, "modules")
    if os.isdir(modules) then
        os.vcp(modules, path.join(dest, "modules"))
    end
end

local function _copy_exe(bin_dir, dest, name)
    local src = path.join(bin_dir, exe_name())
    assert(os.isfile(src), "package: %s not found — run `xmake` first", src)
    os.vcp(src, path.join(dest, name or exe_name()))
end

-- copy the MSVC CRT beside the executable. The app is built /MD, so a clean
-- machine needs the redistributable DLLs present (CMake does this via
-- InstallRequiredSystemLibraries, CMakeLists.txt:936-938).
local function _copy_msvc_runtime(dest)
    local msvc = import("core.tool.toolchain").load("msvc",
        {plat = config.plat(), arch = config.arch()})
    local vcvars = msvc and msvc:config("vcvars")
    local redist_root = vcvars and vcvars.VCToolsRedistDir
    local copied = 0
    if redist_root then
        local arch = config.arch() == "x64" and "x64" or config.arch()
        for _, dir in ipairs(os.dirs(path.join(redist_root, arch, "Microsoft.VC*.CRT"))) do
            for _, dll in ipairs(os.files(path.join(dir, "*.dll"))) do
                os.vcp(dll, path.join(dest, path.filename(dll)))
                copied = copied + 1
            end
        end
    end
    if copied == 0 then
        wprint("package: MSVC redistributable DLLs not found; the shipped tree " ..
               "will only run where the VC++ runtime is installed")
    end
end

-- render an @VAR@ template (CMake configure_file semantics)
local function _configure_file(src, dst, vars)
    local content = assert(io.readfile(src), "package: cannot read %s", src)
    content = content:gsub("@([%w_]+)@", function (name)
        return vars[name] or ""
    end)
    io.writefile(dst, content)
end

-- ------------------------------------------------------------------ windows
--
--   .           orca-slicer.exe, OrcaSlicer.dll, runtime DLLs, pnp_cli, LICENSE
--   ./resources contents of resources/
--   ./modules   contents of the pnp dist modules/
--
-- Mirrors CMakeLists.txt:934-938 and src/CMakeLists.txt:295-302.
local function _stage_windows(bin_dir, dest)
    -- Runtime libraries, then the executable by name. Deliberately NOT a
    -- *.exe glob: the build dir also holds the test suites and can hold stale
    -- artifacts (e.g. an OrcaSlicer.exe left over from before OrcaSlicer
    -- became a shared library).
    _copy_libs(bin_dir, dest)
    _copy_exe(bin_dir, dest)
    _copy_backend(bin_dir, dest)
    os.vcp(path.join(os.projectdir(), "resources"), path.join(dest, "resources"))
    os.vcp(path.join(os.projectdir(), "LICENSE.txt"), path.join(dest, "LICENSE.txt"))
    _copy_msvc_runtime(dest)
end

-- ------------------------------------------------------------------- macosx
--
--   OrcaSlicer.app/Contents/Info.plist
--   OrcaSlicer.app/Contents/MacOS/       OrcaSlicer, pnp_cli, modules/
--   OrcaSlicer.app/Contents/Frameworks/  *.dylib
--   OrcaSlicer.app/Contents/Resources/   contents of resources/
--
-- Resources land directly in Contents/Resources because that is what
-- src/CMakeLists.txt:246-248 symlinks, and Info.plist's CFBundleIconFile is
-- "images/OrcaSlicer.icns", i.e. relative to Contents/Resources.
--
-- UNVERIFIED — no Mac available to build or run this.
local function _stage_macos(bin_dir, dest)
    local app = path.join(dest, "OrcaSlicer.app")
    local contents = path.join(app, "Contents")
    local macos_dir = path.join(contents, "MacOS")
    local res_dir = path.join(contents, "Resources")

    os.mkdir(macos_dir)
    os.mkdir(res_dir)

    _copy_exe(bin_dir, macos_dir)
    _copy_backend(bin_dir, macos_dir)
    _copy_libs(bin_dir, path.join(contents, "Frameworks"))

    -- resources/ CONTENTS into Contents/Resources
    for _, item in ipairs(os.filedirs(path.join(os.projectdir(), "resources", "*"))) do
        os.vcp(item, path.join(res_dir, path.filename(item)))
    end
    os.vcp(path.join(os.projectdir(), "LICENSE.txt"), path.join(res_dir, "LICENSE.txt"))

    _configure_file(
        path.join(os.projectdir(), "src", "dev-utils", "platform", "osx", "Info.plist.in"),
        path.join(contents, "Info.plist"),
        {
            SLIC3R_APP_KEY  = "OrcaSlicer",
            SLIC3R_APP_NAME = "OrcaSlicer",
            SLIC3R_BUILD_ID = version_full()
        })
    io.writefile(path.join(contents, "PkgInfo"), "APPL????\n")
    return app
end

-- -------------------------------------------------------------------- linux
--
-- portable (fhs off):
--   .           orca-slicer, *.so, pnp_cli, LICENSE
--   ./resources contents of resources/
--   ./modules
-- Mirrors CMakeLists.txt:953-955.
local function _stage_linux_portable(bin_dir, dest)
    _copy_libs(bin_dir, dest)
    _copy_exe(bin_dir, dest)
    _copy_backend(bin_dir, dest)
    os.vcp(path.join(os.projectdir(), "resources"), path.join(dest, "resources"))
    os.vcp(path.join(os.projectdir(), "LICENSE.txt"), path.join(dest, "LICENSE.txt"))
    -- CMakeLists.txt:954 stages the desktop entry under resources/applications
    -- in the non-FHS branch
    os.vcp(path.join(os.projectdir(), "src", "dev-utils", "platform", "unix",
                     "com.orcaslicer.OrcaSlicer.desktop"),
           path.join(dest, "resources", "applications",
                     "com.orcaslicer.OrcaSlicer.desktop"))
end

-- FHS (fhs on), rooted at dest so it can be tarred or DESTDIR-installed:
--   <prefix>/bin/orca-slicer, pnp_cli, modules/
--   <prefix>/share/OrcaSlicer/                       resources
--   <prefix>/share/applications/*.desktop
--   <prefix>/share/icons/hicolor/<N>x<N>/apps/OrcaSlicer.png
-- Mirrors CMakeLists.txt:939-950 and 970-971.
local function _stage_linux_fhs(bin_dir, dest)
    local prefix = (get_config("prefix") or "/usr/local"):gsub("^/", "")
    local root = path.join(dest, prefix)
    local bin = path.join(root, "bin")
    local share = path.join(root, "share")

    os.mkdir(bin)
    _copy_exe(bin_dir, bin)
    _copy_backend(bin_dir, bin)
    _copy_libs(bin_dir, path.join(root, "lib"))

    -- resources/ CONTENTS into share/OrcaSlicer (CMake excludes */udev; this
    -- tree currently has no udev directory, but keep the exclusion so the
    -- behaviour survives if one is added back)
    local res_dest = path.join(share, "OrcaSlicer")
    for _, item in ipairs(os.filedirs(path.join(os.projectdir(), "resources", "*"))) do
        if path.filename(item) ~= "udev" then
            os.vcp(item, path.join(res_dest, path.filename(item)))
        end
    end

    os.vcp(path.join(os.projectdir(), "src", "dev-utils", "platform", "unix",
                     "com.orcaslicer.OrcaSlicer.desktop"),
           path.join(share, "applications", "com.orcaslicer.OrcaSlicer.desktop"))

    for _, size in ipairs({32, 128, 192}) do
        local icon = path.join(os.projectdir(), "resources", "images",
            "OrcaSlicer_" .. size .. "px.png")
        if os.isfile(icon) then
            os.vcp(icon, path.join(share, "icons", "hicolor",
                size .. "x" .. size, "apps", "OrcaSlicer.png"))
        end
    end

    os.vcp(path.join(os.projectdir(), "LICENSE.txt"),
           path.join(share, "OrcaSlicer", "LICENSE.txt"))
end

-- assemble the staged tree from scratch and return its path
function assemble()
    config.load()

    local bin_dir = bindir()
    assert(os.isdir(bin_dir), "package: %s not found — run `xmake` first", bin_dir)

    local dest = staging_dir()
    os.tryrm(dest)
    os.mkdir(dest)

    local plat = config.plat()
    if plat == "windows" then
        _stage_windows(bin_dir, dest)
    elseif plat == "macosx" then
        wprint("package: the macOS bundle layout is UNVERIFIED — never built on a Mac")
        _stage_macos(bin_dir, dest)
    else
        if has_config("fhs") then
            wprint("package: the Linux FHS layout is UNVERIFIED — never built on Linux")
            _stage_linux_fhs(bin_dir, dest)
        else
            wprint("package: the Linux portable layout is UNVERIFIED — never built on Linux")
            _stage_linux_portable(bin_dir, dest)
        end
    end

    return dest
end
