# P1 resume checkpoint — user pause, 2026-10-05

## Resume checklist

- [x] Stop implementation at the user's request: “save everything needed to continue later”.
- [x] Stop and close assigned agents; no owned build/test/benchmark runner remains active.
- [x] Preserve and push qualified integration source `1936fe6` with full Debug/Release/ASan receipts.
- [x] Save and push private source-only WIP `3f49ea2`, including all previously untracked source/test files.
- [x] Copy and reread/hash-verify local helpers, attempts/failures, source diffs and resume evidence.
- [x] Record the unchanged Wand source, exact qualification boundaries and ordered next steps below.
- [ ] Resume only after a new user instruction. Verify all worktree identities and clean states first.
- [ ] Review and integrate the private residency/Warp/mask candidates, then complete host protocol/admission.
- [ ] Complete remaining P1 callers, current validation, native activation and original G1 measurements.
- [ ] Move P1 to Done only after every original milestone exit row passes; P2–P6 remain a separate roadmap.

**Status: HOLD at the human's request. P1 remains incomplete.** No implementation, agent, build, test or
benchmark should restart until the user resumes. This document supersedes older pause checkpoints for the
current work. Qualified slices retain their recorded scope; preservation is not qualification.

## Branches and source ownership

| Role | Worktree | Branch / saved source |
| --- | --- | --- |
| Native application | `Z:/src/RedPrism` | `codex/ui-never-blocks-paused-2026-10-03`, source unchanged from `a254d84`; this pause adds docs only |
| Qualified integration candidate | `C:/Users/eric/.codex/worktrees/p1-opacity-host-proof/RedPrism` | `codex/p1-opacity-host-proof`, qualified source `1936fe6`; this pause adds docs only |
| Private later source | `C:/Users/eric/.codex/worktrees/p1-sampler-callers/RedPrism` | `codex/p1-sampler-callers`, WIP checkpoint `3f49ea2`, parent `e0360b0` |
| Separate Wand candidate | `C:/Users/eric/.codex/worktrees/p1-sampler/RedPrism` | `codex/p1-sampler`, unchanged clean `8f5110d` |

The integration tree is the current qualified implementation source. It has not been activated in Native.
The private Caller tree contains a mix of older already-integrated changes and new unqualified work; its
plan text is stale. Never copy the entire tree, its whole `ApplicationContinuations.cpp`, or its full
`DocumentTests.cpp` into Integration. Transfer reviewed hunks and preserve newer diagnostics/goldens.
Do not reset, clean or merge the unrelated dirty `p1-io-durability` worktree (detached `a384df4`) or its
stash `6d7acec6ef9d84b4e2e0c5c3a6386eb6d029a165`, or historical `.build/worktrees/rf/*` checkouts.

## Last qualified step: immutable history storage index

[Exact test record](HistoryStorageIndexCandidate_2026-10-05.md).
`1936fe6` implements worker-prepared unique Entry/Raster/Selection ownership, changed-entry membership,
stable Undo/Redo membership, live-document exclusion, fresh resident-byte sampling, conservative unique
retirement bounds, O(1) sealed-stack metadata and small-owner retirement on the MTA owner.
It adds 46 document and nine owner-policy HostTests controls. All previous numeric goldens, PASS
multiplicities/output routes and ordered application stages remain intact.

| Full x64 suite | Actual child / exit | Observed registry | Independently verified archive |
| --- | --- | --- | --- |
| Debug r74 | PID 42368 / 0 | 6,729 document, 9,298 application, 79 stages | `p1-history-unique-index-x64-Debug-r74-full-qualified`, 1,041 copies |
| Release r75 | PID 83256 / 0 | Same complete registry | `p1-history-unique-index-x64-Release-r75-full-qualified`, 1,041 copies |
| ASan Debug r76 | PID 47476 / 0 | Same complete registry, real sanitizer detection | `p1-history-unique-index-x64-ASanDebug-r76-full-qualified`, 1,048 copies |

All three profiles use identical 771 owned inputs. Freeze SHA-256:
`9FFD6003832CDB4ED4CD1A0CCDB87324A61BA98C2E87F8267030D4BF562D6D6B`.
Archive completion SHA-256, in table order:

- Debug: `5F1285BF02F287D28D45E3783746F27644F6980F71A4B8C2C6D70149DEFD0EEE`.
- Release: `8A2214E1769A7B66BA2C96D86F085E091F284AA69098580FFA549749DFE81D27`.
- ASan: `4136663CFD7D88D370AA5167277450BE996F219AF8A06769029F0CA55432D0C5`.

Archives live under `C:/Users/eric/AppData/Local/RedPrism/QualificationArchives`.
Latest skills (15), formatting (651) and diff checks passed before `1936fe6`.
Current static analysis, ARM64 cross-builds, native activation and G1 remain due for this source.
Index cloning/sampling/bounds still scale with distinct owners; no constant-time delta/G1 claim is made.
Late sparse materialization after worker sampling remains unresolved in this qualified implementation.

## Unqualified private candidates and concrete risks

Read the [private candidate checkpoint](P1PrivateCandidatePause_2026-10-05.md) before editing.
Source checkpoint `3f49ea2` includes:

- Opt-in residency epoch, short worker lease, nonblocking owner permit and worker reprepare, plus 23 intended
  core controls. No production host uses it yet. Same-thread lease ownership needs enforcement; crossing
  threads can unlock a mutex from the wrong thread and leave the original TLS state held.
- Smudge/Liquify private cancellable source copy and 14 intended controls. Complete async Heal/Warp source,
  mouse-up/chaining and mid-copy cancellation remain open; an invalid retained object can keep vector capacity
  until it is destroyed.
- Fitting sparse Gray8 thumbnails become private dense thumbnails, keeping eager mask-edge semantics;
  six intended controls. Pixel/allocation/refusal/reference paths still need linked runtime qualification.

The strict private four-TU history code-generation probe r6 returned 0. It predates the final `Data()`
lease guard and does not compile or run all Warp/thumbnail controls. It is not a full build/runtime result.
The intended new document total is 43, yielding 6,772 from the qualified 6,729 baseline if transferred
unchanged. Independently count observed controls and all preceding registry entries in the actual run.

Primary next implementation work:

1. Restrict/enforce same-thread lease ownership and review permit/publication move/destruction lifetimes.
2. Use `Job::DeferCompletion` and same-record `Resume` for worker-only history refresh, preserving the original
   ticket, reservation, FIFO order, intent and revision; do not rerun pixels or create another admitted edit.
3. Admit the complete extra candidate-index/container peak and retain reservations through acknowledgement.
   Cover Busy/Refresh, cancellation, allocation faults/retry, predecessor roots, shutdown and stale authority.
4. Add live-history notification/maintenance after cache growth. Fresh `RetainedBytes` diagnostics already
   resample; prior trim decisions still require maintenance. Release owner permits before callbacks/waits.
5. Audit residual materializers: semantic `MaskEdgeBackground`, sparse `ReadRegion`/child `Data`, composition
   `WriteTiles`/upload fallback, reference transform/brush/sample paths and lazy Heal/Warp host opening.
   PNG/JPEG/project encoding already reads private bands; do not assume export still calls shared `Data`.
6. Integrate narrowly into Integration; preserve every original failure record and qualify linked tests.

## Wand agent checkpoint

Ramanujan was stopped and closed without any file writes, builds, tests or commits. Wand remains clean at
`8f5110d`; there is no partially implemented Add/Sub patch to integrate. Its read-only finding is that
All Layers mode propagation and checked Clipper admission also need narrow changes in
`RedPrism/ApplicationWandCompositeHostJobs.cpp` in addition to the existing layer job, continuations and
request descriptors. Include that file in any future bounded agent scope.

On resume, implement settled Add/Sub with event-time mode/AA, noncoalescing command barriers, conservative
checked Clipper workspace bounds, no-match Add/Sub preserving selection/AA, and New no-match Deselect.
Add independent core/actual-host controls without rebaselining existing goldens. Coexisting/live contexts,
general geometry, Clone All Layers and graph shutdown remain open. Primary reviews architecture and faults;
cheaper agents may handle bounded tests/audits. Next Wand attempt is r22 or later, never overwrite r21.

## Ordered resume and validation

1. Read this checkpoint, the opening [P1 checklist](../Plans/WIP/UiNeverBlocks_2026-09-22.md), relevant child
   plans and authoritative domain specs. Check Git heads/status and the exact archive manifests.
2. Review private candidates against qualified `1936fe6`, then implement/qualify the host protocol and full
   admission before claiming H3 complete. Keep future source separate from the qualified archives.
3. Use fresh create-new run/recorder names. Integration's next attempt is r77 or later; check the directory
   inventory first. Retain the actual process receipt and exact frozen compiler/helper inputs. Borrowed
   includes in the small code-generation probes are not a full dependency freeze.
4. Run skills/format and required full x64 Debug, Release, ASan Debug gates; independently compare all PASS
   multiplicities/routes and the exact 79-stage baseline. Run current analysis and all ARM64 cross-builds.
   Respect the running-output guard; never terminate a user's RedPrism window. ARM64 runtime needs native
   hardware and remains unavailable here.
5. Archive/verify each actual attempt before repairing or advancing. Activate qualified source in Native
   only after reviewing all writers/lifetimes, then run the required native matrix and original G1 gates.
6. Complete remaining P1 routes: pointer-held sampler Escape and device/fallback cases; wand/clone;
   Heal/Warp lazy source, chaining and mouse-up; Shape Redraw; selection booleans/coverage/display bounds;
   clipboard prefill/retry/durability/end-session; file/size apply overruns; H2 native roots/display writers;
   H3 late cache/reference/delta cost; H4 first uploads/chrome sync/retirement; user workflow acceptance.
7. Keep the original latency targets: largest A/B fixtures, owner p99 <=4 ms/max16 ms, ready apply <=2 ms,
   no UI pixel work, and original presentation/cancellation rows/exceptions. Earlier recorded overruns are
   failures to repair. Do not weaken targets to mark P1 Done.

The Release sampler performance attempt r1 was refused: actual exit 5, `displayOn=0`, zero presents and
no timing result. Its awake-display user question has no recorded answer. Do not bypass the guard or infer
speed. On a later resume with an awake/unlocked display, use qualified Release source and create-new perf r2
(or the next unused name). R2/R3 timings are unavailable. No RAW/Photoshop performance claim is qualified.
The original public RAW corpus remains 19 CC0 samples covering nine extensions, including five large files;
its manifest `Tests/Fixtures/raw-corpus.json`, license records and
`Build/bench/get-raw-corpus.ps1` recreate `C:/RedPrism.Perf/corpus` on another machine. See the detailed
[P2–P6 roadmap](../Plans/WIP/ImageProcessingAccelerationAndRaw_2026-10-04.md); implementation begins after P1.

## Recoverable local tools and evidence

Pause archive:
`C:/Users/eric/AppData/Local/RedPrism/QualificationArchives/p1-user-pause-2026-10-05`.
977 files / 116,847,649 bytes reread/hash-verified at `2026-10-05T05:31:39.955270+00:00`.
Preservation manifest SHA-256:
`5BC3A3EAFA462817CE34FA2D41C0AD9C03F106B74827BED8B5709337CA958A3F`.
This is source/tool preservation only, not runtime qualification.

- `helpers/`: `private_desktop_process.py`, `run-p1-focused.py`, `start-p1-focused.py`,
  `archive-managed-validation.ps1`, `verify-history-index-registries.py`, `integrate-history-index-r1.py`,
  `run-history-index-codegen.py`, `run-p1-sampler-perf.py`, and the preservation script.
- `evidence/`: all six index code-generation probes (including original failures), admission/topology probes,
  integration r1 receipt, refused performance r1 and focused r74/r75/r76 recorder receipts.
- `caller-source/` and `git/`: exact original dirty source copies, tracked binary diffs and four worktree heads.
- `preservation-manifest.json` / `preservation-completed.json`: verified lengths, SHA-256 and completion.

Current original helpers still live in Native `.build/`; no build products or helpers were added to Git.
The three qualified full-suite archives are separate from the pause archive and remain authoritative.
Copy generated archives separately when moving computers; source/docs are pushed on their named branches.
Restore helpers to their documented locations or adapt explicit paths in a new recorded attempt; never mutate
existing qualification receipts. All agents are closed and no implementation resumes automatically.
