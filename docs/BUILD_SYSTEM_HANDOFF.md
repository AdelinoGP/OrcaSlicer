# Technical Handoff: Xmake + Conan Build System Migration

**Date:** 2026-08-06, updated 2026-08-07
**Status:** Windows x64 **builds, links and launches** — `xmake` produces `orca-slicer.exe` + `OrcaSlicer.dll` with resources and the PNP backend staged, and the GUI comes up. Packaging, tests, and macOS/Linux pending. See §6 Steps 1-3.
**Read first:** `docs/adr/0001-xmake-conan-build-system.md` (the decision, incl. its amendment) and `conan/README.md` (dependency provisioning). `build-system-research.md` is the original cited comparison.

> **Sections 2-4 below are the original 2026-08-06 proof record and have NOT been rewritten.** Several of their conclusions were overturned by later work — most importantly the §3.4 "needs a custom recipe" table (ten entries; the real answer is one) and §4's "nothing has been committed". Where §6 (Next steps) and §5 (Pitfalls) disagree with §2-4, **§5/§6 win** — they were corrected against actual build and link failures.

---

## 1. What was decided (executive summary)

Replace CMake (`CMakeLists.txt` + `deps/` ExternalProject tree) with a two-layer cargo-adjacent build:

```
Xmake  -> C++ target graph, source discovery (globs), tests, staging, packaging (sole frontend)
Conan 2-> third-party C/C++ dependencies (add_requires("conan::..."))
Cargo  -> pinch_n_print_cli backend, UNCHANGED (xmake invokes `cargo xtask dist`)
```

- **Pure Xmake/Xrepo was tried and FAILED the core proof** (see §3.1) — this decision is proven, not theoretical.
- CMake and `deps/` remain in-tree and authoritative until cross-platform build/test/package parity is reached. **Do not delete them.**
- Migration is Windows-x64-first, then macOS and Linux.

---

## 2. Environment state (this machine)

All tools installed and working:

| Tool | Version | Installed via |
|---|---|---|
| xmake | 3.0.9+HEAD.2b184e178 | `scoop install xmake` (also installs `xrepo`) |
| conan | 2.31.2 | `pip install conan` (Python 3.14.4 via vfox) |
| MSVC | VS 18 2026 (compiler version 195) | pre-existing, `C:\Program Files\Microsoft Visual Studio\18\Community` |
| CMake | 4.3.4 (system) | pre-existing `C:\Program Files\CMake\bin` |
| cargo | 1.96.0 | pre-existing |

**Conan cache is WARM** (`C:\Users\agpen\.conan2`): 37 packages built/installed including the full wxWidgets 3.3.2 static build and its transitive tree. Packages cached:

```
autoconf/2.71 automake/1.16.5 b2/5.5.3 boost/1.84.0 bzip2/1.0.8 cereal/1.3.2 cgal/5.6.3
cmake/3.31.12 cmake/4.4.0 draco/1.5.7 eigen/5.0.1 expat/2.8.2 gettext/0.26 glfw/3.4 gmp/6.3.0
libiconv/1.17 libjpeg/9f libnoise/1.0.0 libpng/1.6.47 libtiff/4.7.2 libwebp/1.6.0 m4/1.4.19
mpfr/4.2.1 msys2/cci.latest nanosvg/cci.20231025 ninja/1.13.2 opengl/system pcre2/10.42
qhull/8.0.2 wxwidgets/3.3.2 xz_utils/5.8.3 yasm/1.3.0 zlib/1.3.1 zlib/1.3.2
```

Default Conan profile (`C:\Users\agpen\.conan2\profiles\default`):
```
[settings]
arch=x86_64
build_type=Release
compiler=msvc
compiler.cppstd=14        # <-- MUST be raised to 17 for the app (see §5.2)
compiler.runtime=dynamic  # <-- MUST be static if SLIC3R_STATIC=1 semantics are kept
compiler.version=195
os=Windows
```

**Proof artifacts** (scratch dir, safe to reuse or delete): `C:\Users\agpen\AppData\Local\Temp\opencode\pnp-build-proof\`
- `pure-xmake/` — wxproof.exe built with xrepo wxwidgets 3.2.4 prebuilt DLLs (the negative proof)
- `conan-hybrid/` — wxproof.exe built with **static wxWidgets 3.3.2 from source** (the positive proof); `build/.conan/wxwidgets/3.3.2/` contains the generated `conanfile.txt` + host/build profiles showing exactly what xmake passes to conan
- `syntax.lua`, `validate.lua`, `check*.lua` — xmake sandbox probe scripts (mostly dead ends, see §6 pitfalls)

---

## 3. Proof results (do not redo unless you doubt them)

### 3.1 Pure Xmake/Xrepo — FAILED (decisive)

xrepo's `wxwidgets` package (`~/.xmake/repositories/xmake-repo/packages/w/wxwidgets/xmake.lua`):
- Windows installs **prebuilt shared DLLs only** (`wxMSW-$(version)_vc14x_x64_Dev.7z`), max version **3.2.4** (3.2.5 exists only for non-Windows).
- `shared`, `vs_runtime`, `debug` configs are `readonly = true` — cannot build static, cannot choose runtime.
- No WebView Edge/private-fonts options; no way to point at the SoftFever 3.3.2 fork.

The project **requires**: static wxWidgets 3.3.2 from `SoftFever/Orca-deps-wxWidgets` (12 fork commits: dark theme, font/grid fixes, ARM64 webview, gstplayer disable) with `wxBUILD_SHARED=OFF`, `wxUSE_WEBVIEW_EDGE=ON`, `wxUSE_PRIVATE_FONTS=ON`, `wxUSE_MEDIACTRL=ON`, `wxUSE_OPENGL=ON`, private-headers install step. Full CMake arg set in `deps/wxWidgets/wxWidgets.cmake` (lines 30-57) — this is the recipe spec to replicate.

### 3.2 Xmake + Conan 2 — PASSED

- ConanCenter `wxwidgets` recipe has **3.3.2/3.3.3** with `shared`, `webview`, `mediactrl`, `opengl`, `aui`, `html`, `stc`, `cairo` options + `custom_enables`/`custom_disables` to pass arbitrary `wxUSE_*` flags.
- Static 3.3.2 built from source (ninja generator, see §5.1), `wxproof.exe` compiled and linked.
- The exact working `add_requires` (also in `xmake.lua`):

```lua
add_requires("conan::wxwidgets/3.3.2", {
    configs = {
        conf = {"tools.cmake.cmaketoolchain:generator=Ninja"},
        options = {
            "shared=False",
            "webview=True", "mediactrl=True", "opengl=True",
            "aui=True", "html=True",
            "stc=False", "cairo=False",
            "custom_enables=wxUSE_PRIVATE_FONTS, wxUSE_GLCANVAS_EGL, wxUSE_WEBREQUEST, wxUSE_WEBVIEW_EDGE",
            "custom_disables=wxUSE_DETECT_SM, wxUSE_WEBVIEW_IE, wxUSE_LIBSDL, wxUSE_XTEST, wxUSE_LIBTIFF, wxUSE_NANOSVG, wxUSE_LIBWEBP"
        }
    }
})
```

### 3.3 Full dependency resolution — PASSED

The scaffold's full `add_requires` set resolved on the first pass: boost/1.84.0, eigen/5.0.1, cereal/1.3.2, draco/1.5.7, qhull/8.0.2, glfw/3.4, cgal/5.6.3 (gmp+mpfr autotools chain built on MSVC), libnoise/1.0.0, zlib/1.3.1, libpng/1.6.47, libjpeg/9f — all with **zero custom recipes**.

### 3.4 ConanCenter version coverage vs project pins (what needs custom recipes)

> **SUPERSEDED — this table is wrong.** It was built from ConanCenter's *listed* versions, but the registry still serves delisted ones: `opencascade/7.6.0`, `openssl/1.1.1w`, `freetype/2.12.1`, `openexr/2.5.5` and `cereal/1.3.0` are all exact hits. Four of the "custom recipe" entries (OpenVDB, OpenCSG, OpenEXR, GLEW) turned out to serve only dead SLA-era code and were dropped entirely. Actual outcome: **two repo recipes**, both thin deltas over center (`opencascade` minus tcl/Draw; `wxwidgets` plus private headers, secretstore, debug-level 0, libsoup gating). See §6 Step 2 and `conan/README.md` for the real table. Always probe with `conan download <ref> -r conancenter --only-recipe` before writing a recipe.

| Project dep (deps/*.cmake) | ConanCenter has | Action |
|---|---|---|
| wxWidgets 3.3.2 fork | 3.3.2 vanilla | **Passed with vanilla + custom flags**; fork-patch equivalence still unproven |
| boost 1.84.0 | 1.84.0 | use center |
| eigen 5.0.1 | 5.0.1 | use center |
| cereal 1.3.0 | 1.3.2 | use center (nearest; behavior gate) |
| draco 1.5.7 | 1.5.7 | use center |
| qhull 8.0.2 | 8.0.2 | use center |
| glfw 3.4 | 3.4 | use center |
| CGAL 5.6.3 | 5.6.3 | use center |
| libnoise (SoftFever fork, tag 1.0) | 1.0.0 | use center first; verify against fork |
| zlib/libpng/libjpeg | yes | use center (transitive via wx) |
| **OCCT 7.6.0** | 7.9.1 only | **custom recipe** |
| **OpenCV 4.6.0** | 4.14.0 only | **custom recipe** |
| **OpenVDB 6.2.1** (tamasmeszaros fork, commit a68fd58) | 12.1.1 only | **custom recipe** |
| **TBB 2021.5** | tbb tops at 2020.3; onetbb 2023.1.0 | **custom recipe** (or adopt onetbb) |
| **OpenSSL 1.1.1w** | 3.x only | **custom recipe** |
| **curl 7.75** | 8.21.0 (as `libcurl`) | **custom recipe** |
| **freetype 2.12.1** | 2.13.2+ | **custom recipe** |
| **OpenCSG 1.4.2** | **no recipe** | **custom recipe** |
| **OpenEXR 2.5.5** | 2.5.7+ | **custom recipe** |
| **NLopt 2.5.0** | 2.10.1+ | **custom recipe** |

---

## 4. Current repo state

> **SUPERSEDED.** Everything below is committed on `pnp/main`:
> `2f43521169` (step 1: profiles + lockfile), `52b616d69d` (step 2: consolidated graph),
> `85507d9c1e` (3a: libslic3r), `c419a2ff8c` (3b: libslic3r_gui), `fb46ce5729` (3c: exe links).
> Current layout: `xmake.lua`, `conan/{conanfile.py,conan.lock,profile_host.txt,profile_build.txt,README.md,recipes/{opencascade,wxwidgets}}`, `docs/{BUILD_SYSTEM_HANDOFF.md,adr/0001-*.md}`, `build-system-research.md`.

Original 2026-08-06 record:

```
?? build-system-research.md      (306 lines — cited research report)
?? docs/adr/0001-xmake-conan-build-system.md   (50 lines — the ADR)
?? docs/BUILD_SYSTEM_HANDOFF.md  (this file)
?? xmake.lua                     (the scaffold + Step-1 conan wiring)
?? conan/                        (profile_host.txt, profile_build.txt, conanfile.py, conan.lock, README.md)
```

`xmake.lua` structure (validated: parses, configures, resolves all deps):
- Options: `slic3r_static`, `slic3r_gui`, `pnp_bundle_cli`, `pnp_dist_dir`
- Conan deps: the full set from §3.3/§3.4 (custom-recipe ones commented out with a TODO)
- Version header generation via `add_configfiles` for `libslic3r_version.h` (vars from `version.inc`) and `GeneratedConfig.hpp`
- `GIT_COMMIT_HASH` define via `os.iorunv("git", ...)` guarded by `os.exists(".git")`
- Targets: `libslic3r` (glob + 4 exclusions), `libslic3r_gui`, `OrcaSlicer` (with PNP `after_build` staging of `pnp_cli` + `modules/`)
- Tasks: `xmake pnp` (→ `cargo xtask dist`), `xmake test` (→ ctest, temporary)

Key CMake sources to port from (already read):
- `src/libslic3r/CMakeLists.txt` — target deps: Eigen3, admesh, libigl, libnest2d, miniz, opencv_world (PUBLIC); EXPAT, OCCT_LIBS (24 libs listed), boost_libs, cereal, clipper, Clipper2, draco, glu-libtess, JPEG, libslic3r_cgal, mcut, noise, PNG, qhull, qoi, semver, TBB, ZLIB, OpenSSL::Crypto (PRIVATE). `USE_TBB` + `TBB_USE_CAPTURED_EXCEPTION=0` defines. `libslic3r_cgal` sub-target (CutSurface, IntersectionPoints, MeshBoolean, TryCatchSignal, Triangulation) + CGAL `frounding-math` option juggling.
- `src/slic3r/CMakeLists.txt` — ~700-file `SLIC3R_GUI_SOURCES` list (replace with glob), `SLIC3R_CURRENTLY_COMPILING_GUI_MODULE`, `wxDEBUG_LEVEL=0`, `GeneratedConfig.hpp`; links wx, glfw, libcurl, OpenSSL, hidapi, mdns, imgui, imguizmo, minilzo, libvgcode, md4c-html, glad, OpenGL; Windows: `Advapi32`, `Setupapi`, WebView2 include (`deps/WebView2/include`).
- `src/CMakeLists.txt` — exe `OrcaSlicer` (SHARED on Windows, the shim `OrcaSlicer_app_gui` WIN32 + `.rc`), resources junction/symlink, DLL copying, PNP POST_BUILD staging, install rules.

**Orphan files the glob MUST exclude** (dead upstream code, in tree but in no CMakeLists) — verified 2026-08-07 by link errors, not by inspection:

- `src/libslic3r/`: `ExPolygonCollection.cpp`, `JumpPointSearch.cpp`, `TryCatchSignalSEH.cpp` (`#include`d from `TryCatchSignal.cpp` under `_MSC_VER`), `GCodeSender.cpp` (commented out upstream — pre-1.70 boost::asio), `OpenVDBUtils.cpp` (CMake compiles it only `if (TARGET OpenVDB::openvdb)`; OpenVDB is not provisioned).
- `src/slic3r/GUI/`: `Gizmos/GLGizmoAdvancedCut.cpp`, `Gizmos/GLGizmoFaceDetector.cpp` (commented out, CMakeLists:141), `Gizmos/GLGizmoText.cpp` (commented out, CMakeLists:179), `SysInfoDialog.cpp`, `WebUpdatePlugin.cpp`.

**Two claims in earlier revisions of this doc were WRONG — do not reintroduce them:**

1. `src/libslic3r/Circle.cpp` is **LIVE** (`src/libslic3r/CMakeLists.txt:52`). It defines `Slic3r::ArcSegment`. `Geometry/Circle.cpp` (line 148) is a *separate* live file, not a replacement. Excluding it costs 5 unresolved externals at exe link.
2. `src/slic3r/GUI/DeviceCore/**` (30 files) and `GUI/DeviceTab/**` (7 files) are **LIVE**. They look absent from `src/slic3r/CMakeLists.txt` because their own CMakeLists `list(APPEND SLIC3R_GUI_SOURCES ...)` and are pulled in by `add_subdirectory` (lines 699-700). Any "is this file in the CMake list?" audit must resolve `add_subdirectory` recursively, or it will report ~37 false orphans.

---

## 5. Known pitfalls (each cost real time — do not rediscover)

1. **VS 18 2026 generator bug (the big one).** ConanCenter's wxWidgets recipe pins `tool_requires("cmake/[>=3.17 <4]")` → cmake 3.31.12, which does NOT know "Visual Studio 18 2026" → `cmake.configure()` fails with "Could not create named generator Visual Studio 18 2026". Fix: force the Ninja generator via Conan conf `tools.cmake.cmaketoolchain:generator=Ninja` (works on all cmake versions and platforms). A `build_requires = {"cmake/[>=4.0 <5]"}` override in the xmake configs does NOT win over the recipe's own tool_require — don't bother trying it again.
2. **Description-scope Lua (root of xmake.lua) has NO command execution at all** — `os.iorunv`/`os.iorun`/`pcall`/`try` are nil there; calling them errors the whole parse ("attempt to call a nil value"). Command execution only works in *script scope* (rule/target `on_load`, `after_build`, tasks). The scaffold's git-hash detection is therefore a `git_commit_hash` rule whose `on_load` runs `os.iorunv("git", ...)` inside `try {}` and adds the define per-target. (An earlier version of this note claimed `os.iorunv` works at root scope — it does not; that bug made `xmake f` fail until the rule rewrite.)
3. **`set_ldflags` does not exist** — use `add_ldflags`.
4. **`task()` cannot call `set_description`** — put description/usage/options in the `set_menu { }` block.
5. **`$(buildir)` is deprecated** — use `$(builddir)` (scaffold already fixed).
6. **Platform misdetection:** Git's `C:\Program Files\Git\mingw64` in PATH makes xmake pick `mingw` unless you always pass `-p windows -a x64`. Always: `xmake f -p windows -a x64 -m release`.
7. **`add_requires` must be at root scope** (not inside targets); `add_packages("conan::pkg/ver")` inside the target uses the full qualified name.
8. **Conan profile defaults are wrong for this project:** `compiler.cppstd=14` (need 17) and `compiler.runtime=dynamic` (need static if preserving `SLIC3R_STATIC=1` semantics). Fix via xmake configs `settings = {"compiler.cppstd=17", "compiler.runtime=static"}` or a checked-in `conan/profile_host.txt` (see §6 step 1). NOTE: static MSVC runtime + static libs is the historical default (`SLIC3R_STATIC_INITIAL 1`) but the full app has never been linked static-only via this path — expect link-time discovery of which deps must be dynamic.
9. **Conan install of wxWidgets first build is ~30-60 min** (gettext/iconv/pcre2 autotools on MSVC is the bulk). It is cached now; never re-run `conan remove` on the cache.
10. **`xmake f` with conan requires interactive `-y`** to auto-confirm package installs (it also prompts on version mismatch, e.g. wxwidgets 3.2.5→3.2.4). Always pass `-y`.

---

## 6. Next steps (in order)

### Step 1 — Conan profiles: C++17 + static runtime, checked into repo — DONE (2026-08-06)
- `conan/profile_host.txt` + `conan/profile_build.txt` checked in (cppstd=17; static MSVC runtime on Windows via jinja guard; Ninja conf). xmake **cannot consume** external profiles or lockfiles (verified in `modules/package/manager/conan/v2/install_package.lua`), so the same settings ride along on every `add_requires` via the shared `conan_configs()` helper in xmake.lua: `settings = {"compiler.cppstd=17"}` (CLI `-s` beats xmake's hardcoded cppstd=14 profile line) + `runtimes = "MT"/"MTd"` (xmake maps it to `compiler.runtime=static` + `runtime_type`). Project targets get `set_runtimes("MT"/"MTd")` too. **Keep profile_host.txt and `conan_base` in xmake.lua in sync.**
- Lockfile pinned: `conan/conan.lock`, generated from the consolidated `conan/conanfile.txt` (mirrors the xmake require set; regeneration command in `conan/README.md`). Note: xmake's per-package installs don't enforce the lockfile — it pins versions/revisions for CI/direct-conan/drift detection.
- **Graph conflicts found:** cgal/5.6.3's recipe pins boost/1.83.0 + eigen/3.4.0 vs project's 1.84.0/5.0.1. Solved in the consolidated graph via `[replace_requires]` in profile_host.txt. xmake's isolated per-package installs do NOT see the replace — cgal's own install still resolves the old pins, so **the target port (Step 3) must keep cgal's transitive boost/eigen includedirs from shadowing the project's** (both header-only; watch include order or strip cgal's transitive includes).
- Binary-cache story documented in `conan/README.md` (local cache primary, GH Actions cache is an evictable optimization, `--build=missing` is the correctness fallback).
- Settings change → new package IDs → host packages rebuilt from source once (old binaries stay cached). Measured: the full `xmake f -c -y` with all 12 requires rebuilding under cppstd=17 + MT took **57m21s** on this machine; subsequent clean configures are seconds (cache hit).
- **Verified end-to-end:** `xmake f -c -y -p windows -a x64 -m release` exits 0; `xmake show -t libslic3r` shows `-MT -std:c++17`, `GIT_COMMIT_HASH="<hash>"`, and all conan include dirs. The dual-boost/eigen include hazard is visible in the compile line (direct requires precede cgal's transitives, so project versions win by include order — fragile, still tracked for Step 3).
- **Scaffold config bugs fixed along the way** (config was NOT clean before, contrary to §4's earlier claim):
  - root-scope `os.iorunv` crash → `git_commit_hash` rule (see pitfall 2);
  - headers were globbed into `add_files` ("unknown source file: *.hpp") and the "exclusion" list was actually *added* — now cpp-only globs + `remove_files`;
  - `OrcaSlicer.rc.in` configfile had no `variables` (needs SLIC3R_APP_NAME/BUILD_ID/VERSION + SLIC3R_RESOURCES_DIR=<srcdir>/resources);
  - `GeneratedConfig.hpp` configfile was never wired to `libslic3r_gui` (vars were defined but unused);
  - default configdir is `$(builddir)` root, not `$(builddir)/config` — targets now `set_configdir("$(builddir)/config")` (flat `#include "libslic3r_version.h"` / `"GeneratedConfig.hpp"` style verified in sources);
  - global `set_pcxxheader("src/slic3r/pchheader.hpp")` would have applied the GUI pch to libslic3r — now per-target (`src/libslic3r/pchheader.hpp` vs `src/slic3r/pchheader.hpp`);
  - the `.rc.in` was also passed to `add_files` (unknown source type) — dropped; compiling the generated `.rc` is a Step-3 TODO.

### Step 2 — Full dependency set — DONE 2026-08-06 (one repo recipe; §3.4 was too pessimistic)
Key discoveries that rewrote this step (details in `conan/README.md` + ADR amendment):
1. **ConanCenter still SERVES versions absent from its listing.** `conan download <ref> -r conancenter --only-recipe` found exact pins: **opencascade/7.6.0** (conan2-ready recipe incl. the C++17 portability patch), **openssl/1.1.1w**, **freetype/2.12.1**, **openexr/2.5.5**, **cereal/1.3.0**. Always probe before writing a recipe.
2. **The remaining "custom recipe" deps are dead code in this fork:** OpenVDB's only consumer (`VoxelizeCSGMesh.hpp`) has no callers; OpenCSG appears solely in a comment; OpenEXR and GLEW existed only as openvdb/opencsg deps. All dropped (SLA removal fallout). `OpenVDBUtils.cpp` is excluded from the libslic3r glob (CMake compiles it only `if (TARGET OpenVDB::openvdb)`).
3. **Nearest-upstream picks (behavior-gated):** libcurl/7.86.0 (pin 7.75.0), onetbb/2021.7.0 (pin 2021.5.0, shared — its recipe also demands `hwloc/*:shared=True`), opencv/4.5.5 (pin 4.6.0; core+imgproc only — sole users are SkipPartCanvas + ObjColorUtils), nlopt/2.9.1 (pin 2.5.0; center serves 2.7.1 but its CMakeLists is rejected by CMake 4).
4. **Architecture change (ADR amendment):** per-package `add_requires("conan::...")` proved structurally unsound — isolated installs resolve conflicting transitive versions (occt→freetype/2.13.2, cgal→boost/1.83.0+eigen/3.4.0, libcurl→openssl/3.x) and ignore the lockfile. Replaced by ONE `conan install` of `conan/conanfile.py`; its `generate()` emits `pnp_deps.lua` (per-package cpp_info + direct dep names) consumed by the xmake rule **`pnp.conan`** (targets declare `set_values("pnp.conan.packages", ...)`, flags injected public so they propagate to the binary). Rule `pnp.conan.dlls` stages shared-package DLLs (onetbb, hwloc) beside the exe.
5. **MSVC runtime corrected to /MD (dynamic):** the authoritative CMake build never overrides the runtime; `SLIC3R_STATIC` means static *libraries*, not static CRT. The Step-1 `/MT` reading is reverted (profiles + `set_runtimes("MD"/"MDd")`); the /MT-built binaries stay orphaned in the conan cache. Bonus: the original dynamic-runtime cache became reusable again via Conan's cppstd compatibility fallback.
6. Debug builds need a debug host profile (MDd deps) — not yet set up; `pnp.conan` warns in debug mode.
7. **tcl is unbuildable under VS 2026** (its `nmakehlp`/`rules.vc` bootstrap fails for 8.6.10 AND 8.6.13; no center binaries exist for compiler.version=195). tcl only served OCCT's Draw test harness → repo recipe `conan/recipes/opencascade/` = center 7.6.0 recipe with tcl/tk removed + `BUILD_MODULE_Draw=OFF` (deps/OCCT likewise built Draw-less without tcl). Repo recipes pin `version` in-recipe; `pnp.conan` auto-exports them before installing. GOTCHA: `conan lock create` auto-loads the stale `conan.lock` sitting beside the conanfile — pass `--lockfile=""` after changing a repo recipe or the old recipe revision stays pinned.

### Step 3 — Port the full target graph — BUILD+LINK DONE 2026-08-07 (runtime unverified)

`xmake build OrcaSlicer` exits 0 on Windows x64: `libslic3r` (3a, commit `85507d9c1e`), `libslic3r_gui` (3b, `c419a2ff8c`), `OrcaSlicer.exe` (3c, `fb46ce5729`), with `pnp_cli.exe` + `modules/` and the TBB/hwloc DLLs staged beside it.

**Vendored in-tree targets ported** (from `deps_src/*/CMakeLists.txt` and `src/*/CMakeLists.txt`): admesh, clipper, Clipper2, glu-libtess, mcut, miniz, qoi, semver, glad, libvgcode, imgui, imguizmo, hidapi, mdns, minilzo, md4c, plus `libslic3r_cgal`. `libnest2d` is folded into `libslic3r` (its CMake target links back to libslic3r — cyclic for xmake `add_deps`).

**Dependency-config bugs the link exposed** (all fixed; CMake build was ground truth):
- `boost/*:header_only=True` was wrong — the app links 13 compiled components (`CMakeLists.txt:618`). This settles ADR open question 2's sibling.
- wx must be built with `wxBUILD_DEBUG_LEVEL=0` to match the app's `-DwxDEBUG_LEVEL=0` (`src/slic3r/CMakeLists.txt:815`), else `LNK2005` on `wxFormatString::Validate`.
- ConanCenter's wx recipe requires `libsoup` for *any* `webview=True`; it only backs the GTK/WebKit backend, so Windows got `soup-3.0` in its link line (`LNK1181`). Gated to Linux/FreeBSD in the repo recipe.
- `Synchronization.lib` must be linked: boost::log's `atomic_based_event` uses `WaitOnAddress`/`WakeByAddressSingle` under `BOOST_USE_WINAPI_VERSION=0x602`; conan's boost recipe omits it from `system_libs`.

**MSVC/toolchain gotchas** (each cost a build cycle):
- `set_languages("c99", ...)` makes xmake compile `.c` as C++ (`-TP`) on MSVC — breaks K&R sources (mcut's `shewchuk.c`). Use `c11`.
- CMake's MSVC defaults define `WIN32`/`_WINDOWS`; xmake does not. Sources use `#if WIN32` → `C1017`.
- `deps_src/agg` must NOT be on the include path: its `VERSION` file shadows the C++20 `<version>` header on case-insensitive filesystems. Its only user is removed SLA code.
- `CGAL_DO_NOT_USE_MPZF` (upstream's MSVC define) is deliberately NOT set: it selects `Quotient<Gmpzf>`, whose boost-1.84 mixed comparisons hit a `cl >= 19.40` `C2666` regression. The default `Mpzf` path compiles. **Behavior gate: mesh-boolean tests.**
- `/bigobj` is required (MeshBoolean, GUI TUs); set globally.
- nanosvg comes from `deps_src/nanosvg` (SoftFever fork — needs `nsvgRasterizeXY`), not ConanCenter.
- opencv needs `imgcodecs` (`cv::imread` in `SkipPartCanvas`), and every jpeg consumer in the graph (wx, libtiff, opencv) must agree on `libjpeg-turbo` or conan raises a `provides` conflict.
- **Parallelism**: `-j` default OOM-killed this machine mid-build repeatedly; `-j2`/`-j4` are stable. Builds are incremental, so a killed run resumes.

**3d — Windows exe split + GUI launch (commit `94029e9117`).** On Windows `OrcaSlicer` is a SHARED lib (`OrcaSlicer.dll`, exports `orcaslicer_main`) and `OrcaSlicer_app_gui` is the WIN32 launcher `orca-slicer.exe` that probes OpenGL then `LoadLibrary`s the dll — same split as `src/CMakeLists.txt:105-178`. `resources/` is symlinked beside the exe (copy fallback). **Verified: the GUI launches**, main window `Untitled - OrcaSlicer`, no warning dialogs.

Three traps in the `.rc`/manifest path, all silent:
- `add_files()` resolves at **load time**, so a generated file that does not exist yet is skipped without a warning — the exe then links with no manifest (wx pops a "Common Controls v6" warning dialog at startup) and no icon. Render generated sources in `on_load` and register them with `target:add("files", ...)`. `on_load` also has `os.mkdir`, which description scope lacks.
- `set_values` stores a flat string list, **not** a Lua map — passing `version_vars` through it yields empty substitutions and `RC2127` on an empty `FILEVERSION`. Pass `KEY=VALUE` strings and reassemble.
- The RC compiler treats a backslash in a string literal as an escape, so a native Windows path in the icon line silently becomes garbage (`esources` → CR + `esources`). Use forward slashes, as CMake's `SLIC3R_RESOURCES_DIR` does.

**Remaining in Step 3:**
- macOS `Info.plist`; Linux `orca-slicer` naming/FHS.
- Tests: port `tests/` Catch2 to `xmake test` (keep ctest working in parallel).
- Deeper runtime verification: the GUI boots, but slicing/PNP handoff, dark theme and dialog rendering are unexercised (ADR open question 1 — wx fork-patch equivalence — needs human visual inspection).
- `unix/fhs.hpp` is generated with the portable layout on all platforms; Linux packaging will need the real FHS values (CMake uses `#cmakedefine`, which `add_configfiles` cannot render).

### Step 4 — PNP integration (already scaffolded, dist verified present)
- `xmake pnp` → `cargo xtask dist`; `after_build` on `OrcaSlicer` copies `pnp_cli` + `modules/` beside the exe (mirrors `src/CMakeLists.txt:263-278`).
- **Verified:** `pinch_n_print_cli/target/dist/` already contains `pnp_cli.exe` + `modules/`, so the `after_build` staging can be tested immediately.

### Step 5 — Packaging parity, then cutover
- Portable dir first (exe + resources + dlls + pnp_cli + modules), then NSIS (Windows) and mac/linux packages, then delete CMake only after parity.

---

## 7. Validation commands

```bash
# consolidated dependency graph resolves + builds (what rule pnp.conan runs)
conan install conan/conanfile.py \
    --profile:host=conan/profile_host.txt --profile:build=conan/profile_build.txt \
    --lockfile=conan/conan.lock --build=missing -of build/conan/windows_x64_release

# scaffold configures (triggers pnp.conan automatically when stale)
xmake f -c -y -p windows -a x64 -m release

# flags reach the compiler (runtime, cppstd, conan includes, GIT_COMMIT_HASH)
xmake show -t libslic3r

# lockfile still matches conanfile.py (should print no changes)
conan lock create conan/conanfile.py -pr:h=conan/profile_host.txt \
    -pr:b=conan/profile_build.txt --lockfile-out=/tmp/check.lock && diff conan/conan.lock /tmp/check.lock

# conan cache health
conan list "*:*"
```

---

## 8. Open questions for the next session

1. **Fork-patch equivalence:** does the app need the 12 SoftFever wxWidgets commits (dark theme etc.) or does vanilla 3.3.2 + the custom flags suffice? Decide via a GUI smoke test (launch, dark theme, dialogs). If patches are needed, add a repo recipe under `conan/recipes/` with conandata patches rather than a fork.
2. ~~Static vs dynamic runtime~~ — RESOLVED: /MD everywhere (the CMake build never set /MT; see Step 2.5). Boost is compiled (not header-only) and static — see Step 3.
3. ~~TBB version~~ — RESOLVED: onetbb/2021.7.0 shared, behavior gate at runtime.
4. **libnoise:** SoftFever fork vs center 1.0.0 — behavior gate.
5. **OCCT static-on-Windows divergence:** deps/ built OCCT Shared on Windows; conan builds it static everywhere. Gate at STEP/3MF import smoke test.
6. **Nearest-upstream behavior gates** pending target port: libcurl 7.86.0, opencv 4.5.5, nlopt 2.9.1, cereal 1.3.0 (was 1.3.2 in scaffold), onetbb 2021.7.0.
7. **vcpkg as fallback** (documented in research) only if a specific Conan recipe fails irreparably.
