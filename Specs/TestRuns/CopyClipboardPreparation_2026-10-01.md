# Copy and clipboard transfer preparation

Status: preparation primitives qualified; production callers and G1 remain open.

## Scope

`CopyPixelsWork` prepares immutable image/mask/merged pixel copies, selection coverage, cropped origin and
checked private memory. `ClipboardTransfer` prepares immediate PNG/DIBV5 movable blocks, performs worker-owned
Win32 transfer and seals incoming sequence/limits before owned byte copying and decode. `OwnerWorkMultiplexer`
routes at most eight owner sources without allocation, waits or callback-time registration changes.

These modules have no production callers yet. Atomic Cut/private clipboard publication, complete admission,
post-close private-origin authority, timer retries and Application source routing remain open. No UI latency,
RAW speed, Photoshop comparison or native ARM64 runtime claim follows from these primitives.

## Independent controls

| Suite | New assertions | References |
| --- | ---: | --- |
| DocumentTests | 33 | Literal BGRA/mask pixels, independently derived AA area, copied origin, sparse rows, source/coverage/rows/publication cancellation, allocation propagation and large metadata-only memory floors |
| IoTests | 125 | Literal PNG/DIB, straight versus premultiplied alpha, sealed inspection limits/sequence, partial/empty publication, reached native faults, early/late cancellation, delayed renderer and reads after immediate owner destruction |
| HostTests | 31 | Fixed capacity, leaf registration/refusal, pending versus ready, literal round-robin order, reused slots, recursion and callback registration refusal |

Native clipboard waits are test-only readiness/join failure bounds, including a separate 60-second complete-suite
bound. The controls preserve and restore the user's clipboard. They do not measure nominal clipboard latency.
The outgoing sequence is independently observed after the last placement while open; no assumption is made that
it equals a later-open sequence. Private-origin Paste authority must resolve that difference before activation.

## Initial Release failure

The first clean run passed x64 Debug, then Release's linked-image ISA gate rejected three actual `vpsrlvd`
instructions in the new test fixture's four-byte `Put32` loop. Disassembly and PDB inline records identify
compiler automatic vectorization, including compiler capability branches. This is not inline table data.
The fixture now writes the four little-endian bytes explicitly. The parser and runtime allowlist are unchanged.
No test expected pixel was changed by this correction.

Before editing, all 434 frozen physical inputs, five Release images/PDBs, app-local DLLs, logs, disassembly,
symbols and contribution metadata were byte-preserved and hash-verified under
`.build/p1-copy-transfer-r1-Release-isa-failure`.

- Source freeze SHA-256: `71BEF80B49FB3C7D1CF33CBA2DB8F0021C4BD25F640FB834DACAA64921790D49`.
- Failed IoTests image SHA-256: `D23883CE07C0B6DAC56935AB58F7B527BF53D2C7E3400097E11B5624864E6BB8`.

## Later preserved failures

The r2 Debug gate passed. Release passed ISA and all 189 new controls, then failed three existing folder rollback
assertions. Its exact 434 inputs/images/logs are preserved under
`.build/p1-copy-transfer-r2-Release-rotation-failure` (freeze
`592C183E0025CE4AD206343311530D1DA12BC16FC42F726C9B2ED466EA4EE743`, IoTests image
`AF800843238AE90A170A2FD799C2A0DC80D8541CD89593CFFF7C9F89626F8C56`). The unchanged preserved IoTests image passed
on recheck. The initiating OS error was not logged and remains unknown. Review found two old fault counters that
covered only eight stages and would refuse the later destination timer stages. Both now cover the complete enum;
three independent controls require those deadline stages to pass through the two-point fault policy. Conditional
stage/error/counter diagnostics preserve the original rollback assertions and production save policy.

The r3 Debug run passed those folder controls but failed one new partial-publication assertion. Its exact inputs,
Debug images/PDBs/DLLs and logs are preserved under `.build/p1-copy-transfer-r3-Debug-clipboard-failure` (freeze
`DB0B886B7D189711B32E8E834677A5D7F9CFEBEF6C044F20501C87A14A3AD30D`, IoTests image
`3B9774BD7F69E511647FE3687B5F6B47BE8554D92A2F1C514F2408F5F4E295FF`). The assertion did not print its Publication
fields, so the original status is unknown. This case still used one native open attempt; unrelated listeners can
refuse that open before its injected format failure is reached. Partial-format and native Empty fault cases now
use the existing bounded test-only Busy readiness retry. It retains the sealed source sequence, stops on stale,
cancellation or any emptied result, and never retries partial publication. The explicit injected Open and actual
held-Busy controls still make exactly one attempt. Publication truth and expected bytes remain unchanged.

## Final clean qualification

The r4 matrix passed x64 Debug, Release and ASan Debug full tests, including the real sanitizer probe, and
ARM64 Debug, Release and ASan Debug clean cross-builds against the same 434 frozen inputs. The 189 new controls
and three fault-policy completeness controls pass in each x64 profile. Skills (15) and formatting (383) pass.
ARM64 runtime, production callers and latency were not qualified.

Release version: `0.1.133.0`. The five Release images, matching PDBs, app-local DLLs, eight gate logs and exact
source/binary manifests are byte-preserved and hash-verified in `.build/p1-copy-transfer-r4-qualified`.

- Source freeze SHA-256: `1D6C5484530E8B94B8474846A6989816319969CE0E01487EB2FDCEE2D1B5CAC6`.
- Release application SHA-256: `D3CB47360F59D4DF08A9608A0D99ED66EF28AED89BD25B0241106219C320C07A`.
- Unchanged ISA runtime allowlist SHA-256: `6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.

The archive provenance records the implementation commit after publication. All earlier failed inputs and images
remain preserved. Only F5b.1, A5.4a and the owner multiplexer primitive close; their parent operations remain open.
