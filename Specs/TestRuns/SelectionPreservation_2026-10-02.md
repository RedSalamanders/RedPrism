# Selection commands preserving a live gesture

Date: 2026-10-02.

## Scope and independent expectations

S2a qualifies a separate Select All/Deselect preparation authority for an already open brush, warp or shape.
Settled history-restore authority remains unchanged. This core does not route production commands or pointer
input; ordered gesture closure, host writer barriers, scheduler admission and measured G1 remain open.

Brush/warp identity uses weak control-block ownership comparisons; the worker neither owns nor reads mutable
gesture state. Shape authority binds a monotonic opening counter, independently of changing drag bounds.
Cancellation/reopening, including an identical shape, refuses the old result. Document serial, generation,
restore serial, history epoch, active target, mask targeting and crop changes also refuse publication.
Panel/gradient/transform/lifted-pixel/lasso/outline-move contexts are refused. Continued samples and shape drags
within the same gesture are permitted by this core boundary.

The 136 unique `selection-preserve` assertions use literal full-canvas clockwise geometry with AA enabled and
absent selection for Deselect. They cover exact history/name/depth, saved markers, no-op redo, moved/foreign/
single-use inputs, reached cancellation and allocation-failure stages, recovery and gesture replacement.
Independent committed-pixel checks start with a 2x2 selection on a 4x4 source, paint a white diameter-1 brush,
publish either command and append outside the original clip. Finishing preserves every literal source pixel
outside the original selection. Undo first restores brush pixels while retaining the command's selection;
the next Undo restores the original selection. The core therefore preserves the stroke's captured coverage.
Existing legacy numeric goldens are unchanged.

The reservation adds the inline BrushStroke/WarpStroke/control allocation retained by weak identities after
strong cancellation to the existing conservative history/input/result bounds. Destructed gesture vectors and
rasters are not retained by the weak references. This is declared ownership accounting, not measured RSS or
constant-time H2/H3 qualification.

## Private attempts retained before correction

| Attempt | Actual process result | Exact inputs / evidence | Archive completion UTC |
| --- | --- | ---: | --- |
| r1 | Compile failure C2660: the new test omitted ReadRegion's fill argument | 299 / 124 | `2026-10-02T19:28:11.0434314+00:00` |
| r2 | Compile failure C4267: the test passed a buffer size as the fill byte | 299 / 124 | `2026-10-02T19:29:15.8744388+00:00` |
| r3 | Build passes; full DocumentTests reports seven new fixture failures | 299 / 137 | `2026-10-02T19:32:22.8376303+00:00` |
| r4 | Final full Debug DocumentTests passes all 136 unique controls and existing goldens | 299 / 137 | `2026-10-02T19:38:26.8672034+00:00` |

The corrected fixture expectations are independently explained by existing behavior: a first Warp Append
records its opening but no point, repeated WarpImage calls need pixel comparison rather than raster-handle
identity, the committed title is `Brush Stroke`, and UndoName inside an open transaction names its prior entry.
No production behavior or legacy golden was changed to satisfy these tests.

Final r4 build: `2026-10-02T19:35:37.2735815+00:00` to `2026-10-02T19:35:48.0437090+00:00`, exit 0.
Final full runtime: `2026-10-02T19:37:07.9749585+00:00` to `2026-10-02T19:37:44.5321600+00:00`, exit 0.
Its stdout contains 136 distinct PASS names, the exact `136 selection-preserve assertions; 0 failed.` summary,
the full document completion marker and no FAIL line.

## Private provenance

Final archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-selection-preserve-private-r4-Debug-qualified`.
Image SHA-256: `D22F8DFF5FD6F19D80DABCF664A175A6A7D4AAC405754CFF35BA3545215AF3D2`.
PDB SHA-256: `AECCE112CA7F939B52B41289BBCF2BAB53197A04003FCA76C3768CC7E2210C2A`.
Source manifest SHA-256: `8C97E14F391F7611B40427859CF7978598CD575E461085C84A8E85272D39EA72`.
Evidence manifest SHA-256: `0C37F6C7E01C42CF5D2A0EB6453465D3FC64F7188A1D3433E970C196B3943F9B`.
The completion receipt and manifests are independent; every copied source/evidence/product is SHA-verified.
Earlier attempts retain their exact sources, products when built, compiler tracking and original logs.

## Native qualification

Combined S4b/S2a native r2 passes all six profiles at `2026-10-02T20:13:23.3037530Z`. Each full x64 suite passes
the exact 136 preservation, 117 wand, 71 cancellation/chunk and 23 ISA controls plus unchanged document goldens,
WARP self-test and harness gates. The x64 ASan suite includes the actual sanitizer detection probe. All three
ARM64 cross-builds pass; ARM64 runtime, production live-command ordering and G1 are not claimed.

Independent verification completes at `2026-10-02T20:15:19.1295699+00:00`; the immutable archive completes at
`2026-10-02T20:20:56.8817217+00:00`, retaining 598 exact native inputs and 7,223 evidence files. Source parity is
checked against the final S2a private source set, including its inherited S4b source. All manifests/copies,
product hashes and serial UTC receipts are verified before another build. See the shared native profile table
and archive provenance in [WandLayerSelection](WandLayerSelection_2026-10-02.md).
