# P1 selection outline output allocation and retirement — private component

## Current checklist

- [x] Main implements checked output data/buffer and shared-control accounting with the original publication owner.
- [x] Main corrects the agent's callback authority, null scheduler and expired-object versus weak-control assumptions before compilation.
- [x] Preserve the actual r1 fixture compilation refusal: an unnamed-namespace Raster alias conflicts with global Raster in two outer cleanup assignments. No runtime starts.
- [x] Fresh r2 renames only that test alias; production/test predicates, numeric values and roster are unchanged.
- [x] Actual Release build and complete DocumentTests pass, with exact source/dependency/product bookends and independent ordered readback.
- [ ] Complete original CPU/GPU admission, opaque D2D bounds/cancellation, view/device authority and actual host integration.
- [ ] Native Debug/Release/real ASan, ARM cross-builds and measured P1/G1 acceptance.

## Exact evidence

Task-owned private packet: `.build/P1Drafts/SelectionOutlineAccounting-r2-Main`.
Source inventory 1,290, canonical SHA256 `334D795E74DDCE5EEDDA579CB7B98FA40E03FF77A593590A38C435C16512FCD2`.
Execution-input manifest SHA256 `969930E9196B9416B450B1EB88B44E72958F8ABD950F56A67FF80029E1A6F451`.
Independent report SHA256 `C1AA0501E6D2D7AD87134AFB31518EC7322F6C308E6872288F1FAF7FCCAB69F1`.
Build child 23144 exits 0, 2026-10-12T14:23:16.575876+00:00 to 2026-10-12T14:26:56.550419+00:00, zero warnings/errors.
Actual DocumentTests child 43928 exits 0, 2026-10-12T14:27:10.287055+00:00 to 2026-10-12T14:27:20.893711+00:00.
Actual stdout SHA256 `B58039276932EE1BA6778A3E01F688C1600F14EA7DEFE9265E07E38BE20C73D9`; stderr is empty.

All 20,746 Core7 records remain in order; 49 original raster/view records and 38 new allocation assertions plus
summary produce exactly 20,834. Only the original named Wand scratch/allocation statistics are normalized
within their previous bounds; area/work and every other record stay exact. The reader refuses missing,
duplicated, changed and reordered records. This proof covers private Release x64 only.

## What changed and what was observed

Nine-plane output is constructed through MakeAccountedPublicationObject with a complete new pixel-buffer
charge and conservative control/allocation allowance. Source retains its older owner. The immutable local
limit refuses before a result escapes, and original-ticket publication binding seals future allocations.
The actual worker runner translates limit-one and sealed-owner bad_alloc to E_OUTOFMEMORY. Prior output
identity, pointer and size stay unchanged on both refusals.

One descriptor-reserved ticket splits into exact output and source/temporary partitions at the correct
successful DeferCompletion callback. Full charge survives the last strong raster. After joined deferred data
destruction, an expired source object still has a weak control retaining its partition. Dropping it refunds
only that partition; the output's weak controls retain the exact output charge until their final release and
real acknowledgement. A separate admitted worker release records an actual local-allocation decrease before
returning, with positive shared-control accounting still held, proving data retirement rather than inferring
it solely from weak-pointer expiration.

Existing fixed-DIP, eight-phase, clipping, translation, double-anchor, hole and literal pixel tests are unchanged.
The 64 MiB opaque D2D allowance remains planning headroom, not a proven bound. DrawGeometry/EndDraw
uninterruptible intervals, complete parent admission, GPU resource/data retirement, actual host routing and
performance remain open. No native selection source changed and this does not close S5 or P1.
