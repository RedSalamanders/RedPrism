# RedPrism local compiler-policy qualification

Status: implementation complete; qualification remains ACTIVE. MSVC 14.52.37025 is installed/selected. Release explicitly enables /Gy /GL /OPT:REF and full /LTCG; dependency source/API pins are unchanged. No first-party native C++/header/shader source changed.

All six builds passed. Release and ASan Full passed. The initial Debug Full failed; affected Runtime/Captures and Performance remainder retries passed. The initial Debug failure and earlier ASan rectangle assertion remain repeatability limits; a focused retry is not a new complete Debug Full receipt.

Final read-only checks passed: fold-check, validate, format. Their exact commands, times and logs are retained in E:/RedPrism.Perf/reports/Msvc1452ReleaseOptimization_2026-10-23/FinalChecks/. Folded changelogs remain current. Validation does not erase an earlier failed runtime receipt.

The final [reviewed runtime study](runtime-20261023T200008-9f548f777d48440dbf98e2fe1e483e40-reviewed.md) retains 50 actual native processes. Collection harness and frozen closure hashes were verified, with zero observed concurrent compiler/test-coordinator samples. Independent verification checked raw native metrics, receipts, ABBA/BAAB order, medians, paired ratios, exact sign-flip probabilities and corrected Holm adjustment. No reliable runtime improvement/regression or non-regression guarantee is established; instability and nonsignificant rows retain those interpretations.

Original statistics are retained as superseded: an integer Math.Min overload rounded Holm probabilities. Only double-holm-v2 results are reviewed. The [method source package](study-method-sources.zip) includes the unchanged collection sources, corrected analyzer, exact nonzero-probability controls and independent verifier. [Build duration observations](BuildDurations.md) are descriptive and do not establish a build-speed effect.

Native ARM64 execution is unavailable on this x64 host. Actual GitHub-hosted 14.52 download/install provisioning remains pending; local verification and refusal controls passed. These gates and any listed runtime failures keep the indexed WIP plan ACTIVE.

The consolidated settings, exact binary bytes, performance rows and cross-repository status are retained externally at E:/DxUi.Perf/reports/Msvc1452ReleaseOptimization_2026-10-23/CrossRepository.md. This owner folder retains only its own application/library results. Historical build/test receipts remain evidence of executed runs; recovered intermediate caches do not provide a current SkipBuild attestation.
