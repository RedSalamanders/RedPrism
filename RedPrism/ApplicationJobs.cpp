#include "Application.h"

#include "ChromeHost.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <format>

namespace
{
struct ShutdownEvidence final
{
    wil::unique_handle started{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> returned{false};
    std::atomic<bool> mtaAlive{false};
    bool destroyedAfterRun = false;
    unsigned int applies = 0;
};

class ShutdownGateJob final : public Job
{
  public:
    explicit ShutdownGateJob(ShutdownEvidence& evidence) noexcept : _evidence(evidence) {}
    ~ShutdownGateJob() override
    {
        _evidence.destroyedAfterRun = _evidence.returned.load();
    }
    JobResult Run(JobContext& context) override
    {
        (void)SetEvent(_evidence.started.get());
        if (WaitForSingleObject(context.shutdownEvent, 10000) != WAIT_OBJECT_0)
        {
            SelfTestFailure(L"Worker shutdown failure-exit gate timed out.\n");
            std::_Exit(5); // failure bound only; do not destroy a still-borrowed test frame
        }
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        _evidence.mtaAlive.store(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA);
        _evidence.returned.store(true);
        return {};
    }
    void Completed(JobResult) noexcept override
    {
        ++_evidence.applies;
    }

  private:
    ShutdownEvidence& _evidence;
};

struct ActivityEvidence final
{
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<DWORD> waitResult{MAXDWORD};
    std::atomic<DWORD> waitError{0};
    std::atomic<bool> returned{false};
    bool applied = false;
};

class ActivityGateJob final : public Job
{
  public:
    explicit ActivityGateJob(ActivityEvidence& evidence) noexcept : _evidence(evidence) {}
    JobResult Run(JobContext& context) override
    {
        context.ReportProgress(37);
        (void)SetEvent(_evidence.entered.get());
        const HANDLE handles[]{_evidence.release.get(), context.shutdownEvent};
        const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        _evidence.waitError.store(waited == WAIT_FAILED ? GetLastError() : 0);
        _evidence.waitResult.store(waited);
        _evidence.returned.store(true);
        if (waited == WAIT_OBJECT_0)
        {
            return {};
        }
        return {JobStatus::Cancelled};
    }
    void Completed(JobResult result) noexcept override
    {
        _evidence.applied = result.status == JobStatus::Finished;
    }

  private:
    ActivityEvidence& _evidence;
};
} // namespace

void Application::JobNoticeThunk(void* context, JobNotice notice) noexcept
{
    auto& owner = *static_cast<Application*>(context);
    LARGE_INTEGER now{};
    (void)QueryPerformanceCounter(&now);
    const auto ticks = static_cast<std::uint64_t>(now.QuadPart);
    owner._jobActivity.Observe(notice, ticks);
    owner.SyncJobActivity(ticks);
}

void Application::SyncJobActivity(std::uint64_t nowQpc) noexcept
{
    const auto& active = _jobActivity.Selected();
    const bool visible = _jobActivity.Visible(nowQpc, _jobQpcFrequency);
    if (_jobIndicatorTimer && (!active || visible || active->ticket != *_jobIndicatorTimer))
    {
        if (_window)
        {
            (void)KillTimer(_window.get(), kJobIndicatorTimerId);
        }
        _jobIndicatorTimer.reset();
    }
    if (active && !visible && !_jobIndicatorTimer && _window && _jobQpcFrequency != 0)
    {
        const auto elapsed = nowQpc >= active->startQpc ? nowQpc - active->startQpc : 0;
        const double remaining = 250.0 - static_cast<double>(elapsed) * 1000.0 / static_cast<double>(_jobQpcFrequency);
        const UINT delay = static_cast<UINT>(std::clamp(std::ceil(remaining), 1.0, 250.0));
        if (SetTimer(_window.get(), kJobIndicatorTimerId, delay, nullptr) != 0)
        {
            _jobIndicatorTimer = active->ticket;
        }
        else
        {
            // This is a host resource failure, not permission to busy-poll or pretend the deadline is armed.
            OnFrameLoopFailure(E_OUTOFMEMORY);
        }
    }
    if (_chrome)
    {
        _chrome->SetJobProgress(visible, active ? active->percent : std::nullopt);
    }
}

void Application::OnJobIndicatorTimer() noexcept
{
    // Win32 timers repeat unless removed. Kill before any chrome write; a stale queued WM_TIMER only rechecks
    // the current generation/deadline and cannot reveal a quick or already-retired job.
    (void)KillTimer(_window.get(), kJobIndicatorTimerId);
    _jobIndicatorTimer.reset();
    LARGE_INTEGER now{};
    (void)QueryPerformanceCounter(&now);
    SyncJobActivity(static_cast<std::uint64_t>(now.QuadPart));
}

int Application::RunJobActivitySelfTest() noexcept
{
    ActivityEvidence evidence;
    if (!_chrome || !evidence.entered || !evidence.release || _jobActivity.Selected())
    {
        SelfTestFailure(L"Busy-indicator self-test could not initialize.\n");
        return 5;
    }
    std::unique_ptr<Job> job = std::make_unique<ActivityGateJob>(evidence);
    JobTicket ticket;
    if (FAILED(_jobs.Submit(job, {JobClass::Commit}, ticket)))
    {
        SelfTestFailure(L"Busy-indicator self-test could not submit its gated job.\n");
        return 5;
    }
    const auto settle = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(evidence.release.get());
            if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
            {
                SelfTestFailure(L"Busy-indicator self-test failed to settle; its input must stay alive.\n");
                std::_Exit(5);
            }
        });
    if (WaitForSingleObject(evidence.entered.get(), 10000) != WAIT_OBJECT_0)
    {
        SelfTestFailure(L"Busy-indicator worker failed to reach its gate.\n");
        return 5;
    }
    (void)DrainCompletions(_jobs);
    const auto selected = _jobActivity.Selected();
    if (!selected || selected->ticket != ticket || selected->percent != 37u)
    {
        SelfTestFailure(L"Busy-indicator observer missed submit/progress.\n");
        return 5;
    }
    // Exact virtual QPC boundaries, no assertion about the speed of this host or the test's worker scheduling.
    SyncJobActivity(selected->startQpc);
    if (_chrome->JobProgressVisible() || !_jobIndicatorTimer)
    {
        SelfTestFailure(L"A newly started job must arm one deadline without showing the indicator.\n");
        return 5;
    }
    SyncJobActivity(selected->startQpc + (_jobQpcFrequency + 3) / 4);
    if (!_chrome->JobProgressVisible() || _chrome->JobProgressIndeterminate() || _chrome->JobProgressPercent() != 37u ||
        _jobIndicatorTimer)
    {
        SelfTestFailure(L"The busy deadline did not show determinate progress and disarm the timer.\n");
        return 5;
    }
    for (const float dpi : {96.0f, 144.0f})
    {
        if (FAILED(_chrome->Prepare(_renderer.Width(), _renderer.Height(), dpi)))
        {
            return 5;
        }
        const D2D1_RECT_F bounds = _chrome->JobProgressBoundsDip();
        const float width = static_cast<float>(_renderer.Width()) * 96.0f / dpi;
        const float height = static_cast<float>(_renderer.Height()) * 96.0f / dpi;
        if (bounds.left < 0 || bounds.right > width || bounds.bottom > height || bounds.right <= bounds.left ||
            bounds.top < height - ChromeHost::kStatusHeightDip || bounds.bottom <= bounds.top)
        {
            SelfTestFailure(L"Busy progress bounds escaped their status section at 96/144 DPI.\n");
            return 5;
        }
    }
    // Indeterminate animation participates only while the control is visible. The independent HostTests state
    // table covers quick completion and transitions between simultaneous jobs.
    _chrome->SetJobProgress(true, std::nullopt);
    if (!_chrome->JobProgressIndeterminate() || !_chrome->NeedsAnimation())
    {
        SelfTestFailure(L"The visible indeterminate indicator did not request animation.\n");
        return 5;
    }
    // The pinned chrome cancels ordinary button presses on a sibling visibility change. Busy transitions must
    // defer that one write until release, while preserving progress and the click through intervening frames.
    for (const bool show : {true, false})
    {
        const EditorTool target = show ? EditorTool::Brush : EditorTool::Move;
        _chrome->SetJobProgress(!show, 37);
        _chrome->SetCanvasFocused(true);
        (void)_chrome->Prepare(_renderer.Width(), _renderer.Height(), ChromeDpi());
        const D2D1_RECT_F button = _chrome->ToolButtonBoundsDip(target);
        const float scale = ChromeDpi() / 96.0f;
        DxUi::PointerEvent pointer{};
        pointer.xPixels = (button.left + button.right) * 0.5f * scale;
        pointer.yPixels = (button.top + button.bottom) * 0.5f * scale;
        pointer.action = DxUi::PointerAction::Down;
        const bool pressed = _chrome->DispatchPointer(pointer);
        _chrome->SetJobProgress(show, 37);
        (void)_chrome->Prepare(_renderer.Width(), _renderer.Height(), ChromeDpi());
        pointer.action = DxUi::PointerAction::Up;
        const bool released = _chrome->DispatchPointer(pointer);
        if (!pressed || !released || _tool != target || _chrome->JobProgressVisible() != show)
        {
            SelfTestFailure(L"A busy-indicator visibility transition cancelled a held rail button.\n");
            return 5;
        }
    }
    const bool released = SetEvent(evidence.release.get()) != FALSE;
    const auto settled = SettleJobs(_jobs, 10000, DispatchMessageThunk, this);
    if (settled != SettleResult::Settled || !evidence.applied || _jobActivity.Selected() || _jobIndicatorTimer ||
        _chrome->JobProgressVisible())
    {
        SelfTestFailure(std::format(L"Completing the busy job did not retire the indicator: settle={}, applied={}, "
                                    L"selected={}, timer={}, visible={}, idle={}, shutdown={}, window={}, "
                                    L"frameFailure={:#x}, released={}, workerReturned={}, wait={:#x}, "
                                    L"waitError={:#x}, sameTicket={}.\n",
                                    static_cast<unsigned int>(settled), evidence.applied,
                                    _jobActivity.Selected().has_value(), _jobIndicatorTimer.has_value(),
                                    _chrome->JobProgressVisible(), _jobs.IsIdle(), _shutdownStarted,
                                    static_cast<bool>(_window), static_cast<unsigned int>(_frameLoopFailure), released,
                                    evidence.returned.load(), evidence.waitResult.load(), evidence.waitError.load(),
                                    _jobActivity.Selected() && _jobActivity.Selected()->ticket == ticket));
        return 5;
    }
    OnJobIndicatorTimer(); // a timer message already queued before KillTimer must remain harmless
    if (_chrome->JobProgressVisible() || _jobIndicatorTimer)
    {
        SelfTestFailure(L"A stale timer revived a completed job's indicator.\n");
        return 5;
    }
    (void)_chrome->Prepare(_renderer.Width(), _renderer.Height(), ChromeDpi());
    return 0;
}

int Application::RunJobShutdownSelfTest() noexcept
{
    APTTYPE beforeApartment{};
    APTTYPEQUALIFIER beforeQualifier{};
    if (FAILED(CoGetApartmentType(&beforeApartment, &beforeQualifier)) ||
        (beforeApartment != APTTYPE_STA && beforeApartment != APTTYPE_MAINSTA))
    {
        SelfTestFailure(L"The failure-exit self-test requires the initialized UI STA.\n");
        return 5;
    }
    ShutdownEvidence evidence;
    if (!evidence.started)
    {
        SelfTestFailure(L"Could not create the worker shutdown gate.\n");
        return 5;
    }
    // A second owner needs no HWND/device. Exercise the same explicit Shutdown used by Main after a mode's early
    // failure return, while that owner still has a running callback and the caller's STA is alive.
    auto owner = std::make_unique<Application>(_instance, true, nullptr, 0);
    const auto shutdown = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
    if (FAILED(owner->_jobs.Initialize(1)))
    {
        SelfTestFailure(L"Could not start the failure-exit worker pool.\n");
        return 5;
    }
    const auto failWithJobInFlight = [&]() noexcept
    {
        std::unique_ptr<Job> job = std::make_unique<ShutdownGateJob>(evidence);
        JobTicket ticket;
        if (FAILED(owner->_jobs.Submit(job, {JobClass::Commit}, ticket)))
        {
            return 1;
        }
        if (WaitForSingleObject(evidence.started.get(), 10000) != WAIT_OBJECT_0)
        {
            SelfTestFailure(L"Failure-exit worker did not enter its gate.\n");
            std::_Exit(5);
        }
        return 5; // simulate a self-test/bench assertion failing before any successful close path
    };
    const int failedMode = failWithJobInFlight();
    owner->Shutdown();
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    if (failedMode != 5 || !owner->_shutdownComplete || !evidence.returned.load() || !evidence.mtaAlive.load() ||
        !evidence.destroyedAfterRun || evidence.applies != 0 || FAILED(CoGetApartmentType(&apartment, &qualifier)) ||
        apartment != beforeApartment || qualifier != beforeQualifier)
    {
        SelfTestFailure(std::format(
            L"Failure-exit shutdown: mode={}, closed={}, returned={}, workerMta={}, "
            L"destroyedAfterRun={}, applies={}, apartment={} (before {}), qualifier={} (before {}).\n",
            failedMode, owner->_shutdownComplete, evidence.returned.load(), evidence.mtaAlive.load(),
            evidence.destroyedAfterRun, evidence.applies, static_cast<int>(apartment),
            static_cast<int>(beforeApartment), static_cast<int>(qualifier), static_cast<int>(beforeQualifier)));
        return 5;
    }
    return 0;
}
