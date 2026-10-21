#include "Document.Tests.RasterResidencyPlan.h"

#include "../../Common/RasterResidencyPlanWork.h"
#include "../Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::is_copy_constructible_v<RasterResidencyPlanWork>);
static_assert(!std::is_copy_assignable_v<RasterResidencyPlanWork>);
static_assert(!std::is_move_constructible_v<RasterResidencyPlanWork>);
static_assert(!std::is_move_assignable_v<RasterResidencyPlanWork>);

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s raster-residency-plan.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster Gray(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
}

ImageLayer Layer(const wchar_t* name, const Raster& image)
{
    ImageLayer layer =
        ImageLayer::Blank(name, Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    layer.asset = RasterAsset{image, Raster{}, name};
    return layer;
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

RasterResidencyPlanOptions Options(std::uint64_t generation = 7)
{
    RasterResidencyPlanOptions options;
    options.deviceGeneration = generation;
    return options;
}

void RepeatedImageDeduplicatesAndKeepsBindingOrder()
{
    constexpr std::array<std::uint8_t, 16> pixels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const Raster shared = Bgra(2, 2, pixels);
    ImageLayer first = Layer(L"first", shared);
    ImageLayer second = Layer(L"second", shared);
    first.visible = false;
    const Guid firstId = first.id;
    const Guid secondId = second.id;
    CanvasDocument source = Document({first, second});
    const auto admission = RasterResidencyAdmission::Estimate(source, Options());
    auto plan = RasterResidencyPlanWork::Prepare(source, admission);
    Expect(admission.IsValid() && plan != nullptr, "repeated-image-admission-and-prepare");
    Expect(plan && plan->Layouts().size() == 1 && plan->Bindings().size() == 2,
           "two-layers-share-one-layout-and-keep-two-bindings");
    Expect(plan && plan->Layouts().size() == 1 && plan->Layouts()[0].LogicalTextureBytes() == 20 &&
               plan->LogicalRasterBytes() == 20,
           "2x2-bgra-full-mip-logical-raster-literal-20");
    Expect(plan && plan->Bindings().size() == 2 && plan->Bindings()[0].layerId == firstId &&
               plan->Bindings()[0].layoutIndex == 0 && plan->Bindings()[0].role == RasterResidencyRole::Image &&
               plan->Bindings()[1].layerId == secondId && plan->Bindings()[1].layoutIndex == 0 &&
               plan->Bindings()[1].role == RasterResidencyRole::Image,
           "bindings-preserve-layer-order-id-layout-and-image-role-including-hidden-layer");
}

void SurfaceAndLookupAllowancesAreLiteral()
{
    constexpr std::array<std::uint8_t, 16> pixels{};
    const Raster shared = Bgra(2, 2, pixels);
    const CanvasDocument source = Document({Layer(L"one", shared), Layer(L"two", shared)});
    const auto admission = RasterResidencyAdmission::Estimate(source, Options());
    const auto plan = RasterResidencyPlanWork::Prepare(source, admission);
    Expect(plan && plan->CanvasScale() == 1, "small-canvas-scale-one");
    Expect(plan && plan->SurfaceAllowanceBytes() == 88,
           "surfaces-literal-40-mipped-canvas-plus-32-stack-plus-16-r8-record-coverage");
    Expect(plan && plan->LookupAllowanceBytes() == 1158176,
           "lookup-literal-two-layer-33-cube-and-256-entry-tables-1158176");
    Expect(plan && plan->Options().deviceGeneration == 7 && plan->Options().textureLimit == 16384,
           "prepared-plan-retains-generation-and-texture-options");
    // Staging and opaque driver values are conservative policy allowances, not observed allocations or a
    // responsiveness/actual-device-creation guarantee. Logical metadata is not a D3D allocation receipt.
    Expect(plan && plan->Options().stagingBytes == 16ull * 1024 * 1024 &&
               plan->Options().driverHeadroomBytes == 64ull * 1024 * 1024,
           "declared-staging-and-driver-policy-allowances-retained");
}

void GrayMaskUsesItsOwnTwoDimensionalLayout()
{
    constexpr std::array<std::uint8_t, 16> imagePixels{};
    constexpr std::array<std::uint8_t, 6> maskPixels{10, 20, 30, 40, 50, 60};
    ImageLayer layer = Layer(L"masked", Bgra(2, 2, imagePixels));
    const void* const imageIdentity = layer.asset->image.Identity();
    Raster maskRaster = Gray(3, 2, maskPixels);
    const void* const maskIdentity = maskRaster.Identity();
    layer.mask = LayerMask{RasterAsset{maskRaster, Raster{}, L"mask"}, true, std::nullopt, true};
    CanvasDocument source = Document({layer});
    const auto admission = RasterResidencyAdmission::Estimate(source, Options());
    auto prepared = RasterResidencyPlanWork::Prepare(source, admission);
    source = {};
    layer = {};
    maskRaster = {};
    Expect(prepared && prepared->Layouts().size() == 2 && prepared->Bindings().size() == 2,
           "image-and-mask-produce-two-layouts-and-bindings");
    Expect(prepared && prepared->Bindings().size() == 2 && prepared->Bindings()[0].role == RasterResidencyRole::Image &&
               prepared->Bindings()[1].role == RasterResidencyRole::Mask && prepared->Bindings()[0].layoutIndex == 0 &&
               prepared->Bindings()[1].layoutIndex == 1,
           "image-then-mask-binding-order-and-roles");
    Expect(prepared && prepared->Layouts()[0].Source().Identity() == imageIdentity &&
               prepared->Layouts()[1].Source().Identity() == maskIdentity,
           "plan-retains-independent-image-and-mask-source-identities-after-caller-release");
    Expect(prepared && prepared->Layouts().size() == 2 &&
               prepared->Layouts()[1].Source().Format() == RasterFormat::Gray8 && prepared->Layouts()[1].Width() == 3 &&
               prepared->Layouts()[1].Height() == 2 && prepared->Layouts()[1].MipLevels() == 2 &&
               prepared->Layouts()[1].LogicalTextureBytes() == 7,
           "gray-mask-retains-true-3x2-format-dimensions-and-mips");
}

void CropChunksKeepRowBoundaries()
{
    constexpr std::array<std::uint8_t, 252> pixels{};
    const Raster source = Bgra(9, 7, pixels);
    const auto layout = RasterUploadLayout::Make(source, 2, 1, 5, 4, 2, 16384, 12, 12, true);
    const auto first = layout.BaseChunkAt(0);
    const auto second = layout.BaseChunkAt(1);
    Expect(layout.IsValid() && layout.Width() == 3 && layout.Height() == 2 && layout.BaseChunkCount() == 2,
           "crop-scale-output-and-two-row-boundary-chunks");
    Expect(first.sourceX == 2 && first.sourceY == 1 && first.sourceWidth == 5 && first.sourceHeight == 4 &&
               first.scale == 2 && first.outputY == 0 && first.outputHeight == 1 && second.sourceX == first.sourceX &&
               second.sourceY == first.sourceY && second.sourceWidth == first.sourceWidth &&
               second.sourceHeight == first.sourceHeight && second.scale == first.scale && second.outputY == 1 &&
               second.outputHeight == 1,
           "crop-chunk-windows-retain-origin-scale-and-cover-whole-output-rows");
}

void AdmissionOverlapAndPolicyDeltasAreExact()
{
    constexpr std::array<std::uint8_t, 16> pixels{};
    const CanvasDocument source = Document({Layer(L"one", Bgra(2, 2, pixels))});
    auto options = Options();
    options.chunkBytes = 1024;
    options.stagingBytes = 4096;
    options.driverHeadroomBytes = 8192;
    const auto baseline = RasterResidencyAdmission::Estimate(source, options);
    auto stagingOnly = options;
    stagingOnly.stagingBytes = 3072;
    const auto withoutStaging = RasterResidencyAdmission::Estimate(source, stagingOnly);
    auto headroomOnly = options;
    headroomOnly.driverHeadroomBytes = 7168;
    const auto withoutHeadroom = RasterResidencyAdmission::Estimate(source, headroomOnly);
    Expect(baseline.IsValid() && withoutStaging.IsValid() && baseline.CpuBytes() - withoutStaging.CpuBytes() == 1024 &&
               baseline.GpuBytes() - withoutStaging.GpuBytes() == 1024,
           "staging-policy-charge-exactly-on-both-domains");
    Expect(baseline.IsValid() && withoutHeadroom.IsValid() &&
               baseline.CpuBytes() - withoutHeadroom.CpuBytes() == 1024 &&
               baseline.GpuBytes() - withoutHeadroom.GpuBytes() == 1024,
           "driver-headroom-policy-charge-exactly-on-both-domains");

    RasterResidencyOverlap overlap;
    overlap.liveCpuBytes = 11;
    overlap.retiredCpuBytes = 13;
    overlap.liveGpuBytes = 17;
    overlap.retiredGpuBytes = 19;
    const auto withOverlap = RasterResidencyAdmission::Estimate(source, options, overlap);
    Expect(withOverlap.IsValid() && withOverlap.CpuBytes() - baseline.CpuBytes() == 24,
           "live-and-retired-cpu-overlap-summed-exactly");
    Expect(withOverlap.IsValid() && withOverlap.GpuBytes() - baseline.GpuBytes() == 36,
           "live-and-retired-gpu-overlap-summed-exactly");
    overlap = {};
    overlap.liveCpuBytes = 31;
    const auto liveCpu = RasterResidencyAdmission::Estimate(source, options, overlap);
    overlap = {};
    overlap.liveGpuBytes = 37;
    const auto liveGpu = RasterResidencyAdmission::Estimate(source, options, overlap);
    Expect(liveCpu.IsValid() && liveCpu.CpuBytes() - baseline.CpuBytes() == 31 &&
               liveCpu.GpuBytes() == baseline.GpuBytes(),
           "live-cpu-overlap-charges-cpu-domain-only");
    Expect(liveGpu.IsValid() && liveGpu.GpuBytes() - baseline.GpuBytes() == 37 &&
               liveGpu.CpuBytes() == baseline.CpuBytes(),
           "live-gpu-overlap-charges-gpu-domain-only");
    overlap = {};
    overlap.retiredCpuBytes = 23;
    const auto retiredCpu = RasterResidencyAdmission::Estimate(source, options, overlap);
    overlap = {};
    overlap.retiredGpuBytes = 29;
    const auto retiredGpu = RasterResidencyAdmission::Estimate(source, options, overlap);
    Expect(retiredCpu.IsValid() && retiredCpu.CpuBytes() - baseline.CpuBytes() == 23 &&
               retiredCpu.GpuBytes() == baseline.GpuBytes(),
           "retired-cpu-overlap-charges-cpu-domain-only");
    Expect(retiredGpu.IsValid() && retiredGpu.GpuBytes() - baseline.GpuBytes() == 29 &&
               retiredGpu.CpuBytes() == baseline.CpuBytes(),
           "retired-gpu-overlap-charges-gpu-domain-only");
}

void SparseFillOnlyEstimateStaysCold()
{
    const Raster sparse = Raster::MakeSparse(8192, 5464, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0xA5);
    CanvasDocument source = Document({Layer(L"sparse", sparse)}, 8192, 5464);
    const std::uint64_t epoch = Raster::ResidencyEpoch();
    auto options = Options();
    options.textureLimit = 4096;
    const auto admission = RasterResidencyAdmission::Estimate(source, options);
    auto plan = RasterResidencyPlanWork::Prepare(std::move(source), admission);
    // Literal D3D levels: 4096x2732, 2048x1366, 1024x683, 512x341, 256x170, 128x85, 64x42,
    // 32x21, 16x10, 8x5, 4x2, 2x1, 1x1; sum of their BGRA bytes is 59,679,180 (floor halving).
    Expect(admission.IsValid() && plan && plan->Layouts().size() == 1 && plan->CanvasScale() == 2 &&
               plan->Layouts()[0].Width() == 4096 && plan->Layouts()[0].Height() == 2732 &&
               plan->Layouts()[0].BaseChunkAt(0).sourceX == 0 && plan->Layouts()[0].BaseChunkAt(0).sourceY == 0 &&
               plan->Layouts()[0].BaseChunkAt(0).sourceWidth == 8192 &&
               plan->Layouts()[0].BaseChunkAt(0).sourceHeight == 5464 &&
               plan->Layouts()[0].LogicalTextureBytes() == 59679180 && plan->LogicalRasterBytes() == 59679180,
           "8192x5464-full-source-power-two-reduction-and-mipped-output-literal");
    Expect(sparse.IsSparse() && !sparse.IsMaterialized() && plan && plan->Layouts().size() == 1 &&
               !plan->Layouts()[0].Source().IsMaterialized() && Raster::ResidencyEpoch() == epoch,
           "estimate-and-worker-enumeration-do-not-materialize-or-advance-residency-epoch");
}

void CallerReleaseAndChangedSourceRefusal()
{
    constexpr std::array<std::uint8_t, 16> firstPixels{};
    constexpr std::array<std::uint8_t, 16> secondPixels{1};
    CanvasDocument source = Document({Layer(L"same-bytes", Bgra(2, 2, firstPixels))});
    const auto admission = RasterResidencyAdmission::Estimate(source, Options());
    CanvasDocument captured = source;
    source = {};
    auto plan = RasterResidencyPlanWork::Prepare(std::move(captured), admission);
    Expect(plan && plan->Source().width == 2 && plan->Layouts().size() == 1,
           "prepared-plan-remains-valid-after-caller-releases-document");

    CanvasDocument original = Document({Layer(L"same-bytes", Bgra(2, 2, firstPixels))});
    CanvasDocument replacement = original;
    DocumentFixture replacementFixture = DocumentFixture::Copy(replacement);
    replacementFixture.layers = {Layer(L"same-bytes", Bgra(2, 2, secondPixels))};
    replacement = std::move(replacementFixture).Seal();
    const auto originalAdmission = RasterResidencyAdmission::Estimate(original, Options());
    Expect(original.RetirementMemoryBytes() == replacement.RetirementMemoryBytes(),
           "changed-root-control-has-identical-source-byte-count");
    Expect(!RasterResidencyPlanWork::Prepare(std::move(replacement), originalAdmission),
           "changed-immutable-root-refused-despite-equal-byte-count");
}

void RefusalCases()
{
    constexpr std::array<std::uint8_t, 16> pixels{};
    const CanvasDocument source = Document({Layer(L"source", Bgra(2, 2, pixels))});
    Expect(!RasterResidencyAdmission::Estimate(source, RasterResidencyPlanOptions{}).IsValid(),
           "default-zero-generation-refused");
    auto invalidGeneration = Options(0);
    Expect(!RasterResidencyAdmission::Estimate(source, invalidGeneration).IsValid(), "zero-generation-refused");
    auto invalidTextureLimit = Options();
    invalidTextureLimit.textureLimit = 0;
    Expect(!RasterResidencyAdmission::Estimate(source, invalidTextureLimit).IsValid(), "zero-texture-limit-refused");
    auto invalidChunk = Options();
    invalidChunk.chunkBytes = 0;
    Expect(!RasterResidencyAdmission::Estimate(source, invalidChunk).IsValid(), "zero-chunk-size-refused");
    auto invalidChunkAgainstStaging = Options();
    invalidChunkAgainstStaging.chunkBytes = invalidChunkAgainstStaging.stagingBytes + 1;
    Expect(!RasterResidencyAdmission::Estimate(source, invalidChunkAgainstStaging).IsValid(),
           "chunk-larger-than-declared-staging-refused");
    auto invalidRow = Options();
    invalidRow.maximumRowBytes = 0;
    Expect(!RasterResidencyAdmission::Estimate(source, invalidRow).IsValid(), "zero-maximum-row-bytes-refused");
    auto invalidHeadroom = Options();
    invalidHeadroom.driverHeadroomBytes = 0;
    Expect(!RasterResidencyAdmission::Estimate(source, invalidHeadroom).IsValid(), "zero-driver-headroom-refused");
    CanvasDocument invalidGeometry = source;
    invalidGeometry.width = 0;
    Expect(!RasterResidencyAdmission::Estimate(invalidGeometry, Options()).IsValid(),
           "nonpositive-canvas-geometry-refused");
    DocumentFixture excessiveLayers;
    excessiveLayers.width = 2;
    excessiveLayers.height = 2;
    excessiveLayers.layers.resize(static_cast<std::size_t>(kMaxLayerCount) + 1);
    const CanvasDocument excessive = std::move(excessiveLayers).Seal();
    Expect(!RasterResidencyAdmission::Estimate(excessive, Options()).IsValid(),
           "more-than-supported-10000-layer-records-refused-before-plan-allocation");
    RasterResidencyOverlap overflow;
    overflow.retiredGpuBytes = (std::numeric_limits<std::uint64_t>::max)();
    Expect(!RasterResidencyAdmission::Estimate(source, Options(), overflow).IsValid(),
           "overlap-addition-overflow-refused");
    auto cancelledSource = source;
    const auto cancelledAdmission = RasterResidencyAdmission::Estimate(cancelledSource, Options());
    std::stop_source stop;
    stop.request_stop();
    Expect(!RasterResidencyPlanWork::Prepare(std::move(cancelledSource), cancelledAdmission,
                                             CancelToken{stop.get_token()}),
           "pre-requested-cancellation-refuses-before-worker-allocation");
}

struct ReleaseQueue final
{
    DeferredReleaseNode* node = nullptr;
    std::thread::id owner;
    std::thread::id enqueuedOn;
};

void EnqueueRelease(void* context, DeferredReleaseNode& node) noexcept
{
    auto& queue = *static_cast<ReleaseQueue*>(context);
    queue.node = &node;
    queue.enqueuedOn = std::this_thread::get_id();
}

void PlanReleaseUsesAdmittedWorkerLane()
{
    constexpr std::array<std::uint8_t, 16> pixels{};
    CanvasDocument source = Document({Layer(L"release", Bgra(2, 2, pixels))});
    const auto admission = RasterResidencyAdmission::Estimate(source, Options());
    auto plan = RasterResidencyPlanWork::Prepare(std::move(source), admission);
    ReleaseQueue queue;
    queue.owner = std::this_thread::get_id();
    const bool installed = InstallDeferredReleaseSink(EnqueueRelease, &queue);
    Expect(installed, "always-on-owner-release-sink-installed");
    if (!installed)
        return;
    plan.reset();
    const bool queuedOnOwner = queue.node != nullptr && queue.enqueuedOn == queue.owner;
    const bool smallPlanWasStillDeferred = queue.node != nullptr && queue.node->bytes < kDeferredReleaseThreshold;
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
    Expect(queuedOnOwner && destroyedOn != std::thread::id{} && destroyedOn != queue.owner,
           "plan-retirement-is-enqueued-on-owner-and-destroyed-by-admitted-worker");
    Expect(smallPlanWasStillDeferred, "always-on-owner-policy-defers-plan-below-size-threshold");
    Expect(cleared, "release-sink-cleared-after-worker-drain");
}
} // namespace

int RunRasterResidencyPlanTests()
{
    gFailures = 0;
    gAssertions = 0;
    RepeatedImageDeduplicatesAndKeepsBindingOrder();
    SurfaceAndLookupAllowancesAreLiteral();
    GrayMaskUsesItsOwnTwoDimensionalLayout();
    CropChunksKeepRowBoundaries();
    AdmissionOverlapAndPolicyDeltasAreExact();
    SparseFillOnlyEstimateStaysCold();
    CallerReleaseAndChangedSourceRefusal();
    RefusalCases();
    PlanReleaseUsesAdmittedWorkerLane();
    std::printf("raster-residency-plan.assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
