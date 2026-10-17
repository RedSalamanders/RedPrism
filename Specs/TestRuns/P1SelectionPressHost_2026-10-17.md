# Actual Win32 selection-press caller — Release R7 qualification

Date: 2026-10-17. Main-reviewed private caller qualification; integration remains open.

Main's Release-only receipt now exists: status `MAIN_PRIVATE_CURRENT_WIN32_SELECTION_PRESS_RELEASE_R7_QUALIFIED`, qualified at `2026-10-17T01:45:38.540033+00:00`. The new actual-caller real-ASan outcome is a separate open gate; this paper claims no ASan pass.

## Scope and source

Private hidden Win32/WARP Application, actual WndProc messages sent by the fixtures, two workers, original resumable ticket and cached admission. The new physical route requires `_selectionPressTestEnabled`, New mode, Rectangle marquee, no Ctrl, and an existing selection hit inside. This is neither hardware-input coverage nor ordinary activation. Outside-hit drawing, other branches, complete S5/display bounds, native adoption of this candidate, G1 and P1 Done remain open.

Source root: `C:/Users/eric/.codex/worktrees/p1-opacity-host-proof/RedPrism/.build/P1Drafts/S5PressHost-r6-Main/SourceFull`.

Source anchors: `RedPrism/ApplicationSelectionPressJobs.cpp:12` (private gate); `RedPrism/SelectionOutlineGestureJob.cpp:139` (original-parent continuation); `RedPrism/Application.Tests.SelectionPressGesture.cpp:81` and `RedPrism/Application.Tests.SelectionPressShutdown.cpp:94` (fixture Until loops); `RedPrism/Application.cpp:365` and `Common/Jobs.cpp:2013` (canonical completion/UI-action order). All anchors are relative to the source root above.

## Retained attempts

| Attempt | Actual result and retained scope | Archive files | Completion SHA-256 |
| --- | --- | ---: | --- |
| App R4 | PID 97936, exit 5; three physical modes complete, 23 passing checks before cancellation Until returns false. Cause was unconfirmed in the original receipt. | 1687 | `92AF20A524FAC7F255623160374C96DEE7BB7E8EA18F377F814A048437D57466` |
| App R5 | Build 0 / ISA 0; strict failed-R4 prerequisite refusal before runtime. No child runtime PID or outcome exists. | 1688 | `82434D1BFC4E3B7BE987BF53E2C11A82305ED0693CC5ED214B37F62C31771552` |
| App R6 | PID 93128, exit 5 at the same wait; failure-only diagnostics identify the fixture pump gap. New 21 shutdown controls were not reached. | 1694 | `E5265D5A6B949DB7795B851A0F23A2EE43A04CB9A2C16FF852E76E9C86F19FCA` |
| App R7 | PID 76536, exit 0; repaired fixture pump reaches all physical/shutdown/scalar and inherited Release records. Main qualifies Release only. | 1698 | `6977FCDB43D291C2442BCA367F9F4B9738561A88EEED19FD0AE192E9CA493C6A` |

Receipts:

- R4: `C:/Users/eric/.codex/worktrees/p1-opacity-host-proof/RedPrism/.build/P1Drafts/S5PressHost-r4-Main/main-failed-verified.json`; runtime `2026-10-17T01:09:19.571034+00:00`–`2026-10-17T01:09:32.140648+00:00`.
- R5: `C:/Users/eric/.codex/worktrees/p1-opacity-host-proof/RedPrism/.build/P1Drafts/S5PressHost-r5-Main/main-prerequisite-refusal-verified.json` and `previous-host-qualified.json` in that folder; gate status `PREDECESSOR_ACTUAL_R4_FAILED_R5_RUNTIME_NOT_AUTHORIZED`.
- R6: `C:/Users/eric/.codex/worktrees/p1-opacity-host-proof/RedPrism/.build/P1Drafts/S5PressHost-r6-Main/main-failed-verified.json`; runtime `2026-10-17T01:35:41.867953+00:00`–`2026-10-17T01:35:54.387097+00:00`.
- R7: `C:/Users/eric/.codex/worktrees/p1-opacity-host-proof/RedPrism/.build/P1Drafts/S5PressHost-r7-Main/main-qualified.json`; runtime `2026-10-17T01:38:40.547184+00:00`–`2026-10-17T01:39:55.743607+00:00`.

R6's exact retained diagnostic:

```text
DIAG selection-press.cancelled wait: capture-before=1 tracking-before=1 capture-now=1 tracking-now=0 busy=1 sampling=1 pending=2 receipt=0 state=1 controller=0 waiting=0 opened=1 closed=1 moves=1 terminal-lost=1 completed=1 aborted=0 source-current=1 status=0 error=00000000
```

Main's confirmed explanation is that Until pumped messages and completions without RunUiActions. The new Down+Move remained queued (`pending=2`), while the observed state was the prior completed Lost case (`controller=0`, `closed=1`, `completed=1`). R7 adds RunUiActions immediately after DrainCompletions in both fixture Until loops, matching Application and SettleJobs. All original success predicates and deadlines remain unchanged. No production mouse-leave, tracking or capture behavior was changed.

R6/R7 before.json source maps have no added/removed SourceFull paths and exactly two changed files. R7's build log lists only these two freshly compiled TUs; the complete actual command/object inventory has 340 records. It is not a 340-TU fresh rebuild.

| Source under the root above | R7 SHA-256 (live file equals R7 input bookend) |
| --- | --- |
| `RedPrism/Application.Tests.SelectionPressGesture.cpp` | `1CA1182D658BFBA8EAB49599B0FD65A9D38B1A01C07B19EE778A8B79AA5E5012` |
| `RedPrism/Application.Tests.SelectionPressShutdown.cpp` | `1089A715E1FDCEE68D91CABB2C6B55539F00C6E98962C2096D079ABFC5D78D2B` |
| `RedPrism/Application.cpp` (unchanged R6/R7) | `163AA4211E474E940119A82436DAFDDA9D4F029073EC5B7E075B6B6EA9B939C3` |

## Release R7 result and artifact identity

Actual `--self-test --warp`: PID 76536, exit 0, UTC interval above. Build PID 79704, ISA PID 9736 and runtime reader PID 96240 each exit 0. Profile is `/MD /O2 NDEBUG`, without ASan. Main records 145 Application stages, 340 exact compiler command records, 1698 hash-read archived files, source/dependency/product bookends, matching PE/PDB and zero ISA violations.

Logs contain 41 physical + 21 shutdown + 71 scalar + 8,272 inherited Release stdout assertions = 8,405 PASS records, plus all 7,613 inherited stderr PASS records; direct line counting found no FAIL record. Summaries are `application-selection-press checks=41 failures=0`, `application-selection-press-shutdown checks=21 failures=0`, and `scalar-selection-closure assertions=71 failures=0`.

Physical cases are early Up/click, moved Up, moved Lost, cancellation, admission refusal and overflow (511 accepted Moves, refused extra point, one batch presentation). Shutdown covers queued and active prefixes: exact receipt/history-stamp settlement, suppressed completion on joined Close, original controller retirement, independent observer charge retention and idempotent repeated shutdown. The 71 scalar and 8,272/7,613 Release records are inherited coverage.

R7 controller/evidence root: `C:/Users/eric/.codex/worktrees/p1-opacity-host-proof/RedPrism/.build/P1Drafts/S5PressHost-r7-Main`; runtime/build/ISA/reader receipts and streams are in its `actual-release-r1` subfolder. The executable/PDB are under `C:/Users/eric/.codex/worktrees/p1-opacity-host-proof/RedPrism/.build/P1Drafts/S5PressHost-r6-Main/Products/x64/Release`.

| Selected identity | SHA-256 or PE/PDB identity |
| --- | --- |
| Main R7 qualified receipt | `0099AA0C72E3C0B5911CE10265DB94682AA795BFBC7636A400440397D03D5478` |
| Runtime result JSON | `74ADEDC12A3F903AF8613381EC0C40AA137BA60A047D4012635B79DF0DC49F0C` |
| Runtime stdout | `7C1EA85761D1978AC36E9905CC2B102FC8B98DCA7E18E5861FA29C544CA13FBB` |
| Runtime stderr | `0931D2E62A0549FAC958D5EB08590D9198C1A4CA33DC70E26BD44868F031050A` |
| Actual compile-command JSON | `0D8936FD045C13DD071E4C4F4999533F548665F35C6140DB0F387028566189C8` |
| RedPrism.exe | `B3F2D9F0E7C2DADF8E1E18695470E39DF6647ABC9C5E7FBC06D7B48A9244C9DD` |
| RedPrism.pdb | `4B645208BC7FAF54A7D915DF35D85B24359BE8EB1B1AE7FF45F2566D38C20F59` |
| PE/PDB GUID / age | `421A96F1-4E83-41FA-AFF1-A5C5BDD17920` / `2` |

Local receipt, stream, command-JSON and source hashes were recalculated with PowerShell Get-FileHash -Algorithm SHA256; source hashes were compared with R7 before.json. EXE/PDB hashes, GUID and completion hashes are copied from Main's receipts. Archive: `Z:/CodexArtifacts/RedPrism-P1/2026-10-17/P1-SelectionPressHost-x64-Release-private-r7`; manifest SHA-256 `A203451CE72F97819AB6B20D0BE6A4AC58CE70C2995A79BE3F0FE2EE07D62E82`.

R7 retains `main-independent-artifacts-r2.log`: the initial independent reader refused the existing relocated DxUi header mapping. The r3 filesystem adapter recognizes only the exact existing DxUi physical root; source/dependency hashes and archive checks remain intact, with no repeated build or runtime. Main's receipt retains that refusal and cites the r3 report. Its limits remain explicit: object payloads are represented by exact index/product hashes, and some immutable inputs have live bookends without archived copies. Reader controls include 15 independent full-stream rejection cases, plus the source-only 8 shutdown, 8 physical and 12 scalar negatives; these are parser controls, not extra runtime assertions.

The opening P1 and SelectionWorkers checklists record the selected Release result and keep exact-source ASan, native integration, remaining branches, ordinary activation and G1 open. No actual-caller ASan outcome is inferred.

## Exact-source actual-caller real ASan

Main independently qualifies the same S7 caller source under real ASan. Fresh build PID 89792 exits 0 with
zero warnings/errors; all 340 exact project/TLOG compile records use `/fsanitize=address /MDd /Od /Zi` and
RequireAddressSanitizer.h. The unchanged pinned ASan dependencies, source/product bookends, matching PE/PDB
and zero-violation ISA report are verified. The fresh build archive contains 1,693 files; no compiler replays
are used during the subsequent runtime-only attempt.

Actual Application PID 22044 exits 0 between `2026-10-17T02:02:53.879553+00:00` and
`2026-10-17T02:09:00.266950+00:00`: 145 stages, all 41 physical, 21 queued/active shutdown, 71 scalar,
8,272 inherited stdout and 7,613 inherited stderr assertions. The source-defined reader requires every event
and summary in order. Its source-only controls pass 3 full-stream positives and 47 negatives, retaining the
12 scalar/follow, 8 physical and 8 shutdown predecessor controls. The one additional clipboard assertion and
286-check summary are source-version changes, not an ASan conditional; the adapter validates them explicitly
before comparing the original 8,271-record ASan predecessor. No golden or deadline changes.

The separately copied, already qualified 273-TU instrumented DocumentTests control has the same probe source
as S7. Actual PID 74268 exits 1 and reports `AddressSanitizer: heap-use-after-free`. Its six copied products,
source identity and referenced original qualification are hash verified. This control does not substitute
for the 340 instrumented Application compile records or the actual Application run. The runtime archive
contains 31 files and references the complete build archive.

| Identity | SHA-256 |
| --- | --- |
| Main ASan qualification | `19E650D8677FAE1F23874EB66D1C750994A8DA5DA9CF745D6CD1D82C436A8D9B` |
| Actual stdout | `C3B090363BC8E508B5E89854C42D5DBBBA70B08A40B4C5C3A930325D5F5826BA` |
| Actual stderr | `55440BC7606D3389D56BD62BA676C7F809B20E44973DD1FE133B4532355FA55F` |
| Build archive completion | `46471704AE35E911AF4BE645E288715444D8C8AC0842DF8EBC8FF81181515593` |
| Runtime archive completion | `F022DCEEB400CB1BABAC48F47000EFC448DFC07E91917F85E5EF30F92BCEF4BA` |
| RedPrism.exe | `04901D6AF429CF9871FE0C90818DAB32042B5DC5BE06F369FBCB713330BC771E` |
| RedPrism.pdb | `869D4AFF562FCBE96C7E99F3FCD6A936F990E9AB9EC081C344E220AAE5C4CED9` |

Receipts are under `.build/P1Drafts/S5PressHostAsan-r1-Main/` (`main-build-qualified.json` and
`main-qualified.json`). Physical storage is the exact task-specific
`D:/RedPrismBuildCaches/.build/p1-opacity-host-proof-selection-press-asan-r1` root, reached through its
`Storage` junction. Sources and pinned libraries remain at their frozen logical locations; the build/runtime
transport records this mapping and preserves every failure and prior receipt.

## Native adoption boundary

Main adopts 51 code/test/project paths from this qualified source plus the five scoped CPU contracts.
Native Directory.Build.props is unchanged; the private single-compiler property is not imported. Formatting
checks 1,269 files and removes one extra EOF blank line only from SelectionGestureAbort.h. All other adopted
code bytes match the S7 input bookend. The inventory and formatting receipt are under
`.build/P1Integration/SelectionCallerNativeAdoption-r1-Main/`. The native source now needs fresh Debug,
Release, real ASan, analysis and ARM64 cross-build qualification; the seven previously qualified profiles
remain evidence for the earlier native baseline. Ordinary input remains disabled for this new route.
The captured-settings API draft and remaining outside-hit/other branches are not included in this adoption.
These results do not close native integration, complete S5, measured G1 or P1 Done.
