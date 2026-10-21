# Polygonal lifetime: full canonical native ASan checkpoint

Date: 2026-10-18
Status: QUALIFIED for implementation `928e871509f85025c90812122fd3be5a51dbdde2`; P1 remains WIP.
Source documentation HEAD: `7b4522eb18e2293d3281e2cb448ff47603a7da92`.

## Actual command and identity

Canonical `test.ps1 -Platform x64 -Configuration "ASan Debug" -MaxCpuCount 1`, normal incremental build.
Attempt `r1136`, PID 85684, exit 0. UTC interval: 17:57:46.780243–18:57:57.212642.
The coordinator and child joined before qualification. No automatic retry.

1,485 frozen project/code/domain/policy/toolchain inputs cover all 994 translation units.
Freeze SHA-256: `657E4F7EF66911D63063AA404D2252B70BD74E203700EC53B10919C07C7EB5D4`.
Stdout SHA-256: `76A534BE3863DE254AA115D9A48F02EA63BA4B97185551B5705B1A40C23E74FA`.
Stderr SHA-256: `901254C3F75CDEE70982437ABD310496D9EF5B3B6D0C24CD09EBD861545D54C5`.

## Independent qualification

- All 994 actual compiler records retain W4/WX/SDL/stdcpplatest/conformance/UTF-8, the correct CRT/optimization, real ASan instrumentation and the forced sanitizer header.
- Five x64 executables match their full PDB GUID/age. All linked ISA scans pass with the unchanged allowlist.
- HostTests (normal and stress), DocumentTests, CompositionTests and IoTests pass. Application completes all 152 stages / 10,920 stdout assertions, preserving every exact previously qualified stdout/stderr assertion; selected original Polygonal dispatcher is 35 cases / 338 facts.
- The 256-round raster concurrency and saturated 64-job worker stress pass. The actual named UAF child produces the required heap-use-after-free report; main/worker crash probes pass. Kernel, copy/adopt, UI/direct/prepared/sampler/layer-row/gradient, tooling, restore, ARM wiring, WPR and offline RAW controls pass.
- Nine fresh application-harness PNGs pass independent chunk CRC, bounded inflate, dimensions, alpha and nonuniform-content validation: empty-shell 96/144, transform, selection, brush, retouch, adjust, canvas-size and crop at 96 DPI.
- Main visually inspects empty-shell, brush and adjust captures. The first two show a layer-header overlap; static source rectangles do not explain it. H4 retains a separate post-arrange/hit-region diagnosis obligation. PNG integrity is not visual acceptance.

Archive root: `D:\RedPrismBuildCaches\.build\p1-lifetime-full-native-asan-r1`.
Archive: `Archives/P1-LifetimeFullNativeAsan-x64-ASanDebug-r1136`, all 1,757 files independently verified.
Completion SHA-256: `5FBF0A5E86D6A6D7063C33749D599F5FD85C2F2CEAAA1A3900FBCB1BC83EB618`.
Main final receipt: `r1136-main-qualified.json`.

## Preserved reader refusal and limits

The supplemental C++ profile reader initially looked for the command tlog outside its actual project `.tlog` directory. Its original source, failed log and progress record remain preserved. The corrected unique archived lookup retains every profile predicate and passes against the same artifacts; no build/test was replayed. Source-only controls retain two positives and 22 refusals, including joined-child/actual-exit checks.

The earlier inherited sampler-capacity and inverse-selection held-gate ASan failures do not recur. Their original evidence and final audit obligations remain; this passing run does not establish either historical cause.

Private 38–41-case focused test extensions are not included in this full-native receipt. ARM64 Release/real-ASan cross-builds for the same implementation already qualify separately; native ARM64 runtime is unavailable. Latest full Debug/Release/analysis, ordinary activation, G1 and the other P1 delivery areas remain open. No Done or main-merge claim.
