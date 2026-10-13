# Private worker-prepared lasso draft raster — 2026-10-13

Status: private Release CPU component PASS; native/actual host/display/G1 qualification remains open.

## Exact gate

The reviewed isolated candidate preserves all 21,673 predecessor document records and adds 23 assertions
plus a summary. Main runs actual compilation, linked ISA and the complete DocumentTests executable.
Expected rows come from the earlier qualified input roster and the independent agent's fixed draft roster;
only the three existing bounded-positive wand allocation-statistic rows normalize. No legacy golden changes.

| Gate | Actual child | Exit | SHA-256 of stdout |
| --- | --- | --- | --- |
| Release compile | 12764 | 0 | `CAF6317C97DE4525C942AEFC483F7481D900194DEEEBC61E74344C995D7369E6` |
| Linked ISA | 44956 | 0 | `69956DCEA87E5DC041B29E0F25C113EA37BC7AC3A7620AE417831BFAA6E98AE6` |
| Full DocumentTests | 102520 | 0 | `1C1D08B6ABDD46376843C434EC4841BE3481616EE96501074B90E2AE0321112F` |

Qualified receipt SHA-256: `3ECDC305B55143263E78FEDAE96C8A42522F60E20560AC7B669D4B8F61AAB960`. Complete source/dependency/product bookends
and logs are under `.build/P1Drafts/LassoDraftRasterRuntime-r1-Main/actual-release-r3`.

## Independent observable facts

- Handwritten seed vectors, bounds and actual first polygon corner drive a separate D2D/WIC reference.
  Rectangle, ellipse, freehand and polygonal drafts match at 96, 144 and 192 DPI.
- Fixed BGRA values independently require white `(255,255,255,255)`, black shadow `(0,0,0,102)`
  and transparent `(0,0,0,0)` on the horizontal-line fixture; polygon handle interior is opaque white.
- A distinct original arena creator reserves 4 MiB; a distinct original raster ticket reserves 96 MiB,
  captures after admission and resumes the same ticket. The original charges survive source/result readers.
- Invalid grid leaves existing output unchanged. Insufficient/sealed local allocation fails via the
  scheduler's exact `Failed/E_OUTOFMEMORY` translation, with no function return or partial result.
- Event-gated cancellation before preparation and at a checkpoint produces no partial result, keeps the
  immutable source readable and retires original owners. Gates also observe joined shutdown.
- Dropping original writer/source references leaves the result's source readable; final release and
  original reservations drain. Timeout closes the producer before fixture storage can disappear.

## Retained failures and Main review

Private r1 compiler refusal retains complete source/logs: polymorphic Submit requires `unique_ptr<Job>&`
and `format_to_n.size` needs a size conversion. Main repairs only those fixture types.
Private r2 keeps its complete source/logs and two failed refusal references: local reservation rejection
throws `bad_alloc`, so the scheduler reports failure before assigning Prepare's HRESULT. Main requires the
exact terminal status/error and unchanged sentinel instead of inventing a returned HRESULT. Pixel,
DPI, cancellation and history expectations remain unchanged.

Fractional view clipping is deferred to the composition once after the complete premultiplied draft;
per-command clipping would change edge colors. The CPU gate above tests unclipped raster bytes; GPU
fractional-clip numeric proof remains separate. The 64 MiB opaque D2D allowance is planning headroom,
not a proved private-allocation or cancellation bound. This evidence enables no ordinary application route.
