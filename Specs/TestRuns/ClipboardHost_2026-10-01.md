# Ordered clipboard host processing

Status: full native functional qualification passes in matrix r9. All three x64 gates and all three ARM64
clean cross-builds pass against 480 frozen inputs. F5d/F6/P1.9/G1 measurements and remaining failure gates stay open.

## Implemented graph

Copy/Cut/Paste/Copy Merged use the selected host ordinal and acknowledged history barrier. Admitted owner capture
precedes worker private publication/encoding/decode; every OS transfer runs on a worker. Copy/Cut apply their
private result before mirroring and retain it on mirror failure. Paste shares six total acquisition attempts
between metadata inspection and owned byte copy. Retry waits retain the original record/reservation and receiver.

Copy Merged retains its opening whole-document authority through GPU utility tiles. It acknowledges mappings
and releases utility targets before CPU/private-copy/transfer stages. Device replacement after that boundary
cannot restart copied bytes or the writer. Complete source, readback, coverage, encoding, history and retirement
owners are reserved; large CPU owners retire as one aggregate off owner. New Canvas opens immediately and requests
only clipboard dimensions, guarded by invocation, typed intent, session and document lifetime.

The encoder planner bounds overlapping known owners by sequential phase peaks, including capped sink/vector,
64 KiB allocation slack and copying band, PNG/DIB/receipt globals, hash workspace and fixed metadata. Actual
storage/stream object sizes have compile-time bounds. Its 16 MiB WIC allowance is not a hard bound on opaque
codec workspace; native headroom and measured process peaks remain required.

## Private proof and preserved failures

The primary reviewed sidecars and runs the actual full application. The final r9 process exits zero: 52 ordered
clipboard scenarios/268 checks, 14 New Canvas scenarios/56 checks, seven actual delayed-render scenarios/39 checks,
eight publication/no-paint/retry fault scenarios/81 checks and 30 metadata-only merged-readback budget controls.
The independent encoder-budget probe separately passes 37 controls. The full run retains existing WARP, session,
pointer, file, gradient, size and other application assertions.

Actual `WM_RENDERFORMAT` blocks the worker while view input dispatches. Cancellation retains the reservation
until native return; replacement refuses old results. Separate owners shut down while that native call remains
blocked, with an auxiliary event release only after the scheduler's stop event. Their jobs/receivers/borrowed
state survive the final join, no document publication follows shutdown, and new commands are refused.

Preserve exact failed sources/exe/PDB/DLL/logs under `.build/p1-clipboard-host-draft/runtime-failure-r*/`:

- r1: the old clipboard harness observes synchronous results; observations now settle acknowledged jobs.
- r2: foreign fixture installation is denied by an external clipboard reader; bounded one-shot fixture waits
  replace a second competing setup operation without changing production retry policy.
- r3: the minidump proves `ReplaceDocument(document, session.ActiveLayerID())` clears an aliased active-ID argument.
  Capture that fixed ID before clearing the session; native independent replacement controls preserve identity,
  pixels, fresh lifetime and saved history. The callback checks also guard absent targets before dereference.
- r5: the new delayed-render fixture dereferences a blank layer's absent asset. Use an explicit literal asset.
- r6: view checks run at maximum zoom. Start the fixture at zoom 1; the original check still requires a change.
- r7: a fixture's temporary optional active ID may be evaluated before its document-builder argument sets that ID.
  Construct both arguments in sequence. Arm the Paste Open fault at the actual post-Inspect boundary and assert
  six total acquisitions, rather than an extra six Copy attempts.
- r8: all clipboard controls pass, then the later close-through-chrome test clicks stale retained layout. Prepare
  chrome before synthetic pointer dispatch; retain its exact alive/deferred/reposted assertions.

`private-proof-r4/` preserves the earlier full passing 262-input graph. `private-proof-r9/` preserves the final
264-input full pass and process-result metadata. The folder named `private-proof-r8/` predates the final process
exit; its run exits five at the later session check and is a failed snapshot, not qualification. The separate
`runtime-failure-r8/` preserves that same failure. Native activation is work in progress until its full matrix
exits and validates the source freeze.

Native r1 preserves all 480 frozen source inputs and 82 exact evidence files under
`.build/p1-clipboard-host-r1-ASan-failure/`, including five ASan executable/PDB pairs, runtime DLLs, all five ISA
reports, logs and the source freeze. Its build, CPU/I/O/composition/host suites, actual ASan probe and crash probes
pass; the subsequent full application self-test returns five. No clipboard host qualification follows from that
run. The repeat of that exact binary with both stdout and stderr redirected exits zero, including all 444
clipboard host assertions and the remaining full application checks. Its SHA-256 is
`943DCF29DA1395B3A3E10C34877B866C1D37477581E490CCCE942338D6B15BC8`. The initial failure is unreproduced and
its log does not identify a clipboard assertion; no root cause is inferred from the repeat. Retain the failure
and repeat together. Before matrix r2, add stage-return and merged-settlement diagnostics without changing any
expectation or wait bound, and have `test.ps1` preserve both application streams.

Native r2 preserves its 480 source inputs and 82 evidence files under
`.build/p1-clipboard-host-r2-ASan-failure/`. Its I/O suite fails only
`clipboardavailability.producer-first-dib-preserves-literal-pixels`; the preceding control confirms the adapter
requests the producer's DIB and never requests V5. That pixel helper did not report its Copy/Decode status and
did not wait on Busy, unlike its fixture installation/inspection helpers. Before r3, give this fixture helper
four bounded one-shot Busy attempts using the same immutable inspection, and print exact Copy/Decode status,
sequences or observed pixels on failure. Literal expected pixels, production retry policy, invalid/stale refusal,
producer-order assertions and every other expectation remain identical. No actual failure cause is claimed from
the missing r2 diagnostics.

## Native qualification and limits

Native r3 preserves 480 inputs and 83 evidence files under `.build/p1-clipboard-host-r3-ASan-failure/`.
Its full application run fails the post-inspection gate predicate in the Copy retry-exhaustion fixture.
Review finds that `PasteInspected` was reached even when `Inspect` returned Busy, before an owned inspection
existed. Retry Busy before that boundary. An additional scenario injects one busy inspection, then requires
exactly one successful inspection gate and six total acquisitions including the later Copy refusals.
The literal pixels, terminal failure/error, history and retirement expectations remain identical.

Native r4 passes the full x64 ASan suite, then Release fails the delayed Paste held-worker predicate. Its exact
480 inputs and 161 ASan/Release evidence files remain under `.build/p1-clipboard-host-r4-Release-failure/`.
The producer offered delayed data before worker admission, so an unrelated clipboard listener could enter its
render gate first. The old failure lacks individual predicate diagnostics; no specific external reader is
identified. Before r5, the producer participates only at the actual worker's DIB-read observer boundary under
the global clipboard lock. An explicit earlier read must be declined without entering the event gate, and the
delayed format must remain available for the real worker. Diagnostics record every constituent of the held
predicate. All existing delayed-render, view/cancel/replacement/shutdown and literal publication checks remain.
The corrected fault suite has 88 controls; the delayed suite has 41. Native r5 preserves 480 frozen inputs and 83 exact evidence files under `.build/p1-clipboard-host-r5-ASan-failure/`. Its full ASan application run fails only the New Canvas close/reopen suggestion predicate. The old predicate lacks constituent values; its actual failure cause is unproven. Review finds `PrefillInspected` also ran on transient Busy results. Before r6, retry Busy before this settled boundary (the sixth refusal remains observable), add individual reopen diagnostics, and force two Busy attempts in each of two card invocations. Each invocation must visit exactly one settled gate and retain its original six-attempt ceiling. All previous outcome, draft, history, view and retirement assertions remain. The corrected prefill suite has 61 controls. Matrix r6 is interrupted during ISA qualification and has no full process result; preserve 480 inputs and 83 evidence files under `.build/p1-clipboard-host-r6-interrupted/`. No test failure or native qualification is inferred. Matrix r7 uses a separate hidden runner that writes durable per-profile and final process receipts. Its full x64 ASan suite passes, including 268 command, 61 prefill, 41 delayed-render and 88 publication-fault controls; the later Release I/O profile fails one assertion, as detailed in the native r7 record below.

After a bounded successful process wait, `test.ps1` also finishes redirected-stream handlers before reading or
archiving either log, as specified by [Microsoft's Process.WaitForExit documentation](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.process.waitforexit?view=net-10.0).
No application process is terminated and no failure deadline or product assertion is relaxed.

After Paste foundation `fff9fa1`, integrate the sources, budget controls and production routes. Retire the unused
owner-thread mirror/read/decode methods. The Close fault reports an error after actually closing its scope, so
test injection cannot leave the clipboard locked. It preserves complete/partial publication truth.

All three x64 suites, all three ARM64 clean cross-builds, skills, formatting, ISA scans and exact image hashes
qualify F5b.2b.3/A5.4c/F5c.2 in the separate r9 matrix below. F5d/P1.9/G1 stay open.

Owner capture still copies layer metadata, GPU utility composition/uploads still consume owner work, and final
chrome/history publication needs measured G1 qualification. Remaining UI clipboard APIs are harness fixtures.
Codec workspace/headroom, wait-resource faults, all late-shutdown phases, cancellation distributions and
4K/45 MP/100 MP real-loop measurements remain F5d/F6/P1.9/G1. No native ARM64 runtime or Photoshop claim is made.

## Native r7 and r8 qualification

Native r7 preserves 480 frozen source inputs and 166 evidence entries under
`.build/p1-clipboard-host-r7-Release-failure/` (source-manifest SHA-256
`3A5277DA56D1B3838E32C5C4088C271C1A349A17D559040B307902D7F4EBD026`; evidence-manifest SHA-256
`13A18B6A26EB5852FC79D7548AC2ACF3AC685EA867DA4DD7B3A1EF7438D3F652`). The x64 ASan Debug suite passed with
exit code 0; its durable result receipt SHA-256 is
`6331BE25132C8DA8A295012103EB03EB9DA5726FACAA56CAED2BED71A9DECD36`. The subsequent Release I/O suite
failed one assertion, `legacy-deflate-fixture-created`; the matrix receipt records `IoTests.exe` exit code 1.
The preserved `old-deflate-fixture.json` reports that an unchanged `legacy-deflated.prism` existed in the
recorded scratch directory and gives its hash, but this observation does not prove a process-ID collision or
the cause of fixture creation. No AV/filter cause is established.

Native r8 preserves 480 frozen source inputs and 87 evidence entries under
`.build/p1-clipboard-host-r8-ASan-failure/` (source-manifest SHA-256
`B9F3F2DDBB8D1B354F20D795B96C5287382B398E829068D41F74CCA2996BE7A4`; evidence-manifest SHA-256
`575F42B8010C1E7F550152AF93EB53293978B8487FA03E3F3ED62C339D48CD9C`). Its ASan I/O suite exits 1 with 33
failed assertions: 32 folder fixture/save/rotation assertions and one partial-DIB receipt-to-PNG decode case.
The first `v7.save-exploded` failure records staging-to-destination error 5; the longer root also puts the nested
rotation fixture's codec temporary path beyond the ordinary Win32 path limit. Downstream assertions depend on
those initial fixtures and do not establish 32 independent rename failures. The matrix process receipt SHA-256 is
`70FB6971CA715A278FDEDA5876B54ABE123263DE427E98698FD57F2FB9766A9A`; the archived I/O executable SHA-256 is
`676A44246CC95B48C7C0A4BCDEF2B402369DD2F6B6299DAA3389D24F800E3562`. The failure archive remains the
authoritative record; it does not establish an external cause.

Neither r7 nor r8 qualifies the native candidate. A short exclusive scratch root and explicit ZIP close
diagnostics are subsequently qualified in r9 below. The old fixture file under the
recycled PID scratch root is evidence of a stale file in that location, not proof of PID collision. Do not infer
an AV/filter cause from it. For r8, the nested codec temporary-PNG path exceeded 260 characters. This confirms a
path-length failure condition, but supplies no narrow-gate or full-gate pass verdict. It establishes no causal
link to the first r7 rename/exit-5 observation. At that point, a short eight-hex per-run root, created with
exclusive `create_directory` and collision retries, was only a candidate. The narrow r9 diagnostic below
qualifies its long-path fixture result, but not the full native host matrix. The main agent owns architecture
and test fixes; this sidecar records evidence only.

## Narrow ASan I/O r9 and full native matrix r9

The narrow ASan I/O diagnostic run r9 passes all I/O tests. `.build/p1-clipboard-io-r9-result.json` records exit
code 0 at `2026-10-01T20:15:13.7328256Z`, executable SHA-256
`4FCE14850DBE9F1F9E569A6D162FB47C27F25A2EA75C52648FB771F46A21CF69`, and result-file SHA-256
`BE5FD901CC36A9ABD4953A9B0CA14F78B56B54AB0BD492C07F102B2F7A285996`. Its run log ends with “All I/O tests
passed” (SHA-256 `2DA9256755BA3BEC924D67798C9AA01F7671C4DAE2ECE1B33496B8FCB5B81251`); its frozen source manifest
contains 480 inputs (SHA-256 `0007B59B19E71A6431BF12DAC41BA80A1E87D92C13D859B42E844DD736575E4F`). The scope in the
receipt is explicitly narrow and does not qualify the full native matrix.

The short exclusive eight-hex scratch root resolved the nested-codec temporary-PNG path that exceeded 260
characters. The partial-DIB receipt-to-PNG case now passes after diagnostic expansion; production behavior and
expectations were unchanged. This narrow pass does not prove the cause of either the first r7 rename/exit-5
observation or the r8 partial-DIB failure. Both remain unproven; no narrow/full host-gate verdict follows from
this run.

The full native host matrix r9 launched at `2026-10-01T20:15:45.7746697Z` and exits zero at
`2026-10-01T20:35:15.9475478Z`. Its final source-freeze check verifies all 480 inputs unchanged through x64
ASan Debug, Release and Debug full `test.ps1 -Rebuild` gates and ARM64 ASan Debug, Release and Debug clean builds.
All three native application runs pass 268 clipboard command, 61 prefill, 41 delayed-render and 88 fault controls.
The gates include the independent CPU/I/O/composition/host suites, actual ASan defect probe, crash probes, ISA
controls, benchmark correctness smoke, script tests and harness captures. Skills validate all 15 entries;
formatting checks all 429 inputs. ARM64 execution is not claimed.

`.build/p1-clipboard-host-r9-qualified/` preserves the 480 exact source inputs and 301 evidence files, including
all six profiles' executable/PDB/DLL sets, profile and final receipts, native ISA reports and runtime logs.
Its source-manifest SHA-256 is `3C71C54B14CAEFBDCABCB56E79E8E78DAF8C71650DB601604BD44D55F52DB416` and
evidence-manifest SHA-256 is `5E10BBE242C74B3E42E503B6255466A5DDF3A52D8467D11E70EA132A07D87CB2`.
The qualified native RedPrism executable hashes are:

| Profile | SHA-256 |
| --- | --- |
| x64 ASan Debug | `00F3A5470F1D9C81C13F5452F798FB203BA86AF18B6ACB6A2925D6F9BC3B4A0F` |
| x64 Release | `320927782529E37573CB3A609EEAE9387CAC6D3A2229A242C704CED192057BB9` |
| x64 Debug | `B852F7A21F63FE7D54F126231695EE61C437F471C8BAFADA3611467054B12F9F` |

These passing results close the functional host slice. They do not explain the original intermittent save or
clipboard fixture failures, qualify opaque codec workspace, or establish measured owner/application/frame limits.
