# CLAUDE.md

Orca(pnp_gui) — a fork of OrcaSlicer used as the **GUI frontend** for the
[pinch_n_print_cli](https://github.com/AdelinoGP/pinch_n_print_cli) (PNP) backend. C++17
wxWidgets GUI, **xmake + Conan 2** build system. Windows-first fork.

> **Build system:** CMake was removed (ADR-0001). `xmake.lua` defines the target graph;
> `conan/conanfile.py` provisions every third-party dependency as one lockfile-enforced
> graph. The last commit with CMake intact is tagged `pre-xmake-cutover`.
> **Only Windows x64 is verified** — the macOS and Linux paths exist but have never been
> configured or built.

## Fork reality (differs from upstream OrcaSlicer)

- **No native slicing.** `Print::process` is removed; the GUI shells out to `pnp_cli`
  (`src/slic3r/GUI/PnpBackend.cpp`). Headless `--slice` is gutted — use `pnp_cli` directly.
- **SLA fully removed** — SLA 3MF files are refused.
- **Calibration generators removed** — the calibration UI is a mock (not implemented).
- **Backend staging:** the `pinch_n_print_cli/` submodule holds the Rust backend. `cargo xtask
  dist` stages `pinch_n_print_cli/target/dist/` (`pnp_cli` + `modules/`), which xmake bundles
  beside the executable (`xmake pnp` runs the cargo step). `--pnp_dist_dir=` defaults to the
  submodule dist path; `--pnp_bundle_cli=n` skips bundling. The backend resolves relative to
  the running binary, so build tree and shipped tree must reproduce the flat layout.

## Build Commands

Requires [xmake](https://xmake.io) and Conan 2.31+ on PATH. The first build resolves and
compiles the whole dependency graph from source, which is long and memory-hungry — keep the
job count low.

```bash
xmake f -y -m release        # configure (add -a arm64 to cross-target)
xmake -j2                    # build
xmake test                   # all Catch2 suites
xmake test libslic3r_tests   # one suite
```

Useful options: `--fhs=y` (Linux FHS layout instead of portable), `--prefix=`,
`--slic3r_gui=n`, `--pnp_bundle_cli=n`.

## Packaging

```bash
xmake package          # portable directory (all platforms)
xmake pack -f nsis     # Windows installer  — needs NSIS *with the UAC plugin*
xmake appimage         # Linux AppImage     — UNVERIFIED, needs appimagetool
xmake dmg              # macOS disk image   — UNVERIFIED, needs hdiutil
```

The shipping layout is defined once in `xmake/modules/pnp/layout.lua`; the portable
directory and the installer payload both come from it.

xmake's makensis probe compiles a script that `!include`s `UAC.nsh`, so a stock NSIS
install is rejected even though `installer/OrcaSlicer.nsi` deliberately does not use UAC.
Install the UAC plugin alongside NSIS (CI does this in `build_orca.yml`).

## Testing

Catch2 v3 (vendored in `tests/catch2/`), driven by `xmake test`. Seven suites:
`libslic3r`, `fff_print`, `libnest2d`, `filament_group`, `slic3rutils`,
`pnp_config_translator`, `pnp_runtime`.

## Code Style

- C++17, selective C++20. PascalCase classes, snake_case functions/variables
- `#pragma once` for headers. Smart pointers and RAII preferred
- Parallelization via TBB — be mindful of shared state

## Key Entry Points

- App startup: `src/OrcaSlicer.cpp`
- PNP backend shell-out: `src/slic3r/GUI/PnpBackend.cpp`
- All print/printer/material settings: `src/libslic3r/PrintConfig.cpp`
- GUI: `src/slic3r/GUI/`
- Core algorithms: `src/libslic3r/` (GCode/, Fill/, Support/, Geometry/, Format/, Arachne/)
- Printer profiles: `resources/profiles/[manufacturer].json`

## Critical Constraints

- **Backward compatibility required** for .3mf project files and printer profiles
- **Cross-platform** — all changes must work on Windows, macOS, and Linux. Note the macOS
  and Linux builds are currently **unverified** under xmake; treat breakage there as
  expected until someone with those hosts confirms otherwise.
- Profile/format changes need version migration handling
- Dependencies come from Conan (`conan/conanfile.py`), resolved as one graph and pinned by
  `conan/conan.lock` (Windows). Other platforms need their own `conan/conan-<plat>.lock`;
  without one they resolve unlocked and are not reproducible.
- In-tree third-party sources live in `deps_src/` and are compiled by targets in `xmake.lua`

## Code review focus areas

- Changes must not cause regressions in existing functionality, defaults, profiles, or project compatibility.
- Features gated by options must not affect existing behavior when those options are disabled.
- Changes should follow the existing code style and architecture. Architectural changes should be justified in code comments and the PR description.
- Add helper functions or utilities only when existing code cannot reasonably be reused. Avoid duplication.
- Keep code concise and clear. Manually simplify AI generated bloated codes before review.
- Include targeted tests or documented verification for behavior changes, especially in slicing logic, profiles, formats, and GUI defaults.
- For translation changes (`localization/i18n/**/*.po`), check that recurring terms match the [Localization glossary](https://github.com/OrcaSlicer/OrcaSlicer_WIKI/blob/main/guides/localization_glossary.md) for that language.

## Localization & translations

- Translation catalogs live in `localization/i18n/<lang>/OrcaSlicer_<lang>.po`.
- When creating or reviewing translations, use the [Localization glossary](https://github.com/OrcaSlicer/OrcaSlicer_WIKI/blob/main/guides/localization_glossary.md) as the source of truth for recurring terms, so the same English term is always rendered the same way within a language and terms that must stay in English (brand/product names, acronyms, file formats, G-code, macros/variables) are not translated.
- If a term's established translation changes, update both the affected `.po` files and the glossary so they stay in sync.
- Only edit `msgstr` (never `msgid`); keep placeholders (`%s`, `%1%`, `\n`), context (`msgctxt`), and file encoding/line endings intact.
