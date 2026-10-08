# P1 original stroke publication

Status: implementation and qualification in progress; P1 remains WIP
Date: 2026-10-08
Candidate: managed `codex/p1-opacity-host-proof`, parent `077354851beb844e0afcd7ce290efdadf81b8fcc`
DxUi: unchanged `8e8994079e82f79e6765e6a885c03fc687957591`, API revision 3

## Scope

The original final-stroke job owns final coverage, typed readback conversion, healing, sparse document/history
preparation, prepared rows and private display recording under the admitted gesture's original ordinal and one
scheduler record. Successful publication preflights all roots before the nonreentrant document/history, row and
display swaps. Failure/cancellation and shutdown retain original GPU/context and CPU owners through their actual
acknowledgements. Ordinary startup is disabled pending the complete sample/preview path and its qualification.

Reviewed isolated publication probes are foundations, not application qualification. Their prototype document
swap uses a test editor and cannot prove real history/row publication. The canonical application registers real
hidden-WARP final-stroke scenarios in `Application.Tests.StrokeFinish.cpp`; only reached successful controls in
the exact candidate's receipt are runtime evidence.

## Preserved 993-input candidate

Source freeze: `F0629D816B67BF6FE7A1EEDE6E1503B4F46435E3EC2158A366D39CA66E15B190`.

| Attempt | Actual result | Scope |
| --- | --- | --- |
| x64 Debug build r363 | Exit 0; PID 109472 | Compiled all products and passed all five linked ISA checks and gate controls. No application runtime claim. |
| x64 Debug application r364 | Exit 5; PID 101452 | Reached 10 application stages; failed one existing accessibility lifetime assertion before final-stroke tests. |
| x64 Debug build r365 | Exit 0; PID 100620; 993 inputs; freeze `B18C15A636FD170332CA67561A0358546ADC184E07C46D5864AED7C0BBD38EA8`; 04:06:46.497434–04:11:44.478578 +02:00 | Incremental-focus build only. `stdout.log` ends with the ready executable and verified DxUi pin; no runtime or full-profile claim. |
| x64 Debug application r366 | Exit `-536870911`; PID 69588; same 993-input freeze; executable SHA-256 `3516C6552FC5AC444FE7708E4F499A5295D72006FE3016C5DB864962DB04CC35`; 04:12:34.570154–04:13:51.396572 +02:00 | Reached 16 of the 105 registered application stages. The frozen run logged 18 of its then 19 final-stroke checks, without a summary, then exited abnormally during shutdown. The shutdown crash remains unresolved. |

r363 recorder bounds: 01:55:08.580668–02:02:16.638303 UTC. r364 recorder bounds:
02:03:24.277422–02:03:26.138310 UTC. These are recorder bounds, not independently captured kernel lifetime intervals
or G1 timings. The runtime executable stayed unchanged: SHA-256
`B90041BC4FD0B1EDA7F4BBE08B53F31531876C8022F56139E55125056585067F`.

Verified copy archives:

- `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug363-build-2026-10-08`: 1,291 copies;
  source manifest `FA3879922FFC77CF8B9EBF27C1B10E4FB3DE9A84B50C5ACED6E921138BF28A34`;
  evidence manifest `E3A03DE2E14BF53E96306AADFF80AD6C7F186AB88FF97708FB03B2C221132A09`;
  completion `E8738D1FECDF3A186EF9E6008095435D8378EAB0EFA596DF4B3D06D7530BAA4C`.
- `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug364-accessibility-failed-2026-10-08`: 1,291 copies;
  source manifest `B430293473B895E4930B1D6770F69438C11F9C0AFE502586725D12BB7D6CEBFA`;
  evidence manifest `6275D70FA51AE0F44D7332D00F39637477D5D2AA8B62267EF560980DD68B3567`;
  completion `874F234B8D51A7C0FF448118D03164D976F1483E2A46D7206811F3F643B9AEF4`.

## Failure and correction

The accessibility test expected refund after the final strong provider released, while its `oldWitness` weak
pointer still owned the allocated control block. Complete accounting now intentionally retains the original
admission through both row-data destruction and final weak-control-block destruction. The corrected fixture
first asserts that the expired weak witness keeps the full original charge, then releases the witness and runs
the existing exact-refund assertion. This changes the state-machine fixture to the explicit resource contract;
no legacy numeric golden or production pixel math is changed.

Review also corrected the new final-stroke fixture's tip from 2,000 pixels to four, and its held-draft assertion
to the exclusive finishing opening (the live session brush is deliberately null during worker ownership).
Independent literal GPU checks cover opaque red paint and an opaque-white source under a half-tone mask.
A post-preparation `E_OUTOFMEMORY` case checks the exact receipt, unchanged pixels/history and actual worker
retirement. Those controls compiled in r365 and the 18 checks before shutdown passed in r366; the complete stage
still failed and requires a fresh successful run.

Follow-on evidence for freeze `B18C15A636FD170332CA67561A0358546ADC184E07C46D5864AED7C0BBD38EA8`:

- r365: `.build/incremental-x64-build-Debug-r365/result.json` records x64 Debug build exit 0 over 993 inputs;
  stdout lines 8016–8019 show the ready executable and verified DxUi pin. The independently verified archive
  `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug365-build-2026-10-08` contains 1,291 copies:
  993 source inputs and 298 evidence files; source manifest `3D4DAAD42E0D39B5E106076CA658733E87DFB4B0EA1C881FD9065EAAB5A67283`,
  evidence manifest `0F00FF5E2494D8EBF20EE35F0E4E2A9550F049231224D49A828F1F21D53098F9`, completion
  `8DDBED76F8694342D34268F4887DCE7CF26BBA5AF3CB7927CA9E5DD539AA9689`.
- r366: `.build/incremental-x64-runtime-Debug-r366/result.json` records exit `-536870911` after 16 stages;
  stdout lines 1334–1352 show the final-stroke stage start and 18 passing checks, with no summary. The
  independently verified archive `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug366-shutdown-crash-2026-10-08`
  contains 1,291 copies: 993 source inputs and 298 evidence files; source manifest `65EB5DEE8D914E513BF208F4633A0925F962C370B8F671E1872C671007E25DDF`,
  evidence manifest `7C73646EE5DAAE1641B86D4999EB6E085E031F942015A20B87CA7284627A1FA0`, completion
  `C9C1D0A798A8AA0CFE12F66347F2D0E39297F6FBB7EEC3E1D41CB4BF53C42472`.

Current source inventory is seven final-stroke scenarios and 20 checks. Since r366, wrapper shutdown ordering,
the expected `Cancelled`/`E_ABORT` shutdown receipt, and one WM_QUIT isolation check have been repaired or added;
these changes are in build r367 and await runtime proof. The r365 build is compile evidence only, and r366 remains
a failed historical runtime attempt. Neither establishes a full profile, G1, or P1 completion.

The preserved dump (`original-69588.dmp`, SHA-256 `4319EF0B581ADC37E0E06F1B5EEF2A700396C15C629C21B355CCE3774AF93602`)
and matching executable/PDB are bound in `.build/P1Drafts/StrokeFinishShutdownCrash-r1/diagnostic-provenance.json`.
Symbolized raw stack candidates identify `StrokeFinishJob` destruction at its explicit shutdown guard; these
candidates are not an unwound call stack. Source inspection confirms `JobScheduler::Close` destroys job wrappers
after producers join and before its application owner hook. The repair performs the original parent's joined
shutdown detachment in wrapper destruction, before releasing its seed, slot and sources. r368 establishes the
successful shutdown runtime on that exact repaired source.

## Shutdown repair build

r367 x64 Debug build exits 0 on revised 993-input freeze
`D2C5BA2AC66DEF2947241F86D9E0DC8C0C7DE1E34F12CAB77A0E6F3C9B6F0984`.
Recorder PID 20420, bounds 02:24:15.310656–02:29:13.454389 UTC; stdout SHA-256
`50E0AFFE3D3F6E1F6F179B7EFC1A87A8E398FD845B5C57249669966474C09CC4`, stderr is empty.
All five linked ISA scans and gate controls pass. Build proof alone does not establish shutdown runtime.

Verified archive `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug367-shutdown-repair-build-2026-10-08`
contains 1,291 copies (993 source inputs, 298 evidence files), with source manifest
`A28B7EBBF2F9A433F1BA8B8B6365589DFF0B5D8EA4627B5FE5034C8B8538DB20`, evidence manifest
`1C931912A1E5AFDD6006BB2D46425B4B4E6D47AF61DB666716206CFF0CCEBF61` and completion
`D991D5AED1C2664901CAFA38B67E175A9F0A595C3A70834A48312AC5856ADA81`.
Application r368 passes on that freeze: PID 31916, exit 0, recorder bounds 02:30:07.249462–02:32:23.577636 UTC.
Stdout SHA-256 `46842330CB55421DB8C026C3E4599A3A2C1FAA496009744EE44CD79422D02BB9`, stderr
`AD8108E8762ACCD89D86221E7D6B9BC18FE3D6C0B56E637E94CF499707870311`; unchanged executable
`BE4F88B8471F693131D8F015E9EA17722994C6EAE8CA34E4108DCD208DC7BA35`.
All 105 reached application stages pass, including 20 stroke, 34 row and 17 accessibility checks.
The fixed source oracle verifies eight test/source hashes, all 105 stages, 55 added controls and the complete
earlier r362 application PASS sequence with only those additions and its explicit accessibility 16-to-17 count change.
This is actual application runtime proof; full profile and G1 remain due.

Verified archive `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug368-shutdown-repair-runtime-2026-10-08`
contains 1,291 copies; source manifest `EEE6DB9C4D943B8083C9549EB2012A774EEBC3E12D50CAD19BD12A9E68A3D44A`,
evidence manifest `E6129DA797FDCB09D40ADF9BC28B2AD8B39B6F0C2C9FC5FB5FD7642974A045EB`, completion
`0B0D92EA46ECC9B0B60EF23AD8F68E3C7DAD10B415DED98BE7E73D8F56809D31`.

## Endpoint and quarantine follow-up

The current candidate refuses old publication provenance after a real scheduler is closed and reconstructed
at the same address with equal first ticket numbers. Its six endpoint controls retain both old/new charges
on distinct owned endpoints and require that releasing the old graph cannot refund the new admission.

Ten additional application assertions cover healthy-device retirement refusal, dormant unchanged notifications,
two failed offers with the first terminal cause retained, real original-context event recovery, and cancellation
after a refusal. Cancellation cannot use its skipped worker acknowledgement to bypass quarantine or refund
CPU/GPU owners. Only a changed binding generation/route grants one retry; repeated unchanged notifications
remain dormant. The current final-stroke inventory is nine scenarios and 30 assertions. r369 passes the x64
Debug build for this inventory; fresh application runtime qualification remains due. The successful r368 run is
historical after these changes and does not qualify them.

## Endpoint-quarantine build receipt

x64 Debug build r369 exits 0: PID 104216, 2026-10-08 02:42:53.710045–02:48:42.521665 UTC, 993 inputs,
freeze `2517AB0DADFD72D90405C4ED729A47FFEBCE77E19E3ED193E0180D2F940CE30C`. The actual
`.build/incremental-x64-build-Debug-r369/result.json` records incremental-focus build mode, actual child exit,
and no automatic retry; stdout SHA-256 `0501842CD0ED8F3F8EC9448FEA04C7DD95D9E0872874DFF439AC8B71EC0CA95C`,
stderr SHA-256 `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`.

The completed archive `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug369-endpoint-quarantine-build-2026-10-08`
contains 993 source inputs and 298 evidence files. Its root manifests are `source-manifest.json` and
`evidence-manifest.json` (the files are at the archive root, not in subdirectories); their recorded SHA-256
values are `9489C6D98F7AFD9AB9AEC0D2934C7C7276CE9849DEFBB9EBBBBE0C092A802DBA` and
`1E8183EC0F836027F03A71CC451852E7882A03D2241701CD0D283C1D4BDB1CBC`. The matching
`archive-completed.json` records completion at `2026-10-08T02:49:27.3301288+00:00`. This build receipt
establishes compilation only; it does not establish application runtime or stroke qualification.

## Recorder attempts after r369

r370 did not launch a child: `run-p1-focused.py` rejected unsupported `--suite HostTests` during argument
parsing. The diagnostic is in
`Z:\src\RedPrism\.build\focused-recorders\p1-opacity-host-proof-r370\stderr.log`. This attempt provides no
build or runtime evidence.

r371 full Debug exits 1: PID 7380, 02:51:09.725918–02:58:13.571116 UTC, on the same 993-input freeze.
The core suites and crash probes pass, but the application stops at
`application-stroke-finish.setup.actual-second-retirement-refusal`, after the first two quarantine assertions.
Only 16 application stages were reached; this is no complete Debug or application pass.
Stdout SHA-256 `C54935576281930D490CC449775E9673A42E39EF14D9867EE3A42C5374069AF8`, stderr
`97274811472BDE7B6B73C1351FAA73A7BB80CF64AEE1FDA7BF666A48166BCE2F`.
Verified archive `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug371-second-refusal-failure-2026-10-08`
contains 1,300 copies; source manifest `74EEF084C970801ADAB4DD020736673F3690643052BECFEC99482167166D1D53`,
evidence manifest `FD4C503BFBBF18B9043F3D2FF0362043D62F0B1F83501D354CB6B275F2907D76`, completion
`D75D294BAC29FE1AC907C60DB60063F5718ED35D696F9C71E03E3BE8836550D4`.

Rebinding also schedules retirement of the committed display cache. The original diagnostic hook refused
the next barrier globally, allowing that unrelated cache barrier to consume the intended stroke refusal.
The repair targets the exact original stroke ticket on the second offer, while preserving the real queue
binding, original captured context, bounded wait and unchanged assertions. Fresh execution is required.

r372 full Debug exits 1: PID 98136, 03:00:31.836437–03:08:16.292094 UTC, on revised 993-input freeze
`6768EE78DCB68693B95156D7A0243A66D17BF7B7B150442FE030B4EFCA031371`.
All six recovery assertions now pass, including the real second refusal, dormant repeated notifications,
actual original-context event recovery and exact first-error receipt. The next scenario fails at
`application-stroke-finish.setup.quarantine-cancel-worker-acknowledgement`; only 16 stages are reached.
Stdout SHA-256 `FECADE60DCB0DB99CC7112A1E1056B7E9B731BF2A3AA40136F3550F3D340AD1A`, stderr
`CE30A18D40989087208B1C1FEB69B13A846BD262DD0143F6FE4EED00F9E7CDD9`.
Verified archive `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug372-quarantined-cancel-failure-2026-10-08`
contains 1,300 copies; source manifest `3E4EDB6C9638AA4FC0AC0E06358F860B94B6FCDA7D7B82D7919F4EC9E7934CD3`,
evidence manifest `E43A061E2869EE2E3BD501C9BB9C41827F8DBB24DD66766E428277CE9AD20C60`, completion
`533339B451E36EF94DBD7B2F5063A9273C02F87FD95B23222C392F3A9794B589`.

`JobScheduler::Cancel` marks a suspended job and notifies owner work; it does not automatically resume
the record. The quarantined parent had no readiness predicate for that cancellation, leaving its skipped
stage unacknowledged. The repair admits exactly one cancelled `AwaitAck` stage from the owner pump and
records its actual acknowledgement. Quarantine still holds every owner and charge until a real binding
transition and original GPU barrier; repeated cancellation cannot rearm the stage. r373 is compiling this
repair. The existing four cancellation assertions and timeout remain unchanged.

## Remaining gates

### Cancellation repair qualification

Debug build r373 exits 0: PID 111136, 03:09:53.418505–03:14:29.521997 UTC, 993-input freeze
`4773B63ECC28AE6A1B8BC9DEDAE2EAB758FB25CB6A08A5BB2B8E7AB501071B01`.
Stdout SHA-256 `06D61D6A0793138047B25A3D900D8D96BEB1FA30C929DD63362E23AB4C208601`; stderr is empty.
The verified archive `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug373-quarantined-cancel-repair-build-2026-10-08`
contains 1,291 copies; source/evidence/completion hashes are
`019EA97CD25FC2BF0A74B77C83A95FEB97FC2767D20090B8472914D5BDDA0163`,
`C7D17936DB7249AFAABAFA290317CA98480517A944056FB799CD2168C8D9CF9E`,
`414FCC54B552861C6208F19021B5C1250AAE59D01752F8F737F1A20A1933A808`.

Actual application r374 exits 0 on that same freeze: PID 121644,
03:16:08.127947–03:18:28.746963 UTC. All 105 stages, 30 stroke, 34 row-publication and 17 accessibility
controls pass. Source-bound checking verifies eight fixture hashes, all 65 nominated additions and the
complete pinned r362 application PASS sequence, with its reviewed accessibility count transition only.
Executable `3AB7D97935B43595A13ED4342EE23A257509F45B3D868CA4C46DC91C2685511D` is unchanged during the run;
stdout SHA-256 `81D0EF675A181B177277D34228C369D8824F8D7DFA2E3163560E5BA97FB81A75`, stderr
`AD8108E8762ACCD89D86221E7D6B9BC18FE3D6C0B56E637E94CF499707870311`.
The verified archive `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug374-quarantined-cancel-qualified-runtime-2026-10-08`
contains 1,291 copies and 105 stages; source/evidence/completion hashes are
`D650AE268B1F6071DEB91ABB6ADD7074F3B64C4A839AD19960FECFC680AC7372`,
`AA3F8163985E0696E1C6B96E7A6BA709CF42F7E97A3DFF6084CF08E60E26431B`,
`E7FF4E022A4BC3BFACAA6A742FD818F5EEFCA10C1F9ABADA23489943F70E7013`.

This qualifies the actual experimental final-stroke recovery/cancellation/shutdown path in Debug.
Full Debug r375 subsequently passed below. Remaining profiles, ordinary stroke activation and measured G1 remain open.

### Full Debug r375

The canonical full x64 Debug test command passes on the same 993-input source as r373/r374,
freeze `4773B63ECC28AE6A1B8BC9DEDAE2EAB758FB25CB6A08A5BB2B8E7AB501071B01`.
Actual child PID 58120 exits 0. Recorder bounds are
`2026-10-08T03:20:11.186678Z`–`03:28:43.402164Z`; these are not G1 measurements.
Actual stdout SHA256 is `8DA11DDA7D49782BF5DDA3306D614B2C0D089892CA0282EADF00F0A955247252`;
stderr is `ECEC0231BB923C5ED88B20DD8EB86A0B7CBCDC1B3E461956084AC575D23C0F7A`.
All document, host, composition, I/O, crash-probe, application and harness-capture gates pass.

Archive `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Debug375-full-qualified-2026-10-08`
contains 1,302 copies and all 105 application stages; it verifies at `2026-10-08T03:29:44.7307115Z`.
Source manifest: `A8B11F23DCC048F248943A9B494D95137D7E044F7A391D810878F098D60496CB`;
evidence manifest: `52E88E1871D225EC8292A45BAEC25E5950DD3BD43FD61062A47DA49F66B6E427`;
completion: `19656D88B2044F18E4510A5FF05C000D2881CA691E892B474A23D279A46E4691`.

Independent checking of archived sources preserves all 28,628 predecessor aggregate PASS records,
with exactly 104 source-fixed new assertions and four fixed summaries, for 28,736 current records.
The ignored r4 oracle initially ordered the parent controls by lexical source location. Its r5 repair
follows the actual source call order: initial cache/submission controls, joined parent callbacks, then
trailing fixture checks. Names, multiplicities, expectations and product/test code are unchanged.
The separately checked application stream preserves every predecessor PASS record and requires all
65 nominated current controls, strict summaries and the complete 105-stage sequence.
Release r376 subsequently failed compilation below. Release, real ASan, analysis, ARM64 cross-builds and commit qualification remain due.

### Release callback identity repair

Full Release r376 exits 1 during compilation, with no runtime stages: PID 121848,
`03:29:57.464065Z`–`03:31:47.747986Z`, on the r375 freeze. C4100 identifies eight callback
parameters in `ApplicationStrokeFinishJobs.cpp` that appeared only in `assert` expressions.
Release removes those expressions. Stdout is
`DA835BEF0DC4EDD49C7638BB061F77B1AD424BC173A329BA98753200E11108CC`, stderr
`A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`.
Verified 1,290-copy archive
`D:\RedPrismQualificationArchives\P1-stroke-publication-993-Release376-assert-only-compile-failure-2026-10-08`:
source `AFCAC10F5B195675AAF1521A7DDA71B27B2CC9A0F6D35D3C1D71E47181E0E893`,
evidence `0C627CB437A8C3623E1D4AC1563DB9A51807356E19636F379509510A9714179A`,
completion `EA88CD8FD5CF1030F675F88D05619353CF0B385BAF9273B7612A631830881ED7`.

The repair validates exact original ticket, command source, pending phase and captured device generation
in every build configuration. A violated internal callback identity terminates rather than mutating
another request or accepting a foreign GPU acknowledgement. No warning suppression or discarded parameter
replaces the invariant. The source is formatted before the retained r378 run.

r377 was launched prematurely after the format check failed. The parent stopped only its verified owned
PowerShell test process and console, before application runtime, then formatted and restarted.
Its actual receipt is PID 83356, exit 1, `03:34:29.359448Z`–`03:35:20.881410Z`, freeze
`0E13C8868889F555EE3D5D96781EEC32C2E1D588013F59DF4C287174C08E1B81`;
stdout `4887659DED9A891DB8FAC0B650B7654632BA7309281E9F1932AB191E14020720`, empty stderr.
Its local attempt retains exact source/logs. Archival attempted after r378 started and refused a locked
Release executable; that partial archive has no completed verification receipt and qualifies nothing.

Release r378 and ARM64 r47 compile against formatted 993-input freeze
`FFF5E480BE7BA489C1A3EDD99A0E7BCAF76A20F0168A833D00C803CCEC83A993`.
Earlier r375 is historical for this changed source; the current complete matrix remains required.

### ARM64 r47

All three cross-builds pass on that same formatted freeze. The custom recorder checks every frozen
input before and after each build, records the actual child creation/exit interval with `GetProcessTimes`
and binds its driver SHA256 `3D4FE96915A97BABEF4CF114058070F12E461867F9DDAB7BE77B7E7A81B75BDD`.
No native ARM64 runtime result is established.

| Profile | Child PID | Actual child interval UTC | Stdout SHA256 |
| --- | ---: | --- | --- |
| Debug | 28760 | 03:37:28.981380–03:38:45.080324 | `6EBACF8C325701BEE121C6BCEBDC67D4E7D6538E196CA996EE076C0BC758181E` |
| Release | 108500 | 03:38:49.897639–03:40:39.865168 | `8E146AE0783D7656410C404D279A6C87634641DD8DAD01CF88D0B7B631441C18` |
| ASan Debug | 75688 | 03:40:45.059588–03:41:56.692771 | `73AD13DC1B0E1D5BD21FC4FE15393A48857C43B7CB6F7DBE3CE55DB149B9F30F` |

All stderr streams are empty and actual child exits are zero. Archives under
`D:\RedPrismQualificationArchives\P1-stroke-publication-993-ARM64-<profile>47-qualified-2026-10-08`
verify independently:

| Profile token | Copies | Source manifest | Evidence manifest | Completion |
| --- | ---: | --- | --- | --- |
| Debug | 1064 | `D98C02BFF26C01A884D1DDF171845DE958E34E43F3914BCD47EA0504E0DA4AF8` | `9ECB7A46542D2A17D3A71DBD51931DEA90A15307F40C5F06366AF6EB494A18FA` | `754EAB75C366092D649FAF02CD9C77B151AB993D1D1D7E6E204818FF31393FDD` |
| Release | 1064 | `430D5D862930202F86220F6C2980C91C9BA0BC893254971E43B8DBF1B481B276` | `A898F4308AFC06860D2011017ABFE6BD4690A8221ED471DE2830F3ADE939D2DC` | `A184AF6BA08C8205EB4F471817685A7A3E1447399B2856F56A9CFDD0067FF7C6` |
| ASan | 1068 | `3886EEF87923A95F41B9722BFE49DE9CFE66FB21D7665B38B510C0E141ABDC0D` | `71F8CB2CFF26E9FCFED1C831995691AD0FF0E176E8742680AA768346BCABEA13` | `BE9D53FDCF8258067ED3FBFC27BBE6FBA2D2F4D2A863DBCD617694C864A790BE` |

### Remaining delivery gates

Release r378 passes on the formatted source: PID 72256, exit 0,
`03:35:45.494589Z`–`03:43:52.948142Z`. Stdout/stderr hashes are
`724B5BFEB26646960B5720A5B891F6CA5C27AD55B648869F51ADEB9D17AFC9E1` and
`B31B7178564F19BF41CED760303BB11F6CAF4B508C2A52B79BA4BFE3B1A6F2B1`.
Verified archive `D:\RedPrismQualificationArchives\P1-stroke-publication-993-Release378-qualified-2026-10-08`
contains 1,292 copies and 105 stages; source/evidence/completion hashes are
`35E5FEADFFD9EA1DCEB270C26604BA2A3A9AB982762E16E22F9F838F79C845F0`,
`E4C1C2E7CB6516EE78469163830C74932E50BCD8F19D75A09E24F47565FE12D1`,
`9A0F284878CC6B13E5FF0AA37C3154F86D277D87FB3E8A81025C036015F34789`.
Independent archived-source verification preserves all 28,513 predecessor aggregate PASS records plus
115 scanner assertions, 104 publication assertions and four summaries, for 28,736 current records.
The separate application comparison preserves all 4,535 profile-specific predecessor records plus
65 source-fixed assertions and the stroke summary, with the UIA counter change at its original position.

ASan r380 fails before compilation because simultaneous x64 analysis/ASan dependency restores contend
for the same pinned DxUi vcpkg lock at
`.build/dependencies/DxUi/ed2d9c8ce97fa945/vcpkg_installed/x64/vcpkg/vcpkg-running.lock`.
The stderr records `try_take_exclusive_file_lock failed with 16 (device or resource busy)` and
`failed to take lock, another vcpkg may be running against the same directory`. No compilation or runtime
stage is reached. PID 117464 exits 1,
`03:45:16.687763Z`–`03:45:24.325071Z`; stdout/stderr are
`12CBB9E013867D845F1C5FE65FBE36DBA2282BD407A274809A1FD4A4B542DFFD`,
`9643B1C2D7EC3FFFD5E1E3592674B62005B89CECBA61649CE3569E553857D72A`; attempt files:
`.build/incremental-x64-test-ASanDebug-r380/result.json`, `stdout.log` and `stderr.log`.
The verified failed archive
`D:\RedPrismQualificationArchives\P1-stroke-publication-993-ASan380-shared-vcpkg-lock-failure-2026-10-08`
contains 1,293 copies; source/evidence/completion hashes are
`C536DCBC53668C1A95C56DE6234F5B53AE5BB1E1AC4EFF17E6D35AC243DCE34D`,
`576EC67A564B4822534A907BD07EBD105D6F7041BBE0911F4DE018E0EC9B1594`,
`AA4525811F3C1A2713A88BDF55D18D140CDD60C88C6C09330DE130DF0183D9C8`.
All x64 profile builds are serialized for the retry; no lock file or dependency source is changed.

- Complete final-stroke caller coverage, including capture loss, FIFO and every brush kind/mask; retain the
  qualified recovery/cancellation/shutdown expectations above.
- Fully asynchronous Down/Append/Up, prepared sample previews and ordinary activation for all brush kinds/masks.
- Full current-source x64 Debug/Release/real ASan, Release analysis, ARM64 cross-builds and independently verified
  source/product/control inventories. ARM64 runtime claims require a native host.
- Actual G1 measurements, remaining P1 checklist work, domain/user-guide reconciliation and qualified commit/push.

The historical 978-input Clone/opening results are recorded separately in
[`P1CloneWorkerOpening_2026-10-08.md`](P1CloneWorkerOpening_2026-10-08.md). They do not qualify this revised source.

## Historical HRESULT fixture source freeze and follow-up attempts

HRESULT fixture source freeze: **993 inputs**, `EB059799011B1F7397DEA08C3E3F5158E7086B23C65DE3810F0B6A38B01BED23`.
The prior full Debug r375, full Release r378 and ARM64 r47 cross-builds are historical relative to this freeze.
The fresh full x64 Debug/Release/ASan Debug matrix remains open.

Release analysis r379 exits 1 on the preceding formatted freeze
`FFF5E480BE7BA489C1A3EDD99A0E7BCAF76A20F0168A833D00C803CCEC83A993`: PID 100852,
`2026-10-08T03:45:16.687777Z`–`03:49:25.766103Z`, 993 inputs. It reaches compilation but fails on exactly
two C6221 warnings treated as errors in `Tests/HostTests/Host.Tests.PublicationRetention.cpp`, lines 304 and 323.
These are the prerequisite-refusal equality fixtures comparing the int32 publication result with
`HRESULT_FROM_WIN32(ERROR_INVALID_STATE)`. The explicit fixture repair defines
`constexpr auto invalidState = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE));`
and compares both results with that int32 value. It changes only the fixture's HRESULT equality typing.
The failed attempt hashes are stdout `C763ED8C70BDCF13714D694D51CD5505F4635C94F3616DE037ABB0EFA98F4F89`
and stderr `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`; attempt files:
`.build/incremental-x64-analysis-Release-r379/result.json` and `stdout.log`.

Release analysis r381 passes on that HRESULT fixture freeze: PID 72400, exit 0,
`2026-10-08T03:52:15.350207Z`–`04:00:06.767223Z`, 993 inputs. Its result records stdout
`9F672BA1B404709B4CDD88F028171B0B7E0D441072C576A52CA4BA45C2AD4A76` and empty stderr
`E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`; stdout ends with the ready Release
executable and verified DxUi pin. Attempt evidence is in
`.build/incremental-x64-analysis-Release-r381/result.json` and `stdout.log`. The independently verified archive
`D:\RedPrismQualificationArchives\P1-stroke-publication-993-analysis381-qualified-2026-10-08`
contains 993 source inputs and 313 evidence files. Its source/evidence/completion hashes are
`481FD5384C2FB483FDBCFA8B344EACDC17807C1307B41630E5B85B1BC1439BCF`,
`F145C8F94B438E135C580774F2735B27EDB9AEF263316CA25BB7A98490762EC6` and
`E4EFF6EF385A43C8AAA8CA8C19C9C692D142C60FB70DFAED76488FE8989E0BF5`.

ARM64 r48 Debug, Release and ASan Debug all cross-build successfully on the same freeze. Attempt results and
stdout logs are under `.build/residency-arm-builds-r48/` (aggregate `result.json`, plus per-profile `result.json`
and `stdout.log`). The recorder used
actual child handles and `GetProcessTimes`, checked all 993 frozen inputs before and after each child, and
all three archives have completed receipts. These results establish cross-compilation only, not native ARM64
runtime qualification. The receipts are under `D:\RedPrismQualificationArchives\` in
`P1-stroke-publication-993-ARM64-Debug48-qualified-2026-10-08`,
`P1-stroke-publication-993-ARM64-Release48-qualified-2026-10-08` and
`P1-stroke-publication-993-ARM64-ASan48-qualified-2026-10-08`.

| Profile | Child PID | Actual child interval UTC | Stdout SHA256 | Archive source manifest | Archive evidence manifest | Completion |
| --- | ---: | --- | --- | --- | --- | --- |
| Debug | 86724 | 03:54:04.410220–03:54:20.433974 | `22689D2A07DD6D5F5235ACCF780D72BBC0F87BC9CBA859714D3EF7D0E57C4183` | `FD2BCC59E216C4C35471DD8D1E83B20A5A0079607F933F557EFD28F541F78361` | `D9507871BD96A12F8CC4BDC5C606ED7CBC783863BCCF80436CE1DC1C19F7B44B` | `A3946EE42482132B5D3A1623B7B09D25BAF8B284E97A7B502DC8459EFA830982` |
| Release | 19912 | 03:54:28.038369–03:56:07.776766 | `B4EF42D74DD82EDD4301F7B1CE720970CE44B0D77B7F3D4E44436F3A00B06F8E` | `D851D68C30B2C5B8B83B3A5E86070C7C4741A1D4FFCEB476925DF77F0A28F4AB` | `9322BA389362538C35185841F8D96F372D837CE94EFB3B02C921896B893CFEEE` | `3652957226405E4EDFD1B5379F72D3357E74448AB14D40D2C3AB48037107A6E0` |
| ASan Debug | 119084 | 03:56:12.791116–03:56:32.664004 | `1FCFE78A57B7B9728AF64F66DAEAB9E786DBA2AB7BE6F1DA262D2C48CFBF87A0` | `6CBA16107F9096B4FA65535AB0CA6F9FD27C1844F117E8AC9EC2706B8FD995F0` | `47B4AA55179A0677D1774C89E1267851F654033FED3560B3962A6A0864A39E21` | `11A11DD6C8FDC46240A9E76FE379F314826F0F97D38AC0DD23C725C8CC4845C4` |

Fresh ordinary asynchronous pointer sample and preview routing remains open. P1 remains WIP; these analysis and
cross-build results do not close the fresh x64 runtime matrix, G1 measurements, ordinary activation or delivery.

## Accepted-prefix overflow receipt

The experimental final parent now preserves the synchronous host's capacity-loss receipt. A successfully
finalized accepted prefix still publishes its one history entry and complete display/row roots, but returns
exactly `ERROR_NOT_ENOUGH_QUOTA`. It never substitutes the refused point or late Up. Earlier finalization
failure/cancellation/staleness keeps its terminal outcome. `Application.Tests.StrokeFinish.cpp` adds five
source-fixed controls before shutdown, taking that fixture to ten scenarios / 35 assertions. Its clear 8×8
source, hard 4 px red tip, accepted points (3,3) and (4,3), and refused point (7,7) define the literal pixel oracle.
No sample is drained while filling all 512 FIFO slots; Lost uses the separate terminal slot.

Full Debug r382 and all three ARM64 r49 cross-builds pass on current **993-input** freeze
`A0ADC7E26AF83395E81EFDBE38D7927A07B74BB805CF1155D4B7B91B99FC5078`. Release analysis r381 and ARM64 r48
are historical after this source change. Fresh Release r383 is running; x64 ASan Debug and Release analysis
remain due. P1 remains WIP.

### Full Debug r382 qualification

`.build/incremental-x64-test-Debug-r382/result.json` records actual child PID 61104 and exit 0. Its
`StartedUtc`/`CompletedUtc` values, `2026-10-08T04:03:51.617934Z`–`04:12:44.194184Z`, are **recorder bounds**;
they are not a captured kernel lifetime interval or G1 measurements. The canonical full Debug test command
passes; the receipt retains its incremental-focus compilation scope and no automatic retry. Stdout ends with
`All tests passed.` Actual stdout/stderr SHA256 values are
`41148C0AC31A09D1EE14EE841D465838D8258BBCB432B111FE0F49FD66DB7ECE` and
`81092DBA8EFAAC2F850D3D9F75956E7EAF9EE7BA196D4676DB38AC4AA3B71313`.

Completed archive `D:\RedPrismQualificationArchives\P1-stroke-overflow-993-Debug382-full-qualified-2026-10-08`
contains 1,304 copies: 993 source inputs and 311 evidence files. Its completed receipt is dated
`2026-10-08T04:15:08.1951912Z`; the recorded independent verification is `04:15:13.1783564Z`.
Source/evidence/completion hashes are
`39B9948635F095C549F16CDB5A69F975CDC380188CE7F7F1A8B44A647B017A25`,
`462D1C5CDD332FB4A47522ECE77FC2AC7AAB620B0D9A440C1139D03EACFAC10C` and
`BAED7E94A5DE7D80919D7F079741E5E7E2B69FA575B46768F07E9DAC043C3C9B`.
The separate archived actual application log has SHA256
`22CE62225C31A016C085E26D0BB5C6840828D4AFB3F1207984AC2B813E0D1D06`, written at
`2026-10-08T04:12:18.4003065Z` inside the recorder bounds; all 105 application stages pass.

The parent executed the reviewed `.build/P1Drafts/PublicationInventory-r7/verify_full.py` against the archived
`Sources`. It preserves the complete 28,628 predecessor aggregate PASS records, adds 104 fixed controls and four
summaries, and verifies 28,736 aggregate PASS records. Separate actual application verification preserves the
full pinned r362 application predecessor in exact order and multiplicity, including its reviewed UIA 16-to-17
counter transition. All 105 stage anchors and 70 source-fixed additions pass: 35 stroke, 34 row publication and
one weak-UIA witness. The parent reviewed the five fixed overflow source names and syntax, and all five verifier
mutation rejections (missing, duplicate, reorder, extra and skipped stage) pass. The overflow source literals are:

- `application-stroke-finish.overflow.full-fifo-retains-accepted-prefix-and-separate-lost-terminal`
- `application-stroke-finish.overflow.accepted-prefix-finalization-retains-original-pending-ordinal`
- `application-stroke-finish.overflow.quota-receipt-commits-only-literal-accepted-prefix`
- `application-stroke-finish.overflow.published-display-and-rows-exclude-refused-point`
- `application-stroke-finish.overflow.acknowledged-overflow-retires-original-worker-payload-and-stream`

The documentation check independently matches the local result to the completed receipt, log and archive-root
hashes, archived actual application log and all eight r7 source bindings. Its direct aggregate count and stage
anchor count agree with the parent's reviewed verifier result. No build, runtime or verifier execution was
performed by this documentation task.

### ARM64 r49 qualification

`.build/residency-arm-builds-r49/<profile>/result.json` and `stdout.log` record exit 0 and a ready executable for
Debug, Release and ASan Debug on the same A0ADC source freeze. The recorder checks frozen inputs before and
after each child. These are actual child kernel intervals from `GetProcessTimes` on the owned child handles;
all timestamps below are UTC on 2026-10-08. Driver SHA256 is
`25EF5BD279ACB07B1A3D97A8802187BAC3FDF492E9D66A2732E4784FD94F1DC7`; all stderr files are empty, SHA256
`E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`.

| Profile | Child PID | Actual child kernel interval UTC | Stdout SHA256 |
| --- | ---: | --- | --- |
| Debug | 66112 | 04:05:53.982361–04:06:16.001138 | `186D8C9DA8E6FE793A46E97E208095923B92EDFFC3A974F69C2D715B7A2A2840` |
| Release | 83052 | 04:06:20.873989–04:07:11.677591 | `E7D57B6C3CB4FA3173DC3C05F4F22E89D3389C3F4ACB643EA78981C22EE74A8B` |
| ASan Debug | 120880 | 04:07:16.044763–04:07:37.100473 | `97A9FE558BDD98E44E742B2FCBFAD7F4DBBE88D8C56E5FCE3A1EA6097623E9CA` |

Completed archives are under
`D:\RedPrismQualificationArchives\P1-stroke-overflow-993-ARM64-<token>49-qualified-2026-10-08`.
Their receipt fields, source/evidence manifests and completion hashes independently match:

| Token | Copies | Receipt completion UTC | Source manifest | Evidence manifest | Completion hash |
| --- | ---: | --- | --- | --- | --- |
| Debug | 1064 | 04:09:37.2832890 | `FC2EB06057866C4D4156FDE74A363FE0695E25D2B20A659A89F7D6908E264492` | `B2953F6F3F225DA62ED632F8CC36236A1187E6D79D90AB01EC6A31D89783871C` | `B6409189C4A43B95628D7A664465B2784015E75865817F289BFFA49B5B4E7379` |
| Release | 1064 | 04:09:43.3235079 | `850B25FB921E4EF17E5AB74B01EFE397F5D50AA850BEAE273EA4B05CB93117A2` | `7C2C9238CFE4C4957B72AB39B1291352CEE664D28DED15D1C6532EF68F98099C` | `8ADC3D9C97A585DFBDBEDB5F93BB1DD4F27444533EA13248480E8AE3BE5B9591` |
| ASan | 1068 | 04:09:49.1589055 | `BDEC0279B86D07FA7254100E1E79DA77C16A8537095290D0D8B9E846D06FA8C9` | `680A71862155407BFC6C6726A684A7A6D0A05DA4B86D12A228DFD5E8B6AF44DC` | `EBB6876262A3EA12B2915F17009AD0124E2F7FB7E9763F6C69C8D4A31E77DC87` |

ARM64 r49 establishes cross-compilation; native ARM64 runtime qualification requires a native host.

### Full x64 Release r383 qualification

The canonical full x64 Release test command passes on the current **993-input** overflow-receipt freeze
`A0ADC7E26AF83395E81EFDBE38D7927A07B74BB805CF1155D4B7B91B99FC5078`. The recorder result records actual child
PID 93612, exit 0, Release/x64 test mode, and no automatic retry. Its `StartedUtc`/`CompletedUtc` values,
`2026-10-08T04:15:37.726927Z`–`2026-10-08T04:22:54.797547Z`, are **recorder bounds**, not `GetProcessTimes`
kernel intervals or G1 measurements. The archived stdout ends with `All tests passed.` and contains 28,736
aggregate PASS lines. The actual application log was written at `2026-10-08T04:22:31.2610547Z` within those
recorder bounds; all 105 application stages pass.

Independent archived-source verification reports 28,513 predecessor aggregate PASS records plus 115 ISA
assertions, 104 source-fixed controls and four summaries, totaling 28,736. Separate application verification
preserves all 4,535 predecessor records in exact order and multiplicity, adds 70 source-fixed assertions and one
summary, totaling 4,606 records; the complete 105-stage sequence and the UIA 16-to-17 counter transition remain
at their reviewed positions. The 70 additions are 35 stroke, 34 row-publication and one weak-UIA control.

The completed archive
`D:\RedPrismQualificationArchives\P1-stroke-overflow-993-Release383-full-qualified-2026-10-08` contains 1,294
copies: 993 source inputs and 301 evidence files. The completed receipt records completion at
`2026-10-08T06:23:50.4474668+02:00`; its source and evidence hashes match the root manifests. Exact SHA-256
identities are:

| Artifact | SHA-256 |
| --- | --- |
| Source freeze | `A0ADC7E26AF83395E81EFDBE38D7927A07B74BB805CF1155D4B7B91B99FC5078` |
| `result.json` | `789EBA699E2F8A267EA9834B122ADF307861393B190E61CC4B8268C0D245151F` |
| stdout | `6B8126263A2A5832CE32CB03C01C68EC9D41554704BA4623AF6AE845C4BAA117` |
| stderr | `FDFECF6675A7C52D10C18A145644040139EA3C641989976A135B77897B49B817` |
| Actual application log | `CE69B5E850C57E29B88FEF40EE4ED18C3B5229BA362CF0DBE40D2715B7BD16B3` |
| `source-manifest.json` | `2C961ABF5DEA95DAF86EF34D0C3599E935080FD85F5BFF38AC632E2CCC202ADA` |
| `evidence-manifest.json` | `D5272E776A3358C0336B1141E35F825E1DB1F978DE2D5CB98E6778DDB896A467` |
| `archive-completed.json` | `24A7057EC2508101B65FB02A416C001D94122D34F0AE8F47400DFAC377D850FD` |

The completed receipt binds 993 inputs, 301 evidence files, Release/x64 exit 0 and all 105 stages. Its source and
evidence manifest hashes agree with the SHA-256 values above. Full x64 ASan Debug r384 is qualified below. Fresh
Release analysis r385 passes on the same source, as recorded below. Normal asynchronous Append and preview
routing, ordinary activation, actual G1 measurements and the remaining P1 delivery work remain open. P1 is not
Done.

### Full x64 ASan Debug r384 qualification

The canonical full x64 ASan Debug test command passes on the same **993-input** A0ADC overflow-receipt freeze.
The recorder result records actual child PID 70148, exit 0, ASan Debug/x64 test mode and no automatic retry. Its
`StartedUtc`/`CompletedUtc` values, `2026-10-08T04:24:07.066146Z`–`2026-10-08T04:35:55.805093Z`, are **recorder
bounds**, not `GetProcessTimes` kernel intervals or G1 measurements. Archived stdout ends with `All tests passed.`
and contains 37,264 aggregate PASS lines. The actual application log was written at
`2026-10-08T04:35:13.2328005Z` within those recorder bounds; all 105 application stages pass.

Independent r3 profile verification against archived `Sources` preserves 37,104 complete predecessor aggregate
PASS records and adds 152 assertions plus eight summaries, totaling 37,264; the additions comprise 104 ordinary
controls and 48 stress controls. The separate application inventory preserves all 4,535 predecessor records in
exact order and multiplicity, then adds 70 source-fixed assertions and one summary for 4,606 records. It passes
the same 105-stage sequence. The 70 additions are 35 stroke, 34 row-publication and one weak-UIA control; the UIA 16-to-17 counter
transition remains at its original position.

Completed archive
`D:\RedPrismQualificationArchives\P1-stroke-overflow-993-ASan384-full-qualified-2026-10-08` contains 1,295
copies: 993 source inputs and 302 evidence files. Its receipt records completion at
`2026-10-08T06:36:32+02:00`; independent archive verification completed at `2026-10-08T04:36:37.6228214Z`.
The root source/evidence manifest hashes match the receipt. Exact SHA-256 identities are:

| Artifact | SHA-256 |
| --- | --- |
| Source freeze | `A0ADC7E26AF83395E81EFDBE38D7927A07B74BB805CF1155D4B7B91B99FC5078` |
| `result.json` | `B07A6228F50737C68733EAD66D5E0416ECA81741E0AEA5B12CC09C719A390D6F` |
| stdout | `E879ED7B889B692241F1476227E60D8AF1E05968AF3D4CF4803142C83B04FF37` |
| stderr | `4962C4C6E14163ACFC02ECA5BF42A1DF88D6B5775988777F47A8915EE18A4043` |
| Actual application log | `11EF34BD51E8E471AB4D2AF118B4DF4D2609E93041AFF150F63A3956D621F364` |
| `source-manifest.json` | `C942AF49407F595D531D93C61B18FE7E6863F762F91C4F19EB7B999BD77D3F41` |
| `evidence-manifest.json` | `69A826DC2D4CD6813E4D46C450979FE6E0E29FB964754C6E1B58E6CBAB4BE781` |
| `archive-completed.json` | `A0556D274E009D048CB8625C6220509CF806DF4456D70FD0C738CC98AD87C036` |

Fresh Release analysis r385 passes on the A0ADC freeze, as recorded below. Normal asynchronous
Append and preview routing, ordinary activation, actual G1 measurements and the remaining P1 delivery work stay
open; P1 is not Done.

### Fresh Release analysis r385 qualification

The canonical Release analysis build (`build.ps1 -Analyze -MaxCpuCount 2`) passes on the same 993-input A0ADC
source. Actual child PID 26048 exits zero with recorder bounds `2026-10-08T04:36:50.412841Z` to
`2026-10-08T04:44:43.705952Z`; these fields are not kernel intervals or G1 measurements. Stderr is empty.
The linked instruction scan and its acceptance/refusal controls pass; the unchanged DxUi pin is verified as
`8e8994079e82f79e6765e6a885c03fc687957591`.

Completed archive `D:\RedPrismQualificationArchives\P1-stroke-overflow-993-analysis385-qualified-2026-10-08`
contains 1,308 copies and verifies at `2026-10-08T04:45:13.5479217Z`. This is an analysis build, with zero
application stages, not a replacement for the full runtime qualifications above.

| Artifact | SHA-256 |
| --- | --- |
| stdout | `136A44AC5DB8C16BAE3E0FDF762194492D4C3F6D754F2836C9FECC864188FF11` |
| stderr (empty) | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |
| `source-manifest.json` | `0A7FF58EDC0C0232180DF9A1847C63A7FAA6EC2E4A14B66B511A093BC24FED94` |
| `evidence-manifest.json` | `4B1D8F70B61E64A7449A1A5849A70786EBDF305CD195197EA1BF1302644F2327` |
| `archive-completed.json` | `BCE9C36D207FCB904A8AA144DC069350ABABBAA148F751774E9DAE2DB0C7D3CB` |

Current-source `validate-skills.ps1` passes all 15 skills, `format.ps1 -Check` passes all 865 files, and
`git diff --check` passes. Debug r382, Release r383, real ASan r384, Release analysis r385 and all three ARM64
r49 cross-builds qualify the current step. ARM64 cross-builds do not establish native ARM64 runtime proof.

### Current remaining delivery work

The full current-source matrix is qualified above. Commit and push this step before integrating the next source.
All sample-parent drafts under ignored `.build/P1Drafts/SampleParent-*` remain uncompiled and unregistered.
Normal asynchronous Append and preview routing, ordinary activation, actual G1 measurements and the remaining
P1 delivery work stay open. P1 is not Done.
