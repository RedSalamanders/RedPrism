# P1 Warp worker primitives and retirement — 2026-10-08

Status: focused Debug numeric qualification passes for the cancellable primitives and worker factory lifetime. Full Release r511 passes the strengthened final-host qualification on the exact 1,068-input freeze; the combined matrix remains pending.
Detached opening's counter repair passes all 49 focused Debug assertions in r480. Lazy placed-source opening,
tile storage and reviewed sampler/snapshot tests pass focused Debug r482 after build r481. Full Debug r483
fails the metadata fixture's inherited Blur mode; its preserved evidence precedes the explicit Liquify setup
and restoration repair. Full Debug r484 passes those four Warp cases, then fails the opacity fixture's same
inherited Blur mode. Both failed archives are authenticated before repair. The reviewed CPU sample plans,
exclusive transfer guards and tests build successfully in r485. Its completed archive authenticates before
the exact sample-sequence discard repair. Full Debug r486 exposes an invalid new constant-color Smudge fixture:
its pickup crosses the image edge. The repaired interior invariant and independent three-pixel edge reference
pass all 48 plan and 45 session assertions in r487, preserving all 8,617 predecessor Document records. Full r487
later fails the project-open fixture's inherited Blur mode. Its archive authenticates before explicitly selecting
Liquify and restoring the old setting in that fixture. Full x64 Debug r488 passes all suites, crash probes,
application WARP, screenshots and 113 application stages. Its completed archive is independently authenticated
before launching Release r489. Full Release r489 also passes all suites and 113 application stages on the same
1,051-input freeze; its completed archive is independently authenticated. Full x64 ASan Debug r490 completes
with actual child exit 0 and an independently authenticated archive. The Main ASan inventory now passes using
the profile-r2 report, which accounts for the runner's second `HostTests --stress` execution. Debug488,
Release489 and ASan490 main inventories pass. Focused x64 Release analysis r491 completed with actual child exit 0
and independently authenticated archive; ARM64 qualification remains pending.
P1 remains WIP; ordinary Warp migration, complete phase admission, final publication and G1 are open.

## Checkout and scope

Checkout: `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism`, branch
`codex/p1-opacity-host-proof`. Preceding fully qualified checkpoint `eb36e59` is pushed. DxUi stays pinned to
`8e8994079e82f79e6765e6a885c03fc687957591`, API 3. No DxUi source or pin changes are made.

`AppendForWork` preserves the supported finite-input serial dab arithmetic. Row/dab cancellation or refusal
invalidates the private draft; no `Image` or changed-tile API may publish its partial pixels. A private point
prefix may remain. A nonthrowing scope guard also invalidates on allocation exceptions reaching the job runner.
`ImageForWork` and `TakeChangedTilesForWork` adopt fully initialized pixel allocations, check cancellation before
publication, and clear dirty flags only after a complete successful tile snapshot. `FinishWarp` refuses invalid
drafts before an absent sample can become ordinary colored paint.

The main reviews and corrects the cheaper agents' tests. Independent references are literal premultiplied
constant/blank pixels, fixed serial spacing centers, four literal 300 × 300 crossing-tile extents, unchanged
source identity and session revision/generation/history. Existing Smudge/Liquify and selection-preservation
controls remain unchanged. These numeric tests establish neither scheduler ownership nor normal host timing.

## Preserved attempts and focused proof

| Attempt | Result | Source freeze SHA-256 | Scope |
| --- | --- | --- | --- |
| Debug build r472 | exit 1 | `41531A0BDD6D0F1B9C2EA78FF5B1C662969A37A3955BCE740AC11AB60BC09A06` | Six test integration diagnostics: explicit cancellation tokens and `RasterTile.pixels`. No runtime pass. |
| Debug build r473 | exit 0 | `2DB465B81DA34774E9C37CE906E423298D4E9D496E1527E961EAA9A7F5715721` | All five targets and linked instruction controls pass. |
| Recorder r474 | CLI refusal before product launch | no source freeze or product run | Incorrect `--runtime-suite` option; correct option is `--suite`. Original refusal is retained. |
| Debug Document r475 | exit 0 | `7C3689B53FCE68F167B03EC6A3014D4AB371CCF4D5A30E2628961C44C15E7A80` | All Document tests pass; 43 new cancellation/session and 36 new snapshot assertions, two summaries. |
| Debug factory build r476 | exit 0 | `912A342A5B46D2201976642672D0CB9A68B9467CA3DA111681C66172D5F0804C` | All five targets and instruction controls pass. |
| Debug Host r477 | exit 0 | `A42B8B8D288CFAA655E04FB99812AE5321AB33254FEB958BFE4788775FA70380` | All 8,183 predecessor Host records remain ordered; 24 assertions and one summary add 25 records. |
| Debug opening build r478 | exit 0 | `67EF80D507EBDE86E8B1C62333D6EDAF526981DC359D6F9B355D2620600F6166` | All five targets and instruction controls pass; no opening runtime claim yet. |
| Debug counter build r479 | exit 0 | `11BB6B274B3EC65D35BA1D95AF8CADCF233927FD0CAD2188EAA2801E77D2CAA9` | All five targets and instruction controls pass. |
| Debug Document r480 | exit 0 | same r479 freeze | 49 opening assertions and one summary pass; all 8,505 predecessor Document records are preserved in order (8,555 total). |
| Debug lazy build r481 | exit 0 | `43C21DC038D9844A72D28803F0DB12E811F0938DD3ACCDAEFE45AF9C2A387F05` | All five targets and linked instruction controls pass. |
| Debug Document r482 | exit 0 | same r481 freeze | 54 opening, 24 lazy-tile and 31 sampler assertions, three summaries; all 8,505 predecessor records preserved (8,617 total). |
| Full Debug r483 | exit 1 | same r481 freeze | Host/Document/Composition/I/O and crash probes pass; hidden application stops in metadata Warp opening after the four Brush cases. No full gate pass. |
| Full Debug r484 | exit 1 | `1073F13E9078E2AEC4AA65F894FDDAA879F74566876D381D22843E3D0C51036B` | Metadata's four Warp cases and 162-assertion summary pass; hidden application later stops in the opacity Warp fixture after its five Brush assertions. No full gate pass. |
| Debug sample build r485 | exit 0 | `B2C9EEC00BCE1326BD1D76695B1862F3A39BDFE517684751B061F6186626DD08` | All five targets and linked instruction checks pass on 1,051 inputs; sample runtime remains due. |
| Full Debug r486 | exit 1 | `559FD3D60E024EFDA549E2CAC12A5296AABE2E3B0B4365251514470AE01DDA4A` | All 45 session assertions pass; one new constant-color Smudge assertion fails because its radius-20 pickup crosses the exterior. |
| Full Debug r487 | exit 1 | `C57D4F0BD293D2C0776CA7E9B91728C79D29D7930301012B2B068FF3A00553B0` | All CPU suites, Composition/I/O and crash probes pass. The Document stream has 8,712 records, including all 48 plan and 45 session assertions. Application later stops at the project-open live Warp fixture. |
| Full Debug r488 | exit 0 | `616283681BCAFB0289D7B1DFB7D7B2B8669F91530B64CD312FFC53416465576E` | All suites, crash probes, WARP application and harness screenshots pass; all 113 application stages complete. |
| Full Release r489 | exit 0 | same r488 freeze | All suites, crash probes, WARP application and harness screenshots pass; all 113 application stages complete. |
| Full ASan Debug r490 | exit 0 | `6D950D7B6813192169F6AE2A68A004E2B31109E5A24B1C3F13E5DB404AEDE040` | Actual full ASan child, archive authentication and Main profile-r2 output inventory pass on the same 1,051-input freeze; ASan records include the runner's repeated factory lifetime suite. |

r473's actual child PID 110060 runs at `2026-10-08T15:13:41.838853Z`–`15:18:22.894326Z`.
r475's actual child PID 98096 runs at `2026-10-08T15:20:23.926181Z`–`15:20:55.863225Z`.
These are recorder bounds, not kernel intervals or G1 measurements. r475 tests the unchanged archived r473
`DocumentTests.exe`, SHA-256 `F6F773553648CD97D93E31012A7C665718DBF0294E9FFED47C9E87C147462C49`.

The 1,033 compiled/configuration inputs and recorder bytes match between build and runtime. Only the original
three byte-identical recorder helpers relocate from the native helper location to managed `.build/P1Runner`.
All later recorder output is managed. The first source-path verifier refusal and the OEM banner decoding refusal
are retained; main corrects only those three exact path aliases and compares log bytes losslessly. No source
hash, runtime row, golden or multiplicity is relaxed. The complete 8,424-record predecessor Document stream is
preserved in order; r475 has 8,505 records (79 assertions and two summaries added).

The cheaper-agent fixed registry initially refuses its mistyped cancellation-test hash. That refusal remains in
`WarpWorkerInventory-r1/Main-self-check.txt`. Main's r2 revision changes only the integration path and corrected
test source pins; all expected labels, repeated multiplicities, suite order and eight rejection mutants remain
unchanged. It verifies all 81 new records. Actual source/binary binding and complete predecessor preservation
are independently checked by `WarpWorkerInventory-r2/verify-main-475.py`.

Each archive is completed before product overwrite or subsequent source integration. Independent read-back
authenticates all 1,033 sources and 318 evidence files per attempt:

| Archive under `D:\RedPrismQualificationArchives` | Completed receipt SHA-256 |
| --- | --- |
| `P1-warp-worker-1033-Debug472-build-failed-2026-10-08` | `FD427D8E3C0D0C24319D95B6D4F4543EB9F96A878EEEDC9D0945BF6A2A7D7EDE` |
| `P1-warp-worker-1033-Debug473-build-qualified-2026-10-08` | `B734933CC4DE227A2D0A941D2E3E5689FCEB207C0C4DCB4884BB387F56633AF8` |
| `P1-warp-worker-1033-Document475-focused-qualified-2026-10-08` | `1ADD5B3681D32B2807B2D0ECEAF421E851C976E06A1D6C63B9A31F9A0E72AFE2` |
| `P1-warp-factory-1035-Debug476-build-qualified-2026-10-08` | `A4E5F33CB8DF3EC84BC5E92803172030B0A41E90329AE47B06857E92A84BAE11` |
| `P1-warp-factory-1035-Host477-focused-qualified-2026-10-08` | `188FA7ECC9E4CEF392EA17F206D9F0CEA2E5FE2757542847E89D9D1407142B12` |
| `P1-warp-opening-1039-Debug478-build-qualified-2026-10-08` | `3DD02F64EC89A8ACA81A05AB2B8CA203539D4B93D32E613CA4198AF536F6272A` |
| `P1-warp-opening-1039-Debug479-counter-build-qualified-2026-10-08` | `ECEE185FBB88F3186C3604263B1933ECCE196FAF0A7064EE774085762F845D34` |
| `P1-warp-opening-1039-Document480-focused-qualified-2026-10-08` | `F402FFE98CB93614BBEBDA9EC570AC0148EEB2B6349A9E9292F18107D1EA4784` |
| `P1-warp-lazy-1045-Debug481-build-qualified-2026-10-08` | `1FD7B5F82F8E121CAF9724F86DF24E9082828406FD856DADC81E8EEF905B7872` |
| `P1-warp-lazy-1045-Document482-focused-qualified-2026-10-08` | `5BF7F5A5786E4FA6B7580FC255D3FE00C212845B4C2CF933CF708B725F783514` |
| `P1-warp-lazy-1045-Debug483-full-failed-2026-10-08` | `EB78D9C63507434A930C1AE222F1CAB278618FE1D18DAF266AE9A1DF3BB112A0` |
| `P1-warp-lazy-1045-Debug484-context-full-failed-2026-10-08` | `8D1B87842121130C4530C55672F10D9E74C52D22E4E372A90B3DDBB599EA37C1` |
| `P1-warp-sample-1051-Debug485-build-qualified-2026-10-08` | `5D56934E2494A1B7EF94A4E0714CF1412641981A4B97AA1E580F108F6EDC310D` |
| `P1-warp-sample-1051-Debug486-full-failed-2026-10-08` | `2C78741353E1AA42E54ED793190EA77D648E93EF9FC833FA5D2545347031D377` |
| `P1-warp-sample-1051-Debug487-full-failed-2026-10-08` | `CD7F86BD3E694137169BCE4D854F99A39DB8C3CC2AA77A7C6550023A36DAAA4B` |
| `P1-warp-sample-1051-Debug488-full-qualified-2026-10-08` | `D14CFBBA4DEC79CA5A8E21416685D550EC65C29C1D141E9840FB1AD8C5B18B3B` |
| `P1-warp-sample-1051-Release489-full-qualified-2026-10-08` | `41C753D1CF7F3A87B67583FAABF1D441C7C850B255338E081E57C3D73521604A` |

The r477 runtime's actual child PID 26536 runs at `2026-10-08T15:30:58.225464Z`–`15:31:00.116993Z`.
Its `HostTests.exe` SHA-256 is `D851FC18F5768B05EA07F556CE0DC63308DBFEC1D8DC85926433C86A1A376A5A`,
bound to archived build r476. Main independently checks all 1,035 source inputs, the only three exact recorder
path aliases and the sole CLI allowlist addition permitting `HostTests`. Archive read-back verifies all 318
evidence files. Both destruction orders for the weak control block and queued graph preserve the original
4 MiB lease; the owner queues even small drafts, and the joined worker has no owner thread-local sink.

r478's actual child PID 48396 runs at `2026-10-08T15:36:43.418384Z`–`15:42:22.010345Z`. All 1,039 sources
and 318 evidence files are authenticated. Main review then finds that reopening and cancelling a blank Warp
can preserve source/root/history identity. r479 adds the captured opening counter to current-only validation
and nine independent reopen/budget assertions. Source identity checks remain required alongside that counter.
No opening runtime, ordinary activation, complete phase bound or latency claim follows from a successful build.

r480's actual child PID 87204 runs at `2026-10-08T15:51:09.070045Z`–`15:51:40.903676Z`. Main independently
binds its unchanged `DocumentTests.exe` SHA-256 `6EA820749B51F7EAD568943903D0C52571E39BCC5A053FC83E5C0556FAD31AD8`
to saved build r479, authenticates the identical complete 1,039-input source closure, all predecessor output and
the archive's 318 evidence files. The cheaper-agent r1 inventory contains two source-order errors; main identifies
them before runtime comparison and the r2 repair changes only that fixed order and lossless banner decoding.
The fixed 49-label inventory and summary pass, and all nine corruption mutants refuse. Existing numeric pixels,
history assertions and goldens are unchanged.

The subsequent lazy integration makes r479/r480 historical evidence. Production preparation now retains a placed
source view and allocates only tile metadata and the first Smudge tip; the tiny opening fixtures explicitly take
their existing literal 2x2 snapshot on a joined worker. A new 100 MP opening fixture requests no full snapshot.
The main also corrects the sampler agent's output-canary array size and independently calculates the 63/64 cache
corner's four taps at the correct global point (`64,64,63,255`). r481 builds successfully and focused r482
qualifies those changes. Main binds the 1,045 compiled inputs and saved r481 executable
(`8D01EE4BA06DAD0483562F1D763EFD8DA159B7E0B9A5A3A5F387C23F4E9B4253`), checks the complete predecessor
projection and fixed new inventories, and requires seven corruption mutants to refuse. Both archives authenticate
all 1,045 sources and 318 evidence files. The 100 MP opening allocates less than 1 MiB of new private storage,
retains the original placed sparse source and requests no full rendered image. This establishes a tested bound,
not timing or ordinary activation.

Full Debug r483's actual child PID 111104 runs at `2026-10-08T16:14:08.077564Z`–`16:21:11.108992Z` and exits 1.
Independent read-back verifies all 1,045 sources and 327 evidence files before repair. Application.cpp's earlier
Smear fixture leaves `smearMode = Blur`; document replacement deliberately preserves tool settings. The metadata
fixture directly calls `BeginWarp` without selecting a supported Warp mode. The repaired fixture explicitly selects
Liquify for its four Warp cases, restores the original setting at cleanup and adds an exact opening failure diagnostic.
All existing pixel, history, metadata and lifetime expectations stay unchanged. r484 must execute that repair;
integrity verification of the failed archive is not a gate pass. r484's actual child PID 93784 runs at
`2026-10-08T16:28:23.713783Z`–`16:35:41.324412Z`; all 1,045 sources and 327 evidence copies authenticate before
repair. The opacity fixture now explicitly initializes Liquify and restores the inherited mode at cleanup,
without changing its pixel, gesture or history expectations.

## CPU sample transfer integration

r488's actual child PID 26684 runs at `2026-10-08T17:07:20.136683Z`–`17:15:52.127992Z`, exit 0.
Independent read-back authenticates all 1,051 source and 329 evidence copies. Its stdout SHA-256 is
`0ECA4F11D2D5FBA26B21E9AB74F0071FF81F19A17F54D4FC70BAB0747A897354`; stderr is
`393C1C5520EB316BEF109ED7C78288F5FFCF0BB13F08E999B9C4DDB7ED204C99`.
This full Debug pass establishes neither ordinary detached Warp activation nor measured G1 latency.

r489's actual child PID 5568 runs at `2026-10-08T17:22:15.222588Z`–`17:30:37.278031Z`, exit 0.
Independent read-back authenticates all 1,051 source and 315 evidence copies. Its stdout SHA-256 is
`CE69A6407C3CF79E3944E25111DDE5DFBFC49AB043CEE051DA3D152E5A3DF8CD`; stderr is
`1BAC2ACD3B689AABE5C4C49C3D028A2FDEDB4C2CAD97B58D916408FF9002ECEA`.
Both successful profiles have the identical complete source freeze. Main's complete-output inventory passes
for Debug488 and Release489 on that same freeze: 1,051 inputs; 305 new assertions and 8 summaries; 31,478
candidate output records. Removing 313 Warp records yields the 31,165-record predecessor raw projection;
known-callback normalization yields 31,157 records, which compares equal. Each profile preserves all 5,467
application records across 113 stages. These are exact archived-source and executable-bound inventory results; they do not
establish live/final host publication, ordinary Warp activation or G1.

r490's actual child PID 39592 runs at `2026-10-08T17:32:12.259508Z`–`17:45:08.839486Z`, exit 0, in x64 ASan
Debug test mode on the same 1,051-input source freeze `616283681BCAFB0289D7B1DFB7D7B2B8669F91530B64CD312FFC53416465576E`.
The actual stdout SHA-256 is `2491F2E0E12CB1F339562C39914D197F58F3445E3993D1FE822397DB02F2AAC3`; stderr is
`F37BCAAD7DF85A0A16BF36D6DD700122B51F3DEDDC3FD7CE8772CA71B21710B2`. Independent archive read-back authenticates
1,051 sources and 316 evidence files. Completed receipt SHA-256 is
`6D950D7B6813192169F6AE2A68A004E2B31109E5A24B1C3F13E5DB404AEDE040`; source-manifest SHA-256 is
`7DA63ABD52374F9D8A6FCE6E4F000A4A9EF6AC2D779B683D8020A442CEC07B94`; evidence-manifest SHA-256 is
`F30EBFC932668C837D0CFA7A4BFCB8C39D83EB3EAD40FCE85DFB6CBC07DADFB5`. All 113 application stages are recorded.
Main's `.build/P1Drafts/WarpFoundationInventory-profile-r2/Main-ASan490-inventory.json` passes on the same
1,051-input freeze. It binds the exact archived runner and
`HostTests` source, which prove the second `HostTests --stress` execution. The 338 actual new records consist of
313 unique registry records (305 unique assertions and 8 unique summaries) plus the repeated Warp factory
lifetime suite's 25 records (24 assertions and one summary), yielding 329 actual assertions and 9 summaries.
The 40,083 candidate records project to the 39,745-record predecessor raw stream after removing those 338
records; known-callback normalization yields 39,737 records and compares equal. All 5,467 application records
across 113 stages remain unchanged. The original checker refusal is preserved in
`.build/P1Drafts/WarpFoundationInventory-profile-r2/original-ASan490-refusal.json` as checker adaptation history;
the old single-execution registry refused record 314. The profile-r2 adaptation changes no production code or
test numeric expectations.

Release analysis r491's actual child PID 114256 ran at `2026-10-08T17:49:52.618953Z`–
`2026-10-08T17:57:45.076699Z`, exit 0, in x64 analysis mode on the same 1,051-input source freeze
`616283681BCAFB0289D7B1DFB7D7B2B8669F91530B64CD312FFC53416465576E`. Its stdout SHA-256 is
`69D91E5E451F4A793AA2E99F7163C32117E345D7A593D3AF227FE32F54AF665E`; stderr is the empty-stream hash
`E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`. Independent archive read-back authenticates
1,051 sources and 329 evidence files. Completed receipt SHA-256 is
`D0CB307ABD881403CE611F42FCBA2837A4E5F70D28058A305C7D6021A6360E91`; source-manifest SHA-256 is
`7DF7B928307D0CE6CA8374DBC014A4B75C8DCADEB8E33A016F04DF75EA4D2E68`; evidence-manifest SHA-256 is
`C95F2956935FFEFFA8BF1D1BF762C0491C09B9F4AB928C5EEF3800CF8B0BB03C`. The attempt receipt is
`.build/incremental-x64-analysis-Release-r491/result.json`; the archived authentication receipt is
`.build/P1Drafts/WarpLive-r1/Main-Analysis491-archive.json`. This is an incremental analysis attempt with zero
application stages, not a full profile or G1 result.

ARM1051 r58 was prepared but never launched. ARM64 remains pending for the combined latest-source checkpoint;
no ARM64 cross-build is claimed here. No run here proves ordinary Warp activation or G1; P1 remains WIP.

r485's actual child PID 41928 runs at `2026-10-08T16:42:10.777929Z`–`16:47:48.532882Z`, exit 0. Its
1,051 sources and 318 evidence copies authenticate before the opening-plus-sequence discard repair. r486's
actual child PID 72992 runs at `16:50:08.881663Z`–`16:56:32.373322Z`, exit 1. The source/kernel remains unchanged
when the incorrect new fixture is repaired: the radius-20 pickup moves inside the image, with independently
translated spacing centers. A separate radius-2 hard pickup at x=0 followed by one +1 pixel dab requires exactly
three transparent pixels at x=0, y=1/2/3. No legacy numeric expectation or kernel formula changes.

r487's actual child PID 37796 runs at `16:58:07.059040Z`–`17:05:43.964922Z`, exit 1. Main binds its complete
Document subsuite to saved `DocumentTests.exe` SHA-256 `66F2D502845E5C68694D5E96B1570E29244B1B63EA66322098430D351ED25C16`.
The fixed 48-plan/45-session registry, two summaries, seven rejection mutants and complete 8,617-record predecessor
projection pass (8,712 total). Main corrects six label punctuation mistakes in the cheaper-agent inventory from
source before runtime comparison; the initial draft and intermediate refusal remain preserved. This proves the
completed CPU subsuite, not the failed full application gate or the subsequent project-open repair. Both failed
archives authenticate all 1,051 sources and 327 evidence copies before edits. Project-open case 28 directly opens
Warp after inheriting Blur; explicit Liquify setup/restoration repairs that fixture without changing its assertions.

Main integrates the next CPU-only slice after both failed full-run archives complete. A known factory owner
plans the sample from cached geometry, capacities and original source bounds without reading pixels or walking
tiles. A private original-point witness and weak producer control reject edited points, stale/foreign plans and
freed-address reuse. Explicit geometric path growth avoids copying every accumulated dab after each pointer
sample; scratch reserves its declared old/new capacity once before mutation. Outside-only and duplicate samples
retain the previous allocation envelope without inventing off-canvas tiles.

Capture requires the session's sole strong draft owner, records immutable source/history and moves the draft
into `WarpSampleInput`. While it is detached the session exposes only opening state; pixel/point/tile access
and competing paint, selection, adjustment, history and preview teardown routes refuse. Worker append is
single-use and owner calls refuse. Joined successful owner apply restores the same draft without pixel work,
document mutation or history changes. Cancel/replacement clears session authority while the original input
continues to own its private graph until worker retirement. Original-parent admission, recorded preview/final
publication, ordinary activation and G1 remain due.

The cheaper-agent test drafts are preserved separately. Main corrects optional access, explicit token construction
and the radius/movement/margin explanation, and uses the same zero fixture without its extra allocation/copy.
Main adds a changed-point/equal-envelope forgery control, live strong-borrow refusal, moved-input controls,
duplicate worker refusal and real prepared-before-stale document tests. A broadened predicate that would have
dereferenced a detached null draft is caught and repaired before integration. No existing numeric golden or
expectation changes. Build and actual runtime qualification must prove this integrated slice.

## Next integration

The x64-qualified opening/sample foundation is committed and pushed as `ef9c868`.
The immutable preview reader is now integrated, with 32 Document assertions and 24 scheduler lifetime
assertions. The main corrects the cheaper integration script's count labels: 33/15 literal `Expect`
occurrences include definitions and do not describe the executed 32/24 assertions. Include registration
order is formatted; `format.ps1 -Check` passes 928 files and skills validation passes 15 skills.

Debug build r492 fails the new preview refusal fixture's implicit cancellation-token construction.
Its completed archive `P1-warp-preview-1057-Debug492-failed-build-2026-10-08` authenticates 1,057 sources
and 320 evidence copies before repair. Actual child PID 101244 runs at `2026-10-08T18:01:22.008541Z`–
`18:01:41.813511Z`, exit 1, freeze `C10C3757BBE90D528CEC8A2C1B043DB9D07EB1C14EDAC03B78582B623BFC97D6`.
Main changes only that call to explicit `CancelToken{...}`; no numeric or lifetime expectation changes.
Debug build r493 completes successfully on 1,057-input freeze
`DA1D8002478E3B822F421CBDC187F3B2988A7805B3221D27A79AEE4DF91EB988`, all five targets and linked instruction controls.
Actual child PID 616 runs at `2026-10-08T18:04:47.565938Z`–`18:09:17.978744Z`, exit 0.
Focused Document r494 (PID 121196, `18:10:47.924883Z`–`18:11:30.700878Z`) and Host r495 (PID 93760,
`18:12:23.791922Z`–`18:12:25.610222Z`) both exit 0 on that same freeze. Main's
`.build/P1Drafts/WarpPreviewQualification-r1/Main-preview-proof.json` binds their actual executable hashes to
saved build r493: Document `1C640BFBBBBCBA3C436783F543EE2BA7BAD1D1A139CD19D143F1B5CF824E6DC8` and Host
`C166B35644925428A0F8A3721B4EF43431C98812D5DC07CC13798E8B4A0D43CB`. The 32/24 assertions and two summaries
match archived test source in execution order. Removing only these 33/25 records preserves the complete
predecessor PASS/INFO/SUMMARY streams (8,655 Document / 8,204 Host), with no callback normalization.
All three completed archives and their 1,057 source / 320 evidence copies independently authenticate.
This is focused Debug core qualification. Final replay, graphics/host publication, full combined matrix,
ARM64, ordinary activation and G1 remain open.

The private final replay API and 21 CPU reference controls are integrated after those archives complete.
Main repairs the cheaper-agent fixture before integration: original/sample bytes are distinct and the scalar
reference reads the independent literal fixture, with two analytic selected/transparent pixel controls.
Debug build r496 and Document runtime r497 both exit 0 on identical 1,059-input freeze
`ABBB973CB811C6A66F95C6B51ACEE9E75827105E555EBAA18A2970F9D3975C3C`. Build PID 27004 runs at
`2026-10-08T18:22:03.945242Z`–`18:27:27.739064Z`; runtime PID 65004 runs at
`18:28:23.088892Z`–`18:29:06.517116Z`. The runtime executable SHA-256 is
`EB3B1A6A4578B66610DD9714501AC74285C122B5E7BEA2906C8D682062A22B9C`. Main's
`.build/P1Drafts/WarpReplayQualification-r1/Main-replay-proof.json` binds it to the saved build, verifies all
21 assertions plus one summary, and preserves the exact 8,688-record predecessor stream (8,710 candidate
PASS/INFO/SUMMARY records). No numeric reference changes after execution. The completed build/runtime archives
independently authenticate 1,059 sources and 320 evidence copies each. Their completed receipt hashes are
`92BAE79F6DD5793BF9E5B5FC7C1803F2D28D39EBCABB34BA7A7D1D9AACA77C2D` and
`C336F5AD8CC88EC1F45A9A89C43333F500D3941277E722D275038BF16012ECF4` respectively.
This is CPU reference qualification; no compute or host activation is established.

The next staged capture retains the exclusive accepted Warp path, immutable source/history and explicit Warp
gesture kind. Conversion requires every captured point to advance its own ordered composition facade and final
coverage to complete; a raw brush replay cannot grant that path authority. Main reviews the cheaper-agent
authority draft and catches optional move-assignment and moved-from probing errors before integration.
Its initial spacing review incorrectly included the opening center in the recorded path. The 30 authority controls plus main's original-owner
worker-refusal control are integrated (31 total), along with the main-written numeric compute fixture.
All 15 skills validate and 936 files are formatted. Initial combined Debug build r498 fails solely on the
new worker source's missing complete `SharedPublicationMemory` include. Its actual child PID 115588 runs at
`2026-10-08T18:44:21.066526Z`–`18:45:40.162833Z`, exit 1, freeze
`128B085C8A68B845520B7FCF9863FBA00F61DC386B8190B0CF1E0704038AB063`. The completed failed archive authenticates
all 1,065 sources and 320 evidence copies before repair, receipt
`6F5B7C8DFA22B72174000FC3B511C4ADF8B859EF5F5653889C61C078E801F6F8`. Main adds the direct include without
changing algorithms or expectations. Fresh build/runtime qualification is due.

Debug build r499 passes all five targets and linked instruction controls on 1,065-input freeze
`F9C1D30C5CD4DB747AA82C8C65C44482DCD6A49A8C75283C9831FD8B94235CDA`.
Document runtime r500 fails seven authority checks (21 assertions reached). Its completed failed archive
`P1-warp-finish-1065-Document500-failed-2026-10-08` is authenticated before repair, receipt
`D360B62AB9EB02F4386654CC2B57EF15D7F8231477D17FD7CC2E3792CFF3F639`.
Main verifies both `WarpStroke::AppendForWork` and legacy `SmudgeLiquify.swift`: the first append only seeds
`last` and picks up Smudge; it does not record a dab. The distance-two fixture must record only 8.25 and 9.25,
not its opening center 7.25. Only this new authority fixture's path expectation is corrected. Existing pixel
goldens and production algorithms are unchanged. Debug rebuild r502 succeeds on corrected 1,065-input freeze
`C8CE3257AFDDF8680AF0C35E3B5FD8587942B2A40CB776B6C8D9C2A2280F6C7C`; Document r503 passes on that same freeze.

Composition r501 independently passes all 18 numeric compute controls on each of WARP and hardware (36 total,
zero failures) against build r499. Its full predecessor projection remains PARTIAL: asynchronous output from a
pre-existing callback changes print order at record 810 (actual `recording-pump.actual-recording-and-gpu-protocol-reaches-owner`,
expected `recording-pump.real-callback-keeps-original-source-device-and-order`). No complete predecessor claim or
normalized result is established. Its actual child PID 107184 runs at
`2026-10-08T18:59:01.320926Z`–`18:59:09.662728Z`, exit 0, executable SHA-256
`7E8C3ABA5F397F64DB1E3B87A726635345916E5946E3ED33DB5CE68699E42E03`. The completed archive
`P1-warp-finish-1065-Composition501-qualified-2026-10-08` authenticates all source/evidence copies, receipt
`9FE0B112463CDD2BD7F123FE493B6828A441F89B0EA526121AD2D3B809C94BED`.
This diagnostic uses the existing context-owned compute engine and joined worker uint installation; it does
not qualify original-parent asynchronous recording/readback, normal Warp activation or G1. r501 belongs to
r499's freeze, not corrected r502. The combined latest-source matrix remains due.

The worker-only factory preallocates whole-draft retirement and retains the supplied original accounting owner
through both queued node destruction and final weak control deallocation. Main corrects the agent's lifetime
tests to construct directly on the admitted pool worker and to install the deferred sink only on the owner;
the joined worker must have an empty thread-local sink. Literal source, both weak/node destruction permutations,
exact original reservation, below-threshold owner deferral and refusal expectations are retained.

This factory is not normal Warp activation. Opening/live/snapshot/final phase peaks, accepted sample/seal order,
all-touched preview metadata, prepared history/display, actual Cancel/Apply/Undo/device/shutdown and measured
R1/W G1 remain required. Later compiled factory edits make r473/r475 historical evidence for the prior source.

## Ordered Warp authority and final-host build follow-up

Debug build r502 passes on the corrected 1,065-input freeze `C8CE3257AFDDF8680AF0C35E3B5FD8587942B2A40CB776B6C8D9C2A2280F6C7C`. Its actual-child result has exit 0, stdout SHA-256 `3764A0A75C42C36A9B5B752FFCC8A786488AE682E6D02FB9846EF6A2966C4845`, and empty-stderr hash `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`. Canonical archive `D:\RedPrismQualificationArchives\P1-warp-finish-1065-Debug502-qualified-build-2026-10-08` and `.build/P1Drafts/WarpLive-r1/Main-Debug502-archive.json` authenticate 1,065 sources and 320 evidence copies; completed receipt SHA-256 is `685A9F88BE77776F7F2A51DA64A8ABCD21BB873C337508B40EB26F93F0229246`.

Document runtime r503 passes on the same 1,065-input freeze, using the actual `DocumentTests.exe` SHA-256 `A9D6EDC89A58AE8E8FDF7003410A97F55B169B7011E481FF0C7FEE73BE686D4F`. Its stdout ends `All document tests passed.` and hash is `E7A1317D522641C6F8C6036D3C9A18ABF8D078B44910CA54AC74EF66D52F6446`; stderr is empty. The independent `.build/P1Drafts/WarpCompletionQualification-r1/Document502-503-proof.json` source comparison verifies 31 authority assertions, zero failures, and exact predecessor sequence equality for all 8,710 records. Canonical archive `D:\RedPrismQualificationArchives\P1-warp-finish-1065-Document503-qualified-2026-10-08` and `.build/P1Drafts/WarpLive-r1/Main-Document503-archive.json` authenticate 1,065 sources and 320 evidence copies; completed receipt SHA-256 is `BEA4C3004A362C66AED49B0609691FE3BBDBA4580F6B6C98056C6607087F7FAF`. This is focused Document authority proof, not original-host activation or G1.

Composition r501 passes 18 numeric compute controls on WARP and 18 on hardware (36 total, zero failures) against build r499. Its full predecessor projection remains PARTIAL: the 4,255-record candidate and expected projection first differ at record 810 because pre-existing asynchronous callback output order differs. Do not claim a complete predecessor comparison or a normalized result. r501 is from the earlier r499 freeze.

The later final-host admission/application slice has 12 Document admission controls and 8 actual final-application controls integrated; it remains unqualified, and ordinary opening/live/preview Warp is not activated.

Debug build r504 fails on the 1,068-input freeze `40E53AF1F284C1E340EDD46C3B68E52E2D23F3752AE0462FAB088A94ACCCA7AB`. The compiler reports C2661 for the new `BrushStroke` fixture call, which lacks its sixth `nullptr` argument, along with integration errors. Its actual-child receipt has exit 1, stdout SHA-256 `5669A58438B42392EB840371067B2A42347754305716E80373714ED580B72021`, and stderr SHA-256 `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`. Canonical failed archive `D:\RedPrismQualificationArchives\P1-warp-final-host-1068-Debug504-failed-2026-10-08` and `.build/P1Drafts/WarpLive-r1/Main-Debug504-archive.json` authenticate 1,068 sources and 350 evidence copies; completed receipt SHA-256 is `9CD2E14CA9A40DC230E4134A51B82EED90A6A4905B024A96451BA8CCCE41B1A1`. Main corrected the test call; this archive remains failure evidence.

Debug build r505 then fails on 1,068-input freeze `3FD695513179D70A9B9FA17E0EF53591829DB43AE53EB095D3A52B207409E1F7`. It reports `warpInput`/`warpPointCount` missing from `FinishPayload` because those members were declared on `LiveRetirementCapture`, plus follow-on compile errors. Canonical failed archive `D:\RedPrismQualificationArchives\P1-warp-final-host-1068-Debug505-failed-2026-10-08` and `.build/P1Drafts/WarpLive-r1/Main-Debug505-archive.json` authenticate 1,068 sources and 377 evidence copies; actual child exit is 1 and completed receipt SHA-256 is `9A0A93BD396BB09F8FC081892ADD4654A0A1D3FA005E564D17AE253F3F742F9E`. Preserve r505 as a failed build. Main is correcting the member placement after archive authentication.

After the r505 correction, Debug build r506 fails on 1,068-input freeze `6E33C536977A8BA211D664E5208ACFE37ED8B10DC00F706E036C9A5A8E0BBA4F`: `StrokeOpeningMemoryBytes` is unresolved because its consuming test projects lack the `Common/StrokeOpeningWork.cpp` dependency. Canonical failed archive `D:\RedPrismQualificationArchives\P1-warp-final-host-1068-Debug506-failed-2026-10-08` and `.build/P1Drafts/WarpLive-r1/Main-Debug506-archive.json` authenticate 1,068 sources and 377 evidence copies; actual child exit is 1, stdout SHA-256 `A8DDAE7259542C05612A7FFA1D2507A11D3F42C013650B4F341E25741A6AEFC1`, and completed receipt SHA-256 is `88F488D94780C6C73FCE00B58B7EC4A6E1D356F2E5B1A73396E32A08EF962D26`. Archive authentication completed before Main adds the compiled project dependency. No algorithm or golden changed, and no pass is claimed for r504-r506.

## Experimental final Warp parent qualification

Debug build r507 succeeds on 1,068-input freeze `93579B384EF9EF05EA18C5AA35333F9CFE2D11532F45E946FAB04A4C5150E86C`.
Actual child PID 33152 runs at `2026-10-08T19:24:15.863026Z`–`19:29:06.935719Z`, exit 0, empty stderr. All five
executables and linked instruction controls pass. Completed archive `P1-warp-final-host-1068-Debug507-qualified-build-2026-10-08`
authenticates all 1,068 source and 320 evidence files, receipt `355AEA5860E0789677C30072A4D300B9E27053AD521B915766B6A77F1ABABE3B`.

Document r508 runs at `19:30:14.455937Z`–`19:30:57.970325Z`, PID 37816, exit 0, same freeze. Its unchanged executable
hash is `80E3495A985E38955934E7751C42062722419A0AFF39951E537746AD48F13982`, bound to saved r507. Main's
`.build/P1Drafts/WarpLive-r1/Main-Document508-proof.json` verifies the fixed 12 assertion labels and zero-failure
summary against archived source, the unchanged 31 authority controls and exact 8,742-record predecessor projection.
Completed archive `P1-warp-final-host-1068-Document508-qualified-2026-10-08` independently authenticates the same
source/evidence counts, receipt `C7C3DE9C08812004A2F39EAA0380C26C45C8C05C4D5CEBCE0ACB975EE8D9228E`.

Actual Application r509 runs at `19:32:00.389114Z`–`19:34:19.229783Z`, PID 59872, exit 0, same freeze. Its unchanged
executable hash is `C294BE95A1A2BC20C62E3377AEA4A71854B3F7D792E5D9638492E215C38AFB4E`. Both Liquify and Smudge pass
the four original-parent controls each: held final worker after real recording/readback, complete final raster and
placement against the original scalar reference, one correctly named history, acknowledged display/rows and
off-owner retirement, then exact Undo. The entire application reaches all 114 ordered stages. Completed archive
`P1-warp-final-host-1068-Application509-qualified-2026-10-08` authenticates all 1,068 source and 320 evidence files,
receipt `12538CCFFE0EB987310D3A5702003E5AC11DB7417486C1F986A333789CEB0974`. Stderr contains the existing
successful export-host case records, not a diagnostic failure. The original opening and sample preparation are
explicit joined core fixtures; this is experimental final-host proof, with no ordinary Warp live route or G1 claim.

After these archives authenticate, the next fixture adds synthetic keyboard isolation, actual final-display
pixel readback parity and held-final cancellation in both modes. It requires the literal old source/history,
unchanged acknowledged display and exact old reservation after joined off-owner retirement. Four successful
and three cancellation controls per mode total 14. Full Debug r510 passes; its actual receipt and independent
qualification are recorded below. The earlier r509 result does not qualify these new assertions. Release/real
ASan, analysis and combined ARM64 remain pending.

The next preview graphics draft is isolated under `.build/P1Drafts/WarpRecordedPreview-r1`; no production
composition edit or runtime qualification is claimed. Main drafts exact immutable placed-original/touched-tile
band reads and source/grid validation. A cheaper agent supplies only independent literal quadrant bytes and
translation expectations under `WarpRecordedPreviewTests-r1`. Main has drafted the original-ticket recorder
fixture for translated, scaled, rotated, sparse and blank sources in both modes, preserving actual command
acknowledgements, private inherited grids and last-use barriers. It is uncompiled and has no actual GPU pass
yet; Main reviews and integrates it only after the current producer freeze ends.

### Strengthened full Debug r510

Actual test child PID 34828 runs at `2026-10-08T19:36:56.743977Z`–`19:46:57.357832Z`, exit 0. All required
Debug suites, crash probes, instruction controls, RAW corpus controls and harness captures pass. The application
reaches all 114 stages and all 14 final Warp assertions pass, including actual GPU display pixels and both
held-final cancellations. Identity fixtures still begin with explicitly worker-prepared core opening/samples;
this proves neither ordinary live Warp activation nor G1.

- Source freeze: `1B604725BB49AFBF2AD4EFEA36DC00F0478A12D4269ADF65D7AC82C9A71EA420` (1,068 inputs).
- Stdout: `BF19621BDC72430E263EFCC742876AA6F126D28D7838095810336CB823298056`.
- Stderr: `355A1A2B1F563FC78AD395CC347F95908FDF61AA3A7F2B84D12AEFAFAEC0C3DF`.
- Completed archive: `D:\RedPrismQualificationArchives\P1-warp-final-host-1068-Debug510-qualified-2026-10-08`.
- Completion receipt: `10B5924C18E89F9E8D7BE1D2BDA667055E39FCF1D40C98252F800DD82D87F7FE`.
- Source/evidence manifests: `83B331AAB26D794ED5AD12A863371C20F805C1B37557D4AA8858702C79F4B6F0` /
  `400F6EA64CB280A01D5BADA4740D2093E2DF68130E4F23F33BA5F5004D15F4AF`.
- Main independent read-back authenticates all 1,068 source and 331 evidence files before Release starts.
- `Main-Debug510-profile.json` independently checks the fixed 14 labels, 114 unique stages, source hash
  `96A7F7D485866FD4056066DF55B087AAF91DCFA2F7B23563DE8505CE3B686A3D`, and the required core summaries:
  32 reader, 24 lifetime, 21 replay, 31 authority, 12 admission and 36 WARP/hardware compute assertions.

The profile check initially refuses localized MSBuild bytes as invalid UTF-8. Main corrects only the checker:
authenticate the full byte hash, then decode irrelevant localized build text with replacement while matching
the ASCII PASS registries exactly. No source, golden, assertion or producer result changes. Main also fixes
the checker to require the real lifetime summary's `passed=24 failed=0 controls=24` spelling.

Full Release r511 completes on freeze `1B604725BB49AFBF2AD4EFEA36DC00F0478A12D4269ADF65D7AC82C9A71EA420`.
The actual x64 test child PID 98816 exits 0 from `2026-10-08T19:47:39.473555Z` through
`2026-10-08T19:56:34.323118Z`, with 1,068 frozen inputs and no automatic retry. Its stdout/stderr SHA-256 values
are `39CAA4BDE339E0242C318179A6A9E6AA7C52C6CE76247B53E6AA72E35603AAB3` and
`519EF1DCBB7984D6BFF3DEB243061202151C95CE593291F4143EFE64C099DE2D`. Main's independently checked profile
receipt reports PASS, the same freeze, 14 application controls, all 114 application stages, one host execution,
and the zero-failure core summaries for 32 preview-reader, 21 replay, 31 finish and 12 finish-admission checks;
its bound source hash is `96A7F7D485866FD4056066DF55B087AAF91DCFA2F7B23563DE8505CE3B686A3D`. The actual completed
archive `D:\RedPrismQualificationArchives\P1-warp-final-host-1068-Release511-qualified-2026-10-08` authenticates
1,068 source and 317 evidence files. Its completion receipt SHA-256 is
`D3489B10BE9A26385EF56426185C81B6D70DB882AA55D61C08DE53F30B5D1552`; source/evidence manifest SHA-256 values
are `F1AF011F0AF1F4B3D2A3E8EFA869E59700ABD4BFFABCF3A84AFA7668A2D89F51` /
`F2915239A561A36F51973E7330C9965E2CEA575BFAEA9093C2326DDBF862EE27`. The ignored
`.build/P1Drafts/WarpLive-r1/Main-Release511-archive.json` and `Main-Release511-profile.json` independently record
these checks; the archived source-freeze, actual-child result, start receipt, stdout, stderr and in-interval
self-test log hashes were read back and matched against the evidence manifest. This is the strengthened final-host
qualification only: no ordinary live Warp activation or G1 claim follows.

Main review corrects the draft's transposed stdout hash against the actual archive; no test evidence changes.

ASan r512 is running/pending; no result is claimed because no independently checked completed
archive is present. Release analysis and combined ARM64 cross-builds remain pending. The separately staged cheaper
agent ARM64 r59 scripts were main-reviewed and corrected to retain actual `failed` archive labels, run the
independent archive checker before the next configuration and stop after an actual failed child; no ARM64 r59
execution is claimed, and native ARM64 runtime is unavailable.


## Full real-ASan r512 — main verification

Actual test child PID 40656 runs at `2026-10-08T19:57:24.469068+00:00`–`2026-10-08T20:12:28.681775+00:00`, exit 0.
The entire real-instrumented suite, detection probe, both crash probes, all 114 application stages and harness
captures pass on the same 1,068-input freeze `1B604725BB49AFBF2AD4EFEA36DC00F0478A12D4269ADF65D7AC82C9A71EA420`.

- Stdout/stderr SHA-256: `36CD91266F7B0F8B16A3F1EB41F92A80283E3ADFE0F45C72C12F9F565177D849` / `31E05D32EB91B740AC8B574F424E5AE5F6B5B0DD6E337E2C053791C989129D4E`.
- Completed archive: `D:\RedPrismQualificationArchives\P1-warp-final-host-1068-ASan512-qualified-2026-10-08`.
- Completion receipt SHA-256: `F37CB8DC7153FE2F14BD1BF3DB80D09599EB51968EE845DB81731A00C5F0932D`.
- Main independent archive checker authenticates 1068 sources and 318 evidence files.
- Main fixed profile check passes all 14 actual final-Warp assertions, the fixed core summaries and both required
  HostTests executions. No predecessor projection, ordinary Warp activation or measured G1 claim is added.

Release analysis r513 starts only after this actual archive and both independent checks pass. ARM64 r59 remains
pending until analysis completes and its actual archive is checked.


## Release analysis r513 — main verification

Actual analysis child PID 94488 runs at `2026-10-08T20:13:21.901460+00:00`–`2026-10-08T20:23:48.671645+00:00`, exit 0.
All five products, concurrency-analysis controls, linked ISA/PDB controls and exact DxUi pin verification pass
on the unchanged 1,068-input freeze `1B604725BB49AFBF2AD4EFEA36DC00F0478A12D4269ADF65D7AC82C9A71EA420`. This is analysis/build evidence;
no application runtime or G1 measurement is inferred.

- Stdout/stderr SHA-256: `D4A458E03AD23F106F467D890988741DE04C7970D3377E8F8C7893615BBDCFE6` / `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`.
- Archive: `D:\RedPrismQualificationArchives\P1-warp-final-host-1068-Analysis513-qualified-2026-10-08`; completion receipt `C3EAEE873ADF2B9C5A65F5E0B4C573091EE61EAAB18EA41CDB2E2290DEAAA43E`.
- Main independent checker authenticates 1068 sources and 331 evidence files.

Reviewed ARM64 r59 starts at `2026-10-08T20:24:55.994111Z` with recorder PID 82388 only after analysis archive
authentication. Each configuration must finish, archive its actual result and independently authenticate before
the next one starts. Native ARM64 runtime remains unavailable; none is claimed.


## Combined ARM64 r59 — main product verification

All three actual cross-build children exit zero on the same 1,068-source freeze as x64 Debug/Release/real ASan
and Release analysis. Each archive was independently authenticated before the next child was launched. Main
verifies every source/evidence copy, actual process interval, prebuild snapshot and all 15 ARM64 PE/PDB products.
The five ASan products additionally retain real sanitizer compiler/image controls. Native ARM64 runtime remains
unavailable; this closes cross-build qualification only.

- **Debug**: actual PID 9236, `2026-10-08T20:25:36.304664+00:00–2026-10-08T20:27:46.879175+00:00`, exit 0;
  archive `D:\RedPrismQualificationArchives\P1-opacity-host-proof-1068-ARM64-Debug-r59-qualified-2026-10-08`; completion SHA-256 `D1F72F369CF0BE921C59CA80E3BFEE51E96B5841011F6EB9AD1FD7DE742FDAC4`.

- **Release**: actual PID 106564, `2026-10-08T20:28:40.308459+00:00–2026-10-08T20:31:12.807289+00:00`, exit 0;
  archive `D:\RedPrismQualificationArchives\P1-opacity-host-proof-1068-ARM64-Release-r59-qualified-2026-10-08`; completion SHA-256 `FFFCB751F0A932ED8F8E6796A05EEB70B57F5ACC44F65A5BF16E25F80B579E3A`.

- **ASan Debug**: actual PID 39400, `2026-10-08T20:31:56.032656+00:00–2026-10-08T20:33:33.857901+00:00`, exit 0;
  archive `D:\RedPrismQualificationArchives\P1-opacity-host-proof-1068-ARM64-ASan-r59-qualified-2026-10-08`; completion SHA-256 `950E053B1113D145D705A40AE389243B1752B1D00F4C2431D372AAC35564AC4C`.

Main proof: `.build/P1Drafts/Arm1068-r59/Main-ARM59-products.json` (PASS, three profiles / 15 products).
All 15 skills and 939 formatted files pass. No DxUi change is included; its exact pin remains
`8e8994079e82f79e6765e6a885c03fc687957591`. The source checkpoint is qualified for commit/push, while
recorded Warp preview, original opening/live activation, broader P1 delivery areas and measured G1 remain open.
