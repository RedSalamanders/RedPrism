# Polygonal keyboard/header full x64 ASan qualification — 2026-10-19

Status: canonical full x64 ASan Debug integration r1144 passes for the adopted keyboard/header source. P1 remains WIP.

## Run identity

Canonical `test.ps1` x64 / `ASan Debug` full-suite run; PID **83828**, exit **0**. UTC: **2026-10-18 22:41:44.639512–23:07:52.182135**. The 1,493-input source freeze is `9208CEE7E72FB2A729BE28C6B886A7E4182EDAA88EED54D2A9D47AC72ADAE8A3`. Both `Application.cpp` and `Application.Tests.PolygonalDispatch.cpp` resolve to `57d6631796e96e000ae6523fae0df87edd52329c`; recorded root HEAD is `5897ef8bd7dd886a7fefc4ec470bf8286c2810a3`.

The earlier r1 launcher failed on a missing import before creating an attempt root or starting native work. Main prepared the fresh r2 cohort and ran the still-unused attempt ID r1144. That prelaunch failure is retained; it is not a failed or replayed native run.

## Qualified evidence

- Canonical five-suite run completed. Independent profile readback verifies **994 actual compiler commands** across RedPrism 351, HostTests 136, DocumentTests 285, CompositionTests 138 and IoTests 84; every command carries the required real ASan instrumentation and forced header. All five x64 executables have matching full PDBs and independently checked PE/CodeView identities.
- The runtime receipt records **152 Application stages / 11,092 stdout predicates**, including all exact Application predecessors; the selected dispatcher has **47 cases / 508 facts**, preserving the 44 / 469 focused predecessor. Main and worker crash probes pass. CPU, GPU and I/O suite footers and Host stress rerun pass; raster stress covers **256 rounds** plus saturated-worker stress.
- The genuine heap-use-after-free probe is detected (diagnostic SHA256 `93398E267CB1405FD69B4823744FDC398BCE7B9F89536757503202FE2C310D11`). Nine fresh harness captures are independently decoded and checked for PNG CRCs, bounded inflation, dimensions and nonuniform content.
- Independent archive verification confirms **1,768 files**. All five completion readers (archive, artifacts/profile, PE/PDB, C++ profile and runtime/probes/stress/captures) exit 0. Completion SHA256: `137751EA25389B1045D481D7EBBF2541414FE6C76FF4AC6E743D5B697D725B0F`.

Receipts are in `D:/RedPrismBuildCaches/.build/p1-polygonal-keyboard-full-asan-r2/`; the immutable archive is `Archives/P1-PolygonalKeyboardFullAsan-x64-ASanDebug-r1144/`. The actual attempt root is `.build/incremental-x64-test-ASanDebug-r1144/` in the recorded source worktree. Completion metadata records all five successful reader exits against the same r1144 archive identity.

## Qualification boundary

This is full **x64 real-ASan test/runtime** qualification for the recorded source and frozen inputs. Historical sampler-capacity and inverse-selection ASan failures remain preserved; this pass does not establish their cause or a causal repair. The r1 launcher and any source/artifact-reader refusals remain separate retained records; none was converted into a runtime replay.

ARM64 Release r1142 and instrumented ASan cross-build r1143 are separate evidence in `P1PolygonalKeyboardNative_2026-10-19.md`; there is no native ARM64 runtime claim. This does not qualify Release analysis, ordinary activation, remaining P1 gates, G1, or P1 Done.
