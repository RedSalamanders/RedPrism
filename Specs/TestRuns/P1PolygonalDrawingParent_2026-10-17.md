# Polygonal original drawing parent — 2026-10-17

Status: qualified scoped CPU component, adopted. P1 remains WIP.

## Scope and actual proof

The new original `PolygonalDrawingJob` covers Add/Subtract and New with absent/explicit-empty prior.
New with nonempty prior refuses the drawing-only plan. Cached description precedes immutable capture;
opening, event-driven park/replay, terminal geometry/history, residency and final retirement keep one
original ticket/admission. Owner replay consumes at most sixteen metadata samples per continuation.

All **23,401** CPU assertions pass complete scoped Release and real ASan: all 23,307 predecessors plus
94 new facts over twenty calls. The new checks use independent 120-center formulas for rectangle,
union, difference and 15-center triangle results. They cover history/Undo/Redo, captured and terminal
AA, close ordinal/opaque intent, hover/Backspace, conditional/double-click/older close, bounded replay,
overflow Lost, invalid ordinal refusal, scarce dispatch before capture, forged source, foreign Begin,
parked/prepared cancellation, joined Close and last-reader accounting. Main checks every original
roster and runtime byte; no runtime output defines expected assertions.

Fresh 284-TU builds per profile qualify exact compiler inputs/commands, objects, executable/full-PDB
identity and zero ISA violations. The fixture correction rebuilds exactly one TU per profile; all 283
other object bytes and all production source bytes remain identical. Final formatting sorts two
declaration-header includes in the driver. A separate one-driver-TU rebuild per profile preserves all
283 other objects, and both complete runtime suites repeat on that exact formatted source. Superseded fixture objects are
copied and independently hashed before replacement. Final build archives contain 901 Release and
906 ASan files; runtime archives contain 18 and 22 files, all independently read back.

## Retained findings and corrections

1. Main rejects the first prototype retirement loop before runtime: independent roots cannot each
   consume the original retained slot. Use one persistent output/temporary partition. This correction
   is exercised by cancellation, joined Close, pre-capture refusal and last-Backspace tests.
2. R1/R2 actual compiler failures retain all 880 archive files and 283 objects each. The fixture lacked
   `SelectionHitWork.h`/the `SelectionDisplayValue` wrapper and used `near`, a Windows macro. The
   focused follow-up preserves all 1,050 failed compile/source copies before an identifier-only repair.
   Both selected new TUs then compile at /W4 /WX; no warning suppression or numeric reference change.
3. Release R3 preserves 14 runtime archive files: the fixture wrongly demands synchronous memory
   refusal at descriptor submission, then overlaps two thread-local release sinks. Correct it to
   observe dispatch `E_OUTOFMEMORY`, zero Run/capture, exact original input drop, and joined sink
   teardown before the next fixture. Production code is unchanged. Failed completion:
   `8E435B7198194B2BE68BAE39A3924201A97A6B4ECB56585ED38BA9C25F3D9FF1`.
4. The first incremental recipes refuse before launching a compiler because their temporary directory
   already exists. Retain those recipes/logs/source pins; use fresh attempt directories. No product or
   source changes are attributed to that refusal.

The source-derived reader has four isolated byte-preservation positives and thirteen refusal controls.
It validates the complete unchanged predecessor input mapping and forwards predecessor bytes unchanged;
no successful predecessor transcript is synthesized.

## Receipt identities

| Receipt | SHA-256 |
| --- | --- |
| Release build | `4FE698B6F65F8F74110B413EA38E8F8D7227210E313C467AFBB9A221A3206105` |
| Release runtime | `72CA448860230AF18A40169642ECC293A47A8CC5F4AB97F2BC6B9DE47D472161` |
| Asan build | `438FEB384738762D1F005254E49463ABE5E1ED907FCF914FB7FCFFE4606D3C0A` |
| Asan runtime | `B31753570F0C69AC1ADF5CDC0F446F206D991263B0616DCB7868D6104C24E941` |

Actual UAF control PID 108412, expected exit 1; diagnostic SHA-256
`E4D2629CA020FAF80D84971124967BE3B5C0F7A6F8BC384F49AD6B439593B629`.

Private source/readback lives in `.build/P1Drafts/S5PolygonalParent-r3-Main/` and final profile recipes
in `S5PolygonalParentCpuRelease-r6-Main/` / `S5PolygonalParentCpuAsan-r6-Main/`.
A cheaper agent provides the bounded protocol-case sidecar; Main reviews it and owns the production
state machine, memory/lifetime corrections, independent fixture, artifact readers and actual qualification.

## Remaining gates

The actual physical dispatcher, nonempty-New worker containment, multi-click pointer sequencing and
queued Save/tool/lifetime arbitration are separate. The component is registered in DocumentTests;
it does not activate the ordinary application route. Complete latest native Debug/Release/ASan,
analysis and ARM64 cross-builds, ordinary activation and measured G1 remain required for P1 Done.
Native ARM64 runtime is unavailable.
