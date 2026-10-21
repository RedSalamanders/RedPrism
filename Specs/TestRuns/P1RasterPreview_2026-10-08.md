# P1 completed CPU raster-preview foundation — 2026-10-08

Status: Repaired consumer full Release r462, Debug r463 and real ASan r464 pass on the same source, including the unchanged mask literal and eleven independent affine sampling controls. Analysis r465 fails nullable Pixel Move analysis; the explicit branch repair passes r466 analysis. Those passes are historical after selection cache integration. The combined 1,029-input candidate passes full x64 Debug r468, Release r469, real ASan r470, Release analysis r471 and all three ARM64 r57 cross-builds; independent archive, inventory and product verification passes as recorded in [P1SelectionOutlineCache_2026-10-08.md](P1SelectionOutlineCache_2026-10-08.md). Native ARM64 runtime, ordinary activation and G1 remain open. P1 remains WIP.

## Scope

Managed checkout: `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism`, branch
`codex/p1-opacity-host-proof`, predecessor `131b5a329f0afdeb9e53c985961c16a8c9233408`.
DxUi remains pinned to `8e8994079e82f79e6765e6a885c03fc687957591` (API 3); native/sibling checkouts are unchanged.

The candidate adds worker-built immutable raster-preview metadata, success-only original completed-job memory
binding and control-block lifetime accounting for the stroke, metadata and lifted-opening cache. It does not
activate normal display routing or qualify G1, RAW acceleration or Photoshop comparisons.

## Attempts preserved before repair or product overwrite

| Attempt | Actual result | Source freeze | Preserved cause or qualification |
| --- | --- | --- | --- |
| x64 Debug r451 | exit 1, build | `DF86DA08E062540C75604477637200A82342DC64395EED81899D91808DEB404C` | Incomplete `GridRect` definition in the new header; missing I/O/composition source closure. Runtime tests did not run. |
| x64 Debug r452 | exit 1, link | `8B16EC22608F15256F4E9D1C49DB6FD89042FA4C359727759436ABF84142D943` | I/O target lacked the actual worker crash-handler implementation. Runtime tests did not run. |
| x64 Debug r453 | exit 0, full gate | `9CDAF030BB0C0D508BF8874992CDEE8F42B89E7A19973C60386570C4E5224239` | Four suites, crash probes, command-line/RAW corpus controls, all 112 actual Application stages and nine harness captures pass. |
| x64 Release r454 | exit 0, full gate | same `9CDAF030…224239` source | All four suites and 112 actual Application stages pass; independent full predecessor projection verifies. |
| x64 ASan Debug r455 | exit 0, full gate | same `9CDAF030…224239` source | Real sanitizer detection probe, ordinary/stress suites and all 112 actual Application stages pass. |
| x64 Release analysis r456 | exit 1 | same `9CDAF030…224239` source | C6221 HRESULT comparisons: one new completed-memory assertion and two earlier transport comparisons. No runtime qualification. |

Archives are under `D:\RedPrismQualificationArchives\P1-raster-preview-1024-*` with exact attempt numbers and
failure/full-qualified names. Each preserves source, products, output, actual child result and independently
read-back manifests. Failure authentication is not runtime qualification.

r453 actual child PID 115376, start `2026-10-08T11:34:20.351082+00:00`, completion
`2026-10-08T11:42:19.676280+00:00`. These are the canonical recorder's attempt bounds, not G1 kernel/frame timing.
Stdout SHA-256: `3424EF2452336DD74EE62A8B36B66BEB88EE696636AFF3F045B698E447054AF0`.
Completed archive SHA-256: `8B89FA6596E9C6695175326CBC0FB27A8F2245166EF41FF579085DDABC06229E`.
Source manifest: `641BB59F0FBC37980108EDFF2D0934CB1B57751199ACF105911DEB0B0ABF5B22`.
Evidence manifest: `82CE091BC1EE4990C65294C610E19CBACD64E78B0185DC101913601D1ECB794D`.

Independent ordered inventory verification preserves 31,006 predecessor full records plus 101 CPU assertions and
two summaries in Debug and Release (31,109). ASan preserves 39,535 predecessor full records plus 52 Document
assertions and two complete Host runs of 49 assertions/two summaries (39,689). Its stress entrypoint reruns the
ordinary Host controls; it does not rerun the ordinary Document suite. All three actual Application logs preserve
4,984 predecessor records plus the 15 transport assertions and one summary (5,000), with exactly 112 stages.

r454 actual child PID 83008, recorder bounds `2026-10-08T11:43:20.711512+00:00` to
`2026-10-08T11:50:45.943954+00:00`; completed archive
`0341417E439F402E9C51EE864FD2B998F2155A33B146F9CFD146CB6B58679069`.
r455 actual child PID 83416, recorder bounds `2026-10-08T11:51:36.371750+00:00` to
`2026-10-08T12:03:13.155776+00:00`; completed archive
`4C742A5A77A1CEB7F41FEA403EBE29225AF06AA4BE940A0E73209D0B8ABCD572`.
r456 actual child PID 41112, recorder bounds `2026-10-08T12:04:32.381960+00:00` to
`2026-10-08T12:08:24.725881+00:00`; failed completed archive
`E7CAA68CC7B9DAE8AE376A3D9C4342FC7A2C8386D9E340B68706D9BB99CA6DDC`.
The main verifier authenticates every source/product/log copy and the actual child receipt. The first ASan
inventory check incorrectly expected one Host run; reading the frozen harness established its two runs and the
corrected check preserves the entire predecessor projection. This checker repair changes no product or test.

## New behavioral evidence

- 52 `prepared-raster-preview.*` Document assertions, including literal image/mask bytes, placed/stretched source
  geometry, owner/unknown-owner/pre-cancel refusal, unchanged input/list and below-threshold joined retirement.
- Ten `completed-preview-memory.*` Host controls using actual scheduler callbacks, original ticket generation,
  success/failure and exact whole-reservation ownership through final control deallocation.
- 39 `prepared-raster-preview-lifetime.*` Host controls using the actual CPU raster-edit path and a literal 4 MiB
  admission. Three distinct expired weak controls and the separately retained strong lifted-opening cache preserve
  the original charge until semantic destruction, final control release and owner acknowledgement consumption.

The main independently repaired the delegated lifetime draft's missing complete type includes, incorrect
non-expired weak expectation and owner-thread final cache destruction before integrating it. The mask fixture was
extended to cover an unplaced 2×1 mask stretched onto a 4×1 grid. No existing pixel golden was rebased.

## Remaining qualification

The integrated consumer source adds the original CPU reservation handoff, exact pending metadata identity,
full-snapshot gradient/move reconstruction and worker recording. Reconstruction does not inherit a previous
pixel-move grid: its next complete snapshot may omit a former landing tile. Empty image-layer preparation and
coverage now accept an actual stroke image; ordinary empty sources still cover nothing. The new host fixture
checks two different landings in separate 256-pixel tiles against independently constructed literal bytes.
Replacement CPU admissions refuse once rather than waiting on their own retained cache. All opted-in payloads,
including failed or cancelled preparations without output, defer semantic destruction on the owner.

Explicit int32 HRESULT comparisons repair r456's test-only type diagnostics. Debug build r457 passes on its
1,027-input freeze `4550725087D7A3B5F3CB51B237026F6E40EB3E9D60864D1959F9466FDF5B8E90`;
its archive completion is `4FBEB4E60C3B04CE22CE925C536A4349C4A0E36415ABBA2628D16162CBD397C0`.
This is compilation and ISA proof only. Main independently authenticates all 1,027 source and 312 evidence copies.

The zero-input metadata invalidation/redrag revision then runs full Debug r458, actual child PID 58308,
`2026-10-08T12:24:30.656599+00:00` to `2026-10-08T12:31:19.520196+00:00`, exit 1. Its freeze is
`D48E0705E020C5D154FEE370DB3F468974DFAF7F6834F48A369C7A8EE13078D5`; the archived completion is
`81B8251C397E650571C840FC80381E3CF3C50523867CE5DEB24C826CB111B211`, at
`D:\RedPrismQualificationArchives\P1-pending-raster-1027-Debug458-runtime-failed-2026-10-08`.
Main authenticates all 1,027 source and 321 evidence copies. The CPU suites pass, but Application stops at the new
placed-mask original literal; this does not qualify the full profile. The failed fixture expected reveal outside
the original placed mask. Independent arithmetic from the existing contract gives `(31+219)*2 < 2*255`, hence
hide. Its original and preview exterior bytes are zero. Commit publishes the new eager thumbnail whose edge
samples are 0 and 255: equality reaches the reveal threshold, so committed exterior bytes are 255. The revised
fixture explicitly distinguishes all three phases; production mask math and existing goldens are unchanged.

Full Debug r459 passes the corrected fixture, actual move Undo controls and fourteen actual Gradient/Pixel Move
worker fault/cancel/reopen cases. Actual child PID 35756, recorder bounds
`2026-10-08T12:36:24.380780+00:00` to `2026-10-08T12:45:03.044512+00:00`, exit 0, 1,027 inputs, freeze
`393414CBF5CA1EDAC25838F34C476A3E366D908C76FDCC188E6826AE890B02E6`. Archive:
`D:\RedPrismQualificationArchives\P1-pending-raster-1027-Debug459-full-qualified-2026-10-08`;
completed receipt `248EC4B386C06D918BA25EABC55667C2979D6E4EEED1DC1E49B67F5DBA4CE67B`.
Main authenticates all 1,027 source and 323 evidence copies, preserves all 31,109 predecessor full records plus
the eight admission assertions (31,117), and all 5,000 predecessor Application records plus 382 new assertions,
28 case declarations and one summary (5,411). All 113 registered Application stages run. Five synthetic inventory
mutations (missing, duplicate, reordered, failure and missing summary) refuse. Source/product authentication
and ordered inventory proof do not establish G1. Release r460 then fails on this identical source. Its actual
child PID is 37692, recorder interval `2026-10-08T12:46:21.163531Z` to `2026-10-08T12:52:36.819055Z`,
exit 1. The failure is `application-pending-raster.gradient`, target 2,
`MaskUnplaced` original-literal byte 8: expected 219, actual 125. No golden or expectation was rebaselined;
The archived optimized shader contains `deriv_rtx_coarse`/`deriv_rty_coarse` inside unit-square branches despite
their source-level placement before the branch. The main replaces lane derivatives with the exact affine
inverse-transform footprint and adds eleven independent thin/clipped/rotated/minification assertions. The
original failing literal remains unchanged; runtime confirmation of the repair is pending.
The failed archive `D:\RedPrismQualificationArchives\P1-pending-raster-1027-Release460-runtime-failed-2026-10-08`
was independently authenticated at `.build\P1Drafts\PendingRasterFault-r1\Main-Release460-archive.json`.
Completion SHA-256: `EEBF4E921914C70159760ED07CFF3E5DAA416016CA9F9484A34D4D9BFCF58588`;
source-manifest SHA-256: `A457EDB1862383E16A9E986C0B1375D37C52AAAD728FA283A7E85DE422772630`;
evidence-manifest SHA-256: `D0F97F65BD004B789545953DA927E11E1F86CE8FD6C52002A4BBBEC5525FA19A`.
The failed archive authenticates 1,027 source and 309 evidence copies. Release stops during the new Application
stage and has no complete 113-stage inventory. The 31,117 full records and 5,411 Application records belong to
successful Debug r459 above; they must not be attributed to r460. This archive authenticates the failed run,
not a successful inventory or Release qualification. Debug r459 remains
qualified only for source `393414CBF5CA1EDAC25838F34C476A3E366D908C76FDCC188E6826AE890B02E6`; any later
source change invalidates that evidence for the new source. The earlier 1,024-input full matrix is historical
for this candidate. The planned ARM64 r54 driver was prepared for the old freeze and was never launched.
Release r461 then fails compilation of the new independent sampling test: local variable `small` collides with
the Windows RPC macro expanding to `char`. The shader compiles, but no runtime gate runs. Actual child PID 9392,
`2026-10-08T13:03:10.610175Z` to `2026-10-08T13:03:37.510029Z`, exit 1, source
`9C4B7B74940F801E1AB3C5949E3BD92E43D8B764401ECF8B2C2878BBEDCF358D`. The failed archive
`D:\RedPrismQualificationArchives\P1-affine-footprint-1027-Release461-build-failed-2026-10-08` is independently
authenticated (1,027 source and 309 evidence copies); completed receipt
`D39554018033D59A3237E4575404F81D1CB95A8D02B703B5DCAA7BFB608F9269`. Rename that variable to
`scaledLayer` and retain every literal. Full Release r462 passes on the repaired candidate. Skills (15)
and formatting (898 files) passed immediately before the local rename; the final source checks remain due.

Full Release r462, actual child PID 73784, `2026-10-08T13:05:19.107447Z` to
`2026-10-08T13:11:25.870903Z`, exit 0, qualifies the unchanged original mask literal and all eleven new sampling
controls on 1,027-input freeze `FB24F31B0393ED2105D4E65F87B93B046A84413840BCE8FA52EAA8374D7A7262`.
Archive: `D:\RedPrismQualificationArchives\P1-affine-footprint-1027-Release462-full-qualified-2026-10-08`;
completed receipt `1B13357DA596C97633DBD5054579F2387AC8BA4F56AD6AF3DD67F1A4120150A4`, source manifest
`B4E1C66483EFB599DFE944C0FADC62937339323CAEAF8B9C5B5D18F30C248ADF`, evidence manifest
`1BC60B976C0DB10AC75793712DA0867894066F5445184888F67A701D270C79A8`.
Main independently authenticates all 1,027 source and 311 evidence copies and verifies the reviewed fixed
inventory: all 31,109 predecessor full records plus eight metadata and eleven sampling assertions (31,128),
all 5,000 predecessor Application records plus the unchanged 411 new records (5,411), all 113 stages, six
Application and five sampling inventory mutation refusals. Optimized shader SHA-256
`9FBF8C353DD50AAD1F21D522AAAFF1C31CF26A64D7FD560DD3B799A3405C74C5` contains no lane-derivative
instructions. Existing mask, transform, adjustment, coverage, minification and brush goldens all pass. These are
functional and lifetime results, not ordinary activation or measured G1.

Full Debug r463 passes on the same `FB24F31B…7A7262` source, actual child PID 53412,
`2026-10-08T13:12:46.829975Z` to `2026-10-08T13:21:27.432274Z`, exit 0. Archive:
`D:\RedPrismQualificationArchives\P1-affine-footprint-1027-Debug463-full-qualified-2026-10-08`;
completed receipt `2AFD82C6CFEBC932BEB0494B8803DB7ED60117835011FFDCEF4A15299B6276C6`. Main authenticates
1,027 source and 325 evidence copies. The complete inventory has the same 31,128 full records and 5,411
Application records / 113 stages. The initial strict predecessor comparison refused an existing recorded-brush
callback interleaving, without a missing, duplicated or failed assertion. The unchanged source allows recording
completion between asynchronous GPU acknowledgements; stream retirement must follow all acknowledgements.
Main binds both original brush fixtures and the dispatch implementation to their unchanged archived SHA-256
values, preserves the two refusal controls before recording, four/ten exact acknowledgements and two completion
controls per fixture, and requires the final completion after every acknowledgement. Counts derive from two
density clears plus the fixed tile dispatch regions: `2+1+1=4` and `2+3×2+1×2=10`. Every record outside these
two bounded windows remains in its exact predecessor order. Six partial-order corruption mutations refuse;
the six Application and five sampling mutations still refuse. No source, pixel expectation or runtime gate is
changed for this log-order repair. Fresh real ASan r464 is running; analysis and ARM64 cross-builds remain due.

After the recorder/HLSL mask sampling issue is qualified without changing the original literal, run and independently
archive a fresh exact-source matrix, including Debug, Release, real ASan, analysis and ARM64 cross-builds.
Ordinary display/stroke activation and measured R1/W G1 remain separate open gates. Skills (15), format (895) and diff checks passed
for the foundation; the consumer candidate needs its own checks. Native ARM64 runtime is unavailable.
Complete consumer admission, fault/closure scenarios,
ordinary display/stroke activation, all other P1 checklist items and measured R1/W G1 remain open.
