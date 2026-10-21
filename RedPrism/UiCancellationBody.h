#pragma once

#include "UiCancellationCapture.h"

#include <cstdint>
#include <limits>
#include <optional>

// Owner-only reduction of immutable observer samples. The caller explicitly chooses the original ticket and
// the useful Run ordinal: color jobs use ordinal 2 after their admitted ordinal-1 capture stub. No mutable
// Application state is read by a callback. Feed in the log's reservation order, stopping at unpublished holes.
class UiCancellationBody final
{
  public:
    enum class State
    {
        Waiting,
        Entered,
        Exited,
        Skipped,
        ClockFailed,
        Malformed
    };
    enum class StopOutcome
    {
        Pending,
        BeforeBody,
        AlreadyFinished,
        FinishOverlap,
        ReturnedAfterAcceptance,
        Skipped,
        ClockFailed,
        Malformed
    };

    enum class CheckOutcome
    {
        Unobserved,
        BeforeStop,
        StopOverlap,
        ObservedAfterAcceptance,
        ClockFailed,
        Malformed
    };

    UiCancellationBody(JobTicket ticket, JobClass kind, JobExecutionLane lane, std::uint64_t ordinal) noexcept
        : _ticket(ticket), _kind(kind), _lane(lane), _selectedOrdinal(ordinal)
    {
        _malformed = ticket.slot >= JobScheduler::kCapacity || ticket.generation == 0 || ordinal == 0 ||
                     kind >= JobClass::Count || lane >= JobExecutionLane::Count;
    }

    void Observe(JobExecutionNotice notice, std::int64_t ticks) noexcept
    {
        // A different generation at the same slot is not the selected original body.
        if (notice.ticket != _ticket || _malformed)
            return;
        if (notice.kind != _kind || notice.lane != _lane)
        {
            _malformed = true;
            return;
        }
        if (ticks < 0 || (_lastTicks >= 0 && ticks < _lastTicks))
            _clockFailed = true;
        _lastTicks = ticks;
        switch (notice.boundary)
        {
        case JobExecutionBoundary::BodyEntered:
            if (_running || notice.result || _ordinal == (std::numeric_limits<std::uint64_t>::max)())
            {
                _malformed = true;
                return;
            }
            ++_ordinal;
            _running = true;
            _checkSeenThisRun = false;
            if (_ordinal == _selectedOrdinal)
            {
                _entered = true;
                _entryTicks = ticks;
            }
            break;
        case JobExecutionBoundary::BodyExited:
            if (!_running || !ValidResult(notice.result))
            {
                _malformed = true;
                return;
            }
            _running = false;
            if (_ordinal == _selectedOrdinal)
            {
                _exited = true;
                _exitTicks = ticks;
                _result = notice.result;
            }
            break;
        case JobExecutionBoundary::CancellationObserved:
            if (!_running || notice.result || _checkSeenThisRun)
            {
                _malformed = true;
                return;
            }
            _checkSeenThisRun = true;
            if (_ordinal == _selectedOrdinal)
                _checkTicks = ticks;
            break;
        case JobExecutionBoundary::BodySkipped:
            if (_running || !notice.result || notice.result.value().status != JobStatus::Cancelled ||
                notice.result.value().error != 0 || _ordinal == (std::numeric_limits<std::uint64_t>::max)())
            {
                _malformed = true;
                return;
            }
            ++_ordinal;
            if (_ordinal == _selectedOrdinal)
                _skipped = true;
            break;
        default:
            _malformed = true;
        }
    }

    [[nodiscard]] State Current() const noexcept
    {
        if (_malformed)
            return State::Malformed;
        if (_clockFailed)
            return State::ClockFailed;
        if (_skipped)
            return State::Skipped;
        if (_exited)
            return State::Exited;
        return _entered ? State::Entered : State::Waiting;
    }

    // Stop begin and acceptance bracket actual owner cancellation dispatch. An exit during that bracket
    // is a finish overlap, not a accepted-stop-to-return sample. Result apply/drop and external acknowledgements
    // remain a separate caller phase. Negative or reversed clocks cannot qualify a measured interval.
    [[nodiscard]] StopOutcome ClassifyStop(std::int64_t begin, std::int64_t accepted) const noexcept
    {
        if (_malformed)
            return StopOutcome::Malformed;
        if (_clockFailed || begin < 0 || accepted < begin)
            return StopOutcome::ClockFailed;
        if (_skipped)
            return StopOutcome::Skipped;
        if (!_entered)
            return StopOutcome::Pending;
        if (begin < _entryTicks)
            return StopOutcome::BeforeBody;
        if (!_exited)
            return StopOutcome::Pending;
        if (_exitTicks < begin)
            return StopOutcome::AlreadyFinished;
        if (_exitTicks <= accepted)
            return StopOutcome::FinishOverlap;
        return StopOutcome::ReturnedAfterAcceptance;
    }

    // The first true check can race request_stop's return. Preserve that overlap separately instead
    // of inventing a negative acceptance-to-check duration or substituting body exit.
    [[nodiscard]] CheckOutcome ClassifyCheck(std::int64_t begin, std::int64_t accepted) const noexcept
    {
        if (_malformed)
            return CheckOutcome::Malformed;
        if (_clockFailed || begin < 0 || accepted < begin)
            return CheckOutcome::ClockFailed;
        if (_checkTicks < 0)
            return CheckOutcome::Unobserved;
        if (_checkTicks < begin)
            return CheckOutcome::BeforeStop;
        return _checkTicks <= accepted ? CheckOutcome::StopOverlap : CheckOutcome::ObservedAfterAcceptance;
    }

    [[nodiscard]] std::int64_t EntryTicks() const noexcept
    {
        return _entryTicks;
    }
    [[nodiscard]] std::int64_t CheckTicks() const noexcept
    {
        return _checkTicks;
    }
    [[nodiscard]] std::int64_t ExitTicks() const noexcept
    {
        return _exitTicks;
    }
    [[nodiscard]] std::optional<JobResult> RawResult() const noexcept
    {
        return _result;
    }

  private:
    [[nodiscard]] static bool ValidResult(const std::optional<JobResult>& result) noexcept
    {
        if (!result)
            return false;
        switch (result.value().status)
        {
        case JobStatus::Finished:
        case JobStatus::Cancelled:
            return result.value().error == 0;
        case JobStatus::Failed:
            return result.value().error < 0;
        default:
            return false;
        }
    }

    const JobTicket _ticket;
    const JobClass _kind;
    const JobExecutionLane _lane;
    const std::uint64_t _selectedOrdinal;
    std::uint64_t _ordinal = 0;
    std::int64_t _lastTicks = -1;
    std::int64_t _entryTicks = -1;
    std::int64_t _exitTicks = -1;
    std::int64_t _checkTicks = -1;
    std::optional<JobResult> _result;
    bool _running = false;
    bool _checkSeenThisRun = false;
    bool _entered = false;
    bool _exited = false;
    bool _skipped = false;
    bool _clockFailed = false;
    bool _malformed = false;
};
