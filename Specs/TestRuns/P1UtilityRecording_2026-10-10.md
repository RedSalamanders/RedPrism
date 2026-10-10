# P1 private utility recording qualification

## Current checklist

- [x] Integrate the private composition/region graph and original-stream production epoch.
- [x] Compile the first repaired source in Debug r753 and run the complete composition suite r754.
- [x] Preserve the original failed r751 build before repairing its fixture API calls.
- [x] Qualify the strengthened invalid-region and cancelled-mapping cases in Debug r755/r756.
- [x] Integrate and qualify checked admission arithmetic with independent numeric expectations in Debug r757/r758.
- [ ] Migrate the real utility callers under complete original-record admission and retirement.
- [ ] Qualify device replacement, every acquired prefix, hidden completion and joined shutdown.
- [x] Complete and authenticate full Debug r759 on the current 1,144-input source.
- [x] Complete and authenticate full Release r760 on the same current source.
- [x] Complete and authenticate real ASan r761 and Release analysis r762 on the same 1,144-input source.
- [x] Complete and authenticate the ARM64 r90 Debug/Release/real ASan cross-build matrix; native ARM64 runtime remains unavailable.
- [ ] Run real-caller/cache-miss cost measurements and close G1.

This record qualifies components only. The utility graph and admission controls do not establish real-caller integration or full production admission/retirement. Ordinary activation=false, G1=false and P1 Done=false; main integration remains open.
DxUi remains at `8e8994079e82f79e6765e6a885c03fc687957591` (API3).

## Original build failure

Original Debug r751 child PID86316 exits1, 13:15:42.377576–13:16:38.207985 UTC, on 1,140 inputs,
freeze `A43EC198357352DC77CF90BB8CF4845F20450C434DD27B3FB0E270652F17B077`.
The new fixture omitted Raster::Make's byte-count argument and attempted push_back on an immutable
RecordRoot. Main repairs those calls using explicit byte counts and Builder::Append/Seal. The literal
pixel expectations are unchanged. No runtime was launched for that failed source.

Preserved archive:
`D:\RedPrismQualificationArchives\P1-utility-core-1140-Debug751-build-failed-2026-10-10`.
Source/evidence/completion SHA256:
`6E444FEC852949CA7A12A9BA90AC4C3B271AD1BA80701424ED99B3F00233E69A` /
`E7B725598D42FC13037EAE93A6E8A1D506125C392FB7B55D7BA99E0F5DDFCC7B` /
`24637FE4638E4AF99EBB8EAAB9084D6F819BEC772EF5DF3F977C81B7ABA64097`.

## Repaired initial component

Debug r753 child PID19080 exits0, 13:19:07.771084–13:24:46.696143 UTC. Composition runtime r754
child PID72724 exits0, 13:25:08.433762–13:25:18.967697 UTC, on the same 1,140-input freeze
`990CDE66837CBE35B0A5C34E0A7778ED09C4BE033D5BF042263CB7C4E19107D6`.
The unchanged runtime executable SHA256 is
`3F340CC6ADBDE58A65F39AB4421E7A04144871F4C86A235A39122C0FC7CBB791`.

All 41 original-stream restart checks and 46 new region checks pass. The latter execute WARP and
available hardware: three actual mapped regions compare the independent premultiplied sparse pixel
truth table; each stage retains the original ticket and whole fixture charge; command ordinals continue
across successful joined restarts; repeat raster upload bytes remain zero; graph/source/context retirement
occurs on workers before refund. The complete composition executable also passes. Full configuration,
original caller, complete admission, fault/shutdown and performance qualification are separate.

Both archives independently authenticate every frozen source and copied product/evidence file:

| Attempt | Archive | Completion SHA256 |
| --- | --- | --- |
| Debug build r753 | `P1-utility-core-1140-Debug753-build-qualified-2026-10-10` | `952527D99F77B1ECBA319E9C31FDC7E5EFC3ECE99AE3C0B60FC2DB7ED6682287` |
| Composition r754 | `P1-utility-core-1140-Composition754-qualified-2026-10-10` | `1633CD152471D3B86B61BF7B6B0F5FF882589CA44B629C9D82E73822E195063A` |

## Strengthened candidate

The next fixture adds unpublished-region borrow refusal, missing-context refusal, post-exclusion borrow
refusal, an invalid region after actual source capture, and cancellation after an actual mapped-row callback.
The cancelled stage must skip its useful copy body, acknowledge that exact row loan, exclude future borrows,
and retire its original graph/source on a worker before refunding the original reservation. WARP is mandatory;
hardware creation may skip only reported DXGI_ERROR_UNSUPPORTED or DXGI_ERROR_NOT_FOUND.

Original Debug r755 child PID67296 exits0, 13:28:31.019519–13:33:57.984227 UTC. Complete Composition
r756 child PID34156 exits0, 13:34:23.935168–13:34:34.351574 UTC. Both use 1,140 inputs and freeze
`38C7109B572F0C82D55C04E8CF3F1B6345A31FE03BDF807FE88105397617A85F`.
All 41 stream and 104 region controls pass. The unchanged runtime executable SHA256 is
`484F73D225E5036451E674C4AC7F5F53F23340B04F5CA4E1EF88C35C35289D0C`.
Archives `P1-utility-faults-1140-Debug755-build-qualified-2026-10-10` and
`P1-utility-faults-1140-Composition756-qualified-2026-10-10` preserve this distinct source;
r753/r754 remain historical proof.

The cheaper-agent admission test draft supplies dense/sparse construction and input refusal cases. Main
preserves the original draft and adds a non-owning arithmetic descriptor and independently enumerated
numeric totals. ABI-dependent document metadata is checked against the separate construction caches;
the pure descriptor fixtures do not stand in for real document or adapter admission.

## Current admission candidate and complete Debug qualification

The main-reviewed admission tests add four independent literal envelope totals plus checked overflow,
source-owner counts, dense/sparse construction-cache checks and input refusals. The pure estimate does not
establish actual driver allocations, adapter admission or real utility-caller lifecycle. Original source is
1,144 inputs, freeze `DAB609D1CD0055E11CAB62A04749AAAE0CE62EBE776117D0F8762688DB6D5C4B`.

| Attempt | Actual child and scope | Exit |
| --- | --- | --- |
| Debug build r757 | PID45168, 13:36:43.951405–13:42:16.015205 UTC; incremental complete native build | 0 |
| Composition r758 | PID44772, 13:43:36.041432–13:43:46.436654 UTC; complete executable, unchanged SHA256 `0C4A402FE56E32DB5A733045F0318E6C298256BB10F93A52C00D7FFA72A2CDC3` | 0 |
| Full Debug r759 | PID76848, 13:45:43.731456–13:56:30.520406 UTC; unfiltered `test.ps1`, all 124 application stages, actual direct/prepared visible smokes and screenshots | 0 |

Main independently authenticates the r758/r759 archives, all source/product/evidence copies and original
child scope. Composition's 6,140 records preserve all 5,973 predecessor records with exactly 20 policy,
104 region and 41 admission assertions plus two summaries. Host8,786, Document8,782, Io1,825 and complete
Application stdout/stderr assertion inventories remain unchanged. Prepared smoke routing retains the
already-qualified async Stroke/Warp1 and ordinaryActivation0; direct remains0. Both original archives
require actual presentation samples. These smokes establish functionality, not the 45 MP G1 thresholds.

Two existing INFO reservation diagnostics increase by16 bytes. The unchanged admission uses `sizeof(Root)`.
Archived v145 PDB source-matching member layouts prove Storage5,264→5,280: stream grows16 bytes,
inline recording policy grows8, and16-byte aggregate padding absorbs8. Only those two INFO fields are
normalized for predecessor comparison; every assertion stays literal. Five malformed-diagnostic models
(wrong delta/publication, missing/duplicate INFO, outstanding release) refuse. Preserve the initial strict
comparison refusal and the obsolete predecessor-smoke-route reader refusal before their narrow repairs.
The cheaper-agent 14-method pure inventory reader tests independently pass; they are reader-component proof.

| Attempt | Immutable archive | Completion SHA256 |
| --- | --- | --- |
| Debug r757 | `P1-utility-admission-1144-Debug757-build-qualified-2026-10-10` | `80CF83199388252D434FBA45583B6573BA51BB33EB7D7B87580753361D69BD41` |
| Composition r758 | `P1-utility-admission-1144-Composition758-qualified-2026-10-10` | `BA250506D0482C79847B07157CFBD8D518E5754066150001B5CC8070DE04CF5A` |
| Full Debug r759 | `P1-utility-admission-1144-Debug759-qualified-2026-10-10` | `941C80005C6F2DF12D7EFC0D9DF8DBAF7B60427BAE2591A65A1412533039482E` |

## Authenticated current-source x64 matrix

Read-only actual-attempt profiles and archive verification reports are under
`.build/P1Drafts/UtilityQualification-r1-Main`: `r759-utility-profile.json`,
`r760-utility-profile.json`, `r761-utility-profile.json`, and `r762-archive-check.json`.
Every attempt binds the same 1,144 source inputs and freeze `DAB609D1CD0055E11CAB62A04749AAAE0CE62EBE776117D0F8762688DB6D5C4B`.
The archived receipts record actual x64 child exits, stdout/stderr hashes and no automatic retry.

| Attempt | Actual child | Preserved inventory | Archive completion SHA256 |
| --- | --- | --- | --- |
| Full Debug r759 | PID76848, exit0, 15:45:43.731456–15:56:30.520406 +02:00 | 1,144 inputs, 381 evidence files, 124 stages | `941C80005C6F2DF12D7EFC0D9DF8DBAF7B60427BAE2591A65A1412533039482E` |
| Full Release r760 | PID58528, exit0, 15:57:57.458499–16:07:38.038294 +02:00 | 1,144 inputs, 363 evidence files, 124 stages | `1165D4F3646EB58BD9377301DF8D5601C3B2BCF4DDAEC96A8DFA8B8DFE7071E0` |
| Real ASan r761 | PID66780, exit0, 16:08:30–16:24:39 +02:00 | 1,144 inputs, 355 evidence files, 124 stages; ASan Debug profile | `AF3DD2F3EAA933C8E1D4BE7EC14E87A54A6A6F92221E47A7A1E34F1C78459C73` |
| Release analysis r762 | PID47936, exit0, 16:26:12.171045–16:35:23.283180 +02:00 | 1,144 inputs, 377 evidence files; zero runtime stages by analysis scope | `BB230D9CCDCC3AEAB72F1E4B764A927993D247417711DC6F311FDE4D7A12367A` |

All four immutable archives are under `D:\RedPrismQualificationArchives\P1-utility-admission-1144-{Debug759|Release760|ASan761|Analysis762}-qualified-2026-10-10`. The source manifests are independently bound in each archive; r762's profile reports `sources_verified=1144`, `evidence_verified=377`, and PASS. Full Debug/Release/ASan preserve the same 6,140 composition records (5,973 predecessor records, 20 new policy controls, 41 policy controls, 104 region controls and 41 admission controls), plus all predecessor Host (8,786), Document (8,782) and Io (1,825) records. ASan Host inventory is 8,787. Debug/Release/ASan each retain all 124 Application stages and actual smoke receipts; the direct routes remain `async_tools=0`, the prepared route `async_tools=1`. The prior baseline's original assertions remain intact.

The previous 13 EXE/PDB/runtime DLL Release products remain preserved under `.build/qualified-utility-Release-r760` and matched to the archived qualified product before r762 analysis. The utility graph and pure admission estimate remain component evidence: no driver-allocation, real-caller lifecycle, complete production admission or cache-miss cost claim follows. The three ignored drafts `.build/P1Drafts/UtilityCommandFailure-r1-Main`, `UtilityOwnerService-r1-Main`, and `UtilityCurrentContext-r1-Main` remain UNCOMPILED and unintegrated; they have no native proof. ARM64 r90 Debug/Release/real ASan cross-builds pass; main independently reads the actual source/child/archive receipts and all 15 EXE/PDB pairs, including five compiled sanitizer controls. Native ARM64 runtime remains unavailable. Native inputs and pinned owning contracts stayed unchanged through the matrix.

## Next real-parent integration

Move capture/preprocessing, private recording, original command acknowledgements, row copying, detached
processing and final retirement through the same admitted record. Keep the graph's source/residency across
serial regions and1×1 sampler inputs. Queue callbacks only mark/notify; the stable owner pump detaches and
resumes outside callbacks. A stopped recorder or requested cancellation is not GPU completion.

The command pump currently cancels the original scheduler ticket on every source failure. That permanently
cancels the original token, so a device restart cannot resume useful work on that record. The utility parent
needs an explicit, narrowly scoped failure disposition: stop original recording and retain the ticket while
joining/acknowledging/retiring the old graph; default cancellation remains unchanged for current consumers.
Real user cancellation always prevents restart. Prove this disposition and every acquired prefix before using
it for bounded device recovery. No new ticket, reset token or fabricated acknowledgement is permitted.

## Continuing implementation drafts while inputs are frozen

Main drafts the utility-only original-token failure disposition with three actual-pump test scenarios,
without changing the default behavior of current consumers. The failed source remains Failed; a live
original token supplies no successful restart or pixel authority. Real user cancellation still skips Run;
the original deferred retirement hook releases transport after actual acknowledgments.

Main also drafts the fixed original-ticket owner service and retained owner-only GPU-charge slots. Review
confirms actual JobScheduler::Close ordering: all compute/recorder producers join; exact retirement hooks
bind original accounting; wrappers detach; owner cleanup; final worker cleanup; only then wake resets.
The service refuses final charge release until that wake is gone after Close returns. Prefix/capacity/wake,
real-parent caller and joined-shutdown proof remain required. Private code is formatted but not integrated,
compiled or executed: `.build/P1Drafts/UtilityCommandFailure-r1-Main` and `UtilityOwnerService-r1-Main`.

Cheaper-agent ARM64 r90 helper copies bind all1,144 actual source names/bytes. Main preserves the original
copies and corrects their old profile schema/5,973 composition-count assumption to the current6,140-record
utility report with original124-stage receipts and allthree real smoke routes. Twelve pure model tests pass,
including incomplete source/profile/mode/control/activation/analysis refusals. These are reader tests only;
the ARM64 launcher still requires actual current Debug/Release/ASan/analysis reports and has not launched.

## ARM64 cross-build closeout

All three actual r90 children exit0 on the identical DAB609/1,144-input source and unchanged DxUi pin.
Main independently verifies each original child receipt, immutable source/evidence manifests, actual ARM64
PE machine headers and all five EXE/PDB pairs per configuration. The five ASan products contain actual
sanitizer linkage and each has its compiled instrumentation control; this is cross-build evidence only.
No native ARM64 runtime or performance claim is made.

| Configuration | Actual child interval (UTC) | Completion SHA256 |
| --- | --- | --- |
| Debug | PID56160, 2026-10-10T14:40:41.598549+00:00–2026-10-10T14:41:53.916246+00:00 | `1D0D96DDD4D61CD1C180AD6100A7B37DFC6F07A01D98082D50000A24983CDB64` |
| Release | PID79388, 2026-10-10T14:42:46.189075+00:00–2026-10-10T14:44:20.567461+00:00 | `6901A911F3E4BB7D9DF790D01F54BA4EBCECB2CF6995E8E90607B5DE1ADB8E07` |
| ASan Debug | PID74992, 2026-10-10T14:45:06.812852+00:00–2026-10-10T14:46:06.285863+00:00 | `04535933868D0467BFF346F2C41242C15E07B359B4B9782F8F9BC6A4791E5802` |

The main read-back receipt is `.build/P1Drafts/ArmUtility1144-r90-Main/Main-cross-matrix-joined.json`;
original archives are `D:\RedPrismQualificationArchives\P1-utility-recording-1144-ARM64-{Debug|Release|ASan}-r90-qualified-2026-10-10`.
The original reader's wrong archive-path key refusal is preserved before its narrow repair; the native
builds were not replayed. Full x64 Debug/Release/real ASan and Release analysis remain independently
qualified components. The real utility parent, caller migration, production admission, activation and G1
remain open. The user confirms the display is now on/unlocked; fresh actual presentation runs are due.
