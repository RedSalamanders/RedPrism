# Deferred colour target capture and immutable final pixel source

## Checklist

- [x] Reproduce owner metadata copy cost using actual valid model inputs.
- [x] Share immutable opening records and prepare carried metadata on workers.
- [x] Add final pixel-source lifetime/authority tests for later atomic GPU publication.
- [x] Review and correct bounded cheaper-agent tests; pass1,625 private Release/real-ASan facts/profile.
- [x] Compare identical Release benchmark source against baseline/candidate with complete metadata checks.
- [x] Adopt reviewed code/tests and owning contracts.
- [x] Qualify the combined actual native source and changed-source ARM64 matrix; [current six profiles](P1CtrlCaptureMatrix_2026-10-20.md).
- [ ] Qualify final-source analysis with the completed P1 integration.
- [ ] Finish complete phase bounds, original-parent GPU publication, ordinary activation and measured G1.

## Implementation

The deferred destructive panel keeps its original immutable layer root. Its owner token retains mask
image/thumbnail and placement/flags without copying the asset name. Owner input copies retain the same
root. Workers reject a target that differs from the captured record; a ready commit reconstructs the full
mask metadata from that original record. Preview/histogram validation needs no name copy. Commit planning
uses the actual original name size; retirement includes the complete cached source-root bound.
Direct inline capture is unchanged. Deferred live adjustment openings now retain the same original-root authority, as recorded below.

The final PixelMove source accessor refuses zero, moved-from and applied closures. It cannot expose the
retired predecessor after apply, and gives no early history/document mutation authority.

## Private functional proof

Source: `Z:\src\RedPrism\.build\P1Drafts\ColorTargetCapture-Main-r1`.
Evidence is under `Z:\src\RedPrism\.build\P1Evidence`:

- `p1-color-target-capture-private-r3/ASanDebug`: actualPID103852 exits0 at18:12:17UTC.
- `p1-color-target-capture-private-r1/Release`: actualPID86192 exits0 at18:12:17UTC.

Both pass the same1,625 named facts: all1,591 selected existing colour/panel/Wand/pixel-closure assertions,
25 new capture assertions and nine final-source assertions. Stdout SHA-256 is identical:
`9E266A1574308D5F8B0A519D274B2E5C09C0CD4174788C96422FFB04B090FAF5`; stderr is empty.
Actual native worker threads prepare results/history without session access. Checks include literal gamma
pixels, full mask-name/flags/placement, one Undo, corrupted descriptors, cancellation, stale replacement and
source/metadata bounds. ASan additionally reports a genuine isolated heap-use-after-free probe.

Release compiles105 fresh TUs. The first ASan build preserves103 valid objects and fails at two new test
compile errors: implicit size_t-to-int row pitch and a nonexistent Clear API. The first repair helper's104-
object assumption also refuses. All failed source bytes are retained. The final repair compiles the two
test corrections, verifies103 unchanged object hashes/mtimes and all105 actual ASan command records, and
links them into the qualified image. Production code is unchanged by these fixture repairs.
Both executable/full-PDB pairs and actual ISA0 scans pass without an allowlist or DxUi change.

## Same-source capture measurement

`p1-color-capture-comparison-private-r1/comparison-qualified.json` binds the same benchmark source to90
exact Release Common objects per image. The sequential baseline/candidate runs each check24 actual valid
documents: four mask-name sizes, normal/capped layer names and three fresh sessions. Fixture construction,
validation, comparisons, output and destruction are outside the three timed API calls. Complete source,
target, name, mask, history and generation checks pass. The candidate aliases the original metadata and its
owner descriptor's name is empty. No compiler/native harness runs concurrently; five reusable MSBuild nodes
remain idle with identical CPU counters and creation identities before/after each run. The initial overly
broad process-name guard refusal is retained; no process is terminated.

| Normal layer name,64Mi WCHAR mask asset name | Baseline median | Candidate median |
| --- | ---: | ---: |
| BeginColorEdit |17,431.3us |20.1us |
| PrepareColorEditPreview |68,863.7us |3.5us |
| PrepareColorEditCommit |40,579.6us |1.1us |

These are three model owner-capture calls on a synthetic valid document, not full UI/apply/presentation
latency, RAW decode speed or a Photoshop result. They do not close G1 or ordinary host activation. Earlier
build-loaded observations, the benchmark compile/reader refusals and every successful byte identity remain
separate evidence. The combined actual checkout still requires fresh canonical proof after adoption.

## Deferred live adjustment follow-through

The direct deferred opening captures its exact name-free target once. The worker-prepared opening retains
the input's original root and creates the same target descriptor on its worker. Its input remains a whole-
document opening token; no later capture launders stale authority. Existing preview validation reads the
captured original, while carried metadata remains complete. No public type layout changes in this extension.

Private source: `P1Drafts/LiveAdjustmentTargetCapture-Main-r1`. Evidence:
`P1Evidence/p1-live-adjustment-target-capture-private-r1/{ASanDebug,Release}` beneath the ignored workspace.
ASan PID40716 exits0 at19:30:33UTC; Release PID93484 exits0 at20:17:28UTC. Both run exactly1,650 facts:
all1,625 selected predecessors plus25 independent live-adjustment assertions. Stdout is identical SHA-256
`186F37044A2CFDFDFA91705F8A553AD2E866DBE98E56AB5A7EB51D0210D977C6`; stderr is empty.
Real-ASan UAF detection passes. Release builds all106 objects fresh; ASan links four fresh plus102 exact
current-source objects. Main reviews the cheaper tests/readers, corrects actual worker execution and exact
Release CRT flag checking, then verifies executable/full-PDB identity and unchanged actual ISA0 scans.
These selected tests establish no full Document/Application, visible latency, G1 or ordinary activation claim.

## Combined source qualification in progress

The preceding1,010-TU source passes all three ARM64 cross-build profiles (j1armd1/j1armr1/j1arma1), with
five matching AA64/full-PDB pairs per profile. No native ARM64 execution is available. Its full x64 ASan
j1a1 exits1 at the direct cancellation smoke's `cancel-ambient-pointer-input` guard after the five native
suites,154 Application stages/11,814 facts, stress, genuine UAF and crash probes pass. Preserve the failed
attempt; no causal claim identifies the input sender. Isolated same-binary direct and prepared cancellation
smokes subsequently pass. Nine canonical captures were not reached, so this is not a full ASan pass.
The live extension raises the combined count to1,011 TUs; its clean complete canonical matrix remains due.
