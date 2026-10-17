# Polygonal terminal accepted before controller admission — 2026-10-17

Status: complete scoped CPU Release and real-ASan qualification; five reviewed code/test paths are
adopted after the ARM64 baseline readback joins. The physical Polygonal caller and P1 remain WIP.

## Change and independent expectations

The fixed physical stream can accept Finish/Lost before the original controller exists. A const captured
physical ordinal seeds that same controller after admission; it grants no model authority. The prepared
writer/history/source capabilities remain mandatory. A claimed terminal while the stream is still sampling,
or a terminal ordinal that is not after Down, refuses with E_ABORT before source capture.

Fourteen source-defined facts cover accepted Up/Lost, younger opposite-terminal refusal, captured physical
ordinal and AA, exactly one publication/history entry or cancellation, contradictory metadata refusal,
original-prefix acknowledgement and final accounting refund. All 23,401 predecessor facts remain intact:
**23,415 total assertions**. Five code/test paths change; project membership remains 284 TUs. The source-only
reader derives the new block before runtime and forwards every predecessor byte unchanged to its original
validator. Four newline/BOM controls and 13 refusals pass; no complete runtime transcript supplies expectations.

## Exact qualification

| Profile | Actual Document PID / exit | Build files / actual TUs | Runtime files | Build completion SHA-256 | Runtime completion SHA-256 |
| --- | --- | --- | ---: | --- | --- |
| x64 Release | 60380 / 0 | 900 / 284 | 18 | `BACA76956E1DB132733C13C5E1C6023C0057D48A81F96B1A2D493BFCCFC73C1B` | `5F14A24A0B6A693EFEBD1132B3CE41C69B75624794A3AF457A8C87DC47BE3715` |
| x64 real ASan | 43316 / 0 | 905 / 284 | 23 | `6911F9FE6F7F26015C9BC7FF93FC3D1B7C2837EE88C88724864B3B352FAE61C8` | `AD2CF0B2C64189DDEEBDA650A00A69632621C1D671C6E87DBEAB64665CF160F5` |

Main independently verifies every source/artifact archive row, all compiler inputs/objects and profile flags,
exact executable/full-PDB GUID and Age, zero ISA violations, unchanged source/product bookends and the complete
assertion roster. ASan PID 33908 exits 1 with the required genuine heap-use-after-free diagnostic. The ordinary
suite exits 0 with no sanitizer diagnostic. No dependency pin, ISA allowlist or legacy golden changes.

## Preserved reader refusal

The final ASan archive reader initially expects 22 files. The actual archive has 23, including its new reader
metadata. Preserve the original reader and refusal log. Main verifies every one of the 23 live/archive file
lengths and SHA-256 values, unique paths and exact inventory before correcting only the expected count to 23.
The corrected reader checks the same completed process and bytes; no compiler or runtime is repeated.

Private source: `.build/P1Drafts/S5PolygonalHost-r1-Main/SourceFull`.
Recipes/readback: `.build/P1Drafts/S5PolygonalPendingCpuRelease-r1-Main` and
`S5PolygonalPendingCpuAsan-r1-Main`. The latter retains `main-filecount-correction.json`, original reader and
refusal. Exact original admission, final GPU/display ownership, actual multi-click dispatch, queued writes,
lifetime prompts, ordinary activation, measured G1 and latest milestone-wide gates remain separate work.
