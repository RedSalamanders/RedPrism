# P1 shared row lifetime — library and consumer qualification

## Scope

**Status: OPEN. Preserve the original 848-input Debug r195, Release r196 and real ASan r197 passes and analysis r198 failure unchanged. Debug build r210 passes on 854 inputs frozen at `211A82AD25D83813F97260D6FCEB546DEC61B34C733C36E6E29EF44F1E99DD43`. Focused runtime r211 passes retry 18/18, edit 17/17, provider 16/16 and capacity 12/12, then stalls in synchronous notification publication after two notification checks. The owned child dump/termination receipt records exit 5. Shape and full-matrix qualification remain open; production activation is false.**

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

- The initial private compile failure (missing `<objbase.h>`) and original analysis failure r198 remain archived unchanged. The r198 C28020 concerns the original candidate's unproved `transfer.slot < 64`; the fail-fast bound is in the repaired consumer source and does not alter that failed receipt.
- ASan r197 completed successfully for the original 848-input runtime source; its actual sanitizer diagnostic, full stress registry and archive receipt are recorded above. It does not qualify the repaired consumer.
- The repaired source integrates the original-charge wrapper and library pin. Incremental Debug build receipts r200/r202/r203 pass; runtime r201/r204 still fail the same interaction closeout. Full repaired-source matrix qualification is open.
- Deferred-release bytes remain a waiter-readiness hint, not an additional reservation subtracted by `MemoryAdmission`; report them separately rather than summing overlapping physical retirement and retained reservations.
- The compiled 10,000-row provider/MTA and 64-epoch exhaustion/retry fixtures still need passing runtime qualification, including delayed readers, same-ID replacement, post-close release, full names, locale/selection behavior and large-count owner/frame cost. Complete display/utility consumers and all other P1 exits remain open.

At the r204 checkpoint, the primary-reviewed adapter and actual-provider/10,000-row fixtures were compiled but had no passing runtime qualification. The existing [P1 checklist](../Plans/WIP/UiNeverBlocks_2026-09-22.md) and [H4 checklist](../Plans/WIP/BoundedRasterUploads_2026-10-05.md) retain their open requirements.

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
consumer has passing incremental build receipts but no passing runtime qualification. Application, Chrome and UIA snapshots share its original admission
until final graph destruction and owner acknowledgement. Tests now include full 10,000-row/offscreen names,
same-ID replacement, delayed fresh MTA reads after Application destruction, and actual 64-epoch retained-slot
exhaustion/refusal/explicit retry. The separately reviewed queued shape redraw host fixture is also integrated.
Formatting checks 730 files; all 15 repository skill validators pass before the new build. Production remains
disabled, and the complete consumer matrix, embedded notification/reentrancy, owner cost, H4/G1 and other P1
requirements remain open.

## Repaired consumer incremental receipts

These 853-input incremental attempts are separate from the original 848-input matrix above. Each actual failed
attempt was archived before documentation edits. `.build` `result.json` and `independently-verified-archive.json`
match the archive's `archive-completed.json` `Attempt`, counts and manifest hashes; the completion hashes below are
the independently verified archive completion SHA256 values.

| Attempt | Result | Exact finding | Freeze SHA256 | Archive completion SHA256 |
| --- | --- | --- | --- | --- |
| Debug build r199 | Exit 1 | Compile failure: `DocumentHistory::RedoCount` is absent; corrected the fixture to use `CanRedo`/`RedoName`. Archived before repair. | `96DAFFF8B80569E14DD081070DE5714B977693071E0FFC55FD89BC9F1A2B4AF6` | `9E63D140F0DA768A3F619747AFBE2B29F39B8CE6EFECF4F726F29FF5A3AE40A9` |
| Debug build r200 | Exit 0 | Incremental build passes after the API correction. | `BB06381E7483DE6116F7B0789CA0773EB63A15C1F5F26DDDE88F12B344BA00B5` | `DD4DD260CAB6C93A744ABD5FA64AB600E5DB4728458CBD4CB77C78369808DE25` |
| Debug runtime r201 | Exit 5 | 46 boundary controls pass; 17/18 interaction controls pass. Existing `closeout.settled-tree-retains-only-its-current-prepared-cache` fails. | `BB06381E7483DE6116F7B0789CA0773EB63A15C1F5F26DDDE88F12B344BA00B5` | `30A8B39CA3CE38E1DFAA1DD39B8C55D56F7D981981EE8D83F66F856883D0C42D` |
| Debug build r202 | Exit 0 | Incremental build passes after the second-settle repair. | `FD9C51AF4F147F171498B1DEBF725D940B83C0258663173DEC1B84CC9C2C200C` | `E1F402E4C15FD1DC0300E4DAB92A6784B2CFCB55CFCA38BF05D0A5CDAA9FCE9D` |
| Debug build r203 | Exit 0 | Incremental build passes with actual HWND geometry and DPI in the new provider fixture. | `C5972A563D3D117F7DA0135F87941C5EF69A9876CC293E10B21F343B79B08231` | `467B647912609C967F681BB534F8ADF038801657CDA532B6D55C79A9E62ACC83` |
| Debug runtime r204 | Exit 5 | Same closeout failure as r201; 46 boundary controls and 17/18 interaction controls pass. | `C5972A563D3D117F7DA0135F87941C5EF69A9876CC293E10B21F343B79B08231` | `9AE39F851909CDC398EAE6D6CFDC3E1FBE6283F041D5209D0FA69006B0B8D3D9` |

The 853-input rows above record r199–r204. Runtime r201/r204 each list the canvas shutdown, prepared-row boundary and prepared-row interaction stages; the interaction summary is 18 checks with one failure. Their matching logs show the actual hidden WARP host at DPI 144 and the real row/badge HWND geometry. The later focused Debug attempts r205–r208 are recorded below, including their actual build/runtime outcomes and archive completion receipts. At that r204 checkpoint, no consumer activation or P1 completion was claimed; provider/MTA/capacity/notification/shape runtime fixtures had not executed.

## Focused Debug r205–r208

These incremental attempts are focused receipts, not full qualification and not a replacement for the immutable original 848-input Debug/Release/real-ASan matrix. The r205/r206 source freeze has 853 inputs (`F02A28352C342EACFCC257E3BBD3A796978D8242BE5C380C231818882B286D5B`); r207/r208 use 854 inputs frozen at `4F84731FE890CE99275B94CB7A20A44BDD7370FA6B4B3AF775E4806535A2A4BF`; r210/r211 use 854 inputs frozen at `211A82AD25D83813F97260D6FCEB546DEC61B34C733C36E6E29EF44F1E99DD43`. Archive completion values below were read from each owned archive; each attempt’s `.build` result and independent archive receipt agree with the archive completion record.

| Attempt | Result and observed scope | Freeze SHA256 | Archive completion SHA256 |
| --- | --- | --- | --- |
| Debug build r205 | Exit 0; incremental build, 853 inputs. Not runtime qualification. | `F02A28352C342EACFCC257E3BBD3A796978D8242BE5C380C231818882B286D5B` | `3DE47DA1CAA5C54AD3EC9EFC2356EDB77271F895F67B3B7D89EF0A055ACDEE39` |
| Debug runtime r206 | Exit 5; 104 shutdown and 46 boundary checks pass; interaction closeout fails. Diagnostic: `bytes=944015`, `reserved=2832029`, `resident=2832029`, i.e. three retained cache epochs. | `F02A28352C342EACFCC257E3BBD3A796978D8242BE5C380C231818882B286D5B` | `A3D7C3595B89639D36BB6377C867A9E23E591B74E95ABB66F344E6D6576DCF52` |
| Debug build r207 | Exit 0; incremental build, 854 inputs, freeze `4F84731…`; library fix `36c0ff66bd7574038ed2e255f79847a81bf054c7` is at the exact pin. Not runtime qualification. | `4F84731FE890CE99275B94CB7A20A44BDD7370FA6B4B3AF775E4806535A2A4BF` | `957B0E137A7363101EA0EC49D81DF25EDB8319EBCB25FC8EC6009B84DD79F367` |
| Debug runtime r208 | Exit 5; 104 shutdown, 46 boundary and all 18 interaction checks pass. Interaction closeout reports a single `944015`-byte reservation (`reserved=944015`, `resident=944015`). Retry executes 18 checks and fails `only-current-renamed-rows-replace-the-borrow-and-original-charge`. Provider16, capacity12, notification11 and shape9 did not execute because the suite aborted after the retry failure. | `4F84731FE890CE99275B94CB7A20A44BDD7370FA6B4B3AF775E4806535A2A4BF` | `6EFFAD9BCD2AACFD4E9F1AF32DFFC2481E418B6DB312B84867F2CB368A0197BD` |

At r208, no later provider, retained-capacity, notification or shape check had executed. The r210/r211 follow-up and its bounded fixture outcomes are recorded next; the 848-input historical matrix and its preserved failures remain unchanged.
## Follow-up Debug r210–r211

Both attempts use 854 frozen inputs at `211A82AD25D83813F97260D6FCEB546DEC61B34C733C36E6E29EF44F1E99DD43`. They are focused incremental receipts, not the full consumer matrix.

| Attempt | Result and evidence | Freeze SHA256 | Archive completion SHA256 |
| --- | --- | --- | --- |
| Debug build r210 | Exit 0; incremental build completed `2026-10-06T06:44:42.709863Z`. Archive `p1-shared-row-consumer-x64-Debug-r210-build-owned` has 1,114 copies. | `211A82AD25D83813F97260D6FCEB546DEC61B34C733C36E6E29EF44F1E99DD43` | `B1D56195EFD46BDEB4B6EAFC744B369A0C47C3C5A072A49DF2C43A6ADB9F142D` |
| Debug runtime r211 | Exit 5; interval `2026-10-06T06:45:15.150352Z`–`2026-10-06T06:48:07.062099Z`; binary SHA256 `FF6B157D7EE78BFA4DA3FC52C6F8B59E557668A1E5F34B838D7584080CC0195C`. Retry 18/18, edit 17/17, provider 16/16 and capacity 12/12 passed. Notification reached two checks, then the real UIA notification call stalled during synchronous publication. Owned test PID 106680 was captured at `2026-10-06T06:48:06.973596Z`, dumped, and terminated with exit 5. Dump SHA256 `ACD28C307C16EA85B323A80B7037F2876E5B4D5F61B6B5E3B7B5CA9196961946`. Archive `p1-shared-row-consumer-x64-Debug-r211-notification-stall-owned` contains 1,116 copies. Shape was not reached. This failed UIA fixture is not successful notification, shape or full-matrix qualification. | `211A82AD25D83813F97260D6FCEB546DEC61B34C733C36E6E29EF44F1E99DD43` | `875C245D255E060B898747474F2D0A0925F0F5ED88B07811013D11C1306D9EDE` |

The owned stall artifacts are `.build/incremental-x64-runtime-Debug-r211/notification-stall.json` and `.build/incremental-x64-runtime-Debug-r211/notification-stall.dmp`; the actual run is `.build/incremental-x64-runtime-Debug-r211/result.json` and `stdout.log`. The derived `notification-stall-stacks-r3.txt` shows `LayerRowsStructureObserver::HandleStructureChangedEvent` on owner thread `0x13df4`, nested beneath `UiaRaiseStructureChangedEvent`, `RaiseEmbeddedAccessibilityChanges`, `EmbeddedHost::UpdateAccessibility` and `ChromeHost::Prepare`; its client read waits inside UIA. This proves inline owner delivery rather than a worker-scheduler deadlock. The debugger tool's earlier invalid-index/path attempts remain separate failed diagnostic attempts, never test passes.

The library delivery checkpoint is pushed as `cd645ea2375725ed7a899bb6e681ef41d0a3000d`; its full Debug and Release library suites pass. The consumer pin, skill and contract name that exact candidate, and build r212 subsequently passed (record below). The notification fixture now fails immediately if UIA invokes its agile callback on the publishing owner, avoiding another nested-read deadlock while preserving the same 11 state expectations. No successful current consumer notification or shape result is claimed. Full Debug/Release/ASan consumer qualification and remaining P1 runtime/latency gates remain open. P1 remains OPEN and prepared-row production activation remains false.

## Queued delivery consumer checkpoint r212-r213

These attempts use the exact DxUi pin `cd645ea2375725ed7a899bb6e681ef41d0a3000d` and 854 frozen inputs at `A4E632805BEFEFCF9690F8227C5ABC876E444FEFCFE3A6E1CA70E2599B4AE999`. Library full Debug/Release/real ASan and three ARM64 cross-builds pass; they do not replace consumer qualification.

| Attempt | Actual result | Archive completion SHA256 |
| --- | --- | --- |
| Debug build r212 | Child 112252; exit 0; 2026-10-06T07:04:39.172230Z to 2026-10-06T07:11:08.805079Z; 1,114 archive copies verify. | `795801C4E7464A87C50066921DC90D3B77D32E1AC3D27BBA8A4464772694EC67` |
| Debug runtime r213 | Child 76908; exit 5; 2026-10-06T07:19:23.113553Z to 2026-10-06T07:19:24.016181Z; binary `E9F0F655E4158C6FFC09A75C2BBDA88D1D52E076B659333A23E2A2814BFAE19D`; interaction 17/18 passes, failing the final quit-message closeout. Notification and shape are not reached. 1,114 archive copies verify. | `1728FA6F17508F09ED9CCED268B739DFA62B0BCC79F7CD93B5917898763A4308` |

The r213 failure remains unchanged. The new callback guard and queued delivery route are not qualified by this run. Production activation remains false; full consumer matrix, retained row identity across reorder, cost, complete H4 and P1 exit gates remain open.
