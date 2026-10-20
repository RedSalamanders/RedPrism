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
