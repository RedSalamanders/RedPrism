# GPU completion foundation qualification — 2026-09-30

## Scope and result

The resumable scheduler, capability-route selection and bounded GPU readback foundation pass the complete
required configuration matrix. This is a P1.3 substep, not the whole phase. Production readback callers remain
synchronous; utility composition, host migration, explicit EnqueueSetEvent runtime coverage, restart policy
and G1 measurements remain open in [the active plan](../Plans/WIP/GpuCompletionReadback_2026-09-30.md).

The source follows pushed P1.2 commit `faf3cbf`; 232 first-party source/project/ISA policy files were frozen in
`.build/p1-readback-source-hashes.json` and checked before every subsequent configuration. Final verification
found zero differences. Specifications/evidence were reconciled after qualification.

## Executed gates

| Gate | Result | Local evidence |
| --- | --- | --- |
| `validate-skills.ps1` | PASS, 15 skills | `.build/p1-readback-skills.log` |
| `format.ps1 -Check` | PASS, 220 files | `.build/p1-readback-format-check.log` |
| x64 Debug `test.ps1` | PASS | `.build/p1-readback-x64-Debug.log` |
| x64 Release `test.ps1` | PASS | `.build/p1-readback-x64-Release.log` |
| x64 ASan Debug `test.ps1` | PASS, real detection probe and stress | `.build/p1-readback-x64-ASanDebug.log` |
| ARM64 Debug cross-build | PASS | `.build/p1-readback-ARM64-Debug.log` |
| ARM64 Release cross-build | PASS | `.build/p1-readback-ARM64-Release.log` |
| ARM64 ASan Debug cross-build | PASS | `.build/p1-readback-ARM64-ASanDebug.log` |

All existing legacy numeric document/composition/I/O goldens, ISA positive/negative controls, crash probes,
hidden WARP self-test, UI smoke and harness captures remain green. The three x64 runs also verify all 19 local
RAW files and their exact licence/manifest bytes offline; this does not qualify RAW decoding. ARM64 runtime
requires a native host and is not claimed.

## New evidence

- 94 `job-stage.*` PASS lines in the normal Debug suite: retained FIFO/reservation through suspension/resume,
  one final callback, duplicate/reused tickets, cancellation inside/before resume, false-hook finalization,
  failed-stage hook refusal, pending/ready owner semantics, shared event tickets and stale rearm/removal,
  reentrancy guards, synthetic memory successor admission and suspended/running shutdown.
- 22 `gpu-capability.*` PASS lines: literal route prerequisites/HRESULT combinations and real WARP context/device
  identity, null/deferred/mismatched refusal and report reset. WARP reports
  `route=monitored-fence device5=1 context4=1 fence=1 monitored=1 dxgi2=1` in all x64 configurations.
- 89 `gpu-work.*` PASS lines: independent premultiplied BGRA coordinate expectations for cropped and single-pixel
  rows, actual worker copying with returned row pitch, owner-only mapping/acknowledgement, cancel after mapping,
  old-device rows retained across a rebind, duplicate lease refusal, eight-slot refusal, two in-flight limits,
  64 MiB staging saturation waiting on an owned acknowledgement message, and worker join before shutdown Unmap.
  These run without an HWND or Present. No timing is asserted by these correctness tests.

The cap accounts for power-of-two staging texel storage, including lent/retired entries; driver allocation and
row-padding overhead are outside that declared cap and covered by global headroom. Pending requests do not
poll while lent storage prevents admission. The existing scheduler wait pool handles GPU events; no second
pool/thread is added.

## ISA attribution repair

Linking the real worker crash runtime into CompositionTests adds SettingsStore and yyjson dependencies. MSVC
emitted its runtime-dispatched `wmemchr` COMDAT in `SettingsStore.obj`; the initial Debug scan rejected 15 VEX
instructions. The matching PDB identifies exactly `S_GPROC32 wmemchr`, section 2 offset 1,258,096, length 966;
the disassembly branches around the AVX2 body through the CRT capability guard. Only the exact
`SettingsStore.obj!wmemchr` procedure range was added to the reviewed exception list. Surrounding first-party
functions remain scanned. All configuration scans and intentionally failing controls subsequently pass.

## Scheduler measurement

Release `HostTests.exe --bench-jobs` (32 workers, 8 warmups, 200 single Commit jobs) reports:

| QPC region | Median | p95 | Max |
| --- | ---: | ---: | ---: |
| Submit | 1.800 µs | 5.200 µs | 11.400 µs |
| Submit through final drain | 9.000 µs | 20.600 µs | 123.000 µs |

Checksum `645550820096`, `status=ok`; raw log `.build/p1-readback-jobs-release.log`. This is a scheduler
microbenchmark executed while the ASan matrix was active, not an alternating before/after performance trial,
a GPU utility benchmark or evidence of Photoshop/RAW/G1 targets. No image-speed claim follows from it.
