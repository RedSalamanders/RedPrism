# P1 repaired tests: full x64 ASan — 2026-10-19

Status: canonical full real-ASan run r1148 passes for the exact candidate below. P1 remains WIP.

## Source and actual run

Git base `8c3bd38c39120d7beb64a1f58b5d91ebca2f8b3d` plus working-delta SHA256
`196D950118C1052CA16F5AEA6DB81C96989D0F7777A2BE1352E4EE510BBA2A29`; this includes the six test-TU analysis repairs.
The 1,825-input source-freeze SHA256 is `0AB879EF80A0B057E8269317CFFB434FB51271B19AD5C8F2E03A4085930FC6D8`.
This qualifies the frozen working tree, not clean base HEAD alone. Source bookends match.

Canonical `test.ps1 -Platform x64 -Configuration "ASan Debug" -MaxCpuCount 1`: actual PID
**43184**, exit **0**, 2026-10-19T02:26:10.257255+00:00–2026-10-19T02:52:37.691077+00:00 UTC.

## Independently checked results

- All five suites pass. All 994 actual compiler records carry the real ASan profile and forced header; all linked objects and five matching executable/full-PDB pairs independently verify. All five linked ISA reports have zero violations and the unchanged allowlist.
- Full Application: 152 exact stages / 11,313 stdout assertions; selected dispatcher 64 cases / 729 facts. Every r1144 Application predecessor and every focused 64-case fact remains. Across the canonical console, all 72,666 stable predecessor PASS records remain; timestamped probe/tool receipts are checked separately.
- Concurrent raster stress completes 256 rounds, saturated 64-worker stress passes, the genuine heap-use-after-free probe is detected, and both main/worker crash probes pass. Actual UAF diagnostic SHA256: `DF1BBE4BB8A5DA753017B60664AECA06959070CA0CB561A594E0A210CFE476E5`.
- All nine fresh harness PNGs pass independent bounded decoding, CRC, dimensions and nonuniform-content checks. The three-trial layer-row smoke validates 128 names of 1,024 code units, exact memory accounting and both renames; its performance gate stays explicitly unmeasured. All 121 new default AL/RCX ISA controls pass.
- The immutable same-attempt archive contains **3,250 files**, all bytes verified. Manifest SHA256: `FEE139B1D57FB4D884B0F7DEA38C7BCA77AB5BB545B81DF123FD0CE91A3A0EF5`. Source, profile, PE/PDB, runtime, probes, stress, smoke and captures refer to this same archive identity.

## Retained repairs and boundaries

The original r1145 analysis failure and rejected repair r1147/cancelled r1146 remain separate evidence in
`P1AnalysisRepairs_2026-10-19.md`. No successful repeat establishes the cause of historical sampler/inverse ASan failures.

Preserve the initial profile-reader /DEBUG:FULL refusal (canonical emits /DEBUG), the r1148 identical-source
archive-copy refusal and the runtime reader's timestamped layer-row-path refusal. Main proves complete debug/PDB
content, verifies both identical archived copies and independently checks the actual timestamped three-trial
smoke output. The parser preparation refusal is retained too. These are corrections against the same original
products and logs: no native replay. The new generic helpers have no newly executed negative-control matrix.

Cohort: `D:/RedPrismBuildCaches/.build/p1-analysis-repairs-native-r1/`; receipts
`r1148-archive-qualified.json`, `r1148-profile-qualified.json`, `r1148-runtime-qualified.json`.
The separate ARM64 Release/ASan cross-build receipts do not establish native ARM64 runtime. Latest full
Debug/Release and static analysis, ordinary activation, initial Ctrl/lift, remaining delivery gates and measured
G1 are separate. The private worker-target-copy candidate is absent from this qualification.

The later exact terminal-status cast exposed by r1149 is outside this frozen input set; affected requalification is separate.

Later checkpoint: [completed affected repair validation](P1AnalysisRepairLatest_2026-10-19.md). The older input sets above retain their original scope.
