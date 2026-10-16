# P1 Selection Gesture Admission — 2026-10-17

Private Release R3 recovery and real ASan are independently qualified by Main; R1 remains a functional/bound-fixture checkpoint.
The strengthened R2 compiler failure remains retained. Actual cached-admission Application runtime and broader P1 gates remain open.

R1 `main-qualified.json` records 26 new + 22,296 predecessor assertions = 22,322 passing, 271 actual compiler
records, zero ISA violations and 1,613 archived files. Actual PID 5416 exits 0:
`2026-10-16T23:18:07.775104Z`–`2026-10-16T23:18:16.500195Z` (UTC, October 16; October 17 locally).
The accompanying `qualification-limits.json` retains the source-audit gap: active layer, crop and mask can change
without document generation, while history `Begin` changes pending state/epoch without generation or revision.
The old guard could capture fresh opaque opening authority after such a change instead of checking the original
editor/history witness. R1's successful runtime remains valid within its fixture scope; no failing runtime is inferred.

Strengthened R2's actual compiler child (PID 72436) exits 1 with C2661: the new mask fixture calls
`Raster::Make` without `byteCount`. No runtime launches. Main verifies 1588 failed-archive files; completion
`37ABEF1B1BC81EC2EBE293E62A80D5C586971FC3B4E4E06036023050B7CD8F19` is retained in
`.build/P1Drafts/S5PressAdmission-r2-Main/main-failed-verified.json`. After reading the five-argument pixel factory
(`Common/Document.h:196`), Main repairs only `&white` to `&white, 1`; all 12 predicates are unchanged.

R3 `S5PressAdmission-r3-Main/main-qualified.json` records actual runtime PID 30900, exit 0,
`2026-10-16T23:30:24.639902Z`–`2026-10-16T23:30:33.360301Z` (UTC). All 12 new authority facts plus all 22,322 prior assertions pass,
22,334 total, with 272 compiler records, 1,615 archive files, zero ISA violations and source/dependency/product bookends.
Main's exact hashes refuse the initial assumption that all 271 prior objects were unchanged: 245 are reused,
26 existing TUs are recompiled, and the new fixture adds one compiled TU (27 total); the actual build log names the same 27.
The sole source change remains the fixture's added `byteCount=1`; all 12 predicate expressions and labels are unchanged.
[Repair and reuse verification](../../.build/P1Drafts/S5PressAdmission-r3-Main/repair-and-reuse-main-verified.json)
retains the refused assumption and exact object list. The 22,334 passing runtime assertions and 272 actual compiler records remain valid.

Real ASan `S5PressAdmissionAsan-r1-Main/main-qualified.json` records runtime PID 18964, exit 0,
`2026-10-16T23:37:06.401221Z`–`2026-10-16T23:39:34.271747Z` (UTC): 26 admission + 12 authority + 22,296 predecessor facts = 22,334 passing,
272 actual instrumented commands, zero ISA violations and 1,621 verified archive files.
Its own `--asan-probe` (PID 80912) exits 1 at `2026-10-16T23:37:06.108949Z`–`2026-10-16T23:37:06.287111Z`,
with the expected heap-use-after-free diagnostic. This nonzero detection control is separate from the successful suite.
Main's `S5PressAdmissionAsan-r1-Main/qualify-private-asan.py` verifies complete source/dependency/product bookends,
exact executable/PDB identities, the full 60-row ASan profile, and 14 new-family plus 14 engine negative controls
(seven existing-reader and seven independent controls). Archive completion is recorded in the identity table below.

Qualified controller `.build/P1Drafts/S5PressAdmission-r3-Main` uses the repaired source still under
`.build/P1Drafts/S5PressAdmission-r2-Main/SourceFull/`. Its typed
`SelectionOutlineGestureState::Admission` holds `{DocumentReadAuthority, HistoryRestoreAuthority, DocumentHistory::Authority}`.
The descriptor reads cached metadata and scalar witnesses before admission, without graph capture, traversal or
retaining source/history/outline roots. After original scheduler admission, all three witnesses are checked before
retained source and opening preparation. Fixed-limit fixture callers remain distinct from this cached-descriptor route.

The following phase formula is exercised by the qualified private Release/real-ASan fixtures. `H = history.EditMemoryBytes(document)`,
`R = HistoryRestoreRetirementMemoryBytes()`, and `G = loops.StorageBytes() + SelectionLoops::WorkOwnershipAllowance(loops.size())`.
`owner = SharedPublicationMemory::OwnershipAllowance()`; fixed wrapper allowance
`W = 4096 + sizeof(SelectionOutlineGestureJob) + sizeof(SelectionOutlineGestureState) + sizeof(BrushStroke) + sizeof(WarpStroke)`.
Products and additions, including `G` and the total, check `uint64_t` overflow; unusable/zero metadata is refused.

| Phase | Charge | Ownership |
| --- | --- | --- |
| Descriptor, before admission | Cached H/R/G and scalar tuple only | No retained graph roots or work payload capture. |
| Opening | `2H + W + owner` | Continuing publication owner survives useful work. |
| Final | `4H + 2G + W + owner` | Candidate/refresh metadata and translated geometry/construction overlap. |
| Temporary/retirement | `4H + 2R + 2G + W + 3owner` | Original temporary payload covers abort history and old-root retirement ownership. |
| Original admission total | `opening + final + temporary` | One descriptor covers overlapping phase owners. |

Source anchors (relative to the shared source path above): `RedPrism/SelectionOutlineGestureJob.cpp:12` describes cached
admission and checked phase sums; `:140` checks the full original tuple before retained roots;
`RedPrism/SelectionOutlineGestureState.h:22` defines the typed tuple and `:48` stores the optional admitted witness.
`Tests/DocumentTests/Document.Tests.SelectionGestureAdmissionAuthority.cpp:93` introduces late Crop/Active/Mask/HistoryBegin
changes before owner drain. Its four cases pass 12 facts in Release R3 and real ASan: refusal before capture/preparation, preservation of
foreign metadata/literal selection/generation/history, and complete original reservation refund.

`Tests/DocumentTests/Document.Tests.SelectionGestureAdmission.cpp:48` creates the approximately 45 MP
**sparse** 8192 × 5464 image grid with a 65,540-point contour; `:103` checks descriptor scaling without materialization.
This is a sparse ownership/admission fixture; it establishes neither RAW decoding nor speed/performance evidence.

Selected identities come directly from the qualified receipts; receipt-file SHA-256 values are recalculated with `Get-FileHash -Algorithm SHA256`.

| Artifact | R1 Release SHA-256 | R3 Release SHA-256 | Real ASan SHA-256 |
| --- | --- | --- | --- |
| `main-qualified.json` | `1D6F21EE7223385CC92C262043A0244A2A5DACB47F8CF55356D9E88C7E3F0557` | `AB77E38B46091AC31F2A1FBB7C60DB132B3D1B4F12EB35397BE14650E0469987` | `1FD37BB6218494E3A9331A3CD8985D01837DEDB7C979C2F672870F4DE0A9112B` |
| Runtime stdout | `32B3B22A3A14264EC1DC39B216E3FB58236CF556C017302A3783AB72A83007DB` | `C026936FDD05A7D3F5B504B7D6B09FCBFA3B88A76D23EF4C6CE4B997DB4239BE` | `5100AFA74E62B8511BBF8EF62C7761B6F118B1712364AD1D2A0F4DBE901379C0` |
| Executable | `A169B7C83BEFA9E92C0E15E71E2969A44E43D86288DA36C5CA6E3EAC1F2B526B` | `B31B87150A67D061C89BE911D7284D7071E00AB81267276E77A47481833A8562` | `727D4439C38323CD224997D5E66C4A2370A2AADCE0DD0456257D70DA9B828833` |
| PDB | `6B240346BF02A08E80E4DE10D587B1665E31B4A1FC4C99861BECC491E82C4581` | `3AEECA2F9BBEB57E967AA80D2B134BE77DCE4145D1458D1DF436879291426F91` | `80C971839381FE584E001C286163FD9FA344165B293CCDB93186BFE1EBDD3EF4` |
| Archive completion digest | `5F1C4ECAC22913C9B1A391BEE2CFEC04CDF488EF3829CE762AF86C8B0736D825` | `CB01FA01AF21658A01CA23445C42743365997234EE2344C23C3CB6B3C270971F` | `66130B955AB10FD0688777D1A03E2B3BDCAB1291CADA46D21225966B68B196C7` |

R1 receipt/limits: `.build/P1Drafts/S5PressAdmission-r1-Main/{main-qualified.json,qualification-limits.json}`.
R3 receipt/archive: `.build/P1Drafts/S5PressAdmission-r3-Main/{main-qualified.json,archive-receipt.json}`.
ASan receipt/archive/qualifier: `.build/P1Drafts/S5PressAdmissionAsan-r1-Main/{main-qualified.json,archive-receipt.json,qualify-private-asan.py}`.
CodeView: R1 `AD433FFA-C3AD-4A3C-996D-17C48E562B08`, age 1; R3 `5D91243B-8C13-4441-A0D3-3E64EFA9D395`, age 1; ASan `B60B6356-2496-42E3-BD54-C7B4C5E3DEFD`, age 1.

Qualification and remaining gates:

- [x] Main independently qualifies R3 Release: all 12 + 22,322 = 22,334 assertions and the 1,615-file archive.
- [x] Main verifies 245 reused prior objects and 27 compiled TUs (26 existing plus the new fixture); the initial reuse assumption is retained as refused.
- [x] Main independently qualifies the CPU real-ASan step: all 22,334 facts, 272 instrumented command records, its own nonzero UAF control and the 1,621-file archive.
- [ ] Qualify the cached-admission actual Application caller: all 144 stages, 41 new controls and 71 inherited scalar checks remain not run.
  R4 actual Application baseline has build/ISA exits 0 and still waits for native Release r1092 before runtime.
  R5 (`.build/P1Drafts/S5PressHost-r5-Main`) is compiling on `.build/P1Drafts/S5PressAdmission-r2-Main/SourceFull/`;
  its separate 339-source compiler has launched after CPU ASan qualification. R5 runtime also requires the R4 baseline and Main qualification.
- [ ] Qualify ordinary activation, native adoption, G1 and P1 completion; plans remain with Main for update.
