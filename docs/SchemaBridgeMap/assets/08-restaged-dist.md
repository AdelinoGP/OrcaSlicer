# Restaged dist — verification record (ticket 08)

Part of [Restore pnp_cli bundling after the dist layout change, and land the submodule bump](../tickets/08-dist-layout-and-submodule-bump.md).

Measured 2026-08-28, after `cargo xtask dist` (default edition, `developer`) in
`pinch_n_print_cli/` at `a50bfc28`:

| Before the restage | flat root `target/dist/` | `target/dist/developer/` |
|---|---|---|
| `schema_version` | 1.0.0 (stale) | **1.1.0** |
| modules staged | 21 (stale) | **23** |
| `host` array | absent | **93 entries** |

After the restage the flat root is deleted and `developer/` is the only dist; the root holds no
`pnp_cli.exe` at all, so the old silent-stale path is gone: a fork pointed at the root now fails
loudly (xmake bundling) or falls back with a notification (runtime), never silently slices with
a pre-bump backend.