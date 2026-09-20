# Project format (`.prism`)

Status: current normative product contract
Last reviewed: 2026-09-20
Owner: `Common/ProjectStore.*`, `Common/ZipOwner.h`

## Scope

This contract owns how RedPrism loads and saves RedPrism documents. Image decode/encode is
[`IO_ImageCodecs.md`](IO_ImageCodecs.md). Document graph rules are
[`Document/Document_Model.md`](../Document/Document_Model.md).

## Package shapes

A project is either:

1. A ZIP archive whose name ends in `.prism` (Windows file, same layout as the Mac package), or
2. An exploded directory containing `manifest.json` and `images/`.

ZIP I/O uses libzip through `unique_zip` (`zip_discard` on abandon, `zip_close` only on commit). Handles are never
left raw. ZIP members are opened with `zip_source_win32w_create`. Reparse points and `..` path segments in asset names
are rejected (`ProjectIoStatus::Invalid`).

Atomic save: exploded projects write into `dest.saving`, then rotate `dest` → `dest.old` and `dest.saving` → `dest`.
ZIP projects write `path.tmp` then `ReplaceFileW`.

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
| `activeLayerID` | optional UUID |
| `layers` | bottom-to-top array, ≤ 10 000 |

`imageFile` on a layer, when present, MUST equal `<layer UUID>.png` under `images/`. `maskFile` MUST equal
`<layer UUID>.mask.png`. Other names, including `../`, are invalid.

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

Files declaring an older version MUST NOT contain newer fields. Unknown versions are `UnsupportedVersion`.

## v7 fields (`project-format.md` omitted these)

- `adjustment`: object with `kind` (`Hue/Saturation`, `Levels`, `Curves`, `Exposure`, `Gradient Map`, `Grain`) and
  the matching numeric payload. Groups and image-bearing layers MUST NOT carry an adjustment.
- `maskPlacement`: optional transform for an unlinked mask; omitted when the mask follows the layer.
- `maskLinked`: boolean, default true.
- `shape`: optional vector style on a layer (`kind`, RGB, `cornerRadius`).

## Limits

Same as `ValidateDocument`: 30 000 px/side, 100 M image pixels + 100 M mask pixels, 10 000 layers, 4 MiB manifest,
512 MiB encoded asset, 64 group ancestors, 256 live-mask path.

## Status codes

`Ok`, `Invalid`, `UnsupportedVersion`, `TooLarge`, `MissingImage`, `Encode`.

## Validation

`Tests/IoTests` MUST: round-trip the hand-made exploded v7 fixture (group, child, Multiply, Hue/Saturation);
reject `imageFile` path traversal; reject a >4 MiB manifest; load and save both exploded directories and ZIP
`.prism`. ASan Debug is required for this parser.
