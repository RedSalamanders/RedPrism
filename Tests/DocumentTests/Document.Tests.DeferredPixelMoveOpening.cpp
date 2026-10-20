#include "Document.Tests.DeferredPixelMoveOpening.h"

#include "../Support/DocumentFixture.h"
#include "BrushStroke.h"
#include "Document.h"
#include "PixelMoveOpeningWork.h"
#include "RasterEditWork.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace DeferredPixelMoveOpeningTests
{
namespace
{
void Expect(bool condition, const char* fact, int& failures)
{
    std::printf("%s deferred-pixelmove-opening.%s\n", condition ? "PASS" : "FAIL", fact);
    failures += condition ? 0 : 1;
}

DocumentSelection Rect(double x, double y, double width, double height)
{
    return DocumentSelection{
        false, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}}};
}

constexpr std::array<std::uint8_t, 4> kTransparent{};
constexpr std::array<std::uint8_t, 4> kRed{0, 0, 255, 255};
constexpr std::array<std::uint8_t, 4> kBlue{255, 0, 0, 255};

bool PixelIs(const Raster& raster, int x, int y, const std::array<std::uint8_t, 4>& expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return false;
    std::array<std::uint8_t, 4> actual{};
    raster.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
    return actual == expected;
}

bool Initialize(DocumentSession& session)
{
    constexpr int width = 100;
    constexpr int height = 40;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto& color = x < 50 ? kRed : kBlue;
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            std::copy(color.begin(), color.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }
    const Raster pixels = Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    if (!pixels.HasStorage())
        return false;
    session.CreateDocument(width, height, true);
    if (!session.Document() || session.Document()->layers.empty())
        return false;
    const Raster thumbnail = Raster::MakeThumbnail(pixels);
    if (!thumbnail.HasStorage())
        return false;
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                ImageLayer& layer = document.layers.front();
                                layer.asset = RasterAsset{pixels, thumbnail, layer.name};
                                document.selection = Rect(10, 10, 10, 10);
                            });
    if (!session.ActiveLayer() || !session.ActiveLayer()->asset || !session.Document()->selection)
        return false;

    // Leave both history directions populated while restoring the legacy fixture selection.
    session.SetSelection(Rect(30, 10, 10, 10), L"Fixture Selection");
    session.Undo();
    return session.Document() && session.Document()->selection.value() == Rect(10, 10, 10, 10) &&
           session.history.CanUndo() && session.history.CanRedo();
}

std::optional<PreparedPixelMoveOpening> PrepareOnWorker(DocumentSession& session, bool duplicate = false)
{
    auto input = session.PrepareDeferredPixelMoveOpeningInput(duplicate);
    if (!input)
        return std::nullopt;
    std::optional<PreparedPixelMoveOpeningResult> workerResult;
    std::thread worker(
        [captured = std::move(input.value()), &workerResult]() mutable
        { workerResult.emplace(DocumentSession::PrepareDeferredPixelMoveOpening(std::move(captured))); });
    worker.join();
    if (!workerResult || workerResult->status != PixelMoveLiftStatus::Ready || !workerResult->prepared)
        return std::nullopt;
    std::optional<PreparedPixelMoveOpening> prepared;
    prepared.emplace(std::move(workerResult->prepared.value()));
    return prepared;
}

void CaptureIsOwnerOnlyAndNonMutating(int& failures)
{
    DocumentSession session;
    Expect(Initialize(session), "capture.fixture-with-red-blue-selection-and-undo-redo", failures);
    if (!session.Document() || !session.ActiveLayer() || !session.ActiveLayer()->asset)
        return;

    const CanvasDocument documentBefore = session.Document().value();
    const Raster sourceBefore = session.ActiveLayer()->asset->image;
    const auto revisionBefore = session.history.CurrentRevision();
    const int pastBefore = session.history.UndoCount();
    const bool canRedoBefore = session.history.CanRedo();
    const std::wstring redoBefore = session.history.RedoName();
    const std::uint64_t generationBefore = session.Generation();
    auto input = session.PrepareDeferredPixelMoveOpeningInput();
    const bool admitted = input.has_value();
    const bool ownerCaptureUnopened =
        admitted && !session.PendingPixelMove() && session.Document().value() == documentBefore &&
        session.ActiveLayer()->asset->image == sourceBefore && session.history.CurrentRevision() == revisionBefore &&
        session.history.UndoCount() == pastBefore && session.history.CanRedo() == canRedoBefore &&
        session.history.RedoName() == redoBefore && session.Generation() == generationBefore &&
        PixelIs(sourceBefore, 10, 10, kRed) && PixelIs(sourceBefore, 60, 10, kBlue);
    Expect(ownerCaptureUnopened, "owner-capture-keeps-source-document-history-and-pending-state", failures);
    if (!admitted)
        return;

    const auto ownerResult = DocumentSession::PrepareDeferredPixelMoveOpening(std::move(input.value()));
    Expect(ownerResult.status == PixelMoveLiftStatus::Invalid && !ownerResult.prepared,
           "opening-construction-on-owner-thread-is-refused", failures);

    auto fresh = session.PrepareDeferredPixelMoveOpeningInput();
    if (!fresh)
    {
        Expect(false, "worker-capture-fixture-ready", failures);
        return;
    }
    std::optional<PreparedPixelMoveOpeningResult> workerResult;
    std::thread worker(
        [captured = std::move(fresh.value()), &workerResult]() mutable
        { workerResult.emplace(DocumentSession::PrepareDeferredPixelMoveOpening(std::move(captured))); });
    worker.join();
    Expect(workerResult && workerResult->status == PixelMoveLiftStatus::Ready && workerResult->prepared &&
               workerResult->prepared->Lift() && workerResult->prepared->Token().target.has_value(),
           "native-worker-builds-lift-and-full-target-token", failures);
    Expect(!session.PendingPixelMove() && session.Document().value() == documentBefore &&
               session.ActiveLayer()->asset->image == sourceBefore &&
               session.history.CurrentRevision() == revisionBefore && session.history.UndoCount() == pastBefore &&
               session.history.CanRedo() == canRedoBefore,
           "worker-preparation-preserves-session-pixels-and-both-history-directions", failures);
}

void ApplyKeepsHistoryThenRasterEditHasLiteralPixels(int& failures)
{
    DocumentSession session;
    Expect(Initialize(session), "apply.fixture", failures);
    if (!session.Document() || !session.ActiveLayer() || !session.ActiveLayer()->asset)
        return;
    auto prepared = PrepareOnWorker(session);
    Expect(prepared.has_value(), "apply.worker-preparation-ready", failures);
    if (!prepared)
        return;

    const auto documentBefore = session.Document().value();
    const auto generationBefore = session.Generation();
    const auto revisionBefore = session.history.CurrentRevision();
    const int pastBefore = session.history.UndoCount();
    const bool canRedoBefore = session.history.CanRedo();
    const std::wstring redoBefore = session.history.RedoName();
    const bool applied = session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(prepared.value());
    const PixelMove* pending = session.PendingPixelMove();
    Expect(applied && pending && pending->deferred && pending->raster == nullptr && pending->offset == Vec2{0, 0} &&
               pending->sequence == 1 && session.Generation() == generationBefore &&
               session.Document().value() == documentBefore && session.history.CurrentRevision() == revisionBefore &&
               session.history.UndoCount() == pastBefore && session.history.CanRedo() == canRedoBefore &&
               session.history.RedoName() == redoBefore,
           "apply-installs-empty-deferred-pending-without-document-or-history-mutation", failures);
    if (!applied || !pending || !prepared->Token().target || !prepared->Lift())
        return;

    Expect(session.MovePreparedDeferredPixelMoveIfCurrent(prepared.value(), 60.4, 5.4) &&
               session.PendingPixelMove()->offset == Vec2{60, 5},
           "gesture-rounding-uses-nearest-integer-pixel-offset", failures);
    const auto movedSequence = session.PendingPixelMove()->sequence;
    Expect(session.MovePreparedDeferredPixelMoveIfCurrent(prepared.value(), 60.49, 5.49) &&
               session.PendingPixelMove()->offset == Vec2{60, 5} &&
               session.PendingPixelMove()->sequence == movedSequence,
           "same-rounded-offset-does-not-advance-sequence", failures);
    Expect(!session.PendingPixelMove()->duplicate, "pending-retains-move-not-duplicate-flag", failures);

    RasterEditInput rasterInput;
    rasterInput.layer = *session.ActiveLayer();
    rasterInput.selection = session.Document()->selection;
    rasterInput.canvasWidth = session.Document()->width;
    rasterInput.canvasHeight = session.Document()->height;
    rasterInput.pixelLimit = kMaxLayerPixels;
    rasterInput.kind = RasterEditKind::PixelMove;
    rasterInput.offset = session.PendingPixelMove()->offset;
    rasterInput.duplicate = session.PendingPixelMove()->duplicate;
    rasterInput.pixelMoveOpening = prepared->Lift();
    const RasterEditResult raster = RunRasterEdit(rasterInput);
    const auto bounds = raster.selection ? raster.selection->Bounds() : std::nullopt;
    Expect(raster.status == RasterEditStatus::Ready && raster.layer && raster.layer->asset && bounds &&
               bounds->minX == 70 && bounds->minY == 15 && bounds->maxX == 80 && bounds->maxY == 25,
           "lifted-raster-edit-returns-moved-selection-bounds-70-15-80-25", failures);
    if (!raster.layer || !raster.layer->asset)
        return;
    const Raster& result = raster.layer->asset->image;
    Expect(PixelIs(result, 10, 10, kTransparent) && PixelIs(result, 19, 19, kTransparent) &&
               PixelIs(result, 70, 15, kRed) && PixelIs(result, 79, 24, kRed) && PixelIs(result, 60, 10, kBlue) &&
               PixelIs(result, 60, 15, kBlue),
           "literal-cut-hole-red-landing-and-blue-outside-selection", failures);
}

void DuplicateAndRefusals(int& failures)
{
    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        Expect(Initialize(session), "duplicate.fixture", failures);
        auto prepared = PrepareOnWorker(session, true);
        const bool applied = prepared && session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(prepared.value());
        const bool moved = applied && session.MovePreparedDeferredPixelMoveIfCurrent(prepared.value(), 60, 5);
        RasterEditInput input;
        if (moved && session.ActiveLayer() && session.Document() && session.PendingPixelMove())
        {
            input.layer = *session.ActiveLayer();
            input.selection = session.Document()->selection;
            input.canvasWidth = session.Document()->width;
            input.canvasHeight = session.Document()->height;
            input.pixelLimit = kMaxLayerPixels;
            input.kind = RasterEditKind::PixelMove;
            input.offset = session.PendingPixelMove()->offset;
            input.duplicate = session.PendingPixelMove()->duplicate;
            input.pixelMoveOpening = prepared->Lift();
        }
        const auto result = moved ? RunRasterEdit(input) : RasterEditResult{};
        Expect(moved && session.PendingPixelMove()->duplicate && result.status == RasterEditStatus::Ready &&
                   result.layer && result.layer->asset && PixelIs(result.layer->asset->image, 10, 10, kRed) &&
                   PixelIs(result.layer->asset->image, 70, 15, kRed) &&
                   PixelIs(result.layer->asset->image, 60, 10, kBlue) &&
                   PixelIs(result.layer->asset->image, 60, 15, kBlue),
               "duplicate-flag-preserves-source-and-lands-red-over-blue-free-area", failures);
    }

    {
        auto firstOwner = std::make_unique<DocumentSession>();
        auto& first = *firstOwner;
        auto secondOwner = std::make_unique<DocumentSession>();
        auto& second = *secondOwner;
        Expect(Initialize(first) && Initialize(second), "cross-session.fixture", failures);
        auto prepared = PrepareOnWorker(first);
        Expect(prepared && !second.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(prepared.value()) &&
                   !second.PendingPixelMove() && first.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(prepared.value()),
               "cross-session-apply-refuses-foreign-token-and-retains-own-preparation", failures);
    }

    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        Expect(Initialize(session), "stale.fixture", failures);
        auto prepared = PrepareOnWorker(session);
        if (!prepared)
        {
            Expect(false, "stale.worker-preparation-ready", failures);
            return;
        }
        session.EditForTest([](CanvasDocument&) {});
        Expect(!session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(prepared.value()) && !session.PendingPixelMove(),
               "stale-document-generation-rejects-opening", failures);
    }

    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        Expect(Initialize(session), "foreign.fixture", failures);
        auto prepared = PrepareOnWorker(session);
        if (!prepared)
        {
            Expect(false, "foreign.worker-preparation-ready", failures);
            return;
        }
        const bool foreignBegun = session.BeginDeferredPixelMove();
        const auto foreignOpening = session.PendingPixelMove() ? session.PendingPixelMove()->opening : 0;
        Expect(foreignBegun && foreignOpening != 0 &&
                   !session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(prepared.value()) &&
                   session.PendingPixelMove() && session.PendingPixelMove()->opening == foreignOpening,
               "foreign-pending-opening-blocks-apply-and-remains-owned", failures);
    }

    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        Expect(Initialize(session), "cancelled.fixture", failures);
        auto input = session.PrepareDeferredPixelMoveOpeningInput();
        CancelSource cancellation;
        const bool cancelled = cancellation.Cancel();
        std::optional<PreparedPixelMoveOpeningResult> result;
        if (input)
        {
            std::thread worker(
                [captured = std::move(input.value()), &result, token = cancellation.Token()]() mutable
                { result.emplace(DocumentSession::PrepareDeferredPixelMoveOpening(std::move(captured), token)); });
            worker.join();
        }
        Expect(input && cancelled && result && result->status == PixelMoveLiftStatus::Cancelled && !result->prepared &&
                   !session.PendingPixelMove(),
               "pre-cancelled-worker-refuses-without-opening-pending-state", failures);
    }

    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        Expect(Initialize(session), "moved-from.fixture", failures);
        auto input = session.PrepareDeferredPixelMoveOpeningInput();
        if (!input)
        {
            Expect(false, "moved-from.input-captured", failures);
            return;
        }
        PixelMoveOpeningInput moved(std::move(input.value()));
        static_cast<void>(moved);
        std::optional<PreparedPixelMoveOpeningResult> result;
        std::thread worker([captured = std::move(input.value()), &result]() mutable
                           { result.emplace(DocumentSession::PrepareDeferredPixelMoveOpening(std::move(captured))); });
        worker.join();
        Expect(result && result->status == PixelMoveLiftStatus::Invalid && !result->prepared,
               "moved-from-input-is-invalid-and-cannot-build-opening", failures);
    }

    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        Expect(Initialize(session), "assetless.fixture", failures);
        EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers.front().asset.reset(); });
        Expect(!session.PrepareDeferredPixelMoveOpeningInput(), "assetless-active-layer-refuses-owner-capture",
               failures);
    }
}

void AbortRestoresOriginalPendingGraph(int& failures)
{
    DocumentSession session;
    Expect(Initialize(session), "abort.fixture", failures);
    auto prepared = PrepareOnWorker(session);
    if (!prepared)
    {
        Expect(false, "abort.worker-preparation-ready", failures);
        return;
    }
    const CanvasDocument documentBefore = session.Document().value();
    const auto revisionBefore = session.history.CurrentRevision();
    const int pastBefore = session.history.UndoCount();
    const bool canRedoBefore = session.history.CanRedo();
    const std::wstring redoBefore = session.history.RedoName();
    const bool applied = session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(prepared.value());
    const bool moved = applied && session.MovePreparedDeferredPixelMoveIfCurrent(prepared.value(), 60, 5);
    const auto opening = session.PendingPixelMove() ? session.PendingPixelMove()->opening : 0;
    const bool aborted = moved && session.AbortPreparedDeferredPixelMoveIfCurrent(prepared.value());
    const PixelMove* restored = session.PendingPixelMove();
    Expect(aborted && !restored && opening != 0 && session.Document().value() == documentBefore &&
               PixelIs(session.ActiveLayer()->asset->image, 10, 10, kRed) &&
               session.history.CurrentRevision() == revisionBefore && session.history.UndoCount() == pastBefore &&
               session.history.CanRedo() == canRedoBefore && session.history.RedoName() == redoBefore,
           "abort-clears-only-owned-pending-with-source-pixels-history-and-redo-unchanged", failures);
    Expect(!session.AbortPreparedDeferredPixelMoveIfCurrent(prepared.value()),
           "abort-of-same-preparation-is-not-repeatable", failures);
    session.CancelPixelMove();
    const bool beganNew = session.BeginDeferredPixelMove();
    const auto newOpening = session.PendingPixelMove() ? session.PendingPixelMove()->opening : 0;
    Expect(beganNew && newOpening != 0 && newOpening != opening &&
               !session.AbortPreparedDeferredPixelMoveIfCurrent(prepared.value()) && session.PendingPixelMove() &&
               session.PendingPixelMove()->opening == newOpening,
           "old-abort-cannot-affect-a-newer-deferred-opening", failures);
}

void AuthorityAndMaskMetadata(int& failures)
{
    {
        DocumentSession session;
        Expect(Initialize(session), "mask.fixture", failures);
        session.AddLayerMask(true);
        Expect(session.IsMaskSelected() && !session.PrepareDeferredPixelMoveOpeningInput(),
               "mask-selected-capture-refuses-through-actual-public-target-api", failures);
        const auto id = session.ActiveLayerID();
        if (!id)
            return;
        session.SelectLayerTarget(id.value(), false);
        const std::wstring largeName(1024 * 1024, L'M');
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document)
                                {
                                    auto& mask = document.layers.front().mask.value();
                                    mask.asset.name = largeName;
                                    mask.enabled = false;
                                    mask.linked = false;
                                });
        const auto root = session.Document()->layers.Identity();
        const auto generation = session.Generation();
        auto prepared = PrepareOnWorker(session);
        const auto* target = prepared && prepared->Token().target ? &prepared->Token().target.value() : nullptr;
        Expect(target && target->mask && target->mask->asset.name == largeName && !target->mask->enabled &&
                   !target->mask->linked && target->image == session.ActiveLayer()->asset->image,
               "native-worker-retains-entire-large-mask-record-and-source-image", failures);
        Expect(prepared && session.Document()->layers.Identity() == root && session.Generation() == generation &&
                   session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(prepared.value()),
               "image-opening-with-disabled-mask-adopts-without-changing-source-root", failures);
    }
    {
        DocumentSession session;
        Expect(Initialize(session), "foreign-transform.fixture", failures);
        auto prepared = PrepareOnWorker(session);
        session.BeginTransform(true);
        Expect(prepared && session.PendingTransformEdit() &&
                   !session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(prepared.value()) &&
                   session.PendingTransformEdit() && !session.PendingPixelMove(),
               "foreign-transform-refuses-without-settling-or-replacing-it", failures);
    }
    {
        DocumentSession session;
        Expect(Initialize(session), "history.fixture", failures);
        auto prepared = PrepareOnWorker(session);
        session.history.Begin(L"Foreign", session.Document(), session.ActiveLayerID());
        Expect(!session.PrepareDeferredPixelMoveOpeningInput() && prepared &&
                   !session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(prepared.value()) &&
                   session.history.CaptureState().Depth() == 1 && !session.PendingPixelMove(),
               "foreign-history-transaction-refuses-capture-and-previous-preparation", failures);
        session.history.End(session.Document(), session.ActiveLayerID());
    }
    {
        DocumentSession session;
        Expect(Initialize(session), "hidden.fixture", failures);
        EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers.front().visible = false; });
        auto captured = session.PrepareDeferredPixelMoveOpeningInput();
        std::optional<PreparedPixelMoveOpeningResult> result;
        if (captured)
        {
            std::thread worker([input = std::move(captured.value()), &result]() mutable
                               { result.emplace(DocumentSession::PrepareDeferredPixelMoveOpening(std::move(input))); });
            worker.join();
        }
        Expect(captured && result && result->status == PixelMoveLiftStatus::Invalid && !result->prepared &&
                   !session.PendingPixelMove(),
               "visibility-walk-refuses-on-worker-without-owner-ancestor-traversal", failures);
    }
    {
        DocumentSession session;
        Expect(Initialize(session), "unbound-memory.fixture", failures);
        auto captured = session.PrepareDeferredPixelMoveOpeningInput();
        std::optional<PreparedPixelMoveOpeningResult> result;
        if (captured)
        {
            std::thread worker(
                [input = std::move(captured.value()), &result]() mutable
                {
                    auto memory = SharedPublicationMemory::CreateForWork();
                    result.emplace(DocumentSession::PrepareDeferredPixelMoveOpening(std::move(input), {}, memory));
                });
            worker.join();
        }
        Expect(result && result->status == PixelMoveLiftStatus::Ready && result->prepared &&
                   !session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(result->prepared.value()) &&
                   !session.PendingPixelMove(),
               "unbound-output-memory-cannot-publish-a-pending-opening", failures);
    }
}
} // namespace

void Run(int& failures)
{
    CaptureIsOwnerOnlyAndNonMutating(failures);
    ApplyKeepsHistoryThenRasterEditHasLiteralPixels(failures);
    DuplicateAndRefusals(failures);
    AbortRestoresOriginalPendingGraph(failures);
    AuthorityAndMaskMetadata(failures);
}
} // namespace DeferredPixelMoveOpeningTests
