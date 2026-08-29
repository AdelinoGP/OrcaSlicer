# Orca(pnp_gui)

A fork of [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) used as the **GUI frontend** for
[pinch_n_print_cli](https://github.com/AdelinoGP/pinch_n_print_cli) (PNP). This is a **dev-only
frontend** — it is **not** the official OrcaSlicer and is not a general-purpose slicer.

## What this fork is

The native OrcaSlicer slicing pipeline has been replaced by a shell-out to the PNP backend:

- **Slice + preview** work by invoking `pnp_cli` (see [Backend layout](#backend-layout)).
- **Calibration UI** (temperature towers, flow rate, etc.) is a **mock** — the dialogs exist but
  the generators are not implemented.
- **SLA is unsupported** — SLA 3MF files are refused.
- **No native slicing** — `Print::process` and the headless `--slice` path are removed; use
  `pnp_cli` directly for headless slicing.

## Backend layout

`pnp_cli` (`pnp_cli.exe` on Windows) and its `modules/` directory must sit **flat in the same
directory as `orca-slicer.exe`** — the backend resolves both relative to the running executable.

The backend source lives in the **`pinch_n_print_cli/` submodule** (a Rust workspace). To stage a
dist tree:

```bash
cd pinch_n_print_cli
cargo xtask dist
```

This produces `pinch_n_print_cli/target/dist/developer/` (the default edition) containing
`pnp_cli` and `modules/`. The build picks it up automatically: `--pnp_dist_dir` defaults to the
dist root `pinch_n_print_cli/target/dist` and `--pnp_dist_edition` to `developer`, so the bundle
step reads `target/dist/developer/`. `xmake pnp` runs the cargo step for you. Alternatively, copy
`pnp_cli` and `modules/` next to `orca-slicer.exe` by hand.

Configure options:

- `xmake f --pnp_dist_dir=<path>` — bundle from a different dist root.
- `xmake f --pnp_dist_edition=<name>` — bundle a different edition (`developer|hybrid|integrated`).
- `xmake f --pnp_bundle_cli=n` — skip bundling entirely.

The build fails loudly when the configured edition has no dist staged — run `xmake pnp` (or
`cargo xtask dist` in `pinch_n_print_cli`) first. A `--pnp_dist_dir` that is itself one edition
(flat tree, no edition layer) still works but logs a warning.

For backend iteration you can also set **PNP CLI directory** under *Preferences → General* to the
folder holding `pnp_cli`. That override wins over the bundled copy whenever it actually contains
the executable; if it does not, the app logs a warning and falls back to the application
directory. **Clear** resets it to the bundled copy.

## Building

Windows-first fork. **The upstream build instructions do not apply**: this fork builds with
[xmake](https://xmake.io) + [Conan 2](https://conan.io), not CMake (see
[ADR-0001](docs/adr/0001-xmake-conan-build-system.md)). Install both, then:

```bash
xmake f -y -m release   # configure
xmake -j2               # build  (first run compiles the whole dependency graph)
xmake test              # run the Catch2 suites
xmake package           # portable directory
xmake pack -f nsis      # Windows installer
```

Only **Windows x64** is verified. The macOS and Linux code paths exist but have never been
configured or built — expect to fix things there. The last commit with the CMake build system
intact is tagged `pre-xmake-cutover`.

In-tree third-party sources remain in `deps_src/`; everything else comes from Conan.

## License

- This fork is licensed under the **GNU Affero General Public License, version 3**, as is
  upstream OrcaSlicer.
- OrcaSlicer includes a **pressure advance calibration pattern test** adapted from Andrew Ellis'
  generator, which is licensed under GNU General Public License, version 3. Ellis' generator is
  itself adapted from a generator developed by Sineos for Marlin, which is licensed under GNU
  General Public License, version 3.
- The **Bambu networking plugin** is based on non-free libraries from BambuLab. It is optional to
  OrcaSlicer and provides extended functionalities for BambuLab printer users.

## Attribution

This project is a fork of [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) by SoftFever and
contributors, which itself descends from BambuStudio, PrusaSlicer, and Slic3r. The OrcaSlicer
logo was designed by community member [Justin Levine](https://github.com/jal-co).
