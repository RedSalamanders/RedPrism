# Deferred lifted-pixel host and gradient benchmark scenarios — 2026-10-01

Status: P1.5 A5.2 functional model/host activation and A5.3 ordered application passed. Opening coverage/lift
reuse (A5.2a), clipboard/floating work, history construction, real-loop performance and complete P1/G1 remain open.

## Implemented boundary

Production Ctrl-drag and selected-pixel nudges use deferred metadata and admitted worker preparation. One preview
is active with only the latest pending request retained. Requests capture their opening, target, rounded offset,
duplicate mode and input-time Shift step; a queued nudge cannot consult a later keyboard state. Source acquisition
occurs under the complete reservation. Workers borrow no Application, HWND, session or GPU engine.

Pointer release and each selected nudge own their commit ordinal. Exact opening, settings sequence, document
generation and source token validate before one named Move/Duplicate Pixels entry installs the pixels and translated
outline. Cancellation retains the barrier, request and reservation until acknowledgement. Failure or a stale result
cannot replay a nudge, recapture a changed opening or mutate a newer document. A matching prepared preview may
authorize composition adoption. Aggregate retirement includes the captured/result owners and defers large graphs.

The gradient UI benchmark now drives the real coalesced preview and ordered Apply producers. Independent endpoint
bytes verify the preview/committed result. Its checked runner requires an exact binary/DLL freeze and capability
manifest before launch; missing, malformed, stale or incomplete manifests fail before process creation.

## Independent proof

- 52 `pixelmovestate` assertions: deferred metadata, opening/settings clocks, token validation, stale/refused
  ownership, exact latest application, zero offsets, cancellation and literal pixels/history.
- 20 `pixelmove-host` scenarios: both preview boundaries, latest pending work, successful release/nudge,
  cancellation acknowledgement, stale source/opening, resource failure, input-time Shift and large graph release.
- The existing Ctrl-drag/nudge numeric application self-test pumps the actual worker producers without changing
  its expected bytes. All other legacy document, clipboard, filter, selection and composition goldens pass.
- 26-operation UI smoke includes both production gradient scenarios. Eight runner refusal controls pass.
- 268 synthetic ISA parser controls pass, including exact RSP index loads and rows crossing independently proved
  adjacent tables. Real vector instructions, malformed dispatch/PDB/targets, gaps, byte mismatches and overlong
  boundary rows remain refused. Every first-party x64 image passes the linked instruction scan.

## Configuration gates and identities

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 301 native files |
| x64 Debug / Release / ASan Debug | Full suites passed; ASan detection and extended stress passed |
| ARM64 Debug / Release / ASan Debug | Clean cross-builds passed; native execution unavailable |
| Frozen native/project/script inputs | Same 351 inputs verified across the complete final matrix |

The C++ binaries were clean-rebuilt for the preceding R1 matrix. R2 changed only the ISA scanner/parser controls;
all x64 suites were rerun with the scanner changes and ARM64 was clean-rebuilt. The original clean R1 Debug and
Release suites also passed. No C++ source changed between those clean builds and the final R2 matrix.

Source-freeze SHA-256: `686D54B47382C5880DEC56EE7C08B501B9CD4C9E4455623AE2B094E4D7C4BFC1`.
Release 0.1.121.0 executable SHA-256: `42DEC5CD972F1E514255E5C9F421C5A75DECEB4D741F8EC5E0B6AC0EA31B0635`.

Logs: `.build/p1-pixelmove-host-r2-qualify-driver.log`,
`.build/p1-pixelmove-host-r2-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, skills/format logs and
`.build/p1-pixelmove-host-r2-freeze.json`. The qualified executable/DLLs, source freeze and gradient capability
manifest are preserved under `.build/p1-gradient-bench-qualified`; earlier qualified snapshots remain intact.

## Preserved failures and limits

The first Release matrix failed at the initial native folder-project fixture save. Its original source freeze/log
remain `.build/p1-pixelmove-host-freeze.json` and `.build/p1-pixelmove-host-x64-Release.log`. Bounded diagnostics now
report exact save/validation status and stage/native-error observations before later open/save attempts. An
instrumented Release self-test and both later full Release suites pass. The original cause is unconfirmed;
F6 remains open. No retry policy was added to hide the failure.

The clean R1 ASan build stopped at the ISA guard before runtime tests. Two apparent EVEX instructions were actual
DWORD table bytes: one row crossed two proven OnCommand tables; another used the observed contiguous RAX-base/RSP
index-load dispatch. PE bytes, PDB starts/contribution ownership and decoded targets proved both cases. The frozen
failure artifacts and `.build/p1-pixelmove-host-r1-ASan-ISA-failure.json` remain. The corrected scan of that same
image finds 19 table-data rows, 11,690 executable vector instructions and zero violations. The runtime allowlist
is unchanged. Dedicated negative controls and the complete R2 ISA/runtime matrix pass.

Coverage/lift preparation still repeats per preview tick, displayed geometry remains P1.6, and Begin/End history
construction remains P1.9 H1. No 4K/45 MP/100 MP timing claim follows from the correctness smoke. The earlier full
benchmark refused display-off presentation; A3/A4.6/G1 need an active, idle benchmark desktop. This step does not
qualify RAW decoding or a Photoshop comparison.
