# Asynchronous image imports — 2026-10-01

Status: bounded F3b.3b activation and measurement qualification passed. P1/G1 remain open.

## Implemented boundary

Interactive image import uses an owned metadata job followed by separately admitted preparation under the same
selected request and history ordinal. Paths are absolute, counted and independent of callers. Inspection returns
only ordered per-file statuses, dimensions and encoded lengths. Probe/preparation charges can overlap at
submission; Io FIFO prevents preparation before probe retirement. The admitted preparation record first enters
a trivial worker stage, captures settled document metadata on its owner hook under the complete reservation,
then resumes that same record for private decode. No live session or GPU is borrowed on the worker.

The opened read stream denies normal write/delete sharing and checks its actual encoded length before WIC
construction. Reopened decode enforces inspected per-file encoded/pixel ceilings and aggregate added-pixel
ceilings before output allocation. Negative budgets and the hard 100 M frame limit are checked independently of
larger callers. Project-file PNG decode uses the opened-stream limit too. Normal source replacement within the
ceilings is allowed; this is not a cryptographic snapshot or interruption guarantee for an in-flight OS/WIC call.

The complete validated new-baseline/final pair is applied only for the current serial, generation, ordinal and
acquisition. Cancellation, stale lineage and worker/capture failure apply neither result. Ordinary partial
success retains good inputs and reports batch failure. The first good image establishes a saved baseline;
remaining successful imports form one additional entry. Existing targets preserve grouping and floored placement,
and activate the last successful imported layer. Mixed project/image batches preserve the first error and continue on an authorized
replacement lineage. Snapshot capture, history/chrome application and display uploads remain measured owner costs.

## Correctness evidence

- All 122 phase/target cases and eight batch cases pass in each x64 configuration: existing/grouped/new/empty
  targets, probe/preparation before/after, successful ordering, acknowledgement, exact returned failure, actual
  worker/admission/capture allocation failure, persisted-ID reuse, generation changes and device lifetime.
- Held cases require pending receipts, retained history/input/graph owners, view input, exact preparation charge
  after probe retirement, literal imported bytes/placement and zero hidden Present. Terminal cases require zero
  tickets, barriers and input/graph charges. Younger Undo restores existing-target imports as one entry; new
  canvas replacement makes the old request stale, and manual Undo returns to the first-image saved baseline.
- Batch cases cover missing first/last files, cached missing inputs that later appear, frame/encoded growth after
  inspection, all failures and project-then-images success/failure. The mixed success control requires two image
  imports into the opened project as one additional entry; the mixed failure control retains the first error.
- IoTests reports 92 codec work, 121 stream and 63 detached import work checks. Independent PNG/JPEG/BMP literals
  and a classic two-frame TIFF verify metadata/pixels and frame-zero selection. Other checks cover read-only
  cursor/source preservation, sharing errors, exact/over 512 MiB sparse files, signed/hard budgets, cached failed
  plans, aggregate caps, cancellation and allocation floors without shared sparse materialization.
- The 24-row real-loop smoke uses two trials after one warmup at 64×48. Import readiness uses real receipts and
  session metadata, with no timed file reads or pixel scans. The source is the preceding successful PNG export.

## Qualification corrections

The first host run exposed an incorrect new test expectation: legacy `EditorSession.swift:656–657` floors
centered placement. Literal expected origins were corrected to 13/1; existing goldens and placement code stayed
unchanged. The next run found an HRESULT stored in a Boolean (`S_OK` became false); the test now checks
`SUCCEEDED`. A later mixed batch found a production continuation bug: project completion left `FileComplete` as
the next prerequisite, skipping the following image group. `PathImages` now selects `Execute` before settling.
Original failed logs are retained as `.build/p1-image-import-original-{placement,device,mixed}-*`.

Review also found that copying source document records before admission would undercut memory accounting. The
admitted trivial-stage/capture/resume boundary fixes that, with a real allocation-failure control at capture and
an exact held-reservation observation. These corrections were included before the final frozen matrix.

## Configuration gates

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 272 files |
| x64 Debug / Release tests | Passed; all 130 host cases in each |
| x64 ASan Debug, extended stress and real sanitizer probe | Passed; all 130 host cases |
| ARM64 Debug / Release / ASan Debug cross-builds | Passed; native execution unavailable |
| Hardware 4K / 45 MP / 100 MP and WARP 4K, 24 operations | Passed; ten trials after one warmup per row |

Logs: `.build/p1-image-import-driver.log`, `.build/p1-image-import-{x64,ARM64}-*.log` and
`.build/p1-image-import-freeze.json`. The frozen matrix covers 320 code/project/script inputs unchanged through
all six configurations. SHA-256 identities:

- Freeze manifest: `C3600181AAC52B55797C441A16814E5D3F432FD1AC30424620272EA1AEC63410`.
- x64 Release RedPrism: `EC5208BAB2F22B66602A6287605235B531FBCD7DBC978969A0635D39ED6589CA`.

## Measurements and limits

Ten trials follow one warmup per row. PNG inputs are warmed by the preceding production export. Normal benchmark
operations use two full raster layers, one mask and an exposure adjustment; existing-target import starts from
one full raster layer so a second 45 MP image fits the current 100 M aggregate limit. The 100 MP fixture uses one
full layer, and its existing-target import must refuse the additional image with the exact pixel-budget error.
Its PNG content also differs from the multi-layer composite used at 45 MP, so those elapsed values are not a
45-to-100 MP scaling comparison.
These are neither cold-storage/RAW nor Photoshop qualification, nor the master's four-full-layer G1 fixture.

Ryzen 9 9950X3D, RTX 5080 driver 32.0.16.1714, Windows build 26200, 66,155,405,312 physical-memory bytes,
x64 Release 0.1.116.0, Scalar kernels and 32 workers. WARP uses driver 10.0.26100.9278.
Values are milliseconds; ten-trial p95 is the maximum.

| Device / canvas | Import target | Elapsed median | p95 / maximum | Apply maximum | Drain maximum | Frame maximum |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Hardware 4K | New canvas | 48.776 | 53.043 | 0.133 | 0.141 | 5.493 |
| Hardware 4K | Existing layer | 51.348 | 54.842 | 2.538 | 2.546 | 2.298 |
| Hardware 45 MP | New canvas | 169.732 | 183.917 | 0.159 | 0.164 | 30.733 |
| Hardware 45 MP | Existing layer | 172.271 | 208.384 | 11.403 | 11.409 | 49.224 |
| Hardware 100 MP | New canvas | 138.192 | 149.207 | 0.122 | 0.130 | 51.696 |
| Hardware 100 MP | Existing layer refused | 5.682 | 11.936 | 0.073 | 1.699 | 0.137 |
| WARP 4K | New canvas | 47.668 | 48.956 | 0.207 | 0.212 | 3.122 |
| WARP 4K | Existing layer | 54.865 | 68.007 | 3.145 | 3.150 | 3.533 |

Each successful import row records 20 frames; the refusal row records ten. All import rows have zero GPU
readback copies/maps/events/failures. Maximum message dispatch is 30 microseconds across these import rows;
maximum UI action is 125 microseconds. All four runs complete all 24 operations. Logs:
`.build/p1-image-import-benchmark-driver.log` and
`.build/p1-image-import-{hardware-4k,hardware-45mp,hardware-100mp,warp-4k}.log`.
The source freeze was rechecked after the measured driver exited with no changes to its 320 inputs.

Worker migration alone does not meet the 2 ms application budget: measured existing-layer application at
hardware 4K/45 MP and WARP 4K exceeds it. The hardware 45 MP frame cost also leaves G1 open. These aggregate
scopes do not independently attribute the remaining cost; history, chrome, releases and uploads remain P1.9/P3
work. Other rows still expose synchronous pixel edits (100 MP Clear reaches 3.579 seconds of message dispatch),
which remain P1.5 work. Full operation migration, clipboard processing, pending IO/codec interruption, faults and
native ARM64/RAW runtime qualification remain open.
