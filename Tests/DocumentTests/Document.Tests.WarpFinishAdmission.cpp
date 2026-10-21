#include "Document.Tests.WarpFinishAdmission.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "SharedPublicationMemory.h"
#include "WarpFinishWork.h"
#include "WarpOpeningWork.h"
#include "WarpPreviewWork.h"
#include "WarpSampleWork.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s warp-finish-admission.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr int kSide = 16;
constexpr BrushSettings kSettings{6, 0.8, 0, 0, 0, 1};
constexpr Vec2 kOpeningPoint{7.25, 7.25};
constexpr Vec2 kSecondPoint{9.25, 7.25};
constexpr std::array<std::uint8_t, 4> kPremultipliedPixel{12, 24, 48, 96};

Raster SourceRaster()
{
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    for (std::size_t offset = 0; offset < pixels.size(); offset += kPremultipliedPixel.size())
        std::copy(kPremultipliedPixel.begin(), kPremultipliedPixel.end(),
                  pixels.begin() + static_cast<std::ptrdiff_t>(offset));
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

CanvasDocument MakeDocument(const Raster& image)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;
    ImageLayer layer = ImageLayer::Blank(L"Warp finish admission fixture", Vec2{kSide, kSide});
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

struct Fixture final
{
    Raster source = SourceRaster();
    DocumentSession session;
    Guid target;

    Fixture()
    {
        CanvasDocument document = MakeDocument(source);
        target = document.layers.front().id;
        session.ReplaceDocument(std::move(document), target);
    }
    ~Fixture()
    {
        auto draft = session.WeakPendingWarpForTest().lock();
        session.CancelWarp();
        std::thread retire([held = std::move(draft)]() mutable { held.reset(); });
        retire.join();
    }
};

bool OpenWarp(Fixture& fixture)
{
    auto input = fixture.session.PrepareWarpOpeningInput(
        WarpOpeningOptions{kSettings, SmearMode::Liquify, kOpeningPoint, std::this_thread::get_id()});
    if (!input)
        return false;
    std::optional<WarpOpeningResult> opened;
    std::thread openingWorker(
        [&]
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            opened.emplace(
                DocumentSession::PrepareWarpOpeningPublication(std::move(input.value()), {}, {}, std::move(memory)));
        });
    openingWorker.join();
    if (!opened || opened->status != WarpOpeningStatus::Ready || !opened->prepared ||
        !fixture.session.ApplyPreparedWarpOpeningIfCurrent(*opened->prepared))
        return false;
    auto appliedDraft = opened->prepared->TakeAppliedDraftForOwner();
    const bool ownerApplied = appliedDraft && appliedDraft.get() == fixture.session.PendingWarp();
    appliedDraft.reset();
    if (!ownerApplied || !fixture.session.PendingWarp())
        return false;

    const auto plan = fixture.session.PendingWarp()->PlanSampleMemory(kSecondPoint);
    auto sample =
        plan ? fixture.session.TakeWarpSampleInput(fixture.session.PendingWarpOpening().value(), *plan) : std::nullopt;
    if (!sample)
        return false;
    std::optional<WarpSampleInput> completed;
    bool appended = false;
    std::thread sampleWorker(
        [&]
        {
            appended = sample->AppendForWork({});
            completed.emplace(std::move(sample.value()));
        });
    sampleWorker.join();
    return appended && completed && fixture.session.ApplyWarpSampleIfCurrent(*completed);
}

struct CommittedState final
{
    const void* documentRoot = nullptr;
    std::uint64_t generation = 0;
    Guid revision;
    int undo = 0;
    std::size_t future = 0;

    explicit CommittedState(const DocumentSession& session)
        : documentRoot(session.Document() ? session.Document()->layers.Identity() : nullptr),
          generation(session.Generation()), revision(session.history.CurrentRevision()),
          undo(session.history.UndoCount()), future(session.history.CaptureState().FutureCount())
    {
    }

    [[nodiscard]] bool Matches(const DocumentSession& session) const
    {
        const auto history = session.history.CaptureState();
        return session.Document() && session.Document()->layers.Identity() == documentRoot &&
               session.Generation() == generation && session.history.CurrentRevision() == revision &&
               session.history.UndoCount() == undo && history.FutureCount() == future;
    }
};

void AdmissionAndBounds()
{
    Fixture fixture;
    Expect(OpenWarp(fixture), "fixture-opens-and-owner-applies-two-point-path");
    if (!fixture.session.PendingWarp())
        return;

    const CommittedState before(fixture.session);
    const auto cpuOnly = fixture.session.PlanWarpFinishMemory(false);
    const auto withCoverage = fixture.session.PlanWarpFinishMemory(true);
    const auto repeated = fixture.session.PlanWarpFinishMemory(false);
    Expect(cpuOnly && withCoverage && repeated && cpuOnly->cpuBytes > 0 && cpuOnly->gpuBytes == 0 &&
               withCoverage->gpuBytes > 0 && withCoverage->cpuBytes == cpuOnly->cpuBytes &&
               repeated->cpuBytes == cpuOnly->cpuBytes && repeated->gpuBytes == cpuOnly->gpuBytes &&
               repeated->maximumReplayTiles == cpuOnly->maximumReplayTiles,
           "preflight-repeatable-cpu-gpu-split-and-no-document-history-change");
    Expect(cpuOnly && cpuOnly->maximumReplayTiles == 1, "16x16-identity-replay-has-literal-one-tile-maximum");
    Expect(before.Matches(fixture.session) && fixture.session.PendingWarp() &&
               fixture.session.PendingWarp()->IsValid() && fixture.session.PendingWarp()->Points().size() == 2 &&
               fixture.session.ActiveLayer() && fixture.session.ActiveLayer()->asset &&
               fixture.session.ActiveLayer()->asset->image == fixture.source,
           "preflight-preserves-live-draft-source-document-and-history");

    const auto* pending = fixture.session.PendingWarp();
    const auto snapshotBound = pending ? pending->SnapshotMemoryBytesForOwner() : 0;
    const auto sourceBound = fixture.source.RetirementMemoryBytes();
    const auto outputBytes = PixelBuffer::RequiredBytes(kSide * kSide * 4);
    Expect(cpuOnly && snapshotBound && sourceBound && outputBytes &&
               cpuOnly->cpuBytes >= sourceBound + snapshotBound + outputBytes,
           "cpu-plan-independently-covers-known-source-snapshot-and-full-output-floor");

    std::shared_ptr<WarpStroke> borrowed = fixture.session.WeakPendingWarpForTest().lock();
    Expect(borrowed && !fixture.session.PlanWarpFinishMemory(false), "live-borrow-refuses-exclusive-finish-admission");
    borrowed.reset();

    std::optional<WarpFinishMemoryPlan> wrongThreadPlan;
    std::thread wrongThread([&] { wrongThreadPlan = fixture.session.PlanWarpFinishMemory(false); });
    wrongThread.join();
    Expect(!wrongThreadPlan, "wrong-thread-preflight-refuses-known-owner-draft");

    auto captured = fixture.session.TakeWarpFinishInput(fixture.session.PendingWarpOpening().value());
    Expect(captured && !fixture.session.PlanWarpFinishMemory(false),
           "detached-draft-in-finish-input-is-not-session-admissible");
    // The draft is test-only and intentionally left captured until its worker retirement below.
    if (captured)
    {
        std::thread retire([input = std::move(captured.value())]() mutable {});
        retire.join();
    }
}

void RefusalCases()
{
    {
        Fixture missing;
        Expect(!missing.session.PlanWarpFinishMemory(false), "missing-pending-warp-refuses-preflight");
    }

    {
        Fixture changed;
        const bool opened = OpenWarp(changed);
        if (opened)
            EditLayerRecordsForTest(changed.session, [](DocumentFixture& document)
                                    { document.layers.front().transform.origin.x += 1.0; });
        Expect(opened && !changed.session.PlanWarpFinishMemory(false), "changed-target-placement-refuses-preflight");
    }

    {
        Fixture detached;
        const bool opened = OpenWarp(detached);
        if (opened)
        {
            auto draft = detached.session.WeakPendingWarpForTest().lock();
            detached.session.CancelWarp();
            std::thread retire([held = std::move(draft)]() mutable { held.reset(); });
            retire.join();
        }
        Expect(opened && !detached.session.PlanWarpFinishMemory(false) && !detached.session.HasPendingWarp(),
               "detached-or-cancelled-session-draft-refuses-preflight");
    }
}

struct PublishedPreview final
{
    std::shared_ptr<WarpStroke> draft;
    std::shared_ptr<const PreparedWarpPreview> preview;
    bool workerReadValid = false;
};

void SnapshotRetirementBound()
{
    const auto owner = std::this_thread::get_id();
    PublishedPreview result;
    std::thread worker(
        [&]
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            LayerTransform placement;
            placement.size = {kSide, kSide};
            auto draft = WarpStroke::MakePlacedForWork({SourceRaster(), placement, kSide, kSide}, SmearMode::Liquify,
                                                       kSettings, owner, {}, memory);
            if (!draft || !draft->AppendForWork(kOpeningPoint, {}) || !draft->AppendForWork(kSecondPoint, {}))
                return;
            result.preview = PrepareWarpPreviewForWork(*draft, owner, {}, std::move(memory));
            if (result.preview)
            {
                WarpPreviewReader reader(result.preview);
                result.workerReadValid = reader.IsValidForWork();
            }
            result.draft = std::move(draft);
        });
    worker.join();

    const auto ownerBound = result.draft ? result.draft->SnapshotMemoryBytesForOwner() : 0;
    const auto publishedRetirementBytes = result.preview ? result.preview->ownershipBytes : 0;
    bool validAlpha = result.preview && result.preview->tiles.size() == 1;
    if (validAlpha)
    {
        const Raster& tile = result.preview->tiles.front().pixels;
        const auto* pixels = tile.Data();
        validAlpha = pixels && tile.Width() == kSide && tile.Height() == kSide;
        for (std::size_t offset = 0; validAlpha && offset < tile.ByteSize(); offset += 4)
            validAlpha = pixels[offset] <= pixels[offset + 3] && pixels[offset + 1] <= pixels[offset + 3] &&
                         pixels[offset + 2] <= pixels[offset + 3];
    }
    {
        WarpPreviewReader ownerReader(result.preview);
        Expect(result.preview && result.preview->ownerThread == owner && result.workerReadValid &&
                   !ownerReader.IsValidForWork() && ownerBound >= publishedRetirementBytes && validAlpha,
               "owner-cached-bound-covers-joined-worker-all-touched-publication-and-owner-cannot-read");
    }

    std::thread retire(
        [draft = std::move(result.draft), preview = std::move(result.preview)]() mutable
        {
            preview.reset();
            draft.reset();
        });
    retire.join();
}
} // namespace

int RunWarpFinishAdmissionTests()
{
    gAssertions = 0;
    gFailures = 0;
    AdmissionAndBounds();
    RefusalCases();
    SnapshotRetirementBound();
    std::printf("SUMMARY warp-finish-admission checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
