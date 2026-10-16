# P1 Selection Physical Prefix — 2026-10-16

## Result and scope

The private x64 Release R2 and real-ASan R2 runs passed the physical-prefix CPU protocol: the real `HostSelectionPressInput`, two workers, the original parent ticket, and opaque `DocumentSession` path. This does not qualify physical Win32 dispatch, `Application` activation, native adoption, G1, or P1 completion. `P1_done` remains false.

## Release R1: failed process, isolated predicate results

`main-failed-verified.json` records `DocumentTests.exe` exit code 1 (PID 90832; 2026-10-16 21:23:05 UTC). The receipt identifies four failing predicates: `early-up.final-readers-refund-all-original-parent-and-writer-ownership`, `async-shift.final-readers-refund-all-original-parent-and-writer-ownership`, `lost-moved.final-readers-refund-all-original-parent-and-writer-ownership`, and the exact setup label `selection-physical-prefix.setup.capacity-seals-lost`. The three refund predicates retained their own result-history snapshot. Overflow setup had already occupied 511 of 512 event slots, so it refused before exercising actual overflow. The 22,228 predecessor predicates were inspected; this does not make the failed process a passing or qualified run. The initial byte comparison refused a variable timing diagnostic. The source-roster reader then checked the complete predecessor scope, supplying its missing final footer only to isolate that scope after the actual early exit. This supplied footer does not qualify the failed process. The R1 archive receipt says `actual_result_present: false`.

## Release R2: qualified process

The corrected Release executable exited 0 (PID 95884; 2026-10-16 21:26:06 UTC; empty stderr). The frozen expected output records 55 new physical-prefix assertions and 22,228 predecessor assertions passing, 22,283 total. The archive records 269 compile entries: 268 unchanged objects and only `Document.Tests.SelectionPhysicalPrefix.obj` recompiled; 1,604 archived files.

The 55 new assertions cover the eight cases below. Receipt fields `complete_geometry_allocator_control_envelope` and `post_Close_final_parent_refund_independent_endpoint_observer` describe inherited broader qualification controls; they are not results attributable to the 55 new prefix assertions. The 22,228 predecessor results remain separately identified in the total.

### Eight protocol cases

| Case | Input and asserted behavior |
| --- | --- |
| `early-up` | Exact hit and opening captured once; early up completes on the original parent and checks literal final geometry/history/generation/redo. |
| `async-shift` | Two accepted moves preserve the shifted preview; terminal ordinal 4 is acknowledged by the same parent. |
| `lost-moved` | One accepted move then lost-pointer sealing at ordinal 2 publishes the moved geometry. |
| `overflow` | Capacity is 512. Moves 1–511 are accepted; attempted move `(9999,3)` at ordinal 513 triggers overflow. Literal overflow geometry is `{(512,2), (514,2), (514,4), (512,4)}`; the bounded prefix is retained and lost terminal ordinal 512 is acknowledged. |
| `ordinal-exhaustion` | Exactly one move is accepted at ordinal `UINT64_MAX`, following down ordinal `UINT64_MAX-1`; invalid finish is rejected, then loss seals at `UINT64_MAX` without wrapping. |
| `queued-writer` | A younger writer observes only the completed parent; final geometry/history checks writer ordering. |
| `cancel-collect` | Cancellation while the original parent is suspended is collected on that ticket; cancelled history/redo state and resource refund are checked. |
| `foreign-session` | Foreign identity is refused before source capture or hit testing; the original ticket fails with `E_ABORT`, selection/history/generation/redo stay unchanged, only its bounded prefix is dropped, and admission is refunded. |

The common prefix assertions check original admission, same-parent terminal/cancel acknowledgement, literal geometry/AA/history/generation/redo, close/retirement without requery or readmission, bounded-prefix contents, and event-driven body/capture counts. Queued-writer ordering and foreign-session refusal have their additional case-specific assertions.

## Receipts and recomputed SHA-256

Hashes below were recalculated with Windows `Get-FileHash -Algorithm SHA256` on the exact receipt files and archived files named in the first column. Completion hashes are the values recorded by each archive receipt (they are completion digests, not hashes of a same-named file). Source hashes for R1 come from the R1 archive; R2 source hashes come from the R2 archive, not the mutable R1 draft's current `SourceFull`.

| Exact artifact | SHA-256 |
| --- | --- |
| R1 `main-failed-verified.json` | `26F2FDA08245126ACC79D4A9D91A971B7E0BE8E3905525F7023B955D467F791A` |
| R1 `expected.json` / archived `Recipe/expected.json` | `606F84A9E55FE1AE1139E3ADA2107859D181EAA73613758D9729F4EA0C696C65` |
| R1 `archive-receipt.json` | `152AD6C08583186C58F71E1145422AEAD5164166AA1457970666BD0D4B3C1C7D` |
| R1 archive `manifest.json` | `248F52350361B48DAC18033E423628A87ABAF1E5C1B380FC34F32ACCAC2E20D1` (1,600 files) |
| R1 archive completion digest | `EBF0E24734420230B1B243AA81307CE0BBF83A6DC1790D01483B89F15333B2C4` |
| R1 archived `SourceFull/Tests/DocumentTests/Document.Tests.SelectionPhysicalPrefix.cpp` | `FEFD86A3B12DB00B76E88136844AC147EC7329A7043C24540D5348D559889EF3` |
| R2 `main-qualified.json` | `F9789368517C3F88734D060101DFDE8E3E2FD97A5776436501D305560098639E` |
| R2 `expected.json` / archived `Recipe/expected.json` | `49153BC3BBE231A477078727BFB5F6CF0883A0A55ED8AC45B320270D0C2889A5` |
| R2 `archive-receipt.json` | `966190EC20ACAC9BA8C07E9E32170E4F692F8660F545EA30B4CC01BE9B3EB329` |
| R2 archive `manifest.json` | `7972563F7C9D2614D8B5F99E982BD90E66DB90D286CCBA228C2D6B5C3025D767` (1,604 files) |
| R2 archive completion digest | `A08F56B356300C60F6D52F408F70FB714B4AEA5830519A736241916B495E2D57` |
| R2 archived `SourceFull/Tests/DocumentTests/Document.Tests.SelectionPhysicalPrefix.cpp` | `96CA83D9499E12E2A95285B86A603358F387A0BCED3658C60CF194884D982A12` |
| R1/R2 archived `SourceFull/Tests/DocumentTests/Document.Tests.SelectionPhysicalPrefix.h` | `2DFCC4AF921647B1E0094894B7D6151E22863AC00BDB389A4F5A798E07FFFCE6` |
| R1/R2 archived `SourceFull/RedPrism/HostSelectionPressInput.h` | `8F0EB1E8AF0B44C7CBCF3725F67B762E6BB50CE5A27F39B94BA67152E1C59BBF` |
| R1/R2 archived `SourceFull/Common/PointerInputStream.h` | `40E13ED7A3C0DBF449ABB491D657F8D2908F8756395BAD92E709F1372AA30ED6` |
| R1 archived `Products/x64/Release/DocumentTests.exe` / `.pdb` | `5D8A78559BA4734456999BF15310D231E900C8475E8F4DE6918A4ADEFBFA9520` / `874FC6FCB182D8FEAB34C663A976719DE768D72F443AFFF0C0780B7A05670021` |
| R2 archived `Products/x64/Release/DocumentTests.exe` / `.pdb` | `10DBDDB84A74DC6D898015480FDFEF75077EA1B69B1486F999B3948E837A620C` / `DBEE142F50FBCBA16F22809F24C7DF11C40443DBB6508F3D0266D52DADB40932` |
| R2 CodeView identity from `main-qualified.json` | GUID `23DAE26B-1027-454D-9FD9-E920DC845449`, age 2 |

Receipt locations: `.build/P1Drafts/S5PhysicalPrefix-r1-Main/{main-failed-verified.json,expected.json,archive-receipt.json}` and `.build/P1Drafts/S5PhysicalPrefix-r2-Main/{main-qualified.json,expected.json,archive-receipt.json}`. Archived sources and products are under `.build/P1EvidenceArchives/2026-10-16/P1-PhysicalPrefix-x64-Release-private-r1/` and `...private-r2/`.

## Real ASan

The first ASan attempt fails linking with FTK1004/LNK1180 disk-space errors (PID 84576, exit 1;
21:27:27.026907–21:33:48.279396 UTC). Runtime never starts. Main verifies all 1,595 archive copies and retains
completion `F1EB2C159A17AC318D1CEF6906877EA808CA38FD618F29D6A8F4429FCDBE1524`.

After verified closed-cache reclamation and archive relocation, a fresh unchanged-source attempt reuses all
269 instrumented objects and completes the link. Its own UAF probe detects the expected heap-use-after-free.
The actual runtime exits 0 (PID 3748; 21:41:19.766467–21:43:51.565226 UTC): all 55 new assertions and all
22,228 predecessors pass. Main independently checks all 1,611 archived files, the exact existing ASan profile
witness, complete source/dependency/product bookends, every instrumented compiler record and PE/PDB identity.

- Completion: `E1B035B6E7B5855F70F575A6A0DB025EFB521E359F826AC0E133B28ECA327334`.
- Executable: `6F5947E338075B5258CAE5AF8C0599D0A0174348FED43A6BDAB960DAFC7251F8`.
- PDB: `9CCDFA4370210216B19432B48D9C9F1FCD7FBAB79B90B6B60DA599B1A3FCFACA`.
- GUID `8F99F2F0-33DF-4436-B04C-935EEACD4A8B`, age 2.

[ASan qualification](../../.build/P1Drafts/S5PhysicalPrefixAsan-r2-Main/main-qualified.json).
Actual Application handlers, complete caller bounds, other press branches, ordinary activation, native adoption
and G1 remain required. The next isolated application draft extracts the tested protocol into a reusable host
job and adds Win32 message tests; that draft is uncompiled and is not covered by these receipts.
