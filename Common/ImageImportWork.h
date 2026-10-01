#pragma once

#include "Executor.h"
#include "ProjectStore.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Keep these standard-only bounds aligned with HostInputBudget in
// RedPrism/ContinuationRequests.h. The host still performs the full retained-capacity admission.
inline constexpr std::size_t kImageImportMaxPaths = 256;
inline constexpr std::size_t kImageImportMaxPathUnits = 32767;

struct ImageImportFilePlan final
{
    ProjectIoStatus status = ProjectIoStatus::Invalid;
    int pixels = 0;
    std::uint64_t encodedBytes = 0;
};

struct ImageImportProbeResult final
{
    ProjectIoStatus status = ProjectIoStatus::Invalid;
    std::vector<ImageImportFilePlan> files;
};

[[nodiscard]] ImageImportProbeResult ProbeImageImportBatch(std::span<const std::wstring_view> paths,
                                                           CancelToken cancellation = {});

// Complete additional peaks for the two independently admitted graphs. Source raster/selection owners are shared.
// Opaque WIC allocations retain global process headroom; these are declared bounds, not measured exact footprints.
[[nodiscard]] inline std::uint64_t ImageImportMetadataMemoryBytes(std::span<const std::wstring_view> paths) noexcept
{
    if (paths.empty() || paths.size() > kImageImportMaxPaths)
        return 0;
    std::uint64_t bytes =
        2ull * 1024 * 1024 + paths.size() * (sizeof(std::wstring) * 2 + sizeof(ImageImportFilePlan) * 2);
    for (const auto path : paths)
    {
        if (path.empty() || path.size() > kImageImportMaxPathUnits)
            return 0;
        bytes += (static_cast<std::uint64_t>(path.size()) + 1) * sizeof(wchar_t) * 8;
    }
    return bytes;
}

[[nodiscard]] inline std::uint64_t ImageImportMemoryBytes(std::span<const std::wstring_view> paths,
                                                          std::span<const ImageImportFilePlan> files,
                                                          const CanvasDocument* source, int& addedPixelLimit) noexcept
{
    addedPixelLimit = 0;
    std::uint64_t bytes = ImageImportMetadataMemoryBytes(paths);
    if (bytes == 0 || files.size() != paths.size() ||
        (source && source->layers.size() > static_cast<std::size_t>(kMaxLayerCount)))
        return 0;
    const auto add = [&](std::uint64_t value) noexcept
    {
        if (value > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += value;
        return true;
    };
    if (source)
        for (const auto& layer : source->layers)
        {
            // Eight record/name/curve copies cover input, preparation, baseline/final and owner history transients.
            std::uint64_t units = layer.name.size();
            if (layer.asset && layer.asset->name.size() > (std::numeric_limits<std::uint64_t>::max)() - units)
                return 0;
            if (layer.asset)
                units += layer.asset->name.size();
            if (layer.mask)
            {
                if (layer.mask->asset.name.size() > (std::numeric_limits<std::uint64_t>::max)() - units)
                    return 0;
                units += layer.mask->asset.name.size();
            }
            if (units > (std::numeric_limits<std::uint64_t>::max)() / (16 * sizeof(wchar_t)) ||
                !add(units * 16 * sizeof(wchar_t)) || !add(8ull * (sizeof(ImageLayer) + 4096)))
                return 0;
            if (layer.adjustment)
                for (const auto& channel : layer.adjustment->curves.channels)
                    if (channel.size() > 32)
                        return 0;
        }
    std::uint64_t pixels = 0;
    std::uint64_t largest = 0;
    for (const auto& file : files)
    {
        if (file.status != ProjectIoStatus::Ok)
        {
            if (file.pixels != 0 || file.encodedBytes != 0 || file.status == ProjectIoStatus::Cancelled)
                return 0;
            continue;
        }
        if (file.pixels < 1 || file.pixels > kMaxLayerPixels || file.encodedBytes == 0 ||
            file.encodedBytes > kMaxImageEncodedBytes)
            return 0;
        pixels = (std::min)(static_cast<std::uint64_t>(kMaxLayerPixels), pixels + file.pixels);
        largest = (std::max)(largest, static_cast<std::uint64_t>(file.pixels));
    }
    addedPixelLimit = static_cast<int>(pixels);
    // Rounded final outputs, one serial codec's frame allowance, transient thumbnails and new document records.
    return add(16ull * 1024 * 1024) && add(8 * pixels + 4 * largest) &&
                   add(paths.size() * (8ull * (sizeof(ImageLayer) + 4096) + 2ull * 96 * 96 * 4 + 256))
               ? bytes
               : 0;
}

struct ImageImportBatchInput final
{
    // A value snapshot copied into a private working document by RunImageImportBatch. Rasters remain shared handles.
    std::optional<CanvasDocument> document;
    std::optional<Guid> activeLayerID;
    // Views must remain valid for the duration of RunImageImportBatch. Each path is counted and may not contain NUL.
    std::span<const std::wstring_view> paths;
    bool newProject = false;
    std::optional<Vec2> center;
    void (*progress)(void*, std::size_t, ProjectIoStatus) noexcept = nullptr;
    void* progressContext = nullptr;
    // Borrowed only for this invocation; forwarded to every decode. The callback must not throw, wait or touch UI.
    ProjectIoObserver observer{};
    // Optional enforced inspection ceilings, one per path. Failed probes keep their original status and skip decode.
    std::span<const ImageImportFilePlan> files;
    int maximumAddedPixels = kMaxLayerPixels;
};

struct ImageImportBatchResult final
{
    ProjectIoStatus status = ProjectIoStatus::Invalid;
    bool failed = false;
    bool tooLarge = false;
    std::size_t attemptedPaths = 0;

    // When a new canvas is requested, this is the first successfully imported image and its active layer. The host
    // applies it as the document-replacement/saved-baseline boundary before considering additional imports.
    std::optional<CanvasDocument> createdDocument;
    std::optional<Guid> createdActiveLayerID;

    // Existing-document imports and additional imports into a new canvas share this result. It is present only if
    // at least one import beyond the creation baseline succeeded (or an existing document changed).
    std::optional<CanvasDocument> importedDocument;
    std::optional<Guid> importedActiveLayerID;
    std::wstring historyName;
};

// CPU and codec work only. This helper never reaches DocumentSession, Application, UI or a GPU owner.
// Allocation failures propagate to the enclosing job runner's explicit bad_alloc boundary.
[[nodiscard]] ImageImportBatchResult RunImageImportBatch(const ImageImportBatchInput& input,
                                                         CancelToken cancellation = {});
