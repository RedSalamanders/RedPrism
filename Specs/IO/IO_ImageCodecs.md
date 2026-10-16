# Image codecs (WIC)

Status: current normative product contract
Last reviewed: 2026-10-14
Owner: `Common/ImageCodecs.*`, `Common/ClipboardDibWork.*`

## Scope

Windows Imaging Component is the only codec stack in C0–C7. Custom PNG/JPEG encoders and extra vcpkg codec packages
(`libpng`, `libjpeg-turbo`, `libraw`) are prohibited here.

## Project assets

Layer and mask PNGs in a `.prism` package decode to `RasterFormat::Bgra8888` (premultiplied BGRA, 32 bpp) or
`RasterFormat::Gray8` (masks). A mask PNG must already be `GUID_WICPixelFormat8bppGray` (8-bit monochrome, no alpha,
matching legacy `LayerMask.isValid`); any other pixel format is `Unreadable`, never silently desaturated into a
mask. Encode writes PNG through WIC. A successful image decode/import leaves `DecodedImage.thumbnail` empty;
image thumbnails are optional derived state and an explicit `Raster::MakeThumbnail` derivation is at most **96 px**
on the long side. Mask decode still eagerly produces its semantic `Gray8` thumbnail.

The WIC PNG encoder has no premultiplied-alpha pixel format: asking for `32bppPBGRA` silently grants `32bppBGRA`
(straight alpha) instead. The encoder honors whatever format `IWICBitmapFrameEncode::SetPixelFormat` actually
returns, not what was requested, and un-premultiplies premultiplied raster bytes into straight alpha before
`WritePixels` when the two differ — otherwise every semi-transparent pixel round-trips at roughly half its
brightness. Masks (`Gray8`, no alpha) are unaffected.

Encoded assets larger than **512 MiB** are `TooLarge`. Negative remaining pixel budgets are `TooLarge`;
every decoded frame independently obeys the hard 100 M pixel and 30,000 px side limits before output allocation.
The project-file decoder additionally accepts a lower caller encoded-byte ceiling, checked before constructing
WIC decode interfaces. Project attachment passes the current remaining per-kind aggregate pixel ceiling;
frame dimensions must fit before output allocation. A lower ceiling does not change accepted pixel conversion.

## Bounded memory encoding (P1.7 F5b.2b.1)

Memory `EncodePng`/`EncodeJpeg` enforce `min(512 MiB, 4 MiB + 2 * logical pixel bytes)` through
`EncodeMemoryCapacity`; an empty raster has capacity zero. A caller may supply a smaller encoded-byte ceiling.
The smallest limit reaches `CodecMemoryStream` before growth. Valid zero-ceiling or exceeded-ceiling encoding
returns `TooLarge`; every failed/cancelled encode leaves the public byte vector empty. A larger caller ceiling
cannot increase the derived/hard limit. Pixel formats, premultiplication, JPEG white matte, resolution and
existing numeric expectations are unchanged. File forms retain their separate direct checked file destination.

The private stream reserves bounded virtual address space once and commits pages lazily, at most 64 KiB per
native commit call. Reads/writes/stale-gap clearing and final vector copying check cancellation between 64 KiB
bands. Newly committed pages already contain zeros, avoiding a second clearing pass. Shrink/regrow clears stale
committed bytes; cursor/size arithmetic and cap refusal precede growth. A sticky cap-refusal flag prevents
publication even if a codec ignores a refused stream call. Borrow final bytes only with a retained COM owner
and after successful encoder completion; no borrowed view escapes. COM stream methods allocate no C++ storage
and allow no C++ exception; explicit noexcept creation admission translates only bad_alloc to E_OUTOFMEMORY.

Whole host admission additionally charges stream/vector overlap, source/document/history roots, row/conversion
buffers, movable blocks, receipt/hash owners, opaque codec workspace and aggregate retirement. The sink cap
alone does not bound WIC workspace. Production clipboard jobs use whole-graph admission and this capped worker
encoding; complete opaque-workspace/headroom and measured UI qualification remain F5d/G1 work. Cancellation
cannot interrupt an in-flight OS/WIC call.
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

Before starting a decoder, import checks available layer capacity and subtracts every existing image raster from
the aggregate image budget using checked wide arithmetic. It passes only the remainder to WIC's frame-dimension
guard; a full document cannot decode one extra full image and reject it afterward.

Import inspection reads frame-zero container/dimension metadata without requesting pixels, conversion, orientation,
ICC transforms or thumbnails. Its output is empty on every failure. Both inspection and file decode open one owned
read-only `CodecFileStream`, check that handle's encoded length before WIC decoder construction, and deny normal
write/delete sharing for the decoder lifetime. File project PNG decode uses the same opened-stream ceiling in
addition to its existing project-asset attribute guard. A lower caller encoded ceiling is enforced on the decoder's
actual source. Cancellation is checked between calls and 64 KiB reads; it cannot interrupt a synchronous OS/WIC call.

`RunImageImportBatch` is detached CPU/codec work over a value snapshot and counted paths. A new canvas retains
its first successful image as a separate saved-baseline result; further successes produce one named import result.
The new canvas ignores the batch drop point for all images. Existing-canvas imports retain grouping and placement.
Failed files before the first success do not stop later files; other file failures retain prepared successes and
report batch failure, with the history name based on attempted remaining path count. Cancellation clears both
prepared documents/active ids. Preparation never mutates the source document or shared raster bytes. Batch
preparation may consume per-path inspection plans and an aggregate added-pixel ceiling. Reopened sources cannot
exceed those encoded/frame/output ceilings before allocation; changed bytes within the ceilings may still succeed.
Cached failed inspection statuses retain their original order and skip decoding even if the file later appears.
Preparation cancellation clears both baseline/final documents. Production host admission/application uses two
separately admitted Io jobs under one selected input; memory, ordering and owner work are specified in
`Core_PerformanceAndResources.md` and `UI_ApplicationChrome.md`. Clipboard commands use the separately
admitted production graphs described below.

## Export

- PNG: flattened BGRA, document `resolution` in metadata.
- JPEG: 24 bpp BGR, quality in (0, 1], default 0.8, document resolution. A transparent pixel is flattened onto an
  opaque white matte (legacy `ImageExporter.jpeg`'s default `JPEGOptions()`), not dropped to black. Live JPEG
  preview may use the GPU later; CPU WIC is sufficient for C5/C7 export.
- Both write a uniquely named, exclusively created temp file beside the destination, check exact writes,
  flush and close it, then replace the destination atomically (`ReplaceFileW`,
  falling back to `MoveFileExW`), so a crash mid-export never leaves a truncated file at the destination path.

Export selects an exact sibling using the [project staging naming rule](IO_ProjectFormat.md#win32-staging-names):
retain the destination leaf when its generated suffix fits 255 UTF-16 units; otherwise use the short GUID leaf
in that same parent. Exclusive creation and exact owned-path cleanup preserve unrelated siblings. The destination
and generated temporary each receive the appropriate Win32 API spelling; resolving an ordinary path precedes
adding an extended prefix. Already extended/device spellings remain intact. Codec caps, pixels, quality,
metadata, cancellation checkpoints and the publication boundary are unchanged.

## Detached clipboard byte preparation (P1.7 F5a)

`ProbeProjectPng` inspects a borrowed memory PNG through WIC frame-zero `GetSize`, without requesting pixels,
conversion, ICC/orientation processing or thumbnails. The source is borrowed only for that call. It applies the
512 MiB encoded cap, signed remaining budget and independent hard frame/side limits before output allocation;
non-PNG, invalid and cancelled inputs expose empty metadata. An in-flight WIC call is not interruptible.

`ClipboardDibWork` has standard-only interfaces and no clipboard, window, COM or GPU ownership. It accepts the
existing 24/32-bit BI_RGB/BI_BITFIELDS byte layouts, padded rows, bottom-up or top-down height, V4/V5 masks and
the existing BI_RGB palette offset policy. Unsupported 32-bit channel masks are invalid. Plain 32-bit BI_RGB
padding is opaque unless a V4/V5 alpha mask declares alpha. Signed dimensions, header/mask/palette offsets,
row bytes and total storage are checked before pixel access. The independent limits are 512 MiB encoded bytes,
100 M pixels, 30,000 px per side and the lower caller pixel budget. Negative budgets are TooLarge.

Decode fills an adopted final PixelBuffer, using `(channel * alpha + 127) / 255`. Encode emits the literal
124-byte sRGB bottom-up DIBV5 header and straight BGRA, with `(channel * 255 + alpha / 2) / alpha`, clamped to 255;
zero alpha has zero RGB. Sparse rows are read into the final output vector without source materialization or a
second full source frame. Cancellation is checked between rows and before publication; an active scalar row is
not interruptible. Invalid/cancelled results expose no metadata, raster or byte vector. Explicit PixelBuffer
allocation failure reports OutOfMemory; allocating routines otherwise propagate bad_alloc to the worker boundary.
Production clipboard transfer and host graphs consume these preparation routines. Host session-end assertions
cover the current cancellation/publication boundary, but complete contention, resource and measured UI
qualification remains F5d/G1 work. Full Debug r278 and Release r280 pass the repaired publication-counter
oracle; the earlier Release r276 failure remains preserved in
[`Testing_Validation.md`](../Testing/Testing_Validation.md). Identical-source ASan and analysis qualification
are recorded separately in [P1FileSessionEnd](../TestRuns/P1FileSessionEnd_2026-10-06.md).

## Worker clipboard transfer preparation (P1.7 F5b.1)

`ClipboardTransfer` seals the outgoing source sequence when encoding starts. Fully prepared PNG and DIBV5
movable global blocks exist before opening/emptying the clipboard. A one-attempt private worker adapter checks
that sequence while open before EmptyClipboard. Busy, stale and pre-empty cancellation retain private blocks;
successful Empty is irreversible. Thereafter it attempts both formats despite late cancellation, transfers only
successful blocks to Windows, reports exact empty/PNG/DIB/native-error truth and consumes its input. An emptied
attempt is never repeated. The returned sequence is observed after the last placement while open; it is not a
guarantee about a later observation after close or owner destruction. Private-origin Paste authority remains F5b.2.

Incoming inspection scopes borrowed handles/locks within one open interval and checks encoded/pixel/side caps
before large owned allocation. PNG and the available DIB are inspected independently. Copy reopens at exactly
that sequence and rechecks sizes/metadata before copying in bounded chunks. No borrowed handle/pointer escapes
CloseClipboard. Native PNG read failure permits a separately inspected valid DIB; stale metadata/size/sequence
and cancellation are terminal. Owned bytes decode after closing, with PNG priority and DIB fallback. Wide caller
budgets retain the independent 100 M decode cap; cancelled/failed public outputs are empty.

This boundary owns no live session or Application window and performs no retry. Its partial memory report covers
owned bytes and final pixels only; complete codec, capture/history, insertion and retirement admission belongs
to the Application graph. Production Copy/Cut/Paste/Copy Merged jobs and metadata-only New Canvas prefill
use this adapter with whole-graph admission and acknowledged event-backed retries. The shared owner
event/deadline framework is defined in Core_Architecture; actual jobs retain one ticket/reservation and
allow six total acquisitions (shared between Inspect and Copy for Paste). An emptied publication must never
be retried after `EmptyClipboard`; a pre-Empty Busy result may consume another acquisition within the six-attempt
job limit. Current host controls permit those pre-Empty retries and compare the final acquisition count with the
count captured at Empty, proving there was no post-Empty retry. The earlier 48-control clipboard session-end
source passed in Debug r275; Release r276 failed its combined publication-counter assertion. The repaired oracle
reports individual counters and checks the irreversible boundary; production retry behavior is unchanged.
Its fresh qualification is recorded in [P1ClipboardSessionEnd](../TestRuns/P1ClipboardSessionEnd_2026-10-06.md).
Full F5d/session-end and G1 qualification remains open.

### Registered receipt and owned payload binding (P1.7 F5b.2a)

An optional immediate `RedPrism.ClipboardReceipt.v1` registered format carries exactly 40 bytes: `RPCLIP01`,
little-endian version 1, reserved zero, a nonzero 16-byte session id and nonzero little-endian 64-bit private copy
serial. Invalid packet input clears its decoded value. This is content provenance, not security authentication.
The receipt block is prepared before Empty and placed first. Receipt placement failure records an emptied
failure, places no image formats and consumes the attempt. After successful receipt placement, late cancellation
still finishes both image attempts. Only transferred global blocks become Windows-owned.

The encoding worker retains local immutable sizes/SHA-256 fingerprints of the complete exposed PNG/DIB blocks.
Any allocation padding is initialized to zero and included in the digest. Copy reads the receipt and owned image
bytes within the same inspected-sequence open scope. Missing/invalid receipt does not prevent foreign image
decode. After CloseClipboard, worker hashing compares current private session/serial, receipt and every copied
format; at least one copied image format must match. One altered format refuses private origin even when the
preferred PNG remains valid. Identical receipt and payload copied back at a changed sequence retain content
origin. No later sampled sequence or global memory address supplies this authority.

Production Copy/Cut/Paste and Copy Merged consume this binding under their selected history ordinal;
private application, owned encoding/transfer, decode, acknowledged retries and aggregate worker retirement
are specified in Document_Tools and Core_Architecture. The registered host matrix includes command,
prefill, delayed-render and publication-fault controls
([record](../TestRuns/ClipboardHost_2026-10-01.md)). It does not close complete F5d/session-end or measured
UI/digest/G1 costs.

## Status

`Ok`, `Unreadable`, `Unsupported`, `TooLarge`, `HeifUnavailable`, `Encode`, `Cancelled`.

## Worker preparation and cancellation

Decode, import and encode accept an optional `CancelToken`. Cancelled decode/import clears the entire
`DecodedImage`; cancelled memory encode leaves its output byte vector empty. Decoded pixels and eager semantic
mask thumbnails are published only after successful preparation and a final cancellation check; image decode/import
does not produce an image thumbnail. Allocating codec routines
propagate `std::bad_alloc` to the owning worker runner; their public interfaces are not `noexcept`.

WIC pixel decode and encode operate in bands of at most 64 KiB, or one row when a row is larger. Decode fills
the final adopted `PixelBuffer` directly. Encode reads each band through `ReadRasterRegionForWork`, including
sparse rasters, and MUST NOT materialize shared storage. Conversion to PNG straight alpha and JPEG white matte
preserves the existing integer rounding. The granted WIC pixel format is checked before writing.

Cancellation is checked between bands, before and after codec commits and before destination publication.
A WIC COM call, metadata/profile/orientation preparation, file open, flush, close or rename already in progress
cannot be interrupted by these checkpoints; band size is a structural work bound, not a wall-clock guarantee.
Export writes use 64 KiB chunks and clean up only their own temporary. Cancellation acknowledged before
replacement preserves the previous destination. Successful replacement is the external publication boundary:
the operation reports success once published, even if a stop arrives afterward. Parallel asset IO and host
worker migration remain tracked separately by P1.7.

File PNG/JPEG encodes use the private `CodecFileStream` destination directly. Its exclusive file has a hard
512 MiB cap: Write/Seek/SetSize reject exceeding the cap before changing the file or cursor. No whole encoded
HGLOBAL or output vector is retained by file encoding. Memory encoding keeps its existing output API. The
single calling worker owns stream cursor operations; only COM reference counts may be accessed concurrently.
Commit checks flush; checked close transfers and checks the handle close before publication. WIC internal
codec allocations remain part of whole-graph admission/headroom. This reduces encoded-buffer retention at the
cost of disk staging; it is not a claim of faster encoding.

## Validation

`Tests/IoTests` covers PNG pixel round-trip (including the premultiplied/straight-alpha conversion across a range
of alphas), explicit image-thumbnail derivation at the 96 px limit, absent image thumbnails on successful image
decode, eager `Gray8` mask thumbnails with literal semantic edge bytes, JPEG encode (including the white-matte
flatten and the legacy quality golden), mask format rejection, and the missing-HEIF clean error path under ASan
Debug. `ImageCodecWorkTests` adds independent
literal premultiplied PNG pixels, sparse Gray/BGRA multi-band round trips, sparse JPEG bands crossing an alpha
stripe (quality 1, 12/255 RGB tolerance away from color edges), unchanged sparse source storage, precancelled
output clearing, distinct existing export preservation and owned-temp cleanup after replacement failure.
`CodecFileStreamTests` proves hard-cap refusal without partial output, literal write/overwrite/read and retained
prefix after resize, EOF/count semantics, cursor preservation, checked/closed state, cancellation, shared COM
identity/lifetime and CREATE_NEW collision preservation. Extended bytes are not assumed to be zero.

Read-stream cases require literal bytes, exact/lower caps, unchanged source/cursor after forbidden mutations,
shared readers and exact write/delete sharing errors, read-only Stat, closure and cancellation between reads.

`ClipboardDibWorkTests` uses independently authored literal DIB headers/rows and expected premultiplied bytes:
RGB padding, direction, V4/V5 alpha, bitfields/palette offsets, malformed/truncated layouts, signed extrema,
hard/lower/exact budgets, cancellation, literal DIBV5 output and unchanged sparse input. The encoded-size
predicate checks exactly 512 MiB and one byte over without fabricating a huge borrowed span. PNG metadata
controls use a literal 2×3 PNG, wrong/truncated/null sources, signed budgets, valid oversized IHDR and an
over-cap null input rejected before access. These tests do not establish system clipboard transfer, contention,
delayed rendering, in-kernel cancellation latency or large DIB allocation performance.
Import inspection/decoder cases use independent literal PNG/JPEG/BMP and a classic two-frame TIFF (red/green
frame zero, blue/yellow frame one); TIFF imports frame zero. Valid oversized PNG headers exercise the hard pixel
limit with a larger caller budget, sparse encoded files exercise exact/over 512 MiB, and source replacement checks
the lower opened decode ceiling. Missing HEIF remains an environment-dependent error test; successful installed
HEIF decoding and in-flight OS/codec interruption are not established by these fixtures.

Decoded frame pixels are written by `IWICBitmapSource::CopyPixels` into a `PixelBuffer` and published through
`Raster::Adopt`. Conversion, orientation, color management, tight row strides and eager mask thumbnails are
unchanged. Decode MUST fail if allocating or filling that buffer fails; partially initialized bytes are never
published. There is no second full-frame pixel copy at publication.
