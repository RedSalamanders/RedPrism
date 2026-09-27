#include "HistoryWorkTests.h"

#include "../../Common/BrushStroke.h"
#include "../../Common/ContinuationQueue.h"
#include "../../Common/HistoryWork.h"
#include "../../Common/PreviewMailbox.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s history-work-document.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

using Queue = ContinuationQueue<std::uint8_t>;

void SetFixturePixels(DocumentSession& session)
{
    std::array<std::uint8_t, 4 * 2 * 4> bytes{};
    bytes[0] = 10;
    bytes[1] = 20;
    bytes[2] = 30;
    bytes[3] = 255;
    bytes[4] = 40;
    bytes[5] = 50;
    bytes[6] = 60;
    bytes[7] = 255;
    const Raster fixture = Raster::Make(4, 2, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    session.EditForTest(
        [&](CanvasDocument& document)
        {
            if (!document.layers.empty())
            {
                ImageLayer& layer = document.layers.back();
                layer.asset = RasterAsset{fixture, Raster::MakeThumbnail(fixture), layer.name};
            }
        });
}

std::array<std::uint8_t, 4> ActivePixel(const DocumentSession& session, int x, int y) noexcept
{
    std::array<std::uint8_t, 4> pixel{};
    const ImageLayer* layer = session.ActiveLayer();
    if (layer != nullptr && layer->asset.has_value())
    {
        layer->asset->image.ReadRegion(x, y, 1, 1, pixel.data(), 4, 0);
    }
    return pixel;
}

DocumentSession MakeSession()
{
    DocumentSession session;
    session.CreateDocument(4, 2, true);
    session.AddBlankLayer();
    session.brushSettings = BrushSettings{1, 1, 1, 0, 0, 1};
    session.SetForegroundColor(0xFF000000u);
    SetFixturePixels(session);
    return session;
}

bool PaintDot(DocumentSession& session, double x, double y)
{
    if (!session.BeginBrush(Vec2{x, y}))
    {
        return false;
    }
    session.FinishBrush();
    return true;
}

void TestStrokeSnapshotsAfterPredecessor()
{
    DocumentSession session = MakeSession();
    const std::array<std::uint8_t, 4> original = ActivePixel(session, 0, 0);
    HistoryWork history;
    const std::optional<std::uint64_t> filterOrdinal = history.NextOrdinal();
    const std::optional<std::uint64_t> strokeOrdinal = history.NextOrdinal();
    if (!filterOrdinal.has_value() || !strokeOrdinal.has_value())
    {
        Expect(false, "predecessor-and-stroke-ordinals-issued");
        return;
    }
    const std::optional<HistoryWorkStamp> filter =
        history.Begin(session.DocumentSerial(), filterOrdinal.value(), HistoryWorkKind::Commit);
    StrokeReplay replay(session.DocumentSerial(), strokeOrdinal.value());
    Expect(filter.has_value() && replay.EndInput(), "filter-pending-while-stroke-records");
    Expect(!replay.Begin(history, session.DocumentSerial()).has_value() && history.Busy(),
           "stroke-waits-for-filter-apply");

    // The host applies a completed filter on the UI thread before releasing history ownership.
    session.InvertPixels();
    const std::array<std::uint8_t, 4> filtered = ActivePixel(session, 0, 0);
    Expect(filtered == std::array<std::uint8_t, 4>{245, 235, 225, 255}, "invert-filter-produces-numeric-result");
    if (filter.has_value())
    {
        Expect(history.Finish(filter.value()), "filter-finish-after-document-apply");
    }

    const std::optional<HistoryWorkStamp> stroke = replay.Begin(history, session.DocumentSerial());
    Expect(stroke.has_value(), "stroke-starts-after-filter-apply");
    if (!stroke.has_value())
    {
        return;
    }
    // BeginBrush constructs its source from the current layer. Starting it here captures the filtered pixels.
    const bool painted = PaintDot(session, 0.5, 0.5);
    Expect(painted && ActivePixel(session, 0, 0) == std::array<std::uint8_t, 4>{0, 0, 0, 255} &&
               ActivePixel(session, 1, 0) == std::array<std::uint8_t, 4>{215, 205, 195, 255},
           "stroke-uses-post-filter-source");
    Expect(replay.Finish(history, stroke.value(), painted), "stroke-replay-finish-after-apply");
    session.Undo();
    Expect(ActivePixel(session, 0, 0) == filtered, "undo-stroke-restores-filtered-pixels");
    session.Undo();
    Expect(ActivePixel(session, 0, 0) == original, "undo-filter-restores-original-pixels");
}

void TestFailedPredecessorAndQueueOrder()
{
    for (const bool cancelled : {false, true})
    {
        DocumentSession session = MakeSession();
        const std::array<std::uint8_t, 4> original = ActivePixel(session, 0, 0);
        HistoryWork history;
        const std::optional<std::uint64_t> predecessorOrdinal = history.NextOrdinal();
        const std::optional<std::uint64_t> strokeOrdinal = history.NextOrdinal();
        if (!predecessorOrdinal.has_value() || !strokeOrdinal.has_value())
        {
            Expect(false, "failed-predecessor-ordinals-issued");
            continue;
        }
        const std::optional<HistoryWorkStamp> predecessor =
            history.Begin(session.DocumentSerial(), predecessorOrdinal.value(), HistoryWorkKind::Commit);
        StrokeReplay replay(session.DocumentSerial(), strokeOrdinal.value());
        const bool ended = replay.EndInput();
        const std::optional<HistoryWorkStamp> early =
            replay.Begin(history, session.DocumentSerial(), predecessorOrdinal.value());
        Expect(predecessor.has_value() && ended && !early.has_value() && history.Busy(),
               cancelled ? "cancelled-predecessor-blocks-stroke" : "failed-predecessor-blocks-stroke");

        // Failure and cancellation both drop the un-applied predecessor, preserving the current raster as replay base.
        if (predecessor.has_value())
        {
            if (cancelled)
            {
                const std::optional<HistoryWorkStamp> cancel = history.RequestCancel();
                Expect(cancel.has_value() && history.CancelRequested(predecessor.value()),
                       "cancelled-predecessor-cancel-flagged");
            }
            Expect(history.Finish(predecessor.value()), "dropped-predecessor-releases-history");
        }
        const std::optional<HistoryWorkStamp> stroke = replay.Begin(history, session.DocumentSerial());
        Expect(stroke.has_value(), "stroke-begins-on-unchanged-document-after-drop");
        if (!stroke.has_value())
        {
            continue;
        }
        Expect(ActivePixel(session, 0, 0) == original, "failed-or-cancelled-predecessor-keeps-original-pixels");
        const bool painted = PaintDot(session, 0.5, 0.5);
        Expect(replay.Finish(history, stroke.value(), painted), "stroke-after-dropped-predecessor-finishes");
        session.Undo();
        Expect(painted && ActivePixel(session, 0, 0) == original,
               "undo-stroke-after-dropped-predecessor-restores-unchanged-base");
    }

    DocumentSession session = MakeSession();
    HistoryWork history;
    const std::optional<std::uint64_t> commandOrdinal = history.NextOrdinal();
    const std::optional<std::uint64_t> strokeOrdinal = history.NextOrdinal();
    if (commandOrdinal.has_value() && strokeOrdinal.has_value())
    {
        Queue queue;
        std::uint8_t command = 7;
        const ContinuationAdmission admission =
            queue.Enqueue(session.DocumentSerial(), commandOrdinal.value(), ContinuationKind::Command, command);
        StrokeReplay replay(session.DocumentSerial(), strokeOrdinal.value());
        const bool ended = replay.EndInput();
        const Queue::Entry* const front = queue.Front();
        const std::optional<HistoryWorkStamp> blocked =
            front == nullptr ? std::nullopt : replay.Begin(history, session.DocumentSerial(), front->ordinal);
        const Queue::Taken ready = queue.TakeReady(session.DocumentSerial(), false);
        Expect(admission == ContinuationAdmission::Queued && ended && !blocked.has_value() && ready.ready.has_value(),
               "earlier-queued-command-prevents-stroke");
        if (!ready.ready)
        {
            return;
        }
        const auto commandWork = history.Begin(session.DocumentSerial(), ready.ready->ordinal, HistoryWorkKind::Commit);
        Expect(commandWork && !replay.Begin(history, session.DocumentSerial()),
               "dequeued-command-still-blocks-until-apply");
        if (!commandWork)
        {
            return;
        }
        session.InvertPixels();
        Expect(history.Finish(*commandWork), "queued-command-applies-before-stroke");
        const std::optional<HistoryWorkStamp> next = replay.Begin(history, session.DocumentSerial());
        Expect(next.has_value(), "stroke-begins-after-queued-command-applied");
        if (next.has_value())
        {
            Expect(replay.Finish(history, next.value(), false), "blocked-stroke-can-finish-as-failed");
        }
    }
}

void TestSeparateUndoEntriesAndDocumentReplacement()
{
    DocumentSession session = MakeSession();
    const int initialUndoCount = session.history.UndoCount();
    HistoryWork history;
    const std::optional<std::uint64_t> firstOrdinal = history.NextOrdinal();
    const std::optional<std::uint64_t> secondOrdinal = history.NextOrdinal();
    const std::optional<std::uint64_t> undoOrdinal = history.NextOrdinal();
    if (!firstOrdinal.has_value() || !secondOrdinal.has_value() || !undoOrdinal.has_value())
    {
        Expect(false, "stroke-and-undo-ordinals-issued");
        return;
    }
    const std::optional<HistoryWorkStamp> firstStamp =
        history.Begin(session.DocumentSerial(), firstOrdinal.value(), HistoryWorkKind::Stroke);
    const bool first = PaintDot(session, 2.5, 0.5);
    if (firstStamp.has_value())
    {
        Expect(history.Finish(firstStamp.value()), "first-stroke-history-work-finishes");
    }
    const int afterFirst = session.history.UndoCount();
    const std::optional<HistoryWorkStamp> secondStamp =
        history.Begin(session.DocumentSerial(), secondOrdinal.value(), HistoryWorkKind::Stroke);
    Queue undoQueue;
    std::uint8_t undoRequest = 0;
    const ContinuationAdmission undoAdmission =
        undoQueue.Enqueue(session.DocumentSerial(), undoOrdinal.value(), ContinuationKind::Undo, undoRequest);
    Expect(!undoQueue.TakeReady(session.DocumentSerial(), history.Busy()).ready && undoQueue.Size() == 1,
           "undo-remains-queued-during-pending-stroke");
    const bool second = PaintDot(session, 3.5, 0.5);
    if (secondStamp.has_value())
    {
        Expect(history.Finish(secondStamp.value()), "second-stroke-history-work-finishes");
    }
    Expect(first && second && afterFirst == initialUndoCount + 1 && session.history.UndoCount() == afterFirst + 1,
           "two-strokes-create-separate-undo-entries");
    const Queue::Taken queuedUndo = undoQueue.TakeReady(session.DocumentSerial(), history.Busy());
    if (queuedUndo.ready.has_value())
    {
        session.Undo();
    }
    Expect(ActivePixel(session, 3, 0) == std::array<std::uint8_t, 4>{0, 0, 0, 0} &&
               ActivePixel(session, 2, 0) != std::array<std::uint8_t, 4>{0, 0, 0, 0} &&
               undoAdmission == ContinuationAdmission::Queued && queuedUndo.ready.has_value() &&
               queuedUndo.ready->kind == ContinuationKind::Undo,
           "undo-queued-after-stroke-reverts-only-latest-stroke");
    session.Undo();
    Expect(ActivePixel(session, 2, 0) == std::array<std::uint8_t, 4>{0, 0, 0, 0}, "second-undo-reverts-earlier-stroke");
}

void TestDocumentAndPreviewReplacement()
{
    DocumentSession reopened = MakeSession();
    const std::optional<JobToken> preview = reopened.CaptureJobToken();
    HistoryWork history;
    const std::optional<std::uint64_t> ordinal = history.NextOrdinal();
    if (!preview.has_value() || !ordinal.has_value())
    {
        Expect(false, "preview-stamp-and-ordinal-captured");
        return;
    }
    const std::uint64_t oldSerial = reopened.DocumentSerial();
    PreviewMailbox<int> mailbox;
    Expect(mailbox.Begin(), "preview-edit-begins");
    int settings = 25;
    Expect(mailbox.Offer(settings).accepted, "preview-settings-offered");
    const auto launched = mailbox.TakeNext();
    if (!launched)
    {
        Expect(false, "preview-launched");
        return;
    }
    (void)mailbox.Close();
    Expect(mailbox.Begin() && reopened.CanApplyJobResult(*preview, 0) && !mailbox.CanPublish(launched->stamp),
           "unchanged-document-token-does-not-resurrect-off-reopened-preview");
    const std::optional<HistoryWorkStamp> active =
        history.Begin(oldSerial, ordinal.value(), HistoryWorkKind::Selection);
    const std::optional<std::uint64_t> staleQueueOrdinal = history.NextOrdinal();
    if (!staleQueueOrdinal.has_value())
    {
        Expect(false, "stale-queue-ordinal-issued");
        return;
    }
    Queue staleQueue;
    std::uint8_t staleRequest = 4;
    const ContinuationAdmission staleAdmission =
        staleQueue.Enqueue(oldSerial, staleQueueOrdinal.value(), ContinuationKind::Command, staleRequest);
    reopened.CreateDocument(2, 2, false);
    Expect(!reopened.CanApplyJobResult(preview.value(), 0) && reopened.DocumentSerial() != oldSerial,
           "preview-stamp-rejected-after-document-reopen");
    const Queue::Taken staleTaken = staleQueue.TakeReady(reopened.DocumentSerial(), false);
    Expect(staleAdmission == ContinuationAdmission::Queued && staleTaken.stale == 1 && !staleTaken.ready.has_value(),
           "stale-document-queue-entry-dropped-after-reopen");
    const std::optional<std::uint64_t> nextOrdinal = history.NextOrdinal();
    if (nextOrdinal.has_value())
    {
        StrokeReplay staleReplay(oldSerial, nextOrdinal.value());
        Expect(staleReplay.EndInput() && !staleReplay.Begin(history, reopened.DocumentSerial()).has_value(),
               "stale-document-serial-refuses-stroke-replay");
    }
    if (active.has_value())
    {
        Expect(history.Finish(active.value()), "old-work-owner-can-retire-after-reopen");
    }
}

} // namespace

int RunHistoryWorkDocumentTests()
{
    gFailures = 0;
    TestStrokeSnapshotsAfterPredecessor();
    TestFailedPredecessorAndQueueOrder();
    TestSeparateUndoEntriesAndDocumentReplacement();
    TestDocumentAndPreviewReplacement();
    return gFailures;
}
