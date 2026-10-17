# Integrated selection gesture — native qualification

Date: 2026-10-17. Code commit `29a8ac2`; P1 remains WIP. The native source includes the 51 reviewed
code/test/project paths, five CPU contracts and selected caller/shutdown integration. The new physical
route remains test-only. The newer private captured-settings/history-guard draft is not part of this freeze.

## Current matrix

| Profile | Actual result | Source/product qualification |
| --- | --- | --- |
| ARM64 Debug r1093 | Actual parent PID 94712, exit 0; full canonical Rebuild | 1,492 archived files, 969 exact compile records, 1,417 compiled/project/toolchain inputs; five ARM64 PE/full-PDB pairs match |
| ARM64 Release r1094 | Rebuild in progress | Not qualified yet |
| ARM64 instrumented ASan r1095 | Serialized after Release | Not qualified yet; native ARM64 runtime unavailable |
| Latest native x64 Debug/Release/real ASan/analysis | Due for this integration | Earlier native baseline and private caller proofs remain scoped evidence |

The transport pins 1,421 source/runner inputs. Freeze SHA-256:
`01EBF185AD0AD3E42C22D04BC1EC93BE4901F9FA0FE2F00A0C9ADD44A2A37AB5`.
ARM64 Debug runs between `2026-10-17T02:25:05.815432+00:00` and
`2026-10-17T02:51:50.641481+00:00`. Release/instrumented ASan use the same unchanged compiled input freeze.
Cross-compilation proves neither native ARM64 runtime nor measured G1. Default input and P1 Done remain open.

## Independent checks

Main reviews the fresh-source artifact reader, preserving complete archive/source/profile checks and
extended-path reads. Source-derived XML membership requires exactly 340 Application, 273 Document,
134 Host, 138 Composition and 84 I/O source records. Exact per-project TLOG sets and all compiler profile
flags must match; Debug/Release cannot contain ASan, and ASan requires `/fsanitize=address /MDd /Od /Zi`
plus the exact forced RequireAddressSanitizer.h. Four source-only positives and 22 corrupted-command/TLOG
negatives pass before Main opens the serialized reader gate. No current runtime logs define expectations.

The archive reader independently verifies every source/evidence byte, the complete inventory, actual joined
zero child and source/stream hashes. The separate PDB reader compares each archived PE CodeView GUID/age
with llvm-pdbutil's full-PDB summary. All five Debug images have machine AA64. The compiler source command
sets remain exact even if MSBuild records multiple sources in one caret-separated tracking group.

| Qualified receipt | SHA-256 |
| --- | --- |
| r1093 Main qualification | `5F4AA4F33942A0135A59AB8367CA774E0E9F700A6A63EB651D5B4486359591DC` |
| r1093 archive/source/artifact reader | `8B925A54BB2516E5DFE03C2DA3BBFBFD5F6B50573A9B27CABF881059BC1B94AA` |
| r1093 PE/PDB reader | `BE3087586E4408EE2A987B9CBE3E68A6D33CDA2755FDEE1A643CDC5E319478C2` |
| r1093 archive completion | `CCECE22E5FF71E6C9E236802FFCCEF46D5BD8355D6202BA727520335CBD95BF9` |

Receipts remain under `.build/P1Integration/LassoHost-r1-Main/`; the exact transport/preflight and reviewed
reader are under `.build/P1Drafts/SelectionGestureNative-r1-Main/`. Archives reside in the task-specific
`D:/RedPrismBuildCaches/.build/p1-opacity-host-proof-native-29a8ac2-r1/Archives` directory.

## Storage and scope

Before rebuilding, 2,591 files (8,149,323,883 logical bytes) in 13 closed native output/intermediate/ISA groups
are copied and hash verified. Only the verified workspace leaves are replaced with exact task-specific D:
junctions; former Z: targets remain unchanged. A second hash pass verifies every logical path after activation.
The two mapping receipts live under `.build/P1Integration/SelectionCallerNativeAdoption-r1-Main/`.
No source, build property, dependency source or dependency pin changes. Canonical rebuilding of the pinned
DxUi library does not modify its source; pin remains `8e8994079e82f79e6765e6a885c03fc687957591` / API 3.

The earlier native seven-profile qualification remains attached to its earlier source. These new integrated
profiles cannot inherit that result by name. Remaining S5 branches, complete caller/display bounds, ordinary
activation, latest x64 gates and measured G1 remain required before P1 moves to Done.
