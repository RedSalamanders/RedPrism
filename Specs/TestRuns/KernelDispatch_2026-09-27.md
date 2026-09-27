# CPU dispatch and linked instruction boundary — P0.8

Date: 2026-09-27. Host: R1 (Windows x64), the same host recorded in
[MemoryPipeline_2026-09-27.md](MemoryPipeline_2026-09-27.md).
LLVM decoder/PDB tools: 22.1.3 from the discovered Visual Studio installation.

## Implemented scope

- Immutable Scalar function table for Tables, Gradient Map, Grain, Noise, Lens Distortion and Color Cube.
  Each public wrapper dispatches once to a noinline entry in `Kernels_Scalar_Adjust.cpp`.
- The six kernel bodies and five private helpers were compared with the preceding committed source. They match
  after removing whitespace and the necessary `Adjust::kCubeDimension` qualification. Existing goldens run through
  the public wrappers. No approximate math, new vector arithmetic or high `/arch` option was introduced.
- CPU/OS detection checks every AVX2 prerequisite and executes XGETBV only after OSXSAVE. Only Scalar is built;
  Auto selects it. Strict `--isa` accepts the five named tiers and refuses unavailable/unimplemented choices.
- Every x64 build scans all five linked first-party executables and verifies their full PDB identity. Reports
  retain binary, PDB and allowlist hashes, decoded instructions and their section-contribution attribution.

## Results

| Gate | Result | Evidence under `.build/logs/` |
| --- | --- | --- |
| Complete x64 Debug | PASS | `dispatch-x64-Debug-tests.log` |
| Complete x64 Release | PASS | `dispatch-x64-Release-tests.log` |
| Complete x64 ASan Debug, extended stress and real sanitizer defect detection | PASS | `dispatch-x64-ASanDebug-tests.log` |
| Final stricter scan and compiled controls in all three x64 configurations | PASS | `dispatch-scan-Debug.log`, `dispatch-scan-Release.log`, `dispatch-scan-ASanDebug.log` |
| ARM64 Debug / Release / ASan Debug cross-builds | PASS | `dispatch-ARM64-*-build.log` |
| Skills, formatting and whitespace | PASS | 15 skills; source formatting and `git diff --check` |

There are 49 dispatch/CLI checks per HostTests run, and three actual-process rejection checks per configuration.
ASan runs those 49 checks again in its stress invocation. All 13 forced-scalar benchmark smoke rows identify
`isa=scalar`, with stable output checksums and three matching copy/adopt pairs. Both crash probes, WARP self-tests
and nine harness captures per configuration pass.

Final decoded VEX/EVEX counts (all attributed to reviewed MSVC runtime exceptions; zero first-party tier
instructions, zero EVEX instructions and zero violations in these product binaries):

| Image | Debug | Release | ASan Debug |
| --- | ---: | ---: | ---: |
| RedPrism | 11,690 | 414 | 11,690 |
| HostTests | 11,690 | 174 | 11,690 |
| DocumentTests | 11,675 | 126 | 11,675 |
| CompositionTests | 11,675 | 102 | 11,675 |
| IoTests | 11,690 | 217 | 11,690 |

Full records are in `.build/IsaScan/x64/<configuration>/<image>/report.json`; control projects, PDBs and logs are
in `.build/IsaScan/Controls/<configuration>/`. Every configuration proves correctly placed Scalar, AVX2 and
AVX-512 code passes, including a separately counted EVEX instruction. AVX2 in a baseline object, EVEX in an AVX2
object, a runtime-named object from the wrong library, a runtime-named function in the wrong object, and absent
or mismatched PDBs are refused. High-ISA controls are compiled and scanned, never executed.

## Review corrections and limits

Cheaper agents performed the mechanical extraction, CLI work and bounded test work. The primary compared the
source bodies, reviewed the changes, fixed resource placement and diagnostic wording, and ran all gates.
An independent scanner review led to per-object `wmemchr` exceptions and explicit EVEX-control counts.
The baseline confirms MSVC emits runtime-dispatched `wmemchr` inside `ImageCodecs.obj`, `HostTests.obj` and
`IoTests.obj`; only its exact PDB procedure range is exempt. Surrounding functions remain checked.

The first Debug run stopped because the benchmark parser still expected the old row format. After adding the
explicit ISA field and forced-scalar invocation, the full Debug gate passed; the retained log is that complete run.

This gate checks decoded VEX/EVEX boundaries in first-party linked images. Undecoded prefixes in inline data are
reported separately. Third-party DLL dispatch, native pre-AVX execution and native ARM64 execution are not
qualified by this scan. No Intel SDE run was performed. P2 still owns optimized SIMD implementations and speed
measurements. UI responsiveness, RAW decode/develop and Photoshop comparisons remain open in the parent plan.
