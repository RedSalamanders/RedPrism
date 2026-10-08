# P1 Warp worker primitives and retirement — 2026-10-08

Status: focused Debug numeric qualification passes for the cancellable primitives and worker factory lifetime.
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

ARM1051 r58 was prepared but never launched. Main will qualify ARM64 on the combined forthcoming preview
checkpoint 1057 to avoid repeating the full matrix for the metadata-reader edit. ARM64 remains pending until an
actual later receipt. No run here proves ordinary Warp activation or G1; P1 remains WIP.

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

The worker-only factory preallocates whole-draft retirement and retains the supplied original accounting owner
through both queued node destruction and final weak control deallocation. Main corrects the agent's lifetime
tests to construct directly on the admitted pool worker and to install the deferred sink only on the owner;
the joined worker must have an empty thread-local sink. Literal source, both weak/node destruction permutations,
exact original reservation, below-threshold owner deferral and refusal expectations are retained.

This factory is not normal Warp activation. Opening/live/snapshot/final phase peaks, accepted sample/seal order,
all-touched preview metadata, prepared history/display, actual Cancel/Apply/Undo/device/shutdown and measured
R1/W G1 remain required. Later compiled factory edits make r473/r475 historical evidence for the prior source.
