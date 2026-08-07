# Technical Handoff: Xmake + Conan Build System Migration

**Date:** 2026-08-06
**Status:** Proof complete, migration scaffolding in place, custom recipes + full target port pending.
**Read first:** `docs/adr/0001-xmake-conan-build-system.md` (the decision) and `build-system-research.md` (the cited comparison).

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

Untracked (uncommitted — nothing has been committed):

```
?? build-system-research.md      (306 lines — cited research report)
?? docs/adr/0001-xmake-conan-build-system.md   (50 lines — the ADR)
?? docs/BUILD_SYSTEM_HANDOFF.md  (this file)
?? xmake.lua                     (the scaffold + Step-1 conan wiring)
?? conan/                        (profile_host.txt, profile_build.txt, conanfile.txt, conan.lock, README.md)
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

**Orphan files the glob MUST exclude** (dead upstream code, in tree but not in CMake):
`src/libslic3r/Circle.cpp` (top-level; `Geometry/Circle.cpp` is live), `src/libslic3r/ExPolygonCollection.cpp`, `src/libslic3r/JumpPointSearch.cpp`, `src/libslic3r/TryCatchSignalSEH.cpp` (it's `#include`d from `TryCatchSignal.cpp` under `_MSC_VER`). Already handled in the scaffold's `libslic3r_sources()`.

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

### Step 2 — Custom recipes (priority order = risk order)
Directory convention: `conan/recipes/<pkg>/<version>/conanfile.py` + `test_package/`. Reference the SoftFever fork sources (from `deps/*.cmake`):
1. **opencascade/7.6.0** — biggest risk; the app links 24 TK libs; base the recipe on the ConanCenter 7.9.1 recipe and pin the 7.6.0 URL (`Open-Cascade-SAS/OCCT` tag `V7_6_0`).
2. **openvdb/6.2.1** — fork commit `a68fd58d0e2b85f01adeb8b13d7555183ab10aa5` from `tamasmeszaros/openvdb`; copy the ConanCenter 12.1.1 recipe shape but expect patches.
3. **opencv/4.6.0** — app uses `opencv_world` core only; trim features hard.
4. **tbb/2021.5** (or adopt `onetbb/2023.1.0` and verify `TBB::tbb`/`TBB::tbbmalloc` target names).
5. **openssl/1.1.1w**, **libcurl/7.75.0** (center's recipe is `libcurl`, verify target names `libcurl`/`OpenSSL::SSL`/`OpenSSL::Crypto` match `find_package` usage), **freetype/2.12.1**, **opencsg/1.4.2**, **openexr/2.5.5**, **nlopt/2.5.0**.
- Each recipe must pass a link+runtime smoke test before being accepted (the "behavior and tests" gate).

### Step 3 — Port the full target graph
- `libslic3r`: already scaffolded; compile it (the long pole, ~141 files, heavy templates). Expect `libslic3r_cgal` frounding-math handling, `-DUSE_TBB`, OCCT/OpenCV/OpenVDB link adjustments, MSVC `Psapi.lib`/`bcrypt.lib`.
- `libslic3r_gui`: replace the 700-file list with the GUI glob (already scaffolded); WebView2 include dir exists at `deps/WebView2/include`.
- `OrcaSlicer` exe + `OrcaSlicer_app_gui` shim: `.rc`/`.manifest`/`Info.plist` configfiles, `/MANIFEST:NO`, `/DEBUG` in release, DLL copying.
- Tests: port `tests/` Catch2 to `xmake test` (keep ctest working in parallel).

### Step 4 — PNP integration (already scaffolded, dist verified present)
- `xmake pnp` → `cargo xtask dist`; `after_build` on `OrcaSlicer` copies `pnp_cli` + `modules/` beside the exe (mirrors `src/CMakeLists.txt:263-278`).
- **Verified:** `pinch_n_print_cli/target/dist/` already contains `pnp_cli.exe` + `modules/`, so the `after_build` staging can be tested immediately.

### Step 5 — Packaging parity, then cutover
- Portable dir first (exe + resources + dlls + pnp_cli + modules), then NSIS (Windows) and mac/linux packages, then delete CMake only after parity.

---

## 7. Validation commands

```bash
# scratch proof still works (if scratch dir preserved)
cd C:/Users/agpen/AppData/Local/Temp/opencode/pnp-build-proof/conan-hybrid
xmake f -c -y -p windows -a x64 -m release && xmake && ./build/windows/x64/release/wxproof.exe

# scaffold configures (repo root; full dep resolution ~cached now)
xmake f -c -y -p windows -a x64 -m release

# conan cache health
conan list "*:*"

# git-hash define works
grep -rn "GIT_COMMIT_HASH" build/.gens 2>/dev/null | head -2   # or inspect a compile command
```

---

## 8. Open questions for the next session

1. **Fork-patch equivalence:** does the app need the 12 SoftFever wxWidgets commits (dark theme etc.) or does vanilla 3.3.2 + the custom flags suffice? Decide via a GUI smoke test (launch, dark theme, dialogs). If patches are needed, add a Conan recipe with `conan-data` patches rather than a fork.
2. **Static vs dynamic runtime:** confirm `SLIC3R_STATIC` semantics with an actual link of `libslic3r` (the wx static proof linked, but the full app is unproven).
3. **TBB version:** adopt `onetbb/2023.1.0` (center) or write a 2021.5 recipe — depends on whether `TBB::tbbmalloc` name and ABI hold.
4. **libnoise:** SoftFever fork vs center 1.0.0 — behavior gate.
5. **vcpkg as fallback** (documented in research) only if a specific Conan recipe fails irreparably.
