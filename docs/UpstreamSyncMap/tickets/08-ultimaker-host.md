---
title: UltiMaker host support
status: closed
type: implementation
assignee: opencode-agent
blocked-by: []
---

# UltiMaker host support

## Question

Port upstream's UltiMaker S5 local-API print host: `6e4be42a04` (2026-09-20, 19 files,
+1,184/−6; code is `UltiMaker.cpp` 666 lines plus `UltiMaker.hpp` 72) **plus five in-range
follow-ups** that touch the same two files (`4895bc03b4`, `84657ff11e`, `23546e71ce`,
`e821665e4b`, and one further commit). The first draft said "1 commit"; it is six.

This is the cheapest genuinely useful feature in the range, which is why it is the first feature
ticket — but the cost is not zero, and the first draft overstated how clean it is:

- `UltiMaker.hpp` derives from `PrintHost` and includes only `PrintConfig.hpp`, `Http` and wx —
  no `Print`, no `GCode`, nothing removed here. That part holds.
- `PhysicalPrinterDialog.cpp` (65-line hunk) and `PrintHost.cpp` (2 lines) have **zero** fork
  divergence since the base.
- `PrintConfig.cpp` and `PrintConfig.hpp` do **not**: they diverge by +65/−681 and +110/−226
  respectively, because the fork removed large parts of the option set. The three config lines
  and one enum line are small, but they land in heavily rewritten files — expect a hand-port, not
  a clean apply.
- `resources/profiles/UltiMaker.json` already exists at base, so the profile half is additive.
- The commit also touches `src/slic3r/CMakeLists.txt` (absent here — CMake removed),
  `OrcaSlicer.pot`, and `list.txt`. The CMake hunk is dropped; the `.pot` half belongs to
  ticket 07's snapshot.

Value is niche — UltiMaker S5 owners — but the debt is almost nil, which is the opposite of the
belt-printers trade.

## Scope

1. `src/slic3r/Utils/UltiMaker.cpp|hpp` — the host implementation.
2. `PrintHost.cpp` + `PrintConfig.cpp/.hpp` — register the host type.
3. `PhysicalPrinterDialog.cpp` — the 65-line hunk that exposes the host.
4. The profile update in the commit, **unless** ticket 03 already brought `UltiMaker.json` from
   head, in which case skip the profile half entirely and say so.

Interaction with ticket 02: that ticket deliberately omits the `UltiMaker.cpp|hpp` hunks from
`e40030cf81` (network hardening). Once this ticket lands, re-run `git apply --check` for those
hunks and take them, so the host ships hardened rather than being opened up to malformed
responses.

## PNP prerequisite

None. The host uploads files to a printer; it never calls the slicer. This is the one ported
feature that is complete on its own.

## Verification

1. Build and `xmake test` green.
2. Walkthrough, screenshots:
   - Physical Printer dialog lists the UltiMaker host.
   - Configure a host pointing at a local stub endpoint; confirm the dialog accepts it and the
     stored config round-trips through a preset save/load.
   - Attempt an upload against the stub and confirm the request shape (method, path, auth header)
     matches the documented local API — a stub that returns 200 is enough to prove the plumbing,
     since no S5 is available.
   - Malformed-response case from `e40030cf81`'s hunks → no crash.
3. Note in the PR body that no physical S5 was used and what the stub covered.

## Risks

- The host is unverifiable end-to-end without hardware. The stub proves plumbing, not
  compatibility; say so plainly rather than implying the printer path was tested.
- The 65-line `PhysicalPrinterDialog` hunk is the only fork-owned diff this ticket creates.

## Done when

Host ported, config registered, dialog exposes it, stub-based walkthrough with screenshots, any
hardening hunks from ticket 02 re-applied, ledger row updated.

## Answer

Landed on `pnp/main` as **`dc370ee79b`**, ported from upstream head rather than from the original
commit so the follow-ups and `e40030cf81`'s hardening arrive with it.

### What landed

| Path | Source | Note |
|---|---|---|
| `src/slic3r/Utils/UltiMaker.cpp` / `.hpp` | upstream head `808dd7dff8` | 666 + 71 lines; the original commit **plus** its five in-range follow-ups |
| `src/libslic3r/PrintConfig.cpp` / `.hpp` | `6e4be42a04` | `htUltiMaker` in the enum, the key map, `enum_values` and `enum_labels` |
| `src/slic3r/Utils/PrintHost.cpp` | `6e4be42a04` | include + `case htUltiMaker: return new UltiMaker(config);` |
| `src/slic3r/GUI/PhysicalPrinterDialog.cpp` / `.hpp` | `6e4be42a04` | 65-line host line, `Generate API Key` button, UltiMaker field layout, button gated to the host type |

Dropped, with reasons:

- `src/slic3r/CMakeLists.txt` — CMake removed here (ADR-0001).
- `OrcaSlicer.pot` and `localization/i18n/list.txt` — ticket 07 owns the catalogs, and the head
  snapshot already carries these strings. That is also why the head's strings (`_L`, `_u8L`,
  `GUI::format_wxstr`) were kept instead of the original commit's raw literals: the catalogs
  translate them.
- The profile half — `git diff 808dd7dff8 HEAD -- resources/profiles/UltiMaker` is empty, so
  ticket 03 already brought the S5 machine and process presets, the cover and the build-plate
  model.

### The ticket's "re-apply ticket 02's UltiMaker hunks" step

`e40030cf81` touches `UltiMaker.cpp/.hpp` in exactly one place: it deletes the host's private
`get_err_code_from_body` in favour of the shared `PrintHost::get_err_code_from_body` ticket 02
ported. Taking the head file takes that hunk, so no separate `git apply` is needed. Checked with
`git diff 6e4be42a04 808dd7dff8 -- src/slic3r/Utils/UltiMaker.*`, whose only content is the
follow-ups and the hardening.

### Verification

`xmake -j2` (Windows x64, release) green; `xmake test` 7/7 suites, 0 failures.

Walkthrough against a local stub on `127.0.0.1:8099`, screenshots in
[assets/08](../assets/08/):

1. **Host is in the list** — the Print Host type combo shows UltiMaker between Duet and FlashAir
   ([01](../assets/08/01-host-in-dropdown.png)).
2. **Dialog exposes it** — selecting it shows Hostname, Test, `Generate API Key`, User and Password;
   the API-key field, the authorization-type field and the CA file are hidden
   ([02](../assets/08/02-ultimaker-fields.png)). The `Generate API Key` button is absent for
   Octo/Klipper and AstroBox, so the gate works.
3. **Test** — with the stub answering `"Ultimaker S5"`, `{"message":"authorized"}` and
   `{"message":"ok"}`, Test reports "Connection to UltiMaker works correctly."
   ([03](../assets/08/03-test-success.png)). The stub log shows the documented shape: each request
   is first challenged with `401` + `WWW-Authenticate: Digest`, then repeated with
   `Authorization: Digest username="stubuser", realm="stub", nonce=..., uri=..., qop=auth, response=...`
   for `GET /api/v1/system/variant`, `GET /api/v1/auth/check/<user>` and `GET /api/v1/auth/verify`.
4. **Generate API Key** — POSTs the mime form to `/api/v1/auth/request` (473-byte body), parses
   `{"id","key"}` and reports `Username: stubuser  Password: stubpass`
   ([05](../assets/08/05-api-key-created.png)).
5. **Save / reload round-trip** — saved as a user printer preset with `"host_type": "ultimaker"`,
   `"print_host": "127.0.0.1:8099"`, `"printhost_user": "stubuser"`, `"printhost_password":
   "stubpass"`. After selecting that preset and reopening the dialog the fields are intact
   ([06](../assets/08/06-roundtrip-after-relaunch.png)) and Test succeeds again.
6. **Malformed response** — with `/api/v1/system/variant` answering HTML, Test reports "Could not
   connect to UltiMaker" ([07](../assets/08/07-malformed-response.png)); no crash and the app stays
   usable. This is `e40030cf81`'s hardening path.

### Gap: the upload POST was not exercised end-to-end

The GUI path that would call `UltiMaker::upload` is `Plater::send_gcode_legacy` (Print button,
non-BBL preset with a configured host). The plate sliced through `pnp_cli` (129 layers,
"Slice complete", [08](../assets/08/08-sliced-preview.png)), but pressing Print produced no
`PrintHostSendDialog` and no upload request appears in `requests.log` (the log holds only the
connection GETs and the API-key POST). So the upload half (`POST /api/v1/print_job` on the RRF
path, `PUT /api/v1/machine/file/gcodes/...` on DSF) is **unverified**. The dialog, Test and
credential paths are verified. No physical S5 was used; the stub proves plumbing, not
compatibility.

### Divergences this introduces

- `PhysicalPrinterDialog` gains a fork-owned 40-line block (button + gating + layout), plus the
  `m_printhost_generate_creds_btn` member and its DPI rescale. `on_dpi_changed` rescales the new
  button unconditionally, as it already does for Test and Log Out.
- `UltiMaker.cpp/.hpp` stay byte-identical to upstream head so the next sync stays conflict-free;
  the fork does not reformat them.
- The host's `set_auth` uses HTTP Digest, matching the local API; the fork's `Http` already
  implements it (`CURLAUTH_DIGEST`), so no dependency moved.
