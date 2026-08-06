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

This produces `pinch_n_print_cli/target/dist/` containing `pnp_cli` and `modules/`. CMake picks
this up automatically: `PNP_DIST_DIR` defaults to
`<checkout>/pinch_n_print_cli/target/dist`. Alternatively, copy `pnp_cli` and `modules/` next to
`orca-slicer.exe` by hand.

If the dist directory is missing at configure time, CMake warns and skips bundling; it does not
fail the build. Check the configure output for `PNP backend:` to see which way it went.

Configure options:

- `-DPNP_DIST_DIR=<path>` — bundle from a different dist tree.
- `-DPNP_BUNDLE_CLI=OFF` — skip bundling entirely.

For backend iteration you can also set **PNP CLI directory** under *Preferences → General* to the
folder holding `pnp_cli`. That override wins over the bundled copy whenever it actually contains
the executable; if it does not, the app logs a warning and falls back to the application
directory. **Clear** resets it to the bundled copy.

## Building

Windows-first fork. Follow the upstream
[OrcaSlicer Wiki — How to build](https://www.orcaslicer.com/wiki/how_to_build) procedure; the
build system, dependencies (`deps_src/`), and platform scripts (`.devcontainer/`, `scripts/`,
`flatpak/`, `msix/`) are unchanged from upstream.

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
