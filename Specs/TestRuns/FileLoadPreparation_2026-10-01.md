# Adaptive project-load preparation and detached imports — 2026-10-01

Status: helper configuration qualification passed. Production host migration remains open.

## Preparation boundaries

Project probes parse a bounded manifest and inspect fixed 33-byte PNG headers/encoded lengths. They produce no
rasters, WIC interfaces or asset-sized vectors. Their standard result contains the file version and ceilings for
manifest bytes, layer count, aggregate image/mask pixels and maximum encoded asset length. Decode checks all
ceilings again before corresponding allocations. A changed source may be refused or load within the ceilings;
the probe does not snapshot its identity/content. Pixel decode remains authoritative for complete PNG validity.

Separate pure declarations cover metadata preparation and the complete decode peak: parser/name/record/curve/
validation work, rounded pixel outputs, transient thumbnails and one encoded ZIP asset. Opaque codec/library
allocations remain in process headroom. Actual host admission and real-loop latency need F3b.3 qualification.

The detached import helper preserves a separate first-success saved baseline for a new canvas, and one final
additional-import document/history name. It ignores the drop point throughout a new-canvas batch, preserves
existing-canvas grouping/placement, retains partial successes after ordinary file failures, and clears all
prepared output on cancellation. Its final working document is moved once instead of copied after every file.
Import checks the existing aggregate image remainder and layer capacity before creating a decoder.

## Independent tests

Tiny folder and ZIP projects use literal 2×1 / 1×2 color and Gray8 pixels. Probe counts and exact versions/layers/
encoded lengths are independent of the estimator. Exact ceilings preserve all image/mask bytes. Empty projects,
unsupported version, missing/truncated/invalid headers and cancellation at manifest/header observations cover
no-partial-plan behavior. Reopened sources with longer manifests, more layers, larger folder/ZIP encoded assets
and larger decoded pixels are refused with no document/active id; the first-asset refusals record zero completed
decodes. Memory checks cover hard maxima, lowered limits, invalid ceilings and empty-project requirements.

Import cases cover counted-path bounds/NUL refusal, failed first/later files, first successful canvas, ordering,
literal pixels, grouping/centering, singular/plural history names, a full sparse 100 M source without allocation,
unchanged source snapshots and cancellation both after a successful file and at AssetDecoded before mutation.
Scratch ownership uses verified absolute Guid leaves; ZIP fixture handles/sources have deterministic RAII.

## Gates

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 268 files |
| x64 Debug / Release / ASan Debug full tests | Passed, including real sanitizer probe |
| ARM64 Debug / Release / ASan Debug cross-builds | Passed (runtime unavailable) |

Each x64 suite includes 69 project-probe/resource checks and 31 detached import checks. Final logs are
`.build/p1-load-preparation-{platform}-{configuration}.log`, with source freeze at
`.build/p1-load-preparation-freeze.json`. These helpers leave interactive open/import on its current path until
F3b.3 activates host jobs. No G1, RAW or Photoshop claim follows from this helper qualification.

All 316 frozen source/project/script inputs remained unchanged through the final gates. SHA-256:

- Freeze manifest: `153D37B85B776E6DFA938F78783F60CA34F5550EA3179BB4325D6D64D43F1553`.
- x64 Release RedPrism: `93ABE0F71E5039194BA34CCC7397F0A2070251AE0EAFE83D94C7E31E1B93B0D9`.

The bounded review found no concrete cap bypass, partial-result leak or import semantic error. The estimator
checks currently exercise image/encoded growth and invalid/hard limits. Independent mask-only, layer-array and
manifest-input allocation-floor controls are due with F3b.3 host admission; the helper gates alone do not prove
that a production caller submits or retains the declared reservation.
