# P1 recorded stroke-preview prototype

Status: isolated x64 Debug experiment passes on WARP and hardware; application integration and G1 pending

## Scope

The primary's ignored `StrokePreviewGraph-r1` graph records a private working grid and document canvas.
An original scheduler parent, dedicated recorder and actual GPU command pump execute the work. A next
graph copies the previous GPU grid before uploading changed tiles; failure preserves the previous canvas.
This adds allocation/copy costs that remain unmeasured. It is not normal application activation.

The cheaper agent supplied an isolated harness. Primary review fixed optional-asset handling, the missing
`ReadRegion` fill argument, COM apartment query arguments, literal row dimensions, a misleading terminal
failure label and an unrelated pixel-result/retirement assertion. The old failures are preserved.

## Independent pixels and lifecycle

- Translated/scaled two-pixel BGRA input: exact literal first tile, second changed tile, retained first tile,
  unchanged previous canvas, and equality with the existing direct composition.
- Placed source-empty Gray8 mask: actual fill 96, a changed full-mask tile, hidden edge outside placement,
  canvas clipping and opacity 3/4. Source BGRA `{64,96,128,255}` gives byte-space
  `{4608/255,6912/255,9216/255,72}` through fill 96, hence `{18,27,36,72}`.
  The full tile gives `{48,72,96,191.25}`, hence `{48,72,96,191}`. These rational expectations avoid
  half-byte boundaries and were written before the successful run.
- Empty changed set retains the source. Seven invalid source/target/format/window/grid/tile/fill inputs
  return exact `E_INVALIDARG` without an original command acknowledgement.
- Actual original GPU acknowledgements per graph are 12, 9, 12 and 11, on both devices. A separate captured
  original-context barrier acknowledges the final readback before detach. MTA worker destruction precedes
  the parent's zero final charge. The fixed 328 MiB declaration is an experimental fixture cap.

Mask-93/50% attempt `warp-hardware-r2-1` and mask-96/50% attempt `warp-hardware-r2-2` failed exact literals
at half-byte boundaries; recorded and direct composition bytes agreed. No production shader, legacy golden
or tolerance changed. The missing-argument build failure is preserved under `FailedBuild-r1`.

## Successful actual run

Attempt `warp-hardware-r2-3`, PID 121488, 2026-10-07T23:23:25.3143132Z–23:23:25.9295069Z,
exit 0, no timeout. Both `driver=WARP` and `driver=hardware` execute with 44 graph acknowledgements each.
Executable `98F58681B71549949A4D711A75AF793A28EFD2166E893C65375EA089C86F80DE`;
stdout `9F01601C0FE3FDAC7572054A7952051B2A4C92BC56C11C3C1108835B1BD2653D`;
stderr empty (`E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`).

Archive `D:\RedPrismQualificationArchives\P1-stroke-preview-probe-WARP-hardware-r2-3-2026-10-08` contains
168 verified copies, including all three actual run directories, products, four primary graph sources,
test sources, candidate dependency inventories, toolchain and unique build logs. Copy manifest
`D522C41F7549A29C3F5E4F096A8903391EBB5C7B8065306572BD53B78DCC78AE`.
The inventory is a candidate/compiler trace, not complete canonical profile qualification.

Original-parent display-seed ownership, full production peak admission, normal Append/Up/display/rows
integration, current canonical profiles and measured G1 remain required before P1 Done.
