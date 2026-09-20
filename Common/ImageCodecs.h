#pragma once

#include "Document.h"

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
};

struct DecodedImage
{
    Raster image;
    Raster thumbnail;
    std::wstring name;
};

struct JpegExportOptions
{
    double quality = 0.8;
    double resolution = 72;
};

[[nodiscard]] ImageCodecStatus DecodeProjectPng(const std::uint8_t* bytes, std::size_t byteCount, bool mask,
                                                int remainingPixels, DecodedImage& decoded) noexcept;
[[nodiscard]] ImageCodecStatus DecodeProjectPngFile(const wchar_t* path, bool mask, int remainingPixels,
                                                    DecodedImage& decoded) noexcept;
[[nodiscard]] ImageCodecStatus ImportImageFile(const wchar_t* path, int remainingPixels,
                                               DecodedImage& decoded) noexcept;
[[nodiscard]] ImageCodecStatus EncodePng(const Raster& image, double resolution,
                                         std::vector<std::uint8_t>& bytes) noexcept;
[[nodiscard]] ImageCodecStatus EncodePngFile(const Raster& image, double resolution, const wchar_t* path) noexcept;
[[nodiscard]] ImageCodecStatus EncodeJpeg(const Raster& image, const JpegExportOptions& options,
                                          std::vector<std::uint8_t>& bytes) noexcept;
[[nodiscard]] ImageCodecStatus EncodeJpegFile(const Raster& image, const JpegExportOptions& options,
                                              const wchar_t* path) noexcept;
[[nodiscard]] bool IsHeifContainer(std::wstring_view path) noexcept;
