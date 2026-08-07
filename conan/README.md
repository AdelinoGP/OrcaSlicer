# Conan provisioning (`conan/`)

Repo-owned Conan 2 configuration for the Xmake+Conan build
(ADR: `docs/adr/0001-xmake-conan-build-system.md`, state: `docs/BUILD_SYSTEM_HANDOFF.md`).

## Files

| File | Role |
|---|---|
| `profile_host.txt` | Settings every dependency must be built with: `compiler.cppstd=17`, static MSVC runtime on Windows (`SLIC3R_STATIC=1` semantics), Ninja generator (VS 2026 workaround), `[replace_requires]` steering recipe pins (boost, eigen) to the project versions. |
| `profile_build.txt` | Build-context (tools) profile — platform defaults, Ninja conf. |
| `conanfile.txt` | Consolidated require list mirroring `xmake.lua`, used for lockfile generation and direct `conan install` (CI cache warming). |
| `conan.lock` | Pinned versions + recipe revisions for the whole dependency graph. |
| `recipes/` | (Pending) repo-owned recipes for version-pinned/fork deps ConanCenter cannot reproduce — see handoff §6 Step 2. |

## Contract with xmake.lua

xmake's Conan integration generates its **own** per-package profiles and conanfiles
(one isolated `conan install` per `add_requires`); it cannot consume these profile
files or the lockfile. The same settings therefore ride along as CLI overrides via
the shared `conan_configs()` helper in `xmake.lua`. **Any settings change must be
made in both places** (`profile_host.txt` + `conan_base` in `xmake.lua`).

Consequences of the per-package isolation:

- `[replace_requires]` does not apply inside xmake's installs — e.g. cgal's own
  graph still resolves its pinned boost/1.83.0 + eigen/3.4.0 (header-only; the
  duplicate include-path hazard is tracked for the target-port step).
- The lockfile is **not enforced** by xmake builds. It pins the graph for direct
  conan workflows, CI, and drift detection (regenerate and diff after changing
  requires).

## Regenerating the lockfile

After any change to `conanfile.txt` / `xmake.lua` requires:

```bash
conan lock create conan/conanfile.txt \
    --profile:host=conan/profile_host.txt \
    --profile:build=conan/profile_build.txt \
    --lockfile-out=conan/conan.lock
```

## Binary cache story

- **Local developer cache** (`~/.conan2`) is the primary cache. First build compiles
  everything from source (wxWidgets + its autotools transitive chain is the bulk —
  historically ~30–60 min on this class of machine, handoff §5.9). Never
  `conan remove` the cache to "clean up".
- **CI**: GitHub Actions cache is evictable and must be treated as an optimization
  only. `--build=missing` (xmake's default, `configs.build = "missing"`) always
  falls back to source builds, so an evicted cache costs time, not correctness.
- A Conan remote (Artifactory/`conan server`) for published binaries is the durable
  option if CI rebuild times become a problem; not set up yet.
- Settings changes (`cppstd`, `compiler.runtime`) change package IDs and trigger
  full host-package rebuilds — expected, the old binaries stay cached alongside.
