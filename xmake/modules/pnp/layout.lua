--!Shipping layout for OrcaSlicer (pnp_gui)
--
-- Single definition of "what a shipped OrcaSlicer tree looks like", imported by
-- both consumers so they can never drift:
--   * task("package")   -> the portable directory
--   * xpack("OrcaSlicer") -> the NSIS installer payload
--
-- Mirrors the Windows install layout from CMakeLists.txt:934-975 and
-- src/CMakeLists.txt:295-302:
--   .           orca-slicer.exe, OrcaSlicer.dll, runtime DLLs, pnp_cli, LICENSE
--   ./resources contents of resources/
--   ./modules   contents of the pnp dist modules/
--
-- CMakeLists.txt:958-964 makes the same point about its own install() rules:
-- keep the layout in ONE guarded block so the platform variants cannot diverge.

import("core.project.config")

-- full fork version, including the -pnp suffix (matches set_version in xmake.lua
-- and SoftFever_VERSION in the rc vars)
function version_full()
    return "2.5.0-pnp"
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

-- copy the MSVC CRT beside the executable. The app is built /MD, so a clean
-- machine needs the redistributable DLLs present (CMake does this via
-- InstallRequiredSystemLibraries, CMakeLists.txt:936-938).
function _copy_msvc_runtime(dest)
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

-- assemble the staged tree from scratch and return its path
function assemble()
    config.load()

    local bin_dir = bindir()
    assert(os.isdir(bin_dir), "package: %s not found — run `xmake` first", bin_dir)

    local dest = staging_dir()
    os.tryrm(dest)
    os.mkdir(dest)

    -- Runtime libraries, then the shipped executables by name.
    -- Deliberately NOT a *.exe glob: the build dir also holds the test suites
    -- and can hold stale artifacts (e.g. an OrcaSlicer.exe left over from
    -- before OrcaSlicer became a shared library).
    local libpat = config.plat() == "windows" and "*.dll"
        or (config.plat() == "macosx" and "*.dylib" or "*.so*")
    for _, file in ipairs(os.files(path.join(bin_dir, libpat))) do
        os.vcp(file, path.join(dest, path.filename(file)))
    end
    local exe = config.plat() == "windows" and ".exe" or ""
    for _, name in ipairs({"orca-slicer" .. exe, "pnp_cli" .. exe}) do
        local src = path.join(bin_dir, name)
        if os.isfile(src) then
            os.vcp(src, path.join(dest, name))
        elseif name:startswith("orca-slicer") then
            raise("package: %s not found — run `xmake` first", src)
        end
    end

    -- resources: copy the real tree, not the build tree's symlink
    os.vcp(path.join(os.projectdir(), "resources"), path.join(dest, "resources"))

    -- pnp backend modules (the CLI itself is handled above)
    local modules = path.join(bin_dir, "modules")
    if os.isdir(modules) then
        os.vcp(modules, path.join(dest, "modules"))
    end

    os.vcp(path.join(os.projectdir(), "LICENSE.txt"), path.join(dest, "LICENSE.txt"))

    if config.plat() == "windows" then
        _copy_msvc_runtime(dest)
    end

    return dest
end
