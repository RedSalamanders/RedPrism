# Original Polygonal header widget release routing — 2026-10-19

Status: actual header-widget input qualifies focused Release and real ASan. P1 remains WIP.

## Captured failure and fix

The parked original journal returned true for every left release, including the Up matching a chrome button's Down.
DxUi received the press but never its release, so no Deselect callback or ordered command was produced.
The unchanged ASan control PID 14800 exits5:527 checks, one failure,48 completed cases. The public UIA root, bounded
localized enabled-button search, rectangle outside the canvas and point hit all pass before the queue assertion fails.

`RoutePolygonalPressOwner` now returns a chrome Up before reserving an ordinal or recording a raw sample when it is
outside the canvas and the original owns no left press. An admitted canvas press released outside still follows the
original path. No DxUi code or dependency change is involved.

## Actual input and qualification

The new case uses the normal arranged hidden WARP window. From the public `ChromeHost::AccessibilityRoot`, bounded
512-node/depth32 UIA traversal finds the localized enabled Deselect button. Its screen rectangle maps outside the
canvas, and ElementProviderFromPoint independently confirms the same named enabled button. Actual HWND mouse Down/Up
then create exactly one matching later host command. No direct callback, OnCommand or WM_COMMAND is used for this case.
Owned WIL COM providers are released before command pumping or owner destruction.

The existing Pending receipt, same-original cancellation acknowledgment, four completed corners, exact selection/
history/revision/generation and joined-refund checks remain. All 518 predecessor facts pass:49 cases /534 facts.

| Profile | Actual completed runtime (UTC) | Verified files | Artifact manifest SHA256 |
| --- | --- | --- | --- |
| Release | PID 84076 exit0; 2026-10-18T23:34:37.821348+00:00 | 65 | 3CA2AFA36E19B64456B8A8118CA8070273840565F1DB3C04064611D733D31B94 |
| ASan Debug | PID 25496 exit0; 2026-10-18T23:32:34.930242+00:00 | 67 | 682DC22A7B056BF9CE0C59421937DD497DA5F4E6E6FBD4C7EC3A15691BC4319E |

Both stdout SHA256:E8C15DEFB596A0E81AD1AB47E333A515B43F4FF1298E1D3809C0D60F4DCEDAFA.
Each profile has two fresh route/fixture TUs and 349 byte-exact matching-profile inherited objects. Main verifies every
source, mandatory compiler/real-ASan flags, all 351 linked objects, PE/full-PDB pair, zero ISA violations and saved/
exported files with independent PNG/JPEG/package readback. This is zero full-Application stages.

## Preservation and remaining gates

Cohorts:`D:/RedPrismBuildCaches/.build/p1-polygonal-header-widget-main-{r1,r2}`.
The unchanged r1 control and both r2 source maps are preserved as 2,021  distinct content-addressed blobs in
`original-header-widget-source-blobs.zip`, SHA256
9C4FD6F75E10102218CA5A3311BEEF7CE37EFC37A096936CC81D1988819D6901.
Main corrects the cheaper helper's PE section-field offsets, source root and missing control PE/PDB verification,
then checks all artifact bytes and all three source bookends. Preserve the preparation-only ancestor-path refusal.
No runtime is replayed to repair readers.

Latest full integration now expects 152 stages /11,118 stdout assertions. Actual menu widgets, keyboard hardware,
other header/lifetime/DPI/device/hidden/restart branches, initial Ctrl/lift, ordinary activation and measured G1 remain
separate. This grants no native ARM64 runtime or P1-Done claim.
