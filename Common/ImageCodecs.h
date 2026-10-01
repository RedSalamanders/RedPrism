#pragma once

#include "Document.h"
#include "Executor.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class ImageCodecStatus
{
    Ok,
    Unreadable,
    Unsupported,
    TooLarge,
    HeifUnavailable,
    Encode,
    Cancelled,
};

struct DecodedImage
{
    Raster image;
    Raster thumbnail;
    std::wstring name;
};

inline constexpr std::uint64_t kMaxImageEncodedBytes = 512ull * 1024ull * 1024ull;

struct ImageImportMetadata final
{
    int rawWidth = 0;
    int rawHeight = 0;
    std::uint64_t pixels = 0;
    std::uint64_t encodedBytes = 0;
};

struct JpegExportOptions
{
    double quality = 0.8;
    double resolution = 72;
};

[[nodiscard]] ImageCodecStatus DecodeProjectPng(const std::uint8_t* bytes, std::size_t byteCount, bool mask,
                                                int remainingPixels, DecodedImage& decoded,
                                                CancelToken cancellation = {});
// Memory PNG frame-zero metadata only; no pixel conversion or thumbnails, and empty output on every failure.
[[nodiscard]] ImageCodecStatus ProbeProjectPng(const std::uint8_t* bytes, std::size_t byteCount,
                                               std::int64_t remainingPixels, ImageImportMetadata& metadata,
                                               CancelToken cancellation = {});
[[nodiscard]] ImageCodecStatus DecodeProjectPngFile(const wchar_t* path, bool mask, int remainingPixels,
                                                    DecodedImage& decoded, CancelToken cancellation = {},
                                                    std::size_t maximumEncodedBytes = 512u * 1024u * 1024u);
[[nodiscard]] ImageCodecStatus ImportImageFile(const wchar_t* path, int remainingPixels, DecodedImage& decoded,
                                               CancelToken cancellation = {},
                                               std::uint64_t maximumEncodedBytes = kMaxImageEncodedBytes);
// Frame-zero metadata only: no pixel conversion/allocation. Output is empty on every failure.
[[nodiscard]] ImageCodecStatus ProbeImportImageFile(const wchar_t* path, std::int64_t remainingPixels,
                                                    ImageImportMetadata& metadata, CancelToken cancellation = {});
// Enforced private output stream ceiling; whole graph admission also charges the exact returned vector and codec
// workspace.
[[nodiscard]] std::uint64_t EncodeMemoryCapacity(const Raster& image) noexcept;
[[nodiscard]] ImageCodecStatus EncodePng(const Raster& image, double resolution, std::vector<std::uint8_t>& bytes,
                                         CancelToken cancellation = {},
                                         std::uint64_t maximumEncodedBytes = kMaxImageEncodedBytes);
[[nodiscard]] ImageCodecStatus EncodePngFile(const Raster& image, double resolution, const wchar_t* path,
                                             CancelToken cancellation = {});
[[nodiscard]] ImageCodecStatus EncodeJpeg(const Raster& image, const JpegExportOptions& options,
                                          std::vector<std::uint8_t>& bytes, CancelToken cancellation = {},
                                          std::uint64_t maximumEncodedBytes = kMaxImageEncodedBytes);
[[nodiscard]] ImageCodecStatus EncodeJpegFile(const Raster& image, const JpegExportOptions& options,
                                              const wchar_t* path, CancelToken cancellation = {});
[[nodiscard]] bool IsHeifContainer(std::wstring_view path) noexcept;
// Writes `bytes` to `path` (replacing any existing file) and flushes it to disk before returning, so the caller's
// own rename or replace of `path` never races an unflushed write. Callers that need the write itself to be atomic
// write to a temp path with this function and then rename/replace `path` with it (ImageCodecs' own WriteAllBytes,
// ProjectStore's SaveExploded/SaveZip); this function alone does not do that swap.
[[nodiscard]] bool WriteFileBytesToDisk(const wchar_t* path, const std::uint8_t* bytes, std::size_t size) noexcept;
