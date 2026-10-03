# Immutable document record roots candidate (P1.9 H2.2)

## Implementation checklist

- [x] Seed 519 owned files from the qualified lifecycle private r2 archive into a separate private workspace.
- [x] Primary prepares a standard C++ immutable root and exclusive builder; no mutable published-record access.
- [x] Review the cheaper-agent independent ownership/cancellation/failure/reader tests and register them privately.
- [ ] Qualify the private foundation against the completed native predecessor; preserve every failed attempt.
- [ ] Integrate document layer records, private mutation builders, complete reservations and retirement.
- [ ] Qualify unchanged behavior/goldens, all six profiles and paired capture/mutation costs; archive exact evidence.
- [ ] Update the owning contracts/checklists, commit and push the qualified integration.

## Source preparation

The isolated root is
`C:\Users\eric\AppData\Local\RedPrism\PrivateWork\p1-document-record-roots-proof`.
Its 519 original files are copied and hash-verified from
`p1-selection-lifecycle-outline-private-r2-Debug-qualified`, completion
`2A706D648A4051F62B3FB171A12482C35827145EEE07A0EAEE727378690739E9`.
The running lifecycle native matrix uses different frozen sources. This candidate has no compiler/runtime proof
and has not been activated into the application.

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
controls keep an old captured root valid through new private-builder publication. These are reviewed and
registered source controls; no compiler/runtime success or actual job budget/COM retirement is claimed yet.

The foundation does not declare a whole-job memory bound. Record children, root/control-block allocations,
captured history, prepared output and final retirement require the integration's complete admission accounting.
An individual string copy/allocation remains an uninterrupted region; checking each record does not establish a
time deadline. Last-root destruction belongs to the admitted worker/retirement path. Generic thread tests alone
cannot qualify actual scheduler/COM retirement or UI apply timing.

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
