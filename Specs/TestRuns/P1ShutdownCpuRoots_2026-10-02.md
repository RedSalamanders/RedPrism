# P1 shutdown CPU roots: sampling, overlay and benchmark images

Date: 2026-10-02. Scope: private residual application-root patch, 3 files / 85 lines, followed by a fresh incremental six-profile qualification. This record does not close A3, P1 or G1.

## Change and limits

The patch retires the application-owned sampling raster and drag/overlay selection geometry, plus the benchmark's two document roots and two raster roots, on the acknowledged final worker. It preserves the benchmark object and its timing storage/address, completion metadata/flags and known-path cleanup through shutdown. The three-file, 85-line scope is the reviewed private draft recorded in the WIP plan; the native source freeze is independently verified below. No claim is made that actual API initialization failures are covered.

## Private original and candidate evidence

The original standalone application run has exit code 5, image SHA-256 0C06ADE5A0AC0F04DAB4995B7B89FEE5B1F99F629456B793AB11F6267FA83986, and 52 application-shutdown controls with 8 failures. Its failure archive is p1-shutdown-roots-original-r1-failure: 559 source inputs and 23 evidence files. Source-manifest SHA-256: EB926C0FBFE195CED09D2989B2F07B4FC86B8229BFC89EAF38327DBEC17A413F. Evidence-manifest SHA-256: BFF97F06ADCFE141FE87056F3A65345086C4337AFA682BC0A2B5AAD5A690A819. Archive completion UTC: 2026-10-02T12:10:13.1420864Z.

The private candidate completes the full Debug WARP application with exit code 0, image SHA-256 AC2EAA2E95E8CE08DD60B5D102535BD263CDC27D2F3385839154B8951E45B03E, and all 52 shutdown controls passing. Its archive is p1-shutdown-roots-candidate-r1-qualified: 559 source inputs and 39 evidence files. Source-manifest SHA-256: AE41423C7C5C1E5226027F6C6ED7ED083990ADE880ED122C827F68317525756A. Evidence-manifest SHA-256: 0146C7E10F02A6B8312FC2FDDA54A21B0BDDD1E24929E19A349A6D07F502842C. Archive completion UTC: 2026-10-02T12:45:02.7063280Z.

Both archives were checked against their completion receipts; every manifest entry exists and its archived SHA-256 matches. All original 38 controls pass in the original and candidate; the eight original failures belong to the four new after-retirement assertions repeated before/after worker preparation.


## Before/after preparation observations

The four original failing assertions in each phase are host-drag-and-outline-roots-retired-after-shutdown,
benchmark-metadata-preserved-and-document-image-roots-retired-after-shutdown,
host-cpu-roots-retired-on-acknowledged-final-mta-worker, and
second-shutdown-preserves-empty-benchmark-and-single-callback. Their exact names/results remain in the archived
logs; all 52 controls pass in the candidate.

Independent before-shutdown checks establish real 16 MiB sampling and benchmark rasters, distinct storage,
the four-point overlay selection and benchmark metadata. The fixture drops its extra sampling reference before
Shutdown. After acknowledged MTA retirement, drag/overlay and benchmark image roots are empty; the same benchmark
object and timing address, result flags and metadata survive. Existing session/history/private-clipboard ordering,
caller apartment and repeat-Shutdown controls remain unchanged. Known-path cleanup stays with the benchmark
object; these controls do not prove every external path-cleanup failure.

## Fresh native incremental matrix

Runner start UTC: 2026-10-02T12:49:07.5709538Z. Runner scope: Fresh standard incremental x64 full Debug Release ASan gates and ARM64 cross-builds; three application files changed. Final process exit: 0; completed UTC: 2026-10-02T13:08:31.6078117Z. Frozen source count: 582. Native source archive entries: 582. Native evidence archive entries: 327.

Source-manifest SHA-256: 848B91449C7737F3FD31AC19E39159FC5CCB8833F6A7E72857F0DB89F0658803. Evidence-manifest SHA-256: C7F9226CDE98AB9E7E6DC8567C02E8149E9782C5C356DD344A5D2CA50DA23C7F. Every frozen source still matches its SHA-256 and exactly one source-manifest entry; every archived source/evidence file matches its manifest hash.

Archive: C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-shutdown-roots-native-r1-qualified.
Skills and formatting pass (15 skills, 523 C++ inputs). The x64 ASan profile includes its real sanitizer detection
probe and stress; all x64 profiles include both crash probes, unchanged existing suites, hidden WARP application
and harness screenshots. Exact previously integrated lifecycle, transform, publication and ISA controls also pass.

| Profile | Gate | Exit | Completed UTC |
| --- | --- | --- | --- |
| x64 Debug | Full suites and 52 shutdown controls; incremental rebuild | 0 | 2026-10-02T13:08:31.5701419Z |
| x64 Release | Full suites and 52 shutdown controls; incremental rebuild | 0 | 2026-10-02T13:03:19.7512092Z |
| x64 ASan Debug | Full suites and 52 shutdown controls; incremental rebuild | 0 | 2026-10-02T12:59:43.3805962Z |
| ARM64 Debug | Incremental cross-build; no runtime claim | 0 | 2026-10-02T12:50:51.6704297Z |
| ARM64 Release | Incremental cross-build; no runtime claim | 0 | 2026-10-02T12:50:24.5903490Z |
| ARM64 ASan Debug | Incremental cross-build; no runtime claim | 0 | 2026-10-02T12:49:42.2427678Z |

Each x64 profile's application self-test log contains the complete 52-control shutdown set with zero failures. ARM64 entries are cross-builds only; they do not establish ARM64 runtime coverage. The native source freeze contains 582 inputs and its completed profile receipts exactly match the verified-controls receipt.

## Remaining work

Actual processing-initialization API failures, undrained/suspended GPU, event and clipboard leases, live brush/warp/heal drafts, and first/second shutdown coverage for those remaining cases stay open. Broader A3, P1 and G1 remain open, as do the measurements and other follow-up items in [ShutdownCpuRetirement](../Plans/WIP/ShutdownCpuRetirement_2026-10-02.md). These results establish retirement of the selected sampling, overlay and benchmark image roots only.
