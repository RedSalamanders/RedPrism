#pragma once

#include "Document.h"
#include "HistoryRestoreWork.h"
#include "HostLassoInput.h"
#include "Jobs.h"
#include "LassoFinishGeometry.h"
#include "SharedPublicationMemory.h"

#include <cstdint>
#include <memory>
#include <optional>

class PolygonalDrawingJob;
// Cached scalar admission only. New with a nonempty prior needs the separate exact containment
// dispatcher and is deliberately not described by this drawing-only component.
class PolygonalDrawingPlan final
{
  public:
    [[nodiscard]] std::uint64_t TotalBytes() const noexcept
    {
        return _total;
    }
    [[nodiscard]] std::uint64_t OpeningBytes() const noexcept
    {
        return _opening;
    }
    [[nodiscard]] std::uint64_t FinalBytes() const noexcept
    {
        return _final;
    }
    [[nodiscard]] SelectionMode Mode() const noexcept
    {
        return _drawing.mode;
    }

  private:
    friend std::optional<PolygonalDrawingPlan> DescribePolygonalDrawing(const DocumentSession&, SelectionMode) noexcept;
    friend class PolygonalDrawingJob;
    PolygonalDrawingPlan() = default;
    DocumentReadAuthority _document;
    HistoryRestoreAuthority _editor;
    DocumentHistory::Authority _history;
    LassoFinishGeometry::DrawingPlan _drawing{};
    std::uint64_t _total = 0;
    std::uint64_t _opening = 0;
    std::uint64_t _final = 0;
};
[[nodiscard]] std::optional<PolygonalDrawingPlan> DescribePolygonalDrawing(const DocumentSession& session,
                                                                           SelectionMode mode) noexcept;

struct PolygonalDrawingState final
{
    enum class Event
    {
        Opened,
        Changed,
        Terminal
    };
    enum class WorkStage
    {
        Allocated,
        OpeningPrepared,
        FinishPrepared,
        ResidencyRefreshed
    };
    JobScheduler& scheduler;
    DocumentSession& session;
    HostLassoInput& input;
    const std::uint64_t opening;
    const PolygonalDrawingPlan plan;
    const bool antialiasedAtDown;
    PolygonalDrawingState(JobScheduler& jobs, DocumentSession& document, HostLassoInput& prefix, std::uint64_t identity,
                          PolygonalDrawingPlan admission, bool capturedAntialias) noexcept
        : scheduler(jobs), session(document), input(prefix), opening(identity), plan(admission),
          antialiasedAtDown(capturedAntialias)
    {
    }
    PolygonalDrawingJob* controller = nullptr;
    JobTicket original;
    JobResult outcome;
    bool opened = false;
    bool waiting = false;
    bool completed = false;
    bool published = false;
    bool cancelled = false;
    bool endedByBackspace = false;
    bool inputAcknowledged = false;
    bool sameTicket = true;
    unsigned runs = 0;
    unsigned captures = 0;
    unsigned points = 0;
    unsigned cursors = 0;
    unsigned backspaces = 0;
    unsigned maximumSamples = 0;
    unsigned residencyRefreshes = 0;
    std::uint64_t closingOrdinal = 0;
    // Opaque host closure identity, preserved for the physical dispatcher; not a model capability.
    std::uint64_t closingIntent = 0;
    std::uint64_t firstBudget = 0;
    std::uint64_t openingAllocation = 0;
    std::uint64_t finishBudget = 0;
    // Worker probes borrow only immutable data and readiness gates, never mutable session/UI state.
    void* workObserver = nullptr;
    void (*checkpoint)(void*, WorkStage) noexcept = nullptr;
    void* observer = nullptr;
    void (*notify)(void*, Event) noexcept = nullptr;
    void Notify(Event event) const noexcept
    {
        if (notify)
            notify(observer, event);
    }
};
struct PolygonalDrawingPayload;
class PolygonalDrawingJob final : public Job
{
  public:
    // State, session and input remain alive through joined scheduler shutdown. Submission failure
    // clears the controller and fails the entire operation before any geometry/history capture.
    [[nodiscard]] static std::int32_t Submit(PolygonalDrawingState& state) noexcept;
    explicit PolygonalDrawingJob(PolygonalDrawingState& state) noexcept;
    JobResult Run(JobContext& context) override;
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override;
    void Completed(JobResult result) noexcept override;
    void RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept override;
    [[nodiscard]] bool NotifyInput() noexcept;
    [[nodiscard]] bool Finish(bool antialiased, std::uint64_t ordinal, bool cancelled = false) noexcept;

  private:
    enum class Phase
    {
        Allocate,
        Open,
        Collect,
        Finish,
        Refresh
    };
    [[nodiscard]] bool Current() const noexcept;
    [[nodiscard]] bool Resume() noexcept;
    [[nodiscard]] bool Refuse(std::int32_t error) noexcept;
    [[nodiscard]] bool Consume() noexcept;
    void Abort() noexcept;
    void BindRetirement(JobTicket ticket) noexcept;
    PolygonalDrawingState& _state;
    Phase _phase = Phase::Allocate;
    std::shared_ptr<SharedPublicationMemory> _opening, _final, _temporary;
    std::shared_ptr<PolygonalDrawingPayload> _payload;
    DocumentHistory::ResidencyPermit _residency;
    std::int32_t _error = 0;
    bool _terminal = false;
    std::uint64_t _explicitTerminalOrdinal = 0;
    std::uint64_t _lastOrdinal = 0;
};
