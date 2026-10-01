# Immutable history roots and optional image thumbnails — 2026-10-01

Status: qualified correctness and ownership slice of P1.9 H2.1/H4.1. The full x64 suites and all ARM64 clean
cross-builds pass against the same frozen inputs. Full document capture, incremental accounting, first uploads,
measured G1, native ARM64 runtime, RAW and Photoshop comparisons remain open.

## Implemented boundary

History state capture copies two immutable container roots without allocating or traversing entry handles.
Non-const reads remain const reads. Every push, pop or erase constructs a private replacement before publishing;
reference counts never authorize writing a formerly shared root. Admission and retirement include root/control
records and conservative clone/growth capacity. Whole document records and storage-accounting traversal are
separate unfinished costs.

Unused image thumbnails are optional across codecs, paint/paste/lift, color, merge, resize, canvas and transform
producers. Validators accept absence and validate present caches. Every semantic Gray8 mask thumbnail is still
prepared eagerly with its mask, preserving the existing outside-mask tone. Explicit thumbnail construction remains
available; the current chrome has no image-thumbnail consumer. No demand cache or background thumbnail job is claimed.

## Proof

- Each x64 suite passes 28 `history-entries.*` assertions: zero entry-handle copies on capture/read, independent
  integer contents after mutation, weak-owner lifetime, concurrent readers and clone-copy refusal without publication.
- Each suite passes 30 `document-thumbnail.*` assertions in nine producer/history groups. Literal bytes cover
  sparse thumbnail reads, invert, resize, Levels, merge and image/mask painting. Equality, no-op redo and the
  unchanged retained-thumbnail golden are exercised. That golden explicitly supplies a nonzero optional cache.
- Existing I/O and import/resource tests pass. The original Gray8 fixture remains `255,0,128,255`, with eager
  thumbnail format/dimensions and all four bytes checked. RGB decode has no eager image thumbnail.
- All five executable ISA scans pass in all three x64 configurations. All 331 synthetic parser controls and the
  existing compiled misplaced-ISA, runtime-owner/function and PDB rejection controls pass. The runtime allowlist
  SHA-256 remains `6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.

## Configuration and identity

| Gate | Result |
| --- | --- |
| Skills / formatting | 15 skills / 346 native files passed |
| x64 Debug / Release / ASan Debug | Full clean-build suites passed, including stress and real sanitizer probe |
| ARM64 Debug / Release / ASan Debug | Clean cross-builds passed; native runtime unavailable |
| Frozen native/project/script inputs | Same 396 inputs checked throughout and after all six configurations |

Source-freeze SHA-256: `E14643521574A7D1567E8CD4359D680DBDCF794CBF56B5B7E9850796861BFFBE`.
Release 0.1.129.0 executable SHA-256: `80A6DB33486CAEDAA9C16CA9DBFB54102949A92158B20D55A618EF44854083D8`.

Logs and frozen inputs: `.build/p1-resources-r2-driver.log`, its six platform/configuration logs, skills/format/freeze
files and copied x64 self-test stderr. Exact Release executables/PDBs/DLLs, DxUi provenance and source/binary identities
are retained in `.build/p1-resources-r2-qualified/`.

## Measured ownership tradeoff

Reviewed standalone Release microbenchmarks use the equivalent container prototype and independent content/handle
expectations. These console measurements do not establish complete document capture, admission, trim or UI latency.

| Handles | Old vector capture median | Immutable root capture median |
| --- | ---: | ---: |
| 1 | 30.85 ns | 6.43 ns |
| 100 | 694.79 ns | 6.81 ns |
| 1000 | 7659.90 ns | 7.15 ns |

There are 27 paired capture runs, with alternating order. Setup, checksum walks and final destruction are excluded.
Private cloning increases mutation cost: at 100 handles, push/pop averages 876.70 ns per mutation versus 13.36 ns for
exclusively owned vectors; oldest-entry erase is 2261.13 versus 292.86 ns. All 36 paired mutation runs match independent
expectations. This intentional worker/reference-API cost preserves immutable reader ownership; no in-place mutation
of a shared root is accepted. Raw timings, sources, toolchain, hashes and observations remain under
`.build/p1-history-root-benchmark/` and `.build/p1-history-root-mutation-benchmark/`.

## Preserved failures

The first Debug producer test requested a hide-all mask while expecting white. The fixture now requests reveal-all;
its white-to-black literal pixels and new-entry requirement are unchanged. Exact failed image/PDB/DLLs, source freeze
and logs remain in `.build/p1-resources-first-Debug-mask-fixture-failure/`.

The first final matrix passed Debug and Release, then ASan's ISA scanner interpreted a switch-table DWORD as VEX.
The exact failed image/PDB, original parser, source freeze, disassembly and report remain in
`.build/p1-resources-first-ASan-isa-failure/`. It is not retroactively qualified.

The new proof requires the exact contiguous unsigned stack guard and RCX/RAX dispatch, byte-for-byte decoded/PE
agreement, same-owner procedure bounds, every decoded DWORD target and the complete in-range remap. Its encoded
count must end exactly at that procedure's end inside the larger contribution. Earlier symbols contradicting the
count, missing/changed/unknown/overlapping rows, listing-only byte mismatches and real neighboring VEX remain gated.
The previous 297 control expectations are unchanged; 34 new controls include two acceptance and 32 refusal cases.
