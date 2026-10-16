#pragma once

#include "HostSelectionPressInput.h"
#include "Jobs.h"

#include <atomic>
#include <cstdint>

class SelectionOutlineGestureJob;
// Stable owner-side borrowing boundary. The application must join the scheduler before these references die.
// The descriptor's complete admission is supplied before any immutable document/geometry capture.
// This engine handles an exact inside-selection New outline move. Drawing, Wand and lifted pixels are separate
// branches; a caller must resolve them under its own original parent before ordinary activation.
struct SelectionOutlineGestureState
{
    struct Limits final
    {
        std::uint64_t totalBytes, continuingOutputBytes, finalOutputBytes;
    };
    enum class Event
    {
        Opened,
        Moved,
        Terminal
    };
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
    JobTicket original;
    unsigned runs = 0, captures = 0, hits = 0, collectAcks = 0, closurePrepares = 0, clickPrepares = 0, moves = 0;
    std::uint64_t firstBudget = 0, remainingBudget = 0, openingBytes = 0;
    bool opened = false, waiting = false, completed = false, closed = false, clicked = false, aborted = false;
    bool sameTicket = true, partition = false, inputAcknowledged = false, sourceCurrent = false, terminalLost = false,
         overflowObserved = false;
    std::uint64_t terminalOrdinal = 0;
    std::atomic<bool> parentAcknowledged{false};
    JobResult outcome{};
    void Notify(Event event) const noexcept
    {
        if (notify)
            notify(observer, event);
    }
};
