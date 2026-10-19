#include "Document.Tests.PixelMoveClosure.h"

#include "../Support/DocumentFixture.h"
#include "BrushStroke.h"
#include "Document.h"
#include "PixelMoveClosureWork.h"
#include "PixelMoveOpeningWork.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace PixelMoveClosureTests
{
namespace
{
void Expect(bool condition, const char* fact, int& failures)
{
    std::printf("%s pixel-move-closure.%s\n", condition ? "PASS" : "FAIL", fact);
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

    // Keep both history directions populated while restoring the legacy selection bounds.
    session.SetSelection(Rect(30, 10, 10, 10), L"Fixture Selection");
    session.Undo();
    return session.Document() && session.Document()->selection.value() == Rect(10, 10, 10, 10) &&
           session.history.CanUndo() && session.history.CanRedo();
}

std::optional<PreparedPixelMoveOpening> PrepareOpeningOnWorker(DocumentSession& session, bool duplicate = false)
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

bool OpenAndMove(DocumentSession& session, PreparedPixelMoveOpening& opening, double x = 60, double y = 5)
{
    return session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(opening) &&
           session.MovePreparedDeferredPixelMoveIfCurrent(opening, x, y);
}

std::optional<PreparedPixelMoveClosure> PrepareClosureOnWorker(DocumentSession& session,
                                                               const PreparedPixelMoveOpening& opening,
                                                               CancelToken cancellation = {})
{
    auto input = session.PrepareDeferredPixelMoveClosureInput(opening);
    if (!input)
        return std::nullopt;
    std::optional<PreparedPixelMoveClosure> workerResult;
    std::thread worker(
        [captured = std::move(input.value()), &workerResult, cancellation]() mutable
        {
            auto result = DocumentSession::PrepareDeferredPixelMoveClosure(std::move(captured), cancellation);
            if (result)
                workerResult.emplace(std::move(result.value()));
        });
    worker.join();
    return workerResult;
}

DocumentHistory::ResidencyValidation AcquireResidency(PreparedPixelMoveClosure& prepared,
                                                      DocumentHistory::ResidencyPermit& permit, int& failures)
{
    auto validation = prepared.TryAcquireHistoryResidency(permit);
    if (validation == DocumentHistory::ResidencyValidation::Refresh)
    {
        permit.Reset();
        bool refreshed = false;
        std::thread worker([&prepared, &refreshed] { refreshed = prepared.RefreshHistoryResidency(); });
        worker.join();
        Expect(refreshed, "apply.history-residency-refresh-runs-on-worker", failures);
        validation = prepared.TryAcquireHistoryResidency(permit); // One bounded reacquisition after refresh.
    }
    return validation;
}

bool ApplyClosure(DocumentSession& session, PreparedPixelMoveClosure& prepared, int& failures)
{
    DocumentHistory::ResidencyPermit permit;
    return AcquireResidency(prepared, permit, failures) == DocumentHistory::ResidencyValidation::Ready &&
           session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(prepared);
}

void OwnerPreparationRefusalAndWorkerImmutability(int& failures)
{
    DocumentSession session;
    Expect(Initialize(session), "capture.literal-100x40-red-blue-selection-and-history", failures);
    if (!session.Document() || !session.ActiveLayer() || !session.ActiveLayer()->asset)
        return;
    auto opening = PrepareOpeningOnWorker(session);
    Expect(opening.has_value(), "capture.opening-built-on-native-worker", failures);
    if (!opening || !session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(opening.value()) ||
        !session.MovePreparedDeferredPixelMoveIfCurrent(opening.value(), 60, 5))
    {
        Expect(false, "capture.pending-move-established", failures);
        return;
    }
    auto input = session.PrepareDeferredPixelMoveClosureInput(opening.value());
    Expect(input.has_value(), "capture.owner-prepares-closure-input", failures);
    if (!input)
        return;

    const CanvasDocument documentBefore = session.Document().value();
    const Raster sourceBefore = session.ActiveLayer()->asset->image;
    const auto revisionBefore = session.history.CurrentRevision();
    const auto generationBefore = session.Generation();
    const int undoBefore = session.history.UndoCount();
    const bool canRedoBefore = session.history.CanRedo();
    const std::wstring redoBefore = session.history.RedoName();
    const auto ownerResult = DocumentSession::PrepareDeferredPixelMoveClosure(std::move(input.value()));
    Expect(!ownerResult, "owner-thread-closure-preparation-is-refused", failures);

    auto fresh = session.PrepareDeferredPixelMoveClosureInput(opening.value());
    if (!fresh)
    {
        Expect(false, "capture.fresh-worker-input", failures);
        return;
    }
    std::optional<PreparedPixelMoveClosure> workerResult;
    std::thread worker(
        [captured = std::move(fresh.value()), &workerResult]() mutable
        {
            auto result = DocumentSession::PrepareDeferredPixelMoveClosure(std::move(captured));
            if (result)
                workerResult.emplace(std::move(result.value()));
        });
    worker.join();
    Expect(workerResult.has_value(), "worker-prepares-final-image-selection-and-history", failures);
    Expect(session.Document().value() == documentBefore && session.ActiveLayer()->asset->image == sourceBefore &&
               session.history.CurrentRevision() == revisionBefore && session.history.UndoCount() == undoBefore &&
               session.history.CanRedo() == canRedoBefore && session.history.RedoName() == redoBefore &&
               session.Generation() == generationBefore && session.PendingPixelMove(),
           "worker-preparation-leaves-document-history-redo-generation-and-pending-unchanged", failures);
}

void FinalDocumentPublicationView(int& failures)
{
    DocumentSession session;
    Expect(Initialize(session), "final-view.valid-source-and-redo", failures);
    auto opening = PrepareOpeningOnWorker(session);
    auto closure = opening && OpenAndMove(session, *opening) ? PrepareClosureOnWorker(session, *opening) : std::nullopt;
    Expect(closure.has_value(), "final-view.worker-prepares-private-closure", failures);
    if (!closure)
        return;

    const auto source = session.Document().value();
    const auto generation = session.Generation();
    const auto undo = session.history.UndoCount();
    const auto redo = session.history.RedoName();
    const auto* final = closure->FinalDocument();
    Expect(final && final->layers.size() == 1 && final->layers[0].asset && final->selection &&
               final->selection->Bounds() && final->selection->Bounds()->minX == 70 &&
               final->selection->Bounds()->minY == 15 && PixelIs(final->layers[0].asset->image, 10, 10, kTransparent) &&
               PixelIs(final->layers[0].asset->image, 70, 15, kRed),
           "final-view.immutable-borrow-has-independent-final-pixels-and-selection", failures);
    Expect(session.Document().value() == source && session.Generation() == generation &&
               session.history.UndoCount() == undo && session.history.RedoName() == redo &&
               !session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(*closure),
           "final-view.borrow-grants-no-early-history-document-or-apply-authority", failures);
    if (!final)
        return;
    const CanvasDocument held = *final;
    PreparedPixelMoveClosure moved(std::move(*closure));
    Expect(!closure->FinalDocument() && moved.FinalDocument() && *moved.FinalDocument() == held,
           "final-view.moved-from-closure-refuses-and-new-owner-retains-source", failures);
    const bool applied = ApplyClosure(session, moved, failures);
    Expect(applied && !moved.FinalDocument() && session.Document().value() == held &&
               session.Generation() == generation + 1 && session.history.UndoCount() == undo + 1,
           "final-view.apply-hides-retired-predecessor-and-adopts-the-prepared-source-once", failures);
    Expect(applied && !session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(moved) && !moved.FinalDocument(),
           "final-view.repeated-apply-never-exposes-the-retired-document", failures);
    session.ClearDocument();
    Expect(held.layers[0].asset && PixelIs(held.layers[0].asset->image, 70, 15, kRed) &&
               PixelIs(held.layers[0].asset->image, 10, 10, kTransparent),
           "final-view.immutable-consumer-survives-session-retirement", failures);
}

void ApplyMoveUndoRedoAndDuplicate(int& failures)
{
    {
        DocumentSession session;
        Expect(Initialize(session), "move.fixture", failures);
        auto opening = PrepareOpeningOnWorker(session);
        if (!opening || !OpenAndMove(session, opening.value()))
        {
            Expect(false, "move.opening-and-offset-ready", failures);
            return;
        }
        const auto revisionBefore = session.history.CurrentRevision();
        const int undoBefore = session.history.UndoCount();
        const auto generationBefore = session.Generation();
        auto closure = PrepareClosureOnWorker(session, opening.value());
        Expect(closure && closure->ChangesDocument(), "move.worker-closure-contains-one-document-edit", failures);
        if (!closure)
            return;
        Expect(!session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(closure.value()) && session.PendingPixelMove() &&
                   session.history.CurrentRevision() == revisionBefore,
               "move.apply-before-first-history-residency-attempt-is-refused", failures);
        const bool applied = ApplyClosure(session, closure.value(), failures);
        Expect(applied && !session.PendingPixelMove() && session.history.UndoCount() == undoBefore + 1 &&
                   session.history.UndoName() == L"Move Pixels" && !session.history.CanRedo() &&
                   session.history.CurrentRevision() != revisionBefore && session.Generation() == generationBefore + 1,
               "move.apply-publishes-one-named-undo-and-clears-redo", failures);
        if (!applied || !session.ActiveLayer() || !session.ActiveLayer()->asset || !session.Document()->selection)
            return;
        Expect(session.Document()->selection->Bounds() && session.Document()->selection->Bounds()->minX == 70 &&
                   session.Document()->selection->Bounds()->minY == 15 &&
                   session.Document()->selection->Bounds()->maxX == 80 &&
                   session.Document()->selection->Bounds()->maxY == 25 &&
                   PixelIs(session.ActiveLayer()->asset->image, 10, 10, kTransparent) &&
                   PixelIs(session.ActiveLayer()->asset->image, 19, 19, kTransparent) &&
                   PixelIs(session.ActiveLayer()->asset->image, 70, 15, kRed) &&
                   PixelIs(session.ActiveLayer()->asset->image, 79, 24, kRed) &&
                   PixelIs(session.ActiveLayer()->asset->image, 60, 10, kBlue) &&
                   PixelIs(session.ActiveLayer()->asset->image, 60, 15, kBlue),
               "move.literal-hole-red-landing-blue-outside-and-selection-70-15-80-25", failures);
        session.Undo();
        Expect(session.Document()->selection && session.Document()->selection->Bounds() &&
                   session.Document()->selection->Bounds()->minX == 10 &&
                   session.Document()->selection->Bounds()->minY == 10 &&
                   session.Document()->selection->Bounds()->maxX == 20 &&
                   session.Document()->selection->Bounds()->maxY == 20 &&
                   PixelIs(session.ActiveLayer()->asset->image, 10, 10, kRed) &&
                   PixelIs(session.ActiveLayer()->asset->image, 60, 10, kBlue) &&
                   session.history.RedoName() == L"Move Pixels",
               "move.undo-restores-exact-source-selection-and-named-redo", failures);
        session.Redo();
        Expect(session.Document()->selection && session.Document()->selection->Bounds() &&
                   session.Document()->selection->Bounds()->minX == 70 &&
                   session.Document()->selection->Bounds()->minY == 15 &&
                   session.Document()->selection->Bounds()->maxX == 80 &&
                   session.Document()->selection->Bounds()->maxY == 25 &&
                   PixelIs(session.ActiveLayer()->asset->image, 10, 10, kTransparent) &&
                   PixelIs(session.ActiveLayer()->asset->image, 70, 15, kRed) &&
                   PixelIs(session.ActiveLayer()->asset->image, 60, 10, kBlue) &&
                   session.history.UndoName() == L"Move Pixels",
               "move.redo-restores-exact-hole-landing-selection-and-undo-name", failures);
    }

    {
        DocumentSession session;
        Expect(Initialize(session), "duplicate.fixture", failures);
        auto opening = PrepareOpeningOnWorker(session, true);
        if (!opening || !OpenAndMove(session, opening.value()))
        {
            Expect(false, "duplicate.opening-and-offset-ready", failures);
            return;
        }
        const int undoBefore = session.history.UndoCount();
        auto closure = PrepareClosureOnWorker(session, opening.value());
        const bool applied = closure && closure->ChangesDocument() && ApplyClosure(session, closure.value(), failures);
        Expect(applied && session.history.UndoCount() == undoBefore + 1 &&
                   session.history.UndoName() == L"Duplicate Pixels" &&
                   PixelIs(session.ActiveLayer()->asset->image, 10, 10, kRed) &&
                   PixelIs(session.ActiveLayer()->asset->image, 15, 15, kRed) &&
                   PixelIs(session.ActiveLayer()->asset->image, 70, 15, kRed) &&
                   PixelIs(session.ActiveLayer()->asset->image, 79, 24, kRed) &&
                   PixelIs(session.ActiveLayer()->asset->image, 60, 10, kBlue),
               "duplicate-one-named-edit-preserves-source-and-lands-exact-red-copy", failures);
    }
}

void ZeroCancellationAndStaleness(int& failures)
{
    {
        DocumentSession session;
        Expect(Initialize(session), "zero.fixture", failures);
        auto opening = PrepareOpeningOnWorker(session);
        if (!opening || !session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(opening.value()) ||
            !session.MovePreparedDeferredPixelMoveIfCurrent(opening.value(), 60, 5) ||
            !session.MovePreparedDeferredPixelMoveIfCurrent(opening.value(), 0, 0))
        {
            Expect(false, "zero.returned-to-origin", failures);
            return;
        }
        const auto documentBefore = session.Document().value();
        const auto revisionBefore = session.history.CurrentRevision();
        const int undoBefore = session.history.UndoCount();
        const bool canRedoBefore = session.history.CanRedo();
        const auto redoNameBefore = session.history.RedoName();
        const auto generationBefore = session.Generation();
        auto closure = PrepareClosureOnWorker(session, opening.value());
        Expect(closure && !closure->ChangesDocument(), "zero.worker-builds-no-history-publication", failures);
        Expect(closure && !closure->FinalDocument(), "final-view.zero-offset-has-no-final-upload-or-row-source",
               failures);
        const bool applied = closure && session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(closure.value());
        Expect(applied && !session.PendingPixelMove() && session.Document().value() == documentBefore &&
                   session.history.CurrentRevision() == revisionBefore && session.history.UndoCount() == undoBefore &&
                   session.history.CanRedo() == canRedoBefore && session.history.RedoName() == redoNameBefore &&
                   session.Generation() == generationBefore &&
                   PixelIs(session.ActiveLayer()->asset->image, 10, 10, kRed),
               "zero-close-drops-gesture-without-history-revision-redo-generation-or-pixel-change", failures);
    }

    {
        DocumentSession session;
        Expect(Initialize(session), "permit-reset.fixture", failures);
        auto opening = PrepareOpeningOnWorker(session);
        auto closure = opening && OpenAndMove(session, opening.value())
                           ? PrepareClosureOnWorker(session, opening.value())
                           : std::nullopt;
        if (!closure)
        {
            Expect(false, "permit-reset.worker-closure-ready", failures);
            return;
        }
        DocumentHistory::ResidencyPermit permit;
        const auto validation = AcquireResidency(closure.value(), permit, failures);
        const bool acquired = validation == DocumentHistory::ResidencyValidation::Ready;
        permit.Reset();
        Expect(acquired && !session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(closure.value()) &&
                   session.PendingPixelMove(),
               "sampled-history-residency-requires-the-original-live-permit-through-apply", failures);
    }

    {
        DocumentSession session;
        Expect(Initialize(session), "unbound-memory.fixture", failures);
        auto opening = PrepareOpeningOnWorker(session);
        auto input = opening && OpenAndMove(session, opening.value())
                         ? session.PrepareDeferredPixelMoveClosureInput(opening.value())
                         : std::nullopt;
        std::optional<PreparedPixelMoveClosure> closure;
        if (input)
        {
            std::thread worker(
                [captured = std::move(input.value()), &closure]() mutable
                {
                    auto memory = SharedPublicationMemory::CreateForWork();
                    auto result =
                        DocumentSession::PrepareDeferredPixelMoveClosure(std::move(captured), {}, std::move(memory));
                    if (result)
                        closure.emplace(std::move(result.value()));
                });
            worker.join();
        }
        if (!closure)
        {
            Expect(false, "unbound-memory.worker-closure-ready", failures);
            return;
        }
        auto& prepared = closure.value();
        DocumentHistory::ResidencyPermit permit;
        const auto validation = AcquireResidency(prepared, permit, failures);
        Expect(validation == DocumentHistory::ResidencyValidation::Ready &&
                   !session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(prepared) && session.PendingPixelMove(),
               "unbound-output-memory-refuses-apply-even-after-residency-ready", failures);
    }

    {
        DocumentSession session;
        Expect(Initialize(session), "cancel.fixture", failures);
        auto opening = PrepareOpeningOnWorker(session);
        auto input = opening && OpenAndMove(session, opening.value())
                         ? session.PrepareDeferredPixelMoveClosureInput(opening.value())
                         : std::nullopt;
        CancelSource cancellation;
        const bool cancelled = cancellation.Cancel();
        std::optional<PreparedPixelMoveClosure> result;
        if (input)
        {
            std::thread worker(
                [captured = std::move(input.value()), &result, token = cancellation.Token()]() mutable
                {
                    auto prepared = DocumentSession::PrepareDeferredPixelMoveClosure(std::move(captured), token);
                    if (prepared)
                        result.emplace(std::move(prepared.value()));
                });
            worker.join();
        }
        Expect(input && cancelled && !result && session.PendingPixelMove() &&
                   PixelIs(session.ActiveLayer()->asset->image, 10, 10, kRed),
               "precancelled-closure-is-refused-and-keeps-original-pending-document", failures);
    }

    {
        DocumentSession session;
        Expect(Initialize(session), "stale-offset.fixture", failures);
        auto opening = PrepareOpeningOnWorker(session);
        if (!opening || !OpenAndMove(session, opening.value()))
        {
            Expect(false, "stale-offset.initial-move", failures);
            return;
        }
        auto closure = PrepareClosureOnWorker(session, opening.value());
        DocumentHistory::ResidencyPermit permit;
        const auto residency = closure ? AcquireResidency(closure.value(), permit, failures)
                                       : DocumentHistory::ResidencyValidation::Invalid;
        const auto beforePixels = session.ActiveLayer()->asset->image;
        const bool movedAgain = session.MovePreparedDeferredPixelMoveIfCurrent(opening.value(), 61, 5);
        const auto revisionBefore = session.history.CurrentRevision();
        const auto generationBefore = session.Generation();
        Expect(closure && residency == DocumentHistory::ResidencyValidation::Ready && movedAgain &&
                   !session.IsPreparedDeferredPixelMoveClosureCurrent(closure.value()) &&
                   !session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(closure.value()) &&
                   session.PendingPixelMove() && session.PendingPixelMove()->offset == Vec2{61, 5} &&
                   session.ActiveLayer()->asset->image == beforePixels &&
                   session.history.CurrentRevision() == revisionBefore && session.Generation() == generationBefore,
               "further-move-sequence-and-offset-rejects-old-closure-without-publication", failures);
    }
}

void ForeignOwnerAndMetadata(int& failures)
{
    {
        DocumentSession source;
        DocumentSession foreign;
        Expect(Initialize(source) && Initialize(foreign), "foreign.fixture", failures);
        auto opening = PrepareOpeningOnWorker(source);
        auto closure = opening && OpenAndMove(source, opening.value()) ? PrepareClosureOnWorker(source, opening.value())
                                                                       : std::nullopt;
        DocumentHistory::ResidencyPermit permit;
        const auto residency = closure ? AcquireResidency(closure.value(), permit, failures)
                                       : DocumentHistory::ResidencyValidation::Invalid;
        const bool sourceCurrent = closure && source.IsPreparedDeferredPixelMoveClosureCurrent(closure.value());
        const bool foreignCurrent = closure && foreign.IsPreparedDeferredPixelMoveClosureCurrent(closure.value());
        const bool refused =
            residency == DocumentHistory::ResidencyValidation::Ready && sourceCurrent && !foreignCurrent &&
            !foreign.ApplyPreparedDeferredPixelMoveClosureIfCurrent(closure.value()) && source.PendingPixelMove() &&
            source.ApplyPreparedDeferredPixelMoveClosureIfCurrent(closure.value());
        Expect(refused && !source.PendingPixelMove() && !foreign.PendingPixelMove() &&
                   source.history.UndoName() == L"Move Pixels",
               "foreign-editor-refuses-prepared-closure-and-original-owner-can-apply-it", failures);
    }

    {
        DocumentSession session;
        Expect(Initialize(session), "new-document.fixture", failures);
        auto opening = PrepareOpeningOnWorker(session);
        auto closure = opening && OpenAndMove(session, opening.value())
                           ? PrepareClosureOnWorker(session, opening.value())
                           : std::nullopt;
        DocumentHistory::ResidencyPermit permit;
        const auto residency = closure ? AcquireResidency(closure.value(), permit, failures)
                                       : DocumentHistory::ResidencyValidation::Invalid;
        const auto oldSerial = session.DocumentSerial();
        auto replacement = session.Document().value();
        const auto active = session.ActiveLayerID();
        permit.Reset(); // Foreign lifecycle work never runs while an old resident witness is held.
        session.ReplaceDocument(std::move(replacement), active);
        Expect(closure && residency == DocumentHistory::ResidencyValidation::Ready &&
                   session.DocumentSerial() != oldSerial &&
                   !session.IsPreparedDeferredPixelMoveClosureCurrent(closure.value()) &&
                   !session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(closure.value()) &&
                   !session.PendingPixelMove() && session.Document() && session.Document()->layers.size() == 1 &&
                   session.history.UndoCount() == 0,
               "new-document-authority-rejects-old-closure", failures);
    }

    {
        DocumentSession session;
        Expect(Initialize(session), "foreign-history.fixture", failures);
        auto opening = PrepareOpeningOnWorker(session);
        auto closure = opening && OpenAndMove(session, opening.value())
                           ? PrepareClosureOnWorker(session, opening.value())
                           : std::nullopt;
        DocumentHistory::ResidencyPermit permit;
        const auto residency = closure ? AcquireResidency(closure.value(), permit, failures)
                                       : DocumentHistory::ResidencyValidation::Invalid;
        permit.Reset();
        session.history.Begin(L"Foreign", session.Document(), session.ActiveLayerID());
        const auto foreignResidency = closure ? AcquireResidency(closure.value(), permit, failures)
                                              : DocumentHistory::ResidencyValidation::Invalid;
        const bool refused = closure && residency == DocumentHistory::ResidencyValidation::Ready &&
                             foreignResidency == DocumentHistory::ResidencyValidation::Ready &&
                             !session.IsPreparedDeferredPixelMoveClosureCurrent(closure.value()) &&
                             !session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(closure.value()) &&
                             session.PendingPixelMove() && session.history.CaptureState().Depth() == 1;
        permit.Reset();
        session.history.End(session.Document(), session.ActiveLayerID());
        Expect(refused && session.PendingPixelMove(),
               "foreign-history-transaction-rejects-closure-without-ending-owner-transaction", failures);
        session.CancelPixelMove();
    }

    {
        DocumentSession session;
        Expect(Initialize(session), "metadata.fixture", failures);
        session.AddLayerMask(true);
        const auto active = session.ActiveLayerID();
        if (!active)
        {
            Expect(false, "metadata.mask-created", failures);
            return;
        }
        session.SelectLayerTarget(active.value(), false);
        const std::wstring layerName(4096, L'L');
        const std::wstring maskName(1024 * 1024, L'M');
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document)
                                {
                                    ImageLayer& layer = document.layers.front();
                                    layer.name = layerName;
                                    auto& mask = layer.mask.value();
                                    mask.asset.name = maskName;
                                    mask.enabled = false;
                                    mask.linked = false;
                                });
        auto opening = PrepareOpeningOnWorker(session);
        auto closure = opening && OpenAndMove(session, opening.value())
                           ? PrepareClosureOnWorker(session, opening.value())
                           : std::nullopt;
        const bool applied = closure && ApplyClosure(session, closure.value(), failures);
        const ImageLayer* layer = session.ActiveLayer();
        Expect(applied && layer && layer->name == layerName && layer->mask && layer->mask->asset.name == maskName &&
                   !layer->mask->enabled && !layer->mask->linked &&
                   PixelIs(layer->asset->image, 10, 10, kTransparent) && PixelIs(layer->asset->image, 70, 15, kRed),
               "closure-preserves-large-layer-and-mask-names-and-mask-metadata-exactly", failures);
    }
}
} // namespace

void Run(int& failures)
{
    OwnerPreparationRefusalAndWorkerImmutability(failures);
    FinalDocumentPublicationView(failures);
    ApplyMoveUndoRedoAndDuplicate(failures);
    ZeroCancellationAndStaleness(failures);
    ForeignOwnerAndMetadata(failures);
}
} // namespace PixelMoveClosureTests
