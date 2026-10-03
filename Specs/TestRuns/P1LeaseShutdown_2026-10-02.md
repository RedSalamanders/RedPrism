# P1 lease shutdown: mapped readback and retry producers

Date: 2026-10-02.

## Scope

The 56 named controls are four real cases with fourteen assertions each: mapped-bound-device, mapped-retired-device, retry-held-producer, and retry-ready-undrained. Each uses an actual standalone hidden-WARP Application with its own sink and pool and a 16 MiB CPU source/payload. DocumentReadbackJob holds its final borrowed mapped rows on the bound device or after GpuWorkQueue::RetireDevice; this is explicit queue device retirement, not actual driver device loss. RetryWaitQueue uses the actual held producer before publication and the actual ready-but-undrained route.

The scoped checkJoined observation precedes queue cleanup, preventing fallback cleanup from supplying a false acknowledgement. The cancelled worker reads borrowed rows before returning; the wrapper acknowledges the exact lease on owner. Source and copied CPU aggregates are destroyed on final MTA retirement, useful callbacks are suppressed, and the actual shutdown stop token releases the event helper so StopNotifications joins. First/second Shutdown, caller STA preservation, and fixture quit consumption are checked.

## Protocol

Private r2 Debug/WARP build and run exit codes: 0/0. Build completion UTC: `2026-10-02T15:09:58.9313346Z`; run completion UTC: `2026-10-02T15:14:07.2764975Z`; qualified archive completion UTC: `2026-10-02T15:15:18.6037552Z`. Image SHA-256: `6BC0E2D16C5D61848B93A30FF0B24E481CBFED3C12968AA02E97BD648EED084F`. Private archive counters: 563 frozen sources, 40 evidence entries. Native freeze count read from actual evidence: 587. Every native/private qualified and retained-failure source/evidence entry was independently SHA-256 verified against its manifests, manifest receipt hashes and archived freeze; current native frozen inputs also matched.

Qualified native run: `p1-lease-shutdown-native-r2`; matrix start UTC: `2026-10-02T15:51:58.8860516Z`. Six profile identities, exact verified-controls profile UTC strings, and completion ordering were checked with DateTimeOffset; the receipt strings below are preserved verbatim. The qualified source count is read from this run's freeze and compared with its receipts/manifests; it is not inherited from failed r1.

## Results

Private r2 and all three native x64 application logs pass exactly 56 named lease, unchanged 432 live-draft, 390 processing/startup and 52 shutdown controls. The matching verified-controls receipt identifies the passed full x64 suites; ARM64 results are cross-builds only.

| Official profile receipt | Exit code | Completed UTC (receipt string) |
| --- | ---: | --- |
| `p1-lease-shutdown-native-r2-ARM64-ASanDebug.log.result.json` | 0 | 2026-10-02T15:54:18.2501547Z |
| `p1-lease-shutdown-native-r2-ARM64-Release.log.result.json` | 0 | 2026-10-02T15:57:05.1941583Z |
| `p1-lease-shutdown-native-r2-ARM64-Debug.log.result.json` | 0 | 2026-10-02T15:59:08.0071585Z |
| `p1-lease-shutdown-native-r2-x64-ASanDebug.log.result.json` | 0 | 2026-10-02T16:11:43.0317168Z |
| `p1-lease-shutdown-native-r2-x64-Release.log.result.json` | 0 | 2026-10-02T16:17:17.9656507Z |
| `p1-lease-shutdown-native-r2-x64-Debug.log.result.json` | 0 | 2026-10-02T16:25:25.7507098Z |

Native final matrix UTC: `2026-10-02T16:25:25.7852876Z`; verified-controls matrix UTC: `2026-10-02T16:25:25.7852876Z`; qualified archive completion UTC (receipt): `2026-10-02T16:27:57.1089287Z`. Native archive counters: 587 frozen sources, 325 evidence entries.

## Retained failures

Private r1 failed linking with LNK1104 opening shared DxUi.lib while its shared build root was being mutated; no application ran. Build exit code: 1; build completion UTC: `2026-10-02T14:59:05.9421739Z`; failure archive completion UTC: `2026-10-02T15:00:36.2808967Z`. Its 563 sources and 171 evidence entries are retained and independently verified.

Native r1 stopped at the x64 ASan ISA gate before runtime after all ARM64 cross-builds passed. The gate reported the instruction-looking ASan switch-table span as `0x1406b6cb8: Z:\src\RedPrism\.build\Intermediate\x64\ASan Debug\RedPrism\Application.obj: vpackssdw	ymm8, ymm4, ymmword ptr [rax]`; its report hashes match the archived ASan image/PDB. No x64 runtime qualification is established for failed native r1; x64 Release and Debug were not built or run. The separate private r2 Debug/WARP runtime qualification remains valid. R1 start UTC: `2026-10-02T15:16:18.9739961Z`; failed matrix UTC: `2026-10-02T15:25:56.1142135Z` (exit 1); failure archive UTC: `2026-10-02T15:29:28.6539347Z`. Retained counters: 586 frozen sources, 243 evidence entries. R1 cross-build completion receipts: ARM64 ASanDebug `2026-10-02T15:18:32.9058129Z`; ARM64 Release `2026-10-02T15:21:23.7460797Z`; ARM64 Debug `2026-10-02T15:23:30.9076082Z`.

## Narrow ISA repair

The private scanner extension adds the observed CMP/MOVSXD RSP+disp32 terminal guard. It passes 54 independent
positive/refusal controls, including each high displacement byte, PE/listing/ownership/extent/target errors and
adjacent real VEX rejection. Existing parser suites pass. Every full native x64 gate also reports all 54 controls.
`IsaRuntimeAllowlist.psd1` stays byte-identical: SHA-256
`6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.

The original archived ASan image is rechecked before rebuilding. An independent row trace changes exactly three
rows: `0x1406a6525` inside OnSelectionAction's 52-byte table (`0x1406a6500`–`0x1406a6534`), and
`0x1406b6cb8`/`0x1406b6cd1` inside EndCanvasEdit's 44-byte table (`0x1406b6cb4`–`0x1406b6ce0`). No old data
recognition disappears. Data rows increase 19→22; vector rows decrease 11691→11690; undecoded prefixes decrease
6→4. All 4,463,392 decoded rows, 11690 runtime rows, and zero tier/EVEX rows remain accounted for; violations are empty.
The first recheck expected only one additional row and failed its expectation, after the parser controls passed.
Its ten inputs/five evidence entries are preserved as `p1-wide-stack-isa-private-r2-recheck-expectation-failure`.
The corrected r3 passes at `2026-10-02T15:51:10.3368650+00:00`; its eleven inputs/nine evidence entries are
independently SHA verified in `p1-wide-stack-isa-private-r3-qualified`, acknowledged at
`2026-10-02T15:51:57.4090880+00:00` before native activation. Source-manifest SHA-256:
`E439D43848885A33E16E84650F65DADE72488BD70240117EE0CE8C751C30F3DB`; evidence-manifest SHA-256:
`52F2839FEF6526EC7BC878669FD310CCA657C0F5F002FC83FBA333B77859B634`.

## Limits

This scoped proof does not close P1/G1, remaining clipboard lease coverage, initialization API fault coverage, or timing qualification/completion. Explicit queue device retirement does not establish actual driver device loss. No ARM64 runtime qualification is claimed.

## Evidence archives

- Native: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-lease-shutdown-native-r2-qualified`; source-manifest.json SHA-256 `D72F34CEA03B429937B6B9A1580229D7CEE434D8023BDBB87E6259409C36352C`; evidence-manifest.json SHA-256 `86C3DBB3BADC7518AFA3894B43414C28952DCA3451E8E76E3F931C104CB265C0`.
- Private r2: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-lease-shutdown-private-r2-qualified`; source-manifest.json SHA-256 `AD2C3416772EC5319AAF1D59A8F75E51BEF3990EB6D1F616C87C739A111485D2`; evidence-manifest.json SHA-256 `96103C4D4EAA6353D3F7ACBC209FEF40FC394667F048F51EE174C655B7494C96`.
- Private r1 failure: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-lease-shutdown-private-r1-link-failure`; source-manifest.json SHA-256 `C1689F2BEE1C9155EAAC98827E090BAA9C5C2670ACAD8F4CAB5BBC39FBADB6DA`; evidence-manifest.json SHA-256 `20ABCAFFDFA86EF846DB41160703F81C364CFF0DCA0AB7BE99471818C31EB35A`.
- Native r1 ISA failure: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-lease-shutdown-native-r1-ASan-isa-failure`; source-manifest.json SHA-256 `D753C6A489DCD0CF4680334E6CEBF9A5137805B9B0860E6BD3BC0E920EF6615A`; evidence-manifest.json SHA-256 `E3FB7DBEC50A41132ABC9A0F650F29AA2ABBD438C2650C5A82F18919B6089B0C`. Gate/log evidence: `.build/IsaScan/x64/ASanDebug/RedPrism/report.json` and `.build/p1-lease-shutdown-native-r1-x64-ASanDebug.log` (verified archived copies).
