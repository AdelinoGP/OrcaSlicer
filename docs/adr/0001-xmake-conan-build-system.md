# ADR-0001: Xmake frontend with Conan 2 dependency provisioning replaces CMake/deps/

The fork replaces its CMake build (`CMakeLists.txt` + `deps/` ExternalProject tree) with a two-layer cargo-adjacent build: **Xmake** owns the C++ target graph, source discovery, tests, staging, and packaging; **Conan 2** owns third-party C/C++ dependencies; Cargo remains authoritative inside `pinch_n_print_cli`. CMake and `deps/` stay in-tree and untouched until cross-platform build/test/package parity is demonstrated.

## Status

Proposed (proof stage: Windows x64 wxWidgets 3.3.2 static build passes).

## Context

The GUI shell-out fork (`Orca(pnp_gui)`) inherits OrcaSlicer's CMake build: ~1,110 C++ files in a manually maintained 700-file source list, ~30 dependencies built from source by `ExternalProject_Add` into a staged prefix, per-platform shell scripts, and a two-language orchestration problem (CMake + `cargo xtask dist` + staging). The goal is one developer-facing command, declarative dependency management, automatic source discovery, and no hand-maintained build scripts.

Grilling session constraints agreed: (1) all three platforms must build from a clean checkout; (2) the replacement must accept nearest-upstream dependency versions when behavior and tests pass, keeping repository-owned recipes where necessary; (3) binary caching for normal developer builds, source-build fallback; (4) portable app directory first, installers after; (5) CMake stays authoritative until build/test/package parity; (6) automatic source globs instead of explicit manifests; (7) one command builds binaries, tests, and packages.

## Proof findings (Windows x64, VS 2026, MSVC)

### Pure Xmake/Xrepo — fails the wxWidgets requirement

- xrepo's `wxwidgets` package on Windows ships **prebuilt shared DLLs only** (wxMSW-3.2.x_vc14x_Dev.7z), max version **3.2.4** (`configs` `shared`/`vs_runtime`/`debug` are `readonly = true`).
- The project requires **static** wxWidgets from the `SoftFever/Orca-deps-wxWidgets` fork at `v3.3.2` (12 Orca-specific commits: dark theme, font/grid fixes, ARM64 webview, gstplayer disable), built with `wxBUILD_SHARED=OFF`, `wxUSE_WEBVIEW_EDGE=ON`, `wxUSE_PRIVATE_FONTS=ON`, `wxUSE_MEDIACTRL=ON`, `wxUSE_OPENGL=ON`, plus private-headers install step.
- A minimal wxWidgets app does build via xrepo (3.2.4 prebuilt), but it cannot express the fork/static/3.3.2/WebView-Edge combination. Verdict: **not feasible** as the sole dependency provider without maintaining a private xrepo fork of wxWidgets, which defeats the purpose.

### Xmake + Conan 2 — passes

- ConanCenter has a `wxwidgets` recipe for **3.3.2/3.3.3** (and 3.2.x) with `shared`, `webview`, `mediactrl`, `opengl`, `aui`, `html`, `stc`, `cairo` options, MSVC static-runtime support, and `custom_enables`/`custom_disables` to pass arbitrary `wxUSE_*` flags.
- With xmake `add_requires("conan::wxwidgets/3.3.2", {configs = {...}})`, xmake generates the Conan 2 `conanfile.txt` + host/build profiles, runs `conan install`, and links the produced libs. The proof app compiled and linked against **static wxWidgets 3.3.2** built from source (ninja generator required on VS 2026 — see Consequences).
- Versions needed but **not** on ConanCenter at the pinned project versions: `occt/7.6.0` (center has 7.9.1), `opencv/4.6.0` (center has 4.14.0), `openvdb` 6.2.1 fork from `tamasmeszaros` (center has 12.1.1 only), `boost/1.84.0` (center has 1.84.0 — OK), `tbb` 2021.5 (center `tbb` tops at 2020.3; `onetbb` has 2023.1.0), `openssl/1.1.1w` (center has 3.x+), `curl` 7.75 (center has 8.21.0), `libcurl` name, `freetype` 2.12.1 (center has 2.13+), `opencsg` 1.4.2 (center has **no** recipe), `openexr` 2.5.5 (center has 2.5.7+), `libnoise` SoftFever fork (center has 1.0.0). These are the candidates for custom Conan recipes in-repo, to be accepted/rejected per the behavior-and-tests gate.

## Decision

1. **Build frontend: Xmake.** Sole developer-facing command (`xmake`, `xmake f`, `xmake test`, custom tasks for `dist`/`package`). Source discovery by directory globs. Invokes `cargo xtask dist` and stages `pnp_cli` + `modules/` beside the executable by default.
2. **Dependency provider: Conan 2.** `add_requires("conan::...")` per dependency, with a repo-owned Conan home / profile / lockfile for reproducibility and offline cache. Repository recipes under `conan/recipes/` for anything ConanCenter cannot reproduce (the fork-patched and version-pinned set above).
3. **Rust backend: unchanged.** Cargo workspace and `cargo xtask dist` remain authoritative; Xmake only orchestrates.
4. **Migration safety: CMake and `deps/` remain** until the Xmake build reaches parity on Windows (x64 first, then ARM64), then macOS and Linux — same targets, tests pass, portable layout identical, installers launch.

## Considered Options

- **Cabin / bpt** — cargo-like build+package drivers, but pre-1.0 with private/alpha registries; betting the release pipeline on them is not acceptable for a shipping GUI.
- **Meson + WrapDB** — viable build system but no wrap for wxWidgets 3.3.2, OCCT, CGAL, or OpenVDB on WrapDB; would still require owning source builds; rejected for higher translation cost.
- **xmake with pure xrepo** — rejected by the wxWidgets proof (prebuilt 3.2.4 shared-only, readonly configs, no fork).
- **vcpkg manifest** — strong Windows alternative, all heavy deps present, but the fork-patched wxWidgets 3.3.2 and OpenVDB fork would still need overlay ports; Conan was proven end-to-end first.

## Consequences

- **VS 2026 generator bug:** ConanCenter's wxWidgets recipe pins `cmake/[>=3.17 <4]` (3.31.12) which lacks the "Visual Studio 18 2026" generator. Fixed by forcing `tools.cmake.cmaketoolchain:generator=Ninja` via Conan conf in the xmake `configs`. The ninja generator works on all supported cmake versions and platforms.
- **MSVC static runtime:** project default is `SLIC3R_STATIC=1`; Conan profiles must set `compiler.runtime=static` (or the recipe's `is_msvc_static_runtime`) so wxWidgets and app share the same runtime.
- **Conan's default `compiler.cppstd=14` profile** must be raised to C++17 to match the app.
- **Long first builds** (wxWidgets from source on MSVC is ~30+ min; its gettext/iconv/pcre2 transitive build is the bulk). Mitigated by Conan binary cache and CI-published binaries.
- **License/ABI drift risk** when accepting nearest-upstream versions (OCCT 7.9.1 vs 7.6.0, OpenCV 4.14 vs 4.6.0, OpenVDB 12 vs fork 6.2.1). Each must pass the behavior-and-tests gate before replacing the pinned dep.
- **Custom recipes needed** for: wxWidgets fork patches (if ConanCenter's vanilla 3.3.2 proves insufficient — proof passed with vanilla + custom flags, fork-patch equivalence still TBD), OCCT 7.6.0, OpenCV 4.6.0, OpenVDB 6.2.1 fork, TBB 2021.5 (or adopt onetbb), OpenSSL 1.1.1w, curl 7.75, OpenCSG (no recipe), freetype 2.12.1, libnoise fork, OpenEXR 2.5.5.
