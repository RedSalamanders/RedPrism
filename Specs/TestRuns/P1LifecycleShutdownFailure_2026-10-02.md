# P1 lifecycle policy and shutdown: failed native r7

Date: 2026-10-02. Scope: P1 lifecycle / shutdown qualification. Status: **failed full six-profile matrix; qualification incomplete**.

## Run receipt and completed profiles

Runner: `.build/qualify-p1-lifecycle-policy-shutdown-native-r7.ps1`. Start: `2026-10-02T10:40:28.3583653Z`. Failure receipt `CompletedUtc` (receiptUTC): **`2026-10-02T10:49:57.9982585Z`**, `ExitCode: 1`, error: `IoTests.exe failed with exit code 1.` Skills validation passed (15 skills); `format.ps1 -Check` checked 523 files.

The six-profile order is ARM64 ASan Debug, Release, Debug, then x64 ASan Debug, Release, Debug. Only the first three completed: ARM64 ASan Debug `0` at `10:42:12.2536889Z`; ARM64 Release `0` at `10:44:26.8589596Z`; ARM64 Debug `0` at `10:46:04.5734623Z`. These are cross-build results, not ARM64 runtime tests. x64 ASan Debug built and entered tests, then failed; no r7 completion receipt exists for x64 Release or Debug. Do not credit stale or pre-existing products as passes.

`test.ps1` runs HostTests, DocumentTests, CompositionTests, then IoTests. In x64 ASan Debug, the failure occurred in the fourth suite, IoTests: `clipboardtransfer.copy.foreign-replacement-refused-empty-output` failed; its summary was 125 checks, 1 failure. ClipboardBindingTransfer reported 34 assertions, 0 failures; ClipboardAvailability reported 45 checks, 0 failures. The log ends with `1 test(s) failed.` This records the observed failure only; its cause is unestablished.

## Shutdown, publication, and ISA controls visible in ASan log

The x64 ASan log contains 28 passing scheduler retirement, 26 close-hook and 34 suspended/prequeued retirement controls. The native publication suite reports 269 checks, zero failures. Strict inline-data proofs pass 99 CL/R8, 56 compact RCX byte-remap and 183 R10/AL controls. Five linked-image ISA scans passed. These partial results do not qualify the failed profile or full matrix; ASan stress and the hidden application self-test had not run.

## Separate focused Release r4 evidence

The successful focused run is `p1-publication-shutdown-focused-r4-Release-qualified`, receipt `.build/p1-publication-shutdown-focused-r4-process.json`, completed `2026-10-02T10:37:18.8188689Z`. It covered the focused Release build/ISA/HostTests/IoTests gates, including 34 suspended/prequeued retirement controls and 269 publication controls. Its archive is separate from the failed full-matrix r7 archive and cannot be used to represent r7 completion.

## Preserved r7 evidence and manifests

Archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-lifecycle-policy-shutdown-native-r7-ASan-clipboard-replacement-failure`.

- Source manifest: 582 entries; SHA-256 `11312639EAA7DD148BE1FD10F4A0C2C71B48CCDF08FF2EFD5E747429C79BA59A`.
- Evidence manifest: 292 entries; SHA-256 `0408ED7A6FEB74CE84618B80578BA8031858202D9F7B66874948A2120DB8958B`.
- ASan log: `.build/p1-lifecycle-policy-shutdown-native-r7-x64-ASanDebug.log`; profile results and failure receipt are preserved under the same archive's `Evidence\.build` directory.

The independent reference assertions and expected clipboard bytes remain unchanged. All P1 and G1 exit criteria and open A3 items remain open; this failed qualification closes none of them.

## Fresh r8 result and preserved failure

The r8 run used 582 frozen inputs and passed skills/formatting, all three clean ARM64 cross-builds and the full
x64 ASan suite, including stress, sanitizer/crash probes, hidden WARP application and harness screenshots.
The ASan receipt completed at `2026-10-02T11:18:21.8354590Z`. ARM64 ASan/Release/Debug completed at
`11:01:58.3523841Z`, `11:04:48.0360912Z` and `11:06:52.3473426Z` respectively. Runtime claims remain x64 only.

Release built and passed ISA, HostTests, DocumentTests and CompositionTests, then IoTests failed
`clipboardtransfer.prepare.source-sequence-captured-before-prepare` and its dependent
`publish.late-cancel-at-png-finishes-truthfully` assertion. All 269 publication recovery controls passed.
The matrix receipt is exit 1 at `2026-10-02T11:22:18.7193610Z`; Debug did not run. The source-sequence fixture
failure did not report whether Open, timer or Close failed. Its cause is unproven; no production fix follows.

Before any further source mutation/rebuild, byte-copy/SHA-256 verification retained 582 source entries and
294 evidence entries at `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-lifecycle-policy-shutdown-native-r8-Release-clipboard-sequence-fixture-failure`.

- Source manifest SHA-256: `4755C4930A83AC1141FDFFAFF46091EE03FE90BA52F2BEE00D0CB182482363B9`.
- Evidence manifest SHA-256: `5BB015CEFE010A49C075AA4A14C9D44E93B7C673385B22642C66BAA8B5D0C846`.

Diagnostic-only fixture Open/Close reporting preserves the four-attempt budget and all original assertions.
A subsequent focused run is separate evidence; it cannot represent a completed six-profile matrix.

## Focused Release diagnostic proof

After the r8 failure archive completed, diagnostic-only fixture Open/Close reporting was added without changing
the four-attempt budget or expected status/pixels. The focused Release build/ISA/IoTests run completed at
`2026-10-02T11:27:56.1576585Z`, exit zero. IoTests completed at `11:27:56.1222787Z`; image SHA-256 is
`4AFB75590DB46BF391FA0530951D16CEB6AA8D5204B3DEE10D79FC4FB919FBC9`.
All 125 clipboard-transfer and 269 publication recovery controls pass, with the existing I/O goldens unchanged.
Its byte-verified archive retains 582 inputs and 289 evidence entries at
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-clipboard-fixture-focused-r1-Release-qualified`.

The new diagnostics observe native OpenClipboard error 5 with valid fixture windows and multiple other holder
processes. Copy records three Busy attempts against the same sealed inspection sequence, then passes. Live PID
identification includes Windows CrossDeviceService, Logitech Options+, Mouse Without Borders, RadeonSoftware,
JDownloader and a Windows service host. This confirms shared-resource contention in that focused run; it does
not establish why the uninstrumented r7/r8 assertions failed. No external process was stopped and no production
clipboard policy changed. The integrated six-profile gate remains open, so this batch is not yet committed/pushed.

## Earlier integration timeline

- [ ] Current P1 integration, 2026-10-02 — Reviewed ordinary transform policy, worker New/New Canvas/Close,
  standalone final CPU retirement and exact CL/R8 ISA proof are activated. Private full WARP host passes
  258 lifecycle controls with unchanged goldens; standalone HostTests passes 28 shutdown controls. The prior
  native transform matrix stopped at Release ISA after ARM64 and x64 ASan passes; its original evidence is retained.
  The repaired parser passes 99 controls and the original-image recheck. Combined r4 passes all ARM64 builds
  and both full x64 ASan/Release suites; Debug stops at one clipboard-content verification. Its exact 572 inputs
  and 295 evidence entries are retained. The independent C9b reader now holds the clipboard through the fixture's
  pixel verification; the focused Debug I/O run passes without changing literal expectations or product behavior.
  The actual application shutdown candidate passes a full private Debug WARP run with 38 standalone shutdown,
  26 scheduler-hook, 112 preview and 45 delayed-clipboard controls. Its 553 inputs and 107 evidence entries are
  retained. Reviewed shutdown sources are integrated. Clean combined r5 passes all ARM64 builds, then stops at
  ASan's ISA gate before runtime. Its 576 inputs and 292 evidence entries are retained; exact compact stack-remap
  proof passes 56 new controls, all existing parser controls and the unchanged original image recheck. No runtime
  allowlist change is made. R6 passes all ARM64 builds and the complete x64 ASan suite, then Release IoTests
  stops on native error 5 during the baseline fixture's staging publication, before injected retry work.
  Its exact 577 inputs and 294 evidence entries are retained. Bounded native staging-publication recovery and
  independent first-save/replacement controls pass focused Release I/O: 269 checks, including unexpected destination
  preservation and complete backup retention. Suspended/prequeued shutdown adds 34 passing focused HostTests controls.
  Focused r4 retains 582 inputs and 292 evidence entries. The intervening Release switch-table ISA refusal is
  archived; its strict R10/AL proof passes 183 independent controls and the unchanged original image recheck.
  The runtime allowlist is unchanged. Fresh clean r7 passes all three ARM64 cross-builds, then stops in x64 ASan
  IoTests at `copy.foreign-replacement-refused-empty-output`; all 269 new publication controls pass. Its exact
  582 inputs and 292 evidence entries are retained before any repair. The failed check had no return-status
  diagnostic, so its cause remains unproven. Diagnostic-only changes retain the exact Stale/empty assertion;
  focused ASan I/O passes all 125 clipboard-transfer checks and all existing IoTests. Its 582 inputs and 289
  evidence entries are retained; no production clipboard change or root cause is claimed. Fresh clean r8 passes
  all three ARM64 cross-builds and the complete x64 ASan suite, including its stress, probes, hidden application
  and screenshots. Release stops at clipboard source-sequence fixture acquisition and its dependent late-cancel
  publication check; all 269 publication recovery controls pass. Debug does not run. Exact r8 evidence retains
  582 inputs and 294 evidence entries before further changes. Fixture Open/Close diagnostics now record immediate
  errors and clipboard-holder identity without changing the four-attempt budget or expected assertions.
  Focused Release I/O passes all 125 clipboard-transfer and 269 publication controls. Its diagnostics observe
  genuine error-5 acquisition contention from several other processes, including three Busy Copy attempts
  retaining the same sealed sequence. This does not establish the unobserved r7/r8 failure cause. No full native
  qualification is established yet. A complete six-profile matrix,
  broader shutdown lifetime coverage, remaining stroke/readback/selection paths, H2/H3/H4 and measured G1 remain open.
## Fixture allowance and independent controls

The revised test-only clipboard acquisition helper follows the existing availability/receipt fixture policy:
30 attempts with 100 ms one-shot event waits. Its lazy timer performs no work on immediate success. Copy
retains the same Inspection and Publish the same PreparedImage; Stale, cancellation, partial publication and
failed waits stop. Fourteen supplied-trace controls cover these transitions and exact 30/29 exhaustion.
All original 125 transfer checks and their literal pixels/status expectations remain; production retry limits
are unchanged. This deliberate fixture policy change does not establish the unobserved r7/r8 failure cause.

Focused Release r2 completes at 2026-10-02T12:02:06.0030335Z, exit 0. IoTests exits 0 at
2026-10-02T12:02:05.9612648Z, with 139 transfer and 269 publication-recovery checks. Skills, formatting and
Release build/ISA controls pass. The IoTests image SHA-256 is
271585D1C1B44D13B19B92BA2434DCEFE04B7543A307F04A516FCCE02696C02E.
Archive `p1-clipboard-fixture-policy-focused-r2-Release-qualified` retains 582 inputs and 289 evidence entries,
verified before the following diagnostic repair or rebuild. A pre-build runner argument error in focused r1
never launches a build or suite; its original start/stdout/stderr remain under .build with no native result claim.

The final native candidate additionally reports GetLastError only for WAIT_FAILED; WAIT_TIMEOUT reports its
actual wait result without a stale error value. The fresh r9 clean six-profile matrix is running. The focused
result is not its full native qualification.
