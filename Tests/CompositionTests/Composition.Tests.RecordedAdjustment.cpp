#include "Composition.Tests.RecordedAdjustment.h"
#include "../Support/DocumentFixture.h"
#include "Adjust.h"
#include "GpuCommandPump.h"
#include "GpuWork.h"
#include "Jobs.h"
#include "OwnerWorkMultiplexer.h"
#include "RecordedDisplayGraph.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::size_t kGraphCount = 3;
constexpr std::uint64_t MiB = 1024ull * 1024;
// Fixture-only complete peak: source/root allowance 4 MiB + all three retained display/stack/mip texture sets
// 48 MiB + three graph/context/driver shadow owners 64 MiB + private deferred command transport 8 MiB +
// simultaneous blit target/readback storage 4 MiB. This is a declared harness reservation, not a production
// admission estimate or a VRAM guarantee.
constexpr std::uint64_t kFixtureParentCharge = 4 * MiB + 48 * MiB + 64 * MiB + 8 * MiB + 4 * MiB;
static_assert(kFixtureParentCharge == 128 * MiB);
unsigned int gFailures = 0;
unsigned int gCase = 0;
constexpr std::array<const char*, 8> kNames{"levels", "curves",           "exposure",      "gradient-map",
                                            "hsv",    "grain-half-scale", "live-exposure", "live-grain"};
constexpr std::array<FilterKind, 8> kFilters{FilterKind::Levels,      FilterKind::Curves,        FilterKind::Exposure,
                                             FilterKind::GradientMap, FilterKind::HueSaturation, FilterKind::Grain,
                                             FilterKind::Exposure,    FilterKind::Grain};
constexpr std::array<int, 8> kTolerances{1, 1, 1, 1, 4, 2, 1, 2};

enum class Phase : unsigned int
{
    Opening,
    Recording,
    StreamRetirement,
    UnpublishedRetirement,
    LastUseBarrier,
    GraphRetirement,
    Terminal
};

void Expect(const char* name, bool passed) noexcept
{
    std::printf("%s recorded-adjust-parent.%s.%s\n", passed ? "PASS" : "FAIL", kNames[gCase], name);
    std::fflush(stdout);
    std::fflush(stderr);
    if (!passed)
        ++gFailures;
}

Raster Solid(int width, int height, const std::array<std::uint8_t, 4>& bgra)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t offset = 0; offset < bytes.size(); offset += bgra.size())
        std::copy(bgra.begin(), bgra.end(), bytes.data() + offset);
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

CanvasDocument MakeDocument(int width, int height, Raster image)
{
    CanvasDocument document;
    document.width = width;
    document.height = height;
    RecordRoot<ImageLayer>::Builder layers(1);
    ImageLayer layer =
        ImageLayer::Blank(L"Recorded display fixture", Vec2{static_cast<double>(width), static_cast<double>(height)});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset = {std::move(image), {}, L"Recorded display fixture"};
    if (!layers.Append(std::move(layer)))
        return {};
    auto root = layers.Seal();
    if (root)
        document.layers = std::move(root.value());
    return document;
}

bool MakeLiveFixtures(std::array<CanvasDocument, kGraphCount>& documents,
                      std::optional<CanvasRenderer::AdjustmentPreview>& preview)
{
    LayerAdjustment original;
    original.kind = gCase == 6 ? AdjustmentKind::Exposure : AdjustmentKind::Grain;
    LayerAdjustment changed = original;
    if (gCase == 6)
        changed.exposureSettings = ExposureSettings{0.8, 0.04, 1.35};
    else
        changed.grainSettings = GrainSettings{58, 1.75, 42, 0x51a7u};
    const Raster source = Solid(6, 2, {40, 20, 10, 255});
    const Raster adjusted = Adjust::ApplyAdjustment(changed, source);
    const std::array<std::uint8_t, 12> samples{128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128};
    const Raster coverage = Raster::Make(6, 2, RasterFormat::Gray8, samples.data(), samples.size());
    const Raster expected = Adjust::BlendThrough(adjusted, source, coverage);
    const std::uint8_t tone = 128;
    const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8, &tone, 1);
    auto lookup = Adjust::BuildAdjustmentLookup(changed);
    if (!source.HasStorage() || !expected.HasStorage() || !mask.HasStorage() || !lookup)
        return false;
    CanvasDocument document = MakeDocument(6, 2, source);
    ImageLayer target = ImageLayer::Blank(L"Live adjustment", {6, 2});
    target.adjustment = original;
    target.mask = LayerMask{{mask, mask, L"Half coverage"}, true, std::nullopt, true};
    const Guid targetId = target.id;
    std::vector<std::uint8_t> upper(6 * 2 * 4);
    const std::array<std::uint8_t, 4> green{20, 240, 20, 255};
    std::copy(green.begin(), green.end(), upper.begin() + 2 * 4);
    ImageLayer above = ImageLayer::Blank(L"Unaffected upper", {6, 2});
    above.transform.sampling = LayerSampling::Nearest;
    above.asset = {Raster::Make(6, 2, RasterFormat::Bgra8888, upper.data(), upper.size()), {}, L"Upper"};
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.push_back(std::move(target));
                            fixture.layers.push_back(std::move(above));
                        });
    documents[0] = document;
    documents[1] = document;
    documents[2] = document;
    EditDocumentFixture(documents[2],
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers[0].asset->image = expected;
                            fixture.layers[1].visible = false;
                        });
    preview = CanvasRenderer::AdjustmentPreview{targetId, {}, {}, std::move(lookup), original, 1};
    return true;
}

bool MakeFixtures(std::array<CanvasDocument, kGraphCount>& documents,
                  std::optional<CanvasRenderer::AdjustmentPreview>& preview)
{
    if (gCase >= 6)
        return MakeLiveFixtures(documents, preview);
    LayerAdjustment adjustment;
    switch (gCase)
    {
    case 0:
        adjustment.kind = AdjustmentKind::Levels;
        Adjust::SetLevelsCurrent(adjustment.levels, LevelRange{8, 1.35, 245, 4, 250});
        break;
    case 1:
        adjustment.kind = AdjustmentKind::Curves;
        adjustment.curves.channels[0] = {{0, 12}, {96, 150}, {255, 240}};
        adjustment.curves.channels[3] = {{0, 28}, {255, 210}};
        break;
    case 2:
        adjustment.kind = AdjustmentKind::Exposure;
        adjustment.exposureSettings = ExposureSettings{0.8, 0.04, 1.35};
        break;
    case 3:
        adjustment.kind = AdjustmentKind::GradientMap;
        adjustment.gradientMapSettings = GradientMapSettings{{0.85, 0.08, 0.18}, {0.12, 0.35, 0.95}, false};
        break;
    case 4:
        adjustment.kind = AdjustmentKind::Hsv;
        adjustment.hsvSettings = Adjust::HsvSettings(72, 28, -8);
        break;
    case 5:
        adjustment.kind = AdjustmentKind::Grain;
        adjustment.grainSettings = GrainSettings{58, 1.75, 42, 0x51a7u};
        break;
    default:
        return false;
    }
    // Literal partial-alpha pixels and selection/mask coverage; CPU filter and BlendThrough remain the
    // independent reference. The final reference graph uses only ordinary composition layer passes.
    const std::array<std::uint8_t, 16> pixels{20, 40, 200, 255, 30, 10, 120, 160, 0, 0, 0, 0, 10, 60, 100, 128};
    const std::array<std::uint8_t, 4> selected{255, 128, 0, 192};
    const std::array<std::uint8_t, 4> masked{255, 192, 128, 64};
    const Raster source = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const Raster coverage = Raster::Make(2, 2, RasterFormat::Gray8, selected.data(), selected.size());
    const Raster mask = Raster::Make(2, 2, RasterFormat::Gray8, masked.data(), masked.size());
    auto lookup = Adjust::BuildAdjustmentLookup(adjustment);
    const double scale = gCase == 5 ? 0.5 : 1;
    const Raster adjusted = Adjust::RunFilter(kFilters[gCase], source, Adjust::AdjustmentSettings(adjustment), scale,
                                              adjustment.Grain().seed);
    const Raster expected = Adjust::BlendThrough(adjusted, source, coverage);
    if (!source.HasStorage() || !coverage.HasStorage() || !mask.HasStorage() || !lookup || !expected.HasStorage())
        return false;
    CanvasDocument document = MakeDocument(6, 2, Solid(6, 2, {40, 20, 10, 255}));
    ImageLayer target = ImageLayer::Blank(L"Selected preview target", {4, 2});
    target.transform.origin = {1, 0};
    target.transform.sampling = LayerSampling::Nearest;
    target.opacity = 0.75;
    target.asset = {source, {}, L"Selected preview target"};
    target.mask = LayerMask{{mask, mask, L"Placed mask"}, true, std::nullopt, true};
    std::vector<std::uint8_t> upper(6 * 2 * 4);
    const std::array<std::uint8_t, 4> green{20, 240, 20, 255};
    std::copy(green.begin(), green.end(), upper.begin() + 2 * 4);
    ImageLayer above = ImageLayer::Blank(L"Unaffected upper", {6, 2});
    above.transform.sampling = LayerSampling::Nearest;
    above.asset = {Raster::Make(6, 2, RasterFormat::Bgra8888, upper.data(), upper.size()), {}, L"Upper"};
    const Guid targetId = target.id;
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.push_back(std::move(target));
                            fixture.layers.push_back(std::move(above));
                        });
    documents[0] = document;
    documents[1] = document;
    documents[2] = document;
    EditDocumentFixture(documents[2], [&](DocumentFixture& fixture) { fixture.layers[1].asset->image = expected; });
    preview = CanvasRenderer::AdjustmentPreview{targetId, source, coverage, std::move(lookup), {}, scale};
    return true;
}

bool PixelsEqualWithin(const std::vector<std::uint8_t>& actual, const std::vector<std::uint8_t>& expected,
                       int tolerance) noexcept
{
    if (actual.size() != expected.size() || actual.empty())
        return false;
    for (std::size_t offset = 0; offset < actual.size(); ++offset)
        if (std::abs(static_cast<int>(actual[offset]) - expected[offset]) > tolerance)
            return false;
    return true;
}

void PrintPixels(const char* label, const std::vector<std::uint8_t>& pixels) noexcept
{
    std::printf("INFO recorded-adjust-parent.%s.%s BGRA", kNames[gCase], label);
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4)
        std::printf(" (%u,%u,%u,%u)", pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]);
    std::printf("\n");
}

bool CheckRefusalsForWork(RecordedDisplayGraph& graph, ID3D11Device* device, const CanvasDocument& document,
                          const CanvasRenderer::AdjustmentPreview& valid, GpuCommandStream& stream)
{
    const auto refuse = [&](const char* name, const CanvasRenderer::AdjustmentPreview& preview,
                            CancelToken cancellation = {}, HRESULT expected = E_INVALIDARG)
    {
        const HRESULT result = graph.PrepareAdjustmentForWork(device, document, preview, stream, cancellation);
        const bool passed = result == expected && graph.IsUnpreparedForTest();
        Expect(name, passed);
        return passed;
    };
    CancelSource cancelled;
    (void)cancelled.Cancel();
    if (!refuse("pre-cancel-refuses-without-resources", valid, cancelled.Token(), E_ABORT))
        return false;
    auto bad = valid;
    bad.previewScale = 0;
    if (!refuse("zero-scale-refuses-without-resources", bad))
        return false;
    bad.previewScale = (std::numeric_limits<double>::infinity)();
    if (!refuse("nonfinite-scale-refuses-without-resources", bad))
        return false;
    bad = valid;
    bad.layerId = {};
    if (!refuse("missing-target-refuses-without-resources", bad))
        return false;
    auto unsupported = std::make_shared<AdjustmentLookup>(*valid.lookup);
    unsupported->adjustment.kind = static_cast<AdjustmentKind>(255);
    bad = valid;
    bad.lookup = std::move(unsupported);
    if (!refuse("unknown-kind-refuses-without-resources", bad))
        return false;
    if (valid.liveOriginal)
    {
        bad = valid;
        bad.liveOriginal.value().exposureSettings = ExposureSettings{0.5, 0, 1};
        if (!refuse("changed-original-refuses-without-resources", bad))
            return false;
        bad = valid;
        bad.source = Solid(1, 1, {0, 0, 0, 255});
        return refuse("live-preview-with-raster-refuses-without-resources", bad);
    }
    bad = valid;
    bad.lookup.reset();
    if (!refuse("missing-lookup-refuses-without-resources", bad))
        return false;
    bad = valid;
    bad.source = Solid(2, 2, {0, 0, 0, 255});
    if (!refuse("different-source-identity-refuses-without-resources", bad))
        return false;
    bad = valid;
    bad.coverage = Solid(2, 2, {0, 0, 0, 255});
    if (!refuse("non-gray-selection-refuses-without-resources", bad))
        return false;
    const std::uint8_t tone = 255;
    bad.coverage = Raster::Make(1, 1, RasterFormat::Gray8, &tone, 1);
    if (!refuse("wrong-selection-grid-refuses-without-resources", bad))
        return false;
    auto incompleteCube = std::make_shared<AdjustmentLookup>();
    incompleteCube->adjustment.kind = AdjustmentKind::Hsv;
    bad = valid;
    bad.lookup = std::move(incompleteCube);
    return refuse("incomplete-cube-refuses-without-resources", bad);
}

class ParentState final : public GpuCommandSourceReceiver, public GpuCompletionReceiver
{
  public:
    ParentState(JobScheduler& scheduler, GpuWorkQueue& queue, GpuCommandPump& pump, ID3D11Device* device,
                ID3D11DeviceContext* immediate) noexcept
        : scheduler(scheduler), queue(queue), pump(pump), device(device), immediate(immediate),
          deviceGeneration(queue.DeviceGeneration())
    {
    }

    JobScheduler& scheduler;
    GpuWorkQueue& queue;
    GpuCommandPump& pump;
    ID3D11Device* const device;
    ID3D11DeviceContext* const immediate;
    const std::uint64_t deviceGeneration;
    std::array<CanvasDocument, kGraphCount> documents;
    std::optional<CanvasRenderer::AdjustmentPreview> preview;
    std::array<RecordedDisplayGraph, kGraphCount> graphs;
    std::array<GpuCommandStream, kGraphCount> streams;
    std::array<GpuCompletionContext, kGraphCount> retiredCapture;
    GpuCompletionContext lastUseCapture;
    std::array<bool, kGraphCount> streamInitialized{};
    std::array<bool, kGraphCount> streamAuthorized{};
    std::array<bool, kGraphCount> streamRetired{};
    std::array<bool, kGraphCount> graphDetached{};
    std::array<bool, kGraphCount> graphRetired{};
    GpuCommandSourceLease sourceLease{};
    JobTicket original{};
    Phase phase = Phase::Opening;
    JobResult workerResult{};
    bool stageAcknowledged = false;
    bool producerJoined = false;
    bool sourceAttached = false;
    bool sourceFailed = false;
    HRESULT sourceError = S_OK;
    unsigned int originalAcknowledgements = 0;
    bool lastUseAcknowledged = false;
    HRESULT lastUseResult = E_PENDING;
    bool blitsPassed = false;
    bool terminal = false;
    bool completedSuccessfully = false;
    bool cleanupPassed = true;
    std::size_t index = 0;

    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT result) noexcept override
    {
        Expect("gpu-callback-carries-original-ticket",
               ticket == original && command.source == original && command.deviceGeneration == deviceGeneration);
        if (ticket != original || command.source != original || command.deviceGeneration != deviceGeneration)
        {
            sourceFailed = true;
            sourceError = E_INVALIDARG;
            return;
        }
        if (SUCCEEDED(result))
            ++originalAcknowledgements;
        else
        {
            sourceFailed = true;
            sourceError = result;
        }
    }

    void CommandSourceFailedOwner(JobTicket ticket, HRESULT error) noexcept override
    {
        Expect("source-failure-is-not-an-acknowledgement", ticket == original && FAILED(error));
        sourceFailed = true;
        sourceError = FAILED(error) ? error : E_UNEXPECTED;
    }

    void GpuCompletionReady(JobTicket ticket, std::uint64_t generation, HRESULT result) noexcept override
    {
        Expect("post-blit-barrier-is-captured-original-stream", ticket == original && generation == deviceGeneration);
        lastUseAcknowledged = ticket == original && generation == deviceGeneration;
        lastUseResult = result;
        if (FAILED(result))
            sourceFailed = true;
    }

    [[nodiscard]] bool StartStream(std::size_t graphIndex) noexcept
    {
        auto& stream = streams[graphIndex];
        const HRESULT initialized = stream.InitializeOwner(scheduler, original, deviceGeneration);
        if (FAILED(initialized))
        {
            sourceFailed = true;
            sourceError = initialized;
            return false;
        }
        streamInitialized[graphIndex] = true;
        const HRESULT attached = pump.AttachOwner(stream, original, *this, sourceLease);
        if (FAILED(attached))
        {
            sourceFailed = true;
            sourceError = attached;
            return false;
        }
        sourceAttached = true;
        producerJoined = false;
        return true;
    }

    void DetachGraphsForRetirement() noexcept
    {
        for (std::size_t graph = 0; graph < kGraphCount; ++graph)
        {
            if (!graphDetached[graph] && !graphRetired[graph])
            {
                graphDetached[graph] = graphs[graph].DetachOwner();
                cleanupPassed = cleanupPassed && graphDetached[graph];
            }
        }
    }

    void BeginGraphRetirement() noexcept
    {
        DetachGraphsForRetirement();
        phase = Phase::GraphRetirement;
        stageAcknowledged = false;
        Expect("same-original-ticket-resumes-for-graph-retirement",
               scheduler.Resume(original, JobExecutionLane::Compute));
    }

    void AdvanceOwner() noexcept
    {
        if (phase == Phase::Recording && producerJoined && streamInitialized[index] && streams[index].CanRetireOwner())
        {
            HRESULT producerStatus = E_PENDING;
            const bool finished = streams[index].ProducerFinishedOwner(producerStatus);
            Expect("joined-producer-and-stream-terminal-before-detach", finished);
            if (!finished)
                return;

            if (sourceAttached)
            {
                const bool detached = pump.DetachOwner(sourceLease, retiredCapture[index]);
                Expect("pump-capture-detaches-after-actual-original-acks", detached);
                if (!detached)
                    return;
                sourceAttached = false;
                sourceLease = {}; // Detach accepts the lease by value; the next Attach requires an empty output.
            }
            const bool authorized = streams[index].AuthorizeRetirementOwner();
            Expect("stream-retirement-authorized-after-real-acks", authorized);
            if (!authorized)
                return;
            streamAuthorized[index] = true;
            phase = Phase::StreamRetirement;
            Expect("same-ticket-worker-retires-captured-transport",
                   scheduler.Resume(original, JobExecutionLane::Compute));
            return;
        }

        if (phase == Phase::LastUseBarrier && lastUseAcknowledged)
        {
            if (lastUseResult != S_OK)
                return; // Keep all graphs and the original charge until joined shutdown destruction.
            Expect("last-use-barrier-is-successful-before-graph-detach", true);
            BeginGraphRetirement();
            return;
        }

        if (!stageAcknowledged)
            return;
        stageAcknowledged = false;

        if (phase == Phase::Opening)
        {
            original = workerTicket;
            if (workerResult.status != JobStatus::Finished)
            {
                BeginGraphRetirement();
                return;
            }
            const HRESULT refused = graphs[1].PrepareAdjustmentForWork(device, {}, {}, streams[1], {});
            Expect("owner-refuses-before-metadata-or-resource-preparation",
                   refused == E_INVALIDARG && graphs[1].IsUnpreparedForTest());
            index = 0;
            if (!StartStream(index))
            {
                // The initialized stream has no producer yet. Resume its real recorder stage so it publishes
                // a failed terminal state; an uninitialized stream has no resources to retire.
                if (streamInitialized[index])
                {
                    phase = Phase::Recording;
                    workerResult = {JobStatus::Failed, static_cast<std::int32_t>(sourceError)};
                    Expect("failed-attach-still-finishes-original-stream-on-worker",
                           scheduler.Resume(original, JobExecutionLane::GpuRecording));
                }
                else
                {
                    BeginGraphRetirement();
                }
                return;
            }
            phase = Phase::Recording;
            Expect("opening-ack-precedes-recording-lane-resume",
                   scheduler.Resume(original, JobExecutionLane::GpuRecording));
            return;
        }

        if (phase == Phase::Recording)
        {
            producerJoined = true;
            if (workerResult.status != JobStatus::Finished)
            {
                sourceFailed = true;
                if (workerResult.status == JobStatus::Failed)
                    sourceError = static_cast<HRESULT>(workerResult.error);
            }
            return; // AdvanceOwner waits for stream.CanRetireOwner and every executing callback.
        }

        if (phase == Phase::StreamRetirement)
        {
            if (!streamRetired[index])
            {
                cleanupPassed = false;
                sourceFailed = true;
            }
            const bool graphCanPublish = workerResult.status == JobStatus::Finished && !sourceFailed &&
                                         streams[index].CanRetireOwner() && streamRetired[index];
            if (!graphCanPublish)
            {
                if (!streamRetired[index] || !streams[index].CanRetireOwner())
                {
                    // Leave the graph attached until joined shutdown retires the actual stream first.
                    BeginGraphRetirement();
                    return;
                }
                phase = Phase::UnpublishedRetirement;
                stageAcknowledged = false;
                Expect("same-ticket-retires-unpublished-graph-after-original-stream",
                       scheduler.Resume(original, JobExecutionLane::Compute));
                return;
            }

            const HRESULT published = graphs[index].PublishOwner(immediate);
            Expect("publish-only-after-joined-recording-and-retired-original-stream", SUCCEEDED(published));
            if (FAILED(published))
            {
                sourceFailed = true;
                phase = Phase::UnpublishedRetirement;
                stageAcknowledged = false;
                Expect("publish-failure-retains-worker-only-graph-retirement",
                       scheduler.Resume(original, JobExecutionLane::Compute));
                return;
            }
            const bool exactPublished = graphs[index].IsPublishedOnOwner(device, immediate);
            Expect("published-graph-matches-actual-device-and-original-context", exactPublished);
            if (!exactPublished)
            {
                sourceFailed = true;
                BeginGraphRetirement();
                return;
            }
            ++index;
            if (index < kGraphCount)
            {
                if (!StartStream(index))
                {
                    if (streamInitialized[index])
                    {
                        phase = Phase::Recording;
                        workerResult = {JobStatus::Failed, static_cast<std::int32_t>(sourceError)};
                        Expect("later-attach-failure-finishes-stream-on-original-job",
                               scheduler.Resume(original, JobExecutionLane::GpuRecording));
                    }
                    else
                    {
                        BeginGraphRetirement();
                    }
                    return;
                }
                phase = Phase::Recording;
                Expect("next-graph-uses-same-original-ticket-and-recording-lane",
                       scheduler.Resume(original, JobExecutionLane::GpuRecording));
                return;
            }

            const bool allPublished = std::all_of(graphs.begin(), graphs.end(), [this](const auto& graph) noexcept
                                                  { return graph.IsPublishedOnOwner(device, immediate); });
            Expect("all-three-exact-graphs-published-before-blit-tests", allPublished);
            if (!allPublished)
            {
                sourceFailed = true;
                BeginGraphRetirement();
                return;
            }
            std::array<std::vector<std::uint8_t>, kGraphCount> pixels;
            bool read = true;
            for (std::size_t graph = 0; graph < kGraphCount; ++graph)
            {
                unsigned int width = 0, height = 0;
                const HRESULT status = graphs[graph].CompositionForTestOwner()->Readback(pixels[graph], width, height);
                read = read && SUCCEEDED(status) && width == 6 && height == 2;
            }
            Expect("three-acknowledged-composites-read-at-original-resolution", read);
            const bool reference = read && PixelsEqualWithin(pixels[1], pixels[2], kTolerances[gCase]);
            Expect("selected-placed-masked-preview-matches-cpu-reference", reference);
            if (!reference)
            {
                PrintPixels("actual", pixels[1]);
                PrintPixels("cpu-reference", pixels[2]);
            }
            bool unaffected = read;
            if (read)
                for (const auto offset : {std::size_t{0}, std::size_t{8}, std::size_t{44}})
                    for (std::size_t channel = 0; channel < 4; ++channel)
                        if (gCase < 6 || offset == 8) // Live adjustment intentionally changes its lower composite.
                            unaffected = unaffected && pixels[0][offset + channel] == pixels[1][offset + channel];
            Expect("unaffected-background-or-opaque-upper-pixels-are-unchanged", unaffected);
            unsigned int width = 0, height = 0;
            std::vector<std::uint8_t> seedAgain;
            const bool immutableSeed =
                read && SUCCEEDED(graphs[0].CompositionForTestOwner()->Readback(seedAgain, width, height)) &&
                seedAgain == pixels[0];
            Expect("published-seed-pixels-remain-unchanged", immutableSeed);
            blitsPassed = read && reference && unaffected && immutableSeed;
            const HRESULT captured = queue.CaptureCompletionContext(lastUseCapture);
            Expect("capture-original-context-after-recorded-blits", SUCCEEDED(captured));
            if (FAILED(captured))
            {
                sourceFailed = true;
                return; // No barrier means no graph detach; joined shutdown owns destruction.
            }
            const HRESULT barrier = queue.EnqueueRetirementCompletion(lastUseCapture, original, *this);
            Expect("enqueue-real-post-blit-original-completion-barrier", SUCCEEDED(barrier));
            if (FAILED(barrier))
            {
                sourceFailed = true;
                return; // Refusal is not a GPU acknowledgement.
            }
            phase = Phase::LastUseBarrier;
            return;
        }

        if (phase == Phase::UnpublishedRetirement)
        {
            if (workerResult.status != JobStatus::Finished || !graphRetired[index])
                cleanupPassed = false;
            BeginGraphRetirement();
            return;
        }
    }

    JobTicket workerTicket{};

    void ShutdownOwnerBeforeQueueClose() noexcept
    {
        pump.AcknowledgeJoinedShutdownOwner();
        if (sourceAttached && index < kGraphCount)
        {
            const bool detached = pump.DetachOwner(sourceLease, retiredCapture[index]);
            cleanupPassed = cleanupPassed && detached;
            sourceAttached = !detached;
            if (detached)
                sourceLease = {};
        }
        for (std::size_t graph = 0; graph < kGraphCount; ++graph)
        {
            if (streamInitialized[graph] && !streamAuthorized[graph] && streams[graph].CanRetireOwner())
                streamAuthorized[graph] = streams[graph].AuthorizeRetirementOwner();
        }
    }

    void ShutdownGraphsOwner() noexcept
    {
        DetachGraphsForRetirement();
    }

    void ShutdownWorker() noexcept
    {
        for (std::size_t graph = 0; graph < kGraphCount; ++graph)
        {
            if (streamInitialized[graph] && streamAuthorized[graph] && !streamRetired[graph])
                streamRetired[graph] = streams[graph].RetireForWork();
            retiredCapture[graph] = {};
            if (graphDetached[graph] && !graphRetired[graph])
                graphRetired[graph] = graphs[graph].RetireForWork();
            cleanupPassed = cleanupPassed && (!streamInitialized[graph] || streamRetired[graph]) && graphRetired[graph];
        }
        preview.reset(); // Admitted source/lookup owners release after all uses on this actual worker.
        documents = {};
        lastUseCapture = {};
    }
};

class RecordedBlitParentJob final : public Job
{
  public:
    explicit RecordedBlitParentJob(ParentState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        if (_state.phase == Phase::Opening)
            return MakeFixtures(_state.documents, _state.preview) ? JobResult{}
                                                                  : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
        if (_state.phase == Phase::Recording)
        {
            if (!_state.sourceAttached)
                return {JobStatus::Failed, static_cast<std::int32_t>(_state.sourceError)};
            if (_state.index == 1 && (gCase == 0 || gCase == 6) &&
                !CheckRefusalsForWork(_state.graphs[1], _state.device, _state.documents[1], _state.preview.value(),
                                      _state.streams[1]))
                return {JobStatus::Failed, E_UNEXPECTED};
            const auto* seed = _state.index ? &_state.graphs[0] : nullptr;
            const HRESULT result = _state.index == 1
                                       ? _state.graphs[1].PrepareAdjustmentForWork(
                                             _state.device, std::move(_state.documents[1]), _state.preview.value(),
                                             _state.streams[1], context.cancellation, seed)
                                       : _state.graphs[_state.index].PrepareForWork(
                                             _state.device, std::move(_state.documents[_state.index]),
                                             _state.streams[_state.index], context.cancellation, seed);
            return FAILED(result) ? JobResult{JobStatus::Failed, static_cast<std::int32_t>(result)} : JobResult{};
        }
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_state.phase == Phase::Opening && result.status != JobStatus::Finished)
        {
            _state.documents = {};
        }
        else if (_state.phase == Phase::Recording && _state.streamInitialized[_state.index])
        {
            const HRESULT producer = result.status == JobStatus::Finished    ? S_OK
                                     : result.status == JobStatus::Cancelled ? E_ABORT
                                                                             : static_cast<HRESULT>(result.error);
            _state.streams[_state.index].FinishProducerForWork(producer);
        }
        else if (_state.phase == Phase::StreamRetirement)
        {
            _state.streamRetired[_state.index] = _state.streams[_state.index].RetireForWork();
            _state.retiredCapture[_state.index] = {};
        }
        else if (_state.phase == Phase::UnpublishedRetirement)
        {
            _state.graphRetired[_state.index] = _state.graphs[_state.index].RetireUnpublishedForWork();
        }
        else if (_state.phase == Phase::GraphRetirement)
        {
            _state.ShutdownWorker();
        }
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        _state.workerTicket = ticket;
        if (_state.phase == Phase::Opening && result.status == JobStatus::Failed)
            return false;
        if (_state.phase == Phase::GraphRetirement)
            return false; // The worker retirement hook ran before this owner acknowledgement.
        _state.workerResult = result;
        _state.stageAcknowledged = true;
        return true;
    }

    void Completed(JobResult result) noexcept override
    {
        _state.completedSuccessfully = result.status == JobStatus::Finished && _state.blitsPassed &&
                                       _state.lastUseAcknowledged && _state.lastUseResult == S_OK &&
                                       _state.cleanupPassed;
        Expect("complete-reservation-still-held-through-final-worker-retirement",
               _state.scheduler.ReservedMemoryBytes() == kFixtureParentCharge);
        _state.terminal = true;
        _state.phase = Phase::Terminal;
        Expect("original-charge-refunded-only-after-terminal-worker-retirement", _state.completedSuccessfully);
    }

  private:
    ParentState& _state;
};

bool WaitForParent(JobScheduler& scheduler, OwnerWorkMultiplexer& routes, ParentState& state) noexcept
{
    const ULONGLONG started = GetTickCount64();
    while (!state.terminal)
    {
        (void)DrainCompletions(scheduler); // Also pumps one ready owner source through the real router.
        state.AdvanceOwner();
        if (state.terminal)
            break;
        if (routes.HasReady() || state.stageAcknowledged ||
            (state.phase == Phase::Recording && state.producerJoined && state.streams[state.index].CanRetireOwner()) ||
            (state.phase == Phase::LastUseBarrier && state.lastUseAcknowledged && state.lastUseResult == S_OK))
            continue;
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= 10000)
            return false;
        const DWORD wait = static_cast<DWORD>(10000 - elapsed); // Failure bound only; readiness is event driven.
        if (WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), wait) != WAIT_OBJECT_0)
            return false;
    }
    return state.completedSuccessfully;
}

bool RunDevice(D3D_DRIVER_TYPE driver, bool optionalHardware)
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT result = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                       D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    if (FAILED(result))
    {
        Expect(optionalHardware ? "hardware-device-unavailable-skipped" : "warp-device-created", optionalHardware);
        return optionalHardware;
    }
    D3D11_FEATURE_DATA_THREADING threading{};
    result = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    if (FAILED(result) || !threading.DriverConcurrentCreates)
    {
        Expect(optionalHardware ? "hardware-concurrent-create-unavailable-skipped"
                                : "warp-supports-worker-resource-creation",
               optionalHardware);
        return optionalHardware;
    }

    JobScheduler scheduler;
    result = static_cast<HRESULT>(scheduler.Initialize(1));
    if (SUCCEEDED(result))
        result = static_cast<HRESULT>(scheduler.EnableGpuRecordingLane());
    if (FAILED(result))
    {
        Expect("scheduler-and-single-recorder-lane-initialize", false);
        scheduler.Close();
        return false;
    }

    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump pump;
    bool routed = scheduler.RegisterOwnerWork(routes);
    result = routed ? queue.Initialize(scheduler, &routes) : E_FAIL;
    if (SUCCEEDED(result))
        result = queue.Bind(device.get(), immediate.get());
    if (SUCCEEDED(result))
        result = pump.InitializeOwner(scheduler, queue, routes);
    if (FAILED(result))
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        (void)pump.CloseOwner();
        Expect("actual-original-gpu-queue-and-command-pump-initialize", false);
        return false;
    }

    ParentState state{scheduler, queue, pump, device.get(), immediate.get()};
    std::unique_ptr<Job> parent = std::make_unique<RecordedBlitParentJob>(state);
    JobTicket original{};
    result = static_cast<HRESULT>(scheduler.Submit(parent,
                                                   {.kind = JobClass::Background,
                                                    .memoryBytes = kFixtureParentCharge,
                                                    .deferredRetirement = true,
                                                    .executionLane = JobExecutionLane::Compute},
                                                   original));
    state.original = original;
    Expect("one-fixture-only-parent-reserves-complete-three-graph-peak",
           SUCCEEDED(result) && !parent && scheduler.ReservedMemoryBytes() == kFixtureParentCharge);
    if (FAILED(result))
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        (void)pump.CloseOwner();
        return false;
    }
    Expect("original-ticket-retains-full-charge-through-all-graphs",
           scheduler.ReservedMemoryBytes() == kFixtureParentCharge);

    const bool passed = WaitForParent(scheduler, routes, state);
    if (!passed)
    {
        Expect("10-second-event-wait-bound-reports-failure-without-fabricated-ack", false);
        pump.StopOwner();
        queue.StopNotifications();
        scheduler.Close({[](void* context) noexcept
                         {
                             auto& state = *static_cast<ParentState*>(context);
                             state.ShutdownOwnerBeforeQueueClose();
                             state.queue.CloseAfterWorkers();
                             state.ShutdownGraphsOwner();
                         },
                         [](void* context) noexcept { static_cast<ParentState*>(context)->ShutdownWorker(); }, &state});
        (void)pump.CloseOwner();
        return false;
    }

    pump.StopOwner();
    queue.StopNotifications();
    scheduler.Close({[](void* context) noexcept
                     {
                         auto& state = *static_cast<ParentState*>(context);
                         state.ShutdownOwnerBeforeQueueClose();
                         state.queue.CloseAfterWorkers();
                         state.ShutdownGraphsOwner();
                     },
                     [](void* context) noexcept { static_cast<ParentState*>(context)->ShutdownWorker(); }, &state});
    const bool pumpClosed = pump.CloseOwner();
    Expect("stable-pump-route-closes-after-all-borrowers", pumpClosed);
    Expect("all-three-real-graph-and-stream-owners-retired",
           state.cleanupPassed && state.streamRetired[0] && state.streamRetired[1] && state.streamRetired[2] &&
               state.graphRetired[0] && state.graphRetired[1] && state.graphRetired[2]);
    Expect("whole-fixture-charge-is-zero-after-worker-retirement", scheduler.ReservedMemoryBytes() == 0);
    std::printf("INFO recorded-adjust-parent.driver=%s original=%zu:%llu acks=%u charge=%llu\n",
                driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware", original.slot,
                static_cast<unsigned long long>(original.generation), state.originalAcknowledgements,
                static_cast<unsigned long long>(kFixtureParentCharge));
    return passed && pumpClosed && state.cleanupPassed;
}
} // namespace

int RunRecordedAdjustmentParentTests()
{
    gFailures = 0;
    bool passed = true;
    for (gCase = 0; gCase < kNames.size(); ++gCase)
    {
        const bool warp = RunDevice(D3D_DRIVER_TYPE_WARP, false);
        const bool hardware = RunDevice(D3D_DRIVER_TYPE_HARDWARE, true);
        passed = warp && hardware && passed;
    }
    return passed && gFailures == 0 ? 0 : 1;
}
