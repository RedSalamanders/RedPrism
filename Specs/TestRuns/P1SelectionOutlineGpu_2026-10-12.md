# Private selection outline GPU component - 2026-10-12

Status: scoped private Release/WARP PASS; S5 and P1 remain open.

## Actual execution

Build child 3796 exits 0 (12:46:39.316224-12:48:37.020902 UTC), zero warnings/errors.
Actual CompositionTests child 92236 exits 0 (12:48:37.906509-12:48:47.830003 UTC), empty stderr.
The complete 1,284-file private source and dependency/product bookends verify unchanged.
Every one of the 6,838 inherited assertion occurrences remains; 69 new assertions and their summary pass.
Actual stdout SHA256: `4DBEB850EAD9B2295BEF681286CC40ED0641BC7FC80B03EDD5AFF0D1F604091C`.
Main readback SHA256: `DDDE09D20CF957897C69DA06C664A079CFB8A6EB45E299B1809EBA175ED0CDD8`.
Reader SHA256: `BBB4B3A879F5E00FF21AA6851E5E940B16EDB119C74F9367578B4C480332033E`.
Generated packet: `.build/P1Drafts/SelectionOutlineGpuRoute-r2-Main`.

## Independent numeric reference

All 65,536 solid/dash R8 pairs run on transparent, translucent and opaque BGRA backgrounds.
For solid s, dash d and background byte c, premultiplied source-over color is
`(255*s*(255-d) + c*(255-s)*(255-d))/65025`; alpha is
`(255*(255*d+s*(255-d)) + a*(255-s)*(255-d))/65025`.
Integer reference rounds only the final byte; hardware UNORM tolerance is one byte.
Positive/negative/offscreen integer origins, clipping, eight independent phase resources, invalid
sizes/origins, released/incomplete resources, zero document/utility compositions and no retained
borrowed mask/blend bindings have explicit assertions. Readback occurs only in numeric tests.

## Retained evidence-reader refusal

The original reader refuses an exact record-order mismatch; its source and refusal report remain unchanged.
Main traces only the same three existing recovery acknowledgement checks moving relative to recorder-joined
notifications in two successful RetainOriginalForRecovery cases. The original Until loop drains both real GPU
and recorder completion events; neither notification precedes the other by contract. Ticket/device/ordinal,
callback capacity/detach prohibition, exact acknowledgement count and original retirement assertions still pass.
The corrected Main reader permits the adjacent triplet only after original failure and before the same
case's independent-compute barrier. It requires one triplet per case and exact order everywhere else.
Seven negative reader controls refuse changed/missing/duplicate/reordered/late callbacks, a changed numeric
record and a changed unrelated predecessor. No product/test/pixel/tolerance/source or runtime record changes.

## Remaining implementation

This establishes the existing composition's built-in pass 9 numeric draw component: one fullscreen draw,
fixed two-mask binding and small constant-buffer update; no owner contour walk, composition or upload.
New code remains private. Complete original CPU/GPU admission, same-device/mip/size authority, source/view/DPI
and device epochs, bounded uploads, final GPU use acknowledgement, replacement/cancellation/recovery/joined
Close, actual host routing and inclusive G1 measurement remain due. This is not ordinary activation or
end-to-end performance acceptance. All nine P1 delivery areas stay open.
