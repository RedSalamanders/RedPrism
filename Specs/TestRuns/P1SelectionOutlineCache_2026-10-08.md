# P1 selection outline cache — 2026-10-08

Status: integrated; r467's idle-chrome regression is preserved and repaired; full Debug r468 passes and its
archive/inventory independently verify. Full Release r469 and real ASan Debug r470 also pass with verified
archives/inventories on the identical source. Release analysis r471 passes and its archive independently
verifies. ARM64 r57 passes all three serialized cross-builds; the main independently verifies all archived
source/evidence copies, 15 ARM64 executables/PDBs and the actual ASan compiler/instrumentation controls.
P1 remains WIP.

## Source and scope

Managed checkout: `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism`, branch
`codex/p1-opacity-host-proof`. The native and sibling checkouts are read-only. DxUi remains pinned to
`8e8994079e82f79e6765e6a885c03fc687957591`, API 3.

r467 freezes 1,029 inputs with SHA-256
`AD3DB4B7452097CCB6ED6CBA2308348AD573B9DE0C9790156BFCBCED85E95632`.
This candidate includes the preceding CPU raster metadata, Gradient/Pixel Move recorder and exact affine
sampling repair, plus the explicit descriptor guard qualified by Release analysis r466. Those earlier
runtime/analysis passes remain historical after this cache integration.

r468 freezes the repaired 1,029-input candidate with SHA-256
`0C7FD838D4C1271C31898B1633AF93B5C4B492391C245CAE5857BDB4CA8348D6`.

## Preserved failure and repair

Full Debug r467 runs at `2026-10-08T13:57:34.217506Z`–`2026-10-08T14:06:37.963032Z`, actual child PID
`88980`, exit 1. All 37 new numeric selection controls and 56 actual outline controls pass, but the existing
`RunChromeSyncSelfTest` observes 40 unnecessary overlay invalidations/preparations when panning with no outline.
Its zero-work expectation is unchanged. The main repairs `CanvasOverlay::SetSelectionMapping` to retain the
current scalar mapping without repainting an absent outline; the mapping remains available for a later source.

Before that source edit, the complete failed source/products/output are archived under
`D:\RedPrismQualificationArchives\P1-selection-outline-1029-Debug467-runtime-failed-2026-10-08`.
The archive process exits zero and independent copy verification authenticates 1,029 sources and 325 evidence
files. Completed receipt SHA-256:
`051B453C9F4BA9B390117EE5FAED4F629C615CD4780F1529EF0F26737DBCD832`;
source manifest: `14F5399D92A9C145A07F427D34D5C1474885C0778F0ED70FBA8FD12A1C99F107`;
evidence manifest: `A5A1942840F346B2DAAFE5E5080055EC242EA13EE114CB7392663F647F0F07C4`.
This failed run establishes no full-profile pass. The r468 run started at `2026-10-08T14:08:49.393793Z`.

## Qualification on the repaired source

Full Debug r468's actual child PID `77020` runs at
`2026-10-08T14:08:54.234720Z`–`2026-10-08T14:20:23.832609Z`, exit 0. All existing suites, numeric goldens,
113 Application stages, failure probes and harness screenshots pass, including the unchanged no-outline pan
gate. These recorder bounds establish no handler, frame or G1 timing measurement.

Archive: `D:\RedPrismQualificationArchives\P1-selection-outline-1029-Debug468-full-qualified-2026-10-08`.
The archiver exits zero before any subsequent producer starts. Independent read-back authenticates all
1,029 source and 327 evidence copies, launch/result/log bindings and source identity. Completed receipt:
`0515D8041603AC8D71FDF93DDB18FB6C4DCDC6A38D49E885EDA381233CE17A01`;
source manifest: `29FC11B18B91D8974ABEF4AB2369508C562624D1353990D053E9CD14D7961AD6`;
evidence manifest: `3D723EC0369A058C1B87791C40480D0F854E5116B187BB65D61E116C0DCDC794`.

The main-reviewed fixed inventory preserves all 31,128 predecessor full-stdout records in order and requires
31,165 candidate records (37 new numeric controls). The separate Application log preserves all 5,411
predecessor records, adds the 56 outline assertions (5,467), and retains 113 ordered stages. The raster
consumer registry requires 438 assertions / 467 case-and-summary records; eight metadata envelope and eleven
sampling-footprint controls remain exact. Only the two unchanged recorded-brush windows permit their
source-proved callback interleaving: exactly four/ten acknowledgements and two stage notifications, with
all acknowledgements before retirement. Six consumer, five sampling and six callback mutation controls refuse;
eight outline/window and four numeric mutation controls separately refuse.

The first r2 verifier invocation refuses three path aliases: read-only native recorder helpers are stored
by basename in the freeze while their original paths include `.build/`. Main inspection finds no hash or
managed-path differences. The r3 checker normalizes only those three exact native helper aliases and passes;
no runtime expectation, source, golden or general ordering rule changes. Checker SHA-256:
`AAF1490CBBAF5587FD5D00335A3C505A0F396916463E932DC42E5004712B0585`.
Main reports remain under `.build/P1Drafts/SelectionOutlineInventory-r2/` (archive and mutation controls)
and `SelectionOutlineInventory-r3/` (complete runtime inventory).

Full Release r469's actual child PID `41152` runs at
`2026-10-08T14:22:58.891872Z`–`2026-10-08T14:31:13.925849Z`, exit 0, on the identical repaired freeze.
Its complete predecessor/consumer inventories verify with the same 31,165 full-stdout / 5,467 Application
records and 113 stages. Archive:
`D:\RedPrismQualificationArchives\P1-selection-outline-1029-Release469-full-qualified-2026-10-08`.
Independent read-back authenticates 1,029 sources and 313 evidence files. Completed receipt:
`688BF7A0560049402566458C86D19ED0CB5E8E91F9FCE033531DB259E6EEA790`;
source manifest: `93E6AD9220408E6BEAA5854331C1FD4B317E72DC687CE7BB549D578F1A042C09`;
evidence manifest: `5556179DC2BD9DB9C64C5B81EFCD8E4CCA66FF34F52A252D6F554C1FC905FFA0`.
The expected invalid-DxUi-pin test writes a refusal to stderr; the actual canonical child exits zero.

Full real ASan Debug r470's actual child PID `41492` runs at
`2026-10-08T14:32:14.378968Z`–`2026-10-08T14:44:55.796624Z`, exit 0. The actual AddressSanitizer
detection probe, all predecessor suites, 113 Application stages and harness screenshots pass. Fixed inventory
verification preserves 39,708 predecessor full-stdout records and requires 39,745 candidate records; the
separate Application inventory remains 5,467 with the same 438 raster consumer assertions and 56 outline additions.
Archive: `D:\RedPrismQualificationArchives\P1-selection-outline-1029-ASan470-full-qualified-2026-10-08`.
Independent read-back authenticates all 1,029 sources and 314 evidence files. Completed receipt:
`5067A8D2DD34255440AE050BD7780FE9D16A3AFC59C02692F2748A80E61454CB`;
source manifest: `EE0C08D160DA6780ACA7148E6109BA83035D41B66B5DE5A0EC12EBB938BA4F67`;
evidence manifest: `EDE8B91176C735DCE1D632E2F93CE4C5F2F697C5451CFE7D906CDC7FFD386542`.

Release analysis r471's actual child PID `57684` runs at
`2026-10-08T14:48:51.103531Z`–`2026-10-08T14:56:55.096658Z`, exit 0, with empty stderr. The linked
instruction controls and unchanged DxUi provenance pass. This is analysis/build evidence, not another runtime run.
Archive: `D:\RedPrismQualificationArchives\P1-selection-outline-1029-Release471-analysis-qualified-2026-10-08`.
Independent read-back authenticates 1,029 source and 327 evidence copies, original child/result/log bindings
and the same freeze. Completed receipt:
`2D60F55D91699A073B2365C2149231EBE3547880271BDAD5FB5698A0BA8190B2`;
source manifest: `9C36E8912C881C1E5A80759AD36C869B7C17114DD6784C3743F18D2CA7FFF8D3`;
evidence manifest: `EE8DA148147AAD14EDAA3914032FEA264FFA4FA854B37FCAD9F07DBA987339D4`.

ARM64 r57's main-reviewed recorder starts at `2026-10-08T14:59:30.387736Z`, PID `57884`.
Its driver SHA-256 is `2041FDF1192D48671E5EF906E7EFF985158CAA1C6F7D2733CB04F03E4062DD4F`;
launcher: `1EF4C1F1419254B6CCBE1FA738437719AFA96B6C4BA873776D0E20CDEC705B13`;
verifier: `1C168AE5CB0D572A8D05B3AF73BCE23A470AF439711979FA25E76D27975B5CD3`.
All three cross-builds exit zero on the identical repaired source. The driver archives each profile before
starting the next. Main copy verification authenticates 1,029 sources and 73/73/77 evidence files respectively;
`Arm1029-r57/Main-ARM57-products.json` also verifies the launch/result/log bindings, pre-build snapshots,
15 executable/PDB pairs, ARM64 PE machine type, and actual ASan compilation and linked instrumentation.

| Profile | Actual child / UTC interval | Completed archive receipt SHA-256 |
| --- | --- | --- |
| Debug | 116740; 15:00:01.820165–15:01:33.936648 | `1B7DAE157EBCDF7FE36004867E26130146F16E345887F43100BC2AC613939FC1` |
| Release | 124504; 15:02:20.908615–15:04:34.364000 | `4B1489D3CE7A08D1E57795DAE111667E09B5C03BC676D90488EA7C9ED5B82DCE` |
| ASan Debug | 111976; 15:05:12.147499–15:06:34.121177 | `0699E38566605B2B3AF7E8165E0249AE27CDBBB0FCCF1FB383DE47301B5627EE` |

Archive roots are `D:\RedPrismQualificationArchives\P1-opacity-host-proof-1029-ARM64-{Debug,Release,ASan}-r57-qualified-2026-10-08`.
No new compiled source enters during these builds. Native ARM64 runtime remains unavailable. These are
cross-build results; actual G1 measurements and ordinary activation remain open. Formatting checks 900 files
and skills validation checks all 15 skills; both pass.

## Behavior and independent references

- `SelectionDisplayValue` shares immutable source loops and returns the rounded Pixel Move offset separately.
  Deferred transforms retain their existing display precedence; the numeric selection API remains unchanged.
- The application caches points relative to source minimum bounds. Only a changed immutable source identity
  converts points. Source and Direct2D factory references are retained, preventing freed-address cache aliasing.
- Pan, zoom, DPI and Pixel Move ticks change one scale/translation matrix. The actual current frame DPI is used
  before painting, avoiding a stale interaction-preparation DPI. Bounds/anchor cancellation stays in double.
- The cached path follows that matrix; fixed stroke transforms keep the existing 1 DIP solid and 4/4 DIP dashed
  pens. [Microsoft's stroke contract](https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/ne-d2d1_1-d2d1_stroke_transform_type)
  distinguishes transformed geometry from the fixed pen, which still respects DPI.
- Empty and absent outlines stop animation and release the preceding path. A new actual device/factory rebuilds
  factory resources from cached points. The pinned single-threaded factory stays on the owner.

The main reviews and integrates the cheaper agent's pure tests: literal source square, positive/negative/tie
rounding, invalid inputs, empty/absent selection, direct and deferred transform precedence, replaced source,
cancel/replacement and a captured view surviving all other owners. The actual existing WARP Gradient/Pixel Move
fixture adds 56 assertions across four Move/Copy × Cancel/Apply cases. Its four corners are checked against a
literal canvas-center/zoom/pan/DPI formula, independently of `ViewFromDocument`. Real painted path counters and
stroke properties prove reuse through both landings, zero/redrag, pan, zoom and DPI; all eight ant phases, equal
points with a new identity, cache-only ownership, empty/absent transitions and actual factory replacement are checked.

## Remaining

No timing claim is made by these hidden semantic/lifetime fixtures. Initial point conversion, owner path
construction/tessellation and large-owner retirement remain S5. Ordinary selection moves still construct their
numeric translated outline. Complete display/stroke/row admission and activation, all other open P1 areas, actual
R1/W G1 and final source/profile qualification remain required before P1 can move to Done.
