# GPU adjustment previews

## Implementation checklist

- [x] Add immutable worker-built lookup/coverage inputs for the six existing adjustment kinds; preserve direct CPU goldens.
- [x] Reuse DYNAMIC LUT and DEFAULT cube textures across settings changes; account for and retire their storage.
- [x] Add a layer-grid adjustment pass that preserves alpha, mixes through edit selection coverage and keys destructive grain in layer pixels.
- [x] Draw the adjusted layer through its ordinary opacity/mask/blend/clipping/folder passes; keep passes 6/7 for adjustment layers.
- [x] Activate worker-prepared GPU previews through the existing one-active/latest-pending mailbox and owning result validation.
- [x] Preserve preview off/identity/Cancel/reopen, settled zero-pass frames, hidden release and device recreation.
- [x] Prove WARP parity against RunFilter plus BlendThrough on translated/scaled layers, selection, masks, clipping and grain; prove lookup reuse.
- [x] Run skills/format, all x64 test configurations and ARM64 cross-builds; record real-loop measurements and remaining costs.
- [x] Reconcile owning contracts/checklists, commit/push and move this plan to Done.

Status: DONE. Parent: [UiNeverBlocks_2026-09-22.md](../WIP/UiNeverBlocks_2026-09-22.md), P1.2.
Owners: `Common/ColorEditWork.*`, `Common/AdjustEdit.cpp`, `Common/Document.h`, `ApplicationAdjustJobs.cpp`,
`CanvasRenderer.*`, composition shaders and CompositionTests. Graphics resources remain on the UI thread;
lookup construction and selection rasterization remain immutable worker work.

## Data and rendering contract

The worker returns the prepared source, layer placement, preview scale, captured settings/seed, selection coverage
and lookup values under the existing opening/sequence/target token. It does not acquire a D3D device or mutate the
session. Direct inline preview/commit APIs remain the CPU reference. A GPU preview is never mistaken for a completed
full-resolution CPU raster when committing; commits use the existing worker graph.

A new pass adjusts one layer's source into a transient target on that source grid. It unpremultiplies, applies the
table/cube/grain operation, repremultiplies while retaining source alpha, and mixes original/result through the
edit's Gray8 coverage. Destructive grain positions are `(pixel + 0.5) / previewScale`. The normal layer pass then
uses this target with the layer's real transform, blend mode, opacity, mask and coverage dependencies. Adjusting
the composite beneath a layer (pass 6) cannot implement this preview.

The target and lookup textures are reused when dimensions/kind permit. Table updates use DISCARD on one DYNAMIC
256×1 RGBA32F texture; cube updates use one DEFAULT 33³ texture. Settings changes do not recreate immutable
textures per tick. CPU cache keys retain their owning Raster/lookup handles. A completed intermediate preview may
remain visible while a newer request waits, without rolling controls back. Cancel/identity/preview off invalidate
the appropriate completed preview and pending mailbox output.

Hidden release destroys graphics surfaces but retains only the CPU handles needed to reconstruct a still-live
preview. Device recreation recreates resources on the new device; old resources cannot be borrowed by a new
generation. Settled pan/zoom frames continue to perform only blit work. GPU completion/readback infrastructure is
P1.3; view-resolution filters and sliced GPU dispatch remain P3.

## Validation reference

Use the existing scalar `Adjust::RunFilter` and coverage `BlendThrough` as the independent pixel reference, then
compose that raster through ordinary layer passes. Compare the new pass's composition at the owning per-kind
tolerances (table 1/255, cube 4/255, grain 2/255). Include partial alpha, translated/scaled placement, selection,
underlying different colors, upper layers, masks, clipping stacks and lookup changes. Assert unchanged texture
creation counters after warmup and zero extra compositions on a settled frame. Do not change legacy expected pixels.

Progress: all checklist items completed. All x64 tests and ARM64 cross-builds passed, with Release real-loop measurements recorded in [GpuAdjustmentPreviews_2026-09-30.md](../../TestRuns/GpuAdjustmentPreviews_2026-09-30.md). P1.3 and the rest of P1/G1 remain open.
