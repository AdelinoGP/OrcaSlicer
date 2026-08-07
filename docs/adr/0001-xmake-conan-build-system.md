# ADR-0001: Xmake frontend with Conan 2 dependency provisioning replaces CMake/deps/

The fork replaces its CMake build (`CMakeLists.txt` + `deps/` ExternalProject tree) with a two-layer cargo-adjacent build: **Xmake** owns the C++ target graph, source discovery, tests, staging, and packaging; **Conan 2** owns third-party C/C++ dependencies; Cargo remains authoritative inside `pinch_n_print_cli`. CMake and `deps/` stay in-tree and untouched until cross-platform build/test/package parity is demonstrated.

## Status

**Accepted and executed (2026-08-07).** CMake and `deps/` are removed from the
repository; xmake + Conan is the only build system.

Cutover state:

| Platform | Build | Tests | Portable | Installer |
|---|---|---|---|---|
| Windows x64 | verified | 7/7 | verified | NSIS, verified |
| macOS | **unverified** | — | written, unrun | dmg written, unrun |
| Linux | **unverified** | — | written, unrun | AppImage written, unrun |

The last commit with the CMake build intact is tagged **`pre-xmake-cutover`**.

The original plan gated deletion on cross-platform parity. That gate was
**waived deliberately**: parity on macOS/Linux cannot be demonstrated from the
only available host (Windows), so holding CMake for it meant holding it
indefinitely. The cost is explicit — those platforms have no working build and
no fallback until someone with that hardware finishes the port.

**flatpak — ported, but local/CI only (2026-08-07).** The manifest now builds
with xmake + Conan; the `wxWidgets` and `orca_deps` modules and their ~15
vendored dependency archives are gone. It declares
`build-args: [--share=network]` so Conan can resolve the graph, which makes it
**Flathub-incompatible** — Flathub forbids build-time network. That was an
explicit decision: this fork builds flatpaks locally and in CI and does not
publish to Flathub (the `app-id` in the manifest is upstream's). Restoring
Flathub support means vendoring the sources of all 30 transitive Conan packages
plus an offline Conan strategy, and a distinct app-id. `build_flatpak.sh` moved
to `scripts/flatpak/build.sh`. None of it has been executed.

**Not carried over:**
- **macOS signing, notarization, universal binaries, and release deploy** from
  the old CI. Secret- and Apple-tooling-dependent, orthogonal to the build
  system, and unexercisable here. The fork has no Apple Developer identity, so
  dmgs are unsigned and need a right-click-Open on first launch; note the
  upstream signing step was gated on `github.repository == 'OrcaSlicer/OrcaSlicer'`
  and so never ran in this fork even before the cutover. Universal binaries
  came from `build_release_macos.sh -a universal`; CI now ships a single arm64
  artifact.

**Superseded proof-stage note:** Proposed (proof stage: Windows x64 wxWidgets
3.3.2 static build passes).

**Amended 2026-08-06 — consolidated dependency graph.** Decision 2 originally
wired Conan through xmake's stock `add_requires("conan::...")`. That integration
runs one *isolated* `conan install` per package, which proved structurally
unsound: separate graphs resolve conflicting transitive versions (opencascade
pulling freetype/2.13.2 beside the project's pinned 2.12.1; cgal pulling
boost/1.83.0 + eigen/3.4.0; libcurl free to resolve openssl 3.x), and neither
the lockfile nor profile `[replace_requires]` can take effect across graphs.
Replaced by ONE `conan install` of the repo-owned `conan/conanfile.py`
(lockfile-enforced, checked-in profiles), whose `generate()` emits per-package
cpp_info + direct-dependency names (`pnp_deps.lua`); the xmake rule `pnp.conan`
runs the install when stale and injects flags into targets. See
`conan/README.md`.

**Findings that shrank the custom-recipe list from ten to one** (2026-08-06):
ConanCenter still *serves* recipe versions absent from its listing — exact
pins found for opencascade/7.6.0, openssl/1.1.1w, freetype/2.12.1,
openexr/2.5.5, cereal/1.3.0. The remaining gaps (openvdb fork, opencsg,
openexr, glew) turned out to provision only SLA-era code that is dead in this
fork and were dropped. Nearest-upstream (behavior-gated): libcurl 7.86.0,
onetbb 2021.7.0, opencv 4.5.5, nlopt 2.9.1. The one repo recipe
(`conan/recipes/opencascade/`) is the center 7.6.0 recipe minus tcl/tk with
`BUILD_MODULE_Draw=OFF`: tcl cannot be built under VS 18 2026 and exists only
for OCCT's Draw test harness (deps/ likewise built OCCT Draw-less, tcl-free).

**MSVC runtime correction:** the Consequences section below assumed
`SLIC3R_STATIC=1` implies a static CRT. The authoritative CMake build never
overrides the MSVC runtime (defaults to /MD); `SLIC3R_STATIC` means static
*libraries*. Profiles use `compiler.runtime=dynamic`.

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
  - *Resolved (2026-08-06):* only two repo recipes were needed — see the amendment above.

## Open questions

1. **~~wxWidgets fork-patch equivalence~~ — CLOSED (2026-08-07).** Vanilla
   wxWidgets 3.3.2 plus the custom flag set is visually indistinguishable from
   the SoftFever fork build: the packaged application was compared against the
   previous CMake build and reported as looking "exactly the same". No fork
   patches are required.
2. **Behavior gates still open.** These deps diverge from the `deps/` pins and
   have not been exercised beyond "builds and the test suites pass":
   - CGAL exact-arithmetic kernel: `Mpzf` instead of `Quotient<Gmpzf>`
     (`CGAL_DO_NOT_USE_MPZF` had to be dropped to avoid a `boost::operators`
     C2666 on cl >= 19.40). Needs mesh-boolean validation.
   - Nearest-upstream picks: libcurl 7.86.0, opencv 4.5.5, nlopt 2.9.1,
     onetbb 2021.7.0, cereal 1.3.0.
   - OCCT built static on Windows where `deps/` built it shared.
   - libnoise from center rather than the fork.
3. **Debug builds.** `conan/profile_host.txt` builds Release dependencies; an
   xmake debug build would link a debug app against release deps and fail on
   MSVC. `pnp.conan` warns. Needs a debug host profile.
4. **macOS/Linux.** Everything: dependency resolution (no lockfiles, GTK/
   fontconfig/dbus untested), compilation, the `.app` bundle, the FHS layout,
   AppImage and dmg. All written, none run.
5. **flatpak on Flathub.** The manifest builds with network access, which
   Flathub forbids. Publishing there again needs the 30 transitive Conan
   package sources vendored as flatpak sources, an offline Conan strategy, and
   a fork-specific app-id.
6. **Version mirror.** `version.inc` is the source of truth, but xmake's
   description scope has no file I/O at all (`io` is nil; `os` exposes only
   `isfile`/`mtime`/`filesize`) while `add_configfiles` needs the values there.
   `xmake.lua` therefore carries a literal mirror guarded by the
   `version_guard` rule, which fails the build on drift. A cleaner fix would be
   a Lua version file that xmake can `includes()`, at the cost of changing what
   the flatpak/msix scripts grep.
