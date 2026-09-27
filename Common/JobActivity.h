#pragma once

#include "JobUiPolicy.h"
#include "Jobs.h"

#include <array>
#include <optional>

// UI-owned projection of scheduler notices; no locks, clocks, allocations, or polling. Display the oldest
// foreground operation through its apply/drop, including queue time. Preview/refinement/background jobs have
// their own panel feedback and never make the global indicator appear. This is NOT the document history barrier.
class JobActivity final
{
  public:
    struct Active final
    {
        JobTicket ticket;
        std::uint64_t startQpc = 0;
        std::optional<unsigned int> percent;
    };

    void Observe(JobNotice notice, std::uint64_t nowQpc) noexcept
    {
        if (notice.ticket.slot >= _records.size() || notice.ticket.generation == 0)
        {
            return;
        }
        auto& record = _records[notice.ticket.slot];
        switch (notice.event)
        {
        case JobNoticeKind::Submitted:
            if (notice.kind == JobClass::Commit || notice.kind == JobClass::Selection ||
                notice.kind == JobClass::Stroke || notice.kind == JobClass::Io)
            {
                record = Active{notice.ticket, nowQpc, std::nullopt};
            }
            break;
        case JobNoticeKind::Progress:
            if (record && record->ticket == notice.ticket && notice.percent <= 100 &&
                (!record->percent || notice.percent > *record->percent))
            {
                record->percent = notice.percent;
            }
            break;
        case JobNoticeKind::Retired:
            if (record && record->ticket == notice.ticket)
            {
                record.reset();
            }
            break;
        }
        _selected.reset();
        for (const auto& candidate : _records)
        {
            if (candidate && (!_selected || candidate->ticket.generation < _selected->ticket.generation))
            {
                _selected = candidate;
            }
        }
    }

    void Clear() noexcept
    {
        _records = {};
        _selected.reset();
    }

    [[nodiscard]] const std::optional<Active>& Selected() const noexcept
    {
        return _selected;
    }

    [[nodiscard]] bool Visible(std::uint64_t nowQpc, std::uint64_t frequency) const noexcept
    {
        return _selected && BusyIndicatorVisible(_selected->startQpc, nowQpc, frequency);
    }

  private:
    std::array<std::optional<Active>, JobScheduler::kCapacity> _records;
    std::optional<Active> _selected;
};
