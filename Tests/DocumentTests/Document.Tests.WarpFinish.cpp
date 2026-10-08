#include "Document.Tests.WarpFinish.h"

#include "../Support/DocumentFixture.h"
#include "SharedPublicationMemory.h"
#include "StrokeFinishWork.h"
#include "WarpFinishWork.h"
#include "WarpOpeningWork.h"
#include "WarpSampleWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s warp-finish.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr int kSide = 16;
constexpr Vec2 kOpeningPoint{7.25, 7.25};
constexpr Vec2 kIntermediatePoint{8.25, 7.25};
constexpr Vec2 kSecondPoint{9.25, 7.25};
// The accepted sample is two pixels from the opening center. Spacing is max(1, 6*0.025)=1 for Liquify
// and max(1, 6*0.08)=1 for Smudge. Opening only seeds `last` (and Smudge pickup), so the
// recorded dab centers are 8.25, 9.25 on the x axis, as in legacy SmudgeLiquify.swift.
constexpr BrushSettings kSettings{6, 0.8, 0, 0, 0, 1};
constexpr std::array<std::uint8_t, 4> kOpeningPixel{83, 47, 19, 255};

// Tiny patterned source keeps the warp operation meaningful. Expectations below concern authority, history,
// and exact source restoration; no expected output pixel is sampled from the candidate implementation.
Raster SourceRaster()
{
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
        {
            const auto offset = static_cast<std::size_t>((y * kSide + x) * 4);
            pixels[offset] = static_cast<std::uint8_t>(x * 11 + y * 3);
            pixels[offset + 1] = static_cast<std::uint8_t>(y * 13 + x * 2);
            pixels[offset + 2] = static_cast<std::uint8_t>(x * 5 + y * 7);
            pixels[offset + 3] = 255;
        }
    const auto offset = static_cast<std::size_t>((7 * kSide + 7) * 4);
    for (std::size_t channel = 0; channel < kOpeningPixel.size(); ++channel)
        pixels[offset + channel] = kOpeningPixel[channel];
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

bool SamePixels(const Raster& actual, const Raster& expected) noexcept
{
    if (!actual.HasStorage() || !expected.HasStorage() || actual.Width() != expected.Width() ||
        actual.Height() != expected.Height() || actual.Format() != expected.Format())
        return false;
    std::array<std::uint8_t, kSide * 4> actualRow{};
    std::array<std::uint8_t, kSide * 4> expectedRow{};
    for (int y = 0; y < kSide; ++y)
    {
        actual.ReadRegion(0, y, kSide, 1, actualRow.data(), kSide * 4, 0);
        expected.ReadRegion(0, y, kSide, 1, expectedRow.data(), kSide * 4, 0);
        if (actualRow != expectedRow)
            return false;
    }
    return true;
}

CanvasDocument MakeDocument(const Raster& image)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;
    ImageLayer layer = ImageLayer::Blank(L"Warp finish fixture", Vec2{kSide, kSide});
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
};

struct OpenResult final
{
    std::optional<std::uint64_t> opening;
    bool ready = false;
    bool ownerAppliedDraft = false;
    bool secondDabApplied = false;
};

// Follow the real opening protocol: capture on the owner, construct on a joined worker, apply on the owner,
// then release the prepared wrapper's temporary draft reference before exclusive finish capture.
OpenResult OpenWarp(Fixture& fixture, SmearMode mode = SmearMode::Liquify)
{
    OpenResult opened;
    auto input = fixture.session.PrepareWarpOpeningInput(
        WarpOpeningOptions{kSettings, mode, kOpeningPoint, std::this_thread::get_id()});
    if (!input)
        return opened;
    std::optional<WarpOpeningResult> result;
    std::thread worker(
        [&]
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            result.emplace(DocumentSession::PrepareWarpOpeningPublication(std::move(input.value()), CancelToken{}, {},
                                                                          std::move(memory)));
        });
    worker.join();
    opened.ready = result && result->status == WarpOpeningStatus::Ready && result->prepared.has_value();
    if (!opened.ready)
        return opened;
    auto& prepared = result->prepared.value();
    if (!fixture.session.ApplyPreparedWarpOpeningIfCurrent(prepared))
        return opened;
    opened.opening = fixture.session.PendingWarpOpening();
    auto temporaryOwner = prepared.TakeAppliedDraftForOwner();
    opened.ownerAppliedDraft =
        opened.opening && temporaryOwner && temporaryOwner.get() == fixture.session.PendingWarp();
    temporaryOwner.reset();
    if (!opened.ownerAppliedDraft || !fixture.session.PendingWarp())
        return opened;

    const auto plan = fixture.session.PendingWarp()->PlanSampleMemory(kSecondPoint);
    auto sample = plan ? fixture.session.TakeWarpSampleInput(opened.opening.value(), plan.value()) : std::nullopt;
    if (!sample)
        return opened;
    std::optional<WarpSampleInput> completed;
    bool appended = false;
    std::thread sampleWorker(
        [&]
        {
            appended = sample->AppendForWork(CancelToken{});
            completed.emplace(std::move(sample.value()));
        });
    sampleWorker.join();
    opened.secondDabApplied = appended && completed && fixture.session.ApplyWarpSampleIfCurrent(completed.value());
    if (completed && !opened.secondDabApplied)
    {
        std::thread retire([input = std::move(completed.value())]() mutable {});
        retire.join();
    }
    return opened;
}

struct FinishWork final
{
    std::optional<WarpFinishInput> input;
    std::optional<StrokeFinishInput> strokeInput;
    std::optional<StrokeFinishResult> result;
    bool freshUnboundMemory = false;
    bool finishBeforeAllPointsRefused = false;
    bool pointsMatchAcceptedOrder = false;
    bool allSamplesComposed = false;
    bool publicationBeforeFinalComposeRefused = false;
    bool finalCoverageComposed = false;
    bool publicationTakenOnce = false;
    bool duplicateConversionRefused = false;
    bool movedFromReplayRefused = false;
    bool retired = false;

    FinishWork() = default;
    FinishWork(const FinishWork&) = delete;
    FinishWork& operator=(const FinishWork&) = delete;
    ~FinishWork()
    {
        Retire();
    }

    void Prepare()
    {
        if (!input)
            return;
        std::thread worker(
            [&]
            {
                WarpFinishInput moved(std::move(input.value()));
                movedFromReplayRefused = input.value().PrepareReplayForWork(
                                             false, CancelToken{}, SharedPublicationMemory::CreateForWork()) == nullptr;
                auto memory = SharedPublicationMemory::CreateForWork();
                freshUnboundMemory = memory && !memory->IsBound();
                // This pointer proves replay construction only; all replay progress goes through `moved`.
                const BrushStroke* replay = moved.PrepareReplayForWork(false, CancelToken{}, std::move(memory));
                if (!replay)
                    return;
                finishBeforeAllPointsRefused = !moved.PrepareFinishForWork(CancelToken{}).has_value();
                const auto points = moved.PointsForWork();
                pointsMatchAcceptedOrder =
                    points.size() == 2 && points[0] == kIntermediatePoint && points[1] == kSecondPoint;
                allSamplesComposed = pointsMatchAcceptedOrder;
                for (std::size_t index = 0; index < points.size(); ++index)
                {
                    auto sample = moved.PrepareNextSampleForWork(CancelToken{});
                    if (!sample || !moved.ComposeSampleForWork(sample.value()))
                    {
                        allSamplesComposed = false;
                        break;
                    }
                }
                if (!allSamplesComposed)
                    return;
                {
                    auto final = moved.PrepareFinishForWork(CancelToken{});
                    if (final)
                    {
                        publicationBeforeFinalComposeRefused = !moved.TakeReplayPublicationForWork().has_value();
                        finalCoverageComposed = moved.ComposeFinishForWork(final.value());
                    }
                } // Finish coverage buffers borrow replay storage until composition has returned.
                if (!finalCoverageComposed)
                    return;
                auto transferred = moved.TakeReplayPublicationForWork();
                publicationTakenOnce = transferred.has_value() && transferred->GestureKind() == StrokeGestureKind::Warp;
                if (transferred)
                    strokeInput.emplace(std::move(transferred.value()));
                duplicateConversionRefused = !moved.TakeReplayPublicationForWork().has_value();
                if (strokeInput)
                {
                    result.emplace(DocumentSession::PrepareStrokeFinishPublication(std::move(strokeInput.value()),
                                                                                   CancelToken{}, {}, {}));
                }
            });
        worker.join();
    }

    [[nodiscard]] bool Ready() const noexcept
    {
        return result && result->status == StrokeFinishStatus::Ready && result->publication.has_value();
    }

    [[nodiscard]] PreparedStrokeFinish& Publication()
    {
        return result->publication.value();
    }

    void Retire()
    {
        if (retired)
            return;
        std::thread worker(
            [&]
            {
                result.reset();
                strokeInput.reset();
                input.reset();
                retired = true;
            });
        worker.join();
    }
};

struct CommittedState final
{
    std::optional<CanvasDocument> document;
    Guid revision;
    int undo = 0;
    std::size_t redo = 0;
    std::uint64_t generation = 0;

    explicit CommittedState(const DocumentSession& session)
        : document(session.Document()), revision(session.history.CurrentRevision()), undo(session.history.UndoCount()),
          redo(session.history.CaptureState().FutureCount()), generation(session.Generation())
    {
    }
};

bool SameCommittedState(const DocumentSession& session, const CommittedState& state) noexcept
{
    return session.Document() == state.document && session.history.CurrentRevision() == state.revision &&
           session.history.UndoCount() == state.undo && session.history.CaptureState().FutureCount() == state.redo &&
           session.Generation() == state.generation;
}

// The fixture is a 16x16 patterned CPU raster and a worker-prepared Warp opening. The normal path emits
// 31 runtime assertions across Liquify, Smudge, capture refusal/discard, metadata and generation staleness, and
// cancel/reopen. These are independent authority/history expectations; legacy numeric goldens are neither changed nor
// reused.
void CaptureAndFinish(SmearMode mode)
{
    Fixture fixture;
    FinishWork work; // Must retire worker-built publication owners before fixture/session destruction.
    const CommittedState original(fixture.session);
    const auto originalRoot = fixture.session.Document()->layers.Identity();
    const OpenResult opened = OpenWarp(fixture, mode);
    Expect(opened.ready && opened.ownerAppliedDraft && opened.secondDabApplied && opened.opening &&
               fixture.session.PendingWarp(),
           mode == SmearMode::Liquify ? "liquify.opened-through-worker-publication"
                                      : "smudge.opened-through-worker-publication");
    if (!opened.opening || !opened.secondDabApplied || !fixture.session.PendingWarp())
        return;

    auto captured = fixture.session.TakeWarpFinishInput(opened.opening.value());
    if (captured)
        work.input.emplace(std::move(captured.value()));
    Expect(work.input && fixture.session.HasPendingWarp() && fixture.session.PendingWarp() == nullptr &&
               fixture.session.PendingWarpOpening() == opened.opening &&
               SameCommittedState(fixture.session, original) &&
               fixture.session.Document()->layers.Identity() == originalRoot,
           mode == SmearMode::Liquify ? "liquify.capture-retains-root-history-generation-and-scalar-opening"
                                      : "smudge.capture-retains-root-history-generation-and-scalar-opening");
    if (!work.input)
        return;

    work.Prepare();
    Expect(work.freshUnboundMemory && work.pointsMatchAcceptedOrder && work.allSamplesComposed &&
               work.finalCoverageComposed && work.publicationTakenOnce && work.duplicateConversionRefused &&
               work.movedFromReplayRefused && work.Ready(),
           mode == SmearMode::Liquify ? "liquify.joined-replay-seals-one-warp-publication"
                                      : "smudge.joined-replay-seals-one-warp-publication");
    Expect(work.finishBeforeAllPointsRefused, mode == SmearMode::Liquify
                                                  ? "liquify-finish-before-all-captured-points-refused"
                                                  : "smudge-finish-before-all-captured-points-refused");
    Expect(work.publicationBeforeFinalComposeRefused, mode == SmearMode::Liquify
                                                          ? "liquify-publication-before-final-compose-refused"
                                                          : "smudge-publication-before-final-compose-refused");
    if (!work.Ready())
        return;

    Expect(fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()),
           mode == SmearMode::Liquify ? "liquify-prepared-publication-current-before-apply"
                                      : "smudge-prepared-publication-current-before-apply");
    const bool applied = fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication());
    const auto expectedName = mode == SmearMode::Liquify ? L"Liquify" : L"Smudge";
    Expect(applied && fixture.session.history.UndoCount() == original.undo + 1 &&
               fixture.session.history.UndoName() == expectedName &&
               !fixture.session.PendingWarpOpening().has_value() &&
               !fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()) &&
               !fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication()),
           mode == SmearMode::Liquify ? "liquify-applies-once-with-one-liquify-history-entry"
                                      : "smudge-applies-once-with-one-smudge-history-entry");
    if (!applied)
        return;
    fixture.session.Undo();
    const ImageLayer* restored = fixture.session.ActiveLayer();
    Expect(fixture.session.Document() == original.document && restored && restored->asset &&
               SamePixels(restored->asset->image, fixture.source) &&
               fixture.session.history.CurrentRevision() == original.revision &&
               fixture.session.history.UndoCount() == original.undo,
           mode == SmearMode::Liquify ? "liquify-undo-restores-literal-original-document-and-history"
                                      : "smudge-undo-restores-literal-original-document-and-history");
}

void CaptureRefusalsAndExactDiscard()
{
    Fixture fixture;
    FinishWork work;
    const OpenResult opened = OpenWarp(fixture);
    Expect(opened.opening && opened.ownerAppliedDraft && opened.secondDabApplied,
           "refusal.fixture-has-two-owner-published-dabs");
    if (!opened.opening || !opened.secondDabApplied)
        return;
    const auto wrong = opened.opening.value() ^ std::uint64_t{1};
    auto wrongCapture = fixture.session.TakeWarpFinishInput(wrong);
    Expect(!wrongCapture && fixture.session.PendingWarp() && fixture.session.PendingWarpOpening() == opened.opening,
           "refusal-wrong-opening-preserves-live-draft");
    {
        Fixture unopened;
        Expect(!unopened.session.TakeWarpFinishInput(0) && !unopened.session.HasPendingWarp(),
               "refusal-missing-pending-opening");
    }
    auto borrowed = fixture.session.WeakPendingWarpForTest().lock();
    auto borrowedCapture = fixture.session.TakeWarpFinishInput(opened.opening.value());
    bool foreignOwnerRefused = false;
    std::thread foreignOwner(
        [&] { foreignOwnerRefused = !fixture.session.TakeWarpFinishInput(opened.opening.value()).has_value(); });
    foreignOwner.join();
    Expect(borrowed && !borrowedCapture && foreignOwnerRefused && fixture.session.PendingWarp() == borrowed.get() &&
               fixture.session.PendingWarpOpening() == opened.opening,
           "refusal-known-owner-and-live-borrow-preserve-draft");
    borrowed.reset();

    auto captured = fixture.session.TakeWarpFinishInput(opened.opening.value());
    if (captured)
        work.input.emplace(std::move(captured.value()));
    Expect(work.input && !work.input->PrepareReplayForWork(false, {}, SharedPublicationMemory::CreateForWork()) &&
               fixture.session.PendingWarpOpening() == opened.opening,
           "captured-original-owner-cannot-run-worker-replay-preparation");
    Expect(work.input && !fixture.session.DiscardStrokeFinishIfCurrent(opened.opening.value()) &&
               fixture.session.HasPendingWarp() && fixture.session.PendingWarp() == nullptr &&
               fixture.session.PendingWarpOpening() == opened.opening,
           "discard-brush-finish-kind-cannot-clear-warp-finish-marker");
    Expect(work.input && fixture.session.PendingWarp() == nullptr && fixture.session.HasPendingWarp() &&
               !fixture.session.DiscardWarpFinishIfCurrent(wrong) &&
               fixture.session.PendingWarpOpening() == opened.opening,
           "discard-wrong-opening-cannot-clear-captured-current-barrier");
    Expect(work.input && fixture.session.DiscardWarpFinishIfCurrent(opened.opening.value()) &&
               !fixture.session.DiscardWarpFinishIfCurrent(opened.opening.value()) && !fixture.session.HasPendingWarp(),
           "discard-exact-opening-clears-current-barrier-once");
}

void GenerationMismatchMakesPreparedResultStale()
{
    Fixture fixture;
    FinishWork work;
    const OpenResult opened = OpenWarp(fixture);
    if (!opened.opening || !opened.secondDabApplied)
    {
        Expect(false, "generation-mismatch.fixture-opened");
        return;
    }
    auto captured = fixture.session.TakeWarpFinishInput(opened.opening.value());
    if (captured)
        work.input.emplace(std::move(captured.value()));
    work.Prepare();
    Expect(work.Ready(), "generation-mismatch.worker-prepares-finish");
    if (!work.Ready())
        return;
    const CommittedState beforeInvalidation(fixture.session);
    fixture.session.EditForTest([](CanvasDocument&) {});
    const CommittedState invalidated(fixture.session);
    const ImageLayer* sourceLayer = fixture.session.ActiveLayer();
    Expect(invalidated.generation != beforeInvalidation.generation &&
               !fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()) &&
               !fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication()) &&
               SameCommittedState(fixture.session, invalidated) && sourceLayer && sourceLayer->asset &&
               SamePixels(sourceLayer->asset->image, fixture.source) && SamePixels(fixture.source, SourceRaster()) &&
               fixture.session.PendingWarpOpening() == opened.opening,
           "generation-mismatch-refusal-preserves-source-history-and-finishing-authority");
    static_cast<void>(fixture.session.DiscardWarpFinishIfCurrent(opened.opening.value()));
}

void CompetingOpacityEditMakesPreparedResultStale()
{
    Fixture fixture;
    FinishWork work;
    const OpenResult opened = OpenWarp(fixture);
    if (!opened.opening || !opened.secondDabApplied)
    {
        Expect(false, "opacity-edit.fixture-opened");
        return;
    }
    auto captured = fixture.session.TakeWarpFinishInput(opened.opening.value());
    if (captured)
        work.input.emplace(std::move(captured.value()));
    work.Prepare();
    Expect(work.Ready(), "opacity-edit.worker-prepares-finish");
    if (!work.Ready())
        return;

    const CommittedState beforeEdit(fixture.session);
    fixture.session.SetOpacity(0.5);
    const CommittedState edited(fixture.session);
    Expect(fixture.session.ActiveLayer() && fixture.session.ActiveLayer()->opacity == 0.5 &&
               edited.undo == beforeEdit.undo + 1 && fixture.session.history.UndoName() == L"Layer Opacity" &&
               edited.document != beforeEdit.document,
           "opacity-edit-records-legitimate-new-document-history");
    const ImageLayer* sourceLayer = fixture.session.ActiveLayer();
    Expect(!fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()) &&
               !fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication()) &&
               SameCommittedState(fixture.session, edited) && sourceLayer && sourceLayer->asset &&
               SamePixels(sourceLayer->asset->image, fixture.source) &&
               fixture.session.PendingWarpOpening() == opened.opening,
           "opacity-edit-stale-refusal-preserves-new-source-history-and-finish-marker");
    static_cast<void>(fixture.session.DiscardWarpFinishIfCurrent(opened.opening.value()));
}

void CancelAndReopenRejectOldPublication()
{
    Fixture fixture;
    FinishWork work;
    const CommittedState original(fixture.session);
    const OpenResult first = OpenWarp(fixture);
    if (!first.opening || !first.secondDabApplied)
    {
        Expect(false, "cancel-reopen.fixture-opened");
        return;
    }
    auto captured = fixture.session.TakeWarpFinishInput(first.opening.value());
    if (captured)
        work.input.emplace(std::move(captured.value()));
    work.Prepare();
    Expect(work.Ready() && fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()),
           "cancel-reopen-prepared-publication-current-before-cancel");
    if (!work.Ready())
        return;
    fixture.session.CancelWarp();
    const OpenResult reopened = OpenWarp(fixture, SmearMode::Smudge);
    Expect(reopened.opening && reopened.opening != first.opening && reopened.secondDabApplied &&
               !fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()) &&
               !fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication()) &&
               fixture.session.PendingWarp() && fixture.session.PendingWarpOpening() == reopened.opening &&
               SameCommittedState(fixture.session, original),
           "cancel-reopen-old-prepared-result-stays-stale-and-preserves-new-draft");
    fixture.session.CancelWarp();
}
} // namespace

int RunWarpFinishTests()
{
    gFailures = 0;
    gAssertions = 0;
    static_assert(!std::is_copy_constructible_v<WarpFinishInput>);
    static_assert(std::is_nothrow_move_constructible_v<WarpFinishInput>);
    static_assert(!std::is_copy_constructible_v<StrokeFinishInput>);
    static_assert(std::is_nothrow_move_constructible_v<StrokeFinishInput>);
    static_assert(!std::is_copy_constructible_v<PreparedStrokeFinish>);
    CaptureAndFinish(SmearMode::Liquify);
    CaptureAndFinish(SmearMode::Smudge);
    CaptureRefusalsAndExactDiscard();
    GenerationMismatchMakesPreparedResultStale();
    CompetingOpacityEditMakesPreparedResultStale();
    CancelAndReopenRejectOldPublication();
    std::printf("SUMMARY warp-finish checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
