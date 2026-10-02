# Mask worker preparation and publication

Status: settled-session mask creation is qualified in all six native profiles. Live producer settlement and
measured P1/G1 bounds remain open.

## Implementation checklist

- [x] Seal admitted document, active target, complete selected-layer IDs, reveal/hide argument and history authority.
- [x] Prepare inverse-mapped selection coverage, adopted Gray8 pixels and eager semantic thumbnail on workers.
- [x] Prepare mask targeting, selection removal and one named history publication as one guarded after-document.
- [x] Pass 57 independent core controls and the complete isolated DocumentTests suite.
- [x] Pass the additional fractional/blank-layer/selection-state and invalid-grid controls (84 DocumentTests
  controls: the original 57 plus 27 extra).
- [x] Pass the focused isolated host proof: 201 controls for the ordered record, acknowledgement,
  cancellation/staleness/faults and aggregate retirement.
- [x] Pass the complete private WARP application run, preserving the original mask-target and badge assertions.
- [ ] Prove live stroke, transform, outline/lasso and younger input settlement through the required P1.4/A6/S3 paths.
- [x] Activate, pass all native x64 suites and ARM64 cross-builds, archive, reconcile contracts, commit/push.
- [ ] Measure capture, application, history/chrome/upload and required G1 scenarios.

## Independent reference

retained numeric golden cases supplies the selected red-layer mask anchors: a 100×40 layer,
document rectangle (20,10)–(50,30), hidden selected pixel (30,20), revealed exterior (5,5), and undo restoring
the selection and absent mask. Revealing masks contain `255 - coverage`; hiding masks contain coverage.
No selection retains the 1×1 white/black mask and existing history names. An explicit empty selection remains
a selection until publication and produces a uniform mask over the layer grid. Literal translation/nonuniform
scale controls name the pixel-center mapping independently. Original image identity and bytes remain unchanged.

The thumbnail is semantic input to `MaskEdgeBackground`. It exists before any prepared mask can publish;
the worker cannot expose an intermediate mask or a pending/missing edge tone.

## Isolated evidence and repairs

`.build/p1-mask-draft/core-run-r3.log` reports 57 mask controls with zero failures and the complete document suite
passing. `core-qualified-r3/` preserves 472 exact inputs and seven evidence files, including the executable/PDB,
build/runtime logs and process metadata. This isolated Debug proof does not qualify other configurations or callers.

Before repair, `core-build-failure-r2/` preserves 472 inputs and four evidence files. Test registration expanded
existing C++ newline escapes into physical newlines. Restore the original test source byte semantics and add only
the mask include/call; no pre-existing golden or expected value changes. A proposed `DocumentSession(1)` admission
control tested the history entry limit instead of worker memory. Replace it with a sparse 100 MP mask owner and
require independent mask-budget refusal before source materialization or live mutation. Admission-floor controls
use the Gray8 output and float-cell storage contract independently of the planner.

## Remaining costs

`mask-focused-qualified-r1/` preserves 475 source inputs and 20 evidence files. The focused WARP process exits
zero and reports 201 host controls with no failures. Gates before/after preparation retain the original ordinal,
history barrier and whole reservation while zoom dispatches. Selected reveal/hide, no-selection white/black and
explicit empty selection preserve literal mask values and eager edge tone. Allocation faults cover admission,
capture, worker and all five prepared-history stages. Source/selection/history/same-ID replacement refuse old
publication; younger Undo restores the outline and absent mask. Cancellation retains ownership through final
acknowledgement, including a 2048² mask graph. Final assertions require no hidden Present, tickets, barriers,
charges or request owners and confirm aggregate CPU retirement off owner. The temporary focused shortcut is
removed before the complete private run and cannot be activated in native sources.

Source/history metadata capture and chrome synchronization still run on the owner and require H2/H3/G1 work.
Settled worker correctness alone does not qualify live gesture/stroke settlement. No native ARM64 runtime,
RAW-processing throughput or Photoshop comparison follows from this record.

## r5 core qualification, full-run r1 failure and r2 pass

The complete isolated x64 Debug `DocumentTests` suite passes in r5, including 84 mask core/extra controls (57 core
+ 27 extra). `.build/p1-mask-draft/core-run-r5-result.json` records exit code 0 and executable SHA-256
`F9E89BEF8AAC8B3E6DFD00477A93FC80C8745459E7C8A1ECAC820BA724BF139F`; the archived `core-qualified-r5`
evidence manifest records this result file as
`85703E7157B0151D92D0FFD11A25F5DA2862B4390A039B29466717EDFD87D4A6`. Its log ends with “All document tests
passed.” The already completed focused host run also remains passing: 201 checks, zero failures, with executable
SHA-256 `D9067EE9D6AD933BA77952C0F3AEC1938E722EDE562C23238928E5DC862DC5D1`.

The complete private WARP application run r1 is a failure, not a qualification. Its result records exit code 5
and executable SHA-256 `D2121FAAE44205ABA51474BB2001110BF262ABD58876F059592E1413DAC2FE73`. The archived
`.build/p1-mask-draft/mask-full-failure-r1/` freeze contains 477 inputs and its evidence manifest contains 31
entries (manifest SHA-256 `E070EF5D598C8EB98BB84D98222F455B434208D5FA63114933520C04D7D76BF6`). The failure is the
old synchronous AddMask observation in `ApplicationLayerEdits.cpp`: the mask-targeted-again UI assertion does
not observe the target after the command. This is an application integration/test observation; it does not
reverse the isolated document or focused-host passes. The r2 full private WARP run below subsequently passes.

The authoritative r5 result and `core-qualified-r5` source/evidence manifests remain the references for core
qualification. No performance or P1/G1 bound is established by these functional results.

The complete private WARP application run r2 exits 0; it is a full run, not the focused proof. Its receipt records
completion at `2026-10-01T20:12:09.7274210Z` and executable SHA-256
`49600513E17C453550B119ECC5506739A3CD87A8969792634B0B28AF9E7128B7`. The frozen source set has 477 inputs and
differs from r1 only at `RedPrism/ApplicationLayerEdits.cpp`: the original AddMask observation now calls
`settle()` before inserting Other. The original mask-target and badge expectations are unchanged. The archive
`.build/p1-mask-draft/mask-full-qualified-r2/` contains 36 evidence entries; its evidence-manifest SHA-256 is
`9554B01DD23D4645A72EE023B666BA9566235E6A9CBCCEA0C0EABE9D908B670D`, and the r2 result-file SHA-256 is
`34D4DBF2F5E69057D1B20478D949C7FFBB1ACCED092680969307D0FEA6B4CCB5`. Its output records zero failures in
mask-host (201 checks), shape-host (199), clipboard-host (268), clipboard-prefill (61), clipboard-delayed (41)
and clipboard-fault (88).

Full private WARP qualification passes in r2. The later native matrix below qualifies the activated callers;
live producer/gesture settlement and measured G1 bounds remain open.

## Native qualification

Activation follows the qualified Shape baseline `1504f92`. The incremental six-profile matrix completes at
`2026-10-01T21:54:33.2351133Z`, process exit 0, against 496 frozen inputs. All x64 Debug, Release and ASan Debug
full suites pass, including the 84 document controls and 201 mask-host checks with zero failures in each profile.
All three ARM64 cross-builds pass; they establish no ARM64 runtime result. Skills validation reports 15 skills;
formatting checks 445 files. This is an incremental matrix following the clean Shape baseline.

`.build/p1-mask-r1-qualified/` preserves 496 exact source inputs and 297 evidence files, including all six
profile images, dependencies, ISA evidence, logs and final receipts. Source-manifest SHA-256 is
`5CAB65322706F35EFF71A65C44D738DA2F56D48788EAD50A838E1998BB73EB15`; evidence-manifest SHA-256 is
`0120020039FB8FE286EBC2714AEA6D794A8BE31188C4CF135B71D94D6A5FBE83`.
Before commit, one trailing blank line is removed from DocumentTests.vcxproj. Parsed XML trees compare equal;
compiled inputs do not change. `.build/p1-mask-post-qualification-xml.json` records both hashes and the comparison;
`.build/p1-mask-publication-freeze.json` retains the final 496-input identity. Formatting and diff checks pass again.

Owner metadata capture, history/chrome/upload costs and real-loop timing remain P1.9/G1. No speed or Photoshop
claim follows from this functional matrix.
