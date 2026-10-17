#pragma once

#include "HistoryRestoreWork.h"
#include "HostSelectionPressInput.h"
#include "Jobs.h"

#include <atomic>
#include <cstdint>
#include <optional>

class SelectionOutlineGestureJob;
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
        DrawingPrepared
    };
    // Optional worker readiness probe for event-gated fixtures. Its context may hold immutable rasters
    // and events, never an Application, mutable session, UI or GPU owner.
    void* workObserver = nullptr;
    void (*workCheckpoint)(void*, WorkStage) noexcept = nullptr;
    unsigned residencyTries = 0, residencyRefreshes = 0;
    JobScheduler& scheduler;
    DocumentSession& session;
    HostSelectionPressInput& prefix;
    const std::uint64_t opening;
    const Limits limits;
    SelectionOutlineGestureState(JobScheduler& jobs, DocumentSession& document, HostSelectionPressInput& input,
                                 std::uint64_t identity, Limits admitted) noexcept
        : scheduler(jobs), session(document), prefix(input), opening(identity), limits(admitted)
    {
    }
    void* observer = nullptr;
    void (*notify)(void*, Event) noexcept = nullptr;
    SelectionOutlineGestureJob* controller = nullptr;
    // Optional exact source of a cache-derived admission. Fixture-only callers may supply fixed limits.
    std::optional<Admission> admissionSource;
    // Explicitly selected kind branches; preserving modes also require allowPreservingMarquee.
    bool allowNewRectangle = false, allowNewEllipse = false, drawing = false;
    bool allowPreservingMarquee = false;
    bool requireCapturedTarget = false;
    JobTicket original;
    unsigned runs = 0, captures = 0, hits = 0, collectAcks = 0, closurePrepares = 0, clickPrepares = 0, moves = 0;
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
};
