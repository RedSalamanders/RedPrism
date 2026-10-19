#pragma once

#include "HistoryRestoreWork.h"
#include "HostSelectionGestureInput.h"
#include "Jobs.h"
#include "LassoFinishGeometry.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

class SelectionOutlineGestureJob;
// Issued only after a successful original journal parent publishes its selection-only result.
// Scalar authority only: no document/history roots. The journal owner outlives all issued receipts.
// Moving consumes the source; the next original parent consumes its receipt before retaining roots.
class SelectionOutlineGestureTransition final
{
  public:
    SelectionOutlineGestureTransition(const SelectionOutlineGestureTransition&) = delete;
    SelectionOutlineGestureTransition& operator=(const SelectionOutlineGestureTransition&) = delete;
    SelectionOutlineGestureTransition& operator=(SelectionOutlineGestureTransition&&) = delete;
    SelectionOutlineGestureTransition(SelectionOutlineGestureTransition&& source) noexcept
        : _family(std::exchange(source._family, nullptr)), _opening(source._opening), _terminal(source._terminal),
          _minimumGeneration(source._minimumGeneration), _after(source._after), _editor(source._editor),
          _history(source._history)
    {
    }

  private:
    friend class SelectionOutlineGestureJob;
    SelectionOutlineGestureTransition(const HostPolygonalInputJournal* family, std::uint64_t opening,
                                      std::uint64_t terminal, std::uint64_t minimumGeneration,
                                      DocumentReadAuthority after, HistoryRestoreAuthority editor,
                                      DocumentHistory::Authority history) noexcept
        : _family(family), _opening(opening), _terminal(terminal), _minimumGeneration(minimumGeneration), _after(after),
          _editor(editor), _history(history)
    {
    }
    const HostPolygonalInputJournal* _family;
    std::uint64_t _opening, _terminal, _minimumGeneration;
    DocumentReadAuthority _after;
    HistoryRestoreAuthority _editor;
    DocumentHistory::Authority _history;
};

// Stable owner-side borrowing boundary. The application must join the scheduler before these references die.
// The descriptor's complete admission is supplied before any immutable document/geometry capture.
// The original parent handles an inside-selection New outline move and explicitly admitted New marquees.
// Preserving marquees require their explicit additional gate. Wand and lifted pixels remain separate branches.
struct SelectionOutlineGestureState
{
    struct Limits final
    {
        std::uint64_t totalBytes, continuingOutputBytes, finalOutputBytes;
    };
    struct Admission final
    {
        DocumentReadAuthority document;
        HistoryRestoreAuthority editor;
        DocumentHistory::Authority history;
        std::optional<LassoKind> newMarqueeKind;
        SelectionMode marqueeMode = SelectionMode::New;
        bool initialPixels = false;
    };
    enum class Event
    {
        Opened,
        Moved,
        Terminal
    };
    enum class WorkStage
    {
        ClosePrepared,
        ClickPrepared,
        ResidencyRefreshStarted,
        DrawingPrepared,
        PixelOpeningPrepared,
        PixelClosurePrepared
    };
    // Optional worker readiness probe for event-gated fixtures. Its context may hold immutable rasters
    // and events, never an Application, mutable session, UI or GPU owner.
    void* workObserver = nullptr;
    void (*workCheckpoint)(void*, WorkStage) noexcept = nullptr;
    unsigned residencyTries = 0, residencyRefreshes = 0;
    JobScheduler& scheduler;
    DocumentSession& session;
    HostSelectionGestureInput prefix;
    const std::uint64_t opening;
    const Limits limits;
    SelectionOutlineGestureState(JobScheduler& jobs, DocumentSession& document, HostSelectionPressInput& input,
                                 std::uint64_t identity, Limits admitted) noexcept
        : scheduler(jobs), session(document), prefix(input), opening(identity), limits(admitted)
    {
    }
    SelectionOutlineGestureState(JobScheduler& jobs, DocumentSession& document, HostPolygonalInputJournal& input,
                                 std::uint64_t identity, Limits admitted) noexcept
        : scheduler(jobs), session(document), prefix(input, identity), opening(identity), limits(admitted)
    {
    }
    enum class WaitReason : std::uint8_t
    {
        None,
        Input,
        Ordinal,
        Terminal
    };
    WaitReason waitReason = WaitReason::None;
    void* hostObserver = nullptr;
    bool (*mayReplay)(void*, std::uint64_t) noexcept = nullptr;
    bool (*mayFinish)(void*, std::uint64_t, bool cancelled) noexcept = nullptr;
    // Owner-only selected-host admission, distinct from the pure readiness query. It may acquire only
    // the matching host barrier; no session/input mutation, source capture, UI dispatch or reentry.
    bool (*admitTerminal)(void*, std::uint64_t, bool cancelled) noexcept = nullptr;
    // Once per consumed owner batch, including no-change input. Signal readiness only, never dispatch UI.
    void (*inputProgress)(void*, std::uint64_t) noexcept = nullptr;
    std::uint64_t lastConsumedOrdinal = 0;
    void (*pressResolved)(void*, std::uint64_t, bool closes) noexcept = nullptr;
    bool endedByBackspace = false;
    bool cancelled = false;
    void* observer = nullptr;
    void (*notify)(void*, Event) noexcept = nullptr;
    SelectionOutlineGestureJob* controller = nullptr;
    // Optional exact source of a cache-derived admission. Fixture-only callers may supply fixed limits.
    std::optional<Admission> admissionSource;
    // Explicitly selected kind branches; preserving modes also require allowPreservingMarquee.
    std::optional<LassoFinishGeometry::DrawingPlan> drawingPlan;
    bool allowNewRectangle = false, allowNewEllipse = false, drawing = false;
    bool allowPreservingMarquee = false;
    // Selected CPU-only initial Ctrl branch for the fixed press stream. Journal/Wand require their
    // distinct typed pixel lineage and renderer phases; this flag does not authorize those routes.
    bool allowInitialPixelMove = false, pixelMove = false;
    bool requireCapturedTarget = false;
    // Caller moves only the exact preceding completed parent's issued transition. Worker admission
    // validates and consumes it; it does not rewrite the journal's immutable per-Down authority.
    std::optional<SelectionOutlineGestureTransition> predecessor;
    JobTicket original;
    unsigned runs = 0, captures = 0, hits = 0, collectAcks = 0, closurePrepares = 0, clickPrepares = 0, moves = 0;
    unsigned maximumDrawingSamples = 0;
    std::uint64_t firstBudget = 0, remainingBudget = 0, openingBytes = 0;
    bool opened = false, waiting = false, completed = false, closed = false, clicked = false, aborted = false;
    bool sameTicket = true, partition = false, inputAcknowledged = false, sourceCurrent = false, terminalLost = false,
         overflowObserved = false;
    std::uint64_t terminalOrdinal = 0;
    std::atomic<bool> parentAcknowledged{false};
    JobResult outcome{};
    void NotifyWork(WorkStage stage) const noexcept
    {
        if (workCheckpoint)
            workCheckpoint(workObserver, stage);
    }
    void Notify(Event event) const noexcept
    {
        if (notify)
            notify(observer, event);
    }
    [[nodiscard]] std::optional<SelectionOutlineGestureTransition> TakeSuccessor() noexcept
    {
        if (!completed || controller || !parentAcknowledged.load(std::memory_order_acquire))
            return std::nullopt;
        auto result = std::move(_successor);
        _successor.reset();
        return result;
    }

  private:
    friend class SelectionOutlineGestureJob;
    std::optional<SelectionOutlineGestureTransition> _successor;
};
