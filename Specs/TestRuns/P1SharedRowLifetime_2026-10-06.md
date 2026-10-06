# P1 shared row lifetime — library qualification and scheduler draft

## Scope

**Status: IN PROGRESS. The DxUi library step is qualified and pushed. The scheduler draft passes full Debug/Release and three ARM64 cross-builds; Release analysis r198 fails on a missing proved slot bound. ASan r197 is running. The consumer adapter remains a draft, the RedPrism pin remains unchanged and production prepared rows are disabled. P1 is not Done.**

Source ownership: library branch `codex/p1-prepared-tree-accessibility`, implementation commit `5f1ccdb6b5decde9f8ef6137e8b646dba51555c4`; checklist follow-up `5ba8378`. RedPrism scheduler source is the managed `codex/p1-opacity-host-proof` candidate after `900c644`, not the native checkout. Generated attempts/products and unqualified drafts stay under `.build/`; they are not committed.

## Qualified library step

The optional immutable source preserves complete offscreen Unicode names, ordered navigation and independent selection. Source identity marks the semantic epoch, including same-count/same-ID name replacements. Native structure notifications retain one active and one coalesced pending snapshot, allow real MTA clients to read and invoke root replacement, and preserve owner responsiveness under a held callback. The embedded root-only and retained-provider tests qualify lifetime and existing STA behavior; they do not qualify a slow real embedded event client or RedPrism adoption.

All 20 default library suites pass in x64 Debug, Release and real ASan Debug. The deliberate sanitizer probe detects heap-use-after-free. All ARM64 configurations cross-compile; no native ARM64 runtime claim. The three common paired Release scenes (`Default`, `MultilineGrid`, `MultilineGridDistinct`) report zero confirmed regressions without changing gates. Named desktop-capability skips remain recorded.

Library evidence is checked in under `Measurements/PreparedTreeAccessibility/2026-10-06/`: matrix, corrected Debug UTC receipt supplement, paired raw summary and README. The original checker failure and failed synchronous-notification probes remain unchanged. Validators, tooling and formatting pass. The compiled library commit is pushed and its branch is synchronized.

## Shared retained-memory draft

The new move-only lease uses the existing 64-slot retained pool and transfers the complete original successful callback reservation without allocation, admission or a second memory query. Its preallocated independent endpoint owns a duplicate wake event and fixed acknowledgement bits/generations. A final CPU graph release acknowledges only after destruction. The owner consumes acknowledgements; a held immutable graph stays charged without preventing useful-work idle. Endpoint references may outlive Close and scheduler/Application destruction. Existing owner-only ticket tests remain unchanged.

The 57 new named checks plus summary cover typed refusal, repeated/outside callbacks, failed/cancelled results, moves, self-assignment, overwritten leases, gated destructor/payload order, MTA release, shared/owner-only capacity, reuse, wakeup, two post-close readable graphs and separation from a newer scheduler. Full predecessor counters and registration/stage checks are preserved by strict r13. Its comparison explicitly requalifies the extended `Common/Jobs` bridge; it does not falsely claim that this changed bridge is byte-identical to the older private probe.

Frozen inputs: 848; manifest SHA256 `6373569CA194FDBAD52BFABCF289F9156B601C83D2F91670988A60F6AE3D66BB`. The only delta from the qualified 846-input r193 source is `Common/Jobs.h/.cpp`, the HostTests registration/project, and the two new shared-lease test files.

| Attempt | Actual child | Exit | UTC interval | Archive readback |
| --- | ---: | ---: | --- | --- |
| Debug r195 | 108264 | 0 | 2026-10-06T04:58:00.082453+00:00 to 2026-10-06T05:06:52.499093+00:00 | 1117 copies; `F2F72AFA3E7F87DDB52B5B60B93ADEEBD7F42D9644E57A4CD6B173578CAF7257` |
| Release r196 | 28388 | 0 | 2026-10-06T05:07:44.563673+00:00 to 2026-10-06T05:15:40.302050+00:00 | 1117 copies; `5B09A595D6C1E2D15D4123E639373EF478A0FBAE1CB3F89407D24FCD427A9FB4` |
| Release analysis r198 | 94148 | 1 | 2026-10-06T05:16:50.854384+00:00 to 2026-10-06T05:21:05.026684+00:00 | 1107 copies; `A2B4054FF55385D5F532EEA4F7EF9ED7D7F8F7CABC7D3AC686F792B742F1EC6F` |
| ARM64 Debug r26 (cross only) | 65264 | 0 | 2026-10-06T05:12:47.519885+00:00 to 2026-10-06T05:13:42.633238+00:00 | 919 copies; `57D0DC75DC1323BBC402A763DE88762C7F8A1A23CF5F6B7815096547F4AFE1CF` |
| ARM64 Release r26 (cross only) | 108580 | 0 | 2026-10-06T05:13:46.527267+00:00 to 2026-10-06T05:14:58.314760+00:00 | 919 copies; `03534A535856F99E1089FFAF089737F3C922D7B63379FF608F4C4E89CA23F3F9` |
| ARM64 ASan Debug r26 (cross only) | 35908 | 0 | 2026-10-06T05:15:03.194486+00:00 to 2026-10-06T05:15:53.120507+00:00 | 923 copies; `B8376B41C00DF365C2E3EE42FF7C0B2167AD02518946C016CF1E23B781EF92E2` |

Debug strict r13 passes: 7,654 host, 7,197 document, 1,694 composition and 9,960 application named controls /84 stages, unchanged ISA registries and predecessor counters. Release strict r13 also passes. Debug proof/helper copy receipt `DC214C72A3EC7DDF0B3C587EF381FA52F6B0809CC5DABD89A36096148FD448AF` leaves the original archive completion unchanged.

## Preserved failures and next work

- Initial private draft compile omitted `<objbase.h>`; exact failed source/log copies remain in `.build/shared-retained-compile-failure-r1`. The repaired draft HostTests run passes all 57 named lease controls; it is not the normal matrix qualification.
- Analysis r198 reports C28020 at `Common/Jobs.cpp:799`: analysis cannot infer `transfer.slot < 64` from successful owner-only transfer. Add an explicit fail-fast bound before indexing, preserve the warning policy, and requalify the repaired source. No current frozen runtime source is modified before child exit and archive readback.
- Finish and archive ASan r197, including the actual expected sanitizer diagnostic and complete stress registry. Its result qualifies the original draft only.
- Integrate the original-charge wrapper and the library pin; no saved scheduler/Application pointer may occur in the reader graph. The actual provider/cache must share one epoch and final visible-owner release must queue the whole graph.
- Deferred-release bytes are a waiter-readiness hint, not an additional reservation subtracted by `MemoryAdmission`; report them separately rather than summing overlapping physical retirement and retained reservations.
- Qualify real embedded providers, delayed foreign readers, same-ID replacement, post-close release, exhaustion/retry, full names, locale/selection behavior and large-count owner/frame cost; then finish complete display/utility consumers and all other P1 exits.

Primary-reviewed adapter and actual-provider/10,000-row test drafts are preserved under the managed `.build/prepared-layer-rows-draft-r1/`; they are not compiled or activated. The existing [P1 checklist](../Plans/WIP/UiNeverBlocks_2026-09-22.md) and [H4 checklist](../Plans/WIP/BoundedRasterUploads_2026-10-05.md) retain their open requirements.

## Follow-up: ASan finished; repaired consumer source integrated

The earlier scope and table above describe the original 848-input source before repair. ASan r197 subsequently
finished with exit 0, actual child 96756, from `2026-10-06T05:16:50.854528+00:00` to
`2026-10-06T05:31:57.123490+00:00`. All 1,124 archived copies read back identically; original completion SHA256
`FC6A0F5B2EA75028BCA1D137D0B243CFC9A35B89BD4AAF9A3138C8B20DCA96F9`.
Strict r13 passes every predecessor and all 8,052 worker-stress controls. The actual expected sanitizer
heap-use-after-free diagnostic is preserved with SHA256
`0817B5208ACB5C7F8CEEDAEAE8F4649D9187F64D63301426A975482376BA1B67`;
its supplement receipt is `00A3F841F7C2175B35F5043A8780543EDB83EB2A1722DBA737138244130F78FD`.
This result qualifies the original runtime source; analysis r198 remains failed.

After actual child exit and archive readback, the primary added the explicit fail-fast slot bound, the full-graph
`PreparedLayerRows` consumer, and the qualified library pin `5f1ccdb6b5decde9f8ef6137e8b646dba51555c4`. The new
consumer is a source draft, not a passed run. Application, Chrome and UIA snapshots share its original admission
until final graph destruction and owner acknowledgement. Tests now include full 10,000-row/offscreen names,
same-ID replacement, delayed fresh MTA reads after Application destruction, and actual 64-epoch retained-slot
exhaustion/refusal/explicit retry. The separately reviewed queued shape redraw host fixture is also integrated.
Formatting checks 730 files; all 15 repository skill validators pass before the new build. Production remains
disabled, and the complete consumer matrix, embedded notification/reentrancy, owner cost, H4/G1 and other P1
requirements remain open.
