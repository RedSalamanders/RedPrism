# P1 Selection Gesture Residency — 2026-10-17

Private x64 Release R2 qualifies the initial 30-control close/click residency scope; the two further review gates below remain open. It passes 30 new + 22,334 predecessor
assertions = 22,364 passing, 273 actual compiler command records, 1,617 verified archive files and zero ISA violations.
Actual runtime PID 87276 exits 0, `2026-10-17T00:24:58.768810Z`–`2026-10-17T00:25:07.546714Z` (UTC).
Source/dependency/product bookends and exact executable/PDB identity pass Main's qualification.

R1's compiler child PID 91820 exits 1: C2653, `CanvasDocumentDraft` undeclared because the fixture omitted
`DocumentDraft.h`. No runtime launches. Main retains its 1,590-file archive, completion
`98B729E7F2A4F415EF2E8E7BAF1AB8FBE0284F2A2E81E877C584CAD40D9903D2`; receipt: `.build/P1Drafts/S5PressResidency-r1-Main/main-failed-verified.json`.
R2 adds the missing include and strengthens the reservation oracle below; all 30 predicate labels are unchanged.

Qualified source remains under `.build/P1Drafts/S5PressResidency-r1-Main/SourceFull/` (the repaired R2 source,
not the failed R1 snapshot). `PreparedSelectionMoveClosure` exposes a nonblocking residency try and cancellable
worker refresh. Owner `DeferCompletion` first validates the prepared intent, then tries the residency permit:
Invalid refuses; a non-ready witness resets the permit and resumes `RefreshClose` or `RefreshClick` on the
original ticket. The worker refreshes existing prepared history; final output transfer waits for a Ready witness.
Zero-offset End reuses the original geometry; click closes End, resets the permit, then captures/prepares Deselect.
`Completed` resets its permit before rollback, retirement binding, prefix acknowledgement and host callbacks;
joined-Close retirement also resets first. No fresh admission or repeated closure preparation is needed.

The five event-gated modes supply six assertions each:

| Mode / literal test label | Qualified control |
| --- | --- |
| lateMove / `late-move` | Materialize the history-only old 64 × 64 BGRA cache after Close preparation: exactly 16,384 bytes; one refresh publishes moved bounds `{3,5,9,9}` and trims the oldest history to one past entry. |
| lateClick / `late-click` | Materialize the same late cache after Deselect preparation; one refresh preserves zero-offset End plus Deselect, leaving no selection and one past entry. |
| busyRelease / `busy-release` | A separate thread holds the residency lease; releasing it lets the original parent finish, preserving two past entries. |
| cancel / `busy-cancel` | Cancel the original ticket while refresh is contended; abort restores bounds `{1,2,7,6}`, original history revision and one past entry. |
| joinedClose / `busy-close` | Joined scheduler Close cancels the original contended wait; rollback/retirement complete and no terminal host callback is delivered. |

All successful modes retain generation delta 2; cancelled/joined-Close modes retain delta 3 and history depth 0.
The explicit refresh-inspection event proves reservation **at least original total + 8,192** for an independently
admitted endpoint observer, while worker `remainingBudget == original total - openingBytes`. After original admission,
the sampler reports scarcity; refresh neither requeries nor readmits. Every mode prepares Close once and refreshes once; lateClick also prepares Deselect once.
The ten-second deadlock watchdog expires as a failure, never a passing release. Successful terminal callbacks can
acquire a residency lease after the gesture permit resets; cancellation and joined-Close release paths are checked separately.
After all external raster, state and session readers release, the independent endpoint still reports `Bytes()` and
`ResidentBytes()` exactly 8,192: all original gesture ownership has refunded. The endpoint is released last.

Frozen source identities below match R2 `expected.json` and were checked with `Get-FileHash -Algorithm SHA256`.
Anchors use the source root above; owner refresh/publication paths are additionally at job `.cpp:194`, `:244`, `:283`;
the independent reservation, late-cache and refund witnesses are at test `.cpp:294`, `:330`, `:349`.

| Source anchor | SHA-256 |
| --- | --- |
| `Common/SelectionMoveWork.h:71` | `E6D627477682E82EC3B981F74DF258BC30EAD4F1BCC901287B0ABAE012FAEE0C` |
| `Common/SelectionMoveWork.cpp:60` | `C2D3A3C987E60DDA66713D47412F9B3B115DA3039D42DC2EBCB0E12A57E852B1` |
| `RedPrism/SelectionOutlineGestureJob.h:23` | `805207BB32D8317BE61EA32400E1C9C903465F396A76DE3CD6F0C7E7577F6C01` |
| `RedPrism/SelectionOutlineGestureJob.cpp:98` | `012DF1AB38F2B2334D1CAA5CC057FE0E7DE6F51D1090D1BD5CDE92401C9EBB13` |
| `Tests/DocumentTests/Document.Tests.SelectionGestureResidency.cpp:200` | `2365988E1F01E2055AF31C2E90D53862BDB3CB1FB148725CEEC980912590E63B` |

Release identities from `.build/P1Drafts/S5PressResidency-r2-Main/main-qualified.json`:

| Artifact | SHA-256 |
| --- | --- |
| Receipt file (recalculated) | `CA2D901AAEE2B7E29F096BE93B68276393CC5777C32FC3504D6786C54157F2B1` |
| Runtime stdout | `8F5FA736C6FE0B663426C5696EEBDDFFF3275A3C28A7B719F4A1A0B6065F478E` |
| Executable | `C6A70EE691B8474AEF88721E4A36312C5AC93A7892C648A2BDE982ED2DDFEDC8` |
| PDB | `60E2BA06486AC26919EC8D091CA8BBE54C64BC892DAF26AC98CB4F414DC00B07` |
| Archive completion digest | `598B291266B470FFF82213E97A13BACD0D9AB6AE99CC227022B5DFB6D0639008` |

CodeView GUID `7483BEE1-E4DF-4E8E-889F-F9B6EFBD6876`, age 1. R2 design/recipe:
`.build/P1Drafts/S5PressResidency-r2-Main/{design.json,expected.json,archive-receipt.json}`.

The exact-source real ASan R1 scope also passes; its actual result and limits are recorded below.

This protocol remains private: owning contracts, milestone integration and the native caller have not adopted it.
Actual Application/ordinary activation, native adoption, G1 and P1 Done remain open.

## Real ASan R1 scoped pass; two further review gates remain open

Main qualifies real ASan R1's existing 30 residency facts plus all 22,334 predecessors (22,364 total):
actual runtime PID 63416 exits 0, `2026-10-17T00:31:34.364185Z`–`2026-10-17T00:34:01.093575Z` (UTC),
with 273 actual instrumented command records and 1,623 verified archive files. Its own UAF detection control
is PID 27888, exit 1. Archive completion SHA-256:
`D9FE4A8276C57C9215369AC5BA7912516FB2B356FB34F8124AA9BB5B7842A0EF`.
[ASan Main qualification](../../.build/P1Drafts/S5PressResidencyAsan-r1-Main/main-qualified.json).
The Release and ASan receipts cover the same unchanged compiled source.

Both CPU profiles—Release R2 and real ASan R1—remain **SCOPED 30-FACT PASSES**.
Their independently confirmed [Release review limits](../../.build/P1Drafts/S5PressResidency-r2-Main/review-limits.json)
and [ASan review limits](../../.build/P1Drafts/S5PressResidencyAsan-r1-Main/review-limits.json) retain two open gates:

- A foreign transform opened during Close refresh leaves generation/revision unchanged and can pass generic
  closure currentness, which omits the pending-transform guard. The available
  `IsPreparedDeferredSelectionMoveGestureCurrent` guard requires an owner-path repair before code integration.
- The final-refund-only oracle does not establish live charged retention; a stronger live-charge observation is required.

Main is preparing a fresh repair and stronger fixtures. Neither scoped CPU pass completes H3 or authorizes code integration.
Owning contracts, milestone/native-caller adoption, ordinary activation, G1 and P1 Done remain open.

## Stronger guard/oracle repair under validation

Main independently confirms the foreign-transform gap from the complete opening guard, `BeginTransform(true)`
and closure currentness. The fresh candidate now checks `IsPreparedDeferredSelectionMoveGestureCurrent` before
Close currentness and its permit try, including after a refresh. Deselect already checks its full restore authority.

The fixture grows to 47 controls across seven modes: the original 30 remain, five live-charge observations are
added, and Foreign Move/Click add 12 controls. They preserve the exact foreign transform address/draft, document
and history generation/revision/depth, selection root/opening, original ticket and callback lease availability.
The charge expectation follows the history graph: Move's entry retains its charged opening snapshot and final
entry/after/translated geometry; zero-offset End drops the opening before charged Deselect; Abort restores the
original uncharged seed. The independent endpoint observes those live charges before checking their final refund.

The first stronger fixture build R3 stops on C4456 (a shadowed local); no runtime runs. R4 stops at the unchanged
2,500 MiB headroom preflight before launching any compiler. The first fixture-name repair R5 incorrectly renames
two `TransformEdit::draft` member accesses and stops on C2039; no runtime runs. Main verifies the 1,590/1,587/1,590-file
archives and retains each failure/refusal. The latest repair restores the original member accesses and renames only
the local variable; reverse local renaming reproduces the original foreign branch. Production code and all 47
predicate expressions/labels remain unchanged. Fresh R6 is running; its 22,381-assertion result and exact-source ASan
remain unqualified. These preparations do not close H3, the actual caller or P1.

## Stronger 47-fact Release R6 and real ASan R2 qualified; CPU review gates closed

Main independently qualifies both exact private profiles: **47 + 22,334 = 22,381 passing assertions**,
273 actual compiler records each (instrumented for ASan), complete source/dependency/product bookends and zero ISA violations.
Release R6 is actual PID 63568, exit 0, `2026-10-17T00:51:17.550729Z`–`2026-10-17T00:51:26.320404Z` UTC,
with 1,617 verified archive files. ASan R2 is actual PID 52284, exit 0,
`2026-10-17T01:14:53.847436Z`–`2026-10-17T01:17:20.366129Z` UTC, with 1,623 verified archive files.
Its original same-executable `--asan-probe` is PID 19708, exit 1,
`2026-10-17T01:14:53.571025Z`–`2026-10-17T01:14:53.741317Z` UTC, with the required heap-use-after-free diagnostic.
The qualifier verifies the exact original 60-row ASan profile witness; Release evidence is not substituted for that stream.

The 47 retain the original 30 labels, add five live-charge observations and 12 Foreign Move/Click controls.
Close now checks `IsPreparedDeferredSelectionMoveGestureCurrent` alongside generic closure currentness before
its permit try, including after refresh; Deselect already checks its full restore authority. The foreign controls
open `BeginTransform(true)` during the refresh without changing generation/history, then prove `E_ABORT` preserves
its exact transform address/draft, selection root/opening and history state, original ticket and callback lease availability.

Live-charge checks observe the independent 8,192-byte endpoint **before** releasing readers: successful Move retains
`8192 + finalOutputBytes + openingBytes`, click retains `8192 + finalOutputBytes`, and abort/joined Close retains `8192`.
Foreign Move retains `8192 + openingBytes`; Foreign Click retains `8192`. Releasing the last external state/session/history
readers then returns each endpoint to exactly `8192`. These observations close the live-retention gap as well as final refund.
The opaque guard and these stronger controls concretely close the two CPU review gates for these frozen profiles.

Current qualified source is `.build/P1Drafts/S5PressResidency-r3-Main/SourceFull/`. SHA-256 values below match
Release `expected.json` and current bytes recalculated with `Get-FileHash -Algorithm SHA256`:

| Source anchor | SHA-256 |
| --- | --- |
| `RedPrism/SelectionOutlineGestureJob.cpp:203` | `FAC8A0C8EA9339B36B8EF478F1029C974045C038BD089546B4CA6D18AF3D90BA` |
| `Tests/DocumentTests/Document.Tests.SelectionGestureResidency.cpp:301` (foreign), `:400` (live charge) | `EBC3D11FD52ED2C0F84A0B2DE713759A7744A4FBD92B961316CD0C06990D6D18` |

| Qualified receipt | Receipt-file SHA-256 | Archive completion SHA-256 |
| --- | --- | --- |
| [Release R6](../../.build/P1Drafts/S5PressResidency-r6-Main/main-qualified.json) | `1691B467CBD80B031A9A5BC9C378AC6C8C7EA1B294D94B5B797CE9E1EBAD504F` | `7564FED8CFA5F2431D3C0F9C4FA25B0B005F8D15FA71C654D65D858DF9390A1F` |
| [Real ASan R2](../../.build/P1Drafts/S5PressResidencyAsan-r2-Main/main-qualified.json) | `5C020D452FA96AA654E119EFD6CD97ECABEE60383CEDBBC770F54EAE6FE17A77` | `D8D4E1F503E02C44564AD3EACDD080CA060F01AAB9E578E6A0472E0CDF5C52F7` |

Receipt-file digests are recalculated; completion IDs come from those receipts. Earlier failure chronology remains retained:
R1 missing `DocumentDraft.h`, stronger R3 C4456, R4's unchanged headroom preflight refusal, and R5 C2039.
The R6 local-identifier/member-access repair preserves every original 30 label and all 47 expressions/labels.
This append supersedes the earlier pending stronger Release/ASan status and the two open CPU review-gate statements only.

Actual Win32 caller qualification remains separate. [App R4 failure](../../.build/P1Drafts/S5PressHost-r4-Main/main-failed-verified.json)
is PID 97936, exit 5 at the cancelled-mode wait for `waiting && moves == 1`, after 23 checks with zero reported
predicate failures; Main verifies 1,687 archive files and leaves the cause unconfirmed. App R5's strict prior-host
prerequisite refuses this failed predecessor before runtime; its build/ISA results do not release the runtime gate.
[App R5 prerequisite](../../.build/P1Drafts/S5PressHost-r5-Main/previous-host-qualified.json).
App R6 is compiling with failure-only diagnostics and 21 new joined-shutdown controls; the planned 340 sources,
145 stages and caller outcomes remain unqualified. Existing success predicates and deadlines are unchanged.
[App R6 preparation](../../.build/P1Drafts/S5PressHost-r6-Main/source-preparation.json).

These CPU outcomes do not close H3 across all callers, native integration, ordinary activation, measured G1 or P1 Done.
