#pragma once

#include "Document.h"
#include "ImageCodecs.h"

#include <optional>
#include <string>
#include <vector>

enum class ProjectIoStatus
{
    Ok,
    Invalid,
    UnsupportedVersion,
    TooLarge,
    MissingImage,
    Encode,
};

struct ProjectLoadResult
{
    ProjectIoStatus status = ProjectIoStatus::Invalid;
    CanvasDocument document{};
    std::optional<Guid> activeLayerID;
    int formatVersion = 0;
};

[[nodiscard]] bool PathLooksLikeZipProject(const wchar_t* path) noexcept;
[[nodiscard]] ProjectLoadResult LoadProject(const wchar_t* path);
[[nodiscard]] ProjectIoStatus SaveProject(const wchar_t* path, const CanvasDocument& document,
                                          const std::optional<Guid>& activeLayerID, bool zipArchive);
[[nodiscard]] ProjectIoStatus ImportImageOnto(CanvasDocument& document, const wchar_t* path,
                                              std::optional<Guid>& activeLayerID) noexcept;
[[nodiscard]] ProjectIoStatus ExportDocumentPng(const Raster& flattened, double resolution,
                                                const wchar_t* path) noexcept;
[[nodiscard]] ProjectIoStatus ExportDocumentJpeg(const Raster& flattened, const JpegExportOptions& options,
                                                 const wchar_t* path) noexcept;
