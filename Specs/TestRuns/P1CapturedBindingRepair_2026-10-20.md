# P1 captured immediate-context binding repair — 2026-10-20

Status: latest-source local matrix qualified; repaired native ARM64 runtime and parent P1 gates remain open.
P1 remains WIP.

## Preserved failure

The preceding six-file Prepare attribution / cancellation fixture source is frozen as
`A87E6837F93157FDE79B89C60EAA85F283D110E6D4603380CB94EB35DC9824F6`
(1,888 inputs; HEAD `715dc1c`). Full x64 Release `ca1r1` passes. Canonical real x64 ASan
`ca1a1`, parent 98776, exits 1 at 2026-10-20T15:40:50.641686Z. Its core suites, genuine
ASan detection, stress and Application self-test pass, but `--bench ui-size-prepared-smoke`
exits 2173 (`0x87D`). Debug, ARM64 cross-build and analysis successors were not launched.
This is a failed ASan run, not a complete runtime qualification.

Main verifies all 3,348 failed archive files; manifest SHA-256:
`BB7C6100DF96971F4419135483E57ADBAA6B31710B98B303F2B4D0B0E5A2F1DE`.
The harness dump and pending marker were separately copied and byte-verified because
the archive excludes compiler TEMP. The 20,190,309-byte dump SHA-256 is
`F66976DB333A7387DC31DE7F478C297C6562D89262A05D8D569B695F7615D28A`.

The exception information contains the D3D11 diagnostic: `ID3D11DeviceContext::GetType`
was executing on two threads using the same context (103592 and 101908). Offline DbgEng
with the unchanged local ASan symbols resolves the raising worker stack through
`PreparedCommittedDisplay::PrepareForWork` → `OriginalDisplayPublication::PrepareForWork`
→ `Application::FinalPublicationJob::Run` → `Application::SizingJob::Run`.
The saved owner stack is in `Application::WaitUntilMessage` after the overlapping call.
The diagnostic establishes the concurrent context access; that later owner sample does
not identify which earlier owner context call overlapped.

Local evidence is under `.build/P1Evidence/p1-chrome-arm-repair-native-r1/`, including
`Archives/ca1a1`, `ca1a1-crash/manifest.json`, `dump-inspection.json` and `dbgeng-stacks.txt`.
The earlier run and original smoke output are preserved without modification.

## Repair and regression contract

The owner already checks the immediate context's type and device in `GpuWorkQueue::Bind`.
Its immutable completion capture retains the exact owner thread, device, context, origin
and generation. `GpuCompletionContext::MatchesBindingForWork` compares those retained
identities without invoking COM or touching the mutable queue. Worker final-display
preparation uses this check before acquiring a graph instead of `GetType` / `GetDevice`.
A match establishes neither current queue authority nor actual GPU completion.

Ten new `captured-completion.binding.*` controls check the actual worker's exact match,
zero/foreign owner, null/foreign device and context, foreign pair, empty capture and
the original binding's persistence after owner rebind. Existing actual publication,
pixel, original-ticket, cancellation, failure and retirement expectations remain intact.
No timing threshold, golden, ISA exception or dependency pin changes.

## Qualification checklist

- [x] Preserve failed attempt, dump bytes and resolve its actual caller.
- [x] Adopt the ownership repair, independent binding controls and owning Core/Rendering contracts.
- [x] Qualify latest-source real x64 ASan, including the originally failing Size smoke.
- [x] Qualify latest-source complete x64 Debug/Release, analysis and all three ARM64 cross-builds.
- [ ] Run the repaired source on the hosted native ARM64 worker; retain the earlier fixture failure separately.
- [ ] Commit/push this qualified step and verify its remote SHA.

Ordinary route activation, measured G1 and the parent plan's other remaining areas are
separate delivery obligations. These controls and a successful repeat cannot qualify them.

## Latest-source local matrix

All seven canonical attempts use the same 1,022-TU source freeze
`B21F8E445AB6CF491D79A0AC74F7DE97BE078F7D9A47866F81DFA017C7606E39`.
Main checks source bookends, original same-profile predecessor rosters, all object/link records,
five matching PE/full-PDB pairs per profile, linked ISA controls, actual ASan detection/stress,
41 unchanged cancellation facts, 10 new binding facts and 12,251 Application facts in 155 stages.
Strict SAL analysis preserves its positive and negative controls. ARM64 rows are cross-builds only.

| Attempt | Profile | Actual parent / exit | Archive manifest SHA-256 |
| --- | --- | --- | --- |
| cb1a1 | x64 ASan Debug test | 106104 / 0 | `B71BFB303A4EDF3518B610722C49B792BD2F600D8DCAE2B899B7417D1F599222` |
| cb1r1 | x64 Release test | 95228 / 0 | `84AC9CC8D977DE9B64B5EF26B9B712686AF9642EC3194EC253114F89EC2212CC` |
| cb1d1 | x64 Debug test | 64652 / 0 | `1C0A939065F506A9ACD03EE2DBE9A867C059B93355B1F67A11912578A1516C3E` |
| cb1armd1 | ARM64 Debug build | 108104 / 0 | `D453E989A0717F527D248B9195B7981A31DB2A3776F8BC7723146F3E0644E8E0` |
| cb1armr1 | ARM64 Release build | 80940 / 0 | `728B8A9152F1112E878BED36EDB30A6C4F40C3EC3994F365AFEF0AA9213D27FC` |
| cb1arma1 | ARM64 ASan Debug build | 27812 / 0 | `87110859F46BAC3A271BB194997445AE420B8D91714C6671C5AAA8C55D642ED5` |
| cb1an1 | x64 Release analysis | 57456 / 0 | `7B4D837CFA1FDFF0AEEE0669231DC134529E0A34FDAEB98E2EB6047E015FAA04` |

The failed ca1a1 attempt and dump above remain a separate result. No predecessor assertion,
timing threshold, golden, dependency pin or ISA exception was relaxed. The default-off
Prepare attribution and publish-before-event cancellation fixture repair are included in
this same qualified source. Their earlier measured cost misses and failed native-hosted
attempt remain retained; this table does not establish a performance fix or native ARM64 pass.

Evidence root: `.build/P1Evidence/p1-captured-binding-native-r1` with per-attempt source,
product/archive/profile/runtime records, strict analysis and `native-matrix-completed.json`.

Existing clipboard/file functional controls are also independently reconciled in each current full x64 profile:
11 exact receipt/origin controls, 56 real-contention records, 45 delayed-render facts, 48 clipboard-session-end
facts and 770 file-session-end facts. Their archived bytes and original expectations are unchanged. This closes
no complete phase/admission, performance or historical-save-cause obligation and requires no duplicate test or
implementation. Local reconciliation: `.build/P1Drafts/P1FileCurrentMatrixMap-Main-r1.json`.
