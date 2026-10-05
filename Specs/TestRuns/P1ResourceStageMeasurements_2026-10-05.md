# P1 resource-stage measurements - 2026-10-05

**Status:** six serial Release telemetry observations completed; this is a qualified telemetry record, not an acceleration result. The original P1 G1, first-display, activation, and exit gates remain open.

## Qualification checklist

- [x] Bind observations to the frozen source and qualified x64 Release r137 archive.
- [x] Confirm all six actual child exits, timestamps, and stdout hashes against the files.
- [x] Validate all 18 raw resource rows per child with the primary sampler helper.
- [x] Retain initial-display, excluded-warmup, and recorded boundaries, including absent samples and overruns.
- [ ] Complete native resource activation and the original G1 / first-display qualification.

## Frozen source and runner

Source commit: e0a9c6721c04fd79570d1ef2899276d4af640d16 (e0a9c67), captured as 811 inputs for the qualified x64 Release r137 run. Archive:

C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-resource-plan-x64-Release-r137-full-qualified

Archive completion SHA256: D2BA5D8A50795361DEC0CD6D846D7168FEDC67F8685D75D6DB82319410FA0E88. This matches started.json and the actual archive-completed.json bytes. Release r137 qualification child PID 78828 exited 0, 2026-10-05 17:21:39.660689+02:00 to 17:27:31.663318+02:00.

The qualification's source-input freeze SHA256 is 1C243ED8FD920E9B61702163C879002A4302C16D9FBF3A41893588D1621AB754. The Native binary-freeze.json in this sampler run has SHA256 FCCF405652B7A152505D9A04D77D2281A4CC436D713298DC6E7A63C782DD2633.

| Frozen file | Length | SHA256 |
| --- | ---: | --- |
| RedPrism.exe | 5,475,840 | 0FB1D2A368DE5AC0F0013D748579EFB3ECA3B89CAC08A5DCF3088AC23C3741D5 |
| RedPrism.pdb | 56,209,408 | 74842C99A254AEFC1E081A76DB111660F972149149BFE34D1B6AAE7232FF22A6 |
| DxUi.provenance.json | 1,774 | 9FED06DC1D633D70CAB9C6039B75AA1E1777949F2F124D10CD72D9713A67256F |
| yyjson.dll | 254,464 | C38A714178D7FA390DE313847D581452ABFD29B6D81FE256C13492A865DA5EE9 |
| z.dll | 91,648 | FD992E76B2DA749DD22042745E0E0A63A4E3BD150937C32C399D8C03C7F482E8 |
| zip.dll | 106,496 | 8EF4C51944912E785D6D5B8E0FE66AE54B322EF530DA23753B9B21371C75AA67 |

The primary runner, run-resource-stage-sampler-perf-r1.py (retained as recorder-source.py), SHA256 0D6DF5AF672513EB7575DFCC01D422DBBC31BEB8E77F2A8B73C35A46E594E77A. Its process helper copy SHA256 FA400D19FFA759B827F533E3099F4075C424AE0B0BB3A987B17149871B83BE29. Both match started.json.

## Child receipts

Times are actual sampler child intervals. Every child exited 0. Each stdout SHA256 was recomputed from its .stdout.log and matches the result record. Every stderr file is empty, SHA256 E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855.

| Device / observation | PID | Started (+02:00) | Completed (+02:00) | Exit | stdout SHA256 |
| --- | ---: | --- | --- | ---: | --- |
| Hardware / 3840x2160 | 74492 | 18:04:27.433107 | 18:04:28.241151 | 0 | 1316818082C109E802FA45599006F5361BEC14C6F29060E7DC68D5C1EFC2879D |
| WARP / 3840x2160 | 91124 | 18:04:28.275484 | 18:04:29.368389 | 0 | 0EEE18162023F1DAEE32192281DC5DB19B24944FAF6CD6F7AE775AC80C1868A3 |
| Hardware / 8192x5464 (45 MP) | 96908 | 18:04:29.399459 | 18:04:30.581464 | 0 | E15D386B2B970F5FBA949BB52DD0B9CAF6798C6845AE88B44778F2356F3F3764 |
| WARP / 8192x5464 (45 MP) | 85888 | 18:04:30.613245 | 18:04:34.705883 | 0 | 4473443036597F20931EFE75489B81EC5C5684ED65E6EF81232E9769ABCCCCD3 |
| Hardware / 10000x10000 (100 MP) | 93396 | 18:04:34.738222 | 18:04:35.885577 | 0 | 110BF5814AB9C165DF0CEA3C4F06893EB848C409673EADE7F61A570E477494FF |
| WARP / 10000x10000 (100 MP) | 29856 | 18:04:35.922513 | 18:04:39.762066 | 0 | 135879E83161F9A192FAD412632712EAB4B5CC22C63895327595B76FA36BD659 |

## Initial-display resource stages

The primary helper validated each child's 18 raw UI RESOURCE lines: exactly the fields name, boundary, stage, samples, p99UpperUs, maxUs, and inclusive; nonnegative decimal counts/times; inclusive=1; one operation name; no duplicate rows; and all six stages at each of three boundaries. Result raw rows match stdout. Values below are initial-display inclusive maxima and sample counts. An em dash means samples=0, hence no measurement rather than a measured zero.

| Device / fixture | Texture create | Shader view create | Mip generation | Raster upload | Composition record | Chrome sync |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Hardware / 3840x2160 | 212 us (n=6) | 17 us (n=6) | 4 us (n=4) | 6,867 us (n=3) | 15,345 us (n=1) | 8,445 us (n=6) |
| WARP / 3840x2160 | 100 us (n=6) | 11 us (n=6) | 1,364 us (n=4) | 6,158 us (n=3) | 25,407 us (n=1) | 7,878 us (n=6) |
| Hardware / 8192x5464 | 425 us (n=6) | 34 us (n=6) | 5 us (n=4) | 36,602 us (n=3) | 75,399 us (n=1) | 7,375 us (n=6) |
| WARP / 8192x5464 | 242 us (n=6) | 23 us (n=6) | 232 us (n=4) | 27,341 us (n=3) | 68,632 us (n=1) | 7,209 us (n=6) |
| Hardware / 10000x10000 | 502 us (n=3) | 43 us (n=3) | 6 us (n=2) | 60,385 us (n=1) | 60,415 us (n=1) | 6,708 us (n=6) |
| WARP / 10000x10000 | 511 us (n=3) | 30 us (n=3) | 202 us (n=2) | 47,923 us (n=1) | 50,832 us (n=1) | 6,668 us (n=6) |

Reference IDs map texture-create=6, shader-view-create=7, mip-generation=8, raster-upload=9, composition-record=10, and chrome-sync=11. They are not emitted in the raw rows.

## Original UI BENCH rows

Original recorded ten-run rows after one excluded warmup; values are microseconds. Existing rows and gates are preserved; this record changes no threshold or acceptance decision. Initial frame max is from the separate initial-display boundary.

| Device / fixture | Recorded elapsed median / p95 / max | Apply max | Recorded frame p99 upper / max | Initial frame max |
| --- | ---: | ---: | ---: | ---: |
| Hardware / 3840x2160 | 9,378.5 / 9,844 / 9,844 | 60 | 2,000 / 1,645 | 17,454 |
| WARP / 3840x2160 | 59,726 / 60,863 / 60,863 | 45 | 4,000 / 2,714 | 26,541 |
| Hardware / 8192x5464 | 9,528.5 / 45,606 / 45,606 | 43 | 2,000 / 1,584 | 106,436 |
| WARP / 8192x5464 | 281,012 / 293,971 / 293,971 | 29 | 4,000 / 3,824 | 69,732 |
| Hardware / 10000x10000 | 9,464 / 43,189 / 43,189 | 89 | 2,000 / 1,836 | 101,254 |
| WARP / 10000x10000 | 255,015 / 285,565 / 285,565 | 53 | 4,000 / 3,439 | 52,625 |

All recorded rows report 30 readback copies and zero busy failures. Initial-display frame maxima span 17.454-106.436 ms and remain visible as first-display overruns. All six chrome-sync maxima are 6.668-8.445 ms, above the 4 ms ETW suppression cutoff; histogram capture retains those calls. This reports the configured cutoff and does not promote it into a new gate. Nested stages are inclusive and may overlap; do not sum them to estimate end-to-end time.

## Scope and live matrix

These are serial actual-loop Release sampler telemetry observations for accepted-prefix fixtures. They show instrumented rows emitted and validated against the frozen binary. They do not establish acceleration, complete G1 behavior, first-display acceptance, scanout latency, native activation, Photoshop comparison, or overall P1 completion.

At capture, the separate main normal matrix was still running: attempt r139, x64 Debug test mode; Python parent PID 48820 and test.ps1 child PID 62692, started at 18:06:57 and 18:07:00 +02:00. No completion is inferred.

## Evidence paths

- Native ignored run bundle: Z:\src\RedPrism\.build\p1-resource-stage-perf-20261005-r1
- Qualified Release archive: C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-resource-plan-x64-Release-r137-full-qualified
- Destination in both trees: Specs/TestRuns/P1ResourceStageMeasurements_2026-10-05.md
