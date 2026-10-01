# Capped WIC memory encoder preparation

Status: native capped encoder qualified in all six configurations; production clipboard graphs remain open.

## Scope and contract

Memory PNG/JPEG output uses an owned capped IStream, reserving virtual address space once and committing at most
64 KiB per call. Cursor/size/cap arithmetic is checked before growth. Write/read/gap clearing use cancellable
64 KiB bands. Newly committed pages already contain zeros; no redundant full zero pass is made. Shrink/regrow
clears stale committed bytes. Owned COM references keep borrowed final bytes alive until worker copying finishes.
Only successful completion exposes the final vector. A sticky refused-cap flag prevents a codec from ignoring a
failed sink operation and publishing a truncated result. File encoding keeps its checked direct file destination.

`EncodeMemoryCapacity` declares `min(512 MiB, 4 MiB + 2 * logical pixel bytes)`, zero for no raster. The optional
lower caller ceiling reaches the stream before allocation; a larger caller ceiling cannot expand the declared
limit. Valid zero/over-cap memory encodes return TooLarge with empty bytes. Cancellation returns Cancelled.
These values bound the sink and returned vector, not opaque WIC allocation or the complete clipboard graph.

## Independent private proof

- 160 stream assertions cover literal bytes, exact/over-cap unchanged state, signed seeks, stale-byte clearing,
  COM identity/reference lifetime, invalid arguments, native reserve/commit faults and reached cancellation.
  Independent Windows page metadata verifies rounding. Three bounded commits expose zero-filled new pages;
  stop/failure at the second/third commit retains allocated pages but exposes no logical output.
- 34 codec controls cover BGRA/Gray PNG and Gray JPEG, exact encoded-size ceilings, one-byte-short/one/zero
  refusal, uint64-max clipping, empty/cancelled output, literal alpha pixels and untouched sparse sources.
- All 103 existing ImageCodecWork checks pass unchanged, including multiband sparse PNG/JPEG, white matte,
  import/probe budgets and destination preservation. No legacy numeric expectation is rebaselined.

The cheaper test agents supply bounded sidecars. Primary review corrects automatic fault injection accidentally
active in cancellation controls, page-size assumptions, absolute zero-band offsets and a self-comparing borrowed
prefix; it adds sticky-cap/null-output cases and bounded commit controls. The initial pure-stream run has five
failures and is byte-preserved with source/project/logs under `.build/p1-codec-memory-draft/r1-test-failure`.
The corrected pure runs pass; the final count is 160. The integrated encoder's first executable cannot load its
missing dependent DLLs (0xC0000135); it executes no tests. Preserve all 128 inputs, image/PDB and logs under
`.build/p1-codec-encoder-probe/r1-loader-failure`; restore byte-verified DLLs from the prior qualified snapshot.
The unchanged encoder image then passes all three suites (`run-r2.log`). Native qualification is still required.

## Remaining work

Admit the complete Copy/Cut/Paste graph: source/private document/history owners, stream/vector overlap,
row/conversion buffers, PNG/DIB vectors and movable blocks, receipt/hash owners, codec workspace and retirement
headroom. Actual host callers, acknowledged retries, origin preference, New Canvas guards and measured G1
remain open. No UI latency, native ARM64 runtime or Photoshop speed claim follows from private codec proof.
## Native activation

After receipt-binding commit `9fe915b` and its 456-input source/binary archive, verify every qualified native
input and all 128 private proof hashes. Copy the exact proved encoder/stream/test bytes, register both suites and
compile the stream in the application and IoTests projects. Native formatting checks 411 files. A new 462-input
six-profile freeze will qualify these sources; do not edit native inputs until the matrix process exits.

## Native qualification corrections

The first frozen run passes x64 Debug, then Release rejects six switch-table DWORDs in KernelBenchmark as an
EVEX instruction. Preserve all 462 inputs and image/PDB/logs under
`.build/p1-capped-encoding-r1-Release-isa-failure/`. Exact decoded guard/dispatch bytes, matching PDB procedure
end and six validated destination RVAs prove this specific terminal RDX table. Add a strict parser with 66
independent positive/negative controls; all 434 controls pass, and the unchanged failed image passes the scan.
Actual instructions remain checked and the runtime allowlist is unchanged.

The second frozen run passes x64 Debug, then Release correctly rejects 16 actual VEX/EVEX instructions in
`CancellationStopsWriteAndReadAfterOne64KiBBand`: MSVC vectorizes the fixture's byte-pattern multiplication.
Preserve all 462 inputs and 23 evidence files, including exact PE/PDB/disassembly/attribution, under
`.build/p1-capped-encoding-r2-Release-fixture-isa-failure/`. Disable vectorization only for that fixture loop;
its byte pattern, cancellation checkpoints and assertions remain identical. This is a real instruction failure,
so no parser exemption or allowlist change applies. Restart qualification with Release first. Neither failed
run qualifies Release runtime, ASan or ARM64.

## Qualified native result

The distinct r3 run passes x64 Release, Debug and ASan Debug tests, then ARM64 Release, Debug and ASan Debug
clean cross-builds. The 462-input freeze is unchanged at explicit process exit. Skills validate 15 entries;
format checks 411 files. Every x64 run passes 160 stream assertions, 34 cap controls and all 103 existing
ImageCodecWork checks. Every x64 build passes all-image ISA scanning and 434 independent parser controls.
ASan's real diagnostic probe, worker crash probe, WARP self-test and application-loop smoke gates pass.

Preserve all 462 physical inputs, five Release image/PDB pairs, runtime DLLs, logs, freeze and failed-IO-scan
correction evidence in `.build/p1-capped-encoding-r3-qualified/`. The failed r1/r2 snapshots remain distinct.
Release version is `0.1.137.0`; app SHA-256 is
`395F4C7AD9C906F9F4F0A79E2F5F8D832383E02569BE4BA5EABB793309BE3578`.
Freeze SHA-256 is `4B623FB038AFBDC191439C2C536A1E36D14E9A9E1671842C5FABD65F88AA5329`.
The runtime allowlist remains `6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.

Close only F5b.2b.1. Opaque WIC workspace, complete host admission/callers/retries, measured G1, native ARM64
runtime and Photoshop comparisons are not qualified by this result. Detached Paste and host clipboard work
remain separately tracked unchecked slices.
