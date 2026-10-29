#include "Document.Tests.RasterResidencyManifest.h"

#include "Common/RasterResidencyManifestWork.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdio>
#include <limits>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::is_copy_constructible_v<RasterResidencyManifestWork>);
static_assert(!std::is_move_constructible_v<RasterResidencyManifestWork>);

namespace
{
unsigned int gChecks = 0;
unsigned int gFailures = 0;
void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    gFailures += condition ? 0 : 1;
    std::printf("%s raster-residency-manifest.%s\n", condition ? "PASS" : "FAIL", name);
}

CanvasDocument Document(std::vector<ImageLayer> layers, int width = 2, int height = 2)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;
    document.layers = std::move(layers);
    return std::move(document).Seal();
}

RasterResidencyPlanOptions Options()
{
    RasterResidencyPlanOptions options;
    options.deviceGeneration = 7;
    return options;
}

ImageLayer Layer(const Raster& raster = {})
{
    auto layer = ImageLayer::Blank(L"Manifest literal", {2, 2});
    if (raster.HasStorage())
        layer.asset = RasterAsset{raster, {}, L"Manifest literal"};
    return layer;
}

auto Prepare(const CanvasDocument& source)
{
    const auto admission = RasterResidencyCpuAdmission::Estimate(source, Options());
    return RasterResidencyManifestWork::Prepare(source, admission);
}

std::size_t Count(const RasterResidencyManifestWork& manifest, RasterResidencyResourceKind kind)
{
    std::size_t count = 0;
    for (const auto& resource : manifest.Resources())
        count += resource.kind == kind ? 1 : 0;
    return count;
}

void PlainLayersDoNotRequireCoverageOrLookups()
{
    constexpr std::array<std::uint8_t, 16> pixels{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    const auto image = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const auto document = Document({Layer(image), Layer(image)});
    const auto admission = RasterResidencyCpuAdmission::Estimate(document, Options());
    auto manifest = RasterResidencyManifestWork::Prepare(document, admission);
    Expect(admission.IsValid() && manifest, "cpu-admission-and-worker-description");
    if (!manifest)
        return;
    Expect(manifest->Resources().size() == 3 && manifest->LogicalGpuBytes() == 60,
           "shared-2x2-bgra-20-plus-two-mipped-canvases-40-literal-60");
    Expect(manifest->RequiredGpuBytes() == 80ull * 1024 * 1024 + 60,
           "gpu-phase-adds-only-declared-16mib-staging-and-64mib-driver-allowances");
    Expect(manifest->Plan().Layouts().size() == 1 && manifest->Plan().Bindings().size() == 2 &&
               manifest->Plan().Layouts()[0].Source() == image,
           "deduplicated-layout-retains-owned-source-and-both-bindings");
    Expect(Count(*manifest, RasterResidencyResourceKind::Stack) == 0 &&
               Count(*manifest, RasterResidencyResourceKind::LayerCoverage) == 0 &&
               Count(*manifest, RasterResidencyResourceKind::GroupCoverage) == 0 &&
               Count(*manifest, RasterResidencyResourceKind::AdjustmentTable) == 0 &&
               Count(*manifest, RasterResidencyResourceKind::AdjustmentCube) == 0,
           "plain-records-require-no-stack-coverage-or-adjustment-allocation");
    std::vector<ImageLayer> many;
    many.reserve(kMaxLayerCount);
    for (int index = 0; index < kMaxLayerCount; ++index)
        many.push_back(Layer(image));
    auto largest = Prepare(Document(std::move(many)));
    Expect(largest && largest->Resources().size() == 3 && largest->LogicalGpuBytes() == 60 &&
               largest->Plan().Bindings().size() == 10000,
           "10000-shared-plain-layers-still-require-three-resources-and-60-logical-bytes");
}

void CoverageRolesAndLookupKinds()
{
    constexpr std::array<std::uint8_t, 4> maskPixels{0, 64, 128, 255};
    const auto mask = Raster::Make(2, 2, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());
    auto group = Layer();
    group.isGroup = true;
    group.mask = LayerMask{RasterAsset{mask, {}, L"Mask"}};
    auto first = Layer();
    auto second = Layer();
    first.maskSourceID = group.id;
    second.maskSourceID = group.id;
    auto manifest = Prepare(Document({group, first, second}));
    Expect(manifest && manifest->LogicalGpuBytes() == 85 && manifest->Resources().size() == 7,
           "mask-5-canvases-40-live-4-group-4-stack-32-literal-85");
    if (manifest)
    {
        Expect(Count(*manifest, RasterResidencyResourceKind::LayerCoverage) == 1 &&
                   Count(*manifest, RasterResidencyResourceKind::GroupCoverage) == 1 &&
                   Count(*manifest, RasterResidencyResourceKind::Stack) == 2,
               "duplicate-live-key-deduplicates-with-separate-group-role-and-stack-pair");
        unsigned int roles = 0;
        for (const auto& resource : manifest->Resources())
            if (resource.kind == RasterResidencyResourceKind::LayerCoverage ||
                resource.kind == RasterResidencyResourceKind::GroupCoverage)
                roles += resource.layerId == group.id && resource.format == RasterResidencyResourceFormat::Gray8 &&
                                 resource.width == 2 && resource.height == 2 && resource.mipLevels == 1
                             ? 1u
                             : 0u;
        Expect(roles == 2, "coverage-descriptors-retain-exact-role-id-format-grid-and-one-level");
    }
    auto levels = Layer();
    levels.adjustment = LayerAdjustment{};
    levels.adjustment->kind = AdjustmentKind::Levels;
    auto hsv = Layer();
    hsv.adjustment = LayerAdjustment{};
    hsv.adjustment->kind = AdjustmentKind::Hsv;
    auto grain = Layer();
    grain.adjustment = LayerAdjustment{};
    grain.adjustment->kind = AdjustmentKind::Grain;
    auto adjustments = Prepare(Document({levels, hsv, grain}));
    Expect(adjustments && adjustments->Resources().size() == 4 && adjustments->LogicalGpuBytes() == 579128,
           "canvases-40-table-4096-cube-574992-and-grain-zero-literal-579128");
    if (adjustments)
    {
        Expect(Count(*adjustments, RasterResidencyResourceKind::AdjustmentTable) == 1 &&
                   Count(*adjustments, RasterResidencyResourceKind::AdjustmentCube) == 1,
               "lut-and-cube-are-distinct-while-grain-has-no-lookup");
        const auto& table = adjustments->Resources()[2];
        const auto& cube = adjustments->Resources()[3];
        Expect(table.layerId == levels.id && table.width == 256 && table.height == 1 && table.depth == 1 &&
                   cube.layerId == hsv.id && cube.width == 33 && cube.height == 33 && cube.depth == 33 &&
                   table.format == RasterResidencyResourceFormat::Rgba32Float &&
                   cube.format == RasterResidencyResourceFormat::Rgba32Float,
               "table-and-cube-literal-dimensions-format-and-binding-ids");
    }
    group.mask->enabled = false;
    auto disabled = Prepare(Document({group}));
    Expect(disabled && disabled->LogicalGpuBytes() == 45 &&
               Count(*disabled, RasterResidencyResourceKind::GroupCoverage) == 0,
           "disabled-group-retains-mask-input-but-requires-no-coverage-target-literal-45");
    first.maskSourceID = Guid::Generate();
    second.maskSourceID = first.maskSourceID;
    auto missing = Prepare(Document({first, second}));
    Expect(missing && missing->LogicalGpuBytes() == 76 &&
               Count(*missing, RasterResidencyResourceKind::LayerCoverage) == 1,
           "missing-source-key-needs-one-blank-coverage-plus-conservative-stack-pair-literal-76");
}

void SparseSourceRemainsUnmaterialized()
{
    const Raster sparse = Raster::MakeSparse(6, 1, RasterFormat::Bgra8888, {}, 0, 0, {}, 91);
    CanvasDocument source = Document({Layer(sparse)}, 6, 1);
    auto options = Options();
    options.textureLimit = 2;
    const auto admission = RasterResidencyCpuAdmission::Estimate(source, options);
    const auto epoch = Raster::ResidencyEpoch();
    auto manifest = RasterResidencyManifestWork::Prepare(source, admission);
    const bool retainedUnmaterialized = manifest && !manifest->Plan().Source().layers[0].asset->image.IsMaterialized();
    Expect(sparse.IsSparse() && !sparse.IsMaterialized() && retainedUnmaterialized && Raster::ResidencyEpoch() == epoch,
           "sparse-image-and-retained-source-stay-unmaterialized");
}

void NarrowSourceAndCanvasUseIndependentScaleArithmetic()
{
    constexpr std::array<std::uint8_t, 24> pixels{};
    const auto image = Raster::Make(6, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    auto source = Document({Layer(image)}, 6, 1);
    auto options = Options();
    options.textureLimit = 2;
    const auto admission = RasterResidencyCpuAdmission::Estimate(source, options);
    auto manifest = RasterResidencyManifestWork::Prepare(source, admission);
    Expect(manifest && manifest->Plan().CanvasScale() == 4 && manifest->Resources().size() == 3,
           "6x1-source-and-canvas-reduce-by-literal-power-of-two-scale-4");
    if (manifest)
    {
        const auto resources = manifest->Resources();
        Expect(resources[0].kind == RasterResidencyResourceKind::Raster && resources[0].width == 2 &&
                   resources[0].height == 1 && resources[0].mipLevels == 2 && resources[0].logicalBytes == 12,
               "6x1-source-proxy-is-2x1-two-mips-and-12-literal-bytes");
        Expect(resources[1].kind == RasterResidencyResourceKind::Canvas &&
                   resources[2].kind == RasterResidencyResourceKind::Canvas && resources[1].width == 2 &&
                   resources[2].width == 2 && resources[1].height == 1 && resources[2].height == 1 &&
                   resources[1].mipLevels == 2 && resources[2].mipLevels == 2 && resources[1].logicalBytes == 12 &&
                   resources[2].logicalBytes == 12,
               "both-6x1-canvas-proxies-are-2x1-two-mips-and-12-literal-bytes");
    }
}

void EveryAdjustmentKindHasItsDeclaredLookupResource()
{
    constexpr std::array<AdjustmentKind, 6> kinds{AdjustmentKind::Levels,   AdjustmentKind::Curves,
                                                  AdjustmentKind::Exposure, AdjustmentKind::GradientMap,
                                                  AdjustmentKind::Hsv,      AdjustmentKind::Grain};
    std::vector<ImageLayer> layers;
    std::array<Guid, kinds.size()> ids{};
    for (std::size_t index = 0; index < kinds.size(); ++index)
    {
        auto layer = Layer();
        layer.adjustment = LayerAdjustment{};
        layer.adjustment->kind = kinds[index];
        ids[index] = layer.id;
        layers.push_back(std::move(layer));
    }
    auto manifest = Prepare(Document(std::move(layers)));
    Expect(manifest && manifest->Resources().size() == 7 && manifest->LogicalGpuBytes() == 591416,
           "four-256-entry-tables-one-33-cube-two-canvases-and-grain-literal-total-591416");
    if (!manifest)
        return;
    unsigned int tables = 0;
    unsigned int tableIds = 0;
    unsigned int cubes = 0;
    unsigned int grainResources = 0;
    for (const auto& resource : manifest->Resources())
    {
        if (resource.kind == RasterResidencyResourceKind::AdjustmentTable)
        {
            const bool knownTableLayer = resource.layerId == ids[0] || resource.layerId == ids[1] ||
                                         resource.layerId == ids[2] || resource.layerId == ids[3];
            for (unsigned int index = 0; index < 4; ++index)
                if (resource.layerId == ids[index])
                    tableIds |= 1u << index;
            tables += knownTableLayer && resource.width == 256 && resource.height == 1 && resource.depth == 1 &&
                              resource.mipLevels == 1 && resource.logicalBytes == 4096
                          ? 1u
                          : 0u;
        }
        else if (resource.kind == RasterResidencyResourceKind::AdjustmentCube)
        {
            cubes += resource.layerId == ids[4] && resource.width == 33 && resource.height == 33 &&
                             resource.depth == 33 && resource.mipLevels == 1 && resource.logicalBytes == 574992
                         ? 1u
                         : 0u;
        }
        grainResources += resource.layerId == ids[5] ? 1u : 0u;
    }
    Expect(tables == 4 && tableIds == 15 && cubes == 1 && grainResources == 0,
           "levels-curves-exposure-gradient-map-table-hsv-cube-grain-no-resource-with-layer-provenance");
}

void CoverageKeysKeepMissingSharedAndGroupRolesDistinct()
{
    constexpr std::array<std::uint8_t, 4> maskPixels{17, 53, 149, 241};
    const auto mask = Raster::Make(2, 2, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());
    auto group = Layer();
    group.isGroup = true;
    group.mask = LayerMask{RasterAsset{mask, {}, L"Coverage provenance"}};
    const Guid groupId = group.id;
    const Guid missingId = Guid::Generate();
    auto first = Layer();
    auto second = Layer();
    first.maskSourceID = groupId;
    second.maskSourceID = groupId;
    auto missingFirst = Layer();
    auto missingSecond = Layer();
    missingFirst.maskSourceID = missingId;
    missingSecond.maskSourceID = missingId;
    auto manifest = Prepare(Document({group, first, second, missingFirst, missingSecond}));
    Expect(manifest && manifest->Resources().size() == 8 && manifest->LogicalGpuBytes() == 89,
           "one-mask-two-shared-live-keys-one-group-key-and-stack-pair-total-eight-resources-89-bytes");
    if (!manifest)
        return;
    unsigned int groupRole = 0;
    unsigned int liveGroupKey = 0;
    unsigned int missingKey = 0;
    for (const auto& resource : manifest->Resources())
    {
        groupRole += resource.kind == RasterResidencyResourceKind::GroupCoverage && resource.layerId == groupId &&
                             resource.width == 2 && resource.height == 2 && resource.logicalBytes == 4
                         ? 1u
                         : 0u;
        liveGroupKey += resource.kind == RasterResidencyResourceKind::LayerCoverage && resource.layerId == groupId &&
                                resource.width == 2 && resource.height == 2 && resource.logicalBytes == 4
                            ? 1u
                            : 0u;
        missingKey += resource.kind == RasterResidencyResourceKind::LayerCoverage && resource.layerId == missingId &&
                              resource.width == 2 && resource.height == 2 && resource.logicalBytes == 4
                          ? 1u
                          : 0u;
    }
    Expect(groupRole == 1 && liveGroupKey == 1 && missingKey == 1,
           "shared-live-missing-and-group-coverage-preserve-role-and-source-id-provenance");
}

struct ManifestReleaseQueue final
{
    DeferredReleaseNode* node = nullptr;
    std::thread::id owner;
    std::thread::id enqueuedOn;
};

void EnqueueManifestRelease(void* context, DeferredReleaseNode& node) noexcept
{
    auto& queue = *static_cast<ManifestReleaseQueue*>(context);
    queue.node = &node;
    queue.enqueuedOn = std::this_thread::get_id();
}

void ManifestReleaseTransfersWholeSourceToJoinedWorker()
{
    auto largeImage = Raster::Make(2048, 2048, RasterFormat::Bgra8888);
    auto source = Document({Layer(largeImage)}, 2048, 2048);
    const auto sourceBytes = source.RetirementMemoryBytes();
    const auto admission = RasterResidencyCpuAdmission::Estimate(source, Options());
    auto manifest = RasterResidencyManifestWork::Prepare(source, admission);
    source = {};
    largeImage = {}; // the manifest is now the sole source graph owner
    ManifestReleaseQueue queue;
    queue.owner = std::this_thread::get_id();
    const bool installed = InstallDeferredReleaseSink(EnqueueManifestRelease, &queue);
    Expect(installed && manifest && sourceBytes >= 16ull * 1024 * 1024,
           "large-manifest-source-admitted-for-owner-deferred-release");
    if (!installed || !manifest)
    {
        if (installed)
            (void)ClearDeferredReleaseSink(&queue);
        return;
    }
    manifest.reset();
    const bool enqueuedOnOwner = queue.node != nullptr && queue.enqueuedOn == queue.owner;
    const auto retainedBytes = queue.node ? queue.node->bytes : 0;
    const bool completeCharge =
        queue.node && retainedBytes >= sourceBytes && retainedBytes >= kDeferredReleaseThreshold;
    std::thread::id destroyedOn;
    std::jthread worker(
        [&]()
        {
            if (queue.node && queue.node->destroy)
            {
                queue.node->destroy(queue.node);
                destroyedOn = std::this_thread::get_id();
            }
        });
    worker.join();
    const bool cleared = ClearDeferredReleaseSink(&queue);
    Expect(enqueuedOnOwner && completeCharge && destroyedOn != std::thread::id{} && destroyedOn != queue.owner,
           "manifest-intrusive-node-enqueued-with-whole-source-charge-and-destroyed-on-joined-worker");
    Expect(cleared, "manifest-release-sink-cleared-after-worker-drain");
}

struct CancelAtManifestPrefix final
{
    CancelSource* cancellation = nullptr;
    std::size_t target = 0;
    std::size_t reached = 0;
};

void CancelManifestAtPrefix(void* context, std::size_t resourceCount) noexcept
{
    auto& observer = *static_cast<CancelAtManifestPrefix*>(context);
    observer.reached = resourceCount;
    if (resourceCount == observer.target)
        (void)observer.cancellation->Cancel();
}

void CancellationAtEveryResourcePrefixPublishesNoManifest()
{
    constexpr std::array<std::uint8_t, 4> maskPixels{3, 67, 131, 251};
    const auto mask = Raster::Make(2, 2, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());
    auto group = Layer();
    group.isGroup = true;
    group.mask = LayerMask{RasterAsset{mask, {}, L"Cancellation mask"}};
    auto child = Layer();
    child.maskSourceID = group.id;
    child.adjustment = LayerAdjustment{};
    child.adjustment->kind = AdjustmentKind::Levels;
    const auto source = Document({group, child});
    const auto admission = RasterResidencyCpuAdmission::Estimate(source, Options());
    constexpr std::size_t resourceCount = 8; // mask raster, two canvases, two coverage roles, table, and two stacks
    unsigned int cancelledPrefixes = 0;
    unsigned int escapedManifests = 0;
    for (std::size_t prefix = 1; prefix <= resourceCount; ++prefix)
    {
        CancelSource cancellation;
        CancelAtManifestPrefix state{&cancellation, prefix, 0};
        auto manifest = RasterResidencyManifestWork::Prepare(
            source, admission, cancellation.Token(), RasterResidencyManifestObserver{CancelManifestAtPrefix, &state});
        cancelledPrefixes += state.reached == prefix && cancellation.Token().IsCancelled() ? 1u : 0u;
        escapedManifests += manifest ? 1u : 0u;
    }
    Expect(cancelledPrefixes == resourceCount, "observer-cancels-after-each-resource-prefix-including-final-resource");
    Expect(escapedManifests == 0, "cancelled-resource-prefixes-never-return-partial-manifest");
}

void RefusalAndCancellation()
{
    const auto source = Document({Layer()});
    const auto admission = RasterResidencyCpuAdmission::Estimate(source, Options());
    const auto other = Document({Layer()});
    Expect(!RasterResidencyManifestWork::Prepare(other, admission), "foreign-root-descriptor-refuses-before-output");
    Expect(!RasterResidencyManifestWork::Prepare(source, {}), "default-admission-refuses");
    CancelSource cancellation;
    (void)cancellation.Cancel();
    Expect(!RasterResidencyManifestWork::Prepare(source, admission, cancellation.Token()), "precancel-refuses");
    auto options = Options();
    options.deviceGeneration = 0;
    Expect(!RasterResidencyCpuAdmission::Estimate(source, options).IsValid(), "unknown-device-generation-refuses");
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    Expect(!RasterResidencyCpuAdmission::Estimate(source, Options(), {maximum, 0, 0, 0}).IsValid(),
           "cpu-overlap-overflow-refuses-before-capture");
    auto gpuOverlap = RasterResidencyCpuAdmission::Estimate(source, Options(), {0, 0, maximum, maximum});
    Expect(gpuOverlap.IsValid() && gpuOverlap.CpuBytes() == admission.CpuBytes(),
           "first-phase-cannot-charge-or-approve-gpu-overlap-owned-by-second-admission");
}
} // namespace

int RunRasterResidencyManifestTests()
{
    gChecks = 0;
    gFailures = 0;
    PlainLayersDoNotRequireCoverageOrLookups();
    CoverageRolesAndLookupKinds();
    SparseSourceRemainsUnmaterialized();
    NarrowSourceAndCanvasUseIndependentScaleArithmetic();
    EveryAdjustmentKindHasItsDeclaredLookupResource();
    CoverageKeysKeepMissingSharedAndGroupRolesDistinct();
    ManifestReleaseTransfersWholeSourceToJoinedWorker();
    CancellationAtEveryResourcePrefixPublishesNoManifest();
    RefusalAndCancellation();
    std::printf("raster-residency-manifest summary: %u checks, %u failures\n", gChecks, gFailures);
    return static_cast<int>(gFailures);
}
