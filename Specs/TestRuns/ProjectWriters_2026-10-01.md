# Asynchronous project save and PNG/JPEG export — 2026-10-01

Status: bounded F4b.1/F4b.2 writer qualification passed; P1/G1 remain open.

## Implemented boundary

Project save captures the settled document, active layer, canonical destination and exact history revision.
Its worker owns one reservation through checked publication. Current completion records the published revision,
including when a later edit exists; cancelled dependent actions do not close or replace the document. Stale
lineage cannot update the current document path or saved revision.

PNG/JPEG export traverses committed-document utility tiles into one adopted CPU result, acknowledges the last
mapping and releases utility targets, then encodes and publishes on a worker under the original reservation.
Device replacement after this detach cannot repeat encoding or publication. Export preserves the project path,
revision, modified state and pixels. Failure notices are posted once, outside modal completion work.

Owned resolved paths reserve every staging/member/codec suffix and the terminator: project save at most 32,637
units, image export 32,728. OS path/component validation remains a worker responsibility.

## Correctness evidence

- Twenty project-save and nineteen export memory checks cover bounds, invalid metadata, path suffix room,
  checked arithmetic and shared/sparse source storage.
- Thirteen project-save host scenarios cover publication, ordered younger edits, pre/post-publication cancellation,
  a cancelled dependent close, exact errors, actual allocation failures, stale lineage, captured saved revision,
  pending transform/opacity settlement and admission/path refusal before Run.
- Fifteen export host scenarios cover literal PNG/JPEG output, ordering, cancellation, exact error/notice handling,
  allocation failures, stale lineage, captured quality/resolution, device changes around encoding, admission/output
  limits and persistent second-tile failure with no encode or published output.
- GPU tests cover 2048 squared first-busy recovery and persistent busy on both completion routes, held recovery
  cancellation/rebind/shutdown, exact rearm/enqueue/removal failures and queue reuse with independent coordinate
  bytes. A second-tile persistent failure cannot publish an acknowledged first tile as a partial Raster.
- All 202 ISA parser controls pass, including 45 exact R9-base/R8-target Menu cases. The runtime allowlist remains
  unchanged. This scanner correction recognizes proved table data, never an object/function exemption.

## Real hardware failure and recovery

The initial six-configuration writer gates and small fixtures passed, but the hardware 4K PNG benchmark failed
before encoding: Map READ DO_NOT_WAIT returned `0x887a000a` on the first 2048 squared tile. A failure-only
diagnostic observed monitored-fence expected value 1 and completed value 1. The exact driver/runtime cause beyond
that observation is not established. Logs: `.build/p1-writers-gpu-diagnostic.log` and its `.err.log`.

The corrected queue retains that original copy, slot, generation and in-flight charge through at most one new
DXGI completion event before one additional nonblocking Map. There is no recopy, timer, polling or blocking Map;
persistent busy fails locally. This bounds attempts, not driver delay or guaranteed mapping success. The API
[event contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgidevice2-enqueuesetevent)
and [Map contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map)
support this failure policy. The frozen qualification completed all 21 scenarios at hardware 4K/45 MP and WARP
4K. Hardware exports used the additional event on 39 of 40 tiles per format at 4K and all 120 tiles per format at
45 MP. WARP used no additional event. All recorded writer rows have zero persistent-busy failures.

## Configuration gates

The source freeze records native code, projects and build/test scripts; specifications are updated during runs.
Debug completed before the freeze with unchanged source inputs, and its complete log is retained rather than
rerunning the same gate. Remaining gates verify the freeze before each configuration and benchmark.

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 261 files |
| x64 Debug tests | Passed |
| x64 Release tests | Passed |
| x64 ASan Debug tests and real sanitizer probe | Passed |
| ARM64 Debug / Release / ASan Debug cross-builds | Passed (runtime unavailable) |
| Hardware 4K / 45 MP and WARP 4K, 21 real-loop scenarios | Passed: ten recorded trials after one warmup per row |

Logs: `.build/p1-writers-qualification-driver.log`, `.build/p1-writers-final-*`,
`.build/p1-writers-freeze.json` and `.build/p1-writers-{hardware-4k,hardware-45mp,warp-4k}.log`.

The final freeze covers 309 source/project/script files. SHA-256 identities:

- Source-freeze manifest: `61EDFA3C4F7D98283B6E2EBCAD795DA208A19C6DFA4D18A7C0D2EC4E18DEC484`.
- x64 Release RedPrism: `EEEC8357F4E5D7713CDA7CAC154BDF57EAD5758A23972B316E3E2D419AD97C23`.
- x64 Release DocumentTests: `2D166DBEC4EE8B5FCBCE47E20DA01C62E3FC3A33FE7BAA06467409503CEBC954`.

## Real-loop writer measurements

Ryzen 9 9950X3D, RTX 5080 driver 32.0.16.1714, 66,155,405,312 physical-memory bytes, Windows build 26200,
x64 Release 0.1.112.0, Scalar kernels and 32 workers. Fixtures contain two full raster layers, a mask and an
exposure adjustment; they do not qualify the master four-full-layer G1 fixture. Values are milliseconds.

| Device / canvas | Writer | Elapsed median | Elapsed p95 / max | Apply max | UI action max |
| --- | --- | ---: | ---: | ---: | ---: |
| Hardware 4K | Project save | 279.728 | 351.310 | 0.132 | 0.105 |
| Hardware 4K | PNG export | 415.563 | 484.387 | 0.090 | 3.709 |
| Hardware 4K | JPEG export | 63.612 | 219.762 | 0.118 | 0.109 |
| Hardware 45 MP | Project save | 974.848 | 1041.005 | 0.070 | 0.101 |
| Hardware 45 MP | PNG export | 1286.407 | 1328.426 | 0.083 | 0.080 |
| Hardware 45 MP | JPEG export | 196.593 | 300.261 | 0.086 | 1.001 |
| WARP 4K | Project save | 232.083 | 341.489 | 0.164 | 0.176 |
| WARP 4K | PNG export | 448.740 | 531.571 | 0.075 | 0.214 |
| WARP 4K | JPEG export | 159.430 | 208.454 | 0.102 | 0.246 |

The ten-trial p95 is the maximum. Counters exclude warmup: each hardware 4K image format records 40 copies,
79 maps and 39 readiness barriers; each hardware 45 MP format records 120 copies, 240 maps and 120 barriers;
each WARP 4K format records 40 copies/maps and no barriers. Persistent-busy failures are zero throughout.
Maximum completion drain reaches 2.985 ms for hardware 45 MP JPEG, 2.956 ms for WARP PNG and 2.531 ms for
WARP JPEG. Hardware 4K PNG also has a 3.709 ms UI action. These observations explicitly leave the 2 ms G1
target open; elapsed writer time is neither a UI blocking duration nor a Photoshop comparison.

## Remaining work

Open/import/clipboard workers, additional writer IO fault/rollback paths, the unexplained intermittent exploded
save failures, remaining GPU readback callers and the complete G1 operation/timing matrix remain open. These
writer graphs also still settle unmigrated canvas edits synchronously. Native ARM64 runtime qualification is
unavailable on this x64 host. No RAW decode or Photoshop speed claim follows from these gates.
