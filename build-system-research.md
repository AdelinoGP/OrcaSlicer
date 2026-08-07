# C/C++ Build and Dependency Options

Research snapshot: 2026-08-06

## Executive Conclusion

Do not replace the project's build systems wholesale. The lowest-risk architecture is:

```text
Conan 2 or vcpkg manifest mode -> third-party C/C++ dependencies
CMake                         -> OrcaSlicer GUI, native targets, install/package rules
Cargo                         -> pinch_n_print_cli workspace and cargo xtask dist
```

Conan 2 is the first package-manager candidate to prototype. vcpkg manifest mode is a strong Windows/MSVC alternative and may be preferable if the project wants Microsoft-oriented tooling. CMake should remain the application build and packaging boundary. `cargo-c` is only relevant if the Rust workspace later exports a C ABI library; it does not replace the current executable-oriented `cargo xtask dist` flow.

Meson, xmake, Cabin, bpt, and Zig build are technically capable tools, but a full migration would replace a large amount of working CMake-specific behavior without solving the main dependency-management problem more safely.

## Project Constraints

The recommendation is based on the current tree, not on a generic C++ project:

- `CMakeLists.txt` sets a CMake 3.13 compatibility floor, C++17, static-library options, platform flags, GUI options, tests, and install behavior.
- `deps/CMakeLists.txt::orcaslicer_add_cmake_project` uses `ExternalProject_Add` to build dependencies into a staged `DESTDIR` prefix, with compiler, generator, configuration, and static-link settings forwarded from the main build.
- `src/CMakeLists.txt` consumes the staged prefix with `find_package`, including a component-heavy wxWidgets lookup and platform-specific link fixes.
- The Windows build has custom DLL copying, resource junctions, manifests, wrapper executables, and MSVC-specific linker options.
- `PNP_BUNDLE_CLI` and `PNP_DIST_DIR` stage `pnp_cli.exe` and `modules/` beside the GUI executable. The flat runtime layout must remain identical in both build and install trees.
- `pinch_n_print_cli/Cargo.toml` is a large Rust workspace. `cargo xtask dist` builds the backend and stages its distribution tree; the C++ dependency manager should not absorb or rewrite that workspace.
- The application installer, translations, resources, generated manifests, tests, and runtime DLL layout are application concerns. A C/C++ package manager supplies dependencies; it does not automatically replace those rules.

## Comparison

| Candidate | Primary role | Relative fit | Main result for this tree |
| --- | --- | --- | --- |
| Conan 2 | C/C++ package manager and recipe/binary system | High | Best first prototype while retaining CMake and Cargo |
| vcpkg manifest mode | C/C++ package manager with CMake/MSBuild integration | High | Strong Windows option; use custom triplets and overlay ports as needed |
| CMake FetchContent / CPM.cmake | CMake-native source acquisition | Medium | Smallest conceptual change, but not a binary package solution |
| xmake | Lua build system and package manager | Medium-low | Capable, but requires a broad build-file rewrite |
| Meson / WrapDB | Build system and source-wrap mechanism | Low-medium | Good build system, weak fit for these large CMake dependencies |
| cargo-c | Cargo subcommand for C ABI libraries | Low for current app | Useful only for a future Rust library ABI, not `pnp_cli` |
| Cabin | Cargo-inspired C/C++ build/package system | Low | Promising interface, but pre-1.0 and registry is private alpha |
| bpt | C/C++ build and library manager | Low | Smaller ecosystem and alpha project state require owning more risk |
| Zig build | Build graph and package-aware build system | Low | Would replace CMake and has no verified required-library path |

"Relative fit" is an engineering judgment based on the repository constraints. It is not a benchmark.

## Conan 2

### Verified capabilities

Conan describes itself as usable with any build system. Its standard CMake path is a `conanfile.py` or `conanfile.txt`, a compiler/platform profile, `conan install`, `CMakeToolchain`, and `CMakeDeps`.

- `CMakeToolchain` translates Conan settings and options into a CMake toolchain file, including generator/platform/toolset, C++ standard, MSVC runtime, `/MP`, compiler paths, and cross-build settings.
- `CMakeDeps` generates files consumed by CMake's `find_package`, including multi-configuration support for Visual Studio.
- Conan recipes expose `build`, `generate`, `package`, `package_info`, `requirements`, `validate`, and deployment hooks.
- The official CMake tutorial demonstrates Visual Studio generation and `cmake --build --config Release`.
- Conan's current documentation is version 2.31.1 and ConanCenter's current repository is the source index for its recipes.

Sources:

- [Conan CMake tutorial](https://docs.conan.io/2/tutorial/consuming_packages/build_simple_cmake_project.html)
- [Conan recipe methods](https://docs.conan.io/2/reference/conanfile/methods.html)
- [CMakeToolchain](https://docs.conan.io/2/reference/tools/cmake/cmaketoolchain.html)
- [CMakeDeps](https://docs.conan.io/2/reference/tools/cmake/cmakedeps.html)
- [ConanCenter Index](https://github.com/conan-io/conan-center-index)

### Required dependency coverage

ConanCenter recipes were located for all four required libraries:

- [wxWidgets recipe](https://raw.githubusercontent.com/conan-io/conan-center-index/master/recipes/wxwidgets/all/conanfile.py)
- [OpenCASCADE recipe](https://raw.githubusercontent.com/conan-io/conan-center-index/master/recipes/opencascade/all/conanfile.py)
- [CGAL recipe](https://raw.githubusercontent.com/conan-io/conan-center-index/master/recipes/cgal/all/conanfile.py)
- [OpenVDB recipe](https://raw.githubusercontent.com/conan-io/conan-center-index/master/recipes/openvdb/all/conanfile.py)

Recipe existence does not prove that the project's exact static, compiler, feature, or ABI combination works. That requires a build proof.

### Fit and migration shape

Conan can replace the dependency acquisition layer while leaving the application target graph, manual GUI source lists, install rules, and Rust workspace alone. A likely first shape is:

1. Add a root `conanfile.py` with the required C/C++ dependencies and explicit static/debug/profile options.
2. Generate Conan files in a dedicated build directory.
3. Configure CMake with the generated `conan_toolchain.cmake` and keep explicit `find_package` calls.
4. Map recipe target names/components with `CMakeDeps.set_property` where upstream names differ from this tree.
5. Move dependencies from `deps/CMakeLists.txt` one at a time, retaining the old `ExternalProject` path until each dependency has passed the platform matrix.
6. Leave `cargo xtask dist`, `PNP_DIST_DIR`, and the existing flat install layout unchanged.

The current CMake floor is older than the CMake versions required by some generated preset schemas. The initial integration should pass `-DCMAKE_TOOLCHAIN_FILE=...` directly rather than depending on generated presets; raise the CMake floor separately if presets are desired.

### Risks

- Recipe defaults may not match the project's static-link, MSVC runtime, GTK, wxWidgets WebView, OpenVDB, CGAL, or Boost requirements.
- A package being available in ConanCenter does not guarantee a compatible binary is available; CI may build it from source.
- Conan's generated CMake target names must be checked against existing `find_package` and link code.
- Conan can help collect runtime dependencies, but the project must preserve its own Windows DLL copying and PNP backend staging rules.

## vcpkg Manifest Mode

### Verified capabilities

Microsoft recommends manifest mode for most users. A project declares dependencies in `vcpkg.json`; `vcpkg-configuration.json` can pin a registry baseline and add registries, overlay ports, or overlay triplets.

- Manifest mode creates a project-local `vcpkg_installed` tree instead of using one global classic-mode installation.
- Version constraints and overrides are available in the manifest.
- Advanced versioning and custom registries require manifest mode.
- The CMake integration supplies a toolchain file and package discovery through normal CMake mechanisms.
- Overlay ports are a drop-in mechanism for local recipes and patches without forking the main registry.
- The official repository describes support for CMake, MSBuild, and other build systems, plus binary and asset caching.

Sources:

- [Manifest mode](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode)
- [Manifest reference](https://learn.microsoft.com/en-us/vcpkg/reference/vcpkg-json)
- [Versioning](https://learn.microsoft.com/en-us/vcpkg/users/versioning)
- [CMake integration](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration)
- [Overlay ports](https://learn.microsoft.com/en-us/vcpkg/concepts/overlay-ports)
- [Registries](https://learn.microsoft.com/en-us/vcpkg/concepts/registries)
- [vcpkg repository overview](https://github.com/microsoft/vcpkg)

### Required dependency coverage

The vcpkg package pages currently expose these package versions:

| Dependency | Current package page result |
| --- | --- |
| wxWidgets | `3.3.1#1` |
| OpenCASCADE | `8.0.1` |
| CGAL | `6.2` |
| OpenVDB | `12.0.1` |

Sources: [wxWidgets](https://vcpkg.io/en/package/wxwidgets.html), [OpenCASCADE](https://vcpkg.io/en/package/opencascade.html), [CGAL](https://vcpkg.io/en/package/cgal.html), and [OpenVDB](https://vcpkg.io/en/package/openvdb.html).

The pages also show important transitive dependencies and port patches. For example, OpenVDB pulls Boost, Imath, OpenEXR, TBB, and Blosc; OpenCASCADE has CMake patches and optional TBB support; CGAL pulls a large Boost graph. Compatibility still needs to be tested against this project.

### Fit and migration shape

vcpkg is a strong alternative if Windows/MSVC developer experience is the primary criterion. The migration would be similar to Conan at the CMake boundary:

- Add `vcpkg.json` and a pinned `vcpkg-configuration.json` baseline.
- Choose a custom triplet for static/shared behavior and the MSVC runtime.
- Point CMake at the vcpkg toolchain before the first `project()` call.
- Use normal `find_package` and imported targets.
- Add overlay ports for project-specific patches or unsupported feature combinations.
- Keep the existing install/package rules and Cargo staging untouched.

The main tradeoff is that vcpkg's current package versions and triplet behavior become part of the project contract. Existing `ExternalProject` patches and destination-prefix assumptions may need to move into overlay ports or custom triplets.

## CMake FetchContent and CPM.cmake

CMake identifies `find_package()` and `FetchContent` as its primary dependency mechanisms. `FetchContent` downloads/populates content during configuration and can add a CMake dependency with `add_subdirectory`; `ExternalProject_Add` performs its download/configure/build steps at build time.

The official documentation recommends pinned content, preferably by commit hash or archive hash. CMake 3.24+ also supports trying `find_package` first and falling back to `FetchContent`, which can support a hybrid developer/CI setup.

CPM.cmake is a thin wrapper around `FetchContent` that adds a compact declaration API, version control, and caching. It is not a separate binary package ecosystem.

Sources:

- [CMake Using Dependencies Guide](https://cmake.org/cmake/help/latest/guide/using-dependencies/index.html)
- [CMake FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)
- [CMake ExternalProject](https://cmake.org/cmake/help/latest/module/ExternalProject.html)
- [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake)

### Fit

This is the smallest change for small CMake-native dependencies, but it is not a replacement for a package manager. It does not provide Conan-style binary settings/profiles or vcpkg-style registries. Applying it to wxWidgets, OpenCASCADE, CGAL, and OpenVDB would move their source builds into the main configure/build graph and would require validating target collisions, options, install behavior, and build-tree size.

The practical use here is selective: retain `ExternalProject` or a package manager for heavyweight dependencies and use FetchContent/CPM for small, self-contained dependencies or test-only libraries.

## xmake

xmake is a Lua-based cross-platform build utility with integrated package management. Its official package documentation verifies:

- `add_requires` declares package requirements and `add_packages` applies them to targets.
- Packages can be fetched, built, installed, and linked automatically.
- Semantic version constraints, multiple repositories, cross-platform package variants, debug packages, package configuration values, private repositories, and `xmake-requires.lock` are supported.
- The homepage advertises integration with Conan and vcpkg.

Sources:

- [xmake homepage](https://xmake.io/)
- [Official package management](https://xmake.io/guide/package-management/using-official-packages.html)
- [Xmake Packages](https://packages.xmake.io/)

### Fit

xmake would be a full build-system migration, not just a dependency-manager change. The current tree contains platform-specific CMake logic for MSVC, resource manifests, DLL copying, install layout, gettext, custom targets, and a large manually maintained source graph. Recreating those rules in Lua is possible, but it increases migration surface and creates a second long-lived build language during the transition.

The official xmake package page does not by itself prove that the four required dependencies have compatible recipes for this project's exact configurations. That package coverage remains `unverified`.

## Meson and WrapDB

Meson is a build system. Its `dependency()` function first searches system dependencies, commonly through pkg-config or CMake package configuration, and can fall back to subprojects. Wrap files can download source archives or VCS projects and can apply overlays and patches.

Meson supports CMake and Cargo wraps, but its own documentation labels both as experimental with no backward or forward compatibility guarantees. WrapDB is a source-wrap repository, not a Conan/vcpkg-style binary package cache.

Sources:

- [Meson dependencies](https://mesonbuild.com/Dependencies.html)
- [Wrap dependency system](https://mesonbuild.com/Wrap-dependency-system-manual.html)
- [Using WrapDB](https://mesonbuild.com/Using-the-WrapDB.html)
- [CMake module](https://mesonbuild.com/CMake-module.html)
- [Rust module](https://mesonbuild.com/Rust-module.html)
- [Mixing build systems](https://mesonbuild.com/Mixing-build-systems.html)
- [WrapDB subprojects](https://github.com/mesonbuild/wrapdb/tree/master/subprojects)

The current WrapDB `subprojects` listing was checked for `wxwidgets.wrap`, `opencascade.wrap`, `cgal.wrap`, and `openvdb.wrap`; none were present. `catch2.wrap` is present. Therefore, Meson would require project-owned wraps or another dependency source for the main libraries.

### Fit

Meson could produce a clean new build definition, but it would require translating the existing CMake target graph and packaging behavior. It would not reduce the need to own the dependency builds for the four large libraries. It is not the preferred migration path for this repository.

## cargo-c

cargo-c is a Cargo subcommand, not a general C/C++ package manager. The official project states that it builds and installs C-ABI-compatible dynamic and static libraries, generates a C header, and generates a pkg-config file. Its commands include `cargo cbuild`, `cargo ctest`, and `cargo cinstall`.

Source: [cargo-c repository and README](https://github.com/lu-zero/cargo-c)

The current PNP backend is an executable plus staged modules, built through the Rust workspace's `cargo xtask dist`. It does not need a C ABI package, header, or pkg-config file. Adding cargo-c would therefore create an unrelated distribution path. Reconsider it only if a Rust crate becomes a library consumed by the C++ GUI.

## Cabin

Cabin presents a Cargo-inspired manifest, build system, package manager, workspaces, profiles, lockfile, toolchain configuration, tests, metadata inspection, and offline/vendoring features in its documentation.

Sources:

- [Cabin repository](https://github.com/cabinpkg/cabin)
- [Cabin documentation](https://cabinpkg.com/docs/)
- [Cabin lockfile reference](https://cabinpkg.com/docs/lockfile/)
- [Cabin dependency kinds](https://cabinpkg.com/docs/dependency-kinds/)

The repository README explicitly says Cabin is pre-1.0. The documentation also says its registry is private alpha and restricted to allowlisted maintainers. Required-library package coverage and CMake/Rust interoperability were not verified.

Cabin is interesting for a new conventional C/C++ project, but adopting it here would replace the current CMake target and packaging model before its registry and compatibility story are proven against wxWidgets, OpenCASCADE, CGAL, and OpenVDB.

## bpt

bpt describes itself as a build system and library manager for C and C++. Its repository contains declarative `bpt.yaml` and package manifests, a dependency solver, and a project-specific package vocabulary.

Sources:

- [bpt repository](https://github.com/vector-of-bool/bpt)
- [bpt README](https://raw.githubusercontent.com/vector-of-bool/bpt/develop/README.md)
- [bpt sample manifest](https://raw.githubusercontent.com/vector-of-bool/bpt/develop/bpt.yaml)
- [bpt documentation](https://bpt.pizza/docs)

The repository's sample manifest uses alpha-versioned bpt packages and a Catch2 test dependency. Required-library recipes, current release/maturity status, Windows/MSVC behavior, and CMake/Rust integration were not verified to the level needed for this project.

bpt would be a high-risk full build migration. It should not be selected over an incremental package-manager integration without a working four-library proof and a complete Windows packaging proof.

## Zig Build

Zig's official build-system documentation describes a `build.zig` build graph with installable executables/libraries, tests, user options, cross-target selection, system-library linking, generated files, and project tools. The current help surface also includes package dependency fetching and a package tree.

Source: [Zig Build System](https://ziglang.org/learn/build-system/)

The build system is attractive for self-contained, reproducible cross-target builds, but the required C++ package recipes and the exact wxWidgets/OpenCASCADE/CGAL/OpenVDB integration path were not verified. Replacing this project's CMake would also require reimplementing its Windows resource/DLL/install behavior and all application packaging rules. Zig is not recommended for this migration.

## Recommended Migration Sequence

### Phase 1: dependency-manager proof

Use Conan 2 first, with vcpkg as the parallel fallback if Conan recipe options do not match the Windows build.

- Define only the required external libraries initially.
- Configure the same static/shared, Debug/Release/RelWithDebInfo, compiler, architecture, and MSVC runtime combinations used by the repository.
- Keep CMake as the target graph and invoke it with the package-manager toolchain.
- Verify imported target names and component behavior before changing application source.
- Do not move Cargo into the C/C++ package manifest.

### Phase 2: one dependency at a time

Replace the corresponding `ExternalProject_Add` entry only after its package-manager build passes on Windows, Linux, and macOS. Keep the old dependency path available during the transition so a recipe mismatch does not block unrelated development.

Priority order should be based on the actual build graph and transitive complexity, not package popularity. wxWidgets and the geometry/volume libraries deserve an early proof because they exercise GUI components, platform libraries, large dependency trees, static linking, and CMake target compatibility.

### Phase 3: packaging verification

Verify that the package-manager integration does not change:

- `OrcaSlicer` and Windows wrapper executable output names.
- Third-party DLL collection and runtime search paths.
- Resource directory layout and install prefix.
- gettext output and application metadata.
- `pnp_cli.exe` plus `modules/` beside the executable.
- Existing CTest targets and CI build configurations.

### Phase 4: reproducibility and CI

Pin the package-manager registry state, recipe/port revisions, profiles or triplets, and source patches. Cache downloaded sources and built packages in CI, but retain a clean uncached job so accidental workstation state does not hide missing declarations.

## Open Verification Items

The following are still `unverified` and should be answered by a small build prototype rather than documentation:

- Exact ConanCenter target names and options for this project's wxWidgets components.
- Static MSVC runtime compatibility for all required Conan or vcpkg packages.
- OpenVDB/TBB/Boost/Imath/OpenEXR ABI and feature compatibility.
- CGAL's required Boost components and license implications for the intended distribution.
- OpenCASCADE optional components and install/config package behavior.
- Whether the chosen package manager can reproduce the current build-tree and install-tree layout without post-build workarounds.
- Clean Windows, Linux, and macOS builds with the Rust backend staged by `cargo xtask dist`.

## Sources Not Used as Evidence

Repository stars, issue counts, and search-result rankings are discovery signals only. They are not evidence that a package manager can build this project. The recommendation is based on first-party documentation, recipe/port presence, and the actual CMake/Cargo structure in this tree.
