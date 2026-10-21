# P1 sparse reads and Apply attribution — 2026-10-11

Status: focused Release qualification and an actual diagnostic observation; P1 remains WIP.

## Qualified source

Managed checkout: `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism`, branch
`codex/p1-opacity-host-proof`, based on pushed `9c1aa9a`. DxUi is unchanged at
`8e8994079e82f79e6765e6a885c03fc687957591`, API revision 3.

Release build `r862`: actual child PID 87944, exit 0, 08:14:00–08:20:50 UTC. All 1,211 source inputs
and the ISA gate pass. Source-freeze SHA256:
`B9471EAA97939644A69A6B608B7BD9857AEE4DF8FCE0EB1CD9DE7E0E27F9EA02`.
The pre-runtime binding includes 20 owning contracts; its SHA256 is
`675D3806594B15CF32B6BD188436A94C952E5BD6584F2323A124314FF396E38D`.

| Actual focused Release scope | Preserved original records | Additions | Result |
| --- | ---: | ---: | --- |
| Host `r863`, PID 46432 | 8,880 | 35 | Exit 0; exact original and new rosters |
| Document `r864` | 9,008 | 18 | Exit 0; exact original and new rosters |
| Application `r865`, PID 82552 | 7,399 stdout / 6,902 stderr | 0 | Exit 0; all 126 ordered stages |

Report SHA256 values:

- Host: `4274163B562D0B219D347981517E8CD9A7015DC22666602DBBC9F21BC0A6D87C`.
- Document: `102F5A3F1DA0269E7D0D1568BED755EA90A2F906AFD18D3AEF4CCDBD7FFB4C96`.
- Application: `78CB26F933B4CD0C38DFC54E0F6685C7EBB243A5882D9BE21880C710CCD894D1`.

`validate-skills.ps1` passes all 15 skills. `format.ps1 -Check` checks 1,069 files. The initial build
`r861` fails because new phase scopes shadowed an existing local; the corrected source uses `phaseTiming`.
Its actual exit, logs and 1,211 source copies are preserved. Three later checker refusals came from incorrect
new-test count, singular/plural prefix and assumed END-marker bookkeeping. Main corrects those readers only;
all original source predicates and exact predecessor requirements remain intact.

## Region-read behavior

Nested sparse tile reads recurse through `ReadRegion` into the caller's existing buffer. They do not request
shared materialization or its gate. Eighteen literal controls cover BGRA/Gray bytes, clipped source windows,
both fill values, untouched row padding, nested reads/crops under a held residency lease, later-window
precedence, and two event-gated readers concurrent with explicit publication. Explicit `Data()` still
publishes inner and outer caches and preserves pointer/storage identity.

The private reference-warp allocation path is separate. Its Debug/Release/real ASan runs preserve all 9,008
original Document records plus 22 additions per profile and include a genuine ASan detection probe.
It is not integrated into native `TransformEdit.cpp`; complete private-source/cache/retirement phase bounds
remain due.

## Actual visible diagnostic observation

One actual hardware run: PID 16216, exit 0, 08:28:29.367577–08:30:21.935151 UTC.
Command: `RedPrism.exe --bench ui-cancel-prepared100`, with child-only environment override
`REDPRISM_UI_TIMING_ATTRIBUTION=1`. No native build or test producer runs concurrently.
All source/contract hashes, 13 executable/PDB/DLL products, DxUi provenance and the power scheme remain
unchanged before and after the child. This instrumentation run cannot qualify a timing gate.

It records 88 active-operation rows including warmups, 80 after warmup, 70 measured cancellations and the
22 original extreme-blur refusal rows. All 88 diagnostic summaries have zero overflow and zero optional
clock failures. The largest action interval is 104 microseconds. HSV trial 6 records:

| Interval | Job identity | Wall microseconds | Thread cycles |
| --- | --- | ---: | ---: |
| Inclusive Apply | Commit `(0,119)` | 2,109 | 9,029,082 |
| Combined job/cancellation-source destruction, original phase 4 | Commit `(0,119)` | 2,046 | 8,768,127 |

The useful cancelled body is `(0,118)`, a different generation. These two nested records identify expensive
late destruction for `(0,119)`; they do not yet distinguish job destruction, cancellation-source destruction
or the worker-pool wake inside deferred release. They also do not establish the cause of the earlier
uninstrumented 4,036 microsecond Curves interval. Raw thread cycles are never converted to elapsed time.

The original cancellation reader refuses the interleaved raw file because it requires contiguous namespace
rows. Main preserves that refusal, validates every diagnostic record, and saves a hashed projection removing
only the validated attribution rows. Every original cancellation row is byte-identical; the unchanged
reader validates the projection's shape. Neither raw nor projected diagnostic input grants G1.

## Preserved evidence and next work

`D:\RedPrismQualificationArchives\P1-Sparse-attribution-native-Release-r862-focused-and-visible-20261011T083753Z`
holds 1,321 read-back copies, including source, contracts, products, focused logs, diagnostic raw output,
both reader versions and the projection. Manifest SHA256:
`662AC1BBBC10E8C6BB79D9AC6A23900791E90B73FBC6CF1660DBCA756B512CD1`.

The private profile and failed-build archive holds 2,952 read-back copies:
`D:\RedPrismQualificationArchives\P1-Sparse-private-profiles-and-attribution-build-failure-20261011T082022Z`.
Manifest SHA256: `EF01E72795B91E420448BD4EC7D029748A3DAFA4887A1DBF9D76B85C2E5FB2DD`.

## Extended recorder and current matrix

Release build `r866` passes (PID 24688, exit 0, 08:41:15–08:47:13 UTC). Its 1,211-input freeze is
`DB8E571639C39757A2FCFDF5D91D70E7384B9C250EA1AE0CE51F44A1F4D7AFA0`; pre-runtime source/20-contract
binding is `E7D9F548C22044F9546A9D772CE4DEB2D3A3E85E401DD11D34DB7DBC6D119FB3`.
It separates job and cancellation-source destruction, adds worker-pool Wake timing with inherited
original-ticket identity, and passes the action destruction origin explicitly through `optional::emplace`.
Its ancestry controls cover nesting/restoration and thread isolation; the private reader passes 26 controls.

- Host `r867`, PID 62692, exit 0: all 8,880 original assertions plus 43 additions; report SHA256
  `3FA51B24A991EF2C6C9983827B0FF1CCD7B3DD7966885A2672B7B7571975B9BE`.
- Application `r868`, PID 40192, exit 0: all 126 ordered stages and 7,399/6,902 original stdout/stderr records;
  report SHA256 `642E772A5CFB4A35D4BC0DC68AED5845DAE2AA1123A27E4D9C2DE295C46876A3`.

The extended hardware diagnostic (PID 25160, exit 0, 08:53:03–08:54:52 UTC) retains 80 active rows after
warmups, 70 measured cancellations and the 22 refusal rows. Apply/action maxima are 188/94 microseconds.
All 88 diagnostic summaries have zero overflow/optional clock failures; no interval reaches the 1 ms
recording threshold. The previous destruction overrun did not recur. This is neither a causal repair nor G1.

The complete read-back archive contains 1,309 files:
`D:\RedPrismQualificationArchives\P1-Sparse-attribution-native-Release-r866-focused-and-visible-20261011T090511Z`.
Manifest SHA256: `DDF25B7D05F23ED0B2C982EB72B19E5A68608636A313FA85812FAE32FD6CD85E`.

Full Debug `r869` (PID 85080, exit 1, 09:04:19–09:17:13 UTC) reaches all core suites, 126 WARP stages and
the direct cancellation smoke, then prepared cancellation smoke refuses ambient keyboard state in Curves
trial 1. Its unchanged guard remains in force. The 1,608-copy archive completion is
`3F4396711E598212E0D9FE9CD7DE8F99AF305ED246F5F99BAF22AEE477BD650C` at
`D:\RedPrismQualificationArchives\P1-Sparse-attribution-Debug-r869-keyboard-refusal-20261011`.

Analysis `r870` (PID 59116, exit 1) fails before compilation because it was launched against the same vcpkg
directory concurrently with `r869`. Its 1,220-copy source/receipt/log archive deliberately contains no stale
native products:
`D:\RedPrismQualificationArchives\P1-Sparse-attribution-analysis-r870-lock-refusal-20261011T092048Z`.
Manifest SHA256: `A610B97E1A5A29F7AC6A736A87E842F9FA5DE73D3F70FB907E85118494A9EB97`.
The remaining native profiles are serialized. Neither failed attempt counts as full qualification.

Serialized analysis `r871` (PID 78432, exit 0, 09:18:29–09:29:23 UTC) passes on the same `DB8E57…`
freeze. Its 1,600-copy read-back archive completion is
`BCC12DAB23D150E4A605D8468AC495369FA979EFA1149F6B181CB5A6397C439C` at
`D:\RedPrismQualificationArchives\P1-Sparse-attribution-analysis-r871-20261011`.
Full Debug `r872` again reaches all 126 WARP stages and direct smoke, then the unchanged prepared-smoke
guard refuses ambient keyboard state in Noise trial 1 (PID 75712, exit 1, 09:31:36–09:46:01 UTC).
Its 1,608-copy read-back archive completion is
`9F61A5E0292E6CE3E32E508B4F1FA0EC6601EB08C9917BFBB08BBC6E678D5DA3` at
`D:\RedPrismQualificationArchives\P1-Sparse-attribution-Debug-r872-keyboard-refusal-20261011`.
Main's full-profile reader explicitly refuses the failed `r869` child; no partial run is reported as full PASS.
After the user reports the keys released, the actual standalone prepared smoke (PID 67084, exit 0,
10:12:49–10:12:58 UTC) passes all ten operations and the unchanged smoke parser. Its executable/source/
contract identities remain unchanged. This is a separate correctness check, not repair of either failed
full profile or timing evidence. A private-desktop feasibility check (PID 86416, exit 5) then refuses
`presentation-unavailable` with `occluded=1`; that desktop cannot replace the real presentation route.

Full Debug `r876` (PID 26016, exit 0, 10:18:56–10:34:45 UTC) passes on the same 1,211-input freeze.
The complete reader preserves Host 8,880+43, Document 9,008+18, Composition 6,838, Io 1,823, all 126
application stages and original assertions, 63 short/132 RBX parser controls, 22 sizing cases and two
cleanup controls. Its 1,610-copy read-back archive is
`D:\RedPrismQualificationArchives\P1-Sparse-attribution-Debug-r876-full-20261011`, completion SHA256
`BF2ABF21840C4DAA507F48F1082180BC7802FD2BD458B6816C1F6EAE978A9680`.
Full Release `r877` (PID 71532, exit 0, 10:37:05–10:49:32 UTC) then passes with the same inventories,
126 stages and parser/sizing/cleanup gates. Its 1,588-copy archive is
`D:\RedPrismQualificationArchives\P1-Sparse-attribution-Release-r877-full-20261011`, completion SHA256
`EE9AF04764A84FB2B3CA2203FB2F59845F6EC5BBB6F6EE71AF94BC88A1436B4C`.
The unchanged complete reader authenticates both profiles. Full real ASan `r878` (PID 37764, exit 0,
10:49:56–11:12:12 UTC) also passes on the same freeze and contracts, including Host 8,881+43,
Document 9,008+18, HostStress 9,279+43, DocumentStress and all unchanged application/core assertions,
126 stages, parser/sizing/cleanup controls and the actual heap-use-after-free detector probe. Its archive is
`D:\RedPrismQualificationArchives\P1-Sparse-attribution-ASan-r878-full-20261011`, completion SHA256
`9042DDBD9E5FFC9C052030A9D23635FF3AE6925FEF3E054D343EA5E3510ED0CC`.
Main's ARM64 `r97` gate independently authenticates these three complete profiles and the actual `r871`
analysis archive. DRAFT and missing-ASan controls refuse before reading reports. It preserves the original
pre-runtime source binding's digest/timestamp and records the later authorization honestly. Cross-builds
start at 11:13:32 UTC and finish at 11:18:33 UTC. Debug, Release and real ASan cross-builds pass;
the independent readers authenticate all 15 PE/PDB pairs and five ASan compiler/image controls on the same
source, contracts and unchanged DxUi pin. Archives are under
`D:\RedPrismQualificationArchives\P1-SparseAttribution1211-r97`; the ASan archive completion SHA256 is
`A08896541D0DD345DD440C0C79FAE434E94BD923897BA65496DD32AF7BD28760`.
Native ARM64 runtime remains unavailable. This completes the current sparse-read/attribution checkpoint's
matrix; it does not close P1 delivery or G1.

Native ASan build `r873` succeeds. Focused actual ASan `r874` Host (8,881 unchanged records plus 43) and
`r875` Document (9,008 plus 18) pass with unchanged source and executable identities; both read-back
archives authenticate against the exact `r873` build. These focused scopes do not replace the full ASan
profile's actual detector probe and stress/runtime gates.

The counter-only wake draft is independently reviewed: original C++ tests compile with warnings as errors,
704 deterministic reset/CAS schedules and a four-consumer bounded-count stress pass. Main independently
runs the executable/model and checks source hashes. This does not qualify Windows work publication,
event wake/rearm, Close or caller lifetime; no broker is integrated. The complete native matrix,
confirmed-path repair and fresh uninstrumented G1 runs remain due.

The later private broker uses one auto-reset event and one one-shot wait on a borrowed existing compute pool;
its owner request has no loop, allocation, SRW acquire or callback submission. Main tests seven deterministic
request/take/rearm/held-lock/Close/refusal cases in normal and genuine ASan runs. The ASan detection probe
actually reports heap-use-after-free in the same executable. This remains a standalone protocol draft.

The cheaper agent's SList caller draft drains/charges real fixed nodes but initially labels bookkeeping
increments as destruction. Main preserves that draft, adds actual `optional<Payload>` destructors, verifies
each node is still charged during its destructor, and moves parent-Close retirement to a joined off-owner
thread. The corrected three scenarios pass normal and genuine ASan: all 32 nodes/4,592 charged bytes are
retired exactly once, later requests survive rearm, and the pending node stays charged through broker Close
until explicit parent retirement. Original byte/count/queue predicates remain intact and hint saturation is
strengthened to the exact expected two. This qualifies actual private payload lifetime, not original
WorkerPool/JobScheduler tickets, native caller admission, production idle or shutdown integration.
Main then creates an isolated opt-in bridge inside the actual `WorkerPool`, leaving the original default
route intact. It reuses that pool, its priority work lanes, callback scope, apartment and joined Close hooks.
Normal `normal-r3` and genuine ASan `asan-r4` preserve all 40 original worker assertions and pass 14 additions:
48 real bodies across three priorities, a foreign producer using the original thread-safe route, pending
broker hints while the sole worker is occupied, ordered Close hooks and actual off-owner payload destruction.
The same ASan image reports an actual heap-use-after-free probe. No native `JobScheduler` uses the bridge;
complete admission, original tickets, live-event failure policy and latency remain unqualified.

The subsequent isolated `JobScheduler` overlay enables this bridge in its actual compute/recording pools.
All 128 original compile items and test predicates remain. Debug `debug-r3` and Release `release-r5` preserve
8,923 original Host assertions; real ASan `asan-r4` preserves 8,924. Each adds 13 pool checks, and the ASan
probe reports actual heap-use-after-free in that same private executable. Initial UTF-8 and delimiter reader
refusals are preserved before exact assertion-byte comparison uses the existing authenticated delimiter.
Copied application executables are separately identified resource/version references; they contain the
original pool and establish no bridge UI acceptance. Native application integration, complete admission,
fault/crash policy and measured costs remain due.

Main's subsequent private fault harness compiles the exact broker body with only its event creation, wait
creation and signal call sites replaced by deterministic failing Win32 wrappers. Production code has no
fault hooks, and no live handle is invalidated. Normal `normal-r1` and instrumented ASan `asan-r2` pass six
startup/refusal/actual-event-cleanup checks. Separate child PIDs 79256 and 87992 exit `0xE0000001` after a
signal failure following publication; the actual installed crash handler produces one nonempty dump and a
marker naming it. The minidump exception stream carries the same terminate code. No in-process Host suite
installs the handler. This qualifies the private failure policy, with native admission/acceptance still due.

Main also reviews the cheaper agent's refusal formatter, corrects its Windows-only header type and throwing
formatter, and independently runs its 30 unchanged literal/parser/bounds checks. The header-only compile and
warnings-as-errors run pass. The diagnostic capture remains an unapplied sketch; the native keyboard guard
and successful benchmark rows are unchanged. Skills validate all 15 entries; formatting checks 1,069 files.

All nine parent delivery areas remain open; there is no ordinary-activation, P1 Done or merge claim.
