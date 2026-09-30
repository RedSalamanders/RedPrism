# Project format (`.prism`)

Status: current normative product contract
Last reviewed: 2026-09-21
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

`Ok`, `Invalid`, `UnsupportedVersion`, `TooLarge`, `MissingImage`, `Encode`.

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
