# Transform settlement foundation — 2026-10-02

Status: all three x64 full test suites and all three ARM64 cross-builds pass against 540 frozen inputs.
This incremental matrix qualifies the selected command/release/floating settlement routes; the complete P1
clean exit, production deferred previews, Alt-duplicate and measured G1 costs remain open.

## Qualification checklist

- [x] Preserve the waited private WARP r13 proof: 344 host checks, zero failures and unchanged existing tests.
- [x] Pass skills, formatting and all six native profiles; ARM64 results are cross-builds only.
- [x] Verify the 540-input freeze and archive sources, six profile images, symbols, logs, ISA evidence and receipts.
- [x] Preserve original runtime/compile failures and the unchanged-binary I/O rerun before repairs.
- [ ] Complete deferred preview/inline-bypass/duplicate/pointer-loss/device/shutdown caller qualification.
- [ ] Measure capture, apply, history, chrome, first upload and G1; complete the full P1 clean exit.

## Functional scope

Application-owned inputs route transform release, header Apply/Cancel, implicit command/tool/layer/write
settlement and floating selection Free Transform through the selected ordinal. The job reserves its complete
peak before owner capture, prepares pixels and history on workers, and validates source/draft/history authority
before publication. Undo/Redo cancels an active draft without traversing unrelated history. Repeated Ctrl+T
retains an existing draft; layer nudges retain input-time Shift and use an ordered worker closure when needed.

Floating opening prepares exact rollback before exposing the temporary layer. The host retains that owner
through merge/cancel/failure/replacement/shutdown; admission, capture, worker or history failure can restore the
still-current opening without failure-path allocation. Newer geometry and source/history replacements refuse
stale work. CPU payloads and rollback aggregates retire off the owner thread. Existing direct numeric references
and legacy goldens are unchanged. Ordinary deferred display activation is not part of this foundation.

The full private r13 WARP application run reports `transform-host summary: 344 checks, 0 failures`. Its actual
process was awaited; the run receipt records exit 0 at `2026-10-02T00:58:11.9729594Z`, executable SHA-256
`A8B6B757F160809D2D0F00E7533A40210A667EBF124E1A8C1F6381452C346A33`. The archive
`.build/p1-distort-draft/transform-host-qualified-r13/` contains 521 source and 168 evidence entries. Its
`evidence-manifest.json` hash is `2F9AD20BADCAE563F76E25577F363E484FD58E7FEF65662A1F9A86D7E7630179`.

## Native matrix

The r5 matrix process exited 0 at `2026-10-02T01:53:46.2441274Z`; its log ends with `PASS freeze 540 files`.
The three x64 logs each contain the 344-check host summary and complete test-suite success markers.
The matrix uses incremental builds and makes no native ARM64 runtime or clean-matrix claim.

| Profile | Result | Completed UTC |
| --- | --- | --- |
| x64 Debug | Full runtime suite passed | `10/02/2026 01:53:46` |
| x64 Release | Full runtime suite passed | `10/02/2026 01:50:13` |
| x64 ASan Debug | Full runtime suite passed | `10/02/2026 01:48:26` |
| ARM64 Debug | Cross-build passed | `10/02/2026 01:42:09` |
| ARM64 Release | Cross-build passed | `10/02/2026 01:41:53` |
| ARM64 ASan Debug | Cross-build passed | `10/02/2026 01:41:36` |

The exact archive is `.build/p1-transform-host-r5-qualified/` (540 sources, 296 evidence entries), a junction to
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-transform-host-r5-qualified`.
Every copy was read/written as bytes and hash-verified. Source-manifest SHA-256:
`C6EFA79D362794611B4FE25B92B1286712EF847DBA359BA2269D9982FCAC0E4F`; evidence-manifest SHA-256:
`11BA0063B389D37D1F9C4D1339C2413F05FA3598650531E9FB6247FCBF5DD059`.
An initial archive pass tried to hash its own active error log and failed sharing; the corrected exclusion and
completed retry preserve the same immutable native candidate. That archival failure is not a runtime failure.

## Preserved failures and fixture diagnostics

The first clean r1 matrix passed all ARM64 cross-builds, then failed three x64 ASan I/O assertions:
`clipboardbinding.receipt.same-payload-with-valid-receipt-actually-matches-control`,
`clipboardavailability.producer-first-dib-never-requests-synthesized-v5` and
`clipboardavailability.producer-first-dib-preserves-literal-pixels`. The binding diagnostic records Busy before
receipt inspection. The exact I/O binary passes an isolated rerun without rebuilding (SHA-256
`F1488C45BDB9F406693D31B92D3D36B86E45423E0DFE257CB73D7D41959A8777`). This does not turn the original matrix
green or establish a cause. The 540-source/291-evidence failure and rerun remain in
`.build/p1-transform-host-r1-failure/`.

The r2 matrix passed ARM64 and x64 ASan, then failed the Release assertion
`clipboardavailability.text-only-is-observed-without-image-format`; `.build/p1-transform-host-r2-failure/`
preserves its 540-source/293-evidence candidate. r3 and r4 are compile failures (missing fixture constant and
missing SystemClipboard include), retained separately in their `p1-transform-host-r*-compile-failure` archives.

The two I/O fixture helpers now use the existing harness's roughly 3 s acquisition budget: at most 30 attempts
with 100 ms event waits, retrying only Busy. Injected Busy remains one attempt; assertions are never retried.
Failure diagnostics record inspection status, attempts, held/current sequence, offered format IDs, immediate
native error and opener process when available. They enumerate formats without requesting clipboard data.
Production acquisition limits and numeric expectations remain unchanged. The later full pass does not prove
that the earlier failure was caused by a particular listener or that this fixture change cured it.

Deferred display/coalescing, all remaining caller migration, full P1 clean qualification and measured performance
remain open. No Photoshop comparison or RAW processing performance is established here.
