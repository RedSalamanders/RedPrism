#include "Composition.Tests.RecordedPixelPreview.h"
#include "../Support/Support.Tests.DocumentFixture.h"
#include "CanvasViewport.h"
#include "GpuCommandPump.h"
#include "GpuWork.h"
#include "Jobs.h"
#include "OwnerWorkMultiplexer.h"
#include "PixelMovePreviewWork.h"
#include "RasterEditWork.h"
#include "RecordedDisplayGraph.h"
#include "SharedPublicationMemory.h"
#include "WarpPreviewWork.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <thread>
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
unsigned int gChecks = 0;
unsigned int gWarpCompleted = 0;
unsigned int gHardwareCompleted = 0;
unsigned int gHardwareSkipped = 0;
const char* gDriverName = "none";
unsigned int gCase = 0;
constexpr int kSide = 32;
constexpr std::array<const char*, 4> kNames{"move", "duplicate", "sparse-move", "sparse-duplicate"};

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
    ++gChecks;
    std::printf("%s recorded-pixel-parent.%s.%s.%s\n", passed ? "PASS" : "FAIL", gDriverName, kNames[gCase], name);
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

// Independent BGRA truth table: opaque red left half / blue right half, selected [4,8)x[4,8).
// Move clears exactly that box; duplicate keeps it. First landing is [20,24)x[8,12), next [24,28)x[16,20).
bool MakeFixtures(std::array<CanvasDocument, kGraphCount>& documents,
                  std::array<CanvasRenderer::StrokePreview, 2>& previews, std::vector<RasterTile>& changed,
                  std::vector<std::uint8_t>& initial, std::vector<std::uint8_t>& final, std::thread::id owner)
{
    constexpr std::array<std::uint8_t, 4> red{0, 0, 255, 255}, blue{255, 0, 0, 255}, white{255, 255, 255, 255};
    const bool duplicate = (gCase % 2) != 0, sparse = gCase >= 2;
    std::vector<std::uint8_t> original(kSide * kSide * 4);
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
            std::copy((x < kSide / 2 ? red : blue).begin(), (x < kSide / 2 ? red : blue).end(),
                      original.begin() + (y * kSide + x) * 4);
    auto image = Raster::Make(kSide, kSide, RasterFormat::Bgra8888, original.data(), original.size());
    if (sparse)
    {
        auto tile = Raster::Make(1, 1, RasterFormat::Bgra8888, white.data(), white.size());
        image = Raster::MakeSparse(kSide, kSide, RasterFormat::Bgra8888, image, 0, 0, {{3, 20, tile}}, 0);
        std::copy(white.begin(), white.end(), original.begin() + (20 * kSide + 3) * 4);
        if (!image.IsSparse() || image.IsMaterialized())
            return false;
    }
    documents[0] = MakeDocument(kSide, kSide, image);
    EditDocumentFixture(documents[0],
                        [](DocumentFixture& doc)
                        {
                            doc.selection = DocumentSelection{
                                false, SelectionLoops{{Vec2{4, 4}, Vec2{8, 4}, Vec2{8, 8}, Vec2{4, 8}}}};
                        });
    documents[1] = documents[0];
    RasterEditInput input;
    input.layer = documents[0].layers.front();
    input.selection = documents[0].selection;
    input.canvasWidth = input.canvasHeight = kSide;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    input.duplicate = duplicate;
    auto memory = SharedPublicationMemory::CreateForWork(kFixtureParentCharge);
    const auto lift = PreparePixelMoveLiftForWork(input, {}, memory);
    if (lift.status != PixelMoveLiftStatus::Ready || !lift.opening)
        return false;
    constexpr std::array<Vec2, 2> offsets{Vec2{16, 4}, Vec2{20, 12}};
    for (std::size_t index = 0; index < previews.size(); ++index)
    {
        auto metadata =
            PixelMovePreviewReader::PrepareDisplayForWork(lift.opening, offsets[index], duplicate, owner, memory);
        if (!metadata)
            return false;
        auto& preview = previews[index];
        const auto& layout = metadata->Layout();
        preview.layerID = layout.target;
        preview.width = layout.width;
        preview.height = layout.height;
        preview.transform = layout.paintTransform;
        preview.source = layout.source;
        preview.sourceX = layout.sourceRect.x;
        preview.sourceY = layout.sourceRect.y;
        preview.pixelSource = std::move(metadata);
    }
    auto literal = [&](int dx, int dy)
    {
        auto bytes = original;
        if (!duplicate)
            for (int y = 4; y < 8; ++y)
                for (int x = 4; x < 8; ++x)
                    std::fill_n(bytes.begin() + (y * kSide + x) * 4, 4, std::uint8_t{0});
        for (int y = 4 + dy; y < 8 + dy; ++y)
            for (int x = 4 + dx; x < 8 + dx; ++x)
                std::copy(red.begin(), red.end(), bytes.begin() + (y * kSide + x) * 4);
        return bytes;
    };
    initial = literal(16, 4);
    final = literal(20, 12);
    input.offset = offsets[1];
    input.pixelMoveOpening = lift.opening;
    auto committed = RunRasterEdit(input);
    if (committed.status != RasterEditStatus::Ready || !committed.layer || !committed.selection)
        return false;
    documents[2] = documents[0];
    EditDocumentFixture(documents[2],
                        [&](DocumentFixture& doc)
                        {
                            doc.layers[0] = std::move(*committed.layer);
                            doc.selection = std::move(committed.selection);
                        });
    changed.clear(); // The reader is complete; no changed-tile overlay or inherited working grid is permitted.
    return !sparse || !image.IsMaterialized();
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
    std::printf("INFO recorded-pixel-parent.%s.%s BGRA", kNames[gCase], label);
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4)
        std::printf(" (%u,%u,%u,%u)", pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]);
    std::printf("\n");
}

bool CheckRefusalsForWork(RecordedDisplayGraph& graph, ID3D11Device* device, const CanvasDocument& document,
                          const CanvasRenderer::StrokePreview& valid, GpuCommandStream& stream)
{
    const auto refuse = [&](const char* name, const CanvasRenderer::StrokePreview& preview,
                            CancelToken cancellation = {}, HRESULT expected = E_INVALIDARG)
    {
        const HRESULT result = graph.PrepareStrokeForWork(device, document, preview, {}, stream, cancellation);
        const bool passed = result == expected && graph.IsUnpreparedForTest();
        Expect(name, passed);
        return passed;
    };
    CancelSource cancelled;
    (void)cancelled.Cancel();
    if (!refuse("pre-cancel-refuses-without-resources", valid, cancelled.Token(), E_ABORT))
        return false;
    auto bad = valid;
    bad.width = kSide - 1;
    if (!refuse("wrong-grid-refuses-without-resources", bad))
        return false;
    bad = valid;
    bad.transform.rotation = 1;
    if (!refuse("wrong-grid-placement-refuses-without-resources", bad))
        return false;
    bad = valid;
    bad.mask = true;
    if (!refuse("mask-target-refuses-without-resources", bad))
        return false;
    bad = valid;
    bad.layerID = {};
    if (!refuse("missing-layer-refuses-without-resources", bad))
        return false;
    bad = valid;
    bad.fill = 255;
    if (!refuse("nontransparent-fill-refuses-without-resources", bad))
        return false;
    bad = valid;
    bad.source = Solid(1, 1, {0, 0, 0, 255});
    if (!refuse("different-source-refuses-without-resources", bad))
        return false;
    bad = valid;
    ++bad.sourceX;
    if (!refuse("wrong-source-origin-refuses-without-resources", bad))
        return false;
    bad = valid;
    bad.warpSource = std::make_shared<PreparedWarpPreview>();
    if (!refuse("two-procedural-source-kinds-refuse-without-resources", bad))
        return false;
    const std::array<RasterTile, 1> unrelated{{RasterTile{0, 0, Solid(1, 1, {0, 0, 0, 255})}}};
    const auto overlay = graph.PrepareStrokeForWork(device, document, valid, unrelated, stream, {});
    Expect("unrelated-tile-overlay-refuses-before-resource-prefix",
           overlay == E_INVALIDARG && graph.IsUnpreparedForTest());
    return overlay == E_INVALIDARG && graph.IsUnpreparedForTest();
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
    const std::thread::id ownerThread = std::this_thread::get_id();
    std::array<CanvasRenderer::StrokePreview, 2> previews;
    std::vector<RasterTile> changed;
    std::vector<std::uint8_t> initial, final;
    std::array<RecordedDisplayGraph, kGraphCount> graphs;
    std::array<GpuCommandStream, kGraphCount> streams;
    std::array<GpuCompletionContext, kGraphCount> retiredCapture;
    GpuCompletionContext lastUseCapture;
    wil::com_ptr_nothrow<ID3D11Texture2D> blitTexture;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> blitTarget;
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
            const HRESULT refused = graphs[1].PrepareStrokeForWork(device, {}, previews[1], {}, streams[1], {});
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
                read = read && SUCCEEDED(status) && width == kSide && height == kSide;
            }
            Expect("three-acknowledged-composites-read-at-original-resolution", read);
            const bool reference =
                read && PixelsEqualWithin(pixels[1], final, 0) && PixelsEqualWithin(pixels[2], final, 0);
            Expect("procedural-preview-and-CPU-commit-match-independent-literal-move-or-copy", reference);
            if (!reference)
            {
                PrintPixels("actual", pixels[1]);
                PrintPixels("cpu-reference", pixels[2]);
            }
            const bool unaffected = read && PixelsEqualWithin(pixels[0], initial, 0);
            Expect("first-preview-matches-independent-literal-placement", unaffected);
            Expect("second-preview-rebuilds-complete-grid-so-old-landing-returns-to-source",
                   !graphs[1].InheritedStrokeForTestOwner());
            unsigned int width = 0, height = 0;
            std::vector<std::uint8_t> seedAgain;
            const bool immutableSeed =
                read && SUCCEEDED(graphs[0].CompositionForTestOwner()->Readback(seedAgain, width, height)) &&
                seedAgain == pixels[0];
            Expect("published-seed-pixels-remain-unchanged", immutableSeed);
            const auto& originalSource = previews[0].pixelSource->Layout().source;
            const bool noAssembly = gCase < 2 || (originalSource.IsSparse() && !originalSource.IsMaterialized());
            Expect("complete-recording-never-assembles-shared-sparse-source", noAssembly);
            CanvasViewport view;
            view.viewSize = {kSide, kSide};
            view.followsFit = false;
            bool actualBlits = true;
            for (auto& graph : graphs)
                actualBlits =
                    SUCCEEDED(graph.BlitOwner(blitTarget.get(), kSide, kSide, view, 0, 0, kSide, kSide)) && actualBlits;
            immediate->OMSetRenderTargets(0, nullptr, nullptr);
            Expect("all-three-recorded-graphs-blit-before-original-last-use-barrier", actualBlits);
            blitsPassed = read && reference && unaffected && immutableSeed && noAssembly && actualBlits;
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

    [[nodiscard]] HRESULT PrepareBlitTargetForWork() noexcept
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = description.Height = kSide;
        description.MipLevels = description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET;
        HRESULT result = device->CreateTexture2D(&description, nullptr, blitTexture.put());
        if (SUCCEEDED(result))
            result = device->CreateRenderTargetView(blitTexture.get(), nullptr, blitTarget.put());
        return result;
    }

    void ShutdownWorker() noexcept
    {
        blitTarget.reset();
        blitTexture.reset();
        for (std::size_t graph = 0; graph < kGraphCount; ++graph)
        {
            if (streamInitialized[graph] && streamAuthorized[graph] && !streamRetired[graph])
                streamRetired[graph] = streams[graph].RetireForWork();
            retiredCapture[graph] = {};
            if (graphDetached[graph] && !graphRetired[graph])
                graphRetired[graph] = graphs[graph].RetireForWork();
            cleanupPassed = cleanupPassed && (!streamInitialized[graph] || streamRetired[graph]) && graphRetired[graph];
        }
        previews = {}; // Entire immutable preview owners release after acknowledged worker graph retirement.
        changed.clear();
        initial.clear();
        final.clear();
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
        {
            if (!MakeFixtures(_state.documents, _state.previews, _state.changed, _state.initial, _state.final,
                              _state.ownerThread))
                return {JobStatus::Failed, E_OUTOFMEMORY};
            const HRESULT target = _state.PrepareBlitTargetForWork();
            return FAILED(target) ? JobResult{JobStatus::Failed, static_cast<std::int32_t>(target)} : JobResult{};
        }
        if (_state.phase == Phase::Recording)
        {
            if (!_state.sourceAttached)
                return {JobStatus::Failed, static_cast<std::int32_t>(_state.sourceError)};
            if (_state.index == 0 && gCase == 0 &&
                !CheckRefusalsForWork(_state.graphs[0], _state.device, _state.documents[0], _state.previews[0],
                                      _state.streams[0]))
                return {JobStatus::Failed, E_UNEXPECTED};
            const auto* seed = _state.index ? &_state.graphs[0] : nullptr;
            const std::span<const RasterTile> changed =
                _state.index == 1 ? std::span<const RasterTile>{_state.changed} : std::span<const RasterTile>{};
            const HRESULT result = _state.index < 2 ? _state.graphs[_state.index].PrepareStrokeForWork(
                                                          _state.device, std::move(_state.documents[_state.index]),
                                                          _state.previews[_state.index], changed,
                                                          _state.streams[_state.index], context.cancellation, seed)
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
            _state.blitTarget.reset();
            _state.blitTexture.reset();
            _state.previews = {};
            _state.changed.clear();
            _state.initial.clear();
            _state.final.clear();
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
    gDriverName = driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware";
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT result = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                       D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    if (FAILED(result))
    {
        if (optionalHardware)
        {
            ++gHardwareSkipped;
            std::printf("INFO recorded-pixel-parent.hardware.%s.device-unavailable-skipped hr=%ld\n", kNames[gCase],
                        static_cast<long>(result));
        }
        else
            Expect("warp-device-created", false);
        return optionalHardware;
    }
    D3D11_FEATURE_DATA_THREADING threading{};
    result = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    if (FAILED(result) || !threading.DriverConcurrentCreates)
    {
        if (optionalHardware)
        {
            ++gHardwareSkipped;
            std::printf("INFO recorded-pixel-parent.hardware.%s.concurrent-create-unavailable-skipped hr=%ld\n",
                        kNames[gCase], static_cast<long>(result));
        }
        else
            Expect("warp-supports-worker-resource-creation", false);
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
    std::printf("INFO recorded-pixel-parent.driver=%s original=%zu:%llu acks=%u charge=%llu\n",
                driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware", original.slot,
                static_cast<unsigned long long>(original.generation), state.originalAcknowledgements,
                static_cast<unsigned long long>(kFixtureParentCharge));
    const bool complete = passed && pumpClosed && state.cleanupPassed;
    if (complete)
        driver == D3D_DRIVER_TYPE_WARP ? ++gWarpCompleted : ++gHardwareCompleted;
    return complete;
}
} // namespace

int RunRecordedPixelPreviewParentTests()
{
    gFailures = 0;
    gChecks = gWarpCompleted = gHardwareCompleted = gHardwareSkipped = 0;
    bool passed = true;
    for (gCase = 0; gCase < kNames.size(); ++gCase)
    {
        const bool warp = RunDevice(D3D_DRIVER_TYPE_WARP, false);
        const bool hardware = RunDevice(D3D_DRIVER_TYPE_HARDWARE, true);
        passed = warp && hardware && passed;
    }
    std::printf("SUMMARY recorded-pixel-parent checks=%u failures=%u warp=%u hardware=%u hardwareSkipped=%u\n", gChecks,
                gFailures, gWarpCompleted, gHardwareCompleted, gHardwareSkipped);
    return passed && gFailures == 0 && gWarpCompleted == kNames.size() ? 0 : 1;
}
