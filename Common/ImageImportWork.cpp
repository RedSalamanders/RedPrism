#include "ImageImportWork.h"

#include <algorithm>
#include <string>
#include <utility>

namespace
{
void DiscardPreparedDocuments(ImageImportBatchResult& result) noexcept
{
    result.createdDocument.reset();
    result.createdActiveLayerID.reset();
    result.importedDocument.reset();
    result.importedActiveLayerID.reset();
    result.historyName.clear();
}

[[nodiscard]] bool ValidPaths(std::span<const std::wstring_view> paths) noexcept
{
    if (paths.empty() || paths.size() > kImageImportMaxPaths)
    {
        return false;
    }
    for (const std::wstring_view path : paths)
    {
        if (path.empty() || path.size() > kImageImportMaxPathUnits || path.find(L'\0') != std::wstring_view::npos)
        {
            return false;
        }
    }
    return true;
}

void RecordStatus(ImageImportBatchResult& result, ProjectIoStatus status) noexcept
{
    if (status != ProjectIoStatus::Ok && status != ProjectIoStatus::Cancelled)
    {
        result.failed = true;
        result.tooLarge = result.tooLarge || status == ProjectIoStatus::TooLarge;
    }
}

[[nodiscard]] ProjectIoStatus SummaryStatus(const ImageImportBatchResult& result) noexcept
{
    if (result.tooLarge)
    {
        return ProjectIoStatus::TooLarge;
    }
    return result.failed ? ProjectIoStatus::MissingImage : ProjectIoStatus::Ok;
}

void ReportProgress(const ImageImportBatchInput& input, std::size_t attempted, ProjectIoStatus status) noexcept
{
    if (input.progress)
    {
        input.progress(input.progressContext, attempted, status);
    }
}
} // namespace

ImageImportProbeResult ProbeImageImportBatch(std::span<const std::wstring_view> paths, CancelToken cancellation)
{
    ImageImportProbeResult result;
    if (!ValidPaths(paths))
        return result;
    result.files.reserve(paths.size());
    for (const auto pathView : paths)
    {
        if (cancellation.IsCancelled())
        {
            result.files.clear();
            result.status = ProjectIoStatus::Cancelled;
            return result;
        }
        const std::wstring path(pathView);
        ImageImportMetadata metadata;
        const auto status = ProbeImportImageFile(path.c_str(), kMaxLayerPixels, metadata, cancellation);
        if (status == ImageCodecStatus::Cancelled || cancellation.IsCancelled())
        {
            result.files.clear();
            result.status = ProjectIoStatus::Cancelled;
            return result;
        }
        const auto mapped = status == ImageCodecStatus::Ok         ? ProjectIoStatus::Ok
                            : status == ImageCodecStatus::TooLarge ? ProjectIoStatus::TooLarge
                            : status == ImageCodecStatus::Unsupported || status == ImageCodecStatus::HeifUnavailable
                                ? ProjectIoStatus::Invalid
                                : ProjectIoStatus::MissingImage;
        result.files.push_back({mapped, static_cast<int>(metadata.pixels), metadata.encodedBytes});
    }
    result.status = ProjectIoStatus::Ok; // per-file failures are retained for the ordered preparation batch
    return result;
}

ImageImportBatchResult RunImageImportBatch(const ImageImportBatchInput& input, CancelToken cancellation)
{
    ImageImportBatchResult result;
    if (!ValidPaths(input.paths) || (!input.files.empty() && input.files.size() != input.paths.size()) ||
        input.maximumAddedPixels < 0 || input.maximumAddedPixels > kMaxLayerPixels)
    {
        result.status = ProjectIoStatus::Invalid;
        return result;
    }
    if (cancellation.IsCancelled())
    {
        result.status = ProjectIoStatus::Cancelled;
        return result;
    }

    const bool createsCanvas = input.newProject || !input.document.has_value();
    const std::optional<Vec2> center = createsCanvas ? std::nullopt : input.center;
    CanvasDocument workingDocument;
    std::optional<Guid> activeLayerID;
    if (!createsCanvas)
    {
        workingDocument = input.document.value();
        activeLayerID = input.activeLayerID;
    }

    bool created = false;
    bool imported = false;
    std::size_t firstSuccessIndex = input.paths.size();
    int addedPixels = 0;
    for (std::size_t index = 0; index < input.paths.size(); ++index)
    {
        if (cancellation.IsCancelled())
        {
            result.status = ProjectIoStatus::Cancelled;
            DiscardPreparedDocuments(result);
            return result;
        }

        // Counted views are not required to be NUL-terminated. ImportImageOnto accepts a C path, so make one
        // bounded per-file copy instead of relying on the backing owner's string representation.
        const std::wstring path(input.paths[index]);
        const ImageImportFilePlan plan =
            input.files.empty() ? ImageImportFilePlan{ProjectIoStatus::Ok, kMaxLayerPixels, kMaxImageEncodedBytes}
                                : input.files[index];
        const int maximumPixels = (std::min)(plan.pixels, input.maximumAddedPixels - addedPixels);
        ProjectIoStatus status = ProjectIoStatus::Invalid;
        if (plan.status != ProjectIoStatus::Ok)
        {
            status = plan.status;
        }
        else if (plan.pixels < 1 || plan.pixels > kMaxLayerPixels || plan.encodedBytes == 0 ||
                 plan.encodedBytes > kMaxImageEncodedBytes)
        {
            status = ProjectIoStatus::TooLarge;
        }
        else if (createsCanvas && !created)
        {
            CanvasDocument initial;
            initial.id = Guid::Generate();
            std::optional<Guid> initialActive;
            status = ImportImageOnto(initial, path.c_str(), initialActive, std::nullopt, cancellation, input.observer,
                                     maximumPixels, plan.encodedBytes);
            if (status == ProjectIoStatus::Ok)
            {
                result.createdDocument = std::move(initial);
                result.createdActiveLayerID = initialActive;
                created = true;
                firstSuccessIndex = index;
                addedPixels += result.createdDocument->layers.front().asset->image.PixelCount();
                if (index + 1 < input.paths.size())
                {
                    workingDocument = result.createdDocument.value();
                    activeLayerID = result.createdActiveLayerID;
                }
            }
        }
        else
        {
            status = ImportImageOnto(workingDocument, path.c_str(), activeLayerID, center, cancellation, input.observer,
                                     maximumPixels, plan.encodedBytes);
            if (status == ProjectIoStatus::Ok)
            {
                imported = true;
                const auto layer = std::find_if(workingDocument.layers.begin(), workingDocument.layers.end(),
                                                [&](const auto& value) { return value.id == activeLayerID; });
                addedPixels += layer->asset->image.PixelCount();
            }
        }

        ++result.attemptedPaths;
        RecordStatus(result, status);
        ReportProgress(input, result.attemptedPaths, status);
        if (status == ProjectIoStatus::Cancelled || cancellation.IsCancelled())
        {
            result.status = ProjectIoStatus::Cancelled;
            DiscardPreparedDocuments(result);
            return result;
        }
    }

    if (imported)
    {
        result.importedDocument = std::move(workingDocument);
        result.importedActiveLayerID = activeLayerID;
        const std::size_t remainingPaths =
            createsCanvas ? input.paths.size() - firstSuccessIndex - 1 : input.paths.size();
        result.historyName = remainingPaths > 1 ? L"Import Images" : L"Import Image";
    }
    result.status = SummaryStatus(result);
    if (cancellation.IsCancelled())
    {
        result.status = ProjectIoStatus::Cancelled;
        DiscardPreparedDocuments(result);
    }
    return result;
}
