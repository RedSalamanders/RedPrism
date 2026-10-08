# P1 Clone worker opening

Status: integrated implementation and Release analysis pass; runtime/cross-build qualification pending
Current candidate: full x64 Debug r356 passes; fresh Release analysis/runtime, ASan and ARM64 cross-builds pending.

## Change

The admitted Clone source now precedes the ordinary typed opening worker for both All Layers and
Single Layer. The opening admits the immutable sample's cached ownership, captures the event-time
offset, constructs the private draft and selection coverage on a worker, and attaches only the empty
coverage engine after current authority checks. Replay uses `BeginPreparedBrushStrokeAt`.

Opening does not migrate the first pointer sample, later Append, final coverage/healing/history,
preview uploads, display or layer rows. Those remain P1 obligations. No G1 result is claimed.

## Independent references

- Existing actual sealed All Layers Escape/Undo and opacity-predecessor scenarios retain their exact
  receipt/ordinal, literal red destination and transparent Undo expectations. Each additionally
  observes one source capture, one off-owner preparation and off-owner source retirement.
- The Single Layer fixture starts with literal red/blue halves. A real worker opening is held after
  preparation, with the source charged and no session brush/history publication. The Clone source
  changes afterward; the original press's offset must still copy literal red onto the blue destination.
- CPU admission controls validate a correctly sized BGRA source and refuse missing, empty, wrong-format,
  wrong-size, mask and unexpected non-Clone sample inputs. The exact additional bound is the sample's
  cached `RetirementMemoryBytes`; no pixel plane is assembled to obtain it.

## Validation

Fresh full x64 Debug, Release, real ASan Debug and three ARM64 cross-builds are pending.
Earlier 977-input runs are historical after the lock repair and Clone change. Native ARM64 runtime,
application asynchronous Append/Up and measured G1 remain unqualified.

### Release analysis r351

Actual child PID 83416, exit 0. Recorder bounds
2026-10-07T23:26:21.644316Z–23:34:25.056526Z include source hashing;
they are not the kernel process creation/exit timestamps.
977 source inputs, freeze `A1F791AEDC0887D32B44C2E1A896A28B2F2BE306B1520071521982D75F50315C`.
stdout `C508B8BF01C516C7C2D02BEC397469AD6B2DEAC715F50ECAE0ECCD0CA478924F`;
stderr empty (`E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`).
The complete analysis build, native positive/negative concurrency controls and linked x64 ISA gates pass.
DxUi remains pinned to `8e8994079e82f79e6765e6a885c03fc687957591`.

Archive `D:\RedPrismQualificationArchives\P1-clone-sample-977-analysis351-qualified-2026-10-08`:
1,286 copies, zero runtime stages, verified 2026-10-07T23:35:08.1771349Z.
Source manifest `8566E26B7C116892B379CB98F0A7C8FDCC63878FC845223D52AE2FED49D0FC1B`;
evidence manifest `998212C8C0D9C5A96E184008B1CF5DA1A2507B6634DBCADF03187A8AFB0DD43C`;
completion `3991F02BC2641BE85363B7F59DCDD7E6B14B6D632BCB9221775E83F35C4C9A31`.
This closes analysis for this source, not a runtime, G1 or P1 completion gate.

### Full Debug r352 — preserved failure

Actual child PID 61792, exit 1. Recorder bounds
2026-10-07T23:35:53.502335Z–23:41:53.014552Z. The 977-input freeze is the
same `A1F791AEDC0887D32B44C2E1A896A28B2F2BE306B1520071521982D75F50315C`.
stdout `4A7864325703CE5D98DF19912DF54C3C5C6A60E8121932F520A131DEEB22C8B6`;
stderr `FD5D1A28F04C332035746F89822EBF07EB8741095FCEDE6493D4D9E64C6FF771`.
All four suites, the eight new Clone admission controls and both crash probes pass.
The earlier standalone Clone shutdown fixture fails its live boundary and charge
checks because source adoption now precedes the separate worker opening.

Archive `D:\RedPrismQualificationArchives\P1-clone-sample-977-Debug352-failed-2026-10-08`:
1,280 copies and two reached stages, verified 2026-10-07T23:42:18.9491005Z.
Source manifest `A23A36444138123B535BC059B7083C26A9A44E697B48B65C97F5FB3EEE8F4D5C`;
evidence manifest `732563AF00AADB4FA6F1A9F8DC1AB6C60451C7160FFB9C10933365F3006194ED`;
completion `CD439326FCB08AE20EB9D4F28F4D55AB861AB13D495928DEA107FBE976C79EBE`.

The repair distinguishes the existing source-adoption notification (2) from a
live notification (3) after the actual opening completes and Down replays.
All Layers and Single Layer live/shutdown fixtures retain their original named
pixel, history, ticket, ordinal, cancellation and retirement assertions. Their
aggregate charge must equal the exact source lease **plus** the nonzero opening
lease. No numeric golden or deadline changes. Fresh qualification is required;
r351 is historical after this repair.

### Runtime r353 — stale binary, excluded

The runtime-only command does not build. PID 110916 exited 5 using the previous
Debug binary (`EAEFF9FD91F42451C4C3E82F29F33C7A3E6980E8CAD115A30B9791952A37DC37`),
so it repeats r352's two standalone live-boundary failures and cannot validate the
repaired source. Recorder bounds 2026-10-07T23:47:31.373206Z–23:47:32.020535Z.
Archive `D:\RedPrismQualificationArchives\P1-clone-stale-runtime353-rejected-2026-10-08`
preserves 1,271 copies and two reached stages, verified 2026-10-07T23:48:07.3784188Z.
Completion `27C9C97D2A2F393B1BFDFF23FB8307486A8D769505B8A8A0DEF973BAEFD06DAD`.
No source qualification is taken from this attempt; the next command rebuilds.

### Debug build r354

Actual child PID 82712, exit 0; recorder bounds
2026-10-07T23:48:21.819105Z–23:52:58.447865Z.
977 source inputs, freeze `4DA82DE3174F26C1EC2CDA0B90AB2080FD6E511938061A0EC375FEFBFAE5BB8C`.
stdout `67C5E73B390BD16C6C71BC78AA99ED5571244808611488D1DC64C16F2C1CA9BC`;
stderr empty. Build, linked ISA checks and exact DxUi pin pass.
Archive `D:\RedPrismQualificationArchives\P1-clone-live-boundary-977-build354-qualified-2026-10-08`
preserves 1,271 copies, verified 2026-10-07T23:53:39.1373529Z;
completion `BB21726CD99B7775F02FBBC615C1A7B84DAE9754CFDEB1B70F7366C8CE57F32D`.
This is compilation evidence. Actual application runtime and the full matrix remain due.

### Actual application Debug runtime r355

PID 104500 exited 0; recorder bounds 2026-10-07T23:53:58.071422Z–23:56:05.166414Z.
The source freeze matches r354. The rebuilt binary hash is
`74198C573DF3513F470C1EDE065B65101C3F61490E8B19E16195765D5D389ACE`.
stdout `25F21A434F65CA0F9F52985D5E146DE05C724119016D6BE2CC5BE3D1436AA111`;
stderr `AD8108E8762ACCD89D86221E7D6B9BC18FE3D6C0B56E637E94CF499707870311`.
All 103 application stages pass, including the repaired Clone live/shutdown
fixtures and the seven new worker-opening controls in the sealed Clone runner.
Archive `D:\RedPrismQualificationArchives\P1-clone-live-boundary-977-runtime355-qualified-2026-10-08`
preserves 1,271 copies and all 103 stages, verified 2026-10-07T23:56:33.5818308Z;
completion `DE5B493B7CC9D96D8CEF64A9C27B812D4D55C5A8CE275CB061DCE389084769DE`.
This is a focused runtime pass, not the full configuration command or G1.

The next candidate also releases the sample input's mutable draft reference
inside the successful owner apply. A real worker prepares/composes; a tracking
CPU engine then proves owner cancellation expires the original draft and ends
the engine once, before the remaining input is destroyed on a worker. Five
new named assertions and a separate summary leave the original 98 session
controls and their summary unchanged. Full matrix qualification remains due.

### Full x64 Debug r356

Actual child PID 42064, exit 0; recorder bounds
2026-10-07T23:58:11.165672Z–2026-10-08T00:06:23.948447Z.
977 inputs, freeze `C7F8C6AD1C3BF04F296D079617BC83F3E40628864B062B61A8575DC0D59C936E`.
stdout `E201F4D4C7C9ED37C311A8A34F677A274F338D1B9B448400457B7AA7BF75ABF9`;
stderr `783290A5122D6BF54FD8B1673886672A7EE568C1CB965847CEFB14CA3E523DC2`.
All four suites, all five joined-input retirement controls and their separate summary, all Clone opening
controls, both crash probes, all 103 application stages and harness screenshots pass.

Archive `D:\RedPrismQualificationArchives\P1-clone-joined-sample-977-Debug356-qualified-2026-10-08`:
1,282 copies, verified 2026-10-08T00:07:31.5723075Z.
Source manifest `C852B1591A636CBC59D062443747FCDC27C4C543E14761BF6AB1BC013C5FB56F`;
evidence manifest `2CA51D540C905551FECA2C4D172CAD44080B1F8BBB2CE0AFB926C5F16784416E`;
completion `6BACB610AABF5D58BE500C80F7CF69559D3B0B23E78BF67FA6AB48D505899BEA`.
The remaining matrix and independent complete-inventory verification are required before commit qualification.

### Release analysis r357 and full Release r358

Both use the same 977-input freeze as r356,
`C7F8C6AD1C3BF04F296D079617BC83F3E40628864B062B61A8575DC0D59C936E`.
Analysis PID 117988 exits 0; recorder bounds
2026-10-08T00:09:26.963866Z–00:17:10.918380Z.
Archive `D:\RedPrismQualificationArchives\P1-clone-joined-sample-977-analysis357-qualified-2026-10-08`
preserves 1,286 copies, verified 2026-10-08T00:17:58.0295138Z;
completion `D02B850C95CD8167E0B142C804B95BEB258D42D5ADBD87AC7EAAE039EDF25FC2`.

Release PID 4496 exits 0; recorder bounds
2026-10-08T00:18:24.562332Z–00:26:01.963346Z.
Archive `D:\RedPrismQualificationArchives\P1-clone-joined-sample-977-Release358-qualified-2026-10-08`
preserves 1,274 copies and all 103 stages, verified 2026-10-08T00:26:54.1376863Z;
completion `1548715F8C41C46E383C8865817A611FFDEF50708FC021CE7B245D3A91CCE14F`.
Independent inventory verification passes for both full runtime profiles r356/r358:
11,139 original application controls, seven new Clone host controls, unchanged original
98 sample-session controls, five joined-input controls and complete predecessor order/multiplicity.
The verifier reads both exact archived application assertion streams. These timestamps
are recorder bounds, not kernel process creation/exit intervals.

### Real ASan r359 — instruction scanner refusal before runtime

PID 87532 exits 1; recorder bounds 2026-10-08T00:26:58.539497Z–00:28:15.883739Z.
The same source freeze applies. No runtime stage is reached.
Archive `D:\RedPrismQualificationArchives\P1-clone-joined-sample-977-ASan359-failed-2026-10-08`
preserves 1,275 copies, verified 2026-10-08T00:33:04.9473777Z;
completion `9347C12D2D07DE3F97F74A22390D5871BE578BE1DC26D0E4807404B55C229CB0`.
The scanner reports `vpunpckhdq` at `0x1406a2c4c` in the existing new-canvas selection fixture.
PE bytes establish the exact seven-row QWORD stack guard / RAX dispatch, maximum 16,
17 DWORD targets, terminal `RET` at `0x1406a2c2a`, one-byte `90` NOP and aligned table
`0x1406a2c2c`–`0x1406a2c70`. The reported vector encoding is a target RVA in that table.

The revised scanner accepts only this additional exact terminal layout and retains its
complete procedure body for the existing byte/ownership/boundary/control-flow proof.
There is no allowlist change. A direct rescan of the same ASan binary passes with
13 verified vector-like table rows and zero violations. Binary SHA256
`A37898216C09671A6449CE7247471CC18D2F1EFC5DB8D19691C0E6BFF260813D`,
PDB `192BA8CAAFDEE4B2E00C466D539A46385D898BB78730BBEF22C12AE58179207C`.
Independent acceptance/refusal controls and the fresh full matrix remain due.
r356–r358 are historical after this scanner source change; none establishes current P1/G1 completion.

The independent one-byte layout fixture is integrated after main review and execution:
115 controls pass (three accepted, 112 refused), including real vector code adjacent to the
table, mismatched PE/listing bytes, seven incomplete/unknown guard rows, overlapping terminal
rows, foreign PDB ownership, bad bounds and branch/call/loop entries into padding or table data.
The earlier 113 three-byte padding controls pass unchanged. Review repaired fixture-only
construction defects before integration: the vector-like target RVA now points to scalar code,
rel8 edges originate within range, and terminal-overlap cases cover the actual one-byte layout.
No legacy golden, existing control expectation, allowlist or scanner rejection policy was weakened.
Fresh qualification includes this additional source and is still required.
# Current 978-input qualification

Full real ASan r360 passes on freeze
`18A5CD3EFC9A48139A12C8352EA40ADB4A7CCC287B7F25F934865770108C9219`.
Actual child PID 74256 exits 0; recorder bounds are
`2026-10-08T00:47:29.929628Z`–`00:59:28.588868Z` (not the kernel process interval).
The archive is `D:\RedPrismQualificationArchives\P1-clone-joined-sample-978-ASan360-qualified-2026-10-08`:
978 source copies, 300 evidence copies and 103 application stages.
Source manifest: `ECC6C5CB06A895774C0BE1157DC2D5EC094B5D246385CAFBE348325BD258FEA4`;
evidence manifest: `DC7DB36090A9E944D7169D8F5987E3EFC42F9FD10681680ACE914EBB5588983D`;
completion: `0A65DCD7A0172CEBCA99A8FCEF68204CFA69C7463546DEA40665A39BDCF1184E`.

Independent verification against explicitly pinned Debug r356 preserves the complete 39,659 predecessor
pass-record sequence and all ordinary summaries, adds exactly the fixed 115 parser controls and their summary,
and verifies all 103 stage names. ASan's raster/worker stress and detector command are checked separately
against their pinned source-derived oracles. The unchanged PixelRecycle compile-time branch requires its
two actual sanitizer annotation controls instead of Debug's one named non-ASan control. Completion timing
may interleave only the pre-existing callback pair inside its four checked fixture windows; pair counts and
local order remain exact. This corrects the ignored verifier's cross-profile projection, with no production
or test-expectation change.

Release analysis r361 also passes on that same freeze: actual child PID 38144, exit 0, recorder bounds
`2026-10-08T01:03:26.651834Z`–`01:11:56.654212Z`.
Archive `D:\RedPrismQualificationArchives\P1-clone-joined-sample-978-analysis361-qualified-2026-10-08`
contains 978 sources and 311 evidence copies; source manifest
`589F95E063C454BCEC57EC3241430029F9DAB86C398B66719C9DA32E6564A5B8`, evidence
`CFCF21317452428E0D35A2A17AB62AF77AFF22F6249FCDB403C9213D9BD5522E`, completion
`212F4DCD9F56DD5F1E641B46C580A0BCD9338F761AEF4B2A5A97DA4D5C11F5B6`.
Fresh full Debug/Release and three ARM64 cross-builds remain required before committing this foundation.
These passes establish neither normal asynchronous stroke activation nor G1 or whole-P1 completion.
