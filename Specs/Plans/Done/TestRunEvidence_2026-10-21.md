# Test-run evidence retention

Status: COMPLETE (tooling retention scope)

Keep generated run output in unique repository-local evidence folders. Preserve failures and interruptions,
raw logs, dumps, screenshots and per-run receipts without copying shared build payloads. Never clear read-only
attributes, traverse reparse points, delete older runs, or weaken source/build/desktop/sandbox checks.

- [x] Inspect the runner and its owning build/validation contract.
- [x] Isolate output under evidence and retain it on all exit paths.
- [x] Exercise success, failure, interruption, collisions, links, streams and read-only files.
- [x] Update contracts, guides, CI artifact paths and changelog; run affected validation.
- [x] Recheck existing junctions and the authorized ReFS control.

Native product behavior and dependency pins are outside this tooling-only change.
Validation records: `Z:\src\DxUi-maintenance\cross-project-run-evidence-20261021`. Each standalone helper passed
32 preservation assertions on ReFS; the NTFS control also checks shared read-only hard links. Source/build and
desktop guards remain strict. Real noninteractive DxUi, RedXe and RedTilde runner controls and current-source
ReFS/NTFS controls are recorded separately from product qualification. The initial RedPrism build attempt on the paused October 3 base was
blocked by then-existing C5240/C2220 in `Tests/DocumentTests/RecordRootBench.cpp:74`; that native source is unchanged.
RedXe's pre-existing C++ format diagnostics are also retained; no native source was modified. This tooling change
does not claim a new full native/platform/product qualification or performance non-regression.

Remote-main integration retains the current scoped suite selection, caller-provided native ARM64 failure bound,
build/source attestation and qualification archive workflow. All current benchmark route output is retained under
the owned run. Current-candidate checks are recorded on the integration PR separately from that historical failure.
