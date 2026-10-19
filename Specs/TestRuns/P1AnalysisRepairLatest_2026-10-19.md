# P1 exact-status test repairs: completed affected validation — 2026-10-19

Status: the six test-TU repair step is ready to commit. P1 remains WIP; latest complete canonical analysis and the broader delivery gates remain open.

## Scope and inputs

The source is base `8c3bd38c39120d7beb64a1f58b5d91ebca2f8b3d` plus the recorded working delta. The only test change after the previously qualified full Debug r1152 / real ASan r1148 is the explicit signed32 terminal-status cast in `StoppedOriginal`. Earlier repair changes preserve exact expected statuses, all runtime memory predicates and all named assertions; only constant-positive requirements become static assertions.

Full Release r1153 has source-freeze `B8D45020646AFE203AB40B36FE29418AAF971F451A5B3708876ACA8238C6570F`, delta `971A77B499C57152B9DC63F9EA119393EC3B6E35B2719924C25922BF59755184`. The refreshed builds have freeze `5BEC5950C7C2F39EFDF5B22D75DF934042BE0C83BFB2F89A1B6ED7F0B7B80463`, delta `359E38E87C238AF5977CD14589488FB763DCD0BF317110C0C528978D11829DF2`. Main compares every non-Markdown Root input between these freezes: all paths and bytes agree. Differences are progress/evidence Markdown only. Each attempt independently records matching source bookends.

## Actual builds and immutable archives

| Attempt | Profile | PID | Exit | Archive files | Manifest SHA256 |
| --- | --- | ---: | ---: | ---: | --- |
| r1153 | x64 Release | 33612 | 0 | 3117 | `4C431E0F0EA4065C142F540519F91E9AB32C217FB6FFAAC0C4EFE52131C90CF5` |
| r1155 | x64 ASan Debug | 70256 | 0 | 3239 | `40EAE003EE3FB6405FAD14A279A61C08847103F514589CE45F51C8FFE0782C8B` |
| r1156 | ARM64 Release | 11120 | 0 | 2909 | `B0C74170BB4C4139E2E733F6135B7C7DD5B1EE7D483D6D7D79CC31BEE9650A3E` |
| r1157 | ARM64 ASan Debug | 73844 | 0 | 2913 | `E9336EA06782FD7164EEFA8157BFBE86B37233E7F994CCD44A7534576DA8DEF7` |
| r1159 | x64 Debug | 82348 | 0 | 3104 | `AAED082334CAD08512268993DABE260A6A6904989F96AACA31A83E55B5D06EB4` |

Each successful build independently verifies 994 actual compile tracking records, all linked object bytes and five matching executable/full-PDB pairs. This is a compile-record roster, not a claim that all 994 TUs were freshly compiled. x64 linked ISA reports have zero violations; ARM64 images are AA64 and ASan records require real instrumentation and the forced sanitizer header.

## Runtime qualification

- Full Release r1153: 152 exact Application stages / 11,313 stdout assertions; 64 Polygonal cases / 729 facts; all previous Application facts plus precisely 221 additional dispatcher facts. Ordinary suite predecessor identities total 45,476 after exact profile handling. Both crash probes, all nine fresh independently decoded/CRC-checked harness PNGs, 121 new AL/RCX controls and the three-trial 128-layer / 1,024-name-unit smoke pass. The smoke's performance gate remains unmeasured.
- ASan r1155 affected Document suite: PID71528 exits0, all 23,614 prior Document facts retained. PID6864 exits0 for the 256-round raster stress. The checker verifies 285 current Document objects and 284 exact inherited predecessor objects, source/image/PDB bookends and that the final terminal-status TU is the sole changed non-Markdown input relative to full ASan r1148. Earlier full ASan r1148 separately supplies its unchanged-scope full suite/UAF/worker-stress/capture evidence; this affected run is not a new full five-suite runtime.
- Debug r1159 affected Document suite: PID89320 exits0, all 23,614 prior Document facts retained with the same single-TU/284-object/source/image/PDB checks against full Debug r1152. Full Debug r1152's independent archive/runtime readback also passed (45,476 ordinary predecessor facts); it predates the final cast.
- ARM64 Release r1156 and ASan r1157 are cross-build qualification only. No native ARM64 runtime was available. ARM64 Debug and latest complete integration remain final-milestone obligations.
- The final cast also passed one affected TU Release analysis, PID85940, source `A6C0EDD2388600EC487EF90526C150FBECC3C18962FC12FF58AC090D45B25253`. This does not replace a latest complete canonical analysis gate. Earlier r1149's only remaining diagnostic was that exact status comparison.

## Retained unsuccessful attempts and reader corrections

- r1154 was interrupted during ISA controls, actual child exit1073807364. No passing wrapper receipt exists. Main's post-resume inspection finds all original inputs unchanged; 111 copied attempt/product/tracking/partial-ISA artifacts are byte-verified under `r1154-interrupted-artifacts` (manifest `4BD97734196608E354AC7D4EFAE2CD71FD60BE163E48D0A9A2F8D26963A225A9`). No successful build is inferred from that attempt.
- r1158 Debug exits1 while Set-Content cannot write a small generated ContributionBounds disassembly fixture, reporting not enough disk space. Its 2,952-file archive remains verified (`D045CE2797772BDFB11D7980FADA9320B6D36FE2094414A8FB126B39765C86B2`). A later small write succeeds in the same directory and free-space inspection reports available capacity. No causal storage fix is claimed; r1159 is a fresh passing attempt with unchanged product/scanner code.
- Release reader refusals r1/r2 incorrectly demanded identical profile-dependent allocation/cancellation counts. Main preserves them, verifies the exact 60 sweep identities and per-profile totals against the unchanged complete failure/cancellation/recovery predicates, and preserves the three literal area results (2/6/2) and 128MiB scratch bounds. Release totals are1,359 allocation prefixes /161 cancellation checkpoints; the previous ASan totals are2,188/168. Debug iterator proxies cause extra arena allocations; Workspace::Allocate also calls Pulse. No predicate or golden is changed to match output.
- Reader r3 omitted the already qualified 221 dispatcher additions when computing the Application total; r4 corrects that baseline. Its report is retained before r5 corrects only the predecessor count label to subtract three explicit ASan-only host facts. No native runtime is replayed for these reader/report corrections. The final reader requires exact source-bound host conditionals and otherwise all named predicates. New helper readers have no newly executed negative-control matrix; old controls do not transfer.

Cohort: `D:/RedPrismBuildCaches/.build/p1-analysis-repairs-native-r1/`. Per-attempt archive/profile JSON and `r1153-runtime-qualified.json`, `r1155-document-runtime/qualified.json`, `r1159-document-runtime/qualified.json` bind all claims to their original bytes.

## Remaining work

The private worker-target-capture candidates, new 100x40 numeric goldens and their private build/readback issues are outside this source. Full current canonical analysis, subsequent code integration, ordinary activation, initial Ctrl/lift, all nine P1 delivery gates, measured G1 and eventual Done/index/merge steps remain open. No Photoshop performance claim follows from these checks.
