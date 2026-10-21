# Integrated selection gesture — native qualification

Date: 2026-10-17. Code commit `29a8ac2`; P1 remains WIP. The native source includes the 51 reviewed
code/test/project paths, five CPU contracts and selected caller/shutdown integration. The new physical
route remains test-only. The newer private captured-settings/history-guard draft is not part of this freeze.

## Current matrix

| Profile | Actual result | Source/product qualification |
| --- | --- | --- |
| ARM64 Debug r1093 | Actual parent PID 94712, exit 0; full canonical Rebuild | 1,492 archived files, 969 exact compile records, 1,417 compiled/project/toolchain inputs; five ARM64 PE/full-PDB pairs match |
| ARM64 Release r1094 | Actual parent PID 95128, exit 0; full canonical Rebuild | 1,492 archive files, 969 exact Release compiler records and five AA64 PE/full-PDB pairs match |
| ARM64 instrumented ASan r1095 | Actual parent PID 86856, exit 0; full canonical Rebuild | 1,496 archive files, 969 actual instrumented compiler records and five matching AA64 PE/full-PDB pairs |
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

## ARM64 Release r1094 qualified

The same immutable `29a8ac2` source freeze completes the canonical ARM64 Release Rebuild:
actual parent PID 95128 exits 0, 02:52:07.149181–03:20:38.330267 UTC. Main's reviewed reader
verifies all 1,492 archive files, all 969 exact Release compiler records, 1,417 compiled/project/toolchain
inputs and five exact AA64 PE/PDB pairs. Debug and Release are both qualified; r1095 instrumented-ASan
is running on the same freeze. Native ARM64 execution is unavailable on this x64 host.

Release stdout SHA-256: `A59513453FB3DAB402AAE2149D20BF715183E7E87BF070204F20356EED9B60A2`.
Archive completion SHA-256: `6AAF862C794AB1E5009336F281FF91AB972E0CC3C04270849FD3843499943A44`.
The exact Main receipt is `.build/P1Integration/LassoHost-r1-Main/r1094-main-qualified.json`;
its SHA-256 is `3EBC70DDCE44B450A87FDA16CF0C20B93444508ACDE9B2B91DDBB581EC7152F2`. This is cross-build proof, not native runtime or G1 qualification.

## ARM64 instrumented ASan r1095 qualified — matrix complete

The same immutable `29a8ac2` source freeze completes canonical instrumented ARM64 ASan Rebuild:
actual parent PID 86856 exits 0, 03:20:55.723846–03:47:00.159950 UTC. Main's reviewed artifact reader
verifies all 1,496 archive files, all 969 actual instrumented compile commands (forced sanitizer header,
`/fsanitize=address /MDd /Od /Zi`), all 1,417 compiled/project/toolchain inputs and five exact AA64 PE/PDB pairs.
The controller joins with exit 0. All three ARM64 cross-build profiles are now independently qualified.

Stdout SHA-256: `2F00AACDC27953ECC517F8BE166035F1B0732FD7BEF7C1B12259BB6A6224F31C`.
Archive completion: `68FFC1BD2FACD69F9CEEBF83CAC3010810F5193675041EDB98B9D19A891FB413`.
Main receipt: `.build/P1Integration/LassoHost-r1-Main/r1095-main-qualified.json`, SHA-256
`F4FD236EAC89E5EECA63288E8ECE55F80CB28435EA3FE3D29F33C96DAC690445`. Native ARM64 execution and performance are not claimed on this x64 host.

The newer private lasso/rectangle primitives and outside-hit parent are separate source snapshots.
Their integration must receive the final latest-source native gates; repeating old-source x64 gates before
that integration would not qualify the later code. Latest x64 Debug/Release/real ASan/analysis remain due.
