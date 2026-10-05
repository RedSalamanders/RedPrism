# P1 worker GPU creation and reservation transfer — 2026-10-05

## Qualification checklist

- [x] Preserve pushed residency/stage predecessor `e0a9c67` and all earlier attempt receipts.
- [x] Primary-review fixed GPU admission, worker-only device creation and owner acknowledgement.
- [x] Primary-review same-ticket upload/adoption and persistent staging/quarantine/shutdown reservations.
- [x] Private Caller r4 passes 473 predecessors and 253 new independent controls (48 budget, 155 creation, 50 integration).
- [ ] Bind the integrated normal source bytes to a fresh Main private probe and register exactly once.
- [ ] Full identical-source Debug, Release, real ASan Debug, Release analysis and three ARM64 cross-builds.
- [ ] Preserve every predecessor control, application route/stage and separate ASan stress registry.
- [ ] Independently archive verified sources/products/diagnostics; commit and push the qualified step.
- [ ] Complete full document allocation, mips/fences, actual budget snapshots, consumer activation and G1.

**Status: private Caller qualified; Main integration/normal qualification pending.**

## Scope and ownership

`GpuResidencyBudget` is an owner-only fixed 128-slot admission domain. The caller supplies a successful actual
DXGI budget snapshot before production admission; zero/invalid/unknown bounds refuse. Current usage and all
live/retired reservations overlap conservatively, with headroom max(64 MiB, budget/8). Checked subtraction
refuses overflow/pressure before capturing owners. Unique monotonically increasing tickets never wrap.
Move-only reservations keep bytes/device generation until their exact owner release. The domain outlives them.
The literal tests use declared synthetic capacity and do not claim real driver allocation or timings.

`GpuRasterTextureCreation` retains the exact owner-captured D3D device/generation and real threading feature
query. Single-threaded devices refuse. Only an advertised DriverConcurrentCreates route allocates the DEFAULT
BGRA8/R8 texture and shader view on a worker. There is no context/pixel/mip operation. Partial failed objects
remain held and unborrowable until owner retirement. Release/acquire state transitions prevent retirement from
releasing a running creator's device; owner acknowledgement follows producer join. Unsupported concurrency
returns ERROR_NOT_SUPPORTED, with no owner allocation fallback. Production fallback remains due.

The optional creation route in `GpuRasterUploadJob` preserves the original scheduler ticket and CPU reservation
through creation and each serial base chunk. It requires a separately admitted persistent queue staging charge,
plus the destination graph charge. Final successful callback alone can transfer the held Raster, texture/view
and GPU reservation exactly once. It clears the job's graphics references before transferring the charge;
the adopted owner survives the producing job. Its lower mips are uninitialized: no display sampling/publication
is permitted until the future admitted mip/recording/fence consumer completes. Externally created destinations
retain their existing external admission contract.

Reusable STAGING allocations retain their own 16 MiB + 64 MiB declared charge independent of destinations.
Uncertain copies and mapped owners on joined shutdown transfer an exact ticket/device graph reservation to
its queue slot. Graphics storage/context/fence release precedes releasing that reservation. Wrong tickets,
foreign generations and stale acknowledgements cannot take another graph. Event waits remain nonpolling;
workers copy only the pitched row lease and never touch the immediate context.

## Independent references and retained failures

Budget controls use literal 1 GiB / 256 MiB usage / 128 MiB headroom arithmetic, the 64 MiB minimum,
128-slot capacity, exact moves, worker refusal, UINT64 overflow and exhausted tickets. Actual WARP reports
DriverConcurrentCreates=TRUE on this host. Creation controls validate literal BGRA 3×2 two-mip 28-byte,
Gray 3×2 two-mip 7-byte, and BGRA 1×5 one-mip 20-byte layouts and actual texture/view descriptors; there is
no fake positive concurrency capability. Forced negative capabilities and texture/view fault boundaries
preserve original HRESULTs and prevent partial borrowing. The original captured device stays alive.

Integration validates literal opaque BGRA pixels and independent Gray proxy pixels
[4,6,8,10,18,20,22,24,29,31,33,34], final callback identity/charges, duplicate refusal, move adoption,
GPU charge outliving CPU job retirement, persistent staging reuse, insufficient/foreign/rebound authority,
post-copy signal quarantine and joined held-producer shutdown. Base-level readback never samples undefined mips.

Private r1 actual PID 72792 and r2 PID 46840 exited 2 on test compilation errors (incorrect void source pointer/
WIL method, and WinRPC's `small` macro). Primary repaired typed bytes/WIL assignment and renamed the identifier;
receipts remain. Private r3 actual PID 98156 exited 14 because its fixed backing arrays were passed as full
capacity, violating Raster::Make's exact byte-count contract. Primary corrected byteCount to width×height×channels
and retained the literal layout/descriptor expectations. Production implementation was not weakened.

Private r4 actual PID 77200, 2026-10-05T15:56:39.471682+00:00 → 15:57:48.201889+00:00, exited 0 on 402 frozen inputs.
Freeze D8C66396A372628E997536221F858AFB00F88C4F3079DD4B937D201D88775354;
stdout EBCAA366D4F0C3FF3D630E5BB58CF18909495F3A7A1E9963E149C2BA44DE531F.
All 14 selected source bytes exactly match Main integration. Its generated/helper/compiler receipts and all
preceding attempts remain under Native `.build`; borrowed pinned Debug dependencies do not replace normal gates.

## Remaining application work

This primitive creates one raster resource. Full committed/preview/brush/utility graphs, canvas/stack/coverage/
lookup creation, live actual DXGI/CPU reservation, bounded mip/composition recording, complete current-epoch
publication, measured destruction, unsupported-driver route, and original largest-fixture Release/ETW G1 remain
required before Native activation/P1 exit. Native ARM64 and R2/R3 hosts remain unavailable; cross builds are narrower.
