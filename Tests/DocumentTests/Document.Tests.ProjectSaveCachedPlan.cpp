#include "Document.Tests.ProjectSaveCachedPlan.h"

#include "Document.h"
#include "ProjectSaveWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s project-save-cached-plan.%s\n", condition ? "PASS" : "FAIL", label);
    ++assertions;
    if (!condition)
        ++failures;
}

// Frozen Core7 Common/ProjectSaveWork.h scalar oracle, copied verbatim except for this test-private name.
[[nodiscard]] inline std::uint64_t ReferenceProjectSaveMemoryBytes(const CanvasDocument& document,
                                                                   std::size_t pathUnits) noexcept
{
    if (pathUnits == 0 || pathUnits > kMaxProjectOutputPathUnits ||
        document.layers.size() > static_cast<std::size_t>(kMaxLayerCount) || document.width < 1 ||
        document.height < 1 || document.width > kMaxCanvasSide || document.height > kMaxCanvasSide)
        return 0;
    // Bounded manifest payload, JSON text copies and codec/scratch allowance (including the 64 KiB directory
    // cleanup buffer allocated before staging). Per-record nodes and dynamic
    // strings are charged below even for documents whose serialized manifest will ultimately be refused.
    std::uint64_t bytes = 16ull * 1024 * 1024;
    const auto add = [&](std::uint64_t value) noexcept
    {
        if (value > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += value;
        return true;
    };
    std::uint64_t largestAsset = 0;
    std::uint64_t assets = 0;
    const auto asset = [&](const RasterAsset& value) noexcept
    {
        if (!value.image.HasStorage() || value.image.Width() < 1 || value.image.Height() < 1 ||
            value.image.Width() > kMaxCanvasSide || value.image.Height() > kMaxCanvasSide ||
            value.image.PixelCount() > kMaxLayerPixels)
            return false;
        largestAsset =
            (std::max)(largestAsset, static_cast<std::uint64_t>(value.image.Stride()) * value.image.Height());
        ++assets;
        const auto units = static_cast<std::uint64_t>(value.name.size());
        return units <= ((std::numeric_limits<std::uint64_t>::max)() - 128) / 32 && add(units * 32 + 128);
    };
    for (const auto& layer : document.layers)
    {
        if (layer.adjustment)
            for (const auto& channel : layer.adjustment->curves.channels)
                if (channel.size() > 32)
                    return 0;
        const auto units = static_cast<std::uint64_t>(layer.name.size());
        // Covers the snapshot, yyjson mutable per-layer values and pretty JSON expansion, including 4x32
        // curve points and every optional adjustment payload. The manifest's 4 MiB publication cap remains.
        if (units > (std::numeric_limits<std::uint64_t>::max)() / 32 || !add(units * 32) ||
            !add(sizeof(ImageLayer) + 64 * 1024) || (layer.asset && !asset(layer.asset.value())) ||
            (layer.mask && !asset(layer.mask->asset)))
            return 0;
    }
    // Two path copies per libzip source plus exclusive staging/member names and fixed source bookkeeping.
    // Asset files are streamed; no 512 MiB encoded vector per source is retained.
    const auto sourceBytes = (static_cast<std::uint64_t>(pathUnits) + 128) * 4 * sizeof(wchar_t) + 4096;
    return add(largestAsset) && add(assets * sourceBytes) && add(sourceBytes * 8) ? bytes : 0;
}

[[nodiscard]] CanvasDocument MakeDocument(std::vector<ImageLayer> layers, int width = 16, int height = 16)
{
    const std::size_t limit = (std::max)(std::size_t{1}, layers.size());
    RecordRoot<ImageLayer>::Builder builder(limit);
    if (!builder.CanSeal())
        std::terminate();
    for (auto& layer : layers)
        if (!builder.Append(std::move(layer)))
            std::terminate();
    auto root = builder.Seal();
    if (!root)
        std::terminate();
    CanvasDocument document;
    document.width = width;
    document.height = height;
    document.layers = std::move(root.value());
    return document;
}

[[nodiscard]] ImageLayer NamedLayer(std::wstring name)
{
    ImageLayer layer;
    layer.name = std::move(name);
    return layer;
}

void CompareReservation(const CanvasDocument& document, std::size_t pathUnits, const char* label) noexcept
{
    Expect(ProjectSaveMemoryBytes(document, pathUnits) == ReferenceProjectSaveMemoryBytes(document, pathUnits), label);
}

void CompareDefaultAndSealedRoots()
{
    const CanvasDocument defaultDocument;
    CompareReservation(defaultDocument, 1, "default-root-matches-frozen-scalar-plan");
    CompareReservation(defaultDocument, kMaxProjectOutputPathUnits,
                       "default-root-maximum-path-matches-frozen-scalar-plan");

    CanvasDocument sealedEmpty = MakeDocument({});
    CompareReservation(sealedEmpty, 1, "sealed-empty-root-matches-frozen-scalar-plan");
    Expect(sealedEmpty.layers.Identity() != nullptr, "sealed-empty-root-has-published-identity");

    CanvasDocument oneLayer = MakeDocument({NamedLayer(L"one")});
    CompareReservation(oneLayer, 1, "single-layer-root-matches-frozen-scalar-plan");
}

void CompareRecordAndPathBoundaries()
{
    std::vector<ImageLayer> records;
    records.reserve(static_cast<std::size_t>(kMaxLayerCount));
    for (int index = 0; index < kMaxLayerCount; ++index)
        records.push_back(NamedLayer(L"record"));
    const CanvasDocument maximumRecords = MakeDocument(std::move(records));
    CompareReservation(maximumRecords, 1, "maximum-layer-count-matches-frozen-scalar-plan");

    const CanvasDocument named =
        MakeDocument({NamedLayer(L"Grüße_日本語_слой_🎨"), NamedLayer(std::wstring(4096, L'n'))});
    CompareReservation(named, 1, "unicode-and-long-layer-names-match-frozen-scalar-plan");
    CompareReservation(named, kMaxProjectOutputPathUnits, "long-legal-path-matches-frozen-scalar-plan");
    CompareReservation(named, 0, "empty-path-is-rejected-like-frozen-scalar-plan");
    CompareReservation(named, kMaxProjectOutputPathUnits + 1, "over-limit-path-is-rejected-like-frozen-scalar-plan");
}

void CompareCurveAndInvalidAssetBoundaries()
{
    LayerAdjustment acceptedAdjustment;
    auto& acceptedCurve = acceptedAdjustment.curves.channels[0];
    acceptedCurve.clear();
    acceptedCurve.reserve(32);
    for (int point = 0; point < 32; ++point)
        acceptedCurve.push_back(CurvePoint{static_cast<double>(point), static_cast<double>(point)});
    ImageLayer accepted = NamedLayer(L"32 points");
    accepted.adjustment = acceptedAdjustment;
    CompareReservation(MakeDocument({std::move(accepted)}), 1, "32-curve-points-match-frozen-scalar-plan");

    LayerAdjustment refusedAdjustment;
    auto& refusedCurve = refusedAdjustment.curves.channels[0];
    refusedCurve.clear();
    refusedCurve.reserve(33);
    for (int point = 0; point < 33; ++point)
        refusedCurve.push_back(CurvePoint{static_cast<double>(point), static_cast<double>(point)});
    ImageLayer refused = NamedLayer(L"33 points");
    refused.adjustment = std::move(refusedAdjustment);
    const CanvasDocument tooManyPoints = MakeDocument({std::move(refused)});
    CompareReservation(tooManyPoints, 1, "33-curve-points-refused-like-frozen-scalar-plan");
    Expect(ProjectSaveMemoryBytes(tooManyPoints, 1) == 0, "33-curve-points-yield-zero-reservation");

    ImageLayer invalid = NamedLayer(L"invalid asset");
    invalid.asset = RasterAsset{Raster{}, {}, L"empty image"};
    const CanvasDocument invalidAsset = MakeDocument({std::move(invalid)});
    CompareReservation(invalidAsset, 1, "empty-raster-asset-refused-like-frozen-scalar-plan");
    Expect(ProjectSaveMemoryBytes(invalidAsset, 1) == 0, "empty-raster-asset-yields-zero-reservation");
}

void CompareRasterAssets()
{
    constexpr std::array<std::uint8_t, 16> rgbBytes{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    constexpr std::array<std::uint8_t, 4> grayBytes{17, 33, 65, 129};
    const Raster rgb = Raster::Make(2, 2, RasterFormat::Bgra8888, rgbBytes.data(), rgbBytes.size());
    const Raster gray = Raster::Make(2, 2, RasterFormat::Gray8, grayBytes.data(), grayBytes.size());
    ImageLayer colored = NamedLayer(L"RGB image with gray mask");
    colored.asset = RasterAsset{rgb, {}, L"RGB source"};
    LayerMask mask;
    mask.asset = RasterAsset{gray, {}, L"Gray mask"};
    colored.mask = std::move(mask);
    CompareReservation(MakeDocument({std::move(colored)}), 17, "rgb-image-and-gray-mask-match-frozen-scalar-plan");

    constexpr std::array<std::uint8_t, 8> patchBytes{20, 21, 22, 255, 23, 24, 25, 255};
    constexpr std::array<std::uint8_t, 2> grayPatchBytes{31, 47};
    const Raster patch = Raster::Make(2, 1, RasterFormat::Bgra8888, patchBytes.data(), patchBytes.size());
    const Raster grayPatch = Raster::Make(2, 1, RasterFormat::Gray8, grayPatchBytes.data(), grayPatchBytes.size());
    const Raster sparseImage =
        Raster::MakeSparse(8, 4, RasterFormat::Bgra8888, rgb, 1, 1, {RasterTile{5, 2, patch}}, 3);
    const Raster sparseMask =
        Raster::MakeSparse(8, 4, RasterFormat::Gray8, gray, 1, 1, {RasterTile{5, 2, grayPatch}}, 9);
    const void* const imageIdentity = sparseImage.Identity();
    const void* const maskIdentity = sparseMask.Identity();
    ImageLayer sparse = NamedLayer(L"sparse pair");
    sparse.asset = RasterAsset{sparseImage, {}, L"sparse RGB"};
    LayerMask sparseLayerMask;
    sparseLayerMask.asset = RasterAsset{sparseMask, {}, L"sparse gray"};
    sparse.mask = std::move(sparseLayerMask);
    const CanvasDocument sparseDocument = MakeDocument({std::move(sparse)});
    CompareReservation(sparseDocument, 37, "sparse-image-and-mask-match-frozen-scalar-plan");
    Expect(!sparseImage.IsMaterialized() && !sparseMask.IsMaterialized(),
           "sparse-planning-does-not-materialize-image-or-mask");
    Expect(sparseImage.Identity() == imageIdentity && sparseMask.Identity() == maskIdentity,
           "sparse-planning-preserves-image-and-mask-identities");

    ImageLayer first = NamedLayer(L"shared first");
    first.asset = RasterAsset{rgb, {}, L"reused"};
    LayerMask firstMask;
    firstMask.asset = RasterAsset{gray, {}, L"reused mask"};
    first.mask = std::move(firstMask);
    ImageLayer second = NamedLayer(L"shared second");
    second.asset = RasterAsset{rgb, {}, L"reused"};
    LayerMask secondMask;
    secondMask.asset = RasterAsset{gray, {}, L"reused mask"};
    second.mask = std::move(secondMask);
    CompareReservation(MakeDocument({std::move(first), std::move(second)}), 5,
                       "repeated-shared-image-and-mask-occurrences-match-frozen-scalar-plan");
}

void CompareRootCopyCloneAndCancellation()
{
    CanvasDocument original = MakeDocument({NamedLayer(L"original root")});
    const auto originalIdentity = original.layers.Identity();
    const auto originalReservation = ReferenceProjectSaveMemoryBytes(original, 23);

    CanvasDocument rootCopy = original;
    Expect(rootCopy.layers.Identity() == originalIdentity, "copied-root-retains-shared-root-identity");
    Expect(ProjectSaveMemoryBytes(rootCopy, 23) == ReferenceProjectSaveMemoryBytes(rootCopy, 23),
           "copied-root-reservation-matches-frozen-scalar-plan");

    auto cloned = RecordRoot<ImageLayer>::Builder::Clone(original.layers, original.layers.size() + 1);
    Expect(cloned.has_value(), "root-clone-builder-created");
    if (!cloned)
        return;
    ImageLayer changed = original.layers[0];
    changed.name = L"new root has a distinct and longer record name";
    Expect(cloned.value().Replace(0, std::move(changed)), "root-clone-record-replaced-before-seal");
    auto sealed = cloned.value().Seal();
    Expect(sealed.has_value(), "root-clone-seals-with-new-summary");
    if (!sealed)
        return;
    CanvasDocument newRoot = original;
    newRoot.layers = std::move(sealed.value());
    Expect(newRoot.layers.Identity() != originalIdentity, "clone-seal-publishes-distinct-root-identity");
    Expect(ProjectSaveMemoryBytes(newRoot, 23) == ReferenceProjectSaveMemoryBytes(newRoot, 23),
           "new-root-reservation-matches-frozen-scalar-plan");
    Expect(ReferenceProjectSaveMemoryBytes(newRoot, 23) != originalReservation,
           "changed-clone-has-distinct-reference-reservation");
    Expect(original.layers.Identity() == originalIdentity, "old-root-identity-survives-clone-seal");
    Expect(ProjectSaveMemoryBytes(original, 23) == originalReservation, "old-root-reservation-survives-clone-seal");
    Expect(ReferenceProjectSaveMemoryBytes(original, 23) == originalReservation,
           "old-root-baseline-still-matches-frozen-scalar-plan");

    CancelSource cancelled;
    auto pendingClone = RecordRoot<ImageLayer>::Builder::Clone(original.layers, original.layers.size() + 1);
    Expect(pendingClone.has_value(), "cancel-test-clone-builder-created");
    if (!pendingClone)
        return;
    static_cast<void>(cancelled.Cancel());
    const auto cancelledSeal = pendingClone.value().Seal(cancelled.Token());
    Expect(!cancelledSeal, "cancelled-clone-seal-is-refused");
    Expect(original.layers.Identity() == originalIdentity, "cancelled-clone-preserves-old-root-identity");
    Expect(ProjectSaveMemoryBytes(original, 23) == originalReservation,
           "cancelled-clone-preserves-old-root-reservation");
}
} // namespace

int RunProjectSaveCachedPlanTests()
{
    failures = 0;
    assertions = 0;
    CompareDefaultAndSealedRoots();
    CompareRecordAndPathBoundaries();
    CompareCurveAndInvalidAssetBoundaries();
    CompareRasterAssets();
    CompareRootCopyCloneAndCancellation();
    std::printf("%s project-save-cached-plan.summary passed=%d failed=%d assertions=%d\n",
                failures == 0 ? "PASS" : "FAIL", assertions - failures, failures, assertions);
    return failures;
}