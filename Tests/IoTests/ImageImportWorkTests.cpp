#include "ImageImportWorkTests.h"

#include "ImageImportWork.h"
#include "ImageImportWorkTests.h"

#include "ImageCodecs.h"
#include "PixelBuffer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s imageimportwork.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

class ScratchDirectory final
{
  public:
    ScratchDirectory()
    {
        std::error_code error;
        const std::filesystem::path parent = std::filesystem::temp_directory_path() / L"RedPrism.ImageImportWork";
        std::filesystem::create_directories(parent, error);
        if (error)
        {
            return;
        }
        _parent = std::filesystem::canonical(parent, error);
        if (error || !_parent.is_absolute())
        {
            return;
        }
        _leaf = Guid::Generate().ToString();
        _path = _parent / _leaf;
        _created = std::filesystem::create_directory(_path, error) && !error;
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    ~ScratchDirectory()
    {
        if (_created)
        {
            std::error_code error;
            const std::filesystem::path resolved = std::filesystem::canonical(_path, error);
            if (!error && resolved.is_absolute() && resolved == _path && resolved.parent_path() == _parent &&
                resolved.filename() == _leaf)
            {
                std::filesystem::remove_all(resolved, error);
            }
        }
    }

    [[nodiscard]] bool IsCreated() const noexcept
    {
        return _created;
    }

    [[nodiscard]] std::wstring Child(std::wstring_view name) const
    {
        return (_path / std::wstring(name)).wstring();
    }

  private:
    std::filesystem::path _path;
    std::filesystem::path _parent;
    std::filesystem::path _leaf;
    bool _created = false;
};

[[nodiscard]] Raster Pixel(std::uint8_t blue, std::uint8_t green, std::uint8_t red)
{
    const std::array<std::uint8_t, 4> bytes{blue, green, red, 255};
    return Raster::Make(1, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

[[nodiscard]] bool WritePixelPng(const std::wstring& path, std::uint8_t blue, std::uint8_t green, std::uint8_t red)
{
    return EncodePngFile(Pixel(blue, green, red), 72, path.c_str()) == ImageCodecStatus::Ok;
}

[[nodiscard]] bool WriteGridPng(const std::wstring& path, int width, int height)
{
    if (width < 1 || height < 1)
    {
        return false;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
            pixels[offset] = static_cast<std::uint8_t>(x * 37 + y * 11);
            pixels[offset + 1] = static_cast<std::uint8_t>(x * 13 + y * 53);
            pixels[offset + 2] = static_cast<std::uint8_t>(x * 71 + y * 29);
            pixels[offset + 3] = 255;
        }
    }
    const Raster image = Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    return image.HasStorage() && EncodePngFile(image, 72, path.c_str()) == ImageCodecStatus::Ok;
}

[[nodiscard]] std::uint64_t FileLength(const std::wstring& path) noexcept
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}

[[nodiscard]] bool HasPixel(const Raster& image, std::array<std::uint8_t, 4> expected) noexcept
{
    if (!image.HasStorage() || image.Width() != 1 || image.Height() != 1 || image.Format() != RasterFormat::Bgra8888)
    {
        return false;
    }
    const std::uint8_t* bytes = image.Data();
    return bytes && std::equal(expected.begin(), expected.end(), bytes);
}

[[nodiscard]] const ImageLayer* Layer(const CanvasDocument& document, const Guid& id) noexcept
{
    for (const ImageLayer& layer : document.layers)
    {
        if (layer.id == id)
        {
            return &layer;
        }
    }
    return nullptr;
}

void CancelAfterFirstSuccess(void* context, std::size_t completed, ProjectIoStatus status) noexcept
{
    if (completed == 1 && status == ProjectIoStatus::Ok)
    {
        (void)static_cast<CancelSource*>(context)->Cancel();
    }
}

struct DecodedAssetStop final
{
    CancelSource cancellation;
    unsigned int visited = 0;
    unsigned int stopAfter = 1;
};

void StopAtDecodedAsset(void* context, ProjectIoStage stage, unsigned int) noexcept
{
    auto& probe = *static_cast<DecodedAssetStop*>(context);
    if (stage == ProjectIoStage::AssetDecoded && ++probe.visited == probe.stopAfter)
    {
        probe.cancellation.Cancel();
    }
}

void ValidateInputBeforeDecode(const ScratchDirectory& scratch)
{
    const std::array<std::wstring_view, 0> empty{};
    const ImageImportBatchInput emptyInput{std::nullopt, std::nullopt, empty};
    const ImageImportBatchResult emptyResult = RunImageImportBatch(emptyInput);
    Expect(emptyResult.status == ProjectIoStatus::Invalid && !emptyResult.createdDocument &&
               !emptyResult.importedDocument,
           "input.empty-batch-refused");

    const std::array<std::wstring_view, 1> emptyPath{L""};
    const ImageImportBatchInput emptyPathInput{std::nullopt, std::nullopt, emptyPath};
    Expect(RunImageImportBatch(emptyPathInput).status == ProjectIoStatus::Invalid, "input.empty-path-refused");

    const std::wstring embedded = scratch.Child(L"validation.png");
    std::wstring embeddedNul = embedded;
    embeddedNul.push_back(L'\0');
    embeddedNul.append(L"ignored.png");
    const std::array<std::wstring_view, 1> embeddedPath{embeddedNul};
    const ImageImportBatchInput embeddedInput{std::nullopt, std::nullopt, embeddedPath};
    Expect(RunImageImportBatch(embeddedInput).status == ProjectIoStatus::Invalid,
           "input.embedded-nul-refused-before-open");

    const std::wstring oversized(kImageImportMaxPathUnits + 1, L'x');
    const std::array<std::wstring_view, 1> oversizedPath{oversized};
    const ImageImportBatchInput oversizedInput{std::nullopt, std::nullopt, oversizedPath};
    Expect(RunImageImportBatch(oversizedInput).status == ProjectIoStatus::Invalid, "input.path-length-budget-enforced");

    std::array<std::wstring_view, kImageImportMaxPaths + 1> tooMany{};
    tooMany.fill(L"x");
    const ImageImportBatchInput tooManyInput{std::nullopt, std::nullopt, tooMany};
    Expect(RunImageImportBatch(tooManyInput).status == ProjectIoStatus::Invalid, "input.path-count-budget-enforced");
}

void FirstFailureThenNewCanvas(const ScratchDirectory& scratch)
{
    const std::wstring missing = scratch.Child(L"first-failure-missing.png");
    const std::wstring valid = scratch.Child(L"first-failure-valid.png");
    Expect(WritePixelPng(valid, 11, 22, 33), "new.first-failure-fixture");
    const std::array<std::wstring_view, 2> paths{missing, valid};
    const ImageImportBatchInput input{std::nullopt, std::nullopt, paths, true, Vec2{99, 101}};
    const ImageImportBatchResult result = RunImageImportBatch(input);

    Expect(result.status == ProjectIoStatus::MissingImage && result.failed && !result.tooLarge,
           "new.first-failure-reported");
    Expect(result.createdDocument.has_value() && result.createdActiveLayerID.has_value() &&
               result.createdDocument.value().width == 1 && result.createdDocument.value().height == 1 &&
               result.createdDocument.value().layers.size() == 1,
           "new.first-success-establishes-canvas");
    if (result.createdDocument && result.createdActiveLayerID)
    {
        const ImageLayer* imported = Layer(result.createdDocument.value(), result.createdActiveLayerID.value());
        Expect(imported && imported->asset && HasPixel(imported->asset.value().image, {11, 22, 33, 255}) &&
                   imported->transform.origin == Vec2{0, 0},
               "new.drop-center-ignored-and-active-set");
    }
    Expect(!result.importedDocument && !result.importedActiveLayerID && result.attemptedPaths == 2,
           "new.no-additional-document-without-additional-success");
}

void PartialSuccessAndPluralName(const ScratchDirectory& scratch)
{
    const std::wstring first = scratch.Child(L"partial-first.png");
    const std::wstring missing = scratch.Child(L"partial-missing.png");
    const std::wstring second = scratch.Child(L"partial-second.png");
    Expect(WritePixelPng(first, 10, 20, 30) && WritePixelPng(second, 40, 50, 60), "partial.success-fixtures");
    const std::array<std::wstring_view, 3> paths{first, missing, second};
    const ImageImportBatchInput input{std::nullopt, std::nullopt, paths, true, Vec2{7, 9}};
    const ImageImportBatchResult result = RunImageImportBatch(input);

    Expect(result.status == ProjectIoStatus::MissingImage && result.failed && result.createdDocument &&
               result.importedDocument && result.importedActiveLayerID && result.historyName == L"Import Images",
           "partial.failure-keeps-prepared-documents-and-count-based-name");
    if (result.createdDocument && result.createdActiveLayerID && result.importedDocument &&
        result.importedActiveLayerID)
    {
        const CanvasDocument& baseline = result.createdDocument.value();
        const CanvasDocument& final = result.importedDocument.value();
        const ImageLayer* firstLayer = Layer(baseline, result.createdActiveLayerID.value());
        const ImageLayer* active = Layer(final, result.importedActiveLayerID.value());
        Expect(baseline.layers.size() == 1 && final.layers.size() == 2 && firstLayer && firstLayer->asset &&
                   HasPixel(firstLayer->asset.value().image, {10, 20, 30, 255}) && active && active->asset &&
                   HasPixel(active->asset.value().image, {40, 50, 60, 255}) && firstLayer->name == L"partial-first" &&
                   active->name == L"partial-second" && final.layers[0].id == baseline.layers[0].id &&
                   final.layers[1].id == active->id && active->transform.origin == Vec2{0, 0},
               "partial.literal-pixels-order-and-active-layer");
    }
}

[[nodiscard]] CanvasDocument ExistingDocument(const Raster& image, Guid groupID, Guid childID)
{
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 20;
    document.height = 16;

    ImageLayer group = ImageLayer::Blank(L"Folder", Vec2{20, 16});
    group.id = groupID;
    group.isGroup = true;
    document.layers.push_back(group);

    ImageLayer child = ImageLayer::Blank(L"Existing", Vec2{1, 1});
    child.id = childID;
    child.parentID = groupID;
    child.asset = RasterAsset{image, Raster{}, child.name};
    child.transform.origin = Vec2{2, 3};
    document.layers.push_back(std::move(child));
    return document;
}

void ExistingDocumentImportPreservesSourceAndPlacement(const ScratchDirectory& scratch)
{
    const std::wstring path = scratch.Child(L"existing-import.png");
    Expect(WritePixelPng(path, 71, 81, 91), "existing.fixture");
    const Raster sourcePixels = Pixel(3, 4, 5);
    const Guid groupID = Guid::Generate();
    const Guid childID = Guid::Generate();
    const CanvasDocument source = ExistingDocument(sourcePixels, groupID, childID);
    const CanvasDocument unchanged = source;
    const std::array<std::wstring_view, 1> paths{path};
    const ImageImportBatchInput input{source, childID, paths, false, Vec2{12.5, 9.5}};
    const ImageImportBatchResult result = RunImageImportBatch(input);

    Expect(result.status == ProjectIoStatus::Ok && !result.failed && !result.createdDocument &&
               result.importedDocument && result.importedActiveLayerID && result.historyName == L"Import Image",
           "existing.prepared-only-when-changed");
    if (result.importedDocument && result.importedActiveLayerID)
    {
        const CanvasDocument& final = result.importedDocument.value();
        const ImageLayer* added = Layer(final, result.importedActiveLayerID.value());
        Expect(final.width == 20 && final.height == 16 && final.layers.size() == 3 && added && added->asset &&
                   added->parentID == groupID && added->transform.origin == Vec2{12, 9} &&
                   added->name == L"existing-import" && HasPixel(added->asset.value().image, {71, 81, 91, 255}),
               "existing.group-order-center-and-active");
    }
    Expect(source == unchanged && source.layers.size() == 2 &&
               sourcePixels.Identity() == source.layers[1].asset.value().image.Identity() &&
               HasPixel(sourcePixels, {3, 4, 5, 255}),
           "existing.snapshot-and-shared-raster-unchanged");
}

void TooLargeFailureIsReportedWithoutPublishing(const ScratchDirectory& scratch)
{
    const std::wstring path = scratch.Child(L"too-large.png");
    Expect(WritePixelPng(path, 8, 9, 10), "too-large.fixture");

    CanvasDocument full;
    full.id = Guid::Generate();
    full.width = 1;
    full.height = 1;
    ImageLayer huge = ImageLayer::Blank(L"Full budget", Vec2{10000, 10000});
    huge.asset = RasterAsset{Raster::MakeSparse(10000, 10000, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0), Raster{},
                             huge.name};
    full.layers.push_back(huge);
    const std::optional<Guid> active = huge.id;
    const std::array<std::wstring_view, 1> paths{path};
    const ImageImportBatchInput input{full, active, paths};
    const ImageImportBatchResult result = RunImageImportBatch(input);

    Expect(result.status == ProjectIoStatus::TooLarge && result.failed && result.tooLarge && !result.createdDocument &&
               !result.importedDocument,
           "too-large.is-distinguished-and-unpublished");
    Expect(full.layers.size() == 1 && full.layers.front().id == huge.id &&
               !full.layers.front().asset.value().image.IsMaterialized(),
           "too-large.source-document-remains-sparse-and-unchanged");
}

void NewProjectReplacesProvidedSnapshotAndCancellationDiscardsAll(const ScratchDirectory& scratch)
{
    const std::wstring first = scratch.Child(L"new-a.png");
    const std::wstring second = scratch.Child(L"new-b.png");
    Expect(WritePixelPng(first, 101, 102, 103) && WritePixelPng(second, 111, 112, 113), "cancel.fixtures");

    const Raster priorPixels = Pixel(1, 2, 3);
    const Guid priorGroup = Guid::Generate();
    const Guid priorChild = Guid::Generate();
    const CanvasDocument prior = ExistingDocument(priorPixels, priorGroup, priorChild);
    const std::array<std::wstring_view, 2> paths{first, second};

    const ImageImportBatchInput replaceInput{prior, priorChild, paths, true, Vec2{80, 90}};
    const ImageImportBatchResult replaced = RunImageImportBatch(replaceInput);
    Expect(replaced.status == ProjectIoStatus::Ok && replaced.createdDocument && replaced.createdActiveLayerID &&
               replaced.importedDocument && replaced.importedActiveLayerID &&
               replaced.createdDocument.value().layers.size() == 1 && replaced.createdDocument.value().width == 1 &&
               replaced.createdDocument.value().height == 1 &&
               replaced.createdDocument.value().layers.front().id != priorChild &&
               replaced.createdDocument.value().layers.front().transform.origin == Vec2{0, 0} &&
               replaced.importedDocument.value().layers.size() == 2 &&
               replaced.importedDocument.value().layers.back().transform.origin == Vec2{0, 0} &&
               replaced.historyName == L"Import Image",
           "new-project-replaces-snapshot-and-ignores-center-for-all-images");

    CancelSource cancel;
    const ImageImportBatchInput input{prior, priorChild, paths, true, Vec2{80, 90}, &CancelAfterFirstSuccess, &cancel};
    const ImageImportBatchResult result = RunImageImportBatch(input, cancel.Token());

    Expect(result.status == ProjectIoStatus::Cancelled && result.attemptedPaths == 1 && !result.createdDocument &&
               !result.createdActiveLayerID && !result.importedDocument && !result.importedActiveLayerID &&
               result.historyName.empty(),
           "cancel.after-first-success-discards-baseline-and-import-result");
    Expect(prior.width == 20 && prior.height == 16 && prior.layers.size() == 2 &&
               priorPixels.Identity() == prior.layers[1].asset.value().image.Identity() &&
               HasPixel(priorPixels, {1, 2, 3, 255}),
           "cancel.source-snapshot-remains-unchanged");
}

void DecodedAssetCancellationBeforeMutation(const ScratchDirectory& scratch)
{
    const std::wstring first = scratch.Child(L"decoded-stop-a.png");
    const std::wstring second = scratch.Child(L"decoded-stop-b.png");
    Expect(WritePixelPng(first, 21, 31, 41) && WritePixelPng(second, 51, 61, 71), "asset-stop.fixtures");
    const Raster sourcePixels = Pixel(6, 7, 8);
    const Guid groupID = Guid::Generate();
    const Guid childID = Guid::Generate();
    const CanvasDocument source = ExistingDocument(sourcePixels, groupID, childID);
    const CanvasDocument unchanged = source;

    CanvasDocument direct = source;
    std::optional<Guid> directActive = childID;
    DecodedAssetStop directStop;
    const ProjectIoStatus directStatus =
        ImportImageOnto(direct, first.c_str(), directActive, Vec2{12, 14}, directStop.cancellation.Token(),
                        ProjectIoObserver{&StopAtDecodedAsset, &directStop});
    Expect(directStatus == ProjectIoStatus::Cancelled && directStop.visited == 1 && direct == source &&
               directActive == childID,
           "asset-stop.decode-checkpoint-precedes-document-mutation");

    const std::array<std::wstring_view, 2> paths{first, second};
    for (const bool newProject : std::array{true, false})
    {
        DecodedAssetStop stop;
        stop.stopAfter = 2;
        ImageImportBatchInput input{source, childID, paths, newProject, Vec2{70, 80}};
        input.observer = ProjectIoObserver{&StopAtDecodedAsset, &stop};
        const ImageImportBatchResult result = RunImageImportBatch(input, stop.cancellation.Token());
        Expect(result.status == ProjectIoStatus::Cancelled && stop.visited == 2 && result.attemptedPaths == 2 &&
                   !result.createdDocument && !result.createdActiveLayerID && !result.importedDocument &&
                   !result.importedActiveLayerID && result.historyName.empty(),
               newProject ? "asset-stop.new-baseline-and-final-discarded" : "asset-stop.existing-final-discarded");
        Expect(input.document.has_value() && input.document.value() == unchanged && source == unchanged &&
                   HasPixel(sourcePixels, {6, 7, 8, 255}),
               newProject ? "asset-stop.new-source-unchanged" : "asset-stop.existing-source-unchanged");
    }
}

void ProbeBatchRecordsPerFileMetadataAndContinuesAfterMissing(const ScratchDirectory& scratch)
{
    const std::wstring missing = scratch.Child(L"probe-missing-first.png");
    const std::wstring twoByThree = scratch.Child(L"probe-literal-2x3.png");
    const std::wstring fourByTwo = scratch.Child(L"probe-literal-4x2.png");
    Expect(WriteGridPng(twoByThree, 2, 3) && WriteGridPng(fourByTwo, 4, 2), "probe.literal-png-fixtures");
    const std::array<std::wstring_view, 3> paths{missing, twoByThree, fourByTwo};
    const ImageImportProbeResult result = ProbeImageImportBatch(paths);
    Expect(result.status == ProjectIoStatus::Ok && result.files.size() == paths.size(),
           "probe.batch-retains-per-file-results-after-failure");
    if (result.files.size() == paths.size())
    {
        Expect(result.files[0].status == ProjectIoStatus::MissingImage && result.files[0].pixels == 0 &&
                   result.files[0].encodedBytes == 0,
               "probe.first-missing-has-no-decoded-metadata");
        Expect(result.files[1].status == ProjectIoStatus::Ok && result.files[1].pixels == 6 &&
                   result.files[1].encodedBytes == FileLength(twoByThree),
               "probe.literal-2x3-pixel-count-and-exact-encoded-length");
        Expect(result.files[2].status == ProjectIoStatus::Ok && result.files[2].pixels == 8 &&
                   result.files[2].encodedBytes == FileLength(fourByTwo),
               "probe.literal-4x2-pixel-count-and-exact-encoded-length");
    }
}

void CachedMissingProbeDoesNotDecodeFileThatAppearsLater(const ScratchDirectory& scratch)
{
    const std::wstring path = scratch.Child(L"probe-missing-then-created.png");
    const std::array<std::wstring_view, 1> paths{path};
    const ImageImportProbeResult probe = ProbeImageImportBatch(paths);
    Expect(probe.status == ProjectIoStatus::Ok && probe.files.size() == 1 &&
               probe.files[0].status == ProjectIoStatus::MissingImage,
           "probe-cache.missing-status-captured");
    Expect(WritePixelPng(path, 17, 29, 43), "probe-cache.file-created-after-probe");
    if (probe.files.size() == 1)
    {
        const ImageImportBatchInput input{std::nullopt, std::nullopt, paths, true,       std::nullopt,
                                          nullptr,      nullptr,      {},    probe.files};
        const ImageImportBatchResult result = RunImageImportBatch(input);
        Expect(result.status == ProjectIoStatus::MissingImage && result.failed && !result.tooLarge &&
                   !result.createdDocument && !result.importedDocument,
               "probe-cache.late-file-remains-cached-missing-without-decode");
    }
}

void ChangedFileCannotExceedProbePixelOrEncodedCeilings(const ScratchDirectory& scratch)
{
    const std::wstring path = scratch.Child(L"probe-growth.png");
    Expect(WritePixelPng(path, 7, 19, 31), "probe-growth.initial-png");
    const std::array<std::wstring_view, 1> paths{path};
    const ImageImportProbeResult probe = ProbeImageImportBatch(paths);
    Expect(probe.status == ProjectIoStatus::Ok && probe.files.size() == 1 && probe.files[0].pixels == 1,
           "probe-growth.initial-one-pixel-plan");
    const std::uint64_t probedLength = probe.files.size() == 1 ? probe.files[0].encodedBytes : 0;
    Expect(WriteGridPng(path, 64, 64), "probe-growth.replacement-png");
    const std::uint64_t grownLength = FileLength(path);
    Expect(grownLength > probedLength, "probe-growth.encoded-length-grew-past-plan");
    if (probe.files.size() == 1)
    {
        const ImageImportBatchInput input{std::nullopt, std::nullopt, paths, true,        std::nullopt,
                                          nullptr,      nullptr,      {},    probe.files, 1};
        const ImageImportBatchResult result = RunImageImportBatch(input);
        Expect(result.status == ProjectIoStatus::TooLarge && result.failed && result.tooLarge &&
                   !result.createdDocument && !result.importedDocument,
               "probe-growth.decode-caps-refuse-grown-source-with-no-output");
    }
}

[[nodiscard]] std::array<ImageImportFilePlan, 2> ProbeTwoOnePixelFiles(const std::wstring& first,
                                                                       const std::wstring& second)
{
    (void)WritePixelPng(first, 31, 47, 59);
    (void)WritePixelPng(second, 61, 73, 89);
    const std::array<std::wstring_view, 2> paths{first, second};
    const ImageImportProbeResult probe = ProbeImageImportBatch(paths);
    if (probe.status != ProjectIoStatus::Ok || probe.files.size() != 2)
    {
        return {};
    }
    return {probe.files[0], probe.files[1]};
}

void AddedPixelLimitPreservesCreatedBaselineAndAcceptsEquality(const ScratchDirectory& scratch)
{
    const std::wstring first = scratch.Child(L"pixel-limit-first.png");
    const std::wstring second = scratch.Child(L"pixel-limit-second.png");
    const auto files = ProbeTwoOnePixelFiles(first, second);
    const std::array<std::wstring_view, 2> paths{first, second};
    Expect(files[0].status == ProjectIoStatus::Ok && files[0].pixels == 1 && files[1].status == ProjectIoStatus::Ok &&
               files[1].pixels == 1,
           "pixel-limit.two-probed-one-pixel-plans");

    const ImageImportBatchInput limitedInput{std::nullopt, std::nullopt, paths, true,  std::nullopt,
                                             nullptr,      nullptr,      {},    files, 1};
    const ImageImportBatchResult limited = RunImageImportBatch(limitedInput);
    Expect(limited.status == ProjectIoStatus::TooLarge && limited.failed && limited.tooLarge &&
               limited.createdDocument && limited.createdActiveLayerID && !limited.importedDocument,
           "pixel-limit.second-image-fails-after-preserving-created-baseline");
    if (limited.createdDocument && limited.createdActiveLayerID)
    {
        const ImageLayer* baseline = Layer(limited.createdDocument.value(), limited.createdActiveLayerID.value());
        Expect(limited.createdDocument->layers.size() == 1 && baseline && baseline->asset &&
                   HasPixel(baseline->asset->image, {31, 47, 59, 255}),
               "pixel-limit.baseline-is-complete-first-import");
    }

    const ImageImportBatchInput equalInput{std::nullopt, std::nullopt, paths, true,  std::nullopt,
                                           nullptr,      nullptr,      {},    files, 2};
    const ImageImportBatchResult equal = RunImageImportBatch(equalInput);
    Expect(equal.status == ProjectIoStatus::Ok && equal.createdDocument && equal.createdActiveLayerID &&
               equal.importedDocument && equal.importedActiveLayerID,
           "pixel-limit.aggregate-equality-accepted-with-baseline-and-import");
    if (equal.createdDocument && equal.createdActiveLayerID && equal.importedDocument && equal.importedActiveLayerID)
    {
        const ImageLayer* baseline = Layer(equal.createdDocument.value(), equal.createdActiveLayerID.value());
        const ImageLayer* imported = Layer(equal.importedDocument.value(), equal.importedActiveLayerID.value());
        Expect(equal.createdDocument->layers.size() == 1 && equal.importedDocument->layers.size() == 2 && baseline &&
                   baseline->asset && HasPixel(baseline->asset->image, {31, 47, 59, 255}) && imported &&
                   imported->asset && HasPixel(imported->asset->image, {61, 73, 89, 255}),
               "pixel-limit.equality-keeps-complete-baseline-and-second-image");
    }
}

void FailedProbeStatusSurvivesZeroPixelCeiling(const ScratchDirectory& scratch)
{
    const std::wstring path = scratch.Child(L"cached-too-large.png");
    Expect(WritePixelPng(path, 1, 2, 3), "probe-too-large.file-exists");
    const std::array<std::wstring_view, 1> paths{path};
    const std::array<ImageImportFilePlan, 1> files{{{ProjectIoStatus::TooLarge, 0, 0}}};
    const ImageImportBatchInput input{std::nullopt, std::nullopt, paths, true,  std::nullopt,
                                      nullptr,      nullptr,      {},    files, 0};
    const ImageImportBatchResult result = RunImageImportBatch(input);
    Expect(result.status == ProjectIoStatus::TooLarge && result.failed && result.tooLarge &&
               result.attemptedPaths == 1 && !result.createdDocument && !result.importedDocument,
           "probe-too-large.cached-status-before-zero-ceiling-and-no-decode-output");
}

void InvalidPlansAndCancellationClearResults(const ScratchDirectory& scratch)
{
    const std::wstring path = scratch.Child(L"invalid-plan.png");
    Expect(WritePixelPng(path, 9, 10, 11), "invalid-plan.fixture");
    const std::array<std::wstring_view, 1> paths{path};
    const std::array<ImageImportFilePlan, 1> plan{{{ProjectIoStatus::Ok, 1, FileLength(path)}}};
    const std::array<ImageImportFilePlan, 2> mismatched{{plan[0], plan[0]}};
    const ImageImportBatchInput negative{std::nullopt, std::nullopt, paths, true, std::nullopt,
                                         nullptr,      nullptr,      {},    plan, -1};
    const ImageImportBatchInput wrongLength{std::nullopt, std::nullopt, paths, true,       std::nullopt,
                                            nullptr,      nullptr,      {},    mismatched, 1};
    Expect(RunImageImportBatch(negative).status == ProjectIoStatus::Invalid,
           "invalid-plan.negative-aggregate-limit-refused");
    Expect(RunImageImportBatch(wrongLength).status == ProjectIoStatus::Invalid,
           "invalid-plan.file-plan-count-mismatch-refused");

    std::array<std::wstring_view, kImageImportMaxPaths + 1> tooMany{};
    tooMany.fill(path);
    int addedLimit = -1;
    Expect(ImageImportMetadataMemoryBytes(tooMany) == 0 &&
               ImageImportMemoryBytes(tooMany, {}, nullptr, addedLimit) == 0 && addedLimit == 0,
           "memory.invalid-path-count-zeroes-limit");
    const std::array<std::wstring_view, 0> noPaths{};
    Expect(ImageImportMetadataMemoryBytes(noPaths) == 0 &&
               ImageImportMemoryBytes(noPaths, {}, nullptr, addedLimit) == 0 && addedLimit == 0,
           "memory.empty-path-list-refused");
    const std::array<ImageImportFilePlan, 1> invalidPixels{{{ProjectIoStatus::Ok, -1, 10}}};
    Expect(ImageImportMemoryBytes(paths, invalidPixels, nullptr, addedLimit) == 0 && addedLimit == 0,
           "memory.invalid-pixel-plan-refused-and-limit-cleared");
    const std::array<ImageImportFilePlan, 1> overPixelLimit{{{ProjectIoStatus::Ok, kMaxLayerPixels + 1, 10}}};
    const std::array<ImageImportFilePlan, 1> overEncodedLimit{{{ProjectIoStatus::Ok, 1, kMaxImageEncodedBytes + 1}}};
    Expect(ImageImportMemoryBytes(paths, overPixelLimit, nullptr, addedLimit) == 0 && addedLimit == 0 &&
               ImageImportMemoryBytes(paths, overEncodedLimit, nullptr, addedLimit) == 0 && addedLimit == 0,
           "memory.pixel-and-encoded-arithmetic-bounds-refused");

    CancelSource cancellation;
    (void)cancellation.Cancel();
    const ImageImportProbeResult probe = ProbeImageImportBatch(paths, cancellation.Token());
    Expect(probe.status == ProjectIoStatus::Cancelled && probe.files.empty(), "cancel.probe-clears-partial-file-plans");
    const ImageImportBatchInput cancelledInput{std::nullopt, std::nullopt, paths, true, std::nullopt,
                                               nullptr,      nullptr,      {},    plan, 1};
    const ImageImportBatchResult cancelled = RunImageImportBatch(cancelledInput, cancellation.Token());
    Expect(cancelled.status == ProjectIoStatus::Cancelled && !cancelled.createdDocument &&
               !cancelled.createdActiveLayerID && !cancelled.importedDocument && !cancelled.importedActiveLayerID &&
               cancelled.historyName.empty(),
           "cancel.batch-discards-all-prepared-output");
}

void ImportMemoryPlanCoversPixelThumbnailAndSourceRecordFloors(const ScratchDirectory& scratch)
{
    const std::wstring path = scratch.Child(L"memory-floor.png");
    const std::array<std::wstring_view, 1> paths{path};
    const std::array<ImageImportFilePlan, 1> maximum{{{ProjectIoStatus::Ok, kMaxLayerPixels, 1}}};
    int addedLimit = -1;
    const std::uint64_t noSource = ImageImportMemoryBytes(paths, maximum, nullptr, addedLimit);
    const std::uint64_t pixelBytes = PixelBuffer::RequiredBytes(static_cast<std::size_t>(kMaxLayerPixels) * 4);
    const std::uint64_t thumbnailBytes = PixelBuffer::RequiredBytes(96u * 96u * 4u);
    const std::uint64_t layerFloor = sizeof(ImageLayer);
    const std::uint64_t metadata = ImageImportMetadataMemoryBytes(paths);
    Expect(addedLimit == kMaxLayerPixels && noSource >= metadata + pixelBytes + thumbnailBytes + layerFloor,
           "memory.100m-pixel-plan-covers-pixelbuffer-thumbnail-and-layer-floor");

    const Raster small = Pixel(21, 32, 43);
    const Raster largeSparse = Raster::MakeSparse(10000, 10000, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    CanvasDocument smallSource = ExistingDocument(small, Guid::Generate(), Guid::Generate());
    CanvasDocument largeSource = smallSource;
    largeSource.layers[1].asset->image = largeSparse;
    const void* identity = largeSource.layers[1].asset->image.Identity();
    int smallLimit = -1;
    int largeLimit = -1;
    const std::uint64_t withSmall = ImageImportMemoryBytes(paths, maximum, &smallSource, smallLimit);
    const std::uint64_t withLarge = ImageImportMemoryBytes(paths, maximum, &largeSource, largeLimit);
    Expect(smallLimit == kMaxLayerPixels && largeLimit == kMaxLayerPixels && withSmall == withLarge &&
               !largeSource.layers[1].asset->image.IsMaterialized() &&
               largeSource.layers[1].asset->image.Identity() == identity,
           "memory.shared-source-raster-not-materialized-or-double-charged");
    std::uint64_t sourceNameUnits = 0;
    for (const ImageLayer& layer : largeSource.layers)
    {
        sourceNameUnits += layer.name.size();
        if (layer.asset)
        {
            sourceNameUnits += layer.asset->name.size();
        }
        if (layer.mask)
        {
            sourceNameUnits += layer.mask->asset.name.size();
        }
    }
    // Independent minimum for captured input, private working copy and prepared output records/names.
    const std::uint64_t sourceFloor =
        3ull * sizeof(ImageLayer) * largeSource.layers.size() + sourceNameUnits * 3ull * sizeof(wchar_t);
    Expect(withLarge >= noSource + sourceFloor, "memory.source-layer-records-and-names-charged");
}
} // namespace

int RunImageImportWorkTests()
{
    gFailures = 0;
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "scratch.created");
    if (!scratch.IsCreated())
    {
        return gFailures;
    }

    ValidateInputBeforeDecode(scratch);
    FirstFailureThenNewCanvas(scratch);
    PartialSuccessAndPluralName(scratch);
    ExistingDocumentImportPreservesSourceAndPlacement(scratch);
    TooLargeFailureIsReportedWithoutPublishing(scratch);
    NewProjectReplacesProvidedSnapshotAndCancellationDiscardsAll(scratch);
    DecodedAssetCancellationBeforeMutation(scratch);
    ProbeBatchRecordsPerFileMetadataAndContinuesAfterMissing(scratch);
    CachedMissingProbeDoesNotDecodeFileThatAppearsLater(scratch);
    ChangedFileCannotExceedProbePixelOrEncodedCeilings(scratch);
    AddedPixelLimitPreservesCreatedBaselineAndAcceptsEquality(scratch);
    FailedProbeStatusSurvivesZeroPixelCeiling(scratch);
    InvalidPlansAndCancellationClearResults(scratch);
    ImportMemoryPlanCoversPixelThumbnailAndSourceRecordFloors(scratch);
    return gFailures;
}
