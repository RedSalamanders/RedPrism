# Image codecs (WIC)

Status: current normative product contract
Last reviewed: 2026-09-20
Owner: `Common/ImageCodecs.*`

## Scope

Windows Imaging Component is the only codec stack in C0–C7. Custom PNG/JPEG encoders and extra vcpkg codec packages
(`libpng`, `libjpeg-turbo`, `libraw`) are prohibited here.

## Project assets

Layer and mask PNGs in a `.prism` package decode to `RasterFormat::Bgra8888` (premultiplied BGRA, 32 bpp) or
`RasterFormat::Gray8` (masks). Encode writes PNG through WIC. Thumbnails are at most **96 px** on the long side.

Encoded assets larger than **512 MiB** are `TooLarge`.

## Import

`ImportImageFile` / `ImportImageOnto` accept JPEG, PNG, TIFF, and HEIC/HEIF when WIC has a HEIF codec. Missing HEIF
support or a file that is not a HEIF container MUST return `HeifUnavailable`, `Unreadable`, or `Unsupported` — never
an uncaught HRESULT or a crash. TIFF uses the first frame.

## Export

- PNG: flattened BGRA, document `resolution` in metadata.
- JPEG: 24 bpp BGR (alpha stripped), quality in (0, 1], default 0.8, document resolution. Live JPEG preview may use
  the GPU later; CPU WIC is sufficient for C5/C7 export.

## Status

`Ok`, `Unreadable`, `Unsupported`, `TooLarge`, `HeifUnavailable`, `Encode`.

## Validation

`Tests/IoTests` covers PNG pixel round-trip, 96 px thumbnails, JPEG encode, and the missing-HEIF clean error path
under ASan Debug.
