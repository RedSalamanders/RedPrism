#include "Document.Tests.StrokeFinishPublication.h"

#include "../Support/DocumentFixture.h"
#include "HistoryRestoreWork.h"
#include "StrokeFinishWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
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
    std::printf("%s strokefinish-publication.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void ExpectCase(bool condition, const char* group, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s strokefinish-publication.%s.%s\n", condition ? "PASS" : "FAIL", group, name);
    gFailures += condition ? 0 : 1;
}

static_assert(!std::is_copy_constructible_v<StrokeFinishInput>);
static_assert(std::is_nothrow_move_constructible_v<StrokeFinishInput>);
static_assert(!std::is_move_assignable_v<StrokeFinishInput>);
static_assert(!std::is_copy_constructible_v<PreparedStrokeFinish>);

constexpr int kSide = 32;
constexpr std::array<std::uint8_t, 4> kSourceBgra{50, 60, 70, 255};
constexpr std::array<std::uint8_t, 4> kPaintBgra{0, 0, 255, 255};
// Independent source-over reference, also used by ReviewedCoreCoverageTests-r2:
// radius1000 covers every center of this 32x32 canvas. Coverage=opacity=1, so
// straight red source-over opaque BGRA(50,60,70,255) is exactly BGRA(0,0,255,255).
// On a mask, full black coverage makes every original gray128 byte exactly zero.
// No expected pixel is obtained by sampling a prepared result; legacy goldens stay unchanged.
Raster SourceRaster()
{
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    for (std::size_t i = 0; i < pixels.size(); i += kSourceBgra.size())
        for (std::size_t c = 0; c < kSourceBgra.size(); ++c)
            pixels[i + c] = kSourceBgra[c];
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster MaskRaster()
{
    std::array<std::uint8_t, kSide * kSide> pixels{};
    pixels.fill(128);
    return Raster::Make(kSide, kSide, RasterFormat::Gray8, pixels.data(), pixels.size());
}

bool AllBgra(const Raster& raster, const std::array<std::uint8_t, 4>& expected) noexcept
{
    if (!raster.HasStorage() || raster.Width() != kSide || raster.Height() != kSide ||
        raster.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, kSide * kSide * 4> actual{};
    raster.ReadRegion(0, 0, kSide, kSide, actual.data(), kSide * 4, 0);
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (actual[i] != expected[i % expected.size()])
        {
            std::printf("DIAG strokefinish-publication pixel=%zu channel=%zu actual=%u expected=%u\n", i / 4, i % 4,
                        static_cast<unsigned int>(actual[i]), static_cast<unsigned int>(expected[i % 4]));
            return false;
        }
    return true;
}

bool AllGray(const Raster& raster, std::uint8_t expected) noexcept
{
    if (!raster.HasStorage() || raster.Width() != kSide || raster.Height() != kSide ||
        raster.Format() != RasterFormat::Gray8)
        return false;
    std::array<std::uint8_t, kSide * kSide> actual{};
    raster.ReadRegion(0, 0, kSide, kSide, actual.data(), kSide, 0);
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (actual[i] != expected)
        {
            std::printf("DIAG strokefinish-publication mask-pixel=%zu actual=%u expected=%u\n", i,
                        static_cast<unsigned int>(actual[i]), static_cast<unsigned int>(expected));
            return false;
        }
    return true;
}

bool ActiveBgra(const DocumentSession& session, const std::array<std::uint8_t, 4>& expected) noexcept
{
    const ImageLayer* layer = session.ActiveLayer();
    return layer != nullptr && layer->asset.has_value() && AllBgra(layer->asset.value().image, expected);
}

bool ActiveMask(const DocumentSession& session, std::uint8_t expected) noexcept
{
    const ImageLayer* layer = session.ActiveLayer();
    return layer != nullptr && layer->mask.has_value() && AllGray(layer->mask.value().asset.image, expected);
}

CanvasDocument FixtureDocument(const Raster& source, const Raster& mask = {})
{
    // This is the current test fixture owner API: mutable fixture vector -> immutable seal.
    // It never writes a published CanvasDocument's RecordRoot or uses EditForTest to invalidate authority.
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;
    ImageLayer layer = ImageLayer::Blank(L"Stroke finish publication fixture", Vec2{kSide, kSide});
    layer.asset = RasterAsset{source, Raster::MakeThumbnail(source), layer.name};
    if (mask.HasStorage())
    {
        LayerMask ownedMask;
        ownedMask.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"Publication mask"};
        layer.mask = std::move(ownedMask);
    }
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

struct Fixture final
{
    Raster source = SourceRaster();
    Raster mask;
    DocumentSession session;

    explicit Fixture(bool withMask = false)
    {
        if (withMask)
            mask = MaskRaster();
        CanvasDocument document = FixtureDocument(source, mask);
        const Guid active = document.layers.front().id;
        session.ReplaceDocument(std::move(document), active);
        session.SetBrushEngine(&CpuBrushCoverage::Shared());
        session.brushSettings = BrushSettings{2000, 1, 1, 0, 0, 1};
        if (withMask)
            session.SelectLayerTarget(active, true);
        session.maskPaintWhite = false;
    }

    [[nodiscard]] bool BeginFullCanvasStroke()
    {
        if (!session.BeginBrush(Vec2{0, 16}, BrushKind::Paint))
            return false;
        session.ContinueBrush(Vec2{16, 16});
        session.ContinueBrush(Vec2{32, 16});
        return session.PendingBrush() != nullptr && session.PendingBrushOpening().has_value();
    }
};

struct OwnerState final
{
    std::optional<CanvasDocument> document;
    DocumentHistory::StateView history;
    Guid revision;
    int undoCount;
    std::size_t redoCount;
    std::uint64_t generation;
    std::optional<Guid> active;
    bool maskSelected;
    std::optional<std::uint64_t> opening;
    const BrushStroke* pending;

    explicit OwnerState(const DocumentSession& session)
        : document(session.Document()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), undoCount(session.history.UndoCount()),
          redoCount(history.FutureCount()), generation(session.Generation()), active(session.ActiveLayerID()),
          maskSelected(session.IsMaskSelected()), opening(session.PendingBrushOpening()),
          pending(session.PendingBrush())
    {
    }
};

bool SameCommittedOwner(const DocumentSession& session, const OwnerState& before) noexcept
{
    return session.Document() == before.document && (session.Document().has_value() == before.document.has_value()) &&
           (!before.document.has_value() ||
            session.Document().value().layers.Identity() == before.document.value().layers.Identity()) &&
           session.history.IsCurrent(before.history) && session.history.CurrentRevision() == before.revision &&
           session.history.UndoCount() == before.undoCount &&
           session.history.CaptureState().FutureCount() == before.redoCount &&
           session.history.UndoName() == before.history.UndoName() &&
           session.history.RedoName() == before.history.RedoName() && session.Generation() == before.generation &&
           session.ActiveLayerID() == before.active && session.IsMaskSelected() == before.maskSelected;
}

bool SameOwner(const DocumentSession& session, const OwnerState& before) noexcept
{
    return SameCommittedOwner(session, before) && session.PendingBrushOpening() == before.opening &&
           session.PendingBrush() == before.pending;
}

std::uint64_t WrongOpening(std::uint64_t opening) noexcept
{
    return opening ^ std::uint64_t{1}; // Different without assuming the next marker or overflowing.
}

// No live DocumentSession enters a worker closure. This aggregate owns the transferred
// input/result; even an early test return resets the complete publication graph on a joined worker.
struct FinishWork final
{
    std::optional<StrokeFinishInput> input;
    std::optional<StrokeFinishResult> result;
    bool returnedCpuEngine = false;
    bool ownerEndedEngine = false;
    bool preparedCpu = false;
    bool composed = false;
    bool movedInputRefused = false;
    bool retired = false;

    FinishWork() = default;
    FinishWork(const FinishWork&) = delete;
    FinishWork& operator=(const FinishWork&) = delete;
    ~FinishWork()
    {
        Retire();
    }

    [[nodiscard]] bool TakeOnOwner(DocumentSession& session, std::uint64_t opening)
    {
        BrushCoverageEngine* ownerEngine = nullptr;
        auto captured = session.TakeStrokeFinishInput(opening, ownerEngine);
        returnedCpuEngine = ownerEngine == &CpuBrushCoverage::Shared();
        if (!captured.has_value())
            return false;
        input.emplace(std::move(captured.value()));
        if (ownerEngine != nullptr)
        {
            ownerEngine->EndStroke(); // Owner only; CPU is stateless, so no density is lost.
            ownerEndedEngine = true;
        }
        return true;
    }

    void Prepare(CancelToken cancellation = {}, StrokeFinishObserver observer = {},
                 DocumentHistory::PrepareObserver historyObserver = {}, bool probeMovedInput = false)
    {
        if (!input.has_value())
            return;
        std::thread worker(
            [&]
            {
                {
                    BrushStroke& draft = input.value().DraftForWork();
                    const auto final = draft.PrepareFinalCoverage(cancellation);
                    if (!final.has_value())
                        return;
                    preparedCpu = !final.value().needsGpu && !final.value().tiles.empty();
                    composed = draft.ComposeFinalCoverage(final.value());
                } // FinalCoverage borrows end before the factory consumes or destroys the private input.
                if (!composed)
                    return;
                // Actual draft declaration: input/cancellation/observer required; history observer explicit too.
                result.emplace(DocumentSession::PrepareStrokeFinishPublication(std::move(input.value()), cancellation,
                                                                               observer, historyObserver));
                if (probeMovedInput)
                {
                    // Factory guards moved authority; never borrow DraftForWork from a moved-from input.
                    const auto reused = DocumentSession::PrepareStrokeFinishPublication(
                        std::move(input.value()), cancellation, {}, historyObserver);
                    movedInputRefused = reused.status == StrokeFinishStatus::Invalid && !reused.publication.has_value();
                }
            });
        worker.join();
    }

    [[nodiscard]] bool Ready() const noexcept
    {
        return preparedCpu && composed && result.has_value() && result.value().status == StrokeFinishStatus::Ready &&
               result.value().publication.has_value();
    }

    [[nodiscard]] PreparedStrokeFinish& Publication()
    {
        return result.value().publication.value();
    }

    void Retire()
    {
        if (retired)
            return;
        std::thread worker(
            [&]
            {
                result.reset();
                input.reset();
                retired = true;
            });
        worker.join();
    }
};

void SuccessfulPublication(bool withMask)
{
    const char* group = withMask ? "ready.mask" : "ready.image";
    Fixture fixture(withMask);
    FinishWork work; // Declared after the fixture: retirement joins before fixture destruction.
    const OwnerState original(fixture.session);
    ExpectCase(fixture.BeginFullCanvasStroke(), group, "actual-begin-and-two-continuations");
    const auto opening = fixture.session.PendingBrushOpening();
    ExpectCase(opening.has_value(), group, "accepted-opening-present");
    if (!opening.has_value())
        return;
    const std::weak_ptr<BrushStroke> draft = fixture.session.WeakPendingBrushForTest();
    ExpectCase(work.TakeOnOwner(fixture.session, opening.value()), group, "exact-opening-captured");
    ExpectCase(work.returnedCpuEngine && work.ownerEndedEngine, group, "cpu-engine-ended-on-owner-after-take");
    ExpectCase(fixture.session.PendingBrush() == nullptr, group, "live-draft-detached");
    ExpectCase(fixture.session.PendingBrushOpening() == opening, group, "exact-opening-retained-while-detached");
    ExpectCase(SameCommittedOwner(fixture.session, original), group, "capture-preserves-original-root-and-history");
    const OwnerState detached(fixture.session);
    ExpectCase(!fixture.session.BeginBrush(Vec2{8, 8}, BrushKind::Paint), group,
               "younger-begin-refused-while-detached");
    ExpectCase(SameOwner(fixture.session, detached), group, "younger-refusal-preserves-detached-owner");
    work.Prepare({}, {}, {}, true);
    ExpectCase(work.Ready(), group, "worker-final-coverage-and-publication-ready");
    ExpectCase(work.movedInputRefused, group, "moved-input-factory-reuse-invalid-with-no-publication");
    ExpectCase(SameOwner(fixture.session, detached), group, "private-preparation-preserves-owner-root-and-history");
    if (!work.Ready())
        return;
    ExpectCase(fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()), group,
               "publication-current-before-apply");
    DocumentHistory::ResidencyPermit permit;
    const auto residency = work.Publication().TryAcquireHistoryResidency(permit);
    ExpectCase(residency == DocumentHistory::ResidencyValidation::Ready, group, "owner-history-residency-ready");
    const bool applied = residency == DocumentHistory::ResidencyValidation::Ready &&
                         fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication());
    permit.Reset();
    ExpectCase(applied, group, "owner-apply-succeeds");
    if (!applied)
        return;
    ExpectCase(fixture.session.history.UndoCount() == original.undoCount + 1, group, "exactly-one-history-entry");
    ExpectCase(fixture.session.history.UndoName() == (withMask ? L"Paint Mask" : L"Brush Stroke"), group,
               "history-name-preserved");
    ExpectCase(!fixture.session.PendingBrushOpening().has_value(), group, "accepted-finish-marker-cleared-after-apply");
    ExpectCase(ActiveBgra(fixture.session, withMask ? kSourceBgra : kPaintBgra), group,
               "literal-all-pixel-image-after-apply");
    if (withMask)
    {
        Expect(ActiveMask(fixture.session, 0), "ready.mask.literal-all-mask-pixels-zero");
        const ImageLayer* layer = fixture.session.ActiveLayer();
        Expect(layer != nullptr && layer->mask.has_value() && layer->mask.value().asset.thumbnail.IsMaterialized() &&
                   AllGray(layer->mask.value().asset.thumbnail, 0),
               "ready.mask.eager-thumbnail-all-pixels-zero");
    }
    const OwnerState published(fixture.session);
    ExpectCase(!fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()), group,
               "applied-publication-not-current");
    ExpectCase(!fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication()), group,
               "double-publication-refused");
    ExpectCase(SameOwner(fixture.session, published), group,
               "double-publication-preserves-root-history-and-generation");
    fixture.session.Undo();
    ExpectCase(ActiveBgra(fixture.session, kSourceBgra), group, "undo-restores-every-literal-source-pixel");
    ExpectCase(fixture.session.Document() == original.document, group, "undo-restores-original-document");
    ExpectCase(fixture.session.history.UndoCount() == original.undoCount &&
                   fixture.session.history.CurrentRevision() == original.revision,
               group, "undo-restores-original-history-revision");
    if (withMask)
        Expect(ActiveMask(fixture.session, 128), "ready.mask.undo-restores-every-gray128-mask-pixel");
    fixture.session.Redo();
    ExpectCase(ActiveBgra(fixture.session, withMask ? kSourceBgra : kPaintBgra), group,
               "redo-restores-every-literal-result-pixel");
    ExpectCase(fixture.session.history.UndoCount() == original.undoCount + 1 && !fixture.session.history.CanRedo(),
               group, "redo-restores-single-entry-with-no-extra-redo");
    if (withMask)
        Expect(ActiveMask(fixture.session, 0), "ready.mask.redo-restores-every-zero-mask-pixel");
    ExpectCase(AllBgra(fixture.source, kSourceBgra), group, "original-raster-remains-all-literal-source-pixels");
    if (withMask)
        Expect(AllGray(fixture.mask, 128), "ready.mask.original-mask-raster-remains-gray128");
    work.Retire();
    ExpectCase(draft.expired(), group, "private-draft-released-after-joined-worker-retirement");
}

void CaptureRefusalsPreserveLiveDraft()
{
    Fixture fixture;
    FinishWork accepted;
    Expect(fixture.BeginFullCanvasStroke(), "capture.actual-begin");
    const auto opening = fixture.session.PendingBrushOpening();
    Expect(opening.has_value(), "capture.opening-present");
    if (!opening.has_value())
        return;
    const OwnerState live(fixture.session);
    BrushCoverageEngine* empty = nullptr;
    auto wrong = fixture.session.TakeStrokeFinishInput(WrongOpening(opening.value()), empty);
    Expect(!wrong.has_value(), "capture.wrong-opening-refused");
    Expect(empty == nullptr, "capture.wrong-opening-preserves-null-output");
    Expect(SameOwner(fixture.session, live), "capture.wrong-opening-preserves-live-draft-root-and-history");
    BrushCoverageEngine* nonempty = &CpuBrushCoverage::Shared();
    auto blocked = fixture.session.TakeStrokeFinishInput(opening.value(), nonempty);
    Expect(!blocked.has_value(), "capture.nonempty-engine-output-refused");
    Expect(nonempty == &CpuBrushCoverage::Shared(), "capture.nonempty-engine-output-not-overwritten");
    Expect(SameOwner(fixture.session, live), "capture.nonempty-output-preserves-live-draft-root-and-history");
    Expect(accepted.TakeOnOwner(fixture.session, opening.value()), "capture.exact-retry-after-both-refusals-succeeds");
    Expect(accepted.returnedCpuEngine && accepted.ownerEndedEngine, "capture.successful-retry-ends-engine-on-owner");
    fixture.session.CancelBrush();
    // Also retire unexpectedly returned rejected inputs on the worker if an API regression occurs.
    std::thread retirement(
        [&]
        {
            wrong.reset();
            blocked.reset();
        });
    retirement.join();
    Expect(AllBgra(fixture.source, kSourceBgra), "capture.original-source-whole-tile-unchanged");
}

void CancelAfterDetachRejectsOldPublication()
{
    Fixture fixture;
    FinishWork work;
    const OwnerState original(fixture.session);
    Expect(fixture.BeginFullCanvasStroke(), "cancel.actual-begin");
    const auto opening = fixture.session.PendingBrushOpening();
    if (!opening.has_value())
    {
        Expect(false, "cancel.exact-opening-captured");
        return;
    }
    Expect(work.TakeOnOwner(fixture.session, opening.value()), "cancel.exact-opening-captured");
    work.Prepare();
    Expect(work.Ready(), "cancel.old-publication-ready-before-owner-cancel");
    if (!work.Ready())
        return;
    fixture.session.CancelBrush();
    Expect(!fixture.session.PendingBrushOpening().has_value(), "cancel.detached-marker-cleared-on-owner");
    Expect(SameCommittedOwner(fixture.session, original), "cancel.owner-cancel-preserves-original-root-and-history");
    const OwnerState cancelled(fixture.session);
    Expect(!fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()), "cancel.old-publication-not-current");
    Expect(!fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication()), "cancel.old-publication-refused");
    Expect(SameOwner(fixture.session, cancelled), "cancel.refusal-preserves-original-root-history-and-generation");
    Expect(ActiveBgra(fixture.session, kSourceBgra), "cancel.all-owner-pixels-remain-original");
    Expect(AllBgra(fixture.source, kSourceBgra), "cancel.source-raster-whole-tile-unchanged");
}

void DiscardUsesExactOpening()
{
    Fixture fixture;
    FinishWork work;
    Expect(fixture.BeginFullCanvasStroke(), "discard.actual-begin");
    const auto opening = fixture.session.PendingBrushOpening();
    if (!opening.has_value())
    {
        Expect(false, "discard.exact-opening-captured");
        return;
    }
    Expect(work.TakeOnOwner(fixture.session, opening.value()), "discard.exact-opening-captured");
    work.Prepare();
    Expect(work.Ready(), "discard.old-publication-ready");
    if (!work.Ready())
        return;
    const OwnerState detached(fixture.session);
    Expect(!fixture.session.DiscardStrokeFinishIfCurrent(WrongOpening(opening.value())),
           "discard.wrong-opening-refused");
    Expect(SameOwner(fixture.session, detached), "discard.wrong-opening-preserves-marker-and-owner");
    Expect(fixture.session.DiscardStrokeFinishIfCurrent(opening.value()), "discard.exact-opening-accepted");
    Expect(!fixture.session.PendingBrushOpening().has_value(), "discard.exact-marker-cleared");
    Expect(SameCommittedOwner(fixture.session, detached), "discard.preserves-original-root-and-history");
    const OwnerState discarded(fixture.session);
    Expect(!fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication()), "discard.old-publication-refused");
    Expect(SameOwner(fixture.session, discarded), "discard.old-publication-refusal-preserves-owner");
    Expect(fixture.BeginFullCanvasStroke(), "discard.younger-actual-begin-accepted");
    const auto younger = fixture.session.PendingBrushOpening();
    Expect(younger.has_value() && younger != opening, "discard.younger-marker-is-distinct-without-assumed-increment");
    const OwnerState liveYounger(fixture.session);
    Expect(!fixture.session.DiscardStrokeFinishIfCurrent(opening.value()),
           "discard.old-marker-cannot-discard-younger-live-draft");
    Expect(SameOwner(fixture.session, liveYounger), "discard.old-marker-refusal-preserves-younger-draft");
    fixture.session.CancelBrush();
}

enum class StaleEdit : std::uint8_t
{
    Document,
    Layer,
    Selection
};
struct StaleCase final
{
    StaleEdit edit;
    const char* group;
};
constexpr std::array<StaleCase, 3> kStaleCases{{
    {StaleEdit::Document, "stale.document"},
    {StaleEdit::Layer, "stale.layer"},
    {StaleEdit::Selection, "stale.selection"},
}};

void LegitimateOwnerEditsRejectOldPublication()
{
    for (const auto& test : kStaleCases)
    {
        Fixture fixture;
        FinishWork work;
        ExpectCase(fixture.BeginFullCanvasStroke(), test.group, "actual-begin");
        const auto opening = fixture.session.PendingBrushOpening();
        ExpectCase(opening.has_value() && work.TakeOnOwner(fixture.session, opening.value()), test.group,
                   "exact-opening-captured");
        work.Prepare();
        ExpectCase(work.Ready(), test.group, "old-publication-ready");
        if (!work.Ready())
            continue;
        const OwnerState beforeEdit(fixture.session);
        bool editObserved = false;
        switch (test.edit)
        {
        case StaleEdit::Document:
        {
            CanvasDocument replacement = FixtureDocument(fixture.source);
            const Guid active = replacement.layers.front().id;
            fixture.session.ReplaceDocument(std::move(replacement), active);
            editObserved = fixture.session.Document().has_value() && beforeEdit.document.has_value() &&
                           fixture.session.Document().value().id != beforeEdit.document.value().id;
            break;
        }
        case StaleEdit::Layer:
            fixture.session.SetOpacity(0.5);
            editObserved =
                fixture.session.Document().has_value() && beforeEdit.document.has_value() &&
                fixture.session.ActiveLayer() != nullptr && fixture.session.ActiveLayer()->opacity == 0.5 &&
                fixture.session.Document().value().layers.Identity() != beforeEdit.document.value().layers.Identity();
            break;
        case StaleEdit::Selection:
            fixture.session.SelectAll();
            editObserved =
                fixture.session.Document().has_value() && beforeEdit.document.has_value() &&
                fixture.session.Selection().has_value() &&
                fixture.session.Document().value().layers.Identity() == beforeEdit.document.value().layers.Identity();
            break;
        }
        ExpectCase(editObserved, test.group, "legitimate-owner-edit-observed");
        const OwnerState edited(fixture.session);
        ExpectCase(!fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()), test.group,
                   "old-publication-not-current");
        ExpectCase(!fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication()), test.group,
                   "old-publication-refused");
        ExpectCase(SameOwner(fixture.session, edited), test.group,
                   "refusal-preserves-current-root-history-selection-and-generation");
        ExpectCase(ActiveBgra(fixture.session, kSourceBgra) && AllBgra(fixture.source, kSourceBgra), test.group,
                   "all-owner-and-source-pixels-remain-original");
        fixture.session.CancelBrush();
    }
}

struct CancelStage final
{
    CancelSource cancellation;
    StrokeFinishStage target;
    bool observed = false;
};
void CancelAtStage(void* context, StrokeFinishStage stage) noexcept
{
    auto& state = *static_cast<CancelStage*>(context);
    if (stage == state.target)
    {
        state.observed = true;
        static_cast<void>(state.cancellation.Cancel());
    }
}
struct StageCase final
{
    StrokeFinishStage stage;
    const char* group;
};
constexpr std::array<StageCase, 4> kStageCases{{
    {StrokeFinishStage::Snapshot, "stage.snapshot"},
    {StrokeFinishStage::Mask, "stage.mask"},
    {StrokeFinishStage::Document, "stage.document"},
    {StrokeFinishStage::History, "stage.history"},
}};

void ObserverCancellationNeverPublishes()
{
    for (const auto& test : kStageCases)
    {
        Fixture fixture(test.stage == StrokeFinishStage::Mask);
        FinishWork work;
        ExpectCase(fixture.BeginFullCanvasStroke(), test.group, "actual-begin");
        const auto opening = fixture.session.PendingBrushOpening();
        ExpectCase(opening.has_value() && work.TakeOnOwner(fixture.session, opening.value()), test.group,
                   "exact-opening-captured");
        const OwnerState detached(fixture.session);
        CancelStage cancellation{.target = test.stage};
        work.Prepare(cancellation.cancellation.Token(), {CancelAtStage, &cancellation});
        ExpectCase(work.preparedCpu && work.composed, test.group,
                   "final-coverage-composed-before-factory-cancellation");
        ExpectCase(cancellation.observed, test.group, "exact-observer-stage-reached");
        ExpectCase(work.result.has_value() && work.result.value().status == StrokeFinishStatus::Cancelled, test.group,
                   "factory-returns-exact-cancelled-status");
        ExpectCase(work.result.has_value() && !work.result.value().publication.has_value(), test.group,
                   "no-publication-exposed");
        ExpectCase(SameOwner(fixture.session, detached), test.group,
                   "cancelled-factory-preserves-root-history-and-opening");
        ExpectCase(ActiveBgra(fixture.session, kSourceBgra) && AllBgra(fixture.source, kSourceBgra), test.group,
                   "cancelled-factory-preserves-every-owner-and-source-pixel");
        if (test.stage == StrokeFinishStage::Mask)
            ExpectCase(ActiveMask(fixture.session, 128) && AllGray(fixture.mask, 128), test.group,
                       "mask-source-and-owner-gray128-unchanged");
        if (opening.has_value())
        {
            ExpectCase(fixture.session.DiscardStrokeFinishIfCurrent(opening.value()), test.group,
                       "owner-discards-exact-failed-finish");
            ExpectCase(!fixture.session.PendingBrushOpening().has_value(), test.group,
                       "discard-clears-only-failed-marker");
            ExpectCase(SameCommittedOwner(fixture.session, detached), test.group,
                       "discard-preserves-source-root-and-history");
        }
    }
}

struct CancelHistory final
{
    CancelSource cancellation;
    bool observed = false;
};
void CancelAtHistorySnapshot(void* context, DocumentHistory::PrepareStage stage) noexcept
{
    auto& state = *static_cast<CancelHistory*>(context);
    if (stage == DocumentHistory::PrepareStage::AfterSnapshot)
    {
        state.observed = true;
        static_cast<void>(state.cancellation.Cancel());
    }
}

void HistoryObserverCancellationNeverPublishes()
{
    Fixture fixture;
    FinishWork work;
    Expect(fixture.BeginFullCanvasStroke(), "history-observer.actual-begin");
    const auto opening = fixture.session.PendingBrushOpening();
    Expect(opening.has_value() && work.TakeOnOwner(fixture.session, opening.value()),
           "history-observer.exact-opening-captured");
    const OwnerState detached(fixture.session);
    CancelHistory cancellation;
    work.Prepare(cancellation.cancellation.Token(), {}, {CancelAtHistorySnapshot, &cancellation});
    Expect(cancellation.observed, "history-observer.after-snapshot-checkpoint-reached");
    Expect(work.result.has_value() && work.result.value().status == StrokeFinishStatus::Cancelled,
           "history-observer.exact-cancelled-status");
    Expect(work.result.has_value() && !work.result.value().publication.has_value(),
           "history-observer.no-publication-exposed");
    Expect(SameOwner(fixture.session, detached), "history-observer.original-root-history-and-opening-unchanged");
    Expect(ActiveBgra(fixture.session, kSourceBgra) && AllBgra(fixture.source, kSourceBgra),
           "history-observer.all-owner-and-source-pixels-original");
    if (opening.has_value())
        Expect(fixture.session.DiscardStrokeFinishIfCurrent(opening.value()),
               "history-observer.owner-discards-exact-finish");
}

void ReferenceHistoryCancelsDetachedFinishFirst(bool redo)
{
    const char* group = redo ? "pending-history.redo" : "pending-history.undo";
    Fixture fixture;
    FinishWork work;
    fixture.session.SetOpacity(0.5);
    if (redo)
    {
        fixture.session.SetOpacity(0.25);
        fixture.session.Undo();
    }
    const OwnerState original(fixture.session);
    ExpectCase(fixture.session.ActiveLayer() != nullptr && fixture.session.ActiveLayer()->opacity == 0.5 &&
                   (redo ? fixture.session.history.CanRedo() : fixture.session.history.CanUndo()),
               group, "older-history-entry-exists-before-stroke");
    ExpectCase(fixture.BeginFullCanvasStroke(), group, "actual-begin");
    const auto opening = fixture.session.PendingBrushOpening();
    ExpectCase(opening.has_value() && work.TakeOnOwner(fixture.session, opening.value()), group,
               "exact-opening-captured");
    work.Prepare();
    ExpectCase(work.Ready(), group, "private-final-publication-ready");
    if (!work.Ready())
        return;
    auto authority = fixture.session.CaptureHistoryRestoreAuthority();
    auto history = fixture.session.history.CaptureState();
    bool settledRestoreRefused = false;
    std::thread worker(
        [&]
        {
            auto restore = PrepareHistoryRestorePublication(std::move(authority), std::move(history), redo);
            settledRestoreRefused = !restore.has_value();
        });
    worker.join();
    ExpectCase(settledRestoreRefused, group, "settled-history-factory-refuses-detached-finish");
    if (redo)
        fixture.session.Redo();
    else
        fixture.session.Undo();
    ExpectCase(!fixture.session.PendingBrushOpening().has_value(), group,
               "first-history-command-cancels-finish-marker");
    ExpectCase(SameCommittedOwner(fixture.session, original), group, "first-command-preserves-older-history-and-root");
    const OwnerState cancelled(fixture.session);
    ExpectCase(!fixture.session.IsPreparedStrokeFinishCurrent(work.Publication()), group,
               "cancelled-publication-not-current");
    ExpectCase(!fixture.session.ApplyPreparedStrokeFinishIfCurrent(work.Publication()), group,
               "cancelled-publication-cannot-reappear");
    ExpectCase(SameOwner(fixture.session, cancelled), group, "refused-publication-preserves-history-and-generation");
    ExpectCase(ActiveBgra(fixture.session, kSourceBgra) && AllBgra(fixture.source, kSourceBgra), group,
               "every-owner-and-source-pixel-remains-original");
    if (redo)
        fixture.session.Redo();
    else
        fixture.session.Undo();
    ExpectCase(fixture.session.ActiveLayer() != nullptr &&
                   fixture.session.ActiveLayer()->opacity == (redo ? 0.25 : 1.0),
               group, "next-history-command-moves-original-older-entry");
    ExpectCase(ActiveBgra(fixture.session, kSourceBgra), group, "older-history-move-preserves-literal-image-pixels");
}

} // namespace

int RunStrokeFinishPublicationTests()
{
    gFailures = 0;
    gAssertions = 0;
    SuccessfulPublication(false);
    SuccessfulPublication(true);
    ReferenceHistoryCancelsDetachedFinishFirst(false);
    ReferenceHistoryCancelsDetachedFinishFirst(true);
    CaptureRefusalsPreserveLiveDraft();
    CancelAfterDetachRejectsOldPublication();
    DiscardUsesExactOpening();
    LegitimateOwnerEditsRejectOldPublication();
    ObserverCancellationNeverPublishes();
    HistoryObserverCancellationNeverPublishes();
    std::printf("%d strokefinish-publication assertions; %d failed.\n", gAssertions, gFailures);
    return gFailures;
}
