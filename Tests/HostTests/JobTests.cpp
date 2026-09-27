#include "Jobs.h"

#include <windows.h>

#include "JobTests.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s job.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
}

wil::unique_handle Event()
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL job.create-event\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return event;
}

// Every wait is a deadlock bound. It does not measure or assert expected runtime.
void Wait(HANDLE event) noexcept
{
    if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
    {
        std::printf("FAIL job.gate-timeout\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
}

// Tests that inspect a published completion before draining it need a wake from that job only. An OS
// memory notification also wakes production schedulers; deterministic admission keeps it out of these gates.
bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

void WaitForScheduler(JobScheduler& scheduler) noexcept
{
    const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
    if (!wake || WaitForSingleObject(wake, 10000) != WAIT_OBJECT_0)
    {
        std::printf("FAIL job.scheduler-wake-timeout\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
}

void SettleAndExpect(JobScheduler& scheduler, const char* name, JobScheduler::Apply apply = nullptr,
                     void* applyContext = nullptr) noexcept
{
    const SettleResult result = SettleJobs(scheduler, 10000, nullptr, nullptr, apply, applyContext);
    Expect(result == SettleResult::Settled, name);
    if (result != SettleResult::Settled)
    {
        std::fflush(stdout);
        std::_Exit(1);
    }
}

class FunctionJob final : public Job
{
  public:
    using RunFunction = JobResult (*)(JobContext&, void*);
    using CompleteFunction = void (*)(void*, JobResult) noexcept;

    FunctionJob(RunFunction run, CompleteFunction completed, void* context) noexcept
        : _run(run), _completed(completed), _context(context)
    {
    }

    JobResult Run(JobContext& context) override
    {
        return _run(context, _context);
    }

    void Completed(JobResult result) noexcept override
    {
        _completed(_context, result);
    }

  private:
    RunFunction _run;
    CompleteFunction _completed;
    void* _context;
};

JobResult ReturnFinished(JobContext&, void*)
{
    return {};
}

void IgnoreCompletion(void*, JobResult) noexcept {}

struct MessageGate final
{
    HANDLE release = nullptr;
    std::atomic<unsigned int>* order = nullptr;
    std::atomic<unsigned int> handled{0};
};

constexpr UINT kReleaseMessage = WM_APP + 37;
constexpr wchar_t kMessageWindowClass[] = L"RedPrism.JobTests.MessageOnly";

LRESULT CALLBACK MessageWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    if (message == kReleaseMessage)
    {
        auto* gate = reinterpret_cast<MessageGate*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (gate)
        {
            gate->handled.fetch_add(1);
            gate->order->fetch_add(1);
            (void)SetEvent(gate->release);
        }
        return 0;
    }
    if (message == WM_NCDESTROY)
    {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

wil::unique_hwnd CreateMessageOnlyWindow(MessageGate& gate, bool& registeredClass)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = static_cast<UINT>(sizeof(windowClass));
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = MessageWindowProc;
    windowClass.lpszClassName = kMessageWindowClass;
    const ATOM atom = RegisterClassExW(&windowClass);
    registeredClass = atom != 0;
    if (!registeredClass && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        std::printf("FAIL job.register-message-window\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    wil::unique_hwnd window{
        CreateWindowExW(0, kMessageWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, &gate)};
    if (!window)
    {
        std::printf("FAIL job.create-message-window\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return window;
}

struct PumpJobState final
{
    HANDLE started = nullptr;
    HANDLE release = nullptr;
    std::atomic<unsigned int> liveJobs{0};
    std::atomic<unsigned int> successfulApplies{0};
    std::atomic<unsigned int> callbackCount{0};
    std::atomic<unsigned int> callbackStatus{static_cast<unsigned int>(JobStatus::Failed)};
    std::atomic<unsigned int> callbackOrder{0};
    std::atomic<unsigned int>* order = nullptr;
};

class PumpJob final : public Job
{
  public:
    explicit PumpJob(PumpJobState& state) noexcept : _state(state)
    {
        _state.liveJobs.fetch_add(1);
    }

    ~PumpJob() override
    {
        _state.liveJobs.fetch_sub(1);
    }

    JobResult Run(JobContext&) override
    {
        (void)SetEvent(_state.started);
        Wait(_state.release);
        return {};
    }

    void Completed(JobResult result) noexcept override
    {
        _state.callbackStatus.store(static_cast<unsigned int>(result.status));
        _state.callbackOrder.store(_state.order->fetch_add(1) + 1);
        _state.callbackCount.fetch_add(1);
    }

  private:
    PumpJobState& _state;
};

void ApplyPumpResult(void* context, Job& job, JobResult result) noexcept
{
    auto& state = *static_cast<PumpJobState*>(context);
    if (result.status == JobStatus::Finished)
    {
        state.successfulApplies.fetch_add(1);
    }
    job.Completed(result);
}

void TestMessagePumpOrdering(bool cancel)
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) >= 0, cancel ? "pump-cancel-initialize" : "pump-apply-initialize");
    auto started = Event();
    auto release = Event();
    std::atomic<unsigned int> order{0};
    PumpJobState callback;
    callback.started = started.get();
    callback.release = release.get();
    callback.order = &order;
    MessageGate gate{release.get(), &order};
    bool registeredClass = false;
    auto window = CreateMessageOnlyWindow(gate, registeredClass);

    std::unique_ptr<Job> job = std::make_unique<PumpJob>(callback);
    JobTicket ticket;
    const auto submitted = scheduler.Submit(job, {}, ticket);
    Expect(submitted >= 0 && !job, cancel ? "pump-cancel-submit" : "pump-apply-submit");
    Wait(started.get());

    if (cancel)
    {
        Expect(scheduler.Cancel(ticket), "pump-cancel-after-worker-started");
    }

    Expect(PostMessageW(window.get(), kReleaseMessage, 0, 0) != FALSE,
           cancel ? "pump-cancel-post-sentinel" : "pump-apply-post-sentinel");
    const auto pumped = PumpMessages();
    Expect(!pumped.quit && pumped.dispatched > 0 && gate.handled.load() == 1,
           cancel ? "pump-cancel-sentinel-dispatched" : "pump-apply-sentinel-dispatched");

    SettleAndExpect(scheduler, cancel ? "pump-cancel-settled" : "pump-apply-settled", ApplyPumpResult, &callback);
    const unsigned int successfulApplies = callback.successfulApplies.load();
    const unsigned int callbackCount = callback.callbackCount.load();
    const unsigned int callbackStatus = callback.callbackStatus.load();
    const unsigned int callbackOrder = callback.callbackOrder.load();
    if (cancel)
    {
        Expect(successfulApplies == 0 && callbackCount == 1 &&
                   callbackStatus == static_cast<unsigned int>(JobStatus::Cancelled),
               "pump-cancel-no-successful-apply");
    }
    else
    {
        Expect(successfulApplies == 1 && callbackCount == 1 &&
                   callbackStatus == static_cast<unsigned int>(JobStatus::Finished) && callbackOrder == 2,
               "pump-apply-once-after-sentinel");
    }
    Expect(scheduler.IsIdle(), cancel ? "pump-cancel-no-live-jobs" : "pump-apply-no-live-jobs");
    Expect(callback.liveJobs.load() == 0,
           cancel ? "pump-cancel-payload-destroyed-after-settle" : "pump-apply-payload-destroyed-after-settle");
    scheduler.Close();
    window.reset();
    if (registeredClass)
    {
        Expect(UnregisterClassW(kMessageWindowClass, GetModuleHandleW(nullptr)) != FALSE,
               cancel ? "pump-cancel-unregister-window-class" : "pump-apply-unregister-window-class");
    }
}

struct ProgressState final
{
    HANDLE reportsIssued = nullptr;
    HANDLE release = nullptr;
    std::atomic<unsigned int> callbacks{0};
    std::atomic<unsigned int> lastPercent{0};
    std::atomic<unsigned int> invalidPercent{0};
    std::atomic<unsigned int> afterFinal{0};
    std::atomic<unsigned int> completed{0};
    std::atomic<unsigned int> resultStatus{static_cast<unsigned int>(JobStatus::Failed)};
};

class ProgressJob final : public Job
{
  public:
    explicit ProgressJob(ProgressState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        context.ReportProgress(1);
        context.ReportProgress(0);
        context.ReportProgress(101);
        (void)SetEvent(_state.reportsIssued);
        Wait(_state.release);
        // These may coalesce. The test makes no publication-count or elapsed-time assumption for them.
        context.ReportProgress(2);
        context.ReportProgress(3);
        return {};
    }

    void Progress(unsigned int percent) noexcept override
    {
        const unsigned int previous = _state.lastPercent.exchange(percent);
        if (percent == 0 || percent > 100 || (previous != 0 && percent < previous))
        {
            _state.invalidPercent.fetch_add(1);
        }
        if (_state.completed.load() != 0)
        {
            _state.afterFinal.fetch_add(1);
        }
        _state.callbacks.fetch_add(1);
    }

    void Completed(JobResult result) noexcept override
    {
        _state.resultStatus.store(static_cast<unsigned int>(result.status));
        _state.completed.fetch_add(1);
    }

  private:
    ProgressState& _state;
};

void TestProgressOrderingAndCoalescing()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1, {}, {StableMemory, nullptr}) >= 0, "progress-initialize");
    auto reportsIssued = Event();
    auto release = Event();
    ProgressState state{reportsIssued.get(), release.get()};
    std::unique_ptr<Job> job = std::make_unique<ProgressJob>(state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {}, ticket) >= 0 && !job, "progress-submit");
    Wait(reportsIssued.get());
    WaitForScheduler(scheduler);
    (void)DrainCompletions(scheduler);
    Expect(state.callbacks.load() == 1 && state.lastPercent.load() == 1 && state.invalidPercent.load() == 0 &&
               state.completed.load() == 0,
           "progress-one-delivered-before-release-invalid-values-ignored");

    (void)SetEvent(release.get());
    SettleAndExpect(scheduler, "progress-final-settled");
    const unsigned int callbacksAtFinal = state.callbacks.load();
    Expect(state.completed.load() == 1 && state.resultStatus.load() == static_cast<unsigned int>(JobStatus::Finished) &&
               state.invalidPercent.load() == 0 && state.afterFinal.load() == 0,
           "progress-valid-monotonic-callbacks-before-final");
    Expect(DrainCompletions(scheduler) == 0 && state.callbacks.load() == callbacksAtFinal &&
               state.afterFinal.load() == 0,
           "progress-no-callback-after-final");
    scheduler.Close();
}

struct FailureState final
{
    std::atomic<unsigned int> callbacks{0};
    std::atomic<unsigned int> status{0};
    std::atomic<std::int32_t> error{0};
};

JobResult ThrowBadAlloc(JobContext&, void*)
{
    throw std::bad_alloc{};
}

void RecordFailure(void* context, JobResult result) noexcept
{
    auto& state = *static_cast<FailureState*>(context);
    state.status.store(static_cast<unsigned int>(result.status));
    state.error.store(result.error);
    state.callbacks.fetch_add(1);
}

void TestBadAllocTranslation()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) >= 0, "bad-alloc-initialize");
    FailureState state;
    std::unique_ptr<Job> job = std::make_unique<FunctionJob>(ThrowBadAlloc, RecordFailure, &state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {}, ticket) >= 0 && !job, "bad-alloc-submit");
    SettleAndExpect(scheduler, "bad-alloc-settled");
    Expect(state.callbacks.load() == 1 && state.status.load() == static_cast<unsigned int>(JobStatus::Failed) &&
               state.error.load() == static_cast<std::int32_t>(E_OUTOFMEMORY),
           "bad-alloc-failed-e-outofmemory-once");
    scheduler.Close();
}

struct CapacityState final
{
    HANDLE lastStarted = nullptr;
    HANDLE release = nullptr;
    std::atomic<unsigned int> calls{0};
};

struct CapacityJobContext final
{
    CapacityState* state = nullptr;
    bool blockAll = false;
};

JobResult RunCapacityJob(JobContext&, void* context)
{
    auto& job = *static_cast<CapacityJobContext*>(context);
    auto& state = *job.state;
    const unsigned int ordinal = state.calls.fetch_add(1) + 1;
    if (job.blockAll || ordinal == JobScheduler::kCapacity)
    {
        (void)SetEvent(state.lastStarted);
        Wait(state.release);
    }
    return {};
}

void TestCapacityAndOwnership()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) >= 0, "capacity-initialize");
    auto lastStarted = Event();
    auto release = Event();
    CapacityState state{lastStarted.get(), release.get(), {0}};
    std::array<CapacityJobContext, JobScheduler::kCapacity> contexts{};
    std::vector<JobTicket> tickets(JobScheduler::kCapacity);
    bool accepted = true;
    for (std::size_t index = 0; index < JobScheduler::kCapacity; ++index)
    {
        contexts[index] = CapacityJobContext{&state, false};
        std::unique_ptr<Job> job = std::make_unique<FunctionJob>(RunCapacityJob, IgnoreCompletion, &contexts[index]);
        if (scheduler.Submit(job, {}, tickets[index]) < 0 || job)
        {
            accepted = false;
            break;
        }
    }
    Expect(accepted, "capacity-accepts-128-owned-jobs");
    Wait(lastStarted.get());
    Expect(state.calls.load() == JobScheduler::kCapacity, "capacity-holds-127-completed-undrained-and-one-running");
    std::unique_ptr<Job> rejected = std::make_unique<FunctionJob>(ReturnFinished, IgnoreCompletion, nullptr);
    Job* const rejectedAddress = rejected.get();
    JobTicket extra;
    const HRESULT overflow = static_cast<HRESULT>(scheduler.Submit(rejected, {}, extra));
    Expect(overflow == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) && rejected.get() == rejectedAddress,
           "capacity-rejection-retains-ownership");
    (void)SetEvent(release.get());
    SettleAndExpect(scheduler, "capacity-drains-after-release");
    Expect(state.calls.load() == JobScheduler::kCapacity && scheduler.IsIdle(), "capacity-no-live-jobs-after-settle");
    scheduler.Close();
}

struct ReuseState final
{
    HANDLE started = nullptr;
    HANDLE release = nullptr;
    std::atomic<unsigned int> result{static_cast<unsigned int>(JobStatus::Failed)};
};

JobResult ReusedSlotJob(JobContext& context, void* value)
{
    auto& state = *static_cast<ReuseState*>(value);
    (void)SetEvent(state.started);
    Wait(state.release);
    const JobStatus result = context.cancellation.IsCancelled() ? JobStatus::Cancelled : JobStatus::Finished;
    state.result.store(static_cast<unsigned int>(result));
    return {result, 0};
}

void TestStaleTicketCannotCancelReuse()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) >= 0, "stale-initialize");
    auto oldStarted = Event();
    auto oldRelease = Event();
    CapacityState oldState{oldStarted.get(), oldRelease.get(), {0}};
    std::array<CapacityJobContext, JobScheduler::kCapacity> oldContexts{};
    std::vector<JobTicket> oldTickets(JobScheduler::kCapacity);
    bool accepted = true;
    for (std::size_t index = 0; index < JobScheduler::kCapacity; ++index)
    {
        oldContexts[index] = CapacityJobContext{&oldState, true};
        std::unique_ptr<Job> job = std::make_unique<FunctionJob>(RunCapacityJob, IgnoreCompletion, &oldContexts[index]);
        if (scheduler.Submit(job, {}, oldTickets[index]) < 0 || job)
        {
            accepted = false;
            break;
        }
    }
    Expect(accepted, "stale-fill-capacity");
    Wait(oldStarted.get());
    (void)SetEvent(oldRelease.get());
    SettleAndExpect(scheduler, "stale-drain-original-generations");

    auto started = Event();
    auto release = Event();
    ReuseState state{started.get(), release.get(), {static_cast<unsigned int>(JobStatus::Failed)}};
    std::vector<JobTicket> replacements(JobScheduler::kCapacity);
    bool reused = false;
    std::size_t oldIndex = 0;
    for (std::size_t index = 0; index < JobScheduler::kCapacity; ++index)
    {
        std::unique_ptr<Job> job = std::make_unique<FunctionJob>(ReusedSlotJob, IgnoreCompletion, &state);
        if (scheduler.Submit(job, {}, replacements[index]) < 0 || job)
        {
            accepted = false;
            break;
        }
        for (std::size_t previous = 0; previous < oldTickets.size(); ++previous)
        {
            if (oldTickets[previous].slot == replacements[index].slot)
            {
                reused = oldTickets[previous].generation != replacements[index].generation;
                oldIndex = previous;
                break;
            }
        }
        if (reused)
        {
            break;
        }
    }
    Expect(accepted && reused, "stale-slot-reused-with-new-generation");
    Wait(started.get());
    Expect(!scheduler.Cancel(oldTickets[oldIndex]), "stale-ticket-cannot-cancel-reused-slot");
    (void)SetEvent(release.get());
    SettleAndExpect(scheduler, "stale-replacement-settled");
    Expect(state.result.load() == static_cast<unsigned int>(JobStatus::Finished), "stale-replacement-not-cancelled");
    scheduler.Close();
}

struct OrderState final
{
    HANDLE firstStarted = nullptr;
    HANDLE firstRelease = nullptr;
    HANDLE secondStarted = nullptr;
    std::atomic<unsigned int> firstApplied{0};
    std::atomic<unsigned int> secondStartedBeforeApply{0};
    std::atomic<unsigned int> secondStatus{static_cast<unsigned int>(JobStatus::Failed)};
    Job* firstJob = nullptr;
};

JobResult FirstSerialJob(JobContext&, void* context)
{
    auto& state = *static_cast<OrderState*>(context);
    (void)SetEvent(state.firstStarted);
    Wait(state.firstRelease);
    return {};
}

JobResult SecondSerialJob(JobContext&, void* context)
{
    auto& state = *static_cast<OrderState*>(context);
    if (state.firstApplied.load() == 0)
    {
        state.secondStartedBeforeApply.fetch_add(1);
    }
    (void)SetEvent(state.secondStarted);
    return {};
}

void RecordSerialApply(void* context, Job& job, JobResult result) noexcept
{
    auto& state = *static_cast<OrderState*>(context);
    if (&job == state.firstJob)
    {
        if (result.status == JobStatus::Finished)
        {
            state.firstApplied.store(1);
        }
    }
    else
    {
        state.secondStatus.store(static_cast<unsigned int>(result.status));
    }
}

struct DifferentKeyState final
{
    HANDLE firstStarted = nullptr;
    HANDLE firstRelease = nullptr;
    HANDLE secondStarted = nullptr;
};

JobResult DifferentKeyFirst(JobContext&, void* context)
{
    auto& state = *static_cast<DifferentKeyState*>(context);
    (void)SetEvent(state.firstStarted);
    Wait(state.firstRelease);
    return {};
}

JobResult DifferentKeySecond(JobContext&, void* context)
{
    (void)SetEvent(static_cast<DifferentKeyState*>(context)->secondStarted);
    return {};
}

void TestSerialKeys()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(2, {}, {StableMemory, nullptr}) >= 0, "serial-initialize");
    auto firstStarted = Event();
    auto firstRelease = Event();
    auto secondStarted = Event();
    OrderState state{firstStarted.get(), firstRelease.get(), secondStarted.get()};
    constexpr JobOptions serial{JobClass::Stroke, 0x51};
    JobTicket firstTicket;
    JobTicket secondTicket;
    std::unique_ptr<Job> first = std::make_unique<FunctionJob>(FirstSerialJob, IgnoreCompletion, &state);
    std::unique_ptr<Job> second = std::make_unique<FunctionJob>(SecondSerialJob, IgnoreCompletion, &state);
    state.firstJob = first.get();
    Expect(scheduler.Submit(first, serial, firstTicket) >= 0 && !first, "serial-first-submit");
    Wait(firstStarted.get());
    Expect(scheduler.Submit(second, serial, secondTicket) >= 0 && !second, "serial-second-submit");
    (void)SetEvent(firstRelease.get());
    WaitForScheduler(scheduler);
    (void)DrainCompletions(scheduler, RecordSerialApply, &state);
    Wait(secondStarted.get());
    Expect(state.secondStartedBeforeApply.load() == 0, "serial-second-waits-through-first-ui-apply");
    SettleAndExpect(scheduler, "serial-same-key-settled", RecordSerialApply, &state);
    Expect(state.secondStatus.load() == static_cast<unsigned int>(JobStatus::Finished), "serial-second-applied");

    auto otherFirstStarted = Event();
    auto otherFirstRelease = Event();
    auto otherSecondStarted = Event();
    DifferentKeyState different{otherFirstStarted.get(), otherFirstRelease.get(), otherSecondStarted.get()};
    JobTicket otherFirstTicket;
    JobTicket otherSecondTicket;
    std::unique_ptr<Job> blocked = std::make_unique<FunctionJob>(DifferentKeyFirst, IgnoreCompletion, &different);
    std::unique_ptr<Job> independent = std::make_unique<FunctionJob>(DifferentKeySecond, IgnoreCompletion, &different);
    constexpr JobOptions keyA{JobClass::Selection, 0xa1};
    constexpr JobOptions keyB{JobClass::Selection, 0xb2};
    Expect(scheduler.Submit(blocked, keyA, otherFirstTicket) >= 0 && !blocked, "different-key-first-submit");
    Wait(otherFirstStarted.get());
    Expect(scheduler.Submit(independent, keyB, otherSecondTicket) >= 0 && !independent, "different-key-second-submit");
    Wait(otherSecondStarted.get());
    (void)SetEvent(otherFirstRelease.get());
    SettleAndExpect(scheduler, "different-key-settled");
    Expect(scheduler.IsIdle(), "serial-tests-no-live-jobs");
    scheduler.Close();
}

struct ReentrantAction final : UiAction
{
    ReentrantAction(JobScheduler& owner, std::atomic<unsigned int>& actions, std::atomic<unsigned int>& nestedDrain,
                    std::atomic<unsigned int>& nestedAction) noexcept
        : scheduler(&owner), actionCount(&actions), nestedDrainCount(&nestedDrain), nestedActionResult(&nestedAction)
    {
    }
    JobScheduler* scheduler = nullptr;
    std::atomic<unsigned int>* actionCount = nullptr;
    std::atomic<unsigned int>* nestedDrainCount = nullptr;
    std::atomic<unsigned int>* nestedActionResult = nullptr;

    void Run() noexcept override
    {
        actionCount->fetch_add(1);
        nestedDrainCount->store(static_cast<unsigned int>(DrainCompletions(*scheduler)));
        nestedActionResult->store(RunUiActions(*scheduler) ? 1u : 0u);
    }
};

struct UiActionState final
{
    std::atomic<unsigned int> completed{0};
    std::atomic<unsigned int> actions{0};
    std::atomic<unsigned int> nestedDrain{99};
    std::atomic<unsigned int> nestedAction{99};
};

void MarkUiCompletion(void* context, JobResult) noexcept
{
    static_cast<UiActionState*>(context)->completed.fetch_add(1);
}

void TestUiActionsAndReentrancy()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1, {}, {StableMemory, nullptr}) >= 0, "ui-action-initialize");
    UiActionState state;
    std::unique_ptr<Job> job = std::make_unique<FunctionJob>(ReturnFinished, MarkUiCompletion, &state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {}, ticket) >= 0 && !job, "ui-action-completion-submit");
    WaitForScheduler(scheduler);
    std::unique_ptr<UiAction> first =
        std::make_unique<ReentrantAction>(scheduler, state.actions, state.nestedDrain, state.nestedAction);
    std::unique_ptr<UiAction> second =
        std::make_unique<ReentrantAction>(scheduler, state.actions, state.nestedDrain, state.nestedAction);
    Expect(scheduler.PostUiAction(first) && !first && scheduler.PostUiAction(second) && !second,
           "ui-action-enqueue-two");
    Expect(RunUiActions(scheduler), "ui-action-first-call-runs-one");
    Expect(state.actions.load() == 1 && state.nestedAction.load() == 0, "ui-action-one-per-call-reentrant-ignored");
    Expect(state.nestedDrain.load() == 0 && state.completed.load() == 0, "ui-action-reentrant-drain-ignored");
    Expect(RunUiActions(scheduler) && state.actions.load() == 2, "ui-action-second-call-runs-next");
    Expect(DrainCompletions(scheduler) == 1 && state.completed.load() == 1,
           "ui-action-completion-drained-after-callback");
    Expect(!RunUiActions(scheduler) && scheduler.IsIdle(), "ui-action-queue-empty-and-idle");
    for (std::size_t index = 0; index < JobScheduler::kUiActionCapacity; ++index)
    {
        std::unique_ptr<UiAction> action =
            std::make_unique<ReentrantAction>(scheduler, state.actions, state.nestedDrain, state.nestedAction);
        Expect(scheduler.PostUiAction(action) && !action, "ui-action-capacity-accepts-eight");
    }
    std::unique_ptr<UiAction> overflow =
        std::make_unique<ReentrantAction>(scheduler, state.actions, state.nestedDrain, state.nestedAction);
    const auto* address = overflow.get();
    Expect(!scheduler.PostUiAction(overflow) && overflow.get() == address, "ui-action-full-retains-caller-ownership");
    SettleAndExpect(scheduler, "ui-action-full-queue-settled");
    Expect(state.actions.load() == 2 + JobScheduler::kUiActionCapacity, "ui-action-every-accepted-action-ran-once");
    scheduler.Close();
}

struct CancelState final
{
    std::atomic<unsigned int> runs{0};
    unsigned int callbacks = 0;
    JobStatus status = JobStatus::Failed;
};

JobResult CountRun(JobContext&, void* context)
{
    static_cast<CancelState*>(context)->runs.fetch_add(1);
    return {};
}

void CancelledCompletion(void* context, JobResult result) noexcept
{
    auto& state = *static_cast<CancelState*>(context);
    ++state.callbacks;
    state.status = result.status;
}

void TestCancellationBeforeRunAndApply()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) == S_OK, "cancel-points-initialize");
    auto entered = Event();
    auto release = Event();
    CapacityState blockerState{entered.get(), release.get()};
    CapacityJobContext blockerContext{&blockerState, true};
    std::unique_ptr<Job> blocker = std::make_unique<FunctionJob>(RunCapacityJob, IgnoreCompletion, &blockerContext);
    JobTicket blockerTicket;
    Expect(scheduler.Submit(blocker, {}, blockerTicket) == S_OK, "cancel-points-blocker-submit");
    Wait(entered.get());

    CancelState beforeRun;
    std::unique_ptr<Job> queued = std::make_unique<FunctionJob>(CountRun, CancelledCompletion, &beforeRun);
    JobTicket queuedTicket;
    Expect(scheduler.Submit(queued, {}, queuedTicket) == S_OK && scheduler.Cancel(queuedTicket),
           "cancel-queued-accepted");
    (void)SetEvent(release.get());
    SettleAndExpect(scheduler, "cancel-queued-settled");
    Expect(beforeRun.runs.load() == 0 && beforeRun.callbacks == 1 && beforeRun.status == JobStatus::Cancelled,
           "cancel-queued-never-runs-and-completes-cancelled-once");
    scheduler.Close();

    JobScheduler completed;
    Expect(completed.Initialize(1, {}, {StableMemory, nullptr}) == S_OK, "cancel-final-initialize");
    CancelState beforeApply;
    std::unique_ptr<Job> done = std::make_unique<FunctionJob>(CountRun, CancelledCompletion, &beforeApply);
    JobTicket doneTicket;
    Expect(completed.Submit(done, {}, doneTicket) == S_OK, "cancel-final-submit");
    WaitForScheduler(completed); // no earlier wake or progress: final is published but undrained
    Expect(completed.Cancel(doneTicket), "cancel-final-before-drain");
    SettleAndExpect(completed, "cancel-final-settled");
    Expect(beforeApply.runs.load() == 1 && beforeApply.callbacks == 1 && beforeApply.status == JobStatus::Cancelled,
           "cancel-final-success-never-applies-after-cancel");
    completed.Close();
}

struct ShutdownState final
{
    HANDLE started = nullptr;
    std::atomic<unsigned int> returned{0};
    std::atomic<unsigned int> callbacks{0};
    std::atomic<unsigned int> destroyedAfterRun{0};
    std::atomic<unsigned int> destroyed{0};
};

class ShutdownJob final : public Job
{
  public:
    struct Payload final
    {
        explicit Payload(ShutdownState& value) noexcept : state(value) {}
        ~Payload()
        {
            state.destroyedAfterRun.store(state.returned.load());
            state.destroyed.fetch_add(1);
        }
        ShutdownState& state;
    };

    explicit ShutdownJob(ShutdownState& state) noexcept : _state(state), _payload(state) {}
    JobResult Run(JobContext& context) override
    {
        (void)SetEvent(_state.started);
        if (WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 10000) != WAIT_OBJECT_0)
        {
            std::printf("FAIL job.shutdown-event-timeout\n");
            std::fflush(stdout);
            std::_Exit(1);
        }
        _state.returned.store(1);
        return {};
    }
    void Completed(JobResult) noexcept override
    {
        _state.callbacks.fetch_add(1);
    }

  private:
    ShutdownState& _state;
    Payload _payload;
};

void TestShutdownAndLateSubmit()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) >= 0, "shutdown-initialize");
    auto started = Event();
    ShutdownState state{started.get()};
    std::unique_ptr<Job> job = std::make_unique<ShutdownJob>(state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {}, ticket) >= 0 && !job, "shutdown-submit");
    Wait(started.get());
    ShutdownState queuedState;
    std::unique_ptr<Job> queued = std::make_unique<ShutdownJob>(queuedState);
    JobTicket queuedTicket;
    Expect(scheduler.Submit(queued, {}, queuedTicket) == S_OK, "shutdown-queued-submit");
    scheduler.Close();
    Expect(state.returned.load() == 1 && state.callbacks.load() == 0 && state.destroyed.load() == 1 &&
               state.destroyedAfterRun.load() == 1,
           "shutdown-joins-before-destroy-and-drops-callback");
    Expect(queuedState.returned.load() == 0 && queuedState.callbacks.load() == 0 && queuedState.destroyed.load() == 1,
           "shutdown-queued-payload-destroyed-without-run-or-apply");
    scheduler.Close();
    Expect(state.destroyed.load() == 1 && state.callbacks.load() == 0, "shutdown-close-idempotent");
    std::unique_ptr<Job> late = std::make_unique<FunctionJob>(ReturnFinished, IgnoreCompletion, nullptr);
    Job* const lateAddress = late.get();
    JobTicket lateTicket;
    Expect(static_cast<HRESULT>(scheduler.Submit(late, {}, lateTicket)) == HRESULT_FROM_WIN32(ERROR_INVALID_STATE) &&
               late.get() == lateAddress,
           "shutdown-late-submit-refused-retains-ownership");
}

} // namespace

int RunJobTests(bool stress)
{
    g_failures = 0;
    TestMessagePumpOrdering(false);
    TestMessagePumpOrdering(true);
    TestProgressOrderingAndCoalescing();
    TestBadAllocTranslation();
    TestCapacityAndOwnership();
    TestStaleTicketCannotCancelReuse();
    TestSerialKeys();
    TestUiActionsAndReentrancy();
    TestCancellationBeforeRunAndApply();
    TestShutdownAndLateSubmit();
    if (stress)
    {
        // Repeat only the event-gated message/pump and generation checks; each iteration has fixed bounded jobs.
        for (unsigned int iteration = 0; iteration < 8; ++iteration)
        {
            TestMessagePumpOrdering((iteration & 1u) != 0);
        }
    }
    std::fflush(stdout);
    return g_failures;
}
