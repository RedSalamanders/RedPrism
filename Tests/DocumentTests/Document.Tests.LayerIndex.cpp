#include "Document.Tests.LayerIndex.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
int gChecks = 0;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s layer-index.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

Guid Id(std::uint8_t value) noexcept
{
    Guid id;
    id.bytes.front() = value;
    id.bytes.back() = static_cast<std::uint8_t>(value ^ 0xA5u);
    return id;
}

ImageLayer Layer(Guid id, const wchar_t* name)
{
    ImageLayer layer = ImageLayer::Blank(name, Vec2{1, 1});
    layer.id = id;
    return layer;
}

std::optional<RecordRoot<ImageLayer>> Root(std::span<const ImageLayer> records)
{
    RecordRoot<ImageLayer>::Builder builder(records.size());
    for (const ImageLayer& record : records)
    {
        if (!builder.Append(record))
            return std::nullopt;
    }
    return builder.Seal();
}

std::optional<RecordRoot<ImageLayer>> Root(std::initializer_list<ImageLayer> records)
{
    return Root(std::span<const ImageLayer>(records.begin(), records.size()));
}

bool Install(DocumentSession& session, RecordRoot<ImageLayer> layers)
{
    const void* const expectedRootIdentity = layers.Identity();
    CanvasDocument document;
    document.width = 32;
    document.height = 32;
    document.layers = std::move(layers);
    session.ReplaceDocument(std::move(document), std::nullopt);
    const auto& installed = session.Document();
    return installed.has_value() && installed.value().layers.Identity() == expectedRootIdentity;
}

Raster Sparse(int width, int height, RasterFormat format)
{
    return Raster::MakeSparse(width, height, format, Raster{}, 0, 0, {}, 0);
}

bool HasRaster(const Raster& raster, int width, int height, RasterFormat format) noexcept
{
    return raster.HasStorage() && raster.Width() == width && raster.Height() == height && raster.Format() == format;
}

void SetImage(ImageLayer& layer, const Raster& image)
{
    layer.asset.emplace();
    layer.asset->image = image;
    layer.asset->name = layer.name;
}

void SetMask(ImageLayer& layer, const Raster& image)
{
    LayerMask mask;
    mask.asset.image = image;
    mask.asset.name = L"mask";
    layer.mask = std::move(mask);
}
} // namespace

int RunLayerIndexTests()
{
    gChecks = 0;
    gFailures = 0;

    const Guid idA = Id(0x11);
    const Guid idB = Id(0x22);
    const Guid idC = Id(0x33);
    const Guid missing = Id(0x44);
    const Guid blankID = Id(0x55);
    const Guid maskOnlyID = Id(0x66);

    const std::vector<ImageLayer> unsorted{Layer(idC, L"third"), Layer(idA, L"first"), Layer(idB, L"second")};
    const std::span<const ImageLayer> source(unsorted);
    const auto sourceA = IndexOf(source, idA);
    const auto sourceC = IndexOf(source, idC);
    const auto sourceMissing = IndexOf(source, missing);
    Expect(sourceA.has_value() && sourceA.value() == 1 && sourceC.has_value() && sourceC.value() == 0 &&
               !sourceMissing.has_value(),
           "span-index-follows-unsorted-source-and-reports-missing");

    const auto slicedA = IndexOf(source.subspan(1, 2), idA);
    const auto slicedB = IndexOf(source.subspan(1, 2), idB);
    Expect(slicedA.has_value() && slicedA.value() == 0 && slicedB.has_value() && slicedB.value() == 1,
           "span-index-is-relative-to-slice");

    auto root = Root(source);
    const bool rootReady = root.has_value();
    bool rootLookup = false;
    bool copiedRootBorrow = false;
    if (rootReady)
    {
        const auto rootA = IndexOf(root.value(), idA);
        const auto rootMissing = IndexOf(root.value(), missing);
        const ImageLayer* borrowed = root.value().FindKey(idC);
        RecordRoot<ImageLayer> copy = root.value();
        const ImageLayer* copiedBorrowed = copy.FindKey(idC);
        rootLookup = rootA.has_value() && rootA.value() == 1 && !rootMissing.has_value() && borrowed != nullptr &&
                     copiedBorrowed == borrowed;
        copiedRootBorrow = copiedBorrowed != nullptr && copiedBorrowed->id == idC && copiedBorrowed->name == L"third";
    }
    Expect(rootReady && rootLookup, "root-index-matches-source-order-and-reports-missing");
    Expect(copiedRootBorrow, "root-copy-retains-borrowed-layer-record");

    {
        DocumentFixture malformed;
        malformed.width = 1;
        malformed.height = 1;
        malformed.layers.resize(static_cast<std::size_t>(kMaxLayerCount) + 1);
        malformed.layers.front().id = idA;
        CanvasDocument oversized = std::move(malformed).Seal();
        Expect(oversized.layers.size() == 10'001 && oversized.layers.front().id == idA,
               "oversized-snapshot-preserves-all-records-for-validation");
        Expect(!oversized.layers.FindIndex(idA) && !oversized.layers.FindKey(idA) && !oversized.layers.HasUniqueKeys(),
               "oversized-snapshot-has-no-index-or-uniqueness-authority");
        Expect(ValidateDocument(oversized) == DocumentStatus::TooLarge,
               "oversized-snapshot-retains-exact-production-size-refusal");
        auto invalidSessionOwner = std::make_unique<DocumentSession>();
        auto& invalidSession = *invalidSessionOwner;
        invalidSession.ReplaceDocument(std::move(oversized), std::nullopt);
        Expect(invalidSession.PixelBudgetLeft(false) == 0 && invalidSession.PixelBudgetLeft(true) == 0,
               "oversized-snapshot-cannot-authorize-cached-pixel-budget");
    }

    bool builderEdits = false;
    bool sourceRootUnchanged = false;
    if (rootReady)
    {
        auto builder = RecordRoot<ImageLayer>::Builder::Clone(root.value(), 4);
        if (builder.has_value())
        {
            auto replacementOwner = std::make_unique<ImageLayer>(Layer(idA, L"replaced"));
            auto& replacement = *replacementOwner;
            auto appendedOwner = std::make_unique<ImageLayer>(Layer(missing, L"appended"));
            auto& appended = *appendedOwner;
            const bool replaceOkay = builder.value().Replace(1, std::move(replacement));
            const bool eraseOkay = builder.value().Erase(0);
            const bool appendOkay = builder.value().Append(std::move(appended));
            auto edited = builder.value().Seal();
            if (edited.has_value())
            {
                const auto replaced = edited.value().FindKey(idA);
                const auto appendedIndex = edited.value().FindIndex(missing);
                const bool hasFirst = edited.value().size() > 0;
                builderEdits = replaceOkay && eraseOkay && appendOkay && edited.value().size() == 3 && hasFirst &&
                               edited.value().Read().front().id == idA && replaced != nullptr &&
                               replaced->name == L"replaced" && appendedIndex.has_value() && appendedIndex.value() == 2;
                const auto originalA = IndexOf(root.value(), idA);
                const ImageLayer* originalLayer = root.value().FindKey(idA);
                sourceRootUnchanged = originalA.has_value() && originalA.value() == 1 && originalLayer != nullptr &&
                                      originalLayer->name == L"first" && root.value().size() == 3;
            }
        }
    }
    Expect(builderEdits, "builder-clone-replace-erase-and-seal-rebuild-index");
    Expect(sourceRootUnchanged, "builder-edited-clone-leaves-source-root-unchanged");

    bool sessionBorrowRetained = false;
    auto lifetimeRoot = Root(source);
    if (lifetimeRoot.has_value())
    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        const bool installed = Install(session, lifetimeRoot.value());
        if (installed && session.Document().has_value())
        {
            session.SelectLayer(idA);
            const ImageLayer* borrowed = session.ActiveLayer();
            RecordRoot<ImageLayer> retainedCopy = session.Document().value().layers;
            const void* const capturedIdentity = retainedCopy.Identity();
            const bool hasBorrowedLayer = borrowed != nullptr;
            const bool retainedSameRoot =
                capturedIdentity != nullptr && session.Document().value().layers.Identity() == capturedIdentity;
            const bool replaced = Install(session, RecordRoot<ImageLayer>{});
            lifetimeRoot.value() = RecordRoot<ImageLayer>{};
            sessionBorrowRetained = hasBorrowedLayer && retainedSameRoot && replaced &&
                                    retainedCopy.FindKey(idA) == borrowed && borrowed->id == idA &&
                                    borrowed->name == L"first";
        }
    }
    Expect(sessionBorrowRetained, "copied-document-root-keeps-borrowed-session-layer-alive");

    const Raster sharedImage = Raster::Make(3, 2, RasterFormat::Bgra8888);
    const Raster uniqueImage = Raster::Make(4, 1, RasterFormat::Bgra8888);
    const Raster derivedThumbnail = Raster::Make(1, 1, RasterFormat::Bgra8888);
    const Raster shapeSource = Raster::Make(1, 1, RasterFormat::Bgra8888);
    const Raster maskA = Raster::Make(2, 2, RasterFormat::Gray8);
    const Raster maskB = Raster::Make(3, 2, RasterFormat::Gray8);
    const Raster maskOnly = Raster::Make(1, 1, RasterFormat::Gray8);
    auto budgetAOwner = std::make_unique<ImageLayer>(Layer(idA, L"shared-a"));
    auto& budgetA = *budgetAOwner;
    SetImage(budgetA, sharedImage);
    budgetA.asset->thumbnail = derivedThumbnail;
    SetMask(budgetA, maskA);
    auto budgetBOwner = std::make_unique<ImageLayer>(Layer(idB, L"shared-b"));
    auto& budgetB = *budgetBOwner;
    SetImage(budgetB, sharedImage);
    SetMask(budgetB, maskB);
    auto budgetCOwner = std::make_unique<ImageLayer>(Layer(idC, L"image-only-c"));
    auto& budgetC = *budgetCOwner;
    SetImage(budgetC, uniqueImage);
    budgetC.shapeImage = shapeSource;
    auto blankOwner = std::make_unique<ImageLayer>(Layer(blankID, L"blank"));
    auto& blank = *blankOwner;
    auto maskOnlyLayerOwner = std::make_unique<ImageLayer>(Layer(maskOnlyID, L"mask-only"));
    auto& maskOnlyLayer = *maskOnlyLayerOwner;
    SetMask(maskOnlyLayer, maskOnly);
    const std::vector<ImageLayer> budgetRecords{budgetA, budgetB, budgetC, blank, maskOnlyLayer};
    const bool smallRastersValid =
        HasRaster(sharedImage, 3, 2, RasterFormat::Bgra8888) && HasRaster(uniqueImage, 4, 1, RasterFormat::Bgra8888) &&
        HasRaster(derivedThumbnail, 1, 1, RasterFormat::Bgra8888) &&
        HasRaster(shapeSource, 1, 1, RasterFormat::Bgra8888) && HasRaster(maskA, 2, 2, RasterFormat::Gray8) &&
        HasRaster(maskB, 3, 2, RasterFormat::Gray8) && HasRaster(maskOnly, 1, 1, RasterFormat::Gray8);
    auto budgetRoot =
        smallRastersValid ? Root(std::span<const ImageLayer>(budgetRecords)) : std::optional<RecordRoot<ImageLayer>>{};
    auto budgetSessionOwner = std::make_unique<DocumentSession>();
    auto& budgetSession = *budgetSessionOwner;
    const bool smallInstalled = budgetRoot.has_value() && Install(budgetSession, budgetRoot.value());
    Expect(smallRastersValid && budgetRoot.has_value() && smallInstalled,
           "small-budget-fixtures-have-valid-raster-storage-and-install-root");

    std::int64_t imageAll = 0;
    std::int64_t maskAll = 0;
    std::int64_t imageMinusA = 0;
    std::int64_t imageMinusRepeatedA = 0;
    std::int64_t imageMinusMissing = 0;
    std::int64_t imageMinusBlank = 0;
    std::int64_t maskMinusB = 0;
    std::int64_t maskMinusImageOnly = 0;
    std::int64_t imageMinusMaskOnly = 0;
    std::int64_t imageSetResult = 0;
    std::int64_t maskSetResult = 0;
    if (smallInstalled)
    {
        imageAll = budgetSession.PixelBudgetLeft(false);
        maskAll = budgetSession.PixelBudgetLeft(true);
        imageMinusA = budgetSession.PixelBudgetLeft(false, {idA});
        imageMinusRepeatedA = budgetSession.PixelBudgetLeft(false, {idA, idA, missing});
        imageMinusMissing = budgetSession.PixelBudgetLeft(false, {missing});
        imageMinusBlank = budgetSession.PixelBudgetLeft(false, {blankID});
        maskMinusB = budgetSession.PixelBudgetLeft(true, {idB});
        maskMinusImageOnly = budgetSession.PixelBudgetLeft(true, {idC});
        imageMinusMaskOnly = budgetSession.PixelBudgetLeft(false, {maskOnlyID});
        const std::unordered_set<Guid> imageSetExclusion{idA, idB, missing};
        imageSetResult = budgetSession.PixelBudgetLeft(false, imageSetExclusion);
        const std::unordered_set<Guid> maskSetExclusion{idA, missing};
        maskSetResult = budgetSession.PixelBudgetLeft(true, maskSetExclusion);
    }
    Expect(smallInstalled && imageAll == 99'999'984, "small-image-total-counts-shared-image-once-per-layer");
    Expect(smallInstalled && maskAll == 99'999'989, "small-mask-total-counts-only-mask-assets");
    Expect(smallInstalled && imageAll == 99'999'984 && maskAll == 99'999'989,
           "small-thumbnail-and-shape-source-do-not-add-budget-pixels");
    Expect(smallInstalled && imageMinusA == 99'999'990, "small-image-single-layer-exclusion");
    Expect(smallInstalled && imageMinusRepeatedA == 99'999'990, "small-repeated-image-exclusion-is-idempotent");
    Expect(smallInstalled && imageMinusMissing == 99'999'984, "small-missing-image-exclusion-is-ignored");
    Expect(smallInstalled && imageMinusBlank == 99'999'984, "small-blank-layer-image-exclusion-is-ignored");
    Expect(smallInstalled && maskMinusB == 99'999'995, "small-mask-layer-exclusion");
    Expect(smallInstalled && maskMinusImageOnly == 99'999'989 && imageMinusMaskOnly == 99'999'984,
           "small-cross-category-exclusions-ignore-other-raster-kind");
    Expect(smallInstalled && imageSetResult == 99'999'996 && maskSetResult == 99'999'993,
           "small-unordered-set-exclusions-match-literal-reference");

    bool emptySemantics = false;
    {
        auto noDocumentOwner = std::make_unique<DocumentSession>();
        auto& noDocument = *noDocumentOwner;
        auto emptyDocumentOwner = std::make_unique<DocumentSession>();
        auto& emptyDocument = *emptyDocumentOwner;
        emptyDocument.CreateDocument(1, 1, false);
        const auto& emptyState = emptyDocument.Document();
        emptySemantics = !noDocument.Document().has_value() && noDocument.PixelBudgetLeft(false) == 0 &&
                         noDocument.PixelBudgetLeft(true) == 0 && emptyState.has_value() &&
                         emptyState.value().layers.empty() && emptyDocument.PixelBudgetLeft(false) == 100'000'000 &&
                         emptyDocument.PixelBudgetLeft(true) == 100'000'000;
    }
    Expect(emptySemantics, "no-document-and-empty-document-budget-semantics");

    bool exactImageBoundary = false;
    {
        auto nearOwner = std::make_unique<ImageLayer>(Layer(idA, L"near-image-boundary"));
        auto& near = *nearOwner;
        const Raster nearImage = Sparse(10'000, 9'999, RasterFormat::Bgra8888); // 99,990,000 pixels.
        SetImage(near, nearImage);
        auto lastOwner = std::make_unique<ImageLayer>(Layer(idB, L"last-image-boundary"));
        auto& last = *lastOwner;
        const Raster lastImage = Sparse(100, 100, RasterFormat::Bgra8888); // 10,000 pixels.
        SetImage(last, lastImage);
        const std::vector<ImageLayer> records{near, last};
        const bool rastersValid = HasRaster(nearImage, 10'000, 9'999, RasterFormat::Bgra8888) &&
                                  HasRaster(lastImage, 100, 100, RasterFormat::Bgra8888);
        auto layers =
            rastersValid ? Root(std::span<const ImageLayer>(records)) : std::optional<RecordRoot<ImageLayer>>{};
        if (layers.has_value())
        {
            auto sessionOwner = std::make_unique<DocumentSession>();
            auto& session = *sessionOwner;
            if (Install(session, layers.value()))
            {
                const auto exact = session.PixelBudgetLeft(false);
                const auto lessNear = session.PixelBudgetLeft(false, {idA});
                exactImageBoundary =
                    exact == 0 && lessNear == 99'990'000 && !nearImage.IsMaterialized() && !lastImage.IsMaterialized();
            }
        }
    }
    Expect(exactImageBoundary, "image-budget-exact-100m-boundary-with-sparse-grids");

    bool exactMaskBoundary = false;
    {
        auto nearOwner = std::make_unique<ImageLayer>(Layer(idA, L"near-mask-boundary"));
        auto& near = *nearOwner;
        const Raster nearMask = Sparse(10'000, 9'999, RasterFormat::Gray8); // 99,990,000 pixels.
        SetMask(near, nearMask);
        auto lastOwner = std::make_unique<ImageLayer>(Layer(idB, L"last-mask-boundary"));
        auto& last = *lastOwner;
        const Raster lastMask = Sparse(100, 100, RasterFormat::Gray8); // 10,000 pixels.
        SetMask(last, lastMask);
        const std::vector<ImageLayer> records{near, last};
        const bool rastersValid = HasRaster(nearMask, 10'000, 9'999, RasterFormat::Gray8) &&
                                  HasRaster(lastMask, 100, 100, RasterFormat::Gray8);
        auto layers =
            rastersValid ? Root(std::span<const ImageLayer>(records)) : std::optional<RecordRoot<ImageLayer>>{};
        if (layers.has_value())
        {
            auto sessionOwner = std::make_unique<DocumentSession>();
            auto& session = *sessionOwner;
            if (Install(session, layers.value()))
            {
                const auto exact = session.PixelBudgetLeft(true);
                const auto lessNear = session.PixelBudgetLeft(true, {idA});
                exactMaskBoundary =
                    exact == 0 && lessNear == 99'990'000 && !nearMask.IsMaterialized() && !lastMask.IsMaterialized();
            }
        }
    }
    Expect(exactMaskBoundary, "mask-budget-exact-100m-boundary-with-sparse-grids");

    bool imageOverBoundary = false;
    {
        auto fullOwner = std::make_unique<ImageLayer>(Layer(idA, L"full-image-grid"));
        auto& full = *fullOwner;
        const Raster fullImage = Sparse(10'000, 10'000, RasterFormat::Bgra8888); // 100,000,000 pixels.
        SetImage(full, fullImage);
        auto extraOwner = std::make_unique<ImageLayer>(Layer(idB, L"one-extra-image-pixel"));
        auto& extra = *extraOwner;
        const Raster extraImage = Sparse(1, 1, RasterFormat::Bgra8888);
        SetImage(extra, extraImage);
        const std::vector<ImageLayer> records{full, extra};
        const bool rastersValid = HasRaster(fullImage, 10'000, 10'000, RasterFormat::Bgra8888) &&
                                  HasRaster(extraImage, 1, 1, RasterFormat::Bgra8888);
        auto layers =
            rastersValid ? Root(std::span<const ImageLayer>(records)) : std::optional<RecordRoot<ImageLayer>>{};
        if (layers.has_value())
        {
            CanvasDocument oversized;
            oversized.width = 1;
            oversized.height = 1;
            oversized.layers = layers.value();
            const bool validatorRejects = ValidateDocument(oversized) == DocumentStatus::TooLarge;
            auto sessionOwner = std::make_unique<DocumentSession>();
            auto& session = *sessionOwner;
            session.CreateDocument(1, 1, false);
            const void* const expectedIdentity = layers.value().Identity();
            session.EditForTest([&](CanvasDocument& document) { document.layers = layers.value(); });
            const auto& installed = session.Document();
            const bool hookInstalled = installed.has_value() && installed.value().layers.Identity() == expectedIdentity;
            imageOverBoundary = validatorRejects && hookInstalled && session.PixelBudgetLeft(false) == 0 &&
                                !fullImage.IsMaterialized() && !extraImage.IsMaterialized();
        }
    }
    Expect(imageOverBoundary, "image-budget-100m-plus-one-test-hook-after-validator-refusal");

    bool maskOverBoundary = false;
    {
        auto fullOwner = std::make_unique<ImageLayer>(Layer(idA, L"full-mask-grid"));
        auto& full = *fullOwner;
        const Raster fullMask = Sparse(10'000, 10'000, RasterFormat::Gray8); // 100,000,000 pixels.
        SetMask(full, fullMask);
        auto extraOwner = std::make_unique<ImageLayer>(Layer(idB, L"one-extra-mask-pixel"));
        auto& extra = *extraOwner;
        const Raster extraMask = Sparse(1, 1, RasterFormat::Gray8);
        SetMask(extra, extraMask);
        const std::vector<ImageLayer> records{full, extra};
        const bool rastersValid =
            HasRaster(fullMask, 10'000, 10'000, RasterFormat::Gray8) && HasRaster(extraMask, 1, 1, RasterFormat::Gray8);
        auto layers =
            rastersValid ? Root(std::span<const ImageLayer>(records)) : std::optional<RecordRoot<ImageLayer>>{};
        if (layers.has_value())
        {
            CanvasDocument oversized;
            oversized.width = 1;
            oversized.height = 1;
            oversized.layers = layers.value();
            const bool validatorRejects = ValidateDocument(oversized) == DocumentStatus::TooLarge;
            auto sessionOwner = std::make_unique<DocumentSession>();
            auto& session = *sessionOwner;
            session.CreateDocument(1, 1, false);
            const void* const expectedIdentity = layers.value().Identity();
            session.EditForTest([&](CanvasDocument& document) { document.layers = layers.value(); });
            const auto& installed = session.Document();
            const bool hookInstalled = installed.has_value() && installed.value().layers.Identity() == expectedIdentity;
            maskOverBoundary = validatorRejects && hookInstalled && session.PixelBudgetLeft(true) == 0 &&
                               !fullMask.IsMaterialized() && !extraMask.IsMaterialized();
        }
    }
    Expect(maskOverBoundary, "mask-budget-100m-plus-one-test-hook-after-validator-refusal");

    bool duplicateConservative = false;
    {
        auto firstOwner = std::make_unique<ImageLayer>(Layer(idA, L"first duplicate"));
        auto& first = *firstOwner;
        auto secondOwner = std::make_unique<ImageLayer>(Layer(idA, L"second duplicate"));
        auto& second = *secondOwner;
        const auto duplicateRoot = Root({first, second});
        if (duplicateRoot.has_value())
        {
            const auto duplicateIndex = IndexOf(duplicateRoot.value(), idA);
            auto sessionOwner = std::make_unique<DocumentSession>();
            auto& session = *sessionOwner;
            session.CreateDocument(1, 1, false);
            const void* const expectedIdentity = duplicateRoot.value().Identity();
            session.EditForTest([&](CanvasDocument& document) { document.layers = duplicateRoot.value(); });
            const auto& installed = session.Document();
            const bool hookInstalled = installed.has_value() && installed.value().layers.Identity() == expectedIdentity;
            if (hookInstalled)
                session.SelectLayer(idA);
            const ImageLayer* active = hookInstalled ? session.ActiveLayer() : nullptr;
            duplicateConservative = hookInstalled && duplicateIndex.has_value() && duplicateIndex.value() == 0 &&
                                    active != nullptr && active->name == L"first duplicate" &&
                                    session.PixelBudgetLeft(false) == 0 && session.PixelBudgetLeft(true) == 0;
        }
    }
    Expect(duplicateConservative, "duplicate-ids-refuse-budget-and-preserve-first-lookup-record");

    std::printf("layer-index.records=%d failures=%d\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
