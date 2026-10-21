# P1 original live stroke sample integration

Status: integrated opt-in sample parent has an archived x64 Debug build and actual-host Debug checkpoint. This is
experimental Debug evidence only. Ordinary activation, the fault matrix, G1 and the remaining source/profile matrix
are open; P1 is not Done.

The preceding complete 993-input recording/publication checkpoint is committed and pushed as `c111248` on
`codex/p1-opacity-host-proof`. Its exact matrix is preserved in
[P1StrokePublication_2026-10-08.md](P1StrokePublication_2026-10-08.md).

## Source and independent references

The integrated experimental parent starts after worker opening and before consuming Down. Its original ticket,
physical ordinal and history authority remain live through all sample phases and final publication. Worker coverage
uses the existing Catmull-Rom/compute reference. Private previews retain exact original GPU acknowledgements and
replace only a complete acknowledged root. CPU opening and GPU reservations grow by maximum, not by accumulating
a fixed charge on every point. Command ordinals remain monotonic through reuse.

The source-fixed helper fixtures declare 33 reservation-growth and 21 real WARP transport-restart controls. The
actual hidden application registers eight mode scenarios with 49 fixed assertions: Paint, Eraser, uniform and
nearest-stretched mask, constant Blur/Heal, active Clone and All Layers Clone, plus duplicate-point and original-ticket
ordering checks. The added application stage is `RunStandaloneStrokeSampleSelfTest`, immediately after the existing
final-stroke stage; predecessor stage order is unchanged.

Independent literals are full red/clear/blue and stretched mask quadrants 64/128/255. The healing wash reference is
archived behavioral reference: 12% gray at 45% opacity. Over opaque red its rounded BGRA is
`14,14,154,255`. `archived behavioral reference+Brush.swift` commits one history entry when patches
exist, even if final colors happen to equal the source. Functional event waits are ten-second deadlock bounds.
These controls are not throughput, responsiveness, Photoshop comparison or native ARM64 runtime evidence.

## Historical failed build r386

The first 998-input integrated build failed fixture compilation: `EditorTool::Blur` does not exist (Blur is
`EditorTool::Smear` with `SmearMode::Blur`), followed by dependent array constexpr diagnostics. A signed/unsigned
history-count comparison also warned. No new application runtime occurred. This failure and its completed archive
are retained as history; the later successful attempts use separate source freezes.

Actual child PID `82568` exited `1`; recorder bounds were
`2026-10-08T04:47:13.757742Z`–`2026-10-08T04:48:03.585268Z`. The source freeze is
`98CDF962AF521EE54F14DC29AB5E6ABC0E1A5CC1AB336E16B7BE405BFA7C2137`. Completed archive
`D:\RedPrismQualificationArchives\P1-live-sample-998-Debug386-build-failed-2026-10-08` verifies at
`2026-10-08T04:48:43.3340374Z`.

| Artifact | SHA-256 |
| --- | --- |
| stdout | `5826483D3EC0ECAFD38C0F800DE2822EB4C5F102BC101E9EE5AFDC1204C0EBC2` |
| stderr | `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7` |
| Source manifest | `06DB83EDD1D7B5BABD26926D8FEA7B0D2B6923A905302024C272B7AED7FD6F42` |
| Evidence manifest | `4231271E7F56CF5E50060E529495CEF95BF200F12107F0E53F1E06102C53AFA4` |
| Completed receipt | `9954D00C3D0AE3EBB32469BFBC6DA17D4DA5AFF4E69A7E74C2C2FA31982021B3` |

## Historical successful Debug build r387 and isolated CompositionTests r388

The Smear/Blur fixture selection and signed history comparison were repaired. The successful r387 x64 Debug build
and separate r388 `CompositionTests` runtime both use the 998-input source freeze
`40C82457DAE5D7C76C9757F789BCDEF26CF1B2995DB7E82A993740A463635C08`. They are historical and do not qualify the
later current source. The isolated composition run passes its 54 controls; its fixture expectations are historical.
It is separate evidence and does not establish a source-fixed full-aggregate comparison.

| Attempt | Result | stdout SHA-256 | stderr SHA-256 | Completed receipt SHA-256 | Source manifest SHA-256 | Evidence manifest SHA-256 |
| --- | --- | --- | --- | --- | --- | --- |
| Debug build r387, PID `40400` | exit `0`; `2026-10-08T04:49:23.881755Z`–`2026-10-08T04:54:12.308511Z` | `5A6C06B4227E9BA77FB5F29B5065578B2614AF075DCC0B1590175EB5A1C49D75` | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` | `1D08504B6A96A2F13835B315DF1AD13EE25491A44327BA2C64B3D7A0562A7913` | `7571BC52CD3C3BAAE1DF5CA4973E2802F8FE1F409BD5B58BD5AD2DEE78B03EFD` | `E3C7014A1CEEB7B6FFDD9FD411C8691609545C1173E67C8BC6D1F81236FFDADC` |
| CompositionTests r388, PID `63800` | exit `0`; `2026-10-08T04:53:43.103202Z`–`2026-10-08T04:53:49.154242Z`; binary unchanged, SHA-256 `5BC07B585B2D3294E241E3E17991CA683CAF7D82E242E1FAEA202F4E7C3293AD` | `A8E100253A56C4F328FFFC22F69D2D5FEED34B2ECDCD5D312E58FC9F99D38A72` | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` | `BFEBF2C5EE816E88C5A8056AB33DD4074E35BC9FE1B8234606C1EE9B4F5F1D85` | `0D86F5A1FF4A72043C2898D3FC80C9C8822342503BA0B8EDE6BEB4D42EE4ABDC` | `D3486038D21E767C21C2F713BE990A6181E8565F36FEB3F9908FA6871216CEF0` |

Archives:

- `D:\RedPrismQualificationArchives\P1-live-sample-998-Debug387-build-qualified-2026-10-08`
- `D:\RedPrismQualificationArchives\P1-live-sample-998-Composition388-runtime-qualified-2026-10-08`

## Current source Debug build r389 and application r390

Both current attempts archive the exact 998-input source freeze
`51299183E1BBF85C8453741EF624E8C3124640FFFE7AF7D651C9BE675687FA49`. The r389 Debug build's actual child exits
zero. Its archived MSBuild log ends with successful outputs for `CompositionTests.exe` and `RedPrism.exe`; the
incremental build receipt records empty stderr. Application r390's actual child exits zero and runs the hidden
`RedPrism` application self-test. Both attempts use the archive paths and completed receipts below.

| Attempt | Result | stdout SHA-256 | stderr SHA-256 | Completed receipt SHA-256 | Source manifest SHA-256 | Evidence manifest SHA-256 |
| --- | --- | --- | --- | --- | --- | --- |
| Debug build r389, PID `64824` | exit `0`; `2026-10-08T04:55:09.109840Z`–`2026-10-08T05:00:31.116450Z`; 998 inputs | `CC41940DABC1F50E79EA440CC7C2A2186681B2BAF0B25643435A2BF5EC7A27BD` | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` | `C5646F4FD29801D56A8F198B415C756FCD0466EE3D94F5072AA537BD29D5C774` | `46D0691AD927F80E79CCB574FA33126DB46EFCC360F25FCDBF8FBEBAAD61EBA7` | `DE74647BDC3196C1BE01A438572B3F01E0DD3B737391607785845939B63FEC6B` |
| Application r390, PID `99008` | exit `0`; `2026-10-08T04:55:45.322455Z`–`2026-10-08T04:58:21.039558Z`; 998 inputs; binary unchanged, SHA-256 `A4C9DD858B584B620B2A58E366462443C4FAF6E66370ABCE3D4E634CC14C3294` | `64EF5A4CC8204F7F3101B707F9A6A01531F8BD18889EDF954F4D96DEEB673E16` | `AD8108E8762ACCD89D86221E7D6B9BC18FE3D6C0B56E637E94CF499707870311` | `EFFB93227B23EA094214D884E72E84C49265B83EBFF0305D04A0E9DFB214B355` | `B77024015F4A6CBBF0AE6C778091593EDFF2FC6A80C756EACC9BE2C96E6E3D91` | `00B6C643054151A5E893D671D3B7C04B3B6F492A5FBB42A5665A40E5B2430F8B` |

The main independently ran the `LiveSampleInventory-r1` verifier against archived `Application390` `Sources`.
It verified that application inventory `4607` predecessor PASS/SUMMARY records are preserved through `4657` total
records after adding exactly 49 assertions and one summary. The integrated application stage inventory advances
from 105 to 106, with predecessor stages in unchanged order. This is the integrated sample's actual-host Debug-only
checkpoint; it does not close ordinary activation, asynchronous production Append/preview routing, the fault matrix,
G1 or the remaining matrix.

Archives:

- `D:\RedPrismQualificationArchives\P1-live-sample-998-Debug389-build-qualified-2026-10-08`
- `D:\RedPrismQualificationArchives\P1-live-sample-998-Application390-runtime-qualified-2026-10-08`

## Remaining qualification

### Retained fault/input attempts

Debug r391 builds the 999-input phase-gate/fault fixture; actual child 40804 exits zero at
`2026-10-08T05:11:56.461591Z`, from recorder start `2026-10-08T05:06:01.836796Z`. Freeze:
`A95DDEE286C44F1B4BC7DAE0E7A73C2B75DEEBD56483F344F4EC898EF2D9C988`. Archive:
`D:\RedPrismQualificationArchives\P1-live-sample-fault-999-Debug391-build-qualified-2026-10-08`;
completed receipt `20C1F7D2A0E97F194BFF2DF70814453607C4999DE8D4CEFB008657F3D585B3FE`.

Actual application r392 uses that same freeze and unchanged binary
`8519AF87C72DDECBE269433CBF4D423AA0372D9C6037E2EFF36D458ABE61CEDF`. Child 63628 exits 5;
recorder bounds `2026-10-08T05:07:10.007586Z`–`2026-10-08T05:08:33.998199Z`. All 49 earlier sample
assertions pass, then the new fault fixture stops at `setup.prepared.cancel.initial-display`, before a fault
producer reaches its event. Its 18-stage failed archive is
`D:\RedPrismQualificationArchives\P1-live-sample-fault-999-Application392-runtime-failed-2026-10-08`;
completed receipt `52AEC46057F02CB6DFD89BCBD180D49BD1F5FEE4491178F976A76E1A48E09051`.
The revised fixture enables recording and owner sources before history/document producers, which respects
the recording lane's startup requirement `nextSequence == 0`. The later r395 attempt identifies a separate
setup cause: the preceding sample fixture left its own WM_QUIT queued. The startup-order change alone did
not repair that failure; no fault phase was reached in either attempt.

Debug r393 compiles the added input/admission fixtures on 1,001 inputs; child 74568 exits 1 at
`2026-10-08T05:15:25.010594Z`, from recorder start `2026-10-08T05:14:30.565284Z`. Freeze:
`4C1515DAE4A4AA71987A038B59615E566060876EEE9609216E2977DC01D74389`. The input fixture omitted
`Selection.h`; `SelectionGeometry::Rect` consequently fails declaration lookup, with dependent assignment
diagnostics. No revised application runtime follows this build. Preserve its exact failed archive before repair:
`D:\RedPrismQualificationArchives\P1-live-sample-input-1001-Debug393-build-failed-2026-10-08`.

Main review of the cheaper input draft also corrected a zero-count gate predicate that returned before its
event, active IDs copied before scene construction, missing red foreground, Shift acknowledgement counters
shared between gestures, and an Undo case that previously sealed Up before testing live input. The reviewed
admission draft reuses the existing startup memory probe; no production memory provider API is added.
The revised 19-scenario fault, seven input and two admission scenarios remain unqualified until actual execution.

### Integrated build r394 and application setup failure r395

Both attempts use 1,001-source freeze `8A8D3582A6F5E35614243FF50503A5E932176CD1A198E03123F14A6A124961F2`.
Debug r394's child 105908 exits zero; recorder bounds are `2026-10-08T05:16:52.112117Z`–
`2026-10-08T05:23:13.749293Z`. Application r395 starts after the executable links while the build's ISA
checks are finishing; child 103932 exits 5 at `2026-10-08T05:20:03.153787Z`, from
`2026-10-08T05:18:23.823037Z`. These are recorder intervals, not G1 measurements.

The same unchanged executable hashes to `3F7EC149530FD3BC12776437435CFF0D704DEA46B159D26E8FE84B44E39DAEB4`.
All 49 sample controls pass, then stage 18 fails at
`application-stroke-sample-fault.setup.prepared.cancel.initial-display-settle`. Inspection traces this
to the preceding fixture's unconsumed WM_QUIT: SettleJobs returns Quit and reposts it before the new
fault producer is reached. Cleanup now consumes the quit from its own hidden window. Failure records
remain intact; phase-fault expectations are unchanged.

| Archive | Result | Source manifest SHA-256 | Evidence manifest SHA-256 | Completion SHA-256 |
| --- | --- | --- | --- | --- |
| `P1-live-sample-input-1001-Debug394-build-qualified-2026-10-08` | build exit 0 | `1CC3B9302326CC4CCE76358DD7320C4BB8D5C941E9D543DB9349D38CA0AC6E64` | `14FB77394C934CA6D634ECEEFED134E30543A4FC765E4F9AC3D11F0A43E8FDA1` | `BE0E51A9955B720F0A04F245261BC36BF864F78BAC35D07803B5EB201AA8CC09` |
| `P1-live-sample-input-1001-Application395-runtime-failed-2026-10-08` | runtime exit 5, 18 stages | `AD4FE3C951FCB55853F0E4D18009EDC8DAEB7ECBDB872F97974470B02406D87D` | `785C8307B54A1B10F937842C8F197D096474E7EB57EF7AB665B3BE0026163D0B` | `7EEE3C2CADDE2794CEAC85E2AD9E1C709C92DFD457487D0A0F7EF1229FC78144` |

Both archives live under `D:\RedPrismQualificationArchives\`; each preserves 1,001 source and 302 evidence
entries. The main checked the independent receipt verifier and its report; the recorded lengths/hashes and
manifest-to-completion bindings agree. The main corrected the delegated draft's description of these
overlapping attempts as sequential. Source-freeze identity and unchanged runtime binary are separate checks.

### Open and sealed Undo references; build r396

`Application.cpp` and RF-904 define the first Undo during an open brush gesture as cancellation without
history movement. Physical Up seals the original stroke; Undo then queues behind it, including while
accepted samples or final preparation remain outstanding. The input matrix now distinguishes all three
states: seven scenarios and 14 assertions. Sample/Input cleanup consumes each owned window's quit.

Debug r396 preserves the main's test-only compile mistake: DocumentHistory has no RedoCount member.
Child 76296 exits 1 on freeze `808443FDE0C34528995BFF8B32D37BA87CA6A94D1D10AD200A3396949AA2F464`;
recorder bounds `2026-10-08T05:29:17.088811Z`–`2026-10-08T05:29:41.097333Z`. Its archive
`D:\RedPrismQualificationArchives\P1-live-sample-undo-1001-Debug396-build-failed-2026-10-08` verifies;
completed receipt `085751CFD3142A540A1AD58015DEAAD287D05EA3B6FAC381DDF0EF3F3CD20335`.
The correction uses the existing `!CanRedo()` API with unchanged state expectation. Revised build r397 and
actual application r398 are running; no pass, normal activation or G1 claim follows until their results qualify.

### First executed fault matrix: build r397 / application r398

Both attempts use freeze `2FC52DCF0396CB24DFBA9D6B2597CA27C6296336D62316F74B69A710DA275F4F` (1,001 inputs).
Debug r397 child 87152 exits zero; recorder bounds `2026-10-08T05:30:58.718828Z`–
`2026-10-08T05:35:41.685384Z`. Actual application r398 child 6228 exits 5; recorder bounds
`2026-10-08T05:31:42.010602Z`–`2026-10-08T05:32:44.876029Z`. Its unchanged binary is
`4684CC89265E0C5E4ABBAF2A81732AB815C8052B2FA36B11A558B68C393A60E7`.

The corrected cleanup reaches all 19 original fault scenarios: 88 assertions pass, one fails at
`preview-recorded.device.exact-terminal-receipt-preserves-original-pixels-and-history`; the summary
reports checks=89, failed=1, scenarios=19. Stage 18 stops the application before input/admission fixtures.
Device-replacement frame exclusion, original producer ownership and off-owner retirement pass. The
main traced the terminal classification race: a GPU callback can set terminal before the recorder joins,
so the terminal retirement branch skips the currentness-to-stale assignment. Final classification now
also reads the sticky original-device-invalid flag. Original fault expectations remain unchanged.

| Archive | Source manifest SHA-256 | Evidence manifest SHA-256 | Completion SHA-256 |
| --- | --- | --- | --- |
| `P1-live-sample-quit-1001-Debug397-build-qualified-2026-10-08` | `B3779DFBF45B84C493D0AF993BCA17DC8C037410795AC5E54872303783E7AC94` | `7AC974483FEC0080D75D529B044B444D2089C7F79A4DA04549124FFC1BA68A49` | `C95780E6132EC8B267ED7CD42C5E8303E77F3AD62D7200380A2DEC56E0C8E6A8` |
| `P1-live-sample-device-1001-Application398-runtime-failed-2026-10-08` | `31036B45EF5EE6679CC916950EA0E125B66960A7ACACD7E38AFBEB0DBF663678` | `4B5BE748BCD56B0BEE5B6D6757CCA6D1F920B9D350DEA300E74309BF77A5E302` | `405C5AA181F29C01756D9F9664F2AC2829505005AE3A191D37DA16196587B156` |

The archives under `D:\RedPrismQualificationArchives\` each verify 1,303 retained source/evidence copies.
The main reviewed the source-fixed fault/input/admission oracle and independently ran its offline mutation
controls: synthetic success is accepted; missing/reordered/duplicate/FAIL/truncated-stage mutations are
refused. That is verifier proof, not a passing application matrix. Revised build r399 and application r400
are in progress. Ordinary activation, G1 and the remaining required configurations stay open.

### Qualified opt-in Debug fault/input/admission checkpoint r399 / r400

Both attempts use 1,001-input freeze `2A41545132E83B3AC0357430531F6E31DE3C0A7CC6B5EC3A2528664DD0DB1EDC`.
Debug build r399 child 16856 exits zero; recorder interval `2026-10-08T05:37:13.143543Z`–
`2026-10-08T05:41:54.102908Z`. Actual application r400 child 109516 exits zero; recorder interval
`2026-10-08T05:37:56.442413Z`–`2026-10-08T05:39:58.610909Z`. Its binary remains unchanged at
`0106DCDE4A0EA0BB5A7FACE38F960427D0EC159E315D7AA2F6B56A412D582498`.

The complete application passes 109 stages. All 89 fault, 14 input and 16 admission assertions pass,
including the unchanged preview-recorded/device expectation that failed in r398. The main independently
ran the reviewed source-fixed oracle against archived Sources and stdout: all 4,657 predecessor PASS/SUMMARY
records remain in order/multiplicity; 119 assertions and three summaries produce exactly 4,779 records.
The 106 predecessor stages retain order with the three registered siblings, totaling 109. Five offline
mutants are rejected. This qualifies the actual opt-in Debug route, not ordinary startup or G1.

| Archive | Source manifest SHA-256 | Evidence manifest SHA-256 | Completion SHA-256 |
| --- | --- | --- | --- |
| `P1-live-sample-device-1001-Debug399-build-qualified-2026-10-08` | `EA906053078CAF3312AFC1E42DE29DBE28D934DB9BE7E150059F91CF348D078C` | `4B768EA512F1030470971819754CEF003A979CB4C4B0D81368109F2E902CE408` | `2E572CE23F90BC1CF69A2E32351CB97455B02C73368C8162923A45B6596F71A6` |
| `P1-live-sample-fault-1001-Application400-runtime-qualified-2026-10-08` | `125C54DE1CDFD3A662A31E5E49F92E34CE45011F516FF3A1C6A2FA1FFC4A51B0` | `3D40FA5BE86556762586314D25C94C49377B4D07041B65978FA77CF255EB7152` | `25DBD866D66766497836BF6DDDAE36A525EED3366AD340381606E6A7068DFA9B` |

Each archive under `D:\RedPrismQualificationArchives\` verifies 1,303 retained copies. Formatting checks
all 873 C++ files; 15 repository skills and git diff checks pass. Required later profiles, patterned-content/
soft-tail parity, complete memory peaks, ordinary activation and actual G1 remain open; no P1 Done claim follows.

The integrated opt-in sample parent now compiles and runs in the actual hidden application on the current archived
source. Continue by qualifying actual asynchronous Append and Up for Paint/Eraser, Heal, Blur, Clone and masks with
unchanged coverage goldens and original-ticket ordering. Ordinary activation, fault injection/matrix coverage,
normal preview routing, G1 measurements and the remaining required source/profile matrix remain open. No P1 Done
claim follows from these Debug attempts.

### Patterned parity: preserved compile failure r401 and qualified Debug r402 / r403

Build r401 (actual child 115000, exit 1) preserves the omitted `ChromeHost.h` include for the complete
DxUi pointer-event type. Recorder bounds: `2026-10-08T05:44:43.364679Z`–`05:45:29.547331Z`;
1,002-input freeze `249F1A19DE991F3478553E58F3FCCE47F9A1427EA2650003EA7F1EF388D39F39`.
Archive `P1-live-sample-parity-1002-Debug401-build-failed-2026-10-08` verifies 1,304 copies, with
source manifest `C7FFDD5AACBB959712F9545456D4661FAE5B9C90F5D43D45FD55D4789D8B3D82`, evidence
manifest `7165E7A1643837C26C81B0EF39C654FC0B049FE5FD01DA623AC5185CF6BFB70B` and completion
`541736A1CE28C25DFFF070178702F03118CE88CADA9E52A4C9BE79FC5C979830`. No runtime follows that failed build.

After the include repair and formatting, build r402 and actual application r403 pass on identical
1,002-input freeze `0CC72FC58FC01F74B24CC4F26EF3A75D36F9F1C7061E7365401768DC9CAA556F`.
Build child 85176 exits zero; recorder bounds `2026-10-08T05:46:50.881751Z`–`05:51:50.221092Z`.
Application child 104340 exits zero; bounds `2026-10-08T05:47:31.479714Z`–`05:49:53.722327Z`.
The runtime executable remains unchanged at `EA3940759B7D1086F187A32AEC7016029A11FA76D70D14BC6AEE5CDF8C5EEBDD`.
These are recorder bounds, not kernel process intervals or G1 measurements; runtime starts after executable
linking while the build's ISA controls finish.

Ten separate actual WARP hosts compare against fresh direct `DocumentSession` sessions using
`CpuBrushCoverage::Shared`: soft/partial-alpha Paint and Erase; fractional selection; nonuniform mask Paint;
nonuniform image and mask Blur; both patterned Clone sources; translated growth with carried semantic mask;
and deterministic nonuniform Heal. Five acknowledged points and the physical Up exercise provisional tail
replacement and a duplicate. The complete live tile images/coverage, tile keys/rectangles and placement
match the CPU reference within the existing 2/255 tolerance. Final pixels and carried mask pixels/placement/
eager semantic thumbnail, exactly one history entry, original six sample acknowledgements and off-owner
retirement also pass. Fresh sessions preserve the fixed initial LCG seed and first Heal seed; no output-derived
seed or golden rebaseline is used. All 70 assertions and the exact ten-scenario summary pass.

Main review and independent execution of the source-fixed parity verifier against archived Sources/stdout
preserve every Application400 predecessor PASS/SUMMARY record and stage in order and multiplicity:
4,779 + 70 assertions + one summary = 4,850 records; 109 + one registered stage = 110 stages.
Synthetic missing/reordered/duplicate/FAIL/extra/truncated-stage mutations are rejected; that offline
verification is separate from actual application execution. Oracle SHA-256:
`7FD52C3557C4F487D4DC59C00DA5B9B03274B75815ED21A0DA7DFDDE768F7158`; reviewed verifier:
`91FBC7B99DE85D48FB76FE79EF9D151FE743DA862550E0483859B08F101F5814`.

| Archive | Source manifest SHA-256 | Evidence manifest SHA-256 | Completion SHA-256 |
| --- | --- | --- | --- |
| `P1-live-sample-parity-1002-Debug402-build-qualified-2026-10-08` | `2EDE06D666D7EAB7872CA438593BB7565FE1A80567FA7CF25974C0D66036D1D9` | `D646F1164D731EDF08F0CA6C8E9FB8745ACBF6FEF09799BBBC6EF79FC6F9B2AF` | `0BED2111EDFC08A2290FDF4310808736A29182E267852F0CB58736E58D2191C9` |
| `P1-live-sample-parity-1002-Application403-runtime-qualified-2026-10-08` | `65AAF837246D13A3A5A045A5D178CE8434471C3812844C9897F38EFB4DC0C331` | `6B2E2BFC7E6BB04D9CE34EFE0F864257B5ECDC100242C104A70A56DC69A2667F` | `4E00A2FA9A666EFE41F9EA3D06C87775617A65F002FF9E35E8A9CE3265D54D9D` |

Both archives under `D:\RedPrismQualificationArchives\` verify 1,304 copies. Full x64 Debug r404,
Release r405, real ASan Debug r406, Release analysis r407 and ARM64 r50 cross-builds are the next
qualification gates. Ordinary activation, complete memory peaks and actual G1 remain open. P1 remains WIP.


### Archived 1,002-input runtime and cross-build matrix r404–r406 / r50

This archived matrix uses the exact 1,002-entry source freeze `0CC72FC58FC01F74B24CC4F26EF3A75D36F9F1C7061E7365401768DC9CAA556F`. The completed receipts bind that freeze to all six archives below. Each actual child exit code is `0`. The receipt intervals identify the completed runs; they are recorder/build-test intervals and do not establish G1 timing.

| Profile | Completed archive | Child PID / exit; receipt interval (UTC+02:00) | Source manifest SHA-256 | Evidence manifest SHA-256 | Completion SHA-256 |
| --- | --- | --- | --- | --- | --- |
| x64 Debug r404 | `P1-live-samples-1002-Debug404-full-qualified-2026-10-08` | `111720 / 0`; `07:58:31.193530`–`08:08:13.618143` | `E931B34D0358F0EB4B65DB0532133DB240880E3174EBFD1C27E7C922F0BA6F76` | `7F72E5FB8A552379D35DFE3EAEFD835FA2A579C53E04CB983334FA82A2358453` | `3A6F87CB65BF5C0F57BA92FFD3E5A657CBB0338B47C683C3D4318781182E0734` |
| x64 Release r405 | `P1-live-samples-1002-Release405-full-qualified-2026-10-08` | `36344 / 0`; `08:08:38.646904`–`08:15:58.748806` | `8BCA9FEA5DF7F51BA5D527E562CDCA899D4D14C927B70F8E4BF29CD3E6C84D9F` | `E11356C39901051824ED2E2F5F4A6D94B1E2A673E84E3A9E2F9F46142ACC4854` | `8FB9A581044DCE11DB776AA7BD79809E4DD3816FCF7825B3720AD9DD65E10614` |
| x64 ASan Debug r406 | `P1-live-samples-1002-ASan406-full-qualified-2026-10-08` | `16832 / 0`; `08:17:13.295347`–`08:28:37.864592` | `9A81D204A7B1E9869BD5B5CB00DEF6C2C798F505FB07E21991F55A9BD797521C` | `9B4BC41DC8239CB2C78B2AB491F0A163B9FC68D2C1187BEC3A2ADF526DF2B6AC` | `9FFC5A5BDCE3B04C894ADD4BF8DBED222FA59CB774DED3745D41A82BDAF690D8` |
| ARM64 Debug r50 | `P1-live-samples-1002-ARM64-Debug-r50-qualified-2026-10-08` | `102320 / 0`; `07:58:52.439652`–`08:00:01.070473` | `89F46FC2FDA496C523C2B92C54BC7C4C08381DC78B1D2C93234A4ABA484577F5` | `B57BA0931ECF021E8EA18399F921274E74C031B6CBC760F1F7CC8DA7B76EA17A` | `F75C59C63579EFA59446C02DFE42341D5CE977C56E494592F1084D2204471B14` |
| ARM64 Release r50 | `P1-live-samples-1002-ARM64-Release-r50-qualified-2026-10-08` | `85224 / 0`; `08:00:06.769432`–`08:01:35.328104` | `5678FD993713260F780FA7109B71DAB32E016F4DC452C0E403EE02CB02C3CA51` | `D92FF6B5B8DF0BC6FD87209C94D5AE354B749D1A74DF99565C7F6573065041C8` | `2D0F9BB8300E81ACB07557424D384CC7E0ED3E929E9AF0D33E0C308CBEA2B067` |
| ARM64 ASan Debug r50 | `P1-live-samples-1002-ARM64-ASan-r50-qualified-2026-10-08` | `76776 / 0`; `08:01:39.931256`–`08:02:39.213681` | `8A3D4EFD4124727C3583AC2314D5AF3C5353A24BF6EA908A59CCD7B5D6901798` | `C8B2B54B90CCE14C172539E352489F51A4F30FA1214E14791324665496421C71` | `F7665AD09D1C49083DF5B34FCEF78DC950A586DF5895E7182C91075A51C64882` |

The source-fixed offline checks in `.build/P1Drafts/LiveSampleFullInventory-r1/{Debug404,Release405,ASan406}-verification.json` were checked against each archive's `archive-completed.json`, source/evidence manifests, and archived source/result evidence. The candidate source root in each report is the matching archived `Sources` directory; all three report the same candidate freeze above. The x64 aggregate stdout hashes in those reports match each archived `stdout.log`; their application-log hashes match the archived `RedPrism.self-test.log`:

| Profile | Aggregate stdout SHA-256 | Application self-test log SHA-256 |
| --- | --- | --- |
| Debug r404 | `1E0988405F7D5EAD773E900A31181A116E894156581378055E92ED8288D2014A` | `F8E303599951812D9166C0C44BE50393744E608A091440BECDFE1ACB49C8DBE2` |
| Release r405 | `5BFFC99241893F216FA89B4C2C74EF37B8C8CF52BCF8E6A15D00D903AE049A40` | `07FE5CDF228603120B1B0EAC7D89409ABDEAF4B6061792B03428C937D8CBD30A` |
| ASan Debug r406 | `59829DDCFA606EAB676AC5CF2DC6024765866A57FF3EEE8EE7E0666BC43E7E63` | `552BABAF3B2A1AE948DB18CB8457628216EF5623C38474C297BCC8577C6A3BF7` |

All three x64 profiles preserve the complete 4,607-record application predecessor inventory and 105 predecessor stages. Each adds 238 assertions and five summaries, yielding 4,850 application records and 110 stages. The Composition suites add 33 GPU-residency-growth and 21 command-stream-restart assertions, plus two summaries (54 assertions total), once per x64 profile. ASan's HostTests stress route does not rerun these Composition suites.

| Profile | Aggregate predecessor → candidate records | Stable projection predecessor → candidate | Separately checked callback records |
| --- | ---: | ---: | ---: |
| Debug r404 | 28,757 → 28,813 | 28,749 → 28,805 | 8 |
| Release r405 | 28,757 → 28,813 | 28,749 → 28,805 | 8 |
| ASan Debug r406 | 37,286 → 37,342 | 37,278 → 37,334 | 8 |

The eight callback records are preserved and checked as four exact assertion pairs in their original recorder cases. They are excluded from the stable projection on both baseline and candidate sides only to compare deterministic records; they are not part of the 54 new Composition assertions. The raw aggregate delta is 56 records: 54 assertions and two summaries.

ARM64 build stdout hashes from the completed receipts are Debug `80062EB9202270F044EDC49C05CD2A119C6F461FF1EFD1FD33D66D18CB9B3C80`, Release `7D986A7440743435C4AFF8A06126383DE76FFD48F3FD2832C950ADAC5DF6DD54`, and ASan Debug `6BDE049A7115225C78A0432D2948FB0F41B1BE760D3BC1C83AF8D718D4A8D63A`; all three stderr hashes are the empty-file SHA-256 `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`. ARM64 r50 is cross-compilation evidence only: Debug, Release, and ASan Debug build children exited zero on the same source freeze. It is not native ARM64 runtime evidence. Its recorded process intervals, and the x64 attempt intervals above, are not G1 measurements. Release analysis r407 subsequently failed; the preserved diagnostic and repair are recorded below. Normal activation, G1, and overall P1 closeout remain open. This record does not move P1 to Done.

### Release analysis r407: preserved stack diagnostic and repair

The actual analysis child 99264 exits 1 on the same 1,002-input freeze. Recorder bounds are
2026-10-08T06:30:19.812631Z–06:34:08.198788Z. C6262 reports a 17,848-byte frame in
Application.Tests.StrokeSampleAdmission.cpp::MakeClearScene: its clear 512x8 BGRA fixture had a
16,384-byte local array. Raster::Make(width, height, Bgra8888) already allocates and zero-fills
that exact source. The repair uses this existing path, changing no pixel, admission, state or tolerance expectation.

Archive P1-live-samples-1002-Release407-analysis-failed-2026-10-08 retains 1,309 verified copies.
Source manifest SHA-256: 4C9221A37966F2510F8F6D1606AEC8D15A1CEB0956C109FCE0999D40C4CBB3D9.
Evidence manifest: 9FCF137CAFB443D0D512675313A6E4DCB5836F956F6C4CD5377E2A6CBF238D5F.
Completion: 2CA4B72B894BE8663D7705D371FF67179B37A8FF126D7CDBBEB9B659A6777766.
Stdout: A7C63900E613A845EB735DC9F4C10A91C40B0305D85980AC3F2937D6A2CA0A33.
Stderr: A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7.

No runtime or successful analysis is inferred from this failed build. r404–r406 and ARM64 r50 remain
historical exact-source passes. Fresh qualification includes the repaired fixture and the next private
recorded adjustment graph. Normal renderer activation, complete memory peaks and actual G1 stay open.

### Recorded adjustment graph and immutable ticks r408–r415

These original completed receipts are archived under `D:\RedPrismQualificationArchives\`. All attempts use 1,004 source entries. The source freeze is `D5085AF6177FAC802D592CE303305065192C8D1097697F645305CA42F99DC6D6` for r408–r410, `D6CC3AE8B869CA454F51644335CACE4A1EB0D463833E318F2E731D743ECC695D` for r411–r412, and `D168BCB649040F2A2B8337C02C9581D9445BC3DAAEFE546F00CA44F8357FC336` for r413–r415.

| Original archive | Actual child (PID / exit) and recorder interval (UTC) | Source manifest SHA-256 | Evidence manifest SHA-256 | Completed receipt SHA-256 |
| --- | --- | --- | --- | --- |
| `P1-recorded-adjustment-1004-Debug408-build-qualified-2026-10-08` | `115688 / 0`; `06:37:01.454418`–`06:41:46.074842` | `B6F18380B4706697A2EB7FCEF9ADC4B9C9336FE3C4C61B31754B9C8C39BF1411` | `A6FCC0C760D0E2852C1DC282E6F15E1260757CDAE23D97D1E5B8E0E3832373D9` | `73DB56D895F944FF326B19E3121CDED53DD1578920ABF870FAA813A6C1099860` |
| `P1-recorded-adjustment-1004-Composition409-qualified-2026-10-08` | `86740 / 0`; `06:38:28.633165`–`06:38:36.618912` | `3E1B6562534D16378E516E119FFBC8FA63AA497A6024F9ECDAA1E5F762581963` | `50895433BE4F903A74DEA0D966EABC39CBB82735284EE7192C88445ACFF5A80D` | `0A79982D554A4CA1D2F6718EC1FDAFC28A2B08924ADA21B0F69FFECF34E7AFF9` |
| `P1-recorded-adjustment-1004-Release410-analysis-failed-2026-10-08` | `119564 / 1`; `06:42:33.274033`–`06:46:24.655861` | `9426EB30F1941319236CED8292979B0B775E4DA81D31AB87962CF271952C6424` | `28C99BE38635687CD4A4243CC083437DA273B51F0EC4AABDA69560002C55CED0` | `3A0B2E260742E813617F920976CF066E5AB5CC8C3493783D451FF6377CE8C560` |
| `P1-adjustment-ticks-1004-Debug411-build-qualified-2026-10-08` | `115996 / 0`; `06:49:44.530278`–`06:53:51.726462` | `4B73AAC841387E3AA302B5CD62C6FAF935B6A6C0C3995D8D390B88F1823C7AE6` | `9739C423BAF23138AFA8F2651D91C01AB0189EAD9C5E6874D9BFF01AA6FCFFC2` | `E15CCC190CD1160941B81FBC0FA8C84AA769FCC789113CAE187B2F47FEB07BF5` |
| `P1-adjustment-ticks-1004-Document412-failed-2026-10-08` | `106484 / 1`; `06:51:34.713623`–`06:52:05.677535` | `32B76B019FB30822964D99DDFF0B42EA9F12DA00C53319575566386F3DBF2C6E` | `E671CFC30D4E40C8A51FD9B905FC505A9036C84BEFF33B865A726B5787BD38D1` | `E21B48A37E2CFD92414DC88E20663A593A6CA2528B400C7C4598B9ADCA91B12D` |
| `P1-adjustment-ticks-1004-Debug413-build-qualified-2026-10-08` | `116060 / 0`; `06:55:59.855149`–`07:00:09.838324` | `B41402B4DC88D21FF32B9CEBC84FDC35EB4CFE0E805C3E76B48A4C9D2A7D8E14` | `57AFCDA68366065AE5BB998D45BAC86050EBFA36B38C28CB72DA1E96E6D3DE68` | `E072552945FB8352AF96FA93A5EAE99D647C0B158A37197272857F68D4E85245` |
| `P1-adjustment-ticks-1004-Document414-qualified-2026-10-08` | `86640 / 0`; `06:58:22.311710`–`06:58:52.920136` | `B1919687E6DC121723B7BAE20A18E26FBCAE99850C868A5984B722B9F5DE58DC` | `4C111E06C0308E2314D72909A91F6AD7FFC45FD03B13B699C7DDE3B311D69537` | `9E3F4703CEA04CBD1ABEEEFC826BE5AE20AE4FBB1468CA20504AA4855B7A4F79` |
| `P1-adjustment-ticks-1004-Application415-failed-2026-10-08` | `121076 / 5`; `06:59:52.654081`–`07:01:06.749273` | `A473E99A58C67923768BE7269178803FEA5BC0B6671A0F05AC01C442BEA80D84` | `E1C136F719B2BF664E460F03BC9B51356417E2219FDA20F1750E90F24A049771` | `905D17408CCE28FEA206CF816778C1EB68252CE5A457FB249A85EA7F0D1414FE` |

The recorded CompositionTests child r409 passes eight adjustment cases across WARP and hardware. Its fixed legacy controls remain separate: 33 GPU-residency-growth and 21 command-stream-restart assertions passed. Release analysis r410 fails C6262 in `SourceHasNoPublishedPixels`: the 16,384-byte local caused a 16,408-byte frame. The repair uses a 2,048-byte row; literal-zero checks are unchanged.

DocumentTests r412 passes all 72 new adjustment-tick assertions but fails six existing `cancel-preview-on-live-literal` controls because immutable ticks also reached direct-reference mode. The `_colorDisplayJobsEnabled` guard repairs that routing without changing numeric goldens. DocumentTests r414 then passes all 72 new assertions and the original literal document tests.

RedPrism application r415 exits 5 after 23 stages at `sampler-caller.picker.adjustment-shadows.first-ack-renews-own-source`. The later repair keeps that assertion name and requires `_session.Generation() == generation` plus `layers.Identity() == sourceRoot.Identity()`; literals `0xFF636363`, `0xFF4B4B4B` and `75/255` are unchanged. This repair is not qualified by r415.

Receipt verification recomputed each source-manifest, evidence-manifest and completed-receipt SHA-256 and confirmed their completion-record bindings, including the attempt freeze hashes. For every archive, the archived `result.json`, `source-freeze.json`, `started.json`, stdout/stderr, and applicable suite/application logs were present and matched their evidence-manifest length and SHA-256. Result records identify the expected build, `CompositionTests`, `DocumentTests`, or `RedPrism` child; runtime results retain their recorded binary hashes and `BinaryUnchanged` status. These are attempt receipts and focused qualification only. Ordinary activation, G1, and P1 closeout remain open.

### Accepted adjustment ticks r416–r420

These original completed receipts are archived under `D:\RedPrismQualificationArchives\`. Each records 1,004 source entries. The source freeze is `23CCC4D161F8C766A3DC8A14488024FC386FFE790FF05E5F0A26C28884FD9A46` for r416–r417 and `398965485A003CD68D72190F6FC6D51DDC5BFEE7C4E55019A9C8CCC3C78BCF48` for r418–r420.

| Original archive | Actual child (PID / exit) and recorder interval (UTC) | Source manifest SHA-256 | Evidence manifest SHA-256 | Completed receipt SHA-256 |
| --- | --- | --- | --- | --- |
| `P1-adjustment-ticks-1004-Debug416-build-qualified-2026-10-08` | `35932 / 0`; `07:08:50.999951`–`07:13:08.759613` | `424522FD1A99118AE63DF4E407A5EDD096757CA0A730CA1B27F5A25B7452A8CE` | `468FB395C2E1B42CB7F7F3DBB656395EE73F5AD8AD761DE4216069145267A043` | `37CFC9FF891AE6EB416BE781064B1D3D4967BA1FD200058F373D47DF1B8F724D` |
| `P1-adjustment-ticks-1004-Application417-failed-2026-10-08` | `7152 / 5`; `07:10:13.390829`–`07:11:43.865573` | `85719931D4AB9446EEC35FC2CF93E47F2F64A4AD15790D568C7966C2E163BE2C` | `D6764E1BA9CA1F01C95191049B6980057E966D6651E09130B01A4070EB6A6E69` | `24E3B2638AA53BB6F52120E078A83624AB58D8CFB46D4B03F744915A786A48C8` |
| `P1-adjustment-ticks-1004-Debug418-build-qualified-2026-10-08` | `28884 / 0`; `07:15:35.154170`–`07:19:48.243077` | `905C6BD59ADEA0C7E489C626CE7C338AB303CAC0274320A670E4FB110BA3CE32` | `14E8683DF1B8A392C0FF05036B5917A109C4FA017B7609D209AC0BA5C7DBFFFC` | `9007436DE8793691EEEFDADBE571E1C9D9256EDB85D0E0CC94E42E48F0E23311` |
| `P1-adjustment-ticks-1004-Application419-qualified-2026-10-08` | `18332 / 0`; `07:20:53.870036`–`07:22:55.196894` | `3CFD7FA6A06C2BCB6442BDDFEF0506A59D44B73C7F1004C132A3272BD89255B1` | `3F843E297761C237C058154C568874469EA21EB9E27632FEBBE1BB7620B8434A` | `98A74506B0AC1FDE0D251CEFDB805068296A376DC83FAD2CFDA5DA453A79C165` |
| `P1-adjustment-ticks-1004-Document420-qualified-2026-10-08` | `39660 / 0`; `07:20:53.864917`–`07:21:25.201924` | `7CBCDE27BF05E0DAB02FA125375D8EB48580B599DE6C96FB078B598F55415B66` | `9E7F80B51A1EF3DD6EB2101BDE7171AF6A12827C7A98A7ED5DC1045A26397377` | `96C8CDE538E7C68736E58303393A6CEA0C4D14356E774903AAD61B85F4AEA7F9` |

The r417 application fails after 24 stages: the live adjustment case expected the legacy red-to-green result, but the red beneath the adjustment did not rotate live. `Common/ColorEditWork.cpp` in the r419 source freeze now combines the original `liveAdjustment` with accepted `edit.settings` using `Adjust::AdjustmentWithSettings`, then builds the worker lookup with `Adjust::BuildAdjustmentLookup`. The strengthened worker-lookup assertion checks the lookup equals the expected current adjustment and differs from the original. Its sampled-value goldens are unchanged.

The r418 Debug build qualifies that repair; the r419 application passes all 110 recorded stages, and the r420 `DocumentTests` run passes all 72 adjustment-tick assertions. The r419 application and r420 document result records identify their expected suites, unchanged binary hashes and exact source freeze. Their archived result, source-freeze, start, stdout/stderr and applicable test logs match the evidence manifests; all five completion records bind to the recomputed source/evidence manifest hashes and freeze hashes.

The fresh x64 Debug r421 child exited 0 at `2026-10-08T07:32:45.134584Z`; its completed archive receipt is recorded below. Main’s independent Historical421 inventory comparison is also recorded below. This attempt does not qualify ordinary activation or G1 and does not close P1.

### Release analysis r422: preserved C6011 and bounded repair

The original completed archive `P1-recorded-adjustment-1004-Release422-analysis-failed-2026-10-08` is under `D:\RedPrismQualificationArchives\`. Its archive acknowledgement is `2026-10-08T07:39:07.6848947Z` (the completion record's `CompleteUtc` is `2026-10-08T07:39:04.4331761Z`). The x64 Release analysis child PID `106488` exited `1`; recorder interval `2026-10-08T07:34:04.506621Z`–`2026-10-08T07:37:53.365247Z`. It uses 1,004 source entries at freeze `398965485A003CD68D72190F6FC6D51DDC5BFEE7C4E55019A9C8CCC3C78BCF48`.

| Source manifest SHA-256 | Evidence manifest SHA-256 | Completed receipt SHA-256 |
| --- | --- | --- |
| `6144173830AFA8E419FA71BBA09FF594BFBCC8608BDC57470AC13F32E0D37E4F` | `9E5E0EA6144B7EF4C17726CF025CDB0FB85583A6E257501F23DCF8D686A41F2D` | `1A201FBD2AA34E200A435E063FD0A8422CCA59DED472BEB1C8785EE26DBAE941` |

Stdout SHA-256: `D26013DB2819E6838334C4C5D4E0CA7DB679FC3C0FEE4D598741B560F2C8A97A`. Stderr SHA-256: `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`. The recomputed manifest and completion hashes match the completed record and freeze. Archived `result.json`, source freeze, start record, stdout and stderr match their evidence-manifest lengths and hashes; the archived `ApplicationStrokeFinishJobs.cpp` source copy also matches its source-manifest hash.

Release analysis reports C6011 in `CaptureFinalOwner`: the conditional `stroke ? stroke->PlanFinishMemory() : std::nullopt` did not prove `stroke` non-null for later uses. The follow-up adds explicit initial-stroke and `PendingOpening` guards, then rechecks the exact mutable-brush pointer and `PendingOpening` after `IncreaseReservation`/`GrowGpu`, and requires a non-null expected engine before use. Bounded refusal returns `E_ABORT`; no warning suppression or tolerance change is involved.

The subsequent r423 Release analysis on the new source failed at the linked ISA scan; its receipt is recorded below, with no scanner qualification claimed. The r421 full Debug pass and its independently verified inventory comparison do not qualify the r422 repair. Ordinary activation, G1 and P1 closeout remain open.

### Full Debug r421 completed receipt; inventory comparison verified

Original archive: `D:\RedPrismQualificationArchives\P1-recorded-adjustment-1004-Debug421-full-qualified-2026-10-08`. The actual x64 Debug test child PID `29456` exited `0`; recorder interval `2026-10-08T07:25:02.728877Z`–`2026-10-08T07:32:45.134584Z`. The completion record contains 1,004 source entries and 315 evidence entries, with 110 recorded stages, on freeze `398965485A003CD68D72190F6FC6D51DDC5BFEE7C4E55019A9C8CCC3C78BCF48`.

| Source manifest SHA-256 | Evidence manifest SHA-256 | Completed receipt SHA-256 |
| --- | --- | --- |
| `4C7634BF7789CCB4A2E33DC9E509A5FBA9AB71EC7C6DF991CB17570EEE370073` | `EC4E56DA0BDFE50E9DACC7B4F851F1BBC756AC8F6A7E5A18C9D542D1EFC02C41` | `07850C723B88E2AE4549E5FA6EB12EA44F0562A872517E3F36F4E7488E9951E0` |

Stdout SHA-256: `2B763EA04077CD71FDE3D2BC657A65BFFA632AAE038917809DA4B789B80202A6`. Stderr SHA-256: `C40DC7D437BC14A3C2A6416D465434EF657F3942B71763483E1B6A990B641A70`. The source/evidence manifest and completion hashes bind to the receipt and source freeze. The archived result, source-freeze, start, stdout/stderr, and in-interval `RedPrism.self-test.log` copies match their evidence-manifest lengths and hashes; the application log SHA-256 is `0C8AE3BF41EA17F9A7BB40B0FF5D0C8C212A883F207C1E08DF85EF8D940FB12D`.

Main independently reran the source-fixed Historical421 verifier (report SHA-256 `FFA4792B57D1DD20182B9C5AA0768F5770220D97983E7D798BD134A1D51FB166`). It verified all 1,004 archived source files and preserved the application inventory at 110 stages and 4,850 records. The full projection is `30,259 → 28,827`, equal to the old baseline projection `28,827`. Across 16 WARP/hardware runs, actual callback acknowledgements were HSV `73`; levels, curves, exposure, gradient-map and grain-half-scale `40` each; and live-exposure and live-grain `33` each. This is source-bound offline inventory verification, not ordinary activation or G1 qualification.

### ARM64 r51 cross-build receipts

Main’s independent archive verification reports 1,004 source files for each profile, 72 Debug and Release evidence files, and 76 ASan Debug evidence files. All three actual ARM64 cross-build children exited 0 on shared freeze `2AC9B711520FCC12C96699185792D250152A890E2B3C3D22FB359DBD1834912B`; native ARM64 runtime was not run.

| Profile and original archive | Actual child (PID / exit); interval (UTC) | Source files / evidence files | Source manifest SHA-256 | Evidence manifest SHA-256 | Completed receipt SHA-256 |
| --- | --- | ---: | --- | --- | --- |
| ARM64 Debug; `P1-recorded-adjustment-1004-ARM64-Debug-r51-qualified-2026-10-08` | `52340 / 0`; `07:50:34.455917`–`07:51:41.460834` | 1,004 / 72 | `BD07B886BC2A7B3FFF487E17B14BCDA9F2AC414CCB2CC8745CB5E89FA0D84150` | `23C1817F47011A943A0CB4B06647DF14304D7D8D871429AC2B6354317D64976E` | `32EEBF776EBA280F2F171AF135A2F1A6BB80D2B492445066CC717A086D0268F6` |
| ARM64 Release; `P1-recorded-adjustment-1004-ARM64-Release-r51-qualified-2026-10-08` | `66568 / 0`; `07:51:52.032992`–`07:53:20.255510` | 1,004 / 72 | `DC02AA6A08D98B72D648369E263DE246F20FE46DD4E293DBB06F3112C06D087B` | `67A7792F89569B1231AF242CB34A68B8B1ED55A927D43F83F5309E4E12177C69` | `7A874128E8592CE2E11C8B74E36696A544D276889C6D867E494EB5A0CB8E0A04` |
| ARM64 ASan Debug; `P1-recorded-adjustment-1004-ARM64-ASan-r51-qualified-2026-10-08` | `86724 / 0`; `07:53:29.728811`–`07:54:28.436435` | 1,004 / 76 | `86F0B84ADE339CB9B19F5B7D5910AFB45B297DD3766206BC5DCCE567A375A23D` | `8C5BB0A36F5189C15E12FF656E491B4DF2978965C2BAE1A0E63B64B9E6E123D2` | `7DA424EC76000565460573294734B78FA0B2E0CE55B4F8D4D5A1674BB4C89073` |

The main independent verification report is `.build/P1Drafts/Arm1004-r51/main-independent-verification.json` (SHA-256 `BE5E1A51C056CF95734B0AEC2648200CC77606D5162D97A949BAA699BE4297EA`); the recorder result is `.build/residency-arm-builds-r51/result.json` (SHA-256 `8B37812EA84179B4BF1CA6AE1F33C7025F48CAEBB54684EC30A51F4E4C2CCA4E`). Recorded stdout hashes are Debug `B924C1D2ADB1FFAF60CDD69ECA8DEA1649B40C89FA311B2941BB80922D77B77C`, Release `38059497C390C0C806D26DA3E8B94CEAAC2B7675A6D75B28247733D020C2CCE8`, and ASan Debug `5A1F47C1CACF7D6A9BA27DB08F42F8FAE471713DD0434636888CF6B541A87ADC`; stderr is empty for all three. This is cross-build evidence only.

### Release analysis r423: linked ISA scan failure

Original archive: `P1-recorded-adjustment-1004-Release423-analysis-failed-2026-10-08` under `D:\RedPrismQualificationArchives\`. The x64 Release analysis child PID `118520` exited `1`; recorder interval `2026-10-08T07:41:08.033285Z`–`2026-10-08T07:45:28.845362Z`; completion record `CompleteUtc` `2026-10-08T07:47:12.4828763Z`. It records 1,004 source entries on freeze `2AC9B711520FCC12C96699185792D250152A890E2B3C3D22FB359DBD1834912B` and 317 evidence entries.

| Source manifest SHA-256 | Evidence manifest SHA-256 | Completed receipt SHA-256 |
| --- | --- | --- |
| `ED406E31366CA6C760EFD8EDDC4F454E8FD7EB2442C8CB7287039D55B921B316` | `6DDD8387AFE523B0107CFD6E99FA871FDEDA4522199FA4DF530969E075FEE52B` | `7BE7821F880C0E10DD1A078432D0A0732AD100E14DECFA6A869A1E28101D07DE` |

Stdout SHA-256: `8DC5A00EF9E10E78663F45F42C1E695D70A23FD3080515F4DD1C6137833E5FFC`. Stderr SHA-256: `1EB271698E0DE7F69812BD0A57882903BD59619B07C42ACB023D1FA6C3F3E7C1`. The completion record binds the recomputed source/evidence manifest hashes and freeze; copied result, source-freeze, start, stdout and stderr match their evidence-manifest hashes and lengths. Archived stderr records `ISA scan rejected ... RedPrism.exe`. This is a failed analysis receipt; scanner qualification is not claimed.

The associated unchanged-image proof accepts those four table entries only. Ordinary activation, G1 and P1 closeout remain open.

### Terminal R13 scanner controls and fresh qualification

The unchanged r423 PE (`FF5A75AC8A579A35BF857C6CB1496AE61FBE31026C8F39A96810A40748D36C24`) and PDB (`7AEBC6CE27DF84138D3E75FE7AF2A6492472EFE03D89A21D132B7766E21E3617`) identify a terminal DWORD switch table. The four repeated `c5 99 12 00` values are RVA `0x1299c5`, a decoded scalar body target. The proof requires the exact guarded R13 dispatch, matching PE/listing bytes, exact same-owner PDB data extent, contribution containment, decoded body targets, continuous scalar predecessor and table rows, and no direct control-flow entry into the protected bytes. It does not permit vector code in that function or change `IsaRuntimeAllowlist.psd1`.

The unchanged-image strict scan records 1,170,242 instructions, 470 vector instructions, six verified inline-table vector-data records and zero violations. The original failed scan remains preserved. Main reruns the existing parser controls separately and reviews all new controls before canonical registration in `IsaScanTests.ps1`.

The first reviewed new fixture run records 101 passes and 14 failures. Main identifies the near-branch array conversion, a target corruption that removed all candidate vector bytes, and three correct `InvalidDataException` refusals that the fixture expected as return values. The repaired fixture retains typed branch bytes and unchanged offending table cells, and accepts only the three designated exact exception types/messages. Its standalone run exits zero with `controls=115 accepted=5 refused=110 failures=0`. Both raw runs and the reviewed scripts are preserved under `.build/P1Drafts/IsaR13Terminal-r1/`; this is parser proof, not a native profile pass.

The registered source passes full Release r424 on 1,005 frozen inputs. The remaining current-source profiles and analysis remain due; private pending-display drafts are excluded from this producer.

### Full Release r424 completed receipt; inventory comparison verified

Original archive: `D:\RedPrismQualificationArchives\P1-recorded-adjustment-1005-Release424-full-qualified-2026-10-08`. The actual x64 Release test child PID `119864` exited `0`; recorder interval `2026-10-08T08:22:51.955640Z`–`2026-10-08T08:30:27.204776Z`. The archived start record invokes `test.ps1 -Platform x64 -Configuration Release` without a scope selector, and archived stdout ends with `All tests passed.` This is an actual full Release profile PASS, with all 110 application stages recorded in the same order as the completed receipt. Archive completion `CompleteUtc` is `2026-10-08T08:31:36.6659117Z`.

The receipt records 1,005 source entries and 305 evidence entries on freeze `92EAD176E338C79AD146D9FAD88E989B583EED8601C8222F8068D9D586A12706`.

| Source manifest SHA-256 | Evidence manifest SHA-256 | Completed receipt SHA-256 |
| --- | --- | --- |
| `91435B06CE56AEE38F1BE6B59FF86EE108D73A3EB34F9DEE0ACE79E8AA7F21AC` | `15EF5C05E84D01B4242B5E81D4066E9ADFE0EEADC6348F285C69A75384C9C5B3` | `188CAA73A9FCC7463805C32FAECED31AAE1E32FF58F3BA4F87E7BD7612FDF650` |

Stdout SHA-256: `56D2008FB7E6D839A4CCAD45BD4CB4741AE2559E80691991BDF251378F6F9212`. Stderr SHA-256: `D7F97ADC7237AC054724A7CB432D37B0679E9B4002C319107F582231292320CC`. The archived `RedPrism.self-test.log` SHA-256 is `5AAF14FA4A7BF6F76271F037F1E2CB7C4F6DFA25527DEC27AF43D9A62F19EFB7`, with receipt-recorded write time `2026-10-08T08:30:02.5390614Z` inside the actual child interval; the application error-log SHA-256 is `AD8108E8762ACCD89D86221E7D6B9BC18FE3D6C0B56E637E94CF499707870311`.

Main’s archive verification report is `.build/P1Drafts/RecordedAdjustmentInventory-r1/Main-Release424-archive.json` (recomputed SHA-256 `3CD27F870894407328BAE9291EC6EE0B8E63D56099CF87FF4063C6AC5D78679F`); it reports PASS for 1,005 archived source files and 305 evidence files. This documentation read-back independently recomputed the manifest/completion and relevant copied-artifact bindings, read the archived actual `result.json` (SHA-256 `202F5F2B65DA57821352FCC25C6EDF4806D62AC5AD00B160DC096BA30F34D584`), and matched its entire attempt tuple to the completion receipt and main report. All 1,005 freeze entries agree with the source manifest; the archived result, freeze, start, stdout/stderr and both application logs match their evidence-manifest lengths and SHA-256 values. The 110-stage application log agrees with the receipt from `RunStandaloneCanvasLifetimeShutdownSelfTest` through `RunRenderingSelfTest`.

Main reviewed the new literal-source terminal-window verifier and independently reran its synthetic checks and actual Release424 archive comparison. The complete stream has 30,950 records; removing only the proven 691-record R13 window leaves 30,259. The separately checked eight callbacks and fixed recorded-parent/live-tick additions project to all 28,827 original Release records in order and multiplicity. All ten original shared ISA windows and the application 110 stages / 4,850 records remain unchanged. Twenty-nine window mutations are refused, including missing/extra/duplicate/reordered cases, failed cases, malformed exception labels, markers and unknown INFO. The independent main report is `.build/P1Drafts/RecordedAdjustmentInventory-r1/Main-Release424-verification.json`, internal report SHA-256 `D564A62EF37526CFF3C274258D25FAEC0583DB7766D46F3ADCE44B2028C4169E`.

The other profiles on this source remain due; ASan425 is active at this update. Ordinary activation, G1 and P1 closeout remain open.

### Full ASan Debug r425 and Debug r426 completed receipts; inventory comparisons verified

These original completed archives are under `D:\RedPrismQualificationArchives\`. Both actual x64 test children exited 0 on the same 1,005-input freeze as Release424: `92EAD176E338C79AD146D9FAD88E989B583EED8601C8222F8068D9D586A12706`. Their archived start records invoke the canonical `test.ps1` with platform and configuration, without a scope selector; both stdout logs end with `All tests passed.` These are actual full profile passes. All intervals below are recorder intervals in UTC on `2026-10-08`.

| Profile and original archive | Actual child (PID / exit); interval (UTC) | Sources / evidence | Source manifest SHA-256 | Evidence manifest SHA-256 | Completed receipt SHA-256 |
| --- | --- | ---: | --- | --- | --- |
| ASan Debug; `P1-recorded-adjustment-1005-ASan425-full-qualified-2026-10-08` | `90460 / 0`; `08:31:58.298547`–`08:45:02.397618` | 1,005 / 306 | `B1ED60102079A0F963D599DD130D9203CF052B86B42A417D32970371C3232125` | `C5D5529FA7AE1B71FBC834F2E71B299B09D4DA9C34D790893B6B33B968B28549` | `C30769FCE7F1E7F86B82A46AEA6002BF8E672F65AA61B0395D5E307E04DAD84A` |
| Debug; `P1-recorded-adjustment-1005-Debug426-full-qualified-2026-10-08` | `86988 / 0`; `08:46:41.078417`–`08:55:19.855756` | 1,005 / 317 | `27D825EDF2E87F87370A9B11E11D3439B51681F6253C0F4B4BC0867DFC9A7176` | `BD8FC31CF55E43010F8FE1E4E2155FEC6276642117C674A19599C938ACF0E596` | `737C7C58047D6BE2D751AF8868699A51479438528E020F208D0A0F57E459ADE9` |

The completion records' `CompleteUtc` values are ASan425 `2026-10-08T08:45:38.7368501Z` and Debug426 `2026-10-08T08:56:00.8133007Z`. Their application log write times are respectively `2026-10-08T08:44:19.1621318Z` and `2026-10-08T08:54:54.1199976Z`, both inside the actual child intervals.

| Profile | Stdout SHA-256 | Stderr SHA-256 | Archived application log SHA-256 |
| --- | --- | --- | --- |
| ASan425 | `F317C46F336088C987642E4BDE8CF2D1F9EA208656FCE5C62B5688AA2E31D544` | `84931B9ADCC6695858B8CF188990B7EC4E063C1601E3EA976F2D9F5E7A92FFA1` | `DDCD9B1A509010AC0F6CA37945681A00B771104C30F251ECB9F963B539FF7A20` |
| Debug426 | `46C4D91DFEFC092FB169284429285454FCA57F3714C3E99222C08E53394362A6` | `08C1E08FA768558F989E1362D0DC25E65C24C64076A29F1D7CC35D44D9EA8A7F` | `5181C1718A34A06305C0E0355F084590ED5310D8909AE96C4B67FEF8A5A463C5` |

Main independently verified all 1,005 archived source files and 306 ASan / 317 Debug evidence files. Its reports under `.build/P1Drafts/RecordedAdjustmentInventory-r1/` have these recomputed file SHA-256 values:

| Profile | Archive verification report / SHA-256 | Source-fixed inventory report / SHA-256 |
| --- | --- | --- |
| ASan425 | `Main-ASan425-archive.json`; `E30EDC29BAFF5690E0D66CC543515830AF570731AE3D2C1210334EC0051C7612` | `Main-ASan425-verification.json`; `DC7F4DCEBBB4017AD65A03DC3C4039A4855AE44E6EDE750EDE715B099BB46C1C` |
| Debug426 | `Main-Debug426-archive.json`; `97F811D1A296EC0441492FE2C34DF715CA504D3E09B454DCFC82C2FA03264B80` | `Main-Debug426-verification.json`; `04EA74CC0A86038B583A6F7D67D61CB5EBDE6ED6E7A1A53AE4415C2FDFEABD99` |

Both main source-fixed inventory comparisons pass with 25 source pins. Each preserves the application at 110 stages and 4,850 records. ASan projects `39,479 → 38,788 → 37,356`, equal to its original `37,356` baseline; Debug projects `30,950 → 30,259 → 28,827`, equal to its original `28,827` baseline. The first reduction removes only the proven 691-record terminal R13 window; the final projection separately checks the eight callback records and fixed recorded-parent/live-tick additions. Each report preserves all ten original shared ISA windows and checks exactly 115 terminal controls: 5 accepted, 110 refused, 0 failures. The canonical stdout logs contain that exact terminal-control summary. ASan425 stdout records `PASS AddressSanitizer detection probe`; both profiles record passing main/worker crash probes and all nine harness screenshots.

This documentation read-back independently recomputed the manifest/completion and relevant copied-artifact hashes, matched each archived actual result's complete attempt tuple to its completion receipt and main archive report, and checked the inventory report's receipt/log bindings. Archived result SHA-256 values are ASan425 `7110BEF68FF1CE1EF4572C80559945446F0EC29EDCCD81DC6C56F1FB99D05780` and Debug426 `7D42064EB690E61B74E0F0B6CCE1F20F75F6B21D5DAEACE37AE687FAD06C8FC4`. Each archived result, source freeze, start, stdout/stderr and both application logs matches its evidence-manifest length and hash; both application error logs have SHA-256 `AD8108E8762ACCD89D86221E7D6B9BC18FE3D6C0B56E637E94CF499707870311`. All 1,005 freeze entries agree with each source manifest, all 25 archived inventory source pins were rehashed, and each application log independently contains the receipt's exact 110-stage sequence and 4,850 inventory records. The nine copied Debug screenshots also match their evidence bindings.

### Release analysis r427 completed receipt; archive read-back verified

The original archive is `D:\RedPrismQualificationArchives\P1-recorded-adjustment-1005-Release427-analysis-qualified-2026-10-08`. Its actual x64 Release analysis child (PID `52620`) exited `0` from `2026-10-08T08:57:06.017703Z` to `2026-10-08T09:05:07.483851Z`. The archived start record binds `build.ps1 -Platform x64 -Configuration Release -MaxCpuCount 2 -Analyze`; this was incremental focused analysis with no forced rebuild or G1 timing claim. The actual result binds 1,005 inputs to freeze `92EAD176E338C79AD146D9FAD88E989B583EED8601C8222F8068D9D586A12706`.

| Source manifest SHA-256 | Evidence manifest SHA-256 | Completed receipt SHA-256 |
| --- | --- | --- |
| `B7D8F63AB132A8D109C4CBE91DD18C3CA0D2863BF135DE9E72C2F3671F42F26B` | `142F2AC596F14593E38AAC90D6C241278CE9BC1EF7F90C096FD637AE26C59B01` | `6CF22AD44A7A1193074898142B34B3E35447FC02C7529BA089F591C4E4429C02` |

The actual result SHA-256 is `5A9722E4A862CE3C55B626E279C27131BE211348764D3AB1D24720D52DEF9804`; source-freeze `92EAD176E338C79AD146D9FAD88E989B583EED8601C8222F8068D9D586A12706`; start `688716A5D8E2E60D5278D669FB178E9F03FD3C3CC37229680B5518F3AA301494`; stdout `794A66E369E141075EF7389BEA9326DCBE2379C7909ABFDA543F3BCA48559AE9`; stderr `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`. Main's independent archive report, `.build/P1Drafts/RecordedAdjustmentInventory-r1/Main-Release427-archive.json` (SHA-256 `3CCC8BE0DC569B5636D52C0BD54EB96CED55360FCAB6994E1E21C08391C3DD93`), records PASS for 1,005 sources and 319 evidence files. This read-back also rehashed every manifest-listed copied source and evidence file: 1,324 checked, zero mismatches; the copied result, freeze, start and logs match the receipt and manifest bindings.

This completes the Release analysis r427 record only. Ordinary activation, G1 and P1 closeout remain open.

### ARM64 r52 cross-builds completed; archive copies and intervals verified

The three actual ARM64 build children exited `0` on the 1,005-input freeze `92EAD176E338C79AD146D9FAD88E989B583EED8601C8222F8068D9D586A12706`, using DxUi pin `8e8994079e82f79e6765e6a885c03fc687957591`. Main's archive reports pass for each profile; the driver/launcher and actual kernel-child interval bindings pass in `.build/P1Drafts/Arm1005-r52/Main-driver-interval-verification.json` (SHA-256 `95A67B2004412A649BA715EBABFFB2F7BF1357EE69ED2611E165C53CFE98F64A`). Bound driver SHA-256: `218ED579BC91A03B03D149B9303219B94200204E53435F43A26B411102D1AFAA`; launcher SHA-256: `FF2770746294ED5ECCD5E8F5C06D60A266D09A53B157651C9870809E6D86E972`.

| ARM64 profile | Actual child (PID / exit) | Actual child interval (UTC) | Sources / evidence | Completed receipt SHA-256 |
| --- | --- | --- | ---: | --- |
| Debug | `98044 / 0` | `09:07:20.792937`–`09:07:35.916082` | 1,005 / 72 | `80713A1678AEC51A5179F1E906E899F75116DFA00B14D2934DADE6C532DE735C` |
| Release | `118928 / 0` | `09:07:46.387445`–`09:07:59.334438` | 1,005 / 72 | `D55329E600D2E3E7A00EBF5AAFD3AFBB919DABC9D4EFD29536A13CFA34162C35` |
| ASan Debug | `97732 / 0` | `09:08:09.163468`–`09:08:22.220171` | 1,005 / 76 | `52880C981DBED9653E99875ED5353A7B93C018E70A3D6752D626C42E2C4762D8` |

| Profile | Source manifest SHA-256 | Evidence manifest SHA-256 |
| --- | --- | --- |
| Debug | `13B794368C3477B5F2CA01CC07EEE98B9CA90CB036E6FD684E99D25397515D18` | `F51A304A823095F695659884D7AB6B80C7199F1058619DFB9781BA288A7EDC39` |
| Release | `29400B5E8823AA90B9D150FF711D56590C383131323AE2137A74895C0FC5048C` | `27FD8DB8797A2F0BEEC0DDE2EEF24A80CF8329295EC5CC85960BD33EBA5438EB` |
| ASan Debug | `19916C31295690012C91A3F8438D2BB5823AB867D41A1963294198348B84C1BE` | `F9CA8864B5C529BA631199A32CD373537C6D9B3CA50E0B8131EF1536493788B5` |

For each named original archive in `D:\RedPrismQualificationArchives\` (`P1-recorded-adjustment-1005-ARM64-{Debug,Release,ASan}-r52-qualified-2026-10-08`), the archive-completed receipt binds the table's profile, PID, exit, interval, source/evidence manifest hashes, input count and freeze. All 1,005 copied source files per archive and all 72/72/76 copied evidence files were rehashed against their manifests with matching lengths and SHA-256 values. Main independently verified these copies in `Main-Debug-archive.json`, `Main-Release-archive.json` and `Main-ASan-archive.json`. These are ARM64 cross-build results; native ARM64 runtime was not run.

Release analysis r427 and all three ARM64 r52 cross-builds are completed. Ordinary activation, G1 and P1 closeout remain open.