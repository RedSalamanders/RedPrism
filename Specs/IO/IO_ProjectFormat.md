# Project format (`.prism`)

Status: current normative product contract
Last reviewed: 2026-09-30
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

Atomic save: exploded projects write into an exclusively created, unique same-directory `dest.~<guid>` staging
directory, then rotate `dest` → `dest.old` and the staging directory → `dest`. ZIP projects write into a unique
same-directory `dest.~<guid>` file opened with `ZIP_CREATE | ZIP_EXCL`, then `ReplaceFileW` (with the existing
`MoveFileExW` fallback). Each staged manifest/asset file is flushed and closed successfully before publication;
ZIP close, flush, and handle close must all succeed before replacement. A failed staging or durability step leaves
the previous destination intact and removes only the temporary file or directory owned by that save. Existing ZIP
readers accept both stored and deflated entries. Writers store already encoded PNG assets and deflate the manifest.

Both shapes first encode assets serially into bounded files in the owned sibling staging directory. ZIP saves
use file sources for those assets through close; they retain filenames/manifest, not all encoded PNG vectors.
Staged asset/manifest files are flushed and closed before rotation or ZIP replacement. Metadata and libzip's
per-source path copies remain charged by graph admission. Serial encoding and extra ZIP staging IO are measured
tradeoffs; bounded parallel asset encoding remains tracked in P1.7.

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
enforced while attaching. A missing or undecodable asset is `MissingImage`; the current document is never replaced
by a partial load.

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
rollback. Successful file replacement/directory publication reports Ok even when stop arrives afterward.
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
destination and document path/revision. Returned failure and worker `bad_alloc` fail the receipt and show the save
notice. A stale lineage cannot update the path or saved revision. Project open, image import and clipboard processing remain synchronous pending
their later P1.7 slices.

Preflight declares one whole-graph peak before owning job construction; submission reserves it before any worker
decode/encode or staged output allocation. The resolved path and worker object are
included, as are 16 MiB fixed manifest/JSON/codec scratch, per-layer records and names, per-asset names and source
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
actions. Open/import/clipboard and additional fault/cancellation measurements remain P1.7 work.

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
project; prove unique save staging preserves unrelated `dest.~*` entries and cleans up its own staging; and prove a
replacement failure leaves an existing archive intact. ASan Debug is required for this parser.

`ProjectCancellationTests` adds independent literal pixels/metadata and observer-controlled stops at manifest,
decoded/encoded asset, libzip close, staged and published boundaries for ZIP/exploded forms. Before publication
it verifies unchanged destination/tree, `.old` and unrelated temporary siblings, no partial load/import and
owned-temp cleanup; after publication it verifies Ok and the new literal content. These structural tests are
not cancellation latency bounds or injected flush/rotation/device-fault qualification.
