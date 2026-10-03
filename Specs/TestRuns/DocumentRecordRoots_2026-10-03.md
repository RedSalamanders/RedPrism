# Immutable document record roots candidate (P1.9 H2.2)

## Implementation checklist

- [x] Seed 519 owned files from the qualified lifecycle private r2 archive into a separate private workspace.
- [x] Primary prepares a standard C++ immutable root and exclusive builder; no mutable published-record access.
- [x] Review the cheaper-agent independent ownership/cancellation/failure/reader tests and register them privately.
- [x] Qualify the initial ownership foundation against the completed native predecessor; preserve the failed attempt.
- [x] Inventory document writers and mutation/pointer lifetime hazards with primary review.
- [x] Qualify value equality/capacity refusal and the private benchmark's complete correctness run.
- [x] Qualify the repaired native foundation and paired Release costs; preserve all original failures.
- [x] Independently verify the complete native archive and record the measured primitive costs.
- [x] Prepare read-only span helpers and direct-array/immutable-root borrowing controls in a second isolated workspace.
- [x] Privately qualify the read-only helpers and preserve all root and preceding document controls.
- [ ] Integrate document layer records, private mutation builders, complete reservations and retirement.
- [ ] Qualify unchanged behavior/goldens, all six profiles and paired capture/mutation costs; archive exact evidence.
- [ ] Update the owning contracts/checklists, commit and push the qualified integration.

## Source preparation

The isolated root is
`C:\Users\eric\AppData\Local\RedPrism\PrivateWork\p1-document-record-roots-proof`.
Its 519 original files are copied and hash-verified from
`p1-selection-lifecycle-outline-private-r2-Debug-qualified`, completion
`2A706D648A4051F62B3FB171A12482C35827145EEE07A0EAEE727378690739E9`.
The lifecycle native matrix has completed and is committed as `39c2f92`. Private r3 qualifies value equality,
oversized-capacity refusal and the complete document suite. Its benchmark also passes a separate correctness run.
Seven reviewed foundation files are activated; native r1 stops at Release's linked ISA gate. Its exact failure
is preserved below before a narrow benchmark-fixture repair. Native r2 qualifies all six profiles and the paired
Release comparison. Full document integration remains open.

`Common/RecordRoot.h` retains a `shared_ptr<const vector<Record>>`. Copy/move and all reads, including on a
non-const handle, borrow the immutable root. Published-root mutation is unavailable; `use_count` never authorizes
an in-place write. A separate noncopyable builder reserves its admitted record limit before editing; append and
insert refuse at that limit before moving an incoming record. Replacement/erase use the private buffer only.
The builder exposes const reads and never returns a mutable record or iterator.

Cloning reads the old root into a new private buffer with cancellation before each record and after the final
copy. A failed/cancelled clone leaves the old root intact. Record copy allocation failures propagate to the
operation's existing worker failure boundary. Sealing first allocates its immutable root owner, then transfers
the private buffer and consumes the builder; a failed root allocation leaves the builder available for retry.
Moved/consumed builders refuse editing and resealing. Record moves, assignments and destruction must be noexcept.

Primary reviews the cheaper-agent tests and adds an allocator parameter with the standard allocator as the
default. Record-buffer and shared-root allocation use that supplied allocator; a new clone receives an explicit
new allocator instead of inheriting the old root's allocation owner. Stateful allocators must preserve noexcept
container movement. Independent allocator controls now reach constructor, root-seal and new-clone allocation
failure, prove retryable sealing and original-root preservation, and account every tracked container/root allocation's
release. A reached record-copy fault separately releases its new buffer. Added event-ordered reader/writer
controls keep an old captured root valid through new private-builder publication. The initial private revision
qualifies below. Actual job budget/COM retirement remains an integration obligation.

The foundation does not declare a whole-job memory bound. Record children, root/control-block allocations,
captured history, prepared output and final retirement require the integration's complete admission accounting.
An individual string copy/allocation remains an uninterrupted region; checking each record does not establish a
time deadline. Last-root destruction belongs to the admitted worker/retirement path. Generic thread tests alone
cannot qualify actual scheduler/COM retirement or UI apply timing.

## Initial private qualification

The qualification binds the completed six-profile lifecycle native archive, completion
`97C09D00D3CF1CD164BD0A3C11E6E0CAF1C12F7D4FA1BF210E66E7083D169E70`, verifies its complete source/evidence tree,
and checks 483 unchanged native/private compiler counterparts. Each private attempt freezes exactly 646 inputs
(522 owned files plus 124 external/header/library/helper inputs). Normal full `DocumentTests` runs with no subset.

Private r1 builds successfully, then exits 3 at the first injected allocator fault. MSVC 14.51.36231's Debug
`vector(const Allocator&) noexcept` first allocates its iterator proxy. Injecting there terminates before the
record-buffer failure boundary can handle the refusal. Its exact source, image/PDB, logs and actual failure
remain in `p1-document-record-roots-private-r1-Debug-failure` (646 inputs / 154 evidence files), completed
`2026-10-03T15:49:44.5676373+00:00`, completion
`3F078E983DB458C2AC85D3D897FF12529268FA0494AC9126FFFCC3C23DD0BAA5`.
All archived lengths/hashes were independently verified before editing.

The corrected controls inject constructor/clone refusal into the actual record-buffer reserve. Debug proxy
allocations remain counted and must retire; final release checks run after the consumed/moved builder and
clone owners leave scope. Root-seal failure still targets the actual shared-root allocation.

Private r2 builds at `2026-10-03T15:54:52.2309559+00:00`–`15:55:01.8101482+00:00`, actual exit 0. Its full runtime
at `15:55:02.8911508+00:00`–`15:55:31.9485158+00:00` exits 0, with 29 unique root assertions and exactly all 5,704
predecessor document PASS names retained. The qualified archive (646 / 154) completes at
`2026-10-03T15:55:39.9694997+00:00`, completion
`F9105F096532C3C4CEACE49EB61B9D5461E86D0827116A03AFBA79464D1A16D9`.
Its source manifest is `36EC52C8E9CC0966C5C4A437FAF7FECC6479E6DCDD67FECB2386521024AF1F7B`, evidence manifest
`F9EA90D3CE2F68A783CF175A0986C090CC83248C79299079752166A7A0347FFE`, executable
`21C087559EAB3B02DEED1122442277809AA85C6D16A9AC2C626C426D28128A17`, PDB
`3B451494EB7D651BA35C1197238AB61FF1E903BCBE7F614E477AD245CD9FF100`.
All archived lengths/hashes were independently verified before the next revision. This qualifies the initial
ownership foundation only; later source additions need fresh qualification.

## Extended private r3

Oversized builder capacity now refuses before record-buffer reservation, including empty-source Clone. Root
identity remains an equality fast path; separately sealed equal values compare equal without record copies.
Changed values, lengths, order and empty sequences retain the existing record-value semantics.

Private r3's 648 inputs (524 owned plus 124 external/helper inputs) build successfully at
`2026-10-03T16:07:43.1292461+00:00`–`16:07:54.7566551+00:00`, actual exit 0. The complete runtime at
`16:07:55.9234859+00:00`–`16:08:36.7836647+00:00` passes 34 unique controls and the exact 5,704 predecessor
document assertions. The archive completes at `2026-10-03T16:08:49.0531509+00:00` with 648 inputs / 155 evidence
files, completion `A6F04E9F16338A242CDDC27A90BE18F0B49A69062C7D96F9310F054285349D80`, source manifest
`C77349B77450E5E390D4CEC809ECDFB67C0CE0464F034AABC54B4526474C852C`, evidence manifest
`038E3B161C5849189FCDE3A5106A4E827821580278F701B4B79C15BF46433B0D`, executable
`63F6E6109D6E5432636576725374344A46633FB2CD16A0012A0F61343B355C79`, PDB
`085468B6A0B1502F60AC85B49254B148C95B6572DFC7F11FEDB0649F8B6306F8`.
All archive lengths/hashes are checked before native activation.

The same private Debug image runs `--bench-record-roots` at `2026-10-03T16:13:14.7209365+00:00`–
`16:13:19.6527235+00:00`, actual exit 0, with all 60 unique paired rows and complete source/result checks.
Debug timings are retained as diagnostics; they are not Release cost evidence.

## Native r1 preserved failure

The native matrix owns PID 103952 at `2026-10-03T16:14:28.3780018+00:00`–`16:34:03.3193438+00:00`, actual exit 1.
It freezes 637 compiler/build inputs, validates 15 skills and checks formatting for 572 files. All three ARM64
cross-builds pass. Full x64 ASan completes at `2026-10-03T16:31:39.9257873+00:00`, including the real sanitizer
probe, WARP runtime and all root/preceding suites. Release produces its images, then the ISA gate rejects 16
VEX/EVEX instructions in `RecordRootBench.obj`; its runtime never starts. x64 Debug does not build or run.
The allowlist stays unchanged.

PDB and disassembly locate the rejected instructions in the benchmark's deterministic `ExpectedGuid`, not the
root capture/mutation path. MSVC emits a vectorized byte-pattern multiply, including EVEX conversion, behind its
runtime feature check. The baseline first-party fixture must still satisfy the linked ISA contract. The narrow
repair uses the identical fixed byte pattern directly; its low/high index bytes remain the unique id keys.

Before editing, the failure archive completes at `2026-10-03T16:41:23.8607387+00:00` with 637 inputs / 3,587 evidence
files, completion `14E755F526991E50289EFD410C1809AB7C0067BFC53D234B42D6BF1BD46EB50F`, source manifest
`95FAC97D990F3A761F4D8285F4E9D055012390C83072254F7D809D9A303AB338`, evidence manifest
`2920EDAC63114BC1FDF82A2D6FFE2186938490656ABC2B11C9429CFF3142D3B2`.
It retains the exact failed EXE/PDB, decoder output, compiler objects/tracking, all logs and actual process exit.
The first archive helper has a PowerShell string interpolation parse error before any archival action; its
exact original is retained beside the corrected helper. No compiler input is edited to repair that helper.

## Native foundation qualification and Release costs

The repaired native r2 matrix, PID 91904, runs from `2026-10-03T16:52:33.5034312+00:00` to
`2026-10-03T17:26:47.0467798+00:00`, actual exit 0. All three ARM64 configurations cross-build; all three x64
configurations pass the complete normal suites, linked ISA controls, hidden WARP self-test and screenshot gates.
ASan also passes its actual detection probe. Each x64 document suite retains all 5,704 preceding assertions and
passes the 34 exact root controls. All 637 compiler inputs remain unchanged. Independent verification confirms
that the literal Guid fixture is the only compiler-source change from the preserved r1 failure.

The immutable `p1-document-record-roots-native-r2-qualified` archive completes at
`2026-10-03T17:27:27.3254563+00:00`: 637 inputs / 217 evidence files. All copies and manifests are independently
verified by length and SHA-256 before further native changes.
Completion: `D36C0CD406F75E7146E2F6888E226D65E920EDDF5051656DCBBAADB7C0FC6AE1`.
Source manifest: `C1E151F0D8DD4838602360259B01854FC4966BF78B6DA78FAD22AA7F12621740`.
Evidence manifest: `95453A87CC6841194D270C7FCCFFB719CEB25F620B5EA7202D34F0F0348F4CA2`.

The qualified x64 Release DocumentTests comparison runs at `2026-10-03T17:26:56.9854481+00:00` through
`17:26:58.2997204`, actual exit 0, on AMD Ryzen 9 9950X3D (16 cores / 32 logical processors).
EXE: `35F25A09A5357F27D4FA6944517C28CD4F602538F9D974BC5776C3058A002D44`.
PDB: `54D816D87852CE70A4F996B0B2680FDF8D98B0864FFC498BA10280F32C698D4B`.
The archive retains all 60 paired rows, image binding, complete content checks and checksums. Medians over five
alternating rounds follow; units are microseconds per operation, including fixed samples and final destruction.

| Records | Root capture | Vector capture | Root Clone/Replace/Seal | Vector copy/replace |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 0.051 | 0.122 | 0.221 | 0.167 |
| 100 | 0.052 | 4.901 | 4.922 | 4.880 |
| 10,000 | 0.052 | 5,999.500 | 5,992.825 | 5,861.812 |

Root captures have constant record cost. Building a changed 10,000-record root still takes about 6 ms and must
remain on an admitted worker. The additional seal/control allocation is accepted for immutable ownership;
the one-record mutation median adds 0.054 microseconds in this primitive comparison. Raw ranges and shared-desktop
outliers remain in evidence. These timings establish neither actual CanvasDocument integration nor UI/G1,
RAW decode, GPU throughput, native ARM64 runtime or Photoshop performance.

## Document integration sequence

- [x] Seed 524 reviewed foundation inputs into `p1-document-layer-records-proof`; no native files are shared for mutation.
- [x] Prepare span-based pure read helpers. Primary adds the document forwarding overload missed by the agent's
  `EffectivelyVisible` caller inventory, keeping `AdjustEdit` source compatible without record copies.
- [x] Prepare direct array/sliced/empty-range, complete/incomplete graph, literal hierarchy/visibility/clipping,
  compatibility and retained immutable-root view controls. These are source-only, with no compiler/runtime claim.
- [x] Qualify the view helpers independently against the reviewed foundation and retained document controls.
  Private compilation can run alongside the repaired native matrix because its output is isolated and its
  dependencies are read-only. Native activation still requires the completed repaired foundation.
- [ ] Replace `CanvasDocument.layers` with its immutable root and expose explicit private builders for every writer.
  Root capture/read/equality must preserve scalar fields, selection, order and raster identity; const helper
  compatibility must never construct an owning vector. Published records cannot yield mutable pointers.
- [ ] Migrate worker after-document/history construction: reuse roots when records are unchanged; build complete
  replacement roots when they change. Keep all construction, cancellation and failure before checked publication.
- [ ] Migrate structural/group/clipping/import/merge/size/floating writers with exact repair/order/rollback behavior.
- [ ] Migrate metadata setters and preview/display construction. An inline whole-root clone in a production UI
  setter is a regression until measured and moved to the admitted worker path; a mutable compatibility alias
  into a published root is forbidden. Preserve eager semantic mask thumbnails and successful-replacement-only discard.
- [ ] Update complete reservations and final-owner retirement for old/new document, history, previews, strings,
  child storage and actual record capacities. H3 incremental unique accounting remains a separate gate.
- [ ] Prove actual owner capture/apply cost and complete native behavior/goldens on all six profiles. Primitive
  benchmark improvements cannot close this document integration or the real UI G1 gate.

## Private layer read-view qualification

The independent `p1-document-layer-records-private-r1-Debug-qualified` run builds at
`2026-10-03T17:06:36.5174806+00:00` through `17:06:48.2509884`, then runs the normal complete DocumentTests
at `17:06:49.8256460` through `17:07:28.4081305`, both actual exit 0. It passes all 12 literal range/borrowing
controls, all 34 exact root controls and the unchanged 5,704 document assertions. Its 650 input / 153 evidence
archive completes at `2026-10-03T17:07:38.5297354+00:00`; every archived copy's length and SHA-256 is independently
verified. Completion: `B50F5A6026630C55C9D5A7DF2E4AD726660E876F901EF928375DDFD711297C23`.
Source manifest: `7D83C09BF312543BA54CF3A17637E846DAFFB7CBC474D3F1765B0127618E4B2F`.
Evidence manifest: `EE8CE56386A297A209D314DCCF3E227AFFFE272BE4758E96C891689D87322F4B`.
EXE: `C0338C1D2159A03AD1186EE3D2A0AAF14DC11951F61384FF39EADE4124063D17`.
PDB: `1167888C60C2DF27981B096FDE076B2418C3A6B892ACB7C9B11639B49A2F2F76`.

Private compilation runs alongside the repaired native foundation matrix with isolated outputs and read-only
dependency inputs. The completed core foundation and frozen repaired fixture bind the candidate; it is not
native activation qualification. The review-only document forwarder borrows its layer range and preserves the
AdjustEdit caller missed by the initial inventory. Full writer migration and owner capture remain open.

## Integration requirements

Replace owner-side `CanvasDocument` record copies with shared immutable layer roots while preserving its scalar
fields, immutable selection, exact layer ordering, value equality, persisted schema and all numeric goldens.
Convert every writer to an explicit private builder before exposing a replacement. Read-only non-const callers
must never clone. Session lookup/read paths retain const records; mutable layer helpers must edit the private
replacement, never return a pointer into a published root. The existing direct reference APIs retain behavior;
their construction remains separate from deferred production placement.

Worker history snapshots share completed roots. Worker after-document construction and renderer display
construction use private builders. Root lifetime stays bound to the captured input/output and retired aggregate,
including refusal and stale completion. Account actual container capacity and all owned child metadata;
incremental storage indexes and constant-time admission remain H3 until separately qualified. Compare 1/100/10000
record captures and mutations with the retained vector reference, retaining literal data/checksums and raw
measurements. No G1 improvement is claimed from source structure alone.
