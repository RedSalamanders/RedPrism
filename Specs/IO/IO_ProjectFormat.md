# Project format (`.prism`)

Status: current normative product contract
Last reviewed: 2026-10-14
Owner: `Common/ProjectStore.*`, `Common/ZipOwner.h`

## Scope

This contract owns how RedPrism loads and saves RedPrism documents. Image decode/encode is
[`IO_ImageCodecs.md`](IO_ImageCodecs.md). Document graph rules are
[`Document/Document_Model.md`](../Document/Document_Model.md). The required serialized shapes are specified below.

## Package shapes

A project is either:

1. A ZIP archive whose name ends in `.prism` (Windows file, same layout as the Mac package), or
2. An exploded directory containing `manifest.json` and `images/`.

`PathLooksLikeZipProject` is true for a directory-less path named `.prism` and for an existing file that starts
with a ZIP signature (`PK` 03 04, 05 06 or 07 08); any other file (an image dropped on the window) is not a project.

ZIP I/O uses libzip through `unique_zip` (`zip_discard` on abandon, `zip_close` only on commit). Handles are never
left raw. ZIP members are opened with `zip_source_win32w_create`. `..` in an asset name is rejected. An exploded
asset or `manifest.json` is rejected when any path component after the project root is a reparse point
(`ExplodedMemberPathAllowed`, `ProjectIoStatus::Invalid`). A missing file is `MissingImage`, not a reparse failure.
A truncated, empty, or non-ZIP `.prism` is `Invalid` or `MissingImage` and yields no layers.

For containment comparison, matching ordinary and extended DOS spellings, or matching ordinary and extended
UNC spellings, use the same case-insensitive root and separator boundary. Attribute/reparse checks retain the
candidate's original API spelling. Other device or volume-GUID namespaces compare only within that same
namespace; no filesystem identity equivalence is inferred. Empty, dot and parent components remain refused.

`Win32FilePath` resolves ordinary project roots before selecting their API spelling. Already extended/device
names are preserved. Directory paths use the extended spelling from 248 UTF-16 units and file paths from 260;
long DOS/UNC paths are converted only after ordinary parsing components are resolved or refused. Input/output
aliasing cannot discard the source spelling during prefix construction.

Atomic save: exploded projects write into an exclusively created, unique staging directory in the exact
destination parent using the sibling naming rule below, then rotate `dest` → `dest.old` and staging → `dest`. Before moving `dest`, the writer must
remove the reserved prior `dest.old`; if that cleanup fails, it leaves `dest` untouched, reports the cleanup error
and does not publish. A cleanup already in progress may partially remove the older backup; a fault injected before
the cleanup starts leaves both trees untouched. If `dest` → `dest.old` fails, `dest` remains the current project and the save
fails. If staging → `dest` fails after the first rename, the writer attempts `dest.old` → `dest` even if
cancellation arrives. A successful rollback leaves the original tree at `dest` and the save fails. If rollback also
fails, the save fails without a `Published` report; the old complete tree remains at `dest.old` for recovery, the
new staging tree is cleaned up when possible, and the failure observer receives both rename errors. When a save
is retried while `dest` is absent, retain the surviving `dest.old` through the new publication attempt; a failed
attempt must restore or preserve that same recovery tree. Remove that backup only after successful publication.
Once staging → `dest` succeeds, publication is irreversible: report `Published` and return `Ok` even if later backup cleanup
fails; retain `dest.old` as recovery and report its exact cleanup error. Cleanup reports never claim publication or
replace the initiating failure status. Cleanup touches only the exact reserved backup or uniquely created staging
path, never sibling names selected by a wildcard.

Before the first successful rotation rename, a **native** destination-to-backup rename error
`ERROR_ACCESS_DENIED` or `ERROR_SHARING_VIOLATION` permits at most two additional attempts after one-shot worker
timer waits of 25 ms and 100 ms. A healthy rename creates no timer. `DestinationRetryScheduled` reports the
immediate native error before each wait. Cancellation before/after a wait returns Cancelled with the existing
destination intact; once the first rename succeeds, the existing finish-or-recover rule applies. Other errors
and injected destination-rename failures are final. `DestinationTimerCreate/Arm/Wait` inject only timer resource
failures after a reached native refusal; failed resource operations report the original native rename error,
preserve the existing destination and clean only the owned staging tree. This extends failure recovery after a
recorded Windows error 5 in `save-durability.folder-rotation`; it does not identify the external reader or filter.
The added worst-case scheduled wait is 125 ms on that error path, with no busy polling or UI-thread wait.

Staging-to-destination publication also permits at most three native rename attempts, only after
`ERROR_ACCESS_DENIED` or `ERROR_SHARING_VIOLATION`, with one-shot worker timer waits of 25 ms and 100 ms
and a 1 s failure bound per event wait. `PublicationRetryScheduled` reports the immediate native refusal.
Before destination rotation succeeds, cancellation at the next boundary returns Cancelled and leaves any
earlier destination/recovery backup intact. After rotation succeeds, a late stop cannot abandon publication:
finish the rename or restore the complete backup using the existing recovery policy. The rename never replaces
an unexpected destination. A successful rename reports Published exactly once and returns Ok, including when
stop arrives afterward. Exhaustion reports the actual final staging rename error before rollback/cleanup.
Injected staging-rename errors remain final and visit no publication timer hooks. Separate
`PublicationTimerCreate/Arm/Wait` faults stop recovery and retain the initiating native rename error.
Healthy native publication creates no timer. This policy follows the recorded r6 first-save error 5;
the responsible reader/filter or other external cause remains unproven. It adds at most 125 ms of nominal
scheduled wait on that publication error path; filesystem calls already in flight remain outside these bounds.

Recovery from a **native** rollback rename error `ERROR_ACCESS_DENIED` or `ERROR_SHARING_VIOLATION` permits at
most three attempts. Between attempts, the calling IO worker blocks on a one-shot waitable timer with nominal
25 ms then 100 ms delays; each event wait has a 1 s failure bound. There is no busy polling, healthy-save timer
cost or UI-thread wait. `RollbackRetryScheduled` reports the immediate native error before each wait. Cancellation
after rotation starts cannot abandon recovery. Other native errors, unavailable/failed timers and injected
operation failures are final; exhausted recovery preserves the complete `.old` tree and reports `RollbackFailed`.
The per-save fault policy exposes separate recovery timer create/arm/wait boundaries, consulted only after a
qualifying native rename failure. A nonzero resource fault stops recovery immediately and still reports the
initiating native rollback error; it cannot claim successful publication or replace that error with the seam's
configured value. Healthy saves and an injected final rollback-rename failure reach none of these timer hooks.
These bounds cover attempts and event waits, not interruption of an in-flight filesystem call. NTFS can reject
a directory rename while any descendant file is open; the independent real-reader fixtures exercise both release
at a retry observation and a reader retained through all attempts. They do not identify the external reader or
filter responsible for previously observed transient errors.

Tree cleanup uses a single 64 KiB path buffer allocated before creating staging, and makes no C++ heap
allocations after it starts, including during bad_alloc unwinding. It removes reparse entries themselves without
traversing their targets. Paths are bounded to 32,767 UTF-16 units and ordinary directory traversal to 128 levels;
exceeding either reports ERROR_FILENAME_EXCED_RANGE. Real cleanup errors may leave a partly cleaned owned tree.
Read-only files are not forcibly changed. Capture and report the initiating Win32 error before later cleanup.

ZIP projects write into a unique file in the exact destination parent using the sibling naming rule below,
opened with `ZIP_CREATE | ZIP_EXCL`, then
`ReplaceFileW` (with the existing `MoveFileExW` fallback). Each staged manifest/asset file is flushed and closed
successfully before publication; ZIP close, flush, and handle close must all succeed before replacement. A failed
pre-publication staging or durability step leaves the previous destination intact and removes only the temporary
file or directory owned by that save. Existing ZIP readers accept both stored and deflated entries. Writers store
already encoded PNG assets and deflate the manifest.

The optional, per-call `ProjectSaveFaultPolicy` also exposes manifest flush/close for both shapes and ZIP commit,
archive flush/close and replacement. Existing directory fault slots 0–13 remain stable; the six durability slots
are appended, with `Count` bounding visitor arrays. A nonzero callback denies its operation before execution,
returns `Encode` without `Published`, and reports the exact error before RAII discard/close and owned temporary
cleanup. Flush errors report `FlushFailed`, close/commit errors `CloseFailed`, and replacement errors
`StagingToDestinationFailed`. ZIP replacement alone among these six gates follows `Staged`. The default empty
policy executes the real operations and creates no injection state. These seams qualify controlled failure
boundaries; they do not establish interruption or cancellation latency for a codec or filesystem call in flight.

Both shapes first encode assets serially into bounded files in the owned sibling staging directory. ZIP saves
use file sources for those assets through close; they retain filenames/manifest, not all encoded PNG vectors.
Staged asset/manifest files are flushed and closed before rotation or ZIP replacement. Metadata and libzip's
per-source path copies remain charged by graph admission. Serial encoding and extra ZIP staging IO are measured
tradeoffs; bounded parallel asset encoding remains tracked in P1.7.

## Win32 staging names

Generated staging uses `<destination-leaf>.~<guid>` when its leaf fits 255 UTF-16 units. Otherwise it uses
`RedPrism.~<guid>` in the exact same parent. The 38-unit GUID suffix remains unique; creation is exclusive,
and cleanup addresses only the exact owned path. Empty or over-255 destination leaves and invalid/oversized
suffixes refuse preparation. This shared rule also applies to PNG/JPEG export. The reserved `dest.old` recovery
name retains its existing semantics and must independently fit the filesystem's component limit.

The native behavior uses this 255-unit bound without querying every destination volume. Current runtime
evidence covers local fixed NTFS and ReFS volumes reporting that limit, not arbitrary or remote filesystems.
Checked flush/close are publication prerequisites; these fixtures do not establish power-loss survival,
slow-I/O latency or interruption of an in-flight filesystem call.

## Manifest

UTF-8 JSON, pretty-printed, object keys in alphabetical order. Maximum **4 MiB**. Identity:

| Key | Rule |
| --- | --- |
| `format` | `com.redprism.project` |
| `version` | integer 1–7 (current writes **7**) |
| `colorSpace` | `sRGB` |
| `documentID` | UUID |
| `width` / `height` | 1–30 000 |
| `resolution` | 1–9600, default 72 |
| `activeLayerID` | optional UUID that MUST name a layer |
| `layers` | bottom-to-top array, ≤ 10 000 |

`imageFile` on a layer, when present, MUST equal `<layer UUID>.png` under `images/`. `maskFile` MUST equal
`<layer UUID>.mask.png`. Other names, including `../`, are invalid. UUIDs are written uppercase and read in either
case.

## Version gates (read)

| Version | Added fields |
| --- | --- |
| 1 | document, layers, transforms, PNG assets |
| 2 | `parentID`, `isGroup` (groups have no `imageFile`) |
| 3 | `opacity`, `blendMode` (groups remain opacity 1 / Normal on disk) |
| 4 | raster `maskFile` / `maskEnabled` on image layers |
| 5 | `maskSourceID` live/clipping mask |
| 6 | folder masks on groups |
| 7 | `adjustment`, `maskPlacement`, `maskLinked`, `shape` |

Files declaring an older version MUST NOT contain newer fields. Unknown versions are `UnsupportedVersion`. Mask
metadata (`maskEnabled`, `maskPlacement`, `maskLinked`) without `maskFile` is `Invalid`.

## Load order

The manifest is parsed and validated (`ValidateDocument`) before any asset is decoded, so a damaged manifest never
costs an image decode. Mask records exist at that point without pixels; the Gray8 format check applies when the
mask PNG is attached (the decoder rejects anything but 8-bit gray). Pixel budgets (100 M image + 100 M mask) are
independent aggregate limits: each decoder receives the remaining count for its asset kind and rejects from frame
dimensions before output-pixel allocation. Attachment retains a defensive post-decode check. `ProjectLoadLimits`
(`ProjectLoadPixelLimits` remains an alias) can lower either total, never raise the hard limits; negative limits are `Invalid`, and a zero limit refuses any
asset of that kind. A later oversized asset returns `TooLarge` with no document or active id published. Both ZIP
and exploded projects follow this rule. A missing or undecodable asset is `MissingImage`; the current document is never replaced
by a partial load.

### Adaptive decode ceilings

`ProbeProjectLoad` validates the manifest and inspects only each asset's fixed 33-byte PNG header and encoded
length on its calling worker. It returns standard values: aggregate image/mask pixels, layer count, manifest
bytes, largest encoded asset and format version. It creates no WIC decoder, raster or asset-sized vector. A
failed/cancelled probe exposes zero limits; `UnsupportedVersion` retains the file's version for its notice.
`ManifestReady` and counted `AssetInspected` observations describe this phase; they are not `AssetDecoded`.

The PNG decoder remains authoritative for complete asset validity. Header inspection checks the mandatory PNG
signature/IHDR shape, nonzero bounded dimensions and Gray8 mask header; it does not validate CRCs or image data.

Passing these limits into `LoadProject` also bounds manifest input bytes before reading, layer count before
record allocation, and encoded asset length before a ZIP vector or file decoder. Source changes between probe
and decode may succeed within all ceilings or return a local failure; they cannot increase declared output or
caller-retained input allocations. This is resource planning, not a claim that reopening the path snapshots its
identity/content. Accepted PNG conversions, schema, ordered attachment and eager semantic mask thumbnails are
unchanged; image thumbnails are absent from decoded project assets.

`ProjectLoadMetadataMemoryBytes` / `ProjectLoadMemoryBytes` declare separate metadata-probe and complete decode
peaks. The metadata allowance includes manifest bytes, parser/string copies, record/vector growth, curve copies
and validation. Decode adds bounded rounded outputs, required semantic mask-thumbnail transients and one maximum
encoded ZIP buffer; optional image thumbnails are not produced or reserved.
Opaque codec/library allocations remain in process admission headroom.

### Production project open (P1.7 F3b.3a)

Interactive project open runs the probe and capped decode as separate Io jobs under one selected host input and
history barrier. The path is copied and resolved lexically at admission. Filesystem inspection, manifest parsing,
PNG header reads, WIC decoding, asset attachment and eager semantic mask thumbnails run on workers. Probe-owned
metadata and file/library handles retire on its worker; only the small limits/version values reach completion.
The decode reservation is declared before submitting the second job. FIFO Io serialization prevents decoding
before the probe job retires; both reservation charges can coexist during submission. A refused second admission
fails locally and leaves the current document intact.

Completion validates the exact acquisition, selected ordinal, document serial and generation before replacement.
Only a validated replacement may rebind the selected batch to the new session serial and authorize its next
acquisition. An external replacement, including reused persisted IDs, or another generation change drops the
result. Cancellation retains the input, receipt, barrier and reservation until acknowledgement and suppresses
later files. Ordinary failure preserves the current document and continues later project paths; the batch retains
the first failing HRESULT even if a later load succeeds. Each failed file posts one localized notice. Unsupported
version formatting runs in the posted notice handler using the file's version, including zero, outside completion.
Successful replacement resets history/targets and uses the captured path and Fit policy. Decode resumes only
after admitted source/history capture. Its worker constructs complete replacement history, selected ids and
retirement owners; completion checks file-operation session authority, exact history epoch and selected acquisition,
then swaps document/history/session/path owners without constructing replacement history. Manifest active-id
validation remains unchanged. Construction refusal preserves the previous document/path/history; replaced owners retire
off the application thread. Replacement over a live panel/stroke/transform/draft discards it only on success;
construction or IO failure preserves it. Borrowed coverage engines end on their owner before CPU draft retirement.
Metadata capture, chrome preparation and uploads remain measured P1.9 costs;
worker migration alone does not establish G1.

## Layer record

| Key | Rule |
| --- | --- |
| `id`, `name`, `isVisible`, `transform` | required; `transform` = `origin`, `size`, `rotation`, `flipX`, `flipY`, `sampling` (`Nearest`, `Smooth`, `High quality`) |
| `imageFile`, `parentID`, `isGroup`, `opacity`, `blendMode` | optional; blend names are the thirteen legacy raw values (`Color Dodge`, `Color Burn`, …) |
| `maskFile`, `maskEnabled`, `maskPlacement`, `maskLinked` | mask, its enabled flag (default true), its transform when moved apart from the layer (same shape as `transform`), and the link flag (default true) |
| `maskSourceID` | live/clipping source UUID |
| `adjustment` | see below |
| `shape` | `kind` (`Rectangle`, `Ellipse`), `red`, `green`, `blue`, `cornerRadius`; written only while the layer is still a live shape (its asset is still the raster the shape drew) |

`transform.origin` and `transform.size` (and `maskPlacement`'s, the same shape) are `[x, y]` / `[width, height]`:
Swift's `CGPoint`/`CGSize` Codable overlay (legacy `LayerTransform`) encodes them as a 2-element unkeyed array, not
a `{x,y}`/`{width,height}` keyed object. The writer emits arrays; the reader also accepts the keyed-object shape
RedPrism itself wrote before this fix, so already-saved projects keep loading.

## Adjustment payload (v7)

`adjustment` mirrors the Swift `LayerAdjustment` Codable shape exactly:

| Key | Shape | Written |
| --- | --- | --- |
| `kind` | `Hue/Saturation`, `Levels`, `Curves`, `Exposure`, `Gradient Map`, `Grain` | always |
| `hue`, `saturation`, `lightness`, `colorize` | legacy HSV scalars | always |
| `levels` | `{channel, ranges:[4 × {black, gamma, white, outputBlack, outputWhite}]}` | always |
| `curves` | `{channel, channels:[4 × [{x, y}…]]}` (2–32 points, x strictly increasing 0…255) | always |
| `hsvSettings` | `{range, colorize, invertRange, adjustments, bands}` | only when present |
| `exposureSettings` | `{exposure −20…20, offset −0.5…0.5, gamma 0.01…9.99}` | only when present |
| `gradientMapSettings` | `{shadows{red,green,blue}, highlights{…}, reversed}` (components 0…1) | only when present |
| `grainSettings` | `{amount 0…100, size 0.5…20, roughness 0…100, seed uint32}` | only when present |

`adjustments` and `bands` are Swift dictionaries keyed by `ColorRange` (`Master`, `Reds`, `Yellows`, `Greens`,
`Cyans`, `Blues`, `Magentas`). Swift encodes a dictionary keyed by an enum that is not `CodingKeyRepresentable` as
an **alternating key/value array** (`["Reds", {…}, "Master", {…}]`), in unspecified order. RedPrism writes that
array form in fixed range order and reads either the array or a plain object keyed by range name. Optional payloads
are omitted when absent so a Levels record saves exactly as before those settings existed (legacy
`settingsSaveAndOlderAdjustmentsStillOpen`). A record missing `levels` or `curves` reads with defaults; the Mac app
requires both, and RedPrism always writes them. Every payload is validated by `LayerAdjustment::IsValid` on read.

## Limits

Same as `ValidateDocument`: 30 000 px/side, 100 M image pixels + 100 M mask pixels, 10 000 layers, 4 MiB manifest,
512 MiB encoded asset, 64 group ancestors, 256 live-mask path.

## Status codes

`Ok`, `Invalid`, `UnsupportedVersion`, `TooLarge`, `MissingImage`, `Encode`, `Cancelled`.

## Worker cancellation and publication

Load/save/import/export accept optional cancellation. File and ZIP member reads, staged writes and codec IO
check between 64 KiB bands; parse/manifest construction and asset loops check between records. libzip close
receives a cancellation callback whose token outlives archive close/discard. OS/codec/parser calls already in
flight are not guaranteed interruptible. A cancelled load exposes no document, active id or partial pixels;
cancelled import restores its original document dimensions, active id and layers. Unsupported-version results
retain the declared version for error reporting. Allocating APIs propagate bad_alloc to the worker runner.

The optional bounded worker observer reports preparation/publication stages, completed assets or archive
percentage, and immediate Win32 failures where reported. Its context is borrowed until return; callbacks must
not throw, wait or access UI. Stop before the final publication check preserves the destination and unrelated
temporary siblings. Once exploded rotation begins, finish or recover it without treating a later stop as a
rollback. It reports distinct destination-to-backup, staging-to-destination, rollback, backup-cleanup and
staging-cleanup failures with the immediate operation error. Successful file replacement/directory publication
reports `Published` and `Ok` even when stop arrives afterward. A rollback failure keeps the old tree under `.old`
when it cannot be restored to the destination; no `Published` event is emitted. A post-publication cleanup failure
leaves the published result successful and the old backup available. Staging cleanup failures are reported while
preserving the save's original status. These events do not broaden cleanup beyond the exact owned staging path or
reserved `.old` path.
Host writer jobs must separately retain this external publication fact, because scheduler cancellation can
still override their generic Finished outcome. Mark only the revision actually published.

### Production project save worker (P1.7 F4b.1)

The host project-save route snapshots the settled immutable document (without selection outline data), active layer,
resolved output path and exact history revision before submitting one Io job. The job owns those values through
completion and calls the existing `SaveProject` publication path off the UI thread. It never borrows the session,
window, chrome or GPU state. The host receipt and input ordinal remain pending, and younger document mutations
remain ordered behind the selected save, through worker acknowledgement and owner completion. Completion revalidates
the history-work stamp, document serial and active ordinal before applying its result.

The worker reports publication independently from its scheduler result. On current published completion, the owner
records the captured path and marks only the captured revision saved; a later edit therefore remains modified.
Publication followed by cancellation still records the external file and captured saved revision, but cancellation
suppresses a dependent close/replacement. Cancellation before publication and failed publication preserve the old
destination and document path/revision. For a current, uncancelled lineage, returned failure and worker `bad_alloc`
fail the receipt and show the save notice. An accepted cancellation on an unpublished current lineage keeps the
receipt Cancelled/E_ABORT even when a later native I/O error is observed; that error remains a worker diagnostic
fact and cannot show another notice or acquire path/saved-revision authority. A stale lineage cannot update the
path or saved revision. Clipboard commands use admitted worker graphs and owned OS transfers; see [`IO_ImageCodecs.md`](IO_ImageCodecs.md) for publication and qualification. Project open and image import are worker graphs.

Preflight declares one whole-graph peak before owning job construction; submission reserves it before any worker
decode/encode or staged output allocation. The resolved path and worker object are
included, as are 16 MiB fixed manifest/JSON/codec scratch (including the 64 KiB cleanup buffer), per-layer records and names, per-asset names and source
metadata, one largest-asset frame allowance, and path-dependent libzip/staging names and bookkeeping. The frame
allowance is not multiplied by asset count; sparse rasters remain shared and encoded PNG vectors are streamed to
staging files rather than retained per asset. Zero, overflow, out-of-range canvas/layer/asset/path/curve inputs
refuse admission before submission. Snapshot metadata and the owned path are constructed at the explicit
recoverable admission boundary; their UI copy/release costs remain part of P1.9 measurements. The reservation survives Run and owner apply/drop; workers do not reserve
incrementally or wait for more memory. Scheduler availability remains subject to its global physical/commit
ceiling and 1 GiB headroom.

### Production utility export worker (P1.7 F4b.2)

PNG/JPEG export captures the settled immutable whole document, current token, resolved path and encoder options
under the selected input ordinal and history barrier. Selection outline data is excluded; no worker borrows the
session or chrome. `ExportReadbackMemoryBytes` adds the rounded full-canvas BGRA output to the conservative
project-save allowance. Metadata/path/curve limits and arithmetic overflow refuse admission; a canvas exceeding
the existing 100 M output pixel limit returns the Too Large notice before allocating readback pixels.
Resolved export destinations reserve 38 units for the exclusive codec sibling suffix and one terminator;
32,728 units is the maximum destination. Project save reserves its nested asset and codec suffixes too,
permitting 32,637 units. Larger resolved destinations fail before submitting or running either worker.

Owner stages prepare utility composition and acknowledge mapped tiles. Workers copy rows, then the owner
validates the last tile, acknowledges its mapping, releases utility GPU targets and resumes the same reserved
job in a detached CPU-only stage. Encoding and checked atomic publication run there. Device changes after
detachment MUST NOT restart encoding or repeat external writes. Before detachment the shared bounded GPU
restart policy applies, and no partial pixel result is published.

The worker independently retains successful publication. Cancellation before publication preserves the old
destination; cancellation afterward reports Cancelled without claiming the published file was rolled back.
The pending request, receipt, memory and barrier survive until acknowledgement. Completion checks token,
lineage, stamp and ordinal. Export leaves project path and saved revision unchanged, preserves captured
quality/resolution and destination, posts one current failure notice, and suppresses cancelled/stale dependent
actions. On confirmed Windows session end, cancellation competes atomically with export readback worker entry: if
cancellation wins, the file writer cannot begin; if the worker has entered writing, its completion event
participates in the single shared 1500 ms wait. The event acknowledges only the CPU write stage; ordinary owner
completion retains receipt and saved-state authority. `WM_ENDSESSION(FALSE)` performs no worker cancellation or
wait. Full Debug and Release actual-message controls pass; remaining configuration, contention, resource and
G1 qualification is tracked in [P1FileSessionEnd](../TestRuns/P1FileSessionEnd_2026-10-06.md),
[`UI_Windowing.md`](../UI/UI_Windowing.md#session-end) and
[`Testing_Validation.md`](../Testing/Testing_Validation.md).

## Validation

`Tests/IoTests` MUST: round-trip the hand-made exploded v7 fixture (Multiply/opacity, a placed unlinked mask, group
and child with shape, a Hue/Saturation adjustment with full HSV/levels/curves payload, a hidden clipped Grain
adjustment with exposure/gradient-map/grain payloads) through exploded and ZIP saves and prove every field
survives; prove the written manifest keeps optional payloads, always writes `levels`/`curves`, and writes the
alternating-array dictionaries; reject `imageFile` path traversal and name mismatch; reject a >4 MiB manifest;
reject version 0 and 42, corrupt JSON, wrong format/colorSpace, v1 groups, v2 blend, v3 mask, v4 clip, v5 folder
mask, v6 adjustment, invalid/unknown adjustment payloads, adjustment on group, group opacity, mask metadata without
a file, missing parent, self clip, blank name, zero-size transform, missing active layer, bad resolution; report a
missing image as `MissingImage`; import PNG/JPEG onto a document; and prove a failed save leaves the previous
archive intact; prove saved PNG ZIP entries use STORE and the manifest uses DEFLATE; load an older DEFLATE-encoded
project; prove unique save staging preserves unrelated `dest.~*` entries and cleans up its own staging; inject
exploded-save fault points for stale-backup cleanup, destination-to-backup rename, staging-to-destination rename,
rollback rename, post-publication backup cleanup and owned staging cleanup; verify exact reported errors,
destination and backup contents, truthful `Published` and `Ok`, failed and successful retries from backup-only
state and preservation of an unrelated sibling. Real filesystem controls exercise read-only cleanup failure,
nested ordinary files, a mount-point junction whose target survives, and the bounded traversal-depth refusal;
and prove a
replacement failure leaves an existing archive intact. ASan Debug is required for this parser.

`ProjectCancellationTests` adds independent literal pixels/metadata and observer-controlled stops at manifest,
decoded/encoded asset, libzip close, staged and published boundaries for ZIP/exploded forms. Before publication
it verifies unchanged destination/tree, `.old` and unrelated temporary siblings, no partial load/import and
owned-temp cleanup; after publication it verifies Ok and the new literal content. These structural tests are
not cancellation latency bounds or injected flush/device-fault qualification. Rotation faults are separate
`rotation-fault.*` controls.

## Immutable save admission projection

The layer root prepares a checked, immutable save-memory projection while its exclusive builder seals. It records per-record/name/curve validity, metadata bytes, largest source-raster footprint and asset occurrence count. Copies retain that same projection with the source root. A changed sealed root prepares its own projection; cancellation leaves the prior root and projection unchanged. Invalid serialization inputs produce a refused projection without changing general root capture semantics.

ProjectSaveMemoryBytes borrows this fixed-size projection in constant time before admission. It adds the established 16 MiB manifest/codec allowance, declared path/source bookkeeping and one largest serial codec input, using checked arithmetic. Asset occurrences remain charged independently even when they share pixels, as the scalar reference requires. This query does not traverse layers, names, curves, sparse storage or thumbnails, and does not copy or materialize their data. Complete operation input/codec/retirement admission and measured Apply costs remain separate requirements.

## Project-format identity

Manifests identify this application with `format: "com.redprism.project"`. Loading requires that exact marker;
other markers are rejected as an invalid format. Existing packages with a different marker require explicit
conversion before loading. This naming change preserves the documented version range and field semantics.

## Canonical project extension

Project archive filenames use `.prism`. Suffix detection compares it case-insensitively and derives the suffix
length from the complete extension. Missing names with that suffix route to project loading; a longer suffix
such as `.prism.png` does not. Open/Save project filters, the Save default extension, and the initial project
name use the same extension.

Renaming an existing archive changes only its filename. The ZIP layout, `com.redprism.project` marker,
supported manifest versions and image encodings are unchanged. Existing ZIP files remain discoverable by their
signature independently of their suffix. Exploded project directories retain folder saves.
