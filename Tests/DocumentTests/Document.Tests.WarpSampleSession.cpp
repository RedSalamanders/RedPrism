#include "Document.Tests.WarpSampleSession.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Executor.h"
#include "LayerMetadataWork.h"
#include "SharedPublicationMemory.h"
#include "WarpOpeningWork.h"
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
int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s warp-sample-session.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr int kSide = 4;
constexpr std::array<std::uint8_t, 4> kSolid{23, 47, 89, 255};
constexpr std::array<std::uint8_t, 4> kZero{};
constexpr Vec2 kOpeningPoint{1.25, 1.25};
constexpr Vec2 kSamplePoint{2.25, 1.25};
constexpr BrushSettings kSettings{2, 1, 0, 0, 0, 1};

Raster SolidImage(const std::array<std::uint8_t, 4>& pixel)
{
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    for (std::size_t offset = 0; offset < pixels.size(); offset += pixel.size())
        for (std::size_t channel = 0; channel < pixel.size(); ++channel)
            pixels[offset + channel] = pixel[channel];
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

CanvasDocument MakeDocument(const Raster& image)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;
    ImageLayer layer = ImageLayer::Blank(L"Warp sample", Vec2{kSide, kSide});
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

struct Fixture final
{
    DocumentSession session;
    Guid target;

    explicit Fixture(const std::array<std::uint8_t, 4>& pixel)
    {
        CanvasDocument document = MakeDocument(SolidImage(pixel));
        target = document.layers.front().id;
        session.ReplaceDocument(std::move(document), target);
    }
};

WarpOpeningOptions OpeningOptions(DocumentSession& session)
{
    (void)session;
    return WarpOpeningOptions{kSettings, SmearMode::Liquify, kOpeningPoint, std::this_thread::get_id()};
}

struct Opened final
{
    std::optional<WarpSampleMemoryPlan> plan;
    std::uint64_t opening = 0;
    const WarpStroke* original = nullptr;
};

// Opening publication allocations are deliberately created on the joined worker.
Opened OpenAndPlan(Fixture& fixture)
{
    Opened state;
    auto openingInput = fixture.session.PrepareWarpOpeningInput(OpeningOptions(fixture.session));
    if (!openingInput)
        return state;

    std::optional<WarpOpeningResult> result;
    std::thread worker(
        [&result, input = std::move(openingInput.value())]() mutable
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            result.emplace(
                DocumentSession::PrepareWarpOpeningPublication(std::move(input), CancelToken{}, {}, std::move(memory)));
        });
    worker.join();
    if (!result || result->status != WarpOpeningStatus::Ready || !result->prepared)
        return state;

    PreparedWarpOpening& prepared = result->prepared.value();
    if (!fixture.session.ApplyPreparedWarpOpeningIfCurrent(prepared))
        return state;
    const auto opening = fixture.session.PendingWarpOpening();
    auto borrowed = prepared.TakeAppliedDraftForOwner();
    const bool sameDraft = opening.has_value() && borrowed && borrowed.get() == fixture.session.PendingWarp();
    borrowed.reset();
    if (!sameDraft || !fixture.session.PendingWarp())
        return state;

    state.opening = opening.value();
    state.original = fixture.session.PendingWarp();
    state.plan = state.original->PlanSampleMemory(kSamplePoint);
    return state;
}

struct AppendResult final
{
    std::optional<WarpSampleInput> input;
    bool appended = false;
    bool expectedPixels = false;
    bool duplicateRefused = false;
};

bool ImageIsLiteral(const Raster& raster, const std::array<std::uint8_t, 4>& pixel) noexcept
{
    if (raster.Width() != kSide || raster.Height() != kSide || raster.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, 4> actual{};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
        {
            raster.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
            if (actual != pixel)
                return false;
        }
    return true;
}

void AppendOnJoinedWorker(AppendResult& result, WarpSampleInput input, const std::array<std::uint8_t, 4>& expected)
{
    std::thread worker(
        [&result, expected, input = std::move(input)]() mutable
        {
            result.appended = input.AppendForWork(CancelToken{{}, std::stop_token{}});
            result.duplicateRefused = !input.AppendForWork({});
            WarpStroke* draft = input.DraftForWork();
            result.expectedPixels = draft && draft->Points().size() == 1 && draft->Points().front() == kSamplePoint &&
                                    ImageIsLiteral(draft->ImageForWork(CancelToken{}), expected);
            result.input.emplace(std::move(input));
        });
    worker.join();
}

bool ScalarState(const DocumentSession& session, std::uint64_t opening, const WarpStroke* pending) noexcept
{
    const auto current = session.PendingWarpOpening();
    return session.HasPendingWarp() && current.has_value() && current.value() == opening &&
           session.PendingWarp() == pending;
}

void CaptureAppendApplyLiteralSamples()
{
    for (const auto& pixel : {kSolid, kZero})
    {
        Fixture fixture(pixel);
        const auto document = fixture.session.Document();
        const auto layerRoot = document.value().layers.Identity();
        const auto imageIdentity = fixture.session.ActiveLayer()->asset->image.Identity();
        const auto generation = fixture.session.Generation();
        const auto revision = fixture.session.history.CurrentRevision();
        const auto undo = fixture.session.history.UndoCount();
        const auto redo = fixture.session.history.CaptureState().FutureCount();
        Opened state = OpenAndPlan(fixture);
        Expect(state.plan.has_value() && state.original && state.opening != 0,
               "capture.fixture-opens-and-plans-on-owner");
        if (!state.plan || !state.original)
            continue;

        const LayerMetadataAuthority metadata = fixture.session.CaptureLayerMetadataAuthority(true);
        auto sample = fixture.session.TakeWarpSampleInput(state.opening, state.plan.value());
        Expect(sample.has_value() && fixture.session.HasPendingWarp() && fixture.session.PendingWarp() == nullptr &&
                   fixture.session.PendingWarpOpening().has_value() &&
                   fixture.session.PendingWarpOpening().value() == state.opening,
               "capture-retains-opening-scalar-and-moves-draft-to-input");
        if (!sample)
            continue;
        Expect(!fixture.session.IsWarpSampleCurrent(sample.value()) && !fixture.session.CanPaint() &&
                   !fixture.session.CanAdjustColors() && !fixture.session.CanEditSelection() &&
                   !fixture.session.CanApplyHistoryRestore(fixture.session.CaptureHistoryRestoreAuthority()) &&
                   !fixture.session.CanApplyLayerMetadataAuthority(metadata),
               "transfer-blocks-edits-and-history-but-tracks-metadata-opening");
        Expect(!fixture.session.TakeWarpSampleInput(state.opening, state.plan.value()) &&
                   fixture.session.PendingWarp() == nullptr && fixture.session.HasPendingWarp(),
               "capture-refuses-duplicate-transfer");
        // The worker must append exactly once. Pixel inspection stays on the joined worker.
        const bool ownerAppendRefused = !sample.value().AppendForWork(CancelToken{{}, std::stop_token{}}) &&
                                        sample.value().DraftForWork() == nullptr;
        Expect(ownerAppendRefused, "worker-api-refuses-owner-append-and-draft-access");
        WarpSampleInput moved(std::move(sample.value()));
        Expect(!sample.value().AppendForWork({}) && !sample.value().DraftForWork() &&
                   !fixture.session.IsWarpSampleCurrent(sample.value()) &&
                   !fixture.session.ApplyWarpSampleIfCurrent(sample.value()),
               "move-invalidates-original-input-without-clearing-transfer");
        AppendResult work;
        AppendOnJoinedWorker(work, std::move(moved), pixel);
        Expect(work.appended && work.duplicateRefused && work.expectedPixels && work.input.has_value(),
               pixel == kZero ? "worker.zero-sample-matches-independent-literal"
                              : "worker.solid-sample-matches-independent-literal");
        if (!work.input)
            continue;

        WarpSampleInput& returned = work.input.value();
        Expect(fixture.session.IsWarpSampleCurrent(returned),
               "joined-owner-sees-current-sample-without-pixel-traversal");
        const bool applied = fixture.session.ApplyWarpSampleIfCurrent(returned);
        Expect(applied && fixture.session.PendingWarp() == state.original &&
                   ScalarState(fixture.session, state.opening, state.original) &&
                   fixture.session.Document().value().layers.Identity() == layerRoot &&
                   fixture.session.ActiveLayer()->asset->image.Identity() == imageIdentity &&
                   fixture.session.Generation() == generation &&
                   fixture.session.history.CurrentRevision() == revision &&
                   fixture.session.history.UndoCount() == undo &&
                   fixture.session.history.CaptureState().FutureCount() == redo,
               "apply-restores-identical-draft-without-document-history-or-generation-change");
        Expect(fixture.session.CanApplyLayerMetadataAuthority(metadata),
               "joined-apply-restores-metadata-authority-for-same-opening");
        Expect(!fixture.session.IsWarpSampleCurrent(returned) && !fixture.session.ApplyWarpSampleIfCurrent(returned),
               "apply-consumes-input-and-refuses-duplicate-publication");
        Expect(!fixture.session.TakeWarpSampleInput(state.opening, state.plan.value()) &&
                   fixture.session.PendingWarp() == state.original &&
                   ScalarState(fixture.session, state.opening, state.original),
               "refusal-stale-plan-after-accepted-append-preserves-current-draft");
        const auto duplicatePlan = fixture.session.PendingWarp()->PlanSampleMemory(kSamplePoint);
        auto second =
            duplicatePlan ? fixture.session.TakeWarpSampleInput(state.opening, duplicatePlan.value()) : std::nullopt;
        Expect(second && duplicatePlan->sequence == state.plan->sequence + 1 &&
                   !fixture.session.DiscardWarpSampleIfCurrent(state.opening, state.plan->sequence) &&
                   ScalarState(fixture.session, state.opening, nullptr),
               "old-sample-discard-cannot-clear-younger-transfer-with-same-opening");
        if (second)
        {
            AppendResult duplicate;
            AppendOnJoinedWorker(duplicate, std::move(second.value()), pixel);
            Expect(duplicate.appended && duplicate.expectedPixels && duplicate.input &&
                       fixture.session.ApplyWarpSampleIfCurrent(duplicate.input.value()) &&
                       ScalarState(fixture.session, state.opening, state.original) &&
                       fixture.session.Document() == document && fixture.session.Generation() == generation &&
                       fixture.session.history.CurrentRevision() == revision,
                   "duplicate-sample-reuses-original-draft-and-keeps-literal-document-history");
        }
        fixture.session.CancelWarp();
    }
}

void CancelledWorkerIsDiscardedWithoutPublication()
{
    Fixture fixture(kSolid);
    const auto document = fixture.session.Document();
    const auto generation = fixture.session.Generation();
    const auto revision = fixture.session.history.CurrentRevision();
    Opened state = OpenAndPlan(fixture);
    auto sample = state.plan ? fixture.session.TakeWarpSampleInput(state.opening, state.plan.value()) : std::nullopt;
    Expect(sample.has_value(), "cancel-worker.fixture-transfers-original-draft");
    if (!sample)
        return;
    std::stop_source stop;
    static_cast<void>(stop.request_stop());
    bool refused = false;
    bool noMutableAccess = false;
    std::optional<WarpSampleInput> result;
    std::thread worker(
        [&, input = std::move(sample.value())]() mutable
        {
            refused = !input.AppendForWork(CancelToken{{}, stop.get_token()});
            noMutableAccess = input.DraftForWork() == nullptr;
            result.emplace(std::move(input));
        });
    worker.join();
    Expect(refused && noMutableAccess && result && !fixture.session.IsWarpSampleCurrent(result.value()) &&
               !fixture.session.ApplyWarpSampleIfCurrent(result.value()) &&
               ScalarState(fixture.session, state.opening, nullptr),
           "cancel-worker.failed-sample-keeps-barrier-until-matching-discard");
    Expect(!fixture.session.DiscardWarpSampleIfCurrent(state.opening + 1, state.plan->sequence) &&
               fixture.session.DiscardWarpSampleIfCurrent(state.opening, state.plan->sequence) &&
               !fixture.session.HasPendingWarp() && fixture.session.Document() == document &&
               fixture.session.Generation() == generation && fixture.session.history.CurrentRevision() == revision &&
               fixture.session.history.UndoCount() == 0,
           "cancel-worker.exact-discard-preserves-complete-source-and-history");
    if (result)
    {
        std::thread retire([input = std::move(result.value())]() mutable {});
        retire.join();
    }
}

void RefusalAndDiscardCases()
{
    {
        Fixture fixture(kSolid);
        Opened state = OpenAndPlan(fixture);
        Expect(state.plan && state.original, "refusal.valid-fixture");
        if (state.plan && state.original)
        {
            auto wrongOpening = fixture.session.TakeWarpSampleInput(state.opening + 1, state.plan.value());
            auto borrowed = fixture.session.WeakPendingWarpForTest().lock();
            Expect(borrowed && !fixture.session.TakeWarpSampleInput(state.opening, state.plan.value()) &&
                       ScalarState(fixture.session, state.opening, state.original),
                   "refusal-live-strong-borrow-prevents-exclusive-transfer");
            borrowed.reset();
            auto editedPlan = state.plan.value();
            editedPlan.point.x += 0.5;
            auto wrongPlan = fixture.session.TakeWarpSampleInput(state.opening, editedPlan);
            Expect(!wrongOpening && !wrongPlan && fixture.session.PendingWarp() == state.original &&
                       ScalarState(fixture.session, state.opening, state.original),
                   "refusal-wrong-opening-and-edited-plan-preserve-pending-session");
        }
    }
    {
        Fixture first(kSolid);
        Fixture second(kSolid);
        Opened a = OpenAndPlan(first);
        Opened b = OpenAndPlan(second);
        auto foreign = a.plan && b.plan ? first.session.TakeWarpSampleInput(a.opening, b.plan.value()) : std::nullopt;
        Expect(a.plan && b.plan && !foreign && a.original && first.session.PendingWarp() == a.original,
               "refusal-foreign-plan-preserves-originating-draft");
    }
    {
        Fixture fixture(kSolid);
        Opened state = OpenAndPlan(fixture);
        if (!state.plan)
        {
            Expect(false, "discard.fixture-opened");
            return;
        }
        auto sample = fixture.session.TakeWarpSampleInput(state.opening, state.plan.value());
        fixture.session.CancelWarp();
        Expect(sample && !fixture.session.HasPendingWarp() && !fixture.session.PendingWarpOpening() &&
                   !fixture.session.IsWarpSampleCurrent(sample.value()),
               "cancel-invalidates-in-flight-input-and-clears-scalar-authority");
        if (sample)
        {
            AppendResult late;
            AppendOnJoinedWorker(late, std::move(sample.value()), kSolid);
            Expect(late.appended && late.input && !fixture.session.IsWarpSampleCurrent(late.input.value()) &&
                       !fixture.session.ApplyWarpSampleIfCurrent(late.input.value()) && !fixture.session.PendingWarp(),
                   "cancelled-opening-refuses-late-worker-append-publication");
            if (late.input)
            {
                std::thread retire([input = std::move(late.input.value())]() mutable {});
                retire.join();
            }
        }
    }
    {
        Fixture fixture(kSolid);
        Opened old = OpenAndPlan(fixture);
        auto sample = old.plan ? fixture.session.TakeWarpSampleInput(old.opening, old.plan.value()) : std::nullopt;
        Expect(sample && fixture.session.DiscardWarpSampleIfCurrent(old.opening, old.plan->sequence) &&
                   !fixture.session.HasPendingWarp() &&
                   !fixture.session.DiscardWarpSampleIfCurrent(old.opening, old.plan->sequence),
               "discard-clears-only-matching-transfer-once");
        Opened next = OpenAndPlan(fixture);
        Expect(next.plan && next.opening != old.opening &&
                   !fixture.session.DiscardWarpSampleIfCurrent(old.opening, old.plan->sequence) &&
                   ScalarState(fixture.session, next.opening, next.original),
               "old-discard-cannot-clear-new-opening");
    }
}

void StaleDocumentAndReopen()
{
    for (const bool clear : {false, true})
    {
        Fixture fixture(kSolid);
        Opened old = OpenAndPlan(fixture);
        auto sample = old.plan ? fixture.session.TakeWarpSampleInput(old.opening, old.plan.value()) : std::nullopt;
        if (!sample)
        {
            Expect(false, clear ? "stale.clear-capture" : "stale.replace-capture");
            continue;
        }
        AppendResult completed;
        AppendOnJoinedWorker(completed, std::move(sample.value()), kSolid);
        Expect(completed.appended && completed.input && fixture.session.IsWarpSampleCurrent(completed.input.value()),
               clear ? "stale.clear-worker-completes-before-clear" : "stale.replace-worker-completes-before-replace");
        if (!completed.input)
            continue;
        if (clear)
            fixture.session.ClearDocument();
        else
            fixture.session.ReplaceDocument(MakeDocument(SolidImage(kZero)), std::nullopt);
        Expect(!fixture.session.IsWarpSampleCurrent(completed.input.value()) &&
                   !fixture.session.ApplyWarpSampleIfCurrent(completed.input.value()) &&
                   !fixture.session.HasPendingWarp(),
               clear ? "stale.clear-refuses-old-input" : "stale.replacement-refuses-old-input");
        std::thread retire([input = std::move(completed.input.value())]() mutable {});
        retire.join();
    }
    {
        Fixture fixture(kSolid);
        Opened old = OpenAndPlan(fixture);
        auto sample = old.plan ? fixture.session.TakeWarpSampleInput(old.opening, old.plan.value()) : std::nullopt;
        if (!sample)
        {
            Expect(false, "stale-reopen.fixture-captured");
            return;
        }
        AppendResult completed;
        AppendOnJoinedWorker(completed, std::move(sample.value()), kSolid);
        Expect(completed.appended && completed.input && fixture.session.IsWarpSampleCurrent(completed.input.value()),
               "stale-reopen.original-worker-completes-before-new-opening");
        fixture.session.CancelWarp();
        Opened reopened = OpenAndPlan(fixture);
        Expect(completed.input && reopened.plan && reopened.opening != old.opening &&
                   !fixture.session.IsWarpSampleCurrent(completed.input.value()) &&
                   !fixture.session.ApplyWarpSampleIfCurrent(completed.input.value()) &&
                   ScalarState(fixture.session, reopened.opening, reopened.original),
               "stale-reopen-refuses-old-input-and-preserves-new-draft");
    }
}
} // namespace

int RunWarpSampleSessionTests()
{
    gFailures = 0;
    gChecks = 0;
    static_assert(!std::is_copy_constructible_v<WarpSampleInput>);
    static_assert(std::is_nothrow_move_constructible_v<WarpSampleInput>);
    CaptureAppendApplyLiteralSamples();
    CancelledWorkerIsDiscardedWithoutPublication();
    RefusalAndDiscardCases();
    StaleDocumentAndReopen();
    std::printf("SUMMARY warp-sample-session checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}
