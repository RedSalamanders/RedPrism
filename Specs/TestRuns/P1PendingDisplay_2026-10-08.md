# P1 pending display qualification record — 2026-10-08

**Latest qualified result:** full Debug r439, Release r440 and real ASan r441 pass on the identical 1,014-input source below. Earlier failure and pending sections retain their historical status. Release analysis442 and all three ARM64 r53 cross-builds also pass. Ordinary activation and G1 remain open.

**Record type:** immutable test-run evidence. This file does not change normative specifications, activate pending-display routing, close P1, or claim G1.

## Scope and conclusion

Six actual attempts were captured for the 1,014-input pending-display candidate: build attempts r428–r430, DocumentTests r431, CompositionTests r432, and Application r433. The archive receipts, source/evidence manifests, copied files, and source-freeze descriptors were independently rehashed in this documentation pass and matched the corresponding completion receipts and the `.build/P1Drafts/PendingDisplay-r2/Main-*-archive.json` readback reports. All copied manifest entries verified (no missing, length-mismatched, or hash-mismatched entries).

The evidence preserves two failed builds, a successful build, a failed DocumentTests run, a passing CompositionTests run, and a failing Application run. Only CompositionTests r432 qualifies its tested slice here. Application r433 exits 5 on two incorrect fixture literals; DocumentTests r431 exits 1 on three missing-live-adjustment fixture expectations. The later r434 candidate was reported as still building; this record has no r434 completion receipt, so its status is pending/unknown.

## Actual attempts

Times below are UTC from the independent main-checkout receipt readbacks. `Sources/Evidence` gives independently rehashed archived-file counts. Full manifest, stdout/stderr log, binary, and receipt hashes are in the ignored companion report `archive-rehash-report.json`.

| Attempt | PID | Actual child UTC start–completion | Exit | Source freeze SHA-256 | Sources / evidence verified | Result |
|---|---:|---|---:|---|---:|---|
| Debug build r428 | 113792 | 2026-10-08T09:13:20.581933Z – 09:14:36.893164Z | 1 | `21A843CEADAC9573F2D178EA3E3AD9AD65481B19FFD712177007D784C7718886` | 1014 / 318 | Failed compile: new Application fixture lacked complete adjustment/host declarations and typing. Preserved. |
| Debug build r429 | 111744 | 2026-10-08T09:16:52.210575Z – 09:17:08.803459Z | 1 | `A96E793F95ED2C1906AB16B1059D249F4159AB692250EE281EACD2AA60324CA9` | 1014 / 327 | Failed link: unnecessary `PendingDisplaySource.cpp` registration in the document-only CompositionTests target exposed unresolved `DocumentSession::ColorLookupPreviewReady() const`. Preserved. |
| Debug build r430 | 79484 | 2026-10-08T09:18:14.109576Z – 09:22:30.527464Z | 0 | `B746F2997C681EFA5D77A8E38C6CF8A2C587D6117C04C060BB3494521D8E5726` | 1014 / 308 | Build passed. This is build evidence only. |
| DocumentTests r431 | 94976 | 2026-10-08T09:23:38.057691Z – 09:24:08.612849Z | 1 | `B746F2997C681EFA5D77A8E38C6CF8A2C587D6117C04C060BB3494521D8E5726` | 1014 / 308 | Failed 3 `pending-display-source` live-fixture checks because the new helper did not provide the live adjustment original. Preserved. Executable hash was unchanged. |
| CompositionTests r432 | 76096 | 2026-10-08T09:23:38.055313Z – 09:23:45.981346Z | 0 | `B746F2997C681EFA5D77A8E38C6CF8A2C587D6117C04C060BB3494521D8E5726` | 1014 / 308 | All CompositionTests passed, including 25 new literal adjustment-display admission checks. Executable hash was unchanged. |
| Application r433 | 103496 | 2026-10-08T09:23:38.597050Z – 09:24:34.481141Z | 5 | `B746F2997C681EFA5D77A8E38C6CF8A2C587D6117C04C060BB3494521D8E5726` | 1014 / 308 | Failed at pending-display stage 14 on two expected pixel literals. Executable hash was unchanged. |

The 431–433 processes are distinct actual child PIDs and receipts. Matching freeze hashes bind them to the same captured source state; their different completion receipts and binary identities remain separate evidence.

## Failure details and correction basis

- **r428:** `Build-x64-Debug.log` reports undefined `AdjustAction` and `ChromeHost` types and missing enum/command members in `Application.Tests.PendingDisplay.cpp`. This was an optional typing/header fixture error, not a runtime result.
- **r429:** `Build-x64-Debug.log` reports the unresolved `ColorLookupPreviewReady()` symbol while linking `CompositionTests.exe`. The source was unnecessary for this document-only target. The later source registration was removed from the CompositionTests target; the r429 failure remains recorded.
- **r431:** the captured runtime log names three failures: `pending-display-source.reference-live-fixture-builds-completed-lookup`, `pending-display-source.live-descriptor-fixture-publishes-lookup-only-result`, and `pending-display-source.newer-controls-fixture-publishes-lookup`. The new helper omitted the live `AdjustmentOriginal` required by the actual `ConfigureColorLookupPreview` path. The main-thread correction is reported as a source-only fixture setup change to provide that original; this record does not claim a later DocumentTests pass.
- **r432:** the archived `CompositionTests` stdout ends with “All composition tests passed.” The 25 `adjustment-display-admission.*` checks cover exact literal size/admission cases. This does not qualify Application host scenarios, G1, or production activation.
- **r433:** the actual pending-display stage reports `expected=255, actual=176` for the plus-one-stop preview and `expected=64, actual=92` for the replacement preview. The fixture had assumed sRGB code values scaled directly; exposure is linear-light. The correct independent literals are 176 and 92: decode sRGB 128 to linear light, multiply/divide by two for ±1 stop, and encode back to sRGB. This is consistent with the existing legacy expectation in `retained numeric golden cases–46` (`+1 stop` yields 176 within tolerance). No legacy golden or production adjustment math is changed by this correction basis. r433 remains a failure and cannot be counted as a pass.

## Pending controls and qualification boundary

The intended next candidate adds eight actual Application scenarios: four destructive/live Exposure OK/Cancel cases, plus four cases that stop after the original planning callback attaches its recorder (Cancel/reopen, joined shutdown, document replacement, and suspension/resume). The original source declares 186 new expected checks in total (31 Document, 25 Composition, and 130 Application). These are planned expectations, not qualified results. In particular, no pass is inferred from the subset of r433 checks that printed PASS before the two literal failures.

The candidate correction and a fresh Debug build/runtime receipt are still required. At the time of the supplied status, r434 was building; until a final receipt and matching archive are read back, r434 remains pending with outcome unknown. A later successful subset will not establish the full profile matrix or G1 by itself.

**Next receipt slot — r434:** actual PID, UTC start/completion, exit, source-freeze SHA-256, stdout/stderr hashes, source/evidence counts and digests, executable SHA-256/`BinaryUnchanged`, and matching readback report are pending. Do not fill these from the in-progress status; bind them only from the completed actual-child receipt.

Pending-display ordinary activation remains disabled. G1 resource/latency measurements and the P1 exit gates remain open. The existing WIP plan/index status was read and intentionally left untouched; this evidence record does not make a P1 Done claim.

## Archive provenance

Each named archive is under `D:\RedPrismQualificationArchives\P1-pending-display-1014-<attempt>-2026-10-08\`. The independent readback reports are the six matching `Main-*-archive.json` files under `.build/P1Drafts/PendingDisplay-r2/`. The companion ignored report records, per attempt, the exact completion receipt hash, source/evidence manifest hashes and counts, source-freeze hash, actual tuple, executable identity and independent readback match.



## Subsequent build and focused attempts — main verified

The earlier pending receipt slot is historical. Main independently rehashed every copied file, bound the actual launch/result, and checked frozen source and unchanged executable identities for these completed attempts.

| Attempt | Actual PID / exit | UTC start — completion | Inputs / evidence | Source freeze | Archive receipt |
|---|---|---|---|---|---|
| Debug434-build-qualified | 69640 / 0 | 2026-10-08T09:28:29.586107+00:00 — 2026-10-08T09:32:51.325929+00:00 | 1014 / 308 | `A83D949A02DB7FB2516A60E5DF4F9E7EE07178C9DF505C8C6A489E5A86AC1954` | `EAA5986D1038F2749B95908DB3270B0941FCDC5DDAD24C4550C94247B110CF0F` |
| Document435-qualified | 82476 / 0 | 2026-10-08T09:36:16.722279+00:00 — 2026-10-08T09:36:47.353129+00:00 | 1014 / 308 | `A83D949A02DB7FB2516A60E5DF4F9E7EE07178C9DF505C8C6A489E5A86AC1954` | `C0B6535C20813CC4C4D25D735D4375FDC534E5EC416D89C541A7B385BE9E9376` |
| Application436-failed | 4180 / 5 | 2026-10-08T09:36:16.726528+00:00 — 2026-10-08T09:37:14.099615+00:00 | 1014 / 308 | `A83D949A02DB7FB2516A60E5DF4F9E7EE07178C9DF505C8C6A489E5A86AC1954` | `7B348F0CBE77757D122335ECD8B80EE0CFEC9C67C37FD0F3C6F522EA2760D3B7` |

- r434 passes the canonical Debug build and linked ISA controls; it is build evidence.
- r435 passes DocumentTests including all 31 new descriptor assertions after the actual live-original fixture capture correction.
- r436 executes all 130 new host assertions, with 129 PASS and one FAIL. The failure is `in-flight-Cancel-excludes-opening-without-changing-original-source-or-history`: the NEW fixture incorrectly required immediate session panel removal. `Document_History.md` and existing `ApplicationColorCancellationTests` instead require the panel/source to remain until selected worker closure acknowledgement. Four published-preview cases, document/device rebinding and the other in-flight cases passed, but the whole Application attempt remains failed. It stops at stage 14 and supplies no complete predecessor runtime proof.

Main corrects that new fixture to require the original retained panel, pending exact Cancel receipt and unchanged authority/history. It also closes a real pending-display gap: accepted Cancel now excludes its current document/opening preview immediately and requests useful-work cancellation/retirement. Refused admission and another opening are unaffected by that predicate. Four new explicit published OK/Cancel acknowledgement-boundary assertions raise the candidate to 134 host + 31 document + 25 composition = 190 new assertions. Existing numeric goldens, old source-fixed assertion streams and adjustment math are unchanged.

Fresh Debug r437 is required for this revised source. The original r436 source, binary and logs remain preserved. No full profile, normal activation, measured peak or G1 claim follows from these focused attempts.

## Revised cancellation boundary and complete Debug qualification

Main verified the following completed archives against every copied source/evidence file and the actual canonical producer or unchanged focused executable. All three use the exact 1,014-input freeze `81B979924CE117EBD23C3142CD7AB10364CF5D20D1F864247A982144BAACC01E`.

| Attempt | Actual PID / exit | Actual UTC start — completion | Inputs / evidence | Completion SHA-256 |
|---|---|---|---|---|
| Debug437-build-qualified | 79900 / 0 | 2026-10-08T09:39:39.152704+00:00 — 2026-10-08T09:44:05.037613+00:00 | 1014 / 308 | `C3A7D9D48D838C362A0CE2A8D29EC4E1AC81E545B3CD9AD562A62C31514F3842` |
| Application438-qualified | 27088 / 0 | 2026-10-08T09:44:50.474309+00:00 — 2026-10-08T09:46:53.804831+00:00 | 1014 / 308 | `A2D494DC7B50125ED827B51FC9EB95D467A3DC49530DB5BFFDDC42B4D72A7972` |
| Debug439-full-qualified | 27508 / 0 | 2026-10-08T09:47:59.242241+00:00 — 2026-10-08T09:55:59.184756+00:00 | 1014 / 319 | `834CC28548937342D7F18F5A9C054EF8278FAF0AC7F7CB05D34469004F3F8194` |

Application438 passes all 134 current host assertions and 111 stages. Main's source-fixed comparison preserves every predecessor Application record in order: 4,850 -> 4,984. Exact summary is 134 checks, zero failures. The actual focused executable SHA-256 is `78BE7AE69C49366B7200A2513F8CF3CF347841BAE2D401C4A0E6BBA89937976C`; its receipt reports `BinaryUnchanged=true`. Focused execution writes its actual stdout, not a new separate `RedPrism.self-test.log`; no stale separate file was used as this runtime result.

Canonical Debug439 passes all suites, ISA controls, script controls, crash probes, benchmark correctness smoke and nine application harness screenshots. Its archived actual Application log also has all 134 new assertions and all 111 stages, with the entire predecessor Application stream preserved. The full stdout contains 31,006 normalized PASS/INFO/SUMMARY records versus 30,950 in Debug426: the 31 document plus 25 composition additions account for the difference; Application output goes to its separate runtime log. Removing only those exact source-fixed additions and separately validating the eight legacy callback records produces the complete 30,942-record predecessor projection. Main inventory report file SHA-256: `ABE16399B00906E0B27AE907210DA80510DB923EB0363C0B7452B0126170E2E2`.

The ignored inventory verifier required main repairs before it could qualify this actual source: archived freeze discovery, exact producer/source-copy bindings and new-stage confinement; adjacent C++ string-token joining after formatting a long assertion label; and metadata-only JSON serialization instead of embedded raw log bytes. No executable, product source, runtime assertion or numeric golden changed during these verifier repairs. Its valid controls and 14 omission/order/unknown-INFO/FAIL/stage-confinement rejection controls pass. The original faulty reports/drafts remain in their separate sidecars.

Release440 is the next full profile, followed by real ASan, analysis and current-source ARM64 cross-builds. Pending ordinary activation, next-operation recorder coexistence, other tool/display/utility consumers, actual peak admission and G1 remain open.

## Full Release r440 — main verified

The canonical full x64 Release test child PID 58024 exited zero between `2026-10-08T09:58:22.562548+00:00` and `2026-10-08T10:07:37.795620+00:00`. Its 1,014-input freeze is `81B979924CE117EBD23C3142CD7AB10364CF5D20D1F864247A982144BAACC01E`, identical to Debug437, Application438 and full Debug439.

Archive: `D:\RedPrismQualificationArchives\P1-pending-display-1014-Release440-full-qualified-2026-10-08`. Main independently rehashed all 1,014 source and 307 evidence copies, the frozen descriptors and actual canonical producer. Completion SHA-256 is `C0747347898A635DAD812654D6827245F8770066AE6C590A4197B50E0DB8502D`; source manifest `46A1EF7131BA31F7274AFCC5ABFC15DB9CBA369C214C58D989D33332C6B09E6E`; evidence manifest `AABA7999A44EFC250B926A7B0A0E24B6E21A4FB39A52C3762FBCD6397772B3A2`.

Source-fixed same-profile inventory verifies 30,950 predecessor full-stdout PASS records to 31,006 candidate records: exactly 31 Document and 25 Composition additions. The complete 30,942-record predecessor projection and eight existing acknowledged-callback controls verify separately. Actual Application output verifies all 111 stages in order and 4,850 predecessor records plus 134 new assertions (4,984), with the exact 134-check summary. Application output is separate from the aggregate suite stdout. Its SHA-256 is `A967E311C0848E8A18AD278526D6266D5BCA1ADD75835D9BE8B07C35CF91A1AE`; canonical stdout `72DC97C6AB738D6C1C36C18A06445EB564366C9136D439BF39EAD2B6F4A58DD0`; stderr `E50B51A92C12E6DC794D1A13BC9B61BB47BA6FBD52B9E26BD924A3F8AEC52C26`.

The linked ISA controls, four suites, actual WARP application, crash probes, script controls, benchmark smoke and nine harness screenshots pass. This is full Release qualification of the opt-in candidate; it is not ordinary activation, a G1 measurement, native ARM64 execution or a P1 Done result. Main reports are `.build/P1Drafts/PendingDisplayInventory-r4/Main-Release440-{archive,inventory}.json`.

## Full real ASan r441 — main verified

Canonical full x64 ASan Debug child PID 105756 exited zero between `2026-10-08T10:10:04.089449+00:00` and `2026-10-08T10:21:53.871205+00:00` on the unchanged 1,014-input `81B979924CE117EBD23C3142CD7AB10364CF5D20D1F864247A982144BAACC01E` freeze. The actual AddressSanitizer detection probe passes; this is instrumented runtime evidence.

Archive: `D:\RedPrismQualificationArchives\P1-pending-display-1014-ASan441-full-qualified-2026-10-08`. Main independently rehashed every 1,014 source and 308 evidence copy, frozen descriptor and actual canonical producer. Completion SHA-256: `451E11AB61419E7327FBDAEB6D66058B3A93BD9CDAA0579F0F6EC337E1FE6FC2`; source manifest: `D56390B87F103F09538588A3EC3DC2C36AF9A456084C30218DA904D0AC09AB42`; evidence manifest: `4F47542D3953A781F373213CEC068FB7C3F8214946881B82AE2B8DDCEB76A865`. Actual stdout: `51B4FFE08A323BAB8945727554B1547E3C4928C40E00F412F9D025C6A4432832`; stderr: `0FCEB8491E7D53B1FCCC540C393C829F3A680D5634F070A4F901D2D89F3E5ECC`.

Same-profile source-fixed inventory verifies 39,479 predecessor aggregate PASS records to 39,535 candidate records, with exactly 31 Document and 25 Composition additions. The complete 39,471-record predecessor projection and eight old callback controls verify separately. The actual application verifies 111 stages and all 4,850 predecessor records plus 134 new assertions (4,984), with the exact 134-check summary. Main inventory report SHA-256: `D6310C59A95AAC7E6298F60011E8A5A03910B94B62B131CC6913F9AB4E6B6F2B`.

The linked ISA/scanner controls, stress cases, all suites, actual WARP host, crash probes, scripts, benchmark smoke and nine harness screenshots pass. Analysis r442 and ARM64 r53 remain due. This does not qualify ordinary activation, transport contention, real G1 or native ARM64 runtime.

## Release analysis r442 — main verified

The canonical `/analyze` Release rebuild child PID 80348 exits zero between `2026-10-08T10:22:52.452315+00:00` and `2026-10-08T10:30:25.505018+00:00`, with the same 1,014-input `81B979924CE117EBD23C3142CD7AB10364CF5D20D1F864247A982144BAACC01E` freeze and unchanged DxUi pin. Main independently verifies 1,014 source and 321 evidence copies and the exact build producer.

Archive: `D:\RedPrismQualificationArchives\P1-pending-display-1014-Analysis442-qualified-2026-10-08`. Completion SHA-256 `83BE83854CE196FEBBD478E98D94C6F5BC12D498A8D337FE1AD8E905DEB4E0EC`; source manifest `D451310F21BFD0FBFB91C0B3C5611BB9E0DB282E71F9A2F6EDFF7D7BEBF73AE2`; evidence manifest `E8C6DE76087E605D555157844EF1D5DDB16F4B2CD60FEAC111D8D75500BF185B`. Actual stdout SHA-256 `A205B83F809A45AB3FFF05BA857F2AE5B79B53107ADFA1F81E3CA92107E46F68`; stderr is empty (`E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`). Build and linked ISA controls pass. This receipt contains no application runtime stages and is not runtime/G1 evidence.

Skill validation passes all 15 skills; `format.ps1 -Check` checks 885 files successfully. ARM64 r53 is launched only after the completed x64 archives: its recorder SHA-256 is `2A4AD7C6B305D13215CB2E44DC271FA40AE85763C3D297A1CBC9D31D5A1FDFA9`, launcher `44106952F8C2A9D3478445D08C4CEE56696D0E1172CDD2B9806C237AA7486FF7`. Each actual ARM64 child checks source and pin before and after build, uses a unique output directory, and records its owned process interval. Final ARM64 qualification remains due until main independently checks all completed archives.

## ARM64 r53 and checkpoint closeout — main verified

All three canonical ARM64 builds pass on the same 1,014-input `81B979924CE117EBD23C3142CD7AB10364CF5D20D1F864247A982144BAACC01E` freeze. Main independently rehashes every source/evidence copy and actual canonical producer, validates the owned `GetProcessTimes` child intervals and recorder/launcher identities, and checks all 15 archived executables as ARM64 PE images with matching PDB copies. For all five ASan executables, `__asan` imports and archived compiler commands with `/fsanitize=address` and `RequireAddressSanitizer.h` verify. These are cross-build results; no native ARM64 runtime host is available.

| Profile | Actual PID / exit | Owned child UTC interval | Completion receipt SHA-256 |
|---|---|---|---|
| Debug | 62376 / 0 | `2026-10-08T10:31:04.976071+00:00` — `2026-10-08T10:32:28.128043+00:00` | `92AE88E8C32B01B0FDE13B257639482A6A2F80C7F878400CCB4980F038BAC538` |
| Release | 31764 / 0 | `2026-10-08T10:32:39.449974+00:00` — `2026-10-08T10:34:41.643589+00:00` | `A4CBBBE91CD37D0B0D6D50859FFA0A14B31063D7E56CB9D814E6FCD553CCDB42` |
| ASan Debug | 104776 / 0 | `2026-10-08T10:34:51.362875+00:00` — `2026-10-08T10:36:09.577640+00:00` | `1CB4F62756DA1D65A38264C5C94C57E79ABBF149C19FB4BD8C869A58E74E61FB` |

Main ARM proof SHA-256: `B7C4DC5015F4F5ADAA945E4718CF5C193E513EA9145AC518A4A6822A1AAC17D8`. Archives are `D:\RedPrismQualificationArchives\P1-pending-display-1014-ARM64-{Debug,Release,ASan}-r53-qualified-2026-10-08`. Individual generic and architecture reports reside in `.build/P1Drafts/Arm1014-r53/`.

The pending-display checkpoint now passes full x64 Debug439 / Release440 / real ASan441, Release analysis442, all three ARM64 r53 cross-builds, 15-skill validation, 885-file formatting check and diff checks. Preserve every earlier failed attempt. This qualifies the opt-in pending source and shared transport lifetime for the tested cancellation/device/shutdown cases. The next transport contention scenario, Gradient/Pixel Move/other consumers, normal activation, complete memory peaks, real G1 and P1 closeout remain open.
