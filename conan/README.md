# Conan provisioning (`conan/`)

Repo-owned Conan 2 configuration for the Xmake+Conan build
(ADR: `docs/adr/0001-xmake-conan-build-system.md` incl. its amendment,
state: `docs/BUILD_SYSTEM_HANDOFF.md`).

## Architecture: one consolidated graph

All third-party C/C++ dependencies are resolved by **one** `conan install` of
`conan/conanfile.py` — a single coherent graph, enforced by `conan/conan.lock`
and the checked-in profiles. The xmake rule `pnp.conan` (in `xmake.lua`) runs
the install when its inputs change and injects per-package flags into targets
from the generated `pnp_deps.lua`.

This replaces xmake's stock per-package `add_requires("conan::...")`
integration (ADR-0001 amendment). The stock flow runs one *isolated* conan
install per package, so separate graphs resolved conflicting transitive
versions — opencascade pulling freetype/2.13.2 beside the project's 2.12.1,
cgal pulling boost/1.83.0 + eigen/3.4.0, libcurl free to pick openssl 3.x —
and neither the lockfile nor `[replace_requires]` could take effect.

```
conan/conanfile.py    requires + options + generate() -> pnp_deps.lua
conan/profile_host.txt settings contract: cppstd=17, /MD on Windows,
                       Ninja generator, [replace_requires] steering
conan/profile_build.txt tool context (defaults)
conan/conan.lock       pinned versions + recipe revisions (enforced)
xmake.lua rule pnp.conan  runs install when stale, injects flags per target
                          via set_values("pnp.conan.packages", ...)
```

## Version policy (ADR-0001)

ConanCenter still *serves* many versions absent from its "latest" listing —
probe with `conan download <ref> -r conancenter --only-recipe` before assuming
a custom recipe is needed.

| Dependency | deps/ pin | Provisioned | Note |
|---|---|---|---|
| wxWidgets | 3.3.2 fork | 3.3.2 exact, **repo recipe** | `recipes/wxwidgets/` (see below); vanilla source + custom wxUSE flags — fork-patch equivalence pending GUI smoke test |
| boost | 1.84.0 | exact | **compiled** (not header-only) + static — the app links 13 components, `CMakeLists.txt:618` |
| eigen | 5.0.1 | exact | |
| cereal | 1.3.0 | exact | |
| draco / qhull / glfw / cgal / libnoise | — | exact | |
| opencascade | 7.6.0 | exact, **repo recipe** | `recipes/opencascade/` — center 7.6.0 recipe with tcl/tk removed + `BUILD_MODULE_Draw=OFF` (tcl cannot build under VS 2026 and only serves Draw; deps/ likewise built Draw-less without tcl). Static everywhere (deps/ built Shared on Windows — deliberate divergence, behavior-gated) |
| freetype | 2.12.1 | exact | via `[replace_requires]` (occt's recipe pins 2.13.2) |
| openssl | 1.1.1w | exact | |
| expat | (vendored) | 2.8.2 | wxwidgets' transitive version, kept single |
| libcurl | 7.75.0 | 7.86.0 | nearest served; behavior gate |
| onetbb | 2021.5.0 | 2021.7.0 | nearest served; behavior gate |
| opencv | 4.6.0 | 4.5.5 | nearest served; core+imgproc+imgcodecs only; behavior gate |
| nlopt | 2.5.0 | 2.9.1 | 2.7.1 served but its CMakeLists is rejected by CMake 4; behavior gate |
| libjpeg-turbo | 3.0.1 | 3.0.2 | NOT vanilla libjpeg — `Thumbnails.cpp` needs `JCS_EXT_RGBA`. wx/libtiff/opencv must all agree or conan raises a `provides` conflict |
| nanosvg | SoftFever fork | **vendored** | `deps_src/nanosvg`; center's package lacks `nsvgRasterizeXY` |
| openvdb / opencsg / openexr / glew | (deps/ built) | **dropped** | only served SLA-era code that is dead in this fork |

Two repo recipes, each a thin documented delta over the ConanCenter recipe
(the header of each `conanfile.py` states exactly what changed and why):

- **`recipes/opencascade/`** — center 7.6.0 minus tcl/tk, `BUILD_MODULE_Draw=OFF`.
  tcl cannot be built under VS 2026 and only serves OCCT's Draw test harness.
- **`recipes/wxwidgets/`** — center 3.3.2 plus: private headers installed
  (`wx/generic/private` etc., as `deps/wxWidgets/wxWidgets.cmake:60-80` did);
  `wxUSE_SECRETSTORE` enabled on Windows/macOS native backends (center gates it
  on libsecret and drops the option off-Linux); `wxBUILD_DEBUG_LEVEL=0` to match
  the app's `-DwxDEBUG_LEVEL=0`; `libsoup` required only on Linux/FreeBSD (it
  backs the GTK/WebKit webview, not Edge/WebView2).

Repo recipes pin `version` in-recipe and are auto-exported by the
`pnp.conan` rule before each install; after changing one, regenerate the
lockfile with `--lockfile=""` (the stale lock beside the conanfile is
auto-loaded otherwise and pins the old recipe revision).

## Regenerating the lockfile

After changing requires/options in `conanfile.py`:

```bash
conan lock create conan/conanfile.py --lockfile="" \
    --profile:host=conan/profile_host.txt \
    --profile:build=conan/profile_build.txt \
    --lockfile-out=conan/conan.lock
```

`--lockfile=""` is required: `conan lock create` silently auto-loads the
existing `conan.lock` sitting beside the conanfile, which pins the old recipe
revisions and makes recipe edits appear to have no effect.

## Settings contract

- `compiler.cppstd=17` — the app is C++17. (Conan's compatibility plugin may
  reuse cppstd=14-built binaries where ABI-compatible; that is expected.)
- **Dynamic MSVC runtime (/MD)** — parity with the authoritative CMake build,
  which never overrides the runtime (`SLIC3R_STATIC` means static *libraries*,
  not static CRT). `xmake.lua` sets `set_runtimes("MD"/"MDd")` to match.
- Debug builds: the checked-in host profile is Release; a debug graph needs a
  debug host profile (MDd deps) — not set up yet, `pnp.conan` warns.
- Ninja generator conf — recipes pinning cmake<4 cannot drive the
  "Visual Studio 18 2026" generator.

## Binary cache story

- **Local cache** (`~/.conan2`) is primary. Never `conan remove` it to "clean
  up". Settings changes reuse compatible binaries where possible; the rest
  rebuild once.
- **CI**: GitHub Actions cache is an evictable optimization only;
  `--build=missing` is the correctness fallback (source builds always work).
- A Conan remote (Artifactory/`conan server`) for published binaries is the
  durable option if CI rebuild times become a problem; not set up yet.
