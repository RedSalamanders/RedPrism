#include "Application.h"

#include <exception>
#include <limits>
#include <memory>
#include <new>

class Application::SelectionCursorJob final : public Job
{
  public:
    SelectionCursorJob(Application& owner, SelectionCursorRequests::Request request,
                       SelectionCursorProbePlan plan) noexcept
        : _owner(owner), _request(request), _plan(plan), _observer(owner._selectionCursorObserver),
          _probe(owner._selectionCursorWorkerProbe)
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_captured)
            return {}; // The original complete admission precedes geometry acquisition on the owner.
        if (!_display)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (_probe.ready)
            _probe.ready(_probe.context, context); // Optional event-gated fixture; borrows no Application.
        _inside = ContainsSelectionForWork(_display.value(), {_request.key.x, _request.key.y}, context.cancellation,
                                           _observer);
        return _inside.has_value() ? JobResult{} : JobResult{JobStatus::Cancelled};
    }
    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        // Includes an admitted/captured stage whose Run was skipped by cancellation or joined Close.
        const bool held = _display.has_value();
        _display.reset();
        if (held && _probe.retired)
            _probe.retired(_probe.context);
    }
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        // Every terminal stage reaches this hook, including cancellation before the initial capture.
        // DeferCompletion is success-only and cannot supply the original ticket in that case.
        _ticket = ticket;
        return false;
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || _captured || _owner._shutdownStarted ||
            _owner._selectionCursorRequests.Latest() != std::optional{_request} ||
            _owner._cursorRevision != _request.key.epoch)
            return false;
        auto display = _owner._session.CaptureSelectionCursorProbe(_plan);
        if (!display)
            return false;
        _display = std::move(display);
        _captured = true;
        ++_owner._selectionCursorCaptures;
        return _owner._jobs.Resume(ticket);
    }
    void Completed(JobResult result) noexcept override
    {
        if (_owner._selectionCursorTicket != std::optional{_ticket})
            return;
        _owner._selectionCursorTicket.reset();
        const bool current = !_owner._shutdownStarted && _owner._cursorRevision == _request.key.epoch &&
                             _owner._session.DescribeSelectionCursorProbe() == std::optional{_plan};
        const auto value = result.status == JobStatus::Finished && current ? _inside : std::optional<bool>{};
        if (!_owner._selectionCursorRequests.Acknowledge(_request, value))
            std::terminate();
        ++_owner._selectionCursorCompletedCallbacks;
        if (value.has_value() && !_owner._quietUi && _owner._window && IsWindowVisible(_owner._window.get()))
        {
            POINT screen{};
            if (GetCursorPos(&screen) && WindowFromPoint(screen) == _owner._window.get())
                (void)PostMessageW(_owner._window.get(), WM_SETCURSOR, reinterpret_cast<WPARAM>(_owner._window.get()),
                                   MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        }
        _owner.StartSelectionCursorOwner();
    }

  private:
    Application& _owner; // Owner hooks only. Run and worker retirement never dereference Application.
    const SelectionCursorRequests::Request _request;
    const SelectionCursorProbePlan _plan;
    const SelectionHitObserver _observer;
    const SelectionCursorWorkerProbe _probe;
    JobTicket _ticket;
    bool _captured = false;
    std::optional<SelectionDisplayValue> _display;
    std::optional<bool> _inside;
};

void Application::InvalidateSelectionCursorOwner() noexcept
{
    _selectionCursorRequests.Invalidate();
    if (_selectionCursorTicket)
        (void)_jobs.Cancel(_selectionCursorTicket.value());
}

bool Application::PreparedCursorOverSelection(Vec2 pixel) noexcept
{
    const auto plan = _session.DescribeSelectionCursorProbe();
    constexpr double margin = 1e-6; // Same conservative bounds prefilter as the existing cursor reference.
    if (!plan || !std::isfinite(pixel.x) || !std::isfinite(pixel.y) || pixel.x < plan.value().bounds.minX - margin ||
        pixel.x > plan.value().bounds.maxX + margin || pixel.y < plan.value().bounds.minY - margin ||
        pixel.y > plan.value().bounds.maxY + margin)
    {
        InvalidateSelectionCursorOwner();
        return false;
    }
    const auto observation = _selectionCursorRequests.Observe(
        {plan.value().document.documentSerial, _cursorRevision, pixel.x, pixel.y, plan.value().document.generation});
    if (_selectionCursorRequests.NeedsCancellation() && _selectionCursorTicket)
        (void)_jobs.Cancel(_selectionCursorTicket.value());
    StartSelectionCursorOwner();
    return observation.state == SelectionCursorRequests::State::Ready && observation.inside;
}

void Application::StartSelectionCursorOwner() noexcept
{
    if (_shutdownStarted || !_asyncSelectionCursorEnabled || _selectionCursorTicket)
        return;
    const auto request = _selectionCursorRequests.Begin();
    if (!request)
        return;
    const auto plan = _session.DescribeSelectionCursorProbe();
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    constexpr std::uint64_t metadata = sizeof(SelectionCursorJob) + 4096;
    if (!plan || plan.value().document.documentSerial != request.value().key.document ||
        plan.value().document.generation != request.value().key.generation ||
        _cursorRevision != request.value().key.epoch || plan.value().retainedBytes > maximum - metadata)
    {
        (void)_selectionCursorRequests.Acknowledge(request.value(), std::nullopt);
        return;
    }
    HRESULT admitted = E_OUTOFMEMORY;
    JobTicket ticket;
    try
    {
        std::unique_ptr<Job> job = std::make_unique<SelectionCursorJob>(*this, request.value(), plan.value());
        admitted = static_cast<HRESULT>(_jobs.Submit(job,
                                                     {.kind = JobClass::Background,
                                                      .memoryBytes = plan.value().retainedBytes + metadata,
                                                      .deferredRetirement = true,
                                                      .allowMemoryWaiting = false},
                                                     ticket));
    }
    catch (const std::bad_alloc&)
    {
        admitted = E_OUTOFMEMORY; // Fail the entire optional probe; never run its geometry on the owner.
    }
    if (FAILED(admitted))
    {
        (void)_selectionCursorRequests.Acknowledge(request.value(), std::nullopt);
        return;
    }
    _selectionCursorTicket = ticket;
    ++_selectionCursorDispatches;
}
