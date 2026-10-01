# Capped WIC memory encoder preparation

Status: private prototype passes; native integration/qualification pending.

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