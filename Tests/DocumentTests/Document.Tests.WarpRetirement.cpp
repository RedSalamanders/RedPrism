#include "Document.Tests.WarpRetirement.h"

#include "../Support/DocumentFixture.h"
#include "DeferredRelease.h"
#include "SharedPublicationMemory.h"
#include "WarpOpeningWork.h"
#include "WarpRetirementWork.h"
#include "WarpSampleWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
int gChecks = 0;
int gFailures = 0;
constexpr int kExpectedChecks = 21;
constexpr int kSide = 4;
constexpr std::array<std::uint8_t, 4> kPixel{23, 47, 89, 255};
constexpr Vec2 kOpeningPoint{1.25, 1.25};
constexpr Vec2 kSamplePoint{2.25, 1.25};
constexpr BrushSettings kSettings{2, 1, 0, 0, 0, 1};

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s warp-retirement.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster SourceImage()
{
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    for (std::size_t offset = 0; offset < pixels.size(); offset += kPixel.size())
        for (std::size_t channel = 0; channel < kPixel.size(); ++channel)
            pixels[offset + channel] = kPixel[channel];
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

CanvasDocument MakeDocument(const Raster& image)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;
    ImageLayer layer = ImageLayer::Blank(L"Warp retirement", Vec2{kSide, kSide});
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

struct Fixture final
{
    DocumentSession session;
    Guid target;
    explicit Fixture()
    {
        CanvasDocument document = MakeDocument(SourceImage());
        target = document.layers.front().id;
        session.ReplaceDocument(std::move(document), target);
    }
};

struct Opened final
{
    std::uint64_t opening = 0;
    std::optional<WarpStroke::SampleMemoryPlan> plan;
    const WarpStroke* draft = nullptr;
};

Opened Open(Fixture& fixture)
{
    Opened state;
    WarpOpeningOptions options{kSettings, SmearMode::Liquify, kOpeningPoint, std::this_thread::get_id()};
    auto input = fixture.session.PrepareWarpOpeningInput(std::move(options));
    if (!input)
        return state;

    std::optional<WarpOpeningResult> result;
    std::thread worker(
        [&result, opening = std::move(input.value())]() mutable
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            result.emplace(DocumentSession::PrepareWarpOpeningPublication(std::move(opening), CancelToken{}, {},
                                                                          std::move(memory)));
        });
    worker.join();
    if (!result || result->status != WarpOpeningStatus::Ready || !result->prepared ||
        !fixture.session.ApplyPreparedWarpOpeningIfCurrent(result->prepared.value()))
        return state;

    const auto opening = fixture.session.PendingWarpOpening();
    auto borrowed = result->prepared->TakeAppliedDraftForOwner();
    if (!opening || !borrowed || borrowed.get() != fixture.session.PendingWarp())
        return state;
    borrowed.reset();
    state.opening = opening.value();
    state.draft = fixture.session.PendingWarp();
    state.plan = state.draft ? state.draft->PlanSampleMemory(kSamplePoint) : std::nullopt;
    return state;
}

struct ReleaseObservation final
{
    int nodes = 0;
    std::thread::id thread;
};

void DestroyAndObserve(void* context, DeferredReleaseNode& node) noexcept
{
    auto& observation = *static_cast<ReleaseObservation*>(context);
    ++observation.nodes;
    observation.thread = std::this_thread::get_id();
    if (node.destroy)
        node.destroy(&node);
}

bool RetireDraftOnJoinedWorker(std::optional<WarpRetirementInput>&& retired, ReleaseObservation& observation)
{
    bool installed = false;
    std::thread worker(
        [retired = std::move(retired), &observation, &installed]() mutable
        {
            installed = InstallDeferredReleaseSink(DestroyAndObserve, &observation);
            retired.reset();
            if (installed)
                static_cast<void>(ClearDeferredReleaseSink(&observation));
        });
    worker.join();
    return installed && observation.nodes > 0 && observation.thread != std::this_thread::get_id();
}

bool RetireBundleOnJoinedWorker(std::optional<WarpRetirementInput>&& retired, std::optional<WarpSampleInput>&& sample,
                                ReleaseObservation& observation)
{
    bool installed = false;
    std::thread worker(
        [retired = std::move(retired), sample = std::move(sample), &observation, &installed]() mutable
        {
            installed = InstallDeferredReleaseSink(DestroyAndObserve, &observation);
            retired.reset();
            sample.reset();
            if (installed)
                static_cast<void>(ClearDeferredReleaseSink(&observation));
        });
    worker.join();
    return installed && observation.nodes > 0 && observation.thread != std::this_thread::get_id();
}

bool ScalarBarrier(const DocumentSession& session, std::uint64_t opening) noexcept
{
    const auto pending = session.PendingWarpOpening();
    return session.HasPendingWarp() && pending && pending.value() == opening && !session.PendingWarp();
}

bool TakeRefusedOnForeignJoinedWorker(DocumentSession& session, std::uint64_t opening,
                                      std::optional<std::uint64_t> sequence)
{
    bool accepted = false;
    std::thread worker(
        [&]
        {
            auto retired = session.TakeWarpRetirementInput(opening, sequence);
            accepted = retired.has_value();
        });
    worker.join();
    return !accepted;
}

void LiveZeroPointDraftRefusalAndRetirement()
{
    Fixture fixture;
    const auto document = fixture.session.Document();
    const auto generation = fixture.session.Generation();
    const auto revision = fixture.session.history.CurrentRevision();
    const auto undo = fixture.session.history.UndoCount();
    Opened state = Open(fixture);
    Expect(state.draft && state.plan && state.draft->Points().empty(), "live.zero-point-opening-is-real-draft");
    if (!state.draft)
        return;

    const auto weak = fixture.session.WeakPendingWarpForTest();
    const auto original = state.draft;
    Expect(TakeRefusedOnForeignJoinedWorker(fixture.session, state.opening, std::nullopt) &&
               fixture.session.PendingWarp() == original && !weak.expired(),
           "live.foreign-worker-refuses-before-draft-transfer");
    auto wrongOpening = fixture.session.TakeWarpRetirementInput(state.opening + 1, std::nullopt);
    Expect(!wrongOpening && fixture.session.PendingWarp() == original && !weak.expired(),
           "live.wrong-opening-refuses-with-draft-and-cache-authority-intact");

    auto retired = fixture.session.TakeWarpRetirementInput(state.opening, std::nullopt);
    Expect(retired && !fixture.session.PendingWarp() && fixture.session.PendingWarpOpening() == state.opening &&
               fixture.session.CanApplyHistoryRestore(fixture.session.CaptureHistoryRestoreAuthority()) == false,
           "live.capture-keeps-opening-barrier-through-retirement");
    if (!retired)
        return;

    ReleaseObservation observation;
    const bool observedWorkerDestruction = RetireDraftOnJoinedWorker(std::move(retired), observation);
    const bool exactAck = fixture.session.DiscardWarpFinishIfCurrent(state.opening);
    Expect(observedWorkerDestruction && weak.expired() && exactAck && !fixture.session.HasPendingWarp() &&
               fixture.session.Document() == document && fixture.session.Generation() == generation &&
               fixture.session.history.CurrentRevision() == revision && fixture.session.history.UndoCount() == undo,
           "live.joined-worker-destroys-draft-before-exact-ack-without-document-or-history-edit");
}

void DetachedSampleRetirementAndAck()
{
    Fixture fixture;
    Opened state = Open(fixture);
    if (!state.plan || !state.draft)
    {
        Expect(false, "update.fixture-opens-and-plans");
        return;
    }
    const auto weak = fixture.session.WeakPendingWarpForTest();
    auto sampleInput = fixture.session.TakeWarpSampleInput(state.opening, state.plan.value());
    Expect(sampleInput.has_value(), "update.original-sample-worker-receives-draft");
    if (!sampleInput)
        return;
    const auto sequence = state.plan->sequence;
    std::optional<WarpSampleInput> returned;
    bool appended = false;
    std::thread producer(
        [&]
        {
            appended = sampleInput->AppendForWork(CancelToken{});
            returned.emplace(std::move(sampleInput.value()));
        });
    producer.join();
    sampleInput.reset();

    // Actual useful producer joined; the owner has not applied this prepared sample.
    Expect(appended && returned && ScalarBarrier(fixture.session, state.opening),
           "update.joined-prepared-sample-keeps-exact-opening-sequence-pending");
    Expect(TakeRefusedOnForeignJoinedWorker(fixture.session, state.opening, sequence) &&
               ScalarBarrier(fixture.session, state.opening),
           "update.cached-owner-identity-refuses-foreign-worker-after-draft-detaches");

    auto wrongSequence = fixture.session.TakeWarpRetirementInput(state.opening, sequence + 1);
    Expect(!wrongSequence && ScalarBarrier(fixture.session, state.opening) && !weak.expired(),
           "update.wrong-sequence-refuses-with-detached-draft-alive");
    Expect(fixture.session.IsWarpSampleCurrent(returned.value()) && !weak.expired(),
           "update.prepared-sample-is-current-before-retirement-capture");
    auto retired = fixture.session.TakeWarpRetirementInput(state.opening, sequence);
    const bool preparedSampleRefused = returned && !fixture.session.IsWarpSampleCurrent(returned.value()) &&
                                       !fixture.session.ApplyWarpSampleIfCurrent(returned.value());
    Expect(retired && preparedSampleRefused && ScalarBarrier(fixture.session, state.opening),
           "update.capture-revokes-prepared-sample-and-keeps-retirement-barrier");
    auto duplicate = fixture.session.TakeWarpRetirementInput(state.opening, sequence);
    Expect(!duplicate && ScalarBarrier(fixture.session, state.opening),
           "update.duplicate-retirement-refuses-while-finishing-authority-is-pending");
    Expect(TakeRefusedOnForeignJoinedWorker(fixture.session, state.opening, sequence) &&
               ScalarBarrier(fixture.session, state.opening),
           "update.foreign-worker-refuses-after-retirement-capture");
    if (!retired || !returned)
        return;

    Expect(!weak.expired() && returned && retired,
           "update.complete-original-bundle-retains-detached-draft-and-cache-wrapper");
    ReleaseObservation retirementObservation;
    const bool draftDestroyedOnRetirementWorker =
        RetireBundleOnJoinedWorker(std::move(retired), std::move(returned), retirementObservation);
    Expect(draftDestroyedOnRetirementWorker && weak.expired() && ScalarBarrier(fixture.session, state.opening),
           "update.complete-bundle-destruction-finishes-before-owner-authority-clears");
    Expect(!fixture.session.DiscardWarpFinishIfCurrent(state.opening) && ScalarBarrier(fixture.session, state.opening),
           "update.finish-ack-before-sample-ack-refuses-without-clearing-authority");
    const bool sampleAck = fixture.session.DiscardWarpSampleIfCurrent(state.opening, sequence);
    Expect(sampleAck && ScalarBarrier(fixture.session, state.opening),
           "update.exact-sample-discard-ack-preserves-finish-retirement-barrier");
    const bool finishAck = fixture.session.DiscardWarpFinishIfCurrent(state.opening);
    Expect(draftDestroyedOnRetirementWorker && weak.expired() && finishAck && !fixture.session.HasPendingWarp(),
           "update.exact-finish-discard-ack-clears-retirement-after-worker-destruction");
}

void OldSequenceCannotClearNewUpdate()
{
    Fixture fixture;
    Opened state = Open(fixture);
    if (!state.plan || !state.draft)
    {
        Expect(false, "sequence.fixture-opens-and-plans");
        return;
    }
    auto first = fixture.session.TakeWarpSampleInput(state.opening, state.plan.value());
    if (!first)
    {
        Expect(false, "sequence.first-update-transfers");
        return;
    }
    std::optional<WarpSampleInput> returned;
    bool appended = false;
    std::thread producer(
        [&]
        {
            appended = first->AppendForWork(CancelToken{});
            returned.emplace(std::move(first.value()));
        });
    producer.join();
    first.reset();
    const bool restored = returned && fixture.session.ApplyWarpSampleIfCurrent(returned.value());
    returned.reset();
    const auto nextPlan =
        fixture.session.PendingWarp() ? fixture.session.PendingWarp()->PlanSampleMemory(kSamplePoint) : std::nullopt;
    auto second = nextPlan ? fixture.session.TakeWarpSampleInput(state.opening, nextPlan.value()) : std::nullopt;
    const auto nextSequence = nextPlan ? nextPlan->sequence : 0;
    const bool oldAckRefused = !fixture.session.DiscardWarpSampleIfCurrent(state.opening, state.plan->sequence);
    Expect(appended && restored && second && nextSequence != state.plan->sequence && oldAckRefused &&
               ScalarBarrier(fixture.session, state.opening),
           "sequence.old-callback-cannot-clear-newer-update-of-same-opening");
    if (!second)
        return;

    std::stop_source secondStop;
    static_cast<void>(secondStop.request_stop());
    std::optional<WarpSampleInput> abandoned;
    bool secondCancelled = false;
    std::thread secondWorker(
        [&]
        {
            secondCancelled = !second->AppendForWork(CancelToken{{}, secondStop.get_token()});
            abandoned.emplace(std::move(second.value()));
        });
    secondWorker.join();
    second.reset();
    auto retired = fixture.session.TakeWarpRetirementInput(state.opening, nextSequence);
    ReleaseObservation observation;
    const bool workerDestruction = RetireBundleOnJoinedWorker(std::move(retired), std::move(abandoned), observation);
    const bool sampleAck = fixture.session.DiscardWarpSampleIfCurrent(state.opening, nextSequence);
    Expect(secondCancelled && workerDestruction && sampleAck && ScalarBarrier(fixture.session, state.opening),
           "sequence.current-sample-ack-keeps-retirement-barrier");
    const bool finishAck = fixture.session.DiscardWarpFinishIfCurrent(state.opening);
    Expect(finishAck && !fixture.session.HasPendingWarp(), "sequence.current-finish-ack-clears-only-after-sample-ack");
}
} // namespace

int RunWarpRetirementCoreTests()
{
    gChecks = 0;
    gFailures = 0;
    static_assert(!std::is_copy_constructible_v<WarpRetirementInput>);
    static_assert(std::is_nothrow_move_constructible_v<WarpRetirementInput>);
    LiveZeroPointDraftRefusalAndRetirement();
    DetachedSampleRetirementAndAck();
    OldSequenceCannotClearNewUpdate();
    if (gChecks != kExpectedChecks)
    {
        std::printf("FAIL warp-retirement.check-count-expected-%d-actual-%d\n", kExpectedChecks, gChecks);
        ++gFailures;
    }
    std::printf("SUMMARY warp-retirement checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}
