# P1 native ARM64 Release validation — 2026-10-20

Status: failed native run; repair and renewed validation pending

## Source and environment

The hosted run [39429595846](https://github.com/RedSalamanders/RedPrism/actions/runs/39429595846)
tested commit `78dfa4de864ff29f0a31b7326c575054755471be` on native Windows ARM64,
not an x64 cross-build. The host reports Cobalt 100, two logical processors, 8 GiB RAM,
Windows build 26200, image `win11-vs2026-arm64` revision `20261013.183.4`, Visual Studio 18
Enterprise and MSVC `14.51.36231`. The reviewed helper selects ARM64-hosted MSBuild and
requests `PreferredToolArchitecture=arm64`, then invokes the unchanged canonical
`test.ps1 -Platform ARM64 -Configuration Release -MaxCpuCount 2`.

The actual child PID was 7836; it exited **1** at `2026-10-20T14:15:37.7843664Z`.
This run is not a native ARM64 qualification.

## Independently verified evidence

- All 26 downloaded evidence files match their recorded lengths and SHA-256 hashes.
- The 2,073-file before/after source snapshots are equal. Each file also matches the exact
  commit's bytes through Git's normal checkout filtering. Custom filters and working-tree
  encodings are unspecified; the committed license's `-text` attribute remains effective.
- All five executable products have machine type AA64 and matching full PDB GUID/age pairs.
  Product presence does not establish a passing runtime suite.
- Host and Document tests print their successful suite footers. CompositionTests contains
  exactly one `FAIL` record and exits nonzero. Canonical execution stops before IO and
  application runtime validation.

The failing record is:

```text
FAIL recorded-parent-cancellation.blocked-body-has-not-been-resumed-or-dispatched-twice
SUMMARY recorded-parent-cancellation passed=40 failed=1
```

The remaining original cancellation checks pass, including real same-ticket Compute/MTA
execution, no blocked owner readiness/resume, retained original memory, terminal cancellation
exactly once and off-owner final destruction. These facts do not waive the failed predicate.

## Repair preparation

Source review finds that the fixture assigns `enteredEventSignaled` from `SetEvent`'s return
value. A successful signal may wake the owner before that assignment. The owner then reads
the still-false flag in the quoted assertion. The prepared repair publishes every blocked-state
observation before signaling the event, handles an actual signal failure with its original
Win32 error, and leaves every assertion unchanged.

This fixture repair is private, unadopted and uncompiled at this checkpoint. It requires renewed
native proof; the failed run is retained. Related texture fixtures read their event-return flags
after thread join, and the sampler fixture reads its flag after terminal completion, so this
review does not justify changing those paths.

## Limits and retained receipts

The earlier hosted run `39403365977` timed out with a null child exit under emulated MSBuild;
it remains a separate failed attempt. The current run demonstrates native build-host progress
and an actual test failure; it does not qualify ARM64 Debug, ASan, visible-frame performance,
R3/Adreno behavior, G1 or P1 Done. A personal ARM64 host remains unavailable.

Local evidence is under `.build/P1Evidence/p1-native-arm-hosted-r2/`:
`main-native-failure-verified.json`, `download-manifest.json` (SHA-256
`6225D9CACBE729098D845BE8F1F26BFBF814A95C1DFA6D8877F207405BB43552`),
the original artifacts and raw logs. A reader-only Git batch-filter protocol refusal is
retained in `main-git-filter-reader-refusal.json`; corrected raw/standalone filtered-byte
reading verifies the same archive without rerunning native tests or changing any hash check.

## Repaired-source run 39479072110: incomplete failed attempt

The single push run on commit `950bebe32e3821107f77e92e0808c13e710c3d16` completes as failure.
Canonical test parent7008 exits1 at2026-10-20T20:07:55.4838546Z. Its Host, Document, Composition
and I/O success footers are present, including recorded-parent-cancellation passed=41 failed=0.
The Application child3196 does not finish within its unchanged1,200-second failure bound. Its captured
partial output has38 begun stages,5,074 PASS facts and no FAIL line; the last captured stage is
RunStandalonePendingRasterDisplaySelfTest. A complete Application exit is not observed.

After parent exit, the wrapper's stdout/stderr copy does not drain within30 seconds. The wrapper fails
before source-after, environment-after and native product-hash receipts are written. Their absence
must remain explicit; successful closing bookends cannot be inferred from the initial snapshot or
downloaded built products. The failed qualification reader correctly refuses this missing evidence.
GitHub's final runner cleanup reports terminating the orphaned child; this supplies no successful
Application terminal result. No retry is dispatched from these observations.

The run's downloaded source-before, raw logs, partial Application output and five executable/PDB files
remain preserved. Independent before-source, downloaded-byte, internal PDB and observed-roster verification
is recorded separately; these limited observations do not qualify a native profile. Slow execution and
a stall remain distinct possibilities. A followup needs progress/CPU diagnostics and closing evidence
recorded before a possible output-drain refusal. Any native-only failure-bound adjustment must be
explicit, retain the failure and preserve every assertion, golden, performance requirement and terminal gate.


## Adopted native-tooling followup

The followup preserves the default 1,200-second bound and permits a native-only optional 5,400-second
failure backstop. The hosted helper requests that observation window. This neither diagnoses nor fixes
the prior slow/stalled Application run. Both Application output streams and bounded CPU/memory progress
are recorded. Actual parent exit, terminal progress, closing source/environment and available product
identities precede the unchanged bounded raw-pipe drain. A drain refusal remains a failure.

The adopted source passes 30 pure-PowerShell controls and three typed invalid-argument children, each
with actual exit 1 and no build sentinel reached. Main retains independent negative counterexamples
for misleading comments/reordered executable AST and unrelated error text. No native Application or CI
helper executes in these controls. [Tooling evidence](P1ValidationTooling_2026-10-21.md) records the
affected qualification. A fresh hosted run with complete terminal/source/environment/product evidence
is still required; no current native runtime pass or performance result is inferred.


## Observed followup 39511716098

The separate CI-only comment commit `43725c68817eb303b519f9837064d03bb77a148c` requests one guarded
native Release push run. The executable helper behavior matches the adopted tooling; all 30 wrapper
controls pass again, and both remote helper/workflow blobs match the committed source. Workflow pins,
permissions, runner, Release-only selection and parent/job limits remain unchanged.

[Run 39511716098](https://github.com/RedSalamanders/RedPrism/actions/runs/39511716098) is observed in
progress in the canonical entrypoint step. The watcher preserves this exact attempt and downloads its
artifacts at termination. This request supplies no runtime pass, closing identity or performance result;
independent terminal, source, environment, product and complete-roster qualification remains due.
Evidence destination: `.build/P1Evidence/p1-native-arm-hosted-r4/`.

## Observed native ARM64 Release run 39511716098 — failed assertion

Run [39511716098](https://github.com/RedSalamanders/RedPrism/actions/runs/39511716098), attempt 1, tested commit `43725c68817eb303b519f9837064d03bb77a148c` from `codex/p1-opacity-host-proof`. The hosted environment was native ARM64 for both OS and process: Windows build 26200, GitHub image `win11-vs2026-arm64` revision `20261013.183.4`, Visual Studio 18 Enterprise / MSVC `14.51.36231`, Cobalt 100, two logical processors, and 8 GiB RAM. The environment receipts before and after record the same architecture, toolchain and run configuration (`ARM64` / `Release`, max CPU count 2, 5,400-second hidden-self-test failure bound). This is native-host evidence, not an x64 cross-build. (Evidence: `Artifacts/native-arm-ci/environment-before.json`, `environment-after.json`, and `build-host-selection.json`.)

The workflow step ran `./.github/scripts/Run-NativeArm64.ps1` under PowerShell 7 with `REDPRISM_CONFIGURATION=Release`. The wrapper selected ARM64-hosted MSBuild and requested `PreferredToolArchitecture=arm64`; the canonical entrypoint was `test.ps1 -Platform ARM64 -Configuration Release -MaxCpuCount 2`. The hidden WARP self-test was application PID 1580, child of canonical runner PID 4492. Application PID 1580 exited **5**; canonical runner PID 4492 exited **1**. The GitHub “Run canonical ARM64 test entrypoint” step and job both concluded failure. The terminal receipt records elapsed time 6,324 seconds, the application log’s final Polygonal summary, and zero owned application processes at termination. The application exited on a failed assertion before the failure bound. (Evidence: `job.log`; `Artifacts/native-arm-ci/started.json`, `child-result.json`, `terminal-progress.json`, `failure.json`; `Artifacts/ARM64/Release/RedPrism.self-test.err.log`.)

The only recorded failed assertion is:

```text
FAIL polygonal-press-host.new.harness-dpi-live.harness-dpi-override-native-message-rebuilds-live-chrome-not-os-transition
SUMMARY polygonal-press-host checks=316 failures=1 reached=25
```

The other 315 checks in that Polygonal group passed. Host, Document, Composition and I/O suites completed with passing results; their observed roster counts are respectively 12,548, 24,003, 8,426 and 1,953. The recorded-parent-cancellation controls passed **41/41**, and all **14/14** pending-raster fault cleanups completed. The Application run stops at the quoted DPI assertion; later stages are not qualified. (Evidence: `Artifacts/native-arm-ci/test.stdout.log`, `test.stderr.log`, `progress.jsonl`; summary: `main-failed-identity-and-rosters-r3.json`.)

The suite comparison retains two explicit Host architecture/FMA label substitutions. The runner records
`INFO brush-density-clear.debug-layer=unavailable`; exactly the two related optional SDK debug-message
controls are unavailable. Every other same-profile suite PASS record matches. This availability boundary
is retained explicitly and supplies no hardware GPU, timing or full Application qualification.

The source receipts contain 2,082 inputs each. `source-before.json` and `source-after.json` are byte-for-byte identical, each SHA-256 `B1AC5246BFB93E5A83CE7ACAC4F2051A5C0BD2F5DC7CAF090F8289D941D7EE91`; the recorded commit is unchanged. The downloaded artifact manifest covers 36 files and has SHA-256 `8792A9BF6F5A7BA0BC2D8503711323476A7145CEE805E502BF81BE1BA193F211`. The independent failed-identity/roster receipt is `main-failed-identity-and-rosters-r3.json` (reader SHA-256 `00680D92A4BA48FBE05BA70EB8D99DE6710E563DC1EF37214BFDF64D74349FEB`). (Evidence: `Artifacts/native-arm-ci/source-before.json`, `source-after.json`; `download-manifest.json`; `main-failed-identity-and-rosters-r3.json`.)

All five executable products are AA64, and each executable has a matching full PDB GUID/age identity (age 1). The product and PDB SHA-256 values are:

| Product | EXE SHA-256 | PDB SHA-256 | PDB GUID / age |
| --- | --- | --- | --- |
| RedPrism | `EC72603729D06AABE846B6D0818E007BCAC9B36451E27D2EE0E360C68CCD6F41` | `1B6BF559913B7380DF8B4D16EB101F932611E65D28E5DC89D892AD4E94E86F89` | `39A15209-0F59-4215-B6E0-BBB24753D42A` / 1 |
| HostTests | `D9EA5698906370E0386A14AC2C09177B1334C09845C3AC26F7413CD6E6786454` | `76C73CEA37A4790A93E62F9538C4AA49B569436A0ECFD3A23ADD45321877B5A5` | `C7163158-A4C7-4C23-B0E5-9F2E1B424EB5` / 1 |
| DocumentTests | `2202344CA989B6031A5C041C62CC5E4A2ED576D5209357395C829D33F9218068` | `40094E1182764517C5214D604FB60C78D6930B1BE2D9A483178DD5EA8875C8A2` | `5C8897C8-80C9-4104-8D3A-71A9083FF63A` / 1 |
| CompositionTests | `457A067550890975530A50C291E4D90C61B717844C1C98A16391CFC131B09FA7` | `A0403E4722F458FFBC19B56074B4378350D2378EB6B93111F59360FA9026D173` | `4159959E-D83D-4F94-9215-81F5DC307DCF` / 1 |
| IoTests | `EC8A3C1CA9D32480CD33593363799EB0FCB30F38F1A940763371DFB39D06394E` | `188FC283FFBC9CCB3898DCC4A65996FFB018D710695558162AB61929CE06051C` | `161B6B7C-97F7-409C-98CD-92CBC0ECC370` / 1 |

(Evidence: `main-failed-identity-and-rosters-r3.json`; downloaded products under `Artifacts/ARM64/Release/`; matching readable summaries under `MainPdbSummaries-r3/`; aggregate hashes in `download-manifest.json`.)

This remains a failed native ARM64 Release run. Product identity and passing suite receipts do not override the DPI failure. The evidence does not establish its native cause, a repair, a passing run on newer source, ARM64 Debug or ASan qualification, performance qualification, G1, or P1 completion. Preserve the earlier failed and incomplete attempts above as separate records; this observation does not replace or reclassify them.
