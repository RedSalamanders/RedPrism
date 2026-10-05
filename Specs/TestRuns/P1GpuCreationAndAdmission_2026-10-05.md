# P1 worker GPU creation and reservation transfer — 2026-10-05

## Qualification checklist

- [x] Preserve pushed residency/stage predecessor `e0a9c67` and all earlier attempt receipts.
- [x] Primary-review fixed GPU admission, worker-only device creation and owner acknowledgement.
- [x] Primary-review same-ticket upload/adoption and persistent staging/quarantine/shutdown reservations.
- [x] Private Caller r4 passes 473 predecessors and 253 new independent controls (48 budget, 155 creation, 50 integration).
- [x] Fresh Main r5 probe binds integrated source bytes and passes all 726 controls; three new suites/register entries appear exactly once.
- [x] Private Main r6 strict Debug/WARP runtime and upload integration probe exits 0 on 404 frozen inputs; remains separate from normal qualification.
- [x] Full identical-source x64 Debug, Release, real ASan Debug, Release analysis and ARM64 cross-build matrix complete.
  - [x] Focused incremental x64 Debug test r142 exits 0; 821 inputs, all tests pass, 253 current P1 controls.
  - [x] Focused incremental x64 Release analysis r143 exits 0; concurrency SAL positive/negative controls and ISA analysis controls pass.
  - [x] Focused incremental x64 Release test r144 exits 0; 821 inputs, all tests pass, 253 current P1 controls.
  - [x] ARM64 Debug, Release and ASan Debug cross-builds r12 each exit 0 on the identical 821-input freeze.
  - [x] x64 real ASan Debug test r145 exits 0; full suites, concurrent worker stress, actual detection probe and application/harness gates pass.
- [x] Preserve every predecessor control, application route/stage and separate ASan stress registry across the full matrix and all configurations.
  - [x] Debug r142 registry proof records 9,744 named application controls, 79 stages, and preserved route counters/ordered stages; all three new suites/register entries are present.
  - [x] Strict per-configuration registry comparisons preserve all predecessors, 9,744 application controls / 79 ordered stages, and all 7,922 separate ASan stress controls.
- [x] Commit and push the qualified step as `c6145c0` on `codex/p1-opacity-host-proof`.
  - [x] Independent archives verify Debug142, analysis143, Release144, real ASan145 and all three ARM12 cross-builds; the unique current ASan diagnostic is independently copied/hashed too.
- [ ] Complete full document allocation, mips/fences, actual budget snapshots, consumer activation and G1.

**Status: Main creation/admission step qualifies the full required matrix and is pushed as `c6145c0`. Live activation and P1 completion remain open.**

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
At that point, all 14 selected source bytes matched Main r5 integration. This was a point-in-time r4/r5 comparison; one test comparison changed before r6. Its generated/helper/compiler receipts and all
preceding attempts remain under Native `.build`; borrowed pinned Debug dependencies do not replace normal gates.

## Remaining application work

This primitive creates one raster resource. Full committed/preview/brush/utility graphs, canvas/stack/coverage/
lookup creation, live actual DXGI/CPU reservation, bounded mip/composition recording, complete current-epoch
publication, measured destruction, unsupported-driver route, and original largest-fixture Release/ETW G1 remain
required before Native activation/P1 exit. Native ARM64 and R2/R3 hosts remain unavailable; cross builds are narrower.


## Fresh integrated private reference

Main r5 actual PID 86148 exited 0, 2026-10-05T16:01:54.436400+00:00 → 16:03:07.461229+00:00; 404 frozen inputs. Freeze `6A16553BCC5F2F56340BA1CE8B370D4BB4E7B5D5B1D8240F2FD5C0EA3BE185C2`; stdout `B50927BDE4EE1B6CD38FAD21D4155A56BD1E9B0CFE21105F85FA72CA2FF26EF2`. Full Main qualification starts after this actual child exit on 821 identical inputs. Release analysis r140 actual PID 95936 exited 1 at dependency-lock acquisition before compilation; no code result is inferred. Its receipt is preserved. Analysis r141 actual PID 29876 exited 1 with C6221 at `completed.result.error == (insufficientReservation ? E_OUTOFMEMORY : E_ABORT)`, an implicit comparison between semantically different integer types. The r143 source repairs it as `static_cast<HRESULT>(completed.result.error) == (insufficientReservation ? E_OUTOFMEMORY : E_ABORT)`; r143 actual PID 6700 exited 0. The initial ARM launch raced the hidden recorder’s still-missing freeze file and created no ARM child/attempt; its driver-only receipt is retained. Unchanged retry r11 passes all three ARM64 cross-builds.
## Current qualification receipts — 2026-10-05

The private Main r6 WARP probe and the normal/analysis/cross-build lanes are separate evidence. Main r6 used 404 inputs. Debug142, analysis143, Release144 and each ARM12 configuration used the identical 821-input source freeze `A453ADAE8FDED7E833A50672DBC53A81C9D19079A22542B357B7F3F15DBC9A36`. The Debug and Release test logs each report 48 budget + 155 creation + 50 integration controls. The r6 freeze matches all 14 current integrated selected-source hashes in the r142 registry proof: 14 matched, none missing or different. These are focused incremental runs, not forced clean rebuilds or G1 evidence. ARM12 proves cross-compilation only, not native ARM64 runtime.

Roots: Main receipts are under `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\`; private Main r6 receipts are under `Z:\src\RedPrism\.build\`.

| Lane (all 2026-10-05 UTC) | Actual child interval; archive verified | Inputs; source-freeze hash | Exact source-freeze and result receipt | Independent archive completion hash |
|---|---|---|---|---|
| Private Main r6 | PID 94444, exit 0; 16:25:37.377759–16:26:48.305206; no independent archive receipt in r6 folder | 404; `3FEF97B4F934A1BFBC61C5B583496349C5668AD85B0307B00F2D86344DF89A4C` | `Z:\src\RedPrism\.build\private-gpu-creation-runtime-r6-20261005\source-freeze.json`; `Z:\src\RedPrism\.build\private-gpu-creation-runtime-r6-20261005\result.json` | — |
| x64 Debug test r142 | PID 77580, exit 0; 16:28:09.912764–16:37:51.567904; verified 16:40:25.0109162 | 821; `A453ADAE8FDED7E833A50672DBC53A81C9D19079A22542B357B7F3F15DBC9A36` | `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\incremental-x64-test-Debug-r142\source-freeze.json`; `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\incremental-x64-test-Debug-r142\result.json` | `F086AA3BACED2535AA5E90B2F5016A7383480CA498E3305B79B2372DB5591DFB` |
| x64 Release analysis r143 | PID 6700, exit 0; 16:29:37.066645–16:36:30.550930; verified 16:40:23.8527802 | 821; `A453ADAE8FDED7E833A50672DBC53A81C9D19079A22542B357B7F3F15DBC9A36` | `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\incremental-x64-analysis-Release-r143\source-freeze.json`; `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\incremental-x64-analysis-Release-r143\result.json` | `2E8AF60C7552209772F3D938EDCFAC98F842390D2C992CA9B52BD5CDE4227955` |
| x64 Release test r144 | PID 77580, exit 0; 16:40:54.842779–16:46:53.832832; verified 16:49:35.2519013 | 821; `A453ADAE8FDED7E833A50672DBC53A81C9D19079A22542B357B7F3F15DBC9A36` | `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\incremental-x64-test-Release-r144\source-freeze.json`; `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\incremental-x64-test-Release-r144\result.json` | `B06B5BB2999758DA22E167080194BEA8AF7A13ED36773F7BCE164884D0064266` |
| ARM64 Debug r12 cross-build | PID 38908, exit 0; 16:40:55.854388–16:42:30.416133; verified 16:49:41.6398273 | 821; `A453ADAE8FDED7E833A50672DBC53A81C9D19079A22542B357B7F3F15DBC9A36` | `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\residency-arm-builds-r12\Debug\source-freeze.json`; `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\residency-arm-builds-r12\result.json` | `B372348F35C31A352CCBA8A32EC918DA5D326F9C210B7689F6E3DCADC892B902` |
| ARM64 Release r12 cross-build | PID 10108, exit 0; 16:42:33.526799–16:42:54.834061; verified 16:49:47.9214166 | 821; `A453ADAE8FDED7E833A50672DBC53A81C9D19079A22542B357B7F3F15DBC9A36` | `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\residency-arm-builds-r12\Release\source-freeze.json`; `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\residency-arm-builds-r12\result.json` | `3333E029F3F86E08945835D0659DF6A069986D17757117AF8DA42A8DA7F2236D` |
| ARM64 ASan Debug r12 cross-build | PID 95984, exit 0; 16:42:58.260283–16:43:19.820131; verified 16:49:55.1737874 | 821; `A453ADAE8FDED7E833A50672DBC53A81C9D19079A22542B357B7F3F15DBC9A36` | `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\residency-arm-builds-r12\ASanDebug\source-freeze.json`; `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\residency-arm-builds-r12\result.json` | `82055B3EC43415252ACCB6D90EE0E4BA3FF81414391917E4209A5533DDDD3C7A` |

x64 ASan Debug r145 actual child PID 34808 exits 0, 16:49:40.007308Z -> 17:02:38.862382Z, with the identical 821-input freeze. Stdout SHA-256 `41DBA91F32FCC6B5DDC0269315CE974214ACD10DD4A09A198B338C18F372267E`. Its independently verified archive has 1,098 copies, completion hash `40707BF8062DBC13A227D0EC3425B6C284503A9DCE0527708D8BC6887BA2C92B`. The unique intentional ASan diagnostic from this actual interval contains heap-use-after-free; its independently copied hash is `294D92A9A5B3267FDB8D2742D6617F66B07176DEDBBA44E0BFEA520ACE5F51F6`, supplement receipt hash `B8468D953ACF8AB3C4740DAB9A8DCC2FFAC01B4CF7966E6B108CEC601CA4256B`. Original archive receipts are unchanged.

Archive receipt paths, in table order after r6: `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\incremental-x64-test-Debug-r142\independently-verified-archive.json`; `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\incremental-x64-analysis-Release-r143\independently-verified-archive.json`; `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\incremental-x64-test-Release-r144\independently-verified-archive.json`; and `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\residency-arm-builds-r12\Debug\independently-verified-archive.json`; `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\residency-arm-builds-r12\Release\independently-verified-archive.json`; `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism\.build\residency-arm-builds-r12\ASanDebug\independently-verified-archive.json`. Archive verification is recorded per lane; it does not complete commit/push or application activation requirements.

## Final predecessor and static-gate verification

Strict r2 registry proofs bind all 14 selected source bytes to private Main r6 and the frozen e0a9c67 baseline. Debug, Release and real ASan preserve all prior counters; Composition controls increase 1,242 -> 1,495 only by the 253 creation/admission controls. Document/Host controls, all 9,744 application controls and 79 ordered stages are unchanged. The separate ASan worker stress retains all 7,922 controls. No test, literal or application route was deleted or rebaselined. Release analysis retains the positive/negative concurrency SAL and ISA checks.

`validate-skills.ps1` passes all 15 skills and `format.ps1 -Check` passes all 701 files after all actual qualification children exited. Their current logs are `.build/gpu-creation-skills-r2.log` and `.build/gpu-creation-format-r2.log` in Main. No Main code mutation preceded evidence archiving. Main integration remains separate from Native activation.
