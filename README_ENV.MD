# OrcaSlicer Windows Build Environment

Notes from a successful full build of OrcaSlicer on Windows 10/11 with
**Visual Studio 18 (2026)**. Captures tool versions, install steps, and the
workarounds needed to get past issues in the upstream build scripts.

## Environment

| Tool | Version | Location |
| --- | --- | --- |
| Visual Studio | 18 (2026), MSVC 19.51.36248 | `C:\Program Files\Microsoft Visual Studio\18\Community` |
| CMake | 4.3.4 | `C:\Program Files\CMake\bin\cmake.exe` |
| Ninja | bundled with VS 18 | `C:\Program Files\...\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` |
| Strawberry Perl | 5.42.2.1 (UCRT, 64-bit) | `C:\tools\perl` |
| OpenSSL | 4.0.1 (prebuilt, dev headers) | `C:\Program Files\OpenSSL-Win64` |
| OS | Windows 10/11 x64 | |
| Build target | x64, `RelWithDebInfo` | |

## What's already in this repo

- `build_release_vs2022.bat` — upstream script (VS 2022 only, generator hard-coded)
- `build_release_vs18.bat` — patched copy using the **Visual Studio 18 2026** generator
- `build_slicer_only.bat` — convenience script that skips the deps build (assumes deps already built)

## One-time setup

### 1. Strawberry Perl (required for `dep_OpenSSL` 1.1.1w)

Git's Cygwin Perl is **not** acceptable for building OpenSSL 1.1.1w — its
build system needs Windows-native Perl that produces backslash paths.

Download the portable zip from
`https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases` (latest
`*-portable.zip`), extract to `C:\tools\`, then add `C:\tools\perl\bin` to
`PATH` for the build session.

Verify:
```bat
where perl   :: should show C:\tools\perl\bin\perl.exe
perl -V      :: should report osname=MSWin32
```

### 2. OpenSSL dev headers (required for `OrcaSlicer.dll`)

`dep_OpenSSL` in the deps tree builds 1.1.1w, but the **slicer** itself calls
`find_package(OpenSSL REQUIRED)`. The prebuilt installer at
`https://slproweb.com/` is the standard source.

```bat
:: download Win64OpenSSL-3.x.x.exe
:: install to C:\Program Files\OpenSSL-Win64
:: ensure "Copy OpenSSL DLLs to ..." and "OpenSSL Headers" are selected
```

Verify:
```bat
dir "C:\Program Files\OpenSSL-Win64\include\openssl\ssl.h"
dir "C:\Program Files\OpenSSL-Win64\lib\VC\x64\MD\libssl.lib"
```

## Build steps

### Build deps + slicer (clean)

```bat
:: from repo root
build_release_vs18.bat x64 debuginfo
```

`x64` selects arch, `debuginfo` selects `RelWithDebInfo`. Output goes to
`deps/build-dbginfo/` and `build-dbginfo/`.

This step:
- downloads ~30 source archives (Boost, CGAL, OCCT, wxWidgets, OpenSSL, TBB, etc.) into `deps/DL_CACHE/`
- builds ~25 external projects into `deps/build-dbginfo/OrcaSlicer_dep/usr/local/`
- installs the slicer to `build-dbginfo/OrcaSlicer/`

**First run takes 2–6 hours** depending on disk and CPU.

### Build slicer only (incremental)

```bat
build_slicer_only.bat
```

Use this after the first successful deps build. Cleans `build-dbginfo\`,
re-configures with VS 18 2026, builds `ALL_BUILD`, runs `gettext`, then runs
the `install` target.

## Known issues and fixes

### `dep_OpenSSL` fails with Cygwin Perl

Symptom:
```
*** This perl implementation doesn't produce Windows like paths (with backward
*** slash directory directory separators).  Please use an implementation that
*** matches your building platform.
```

Fix: install Strawberry Perl and put `C:\tools\perl\bin` first on `PATH`.

If dep_OpenSSL still fails with `Can't open perl script "Configure"` after
interrupting a previous run, the ExternalProject source dir was cleared but
the extract stamp survived. Recover:

```bash
rm -rf deps/build-dbginfo/dep_OpenSSL-prefix/src/dep_OpenSSL
rm -f deps/build-dbginfo/dep_OpenSSL-prefix/src/dep_OpenSSL-stamp/extract-dep_OpenSSL.cmake
rm -rf deps/build-dbginfo/dep_OpenSSL-prefix/src/dep_OpenSSL-build
rm -rf deps/build-dbginfo/CMakeFiles/dep_OpenSSL.dir
rm -f deps/build-dbginfo/dep_OpenSSL.vcxproj deps/build-dbginfo/dep_OpenSSL.vcxproj.filters
mkdir -p deps/build-dbginfo/dep_OpenSSL-prefix/src/dep_OpenSSL
tar -xzf deps/DL_CACHE/OpenSSL/OpenSSL_1_1_1w.tar.gz \
  -C deps/build-dbginfo/dep_OpenSSL-prefix/src/dep_OpenSSL --strip-components=1
```

Then re-create `extract-dep_OpenSSL.cmake` (copy the format from any other
extracted dep's stamp) and rerun the build.

### `dep_CURL` fails with `asn1_string_st` undefined

CURL 7.75.0 headers reference `asn1_string_st::data` from OpenSSL 1.1.x.
If `OPENSSL_ROOT_DIR` points at a newer system OpenSSL (4.0.1), CMake auto-detects
it and CURL compiles against the wrong headers.

Fix: do **not** set `OPENSSL_ROOT_DIR` for the deps build. Let CURL pick up
`dep_OpenSSL 1.1.1w` via the prefix path that the deps project sets up. The
slicer configure then finds the same dep_OpenSSL.

### `dep_Qhull` extracted source vanishes

Same root cause as the OpenSSL case: stamp survives, source is gone. Fix
identically:

```bash
rm -rf deps/build-dbginfo/dep_Qhull-prefix/src/dep_Qhull
rm -f deps/build-dbginfo/dep_Qhull-prefix/src/dep_Qhull-stamp/extract-dep_Qhull.cmake
mkdir -p deps/build-dbginfo/dep_Qhull-prefix/src/dep_Qhull
unzip -q deps/DL_CACHE/Qhull/v8.0.2.zip \
  -d deps/build-dbginfo/dep_Qhull-prefix/src/dep_Qhull
mv deps/build-dbginfo/dep_Qhull-prefix/src/dep_Qhull/Qhull-8.0.2/* \
   deps/build-dbginfo/dep_Qhull-prefix/src/dep_Qhull/
```

### Build script ignores dep failures

`build_release_vs2022.bat` does not abort when an external project fails. The
script will happily run the slicer configure against incomplete deps and fail
in confusing ways.

The patched `build_release_vs18.bat` adds an error check after the deps
build:

```bat
cmake --build . --config %build_type% --target deps -- -m
@echo off
if errorlevel 1 (
    echo DEPS BUILD FAILED with error %errorlevel% - aborting
    exit /b %errorlevel%
)
```

### Visual Studio 18 generator name

`cmake --help` lists this generator as `Visual Studio 18 2026` (not `2025`).
The original `build_release_vs2022.bat` hard-codes `Visual Studio 17 2022`,
which is not present on VS 18-only machines.

### `run_gettext.bat` hangs in python

After a successful install, the batch script invokes `gettext` and ends with
4 Python helper procs that sometimes never exit. The slicer binary and
resources are already installed at that point. The procs are safe to kill
with `taskkill /pid <pid> /f` if the build appears stuck at 100%.

Affected Python procs can be identified by their very small memory footprint
(3–4 MB) and zero CPU time.

## Layout

```
deps/build-dbginfo/
  OrcaSlicer_dep/usr/local/    <- built deps, prefix for slicer
  dep_*-prefix/src/            <- external project sources + stamps
  dep_*.vcxproj                <- one per external project
build-dbginfo/
  src/RelWithDebInfo/          <- slicer intermediate objects + dlls
  OrcaSlicer/                  <- installed slicer (binary + resources)
```

## Quick sanity checks

```bat
:: deps complete
dir deps\build-dbginfo\OrcaSlicer_dep\usr\local\lib\*.lib | find /c ".lib"
:: expect: ~57

:: slicer installed
dir build-dbginfo\OrcaSlicer\orca-slicer.exe
dir build-dbginfo\OrcaSlicer\OrcaSlicer.dll
dir build-dbginfo\OrcaSlicer\resources\i18n\*\OrcaSlicer.mo | find /c ".mo"
:: expect: 22 (en is built-in, 23 dirs, 22 .mo files)
```

## Rebuilding from scratch

To wipe everything and start over:

```bash
rm -rf build-dbginfo build deps/build-dbginfo/OrcaSlicer_dep
:: keep deps/DL_CACHE/ (downloaded source archives) to save bandwidth
```

Then run `build_release_vs18.bat x64 debuginfo` again.
