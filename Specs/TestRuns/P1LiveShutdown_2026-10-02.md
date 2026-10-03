# P1 live shutdown: live draft retirement

Date: 2026-10-02.

## Scope

The private Debug/WARP proof exercises 432 controls across 12 real draft cases—image/mask Paint and Erase, Heal ContentAware/CreateTexture/ProximityMatch, Clone, image/mask Blur, Liquify, and Smudge—at both before-preparation and after-preparation lifecycle points: 24 independent owners, with 18 assertions each. Each case uses its own hidden-WARP Application, graphics/chrome, worker pool, and sink. Weak-only draft observations establish that the draft remains alive through owner detachment and expires inside the acknowledged final MTA retirement hook. This qualifies the live-draft shutdown slice only; it does not close P1 or G1.

## Protocol

The private Debug/WARP build and run receipts report exit code 0. Private image SHA-256: `1ED9EEB7FAEFD89348AB92E9A289E80764EA63B64A6893B71116674A6D3FC2A2`. Build completion UTC: `2026-10-02T13:52:15.8630846Z`; run completion UTC: `2026-10-02T14:26:05.1417394Z`; qualified archive completion UTC: `2026-10-02T14:28:34.7702547Z`. The archive contains 562 source inputs and 25 evidence files. Source/evidence manifest SHA-256: `20B60B074682DE3CB583AC8BA3C36EAB51E4011F03096F70E38525379224FA02` / `AC502A0EE1961CD5AAE81512E863F3B01D59F21F94290F4E81C356BC0E7D2D99`.

The native run forced all six profiles after a six-profile matrix start at `2026-10-02T14:33:20.6725057Z`. All profile receipts and verified-controls passed; the 585 frozen inputs matched both the live files and native archive source manifest. Every source and evidence archive entry in both archives was independently SHA-256 checked.

## Results

The private run reports 432 live-shutdown controls, with the unchanged 390 processing and 52 application-shutdown controls passing. Native x64 Debug, Release, and ASan Debug logs each report the same 432/390/52 counts; the exact-control verifier also checked the full x64 suites, including unchanged lifecycle, preview, clipboard, publication-retry, scheduler-retirement and ISA controls. ARM64 results are cross-builds only.

| Official profile receipt | Exit code | Completed UTC (receipt string) |
| --- | ---: | --- |
| `p1-live-shutdown-native-r1-x64-ASanDebug.log.result.json` | 0 | 2026-10-02T14:52:27.7554033Z |
| `p1-live-shutdown-native-r1-x64-Release.log.result.json` | 0 | 2026-10-02T14:58:04.8041764Z |
| `p1-live-shutdown-native-r1-x64-Debug.log.result.json` | 0 | 2026-10-02T15:06:20.9042701Z |
| `p1-live-shutdown-native-r1-ARM64-ASanDebug.log.result.json` | 0 | 2026-10-02T14:35:32.1974931Z |
| `p1-live-shutdown-native-r1-ARM64-Release.log.result.json` | 0 | 2026-10-02T14:38:09.8621909Z |
| `p1-live-shutdown-native-r1-ARM64-Debug.log.result.json` | 0 | 2026-10-02T14:40:10.1168132Z |

Native matrix completion UTC (receipt): `2026-10-02T15:06:20.9478751Z`. Verified-controls completion UTC (receipt): `2026-10-02T15:06:20.9478751Z`. Final native qualified archive completion UTC (receipt): `2026-10-02T15:07:11.8094238Z`. Native archive: 585 source inputs and 327 evidence files; source/evidence manifest SHA-256: `99FCC75EBC6FDB095E472C7B92B151DBE73C176FFD3CAFEEB11E6B9E165CC58C` / `0FD2B0CAED23661D08188D23BBE1E6CCE1C243388CDE1389CF829AF53D04083A`.

## Limits

This qualification does not close P1/G1 or cover actual API lease retirement/fault injection or measured shutdown timing/completion. ARM64 was cross-built only; no ARM64 runtime qualification is implied.

## Evidence archives

- Native qualified archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-live-shutdown-native-r1-qualified`; manifests `source-manifest.json` and `evidence-manifest.json`.
- Private qualified archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-live-shutdown-private-r1-qualified`; manifests `source-manifest.json` and `evidence-manifest.json`.
