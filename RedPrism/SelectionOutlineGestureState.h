#pragma once

#include "HistoryRestoreWork.h"
#include "HostSelectionGestureInput.h"
#include "Jobs.h"
#include "LassoFinishGeometry.h"
#include "SelectionPixelFinalWork.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

class SelectionOutlineGestureJob;
// Issued only after a successful original journal parent closes its selection or pixel gesture.
// Scalar authority only: no document/history roots. The journal owner outlives all issued receipts.
// Moving consumes the source; the next original parent consumes its receipt before retaining roots.
class SelectionOutlineGestureTransition final
{
  public:
    enum class ResultKind : std::uint8_t
    {
        SelectionOnly,
        PixelGesture,
    };
    [[nodiscard]] ResultKind Kind() const noexcept
    {
        return _kind;
    }
    SelectionOutlineGestureTransition(const SelectionOutlineGestureTransition&) = delete;
    SelectionOutlineGestureTransition& operator=(const SelectionOutlineGestureTransition&) = delete;
    SelectionOutlineGestureTransition& operator=(SelectionOutlineGestureTransition&&) = delete;
    SelectionOutlineGestureTransition(SelectionOutlineGestureTransition&& source) noexcept
        : _family(std::exchange(source._family, nullptr)), _opening(source._opening), _terminal(source._terminal),
          _minimumGeneration(source._minimumGeneration), _after(source._after), _editor(source._editor),
          _history(source._history), _kind(source._kind)
    {
    }

  private:
    friend class SelectionOutlineGestureJob;
    SelectionOutlineGestureTransition(const HostPolygonalInputJournal* family, std::uint64_t opening,
                                      std::uint64_t terminal, std::uint64_t minimumGeneration,
                                      DocumentReadAuthority after, HistoryRestoreAuthority editor,
                                      DocumentHistory::Authority history, ResultKind kind) noexcept
        : _family(family), _opening(opening), _terminal(terminal), _minimumGeneration(minimumGeneration), _after(after),
          _editor(editor), _history(history), _kind(kind)
    {
    }
    const HostPolygonalInputJournal* _family;
    std::uint64_t _opening, _terminal, _minimumGeneration;
    DocumentReadAuthority _after;
    HistoryRestoreAuthority _editor;
    DocumentHistory::Authority _history;
    const ResultKind _kind;
};

// Stable owner-side borrowing boundary. The application must join the scheduler before these references die.
// The descriptor's complete admission is supplied before any immutable document/geometry capture.
// The original parent handles an inside-selection New outline move and explicitly admitted New marquees.
// Preserving marquees require their explicit additional gate. Wand and lifted pixels remain separate branches.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
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
    // Explicit CPU initial Ctrl branch. A journal additionally carries the exact typed predecessor receipt.
    // This does not authorize Wand, host GPU publication or ordinary activation.
    bool allowInitialPixelMove = false, pixelMove = false;
    // Optional host final-publication stages; null preserves the explicit CPU reference route.
    // Complete final-display ceilings are admitted before Submit; continuing output forbids growth.
    std::uint64_t pixelFinalCpuBytes = 0, pixelFinalGpuBytes = 0;
    // Optional worker-only reached-resource fixture. Configured before the original resumes;
    // borrows only an event gate through workObserver, never an Application/session/device.
    void (*pixelFinalPreparedForTest)(void*, void* shutdownEvent) noexcept = nullptr;
    SelectionPixelFinalFactory pixelFinalFactory = nullptr;
    void* pixelFinalHost = nullptr;
    std::uint32_t pixelFinalOwnerThread = 0; // Factory configuration is immutable from Submit through completion.
    std::optional<JobResult> pixelFinalOutcome;
    bool pixelFinalClosed = false;
    bool pixelFinalWaitingForDisplay = false; // Owner-only phase observation; never publication authority.

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
#pragma warning(pop)
