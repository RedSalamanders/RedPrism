# Image codecs (WIC)

Status: current normative product contract
Last reviewed: 2026-09-23
Owner: `Common/ImageCodecs.*`

## Scope

Windows Imaging Component is the only codec stack in C0–C7. Custom PNG/JPEG encoders and extra vcpkg codec packages
(`libpng`, `libjpeg-turbo`, `libraw`) are prohibited here.

## Project assets

Layer and mask PNGs in a `.prism` package decode to `RasterFormat::Bgra8888` (premultiplied BGRA, 32 bpp) or
`RasterFormat::Gray8` (masks). A mask PNG must already be `GUID_WICPixelFormat8bppGray` (8-bit monochrome, no alpha,
matching legacy `LayerMask.isValid`); any other pixel format is `Unreadable`, never silently desaturated into a
mask. Encode writes PNG through WIC. Thumbnails are at most **96 px** on the long side.

The WIC PNG encoder has no premultiplied-alpha pixel format: asking for `32bppPBGRA` silently grants `32bppBGRA`
(straight alpha) instead. The encoder honors whatever format `IWICBitmapFrameEncode::SetPixelFormat` actually
returns, not what was requested, and un-premultiplies premultiplied raster bytes into straight alpha before
`WritePixels` when the two differ — otherwise every semi-transparent pixel round-trips at roughly half its
brightness. Masks (`Gray8`, no alpha) are unaffected.

Encoded assets larger than **512 MiB** are `TooLarge`.

## Import

`ImportImageFile` / `ImportImageOnto` accept JPEG, PNG, TIFF, and HEIC/HEIF when WIC has a HEIF codec. Missing HEIF
support or a file that is not a HEIF container MUST return `HeifUnavailable`, `Unreadable`, or `Unsupported` — never
an uncaught HRESULT or a crash. TIFF uses the first frame.

Import (never RedPrism's own project assets, which are always straight-sRGB and untagged) applies the source's EXIF
orientation tag (274, read from `/app1/ifd/{ushort=274}`, `/ifd/{ushort=274}`, or `System.Photo.Orientation`) with
`IWICBitmapFlipRotator`, and color-manages a non-sRGB embedded profile to sRGB with `IWICColorTransform` before the
premultiplied-BGRA conversion. `ImportImageOnto` places the new layer centered on its `center` argument (a drop point
in document pixels, legacy `insert(_:centeredAt:)`) or else on the canvas, and above the active layer inside its
folder (`Document/Document_Model.md`'s session-edit insertion rule); into an empty document the image becomes the
canvas and the point does not apply. A budget failure restores the previous active layer and reports `TooLarge`,
not `Invalid`.

## Export

- PNG: flattened BGRA, document `resolution` in metadata.
- JPEG: 24 bpp BGR, quality in (0, 1], default 0.8, document resolution. A transparent pixel is flattened onto an
  opaque white matte (legacy `ImageExporter.jpeg`'s default `JPEGOptions()`), not dropped to black. Live JPEG
  preview may use the GPU later; CPU WIC is sufficient for C5/C7 export.
- Both write a temp file beside the destination, flush it, then replace the destination atomically (`ReplaceFileW`,
  falling back to `MoveFileExW`), so a crash mid-export never leaves a truncated file at the destination path.

## Status

`Ok`, `Unreadable`, `Unsupported`, `TooLarge`, `HeifUnavailable`, `Encode`.

## Validation

`Tests/IoTests` covers PNG pixel round-trip (including the premultiplied/straight-alpha conversion across a range
of alphas), 96 px thumbnails, JPEG encode (including the white-matte flatten and the legacy quality golden), mask
format rejection, and the missing-HEIF clean error path under ASan Debug.
