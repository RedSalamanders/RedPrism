#include "ImageImportWorkTests.h"

#include "ImageImportWork.h"
#include "ImageImportWorkTests.h"

#include "ImageCodecs.h"

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
    return gFailures;
}
