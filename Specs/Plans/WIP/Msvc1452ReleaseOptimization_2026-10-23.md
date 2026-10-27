# MSVC 14.52 and Release optimization

Status: ACTIVE qualification; implementation complete
Date: 2026-10-23
Repository: RedPrism
Base: 42861f11451d8528627133cb6681938ee216fd4e

## Checklist

- [x] Isolate this work on ej/msvc1452-release-20261023; preserve the original checkout.
- [x] Retain the original x64 Release build, image sizes/hashes and frozen runtime closure under .build/msvc1452.
- [x] Audit all six profiles before changing policy. Both sides select installed/default MSVC 14.52.37025; there is no retained 14.51 installation, so no compiler-version speedup is claimed.
- [x] Require MSVC 14.52.x and enable Release /Gy /GL /OPT:REF and full /LTCG with non-incremental linking.
- [x] Forward the exact compiler and WPO policy to the unchanged adopted DxUi API pin; give the changed archive policy a separate fingerprint/output root.
- [x] Verify evaluated native profiles, transitive archive settings and broken-policy controls; update build/consumer specs and documentation.
- [x] Execute the local build/native/tooling matrix; passing receipts, failed attempts and remaining acceptance limits are recorded below.
- [ ] Compare final image/archive sizes, build duration and unchanged-fixture runtime/resource samples. Retain original failures, interrupted/noisy attempts and activity observations.
- [ ] Publish local reviewed evidence and settings differences, fold the changelog and validate. Move to Done only after required local qualification is complete.

## Measurement and scope

The DxUi plan of the same name coordinates the five repositories. This repository owns its application measurements and its existing dependency API adoption. The original and candidate binaries must use identical fixtures and the same exact toolset; the Release compiler policy is the intended experimental difference. An initial sample collected alongside other builds is diagnostic, not a quiet-host/non-regression pass. Retain frozen baseline images and every attempt; do not silently rebaseline.

Resource-only projects have no C++ bodies. Independently owned pinned repositories and DLL boundaries remain separate build/optimization units. Existing feature/performance holds are not closed by this compiler-policy change.

Generated raw evidence: .build/msvc1452/audit-before, baseline-build.json, baseline-bin and initial native benchmark reports. Review and retain selected local evidence before closeout.
## LTO runtime attribution

The first Release candidate linked successfully but the ISA gate rejected eight instructions in IoTests. The matching PDB attributes the exact std::_WChar_traits<wchar_t>::find procedure to IO.Tests.Runner.obj and marks an inlined wmemchr. Installed STL __msvc_string_view.hpp calls wmemchr; UCRT wchar.h guards AVX2 with _Avx2WmemEnabled and count >= 16. The linked comparison resolves to that exact global and branches to the scalar path. Add only this reviewed object/procedure pair to the existing exact-range runtime allowance. Keep the failed image/PDB/report and prove wrong-owner/name and out-of-range vector controls still fail. No whole-object or wildcard allowance is added.

- [x] Verify actual rejected image against the narrow allowance and new refusal controls; retain attribution evidence.


## Qualification workspace and current gates

The user authorized E:\RedPrism.Perf on 2026-10-23 for worktrees/builds/evidence. Final qualification lives in E:\RedPrism.Perf\msvc1452 on ej/msvc1452-qualification-20261023; original Z: baselines remain retained and hash-verified frozen copies exist under E:\RedPrism.Perf\msvc1452-frozen. The four consumer API pins are unchanged. Source edits during initial builds were correctly rejected by attestation; retain those attempts and rerun against frozen inputs.

The user approved interactive tests when ready. DxUi preserves its Start/Cancel and desktop-restoration checks. The user explicitly approved the visible 30-second automatic-start countdown for remaining retries. Native ARM64 execution is unavailable on this x64 host. Hosted CI provisions and verifies the required 14.52 family through .github/actions/setup-msvc; the download/install branch remains pending a real GitHub-hosted execution. Local -VerifyOnly and refusal controls must pass without installing software. All required gates stay open until their receipts are reviewed.

## Local qualification checkpoint

Updated 2026-10-23T13:06:21.8133932Z. All six local builds passed. Release and ASan Full passed. Debug Full failed; affected Runtime/Captures and Performance remainder retries passed and remain distinct from an all-green original Full run. Original Debug/ASan failures are retained as repeatability concerns. The exact LTO/UCRT wrapper attribution passed the 274-region ISA replay and narrow identity controls.

Native ARM64 runtime and a real hosted CI provisioning run remain unavailable/pending. The plan stays ACTIVE: local builds do not establish native ARM64 support or close existing feature/performance holds. Paired runtime/resource analysis and reviewed image sizes are recorded in Measurements/Toolchain/2026-10-23-msvc1452/; the consolidated report is retained externally under the approved E:/DxUi.Perf/reports/Msvc1452ReleaseOptimization_2026-10-23/. During the original optimizer-only phase, all first-party native C++/header/shader files remained unchanged.

## Requested local failure remediation

The user limited completion to local x64 tests and ARM64 cross-builds. Native ARM64 runtime and hosted provisioning remain separate gates. The unchanged-source Debug Full rerun passed at 2026-10-23T20:44:46Z; its native evidence is retained under E:/RedPrism.Perf/failure-remediation/initial-full. That repeat does not establish the earlier failure's cause.

The earlier hidden WARP exit 5 ended after the WAND pointer fixture at a silent SettleJobs check. The repair records TimedOut, Quit or Failed through the existing diagnostic channel and preserves the 10-second bound, failure behavior and exit code. Fresh full x64 Debug/Release/ASan and three ARM64 cross-builds remain pending for this candidate. Original optimizer-only frozen binary and runtime pairs remain historical; final repaired artifacts must be measured separately.

Docs/gallery review: this changes failure diagnostics only. Product geometry, rendering, visual fixtures and supported behavior are unchanged; gallery regeneration is unnecessary.

## Final repaired-source local matrix

All six builds passed; Full x64 Debug, Release and ASan Debug passed at 2026-10-24T00:48:25Z, 01:01:10Z and 01:29:08Z respectively. Final independent validation, format, changelog and PrePush planning checks passed. LocalValidation.json retains the exact records. Original image/PDB/ISA inputs are verified in original-isa-and-x64-artifacts.zip (SHA256 66A679D33E3390C56BF115D13135C49DBFEC9805FE36B2D5493D7428C6E4184E); the failed Debug attempt remains separate and its cause unproven. Final repaired-source closures are frozen; the additional size/performance comparison and overall report closeout remain pending. Native ARM64 execution and actual hosted CI provisioning remain outside the user's agreed local scope.
