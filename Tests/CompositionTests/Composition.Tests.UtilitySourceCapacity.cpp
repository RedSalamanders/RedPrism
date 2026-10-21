#include "Composition.Tests.UtilitySourceCapacity.h"

#include "GpuCommandPump.h"
#include "OwnerWorkMultiplexer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kFailureTimeoutMs = 5000;
constexpr std::uint64_t kParentCharge = 1024 * 1024;
constexpr std::size_t kParentCount = GpuCommandPump::kCapacity + 1;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s utility-source-capacity.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    gFailures += condition ? 0 : 1;
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 4ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

template <typename Predicate> bool Until(JobScheduler& scheduler, Predicate ready) noexcept
{
    const std::uint64_t started = GetTickCount64();
    for (;;)
    {
        (void)DrainCompletions(scheduler);
        if (ready())
            return true;
        const std::uint64_t elapsed = GetTickCount64() - started;
        if (elapsed >= kFailureTimeoutMs || !scheduler.WakeHandle())
            return false;
        if (scheduler.HasUiWork())
            continue; // A rotated ready leaf needs the next owner pass; no additional event is promised.
        const DWORD result = WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()),
                                                 static_cast<DWORD>(kFailureTimeoutMs - elapsed));
        if (result != WAIT_OBJECT_0)
            return false;
    }
}

struct Warp final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;

    HRESULT Initialize() noexcept
    {
        constexpr D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &requested,
                                 1, D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    }
};

class SourceParent final : public GpuCommandSourceReceiver
{
  public:
    enum class Stage : std::uint8_t
    {
        Open,
        Record,
        AwaitDetach,
        Retire
    };

    SourceParent(JobScheduler& scheduler, GpuCommandPump& commands, ID3D11Device& device, ID3D11Device& probeDevice,
                 std::uint64_t generation, bool probeFailure) noexcept
        : _scheduler(scheduler), _commands(commands), _device(device), _probeDevice(probeDevice),
          _generation(generation), _probeFailure(probeFailure)
    {
    }

    JobResult Run(JobContext& context)
    {
        if (context.reservedMemoryBytes != kParentCharge)
        {
            _badTicketOrCharge = true;
            return {JobStatus::Failed, E_UNEXPECTED};
        }
        if (_stage != Stage::Record)
            return _stage == Stage::Retire && _terminal.status == JobStatus::Failed ? _terminal : JobResult{};

        _recordThread.store(GetCurrentThreadId(), std::memory_order_release);
        if (_probeFailure && !_probeGate)
            return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY)};
        if (_probeFailure && WaitForSingleObject(_probeGate.get(), kFailureTimeoutMs) != WAIT_OBJECT_0)
            return {JobStatus::Failed, HRESULT_FROM_WIN32(WAIT_TIMEOUT)};
        wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
        ID3D11Device& recordingDevice = _probeFailure ? _probeDevice : _device;
        HRESULT result = recordingDevice.CreateDeferredContext(0, deferred.put());
        if (SUCCEEDED(result))
            result = deferred->FinishCommandList(FALSE, _emptyTransport.put());
        if (SUCCEEDED(result) && _probeFailure)
            result = _stream.PublishForWork(std::move(_emptyTransport), context.cancellation);
        return FAILED(result) ? JobResult{JobStatus::Failed, result} : JobResult{};
    }

    void RetireWorkerStage(JobContext&, JobResult result) noexcept
    {
        if (_stage == Stage::Record)
        {
            _finishProducerThread.store(GetCurrentThreadId(), std::memory_order_release);
            _stream.FinishProducerForWork(result.status == JobStatus::Finished    ? S_OK
                                          : result.status == JobStatus::Cancelled ? E_ABORT
                                                                                  : result.error);
        }
        else if (_stage == Stage::Retire)
        {
            if (_probeFailure &&
                (!_retirementGate || WaitForSingleObject(_retirementGate.get(), kFailureTimeoutMs) != WAIT_OBJECT_0))
                std::terminate();
            _emptyTransport.reset();
            _retirementThread.store(GetCurrentThreadId(), std::memory_order_release);
            if (!_stream.RetireForWork())
                std::terminate();
            _detached = {};
            _retired.store(true, std::memory_order_release);
        }
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept
    {
        if (_ticket.generation == 0)
            _ticket = ticket;
        if (ticket != _ticket)
            _badTicketOrCharge = true;
        if (result.status != JobStatus::Finished)
            _terminal = result;

        if (_stage == Stage::Open)
        {
            if (!_commands.HasSourceSlotOwner())
            {
                _waitingForSource = true;
                _observedFullInOwnerCompletion = true;
                return true;
            }
            return AttachAndResume();
        }
        if (_stage == Stage::Record)
        {
            _producerJoined = true;
            _stage = Stage::AwaitDetach;
            return true;
        }
        return _stage == Stage::AwaitDetach;
    }

    void Completed(JobResult result) noexcept
    {
        _terminal = result;
        _complete.store(true, std::memory_order_release);
    }

    void CommandAcknowledgedOwner(JobTicket, GpuCommandLease, HRESULT) noexcept override
    {
        ++_acknowledgements;
    }

    void CommandSourceFailedOwner(JobTicket ticket, HRESULT result) noexcept override
    {
        ++_sourceFailures;
        _terminal = {JobStatus::Failed, result};
        _callbackSawSameTicket = ticket == _ticket;
        _callbackSawFailure = FAILED(result);
        _callbackSawNoFreeSourceSlot = !_commands.HasSourceSlotOwner();
    }

    bool AttachAndResume() noexcept
    {
        if (!_waitingForSource && _stage != Stage::Open)
            return false;
        if (!_streamInitialized)
        {
            const HRESULT initialized = _stream.InitializeOwner(_scheduler, _ticket, _generation);
            if (FAILED(initialized))
                return false;
            _streamInitialized = true;
        }
        if (!_commands.HasSourceSlotOwner() ||
            FAILED(_commands.AttachOwner(_stream, _ticket, *this, _source,
                                         _probeFailure ? GpuCommandFailureDisposition::RetainOriginalForRecovery
                                                       : GpuCommandFailureDisposition::CancelOriginal)))
            return false;
        _attached = true;
        _waitingForSource = false;
        _stage = Stage::Record;
        return _scheduler.Resume(_ticket, JobExecutionLane::GpuRecording);
    }

    bool DetachAndRetire() noexcept
    {
        if (!_producerJoined || !_attached || _stage != Stage::AwaitDetach)
            return false;
        if (!_commands.DetachOwner(_source, _detached) || !_stream.AuthorizeRetirementOwner())
            return false;
        _attached = false;
        _source = {};
        _stage = Stage::Retire;
        // Release the dedicated recorder for the next original source before holding/destructing this graph.
        return _scheduler.Resume(_ticket, JobExecutionLane::Compute);
    }

    bool AllowProbe() noexcept
    {
        return _probeGate && SetEvent(_probeGate.get()) != FALSE;
    }
    bool AllowRetirement() noexcept
    {
        return _retirementGate && SetEvent(_retirementGate.get()) != FALSE;
    }
    [[nodiscard]] JobResult Terminal() const noexcept
    {
        return _terminal;
    }

    [[nodiscard]] bool WaitingForSource() const noexcept
    {
        return _waitingForSource;
    }
    [[nodiscard]] bool ProducerJoined() const noexcept
    {
        return _producerJoined;
    }
    [[nodiscard]] bool Complete() const noexcept
    {
        return _complete.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool Retired() const noexcept
    {
        return _retired.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool BadTicketOrCharge() const noexcept
    {
        return _badTicketOrCharge;
    }
    [[nodiscard]] bool ObservedFullInOwnerCompletion() const noexcept
    {
        return _observedFullInOwnerCompletion;
    }
    [[nodiscard]] bool CallbackSawNoFreeSourceSlot() const noexcept
    {
        return _callbackSawNoFreeSourceSlot;
    }
    [[nodiscard]] bool CallbackSawSameTicketAndFailure() const noexcept
    {
        return _callbackSawSameTicket && _callbackSawFailure;
    }
    [[nodiscard]] unsigned int SourceFailures() const noexcept
    {
        return _sourceFailures;
    }
    [[nodiscard]] unsigned int Acknowledgements() const noexcept
    {
        return _acknowledgements;
    }
    [[nodiscard]] JobTicket Ticket() const noexcept
    {
        return _ticket;
    }
    [[nodiscard]] GpuCommandSourceLease Source() const noexcept
    {
        return _source;
    }
    [[nodiscard]] DWORD RecordThread() const noexcept
    {
        return _recordThread.load(std::memory_order_acquire);
    }
    [[nodiscard]] DWORD FinishProducerThread() const noexcept
    {
        return _finishProducerThread.load(std::memory_order_acquire);
    }
    [[nodiscard]] DWORD RetirementThread() const noexcept
    {
        return _retirementThread.load(std::memory_order_acquire);
    }
    void ShutdownOwner() noexcept
    {
        if (_attached)
        {
            if (!_commands.DetachOwner(_source, _detached))
                return;
            _attached = false;
            _source = {};
        }
        if (_streamInitialized && !_retired.load(std::memory_order_acquire))
        {
            if (!_stream.AuthorizeRetirementOwner())
                (void)_stream.AcknowledgeEmptyJoinedShutdownOwner();
            (void)_stream.AuthorizeRetirementOwner();
        }
    }

    void ShutdownWorker() noexcept
    {
        if (_streamInitialized && !_retired.load(std::memory_order_acquire))
        {
            _emptyTransport.reset();
            if (!_stream.RetireForWork())
                std::terminate();
            _detached = {};
            _retired.store(true, std::memory_order_release);
        }
    }

  private:
    JobScheduler& _scheduler;
    GpuCommandPump& _commands;
    ID3D11Device& _device;
    ID3D11Device& _probeDevice;
    const std::uint64_t _generation;
    const bool _probeFailure;
    GpuCommandStream _stream;
    GpuCommandSourceLease _source;
    GpuCompletionContext _detached;
    wil::com_ptr_nothrow<ID3D11CommandList> _emptyTransport;
    JobTicket _ticket;
    JobResult _terminal;
    Stage _stage = Stage::Open;
    wil::unique_handle _probeGate{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle _retirementGate{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<DWORD> _recordThread{0};
    std::atomic<DWORD> _finishProducerThread{0};
    std::atomic<DWORD> _retirementThread{0};
    std::atomic<bool> _retired{false};
    std::atomic<bool> _complete{false};
    bool _streamInitialized = false;
    bool _waitingForSource = false;
    bool _attached = false;
    bool _producerJoined = false;
    bool _badTicketOrCharge = false;
    bool _observedFullInOwnerCompletion = false;
    bool _callbackSawNoFreeSourceSlot = false;
    bool _callbackSawSameTicket = false;
    bool _callbackSawFailure = false;
    unsigned int _sourceFailures = 0;
    unsigned int _acknowledgements = 0;
};

// The fixture owns each logical source beyond wrapper destruction, including joined Close hooks.
// Scheduler wrappers forward the exact original stage/callbacks, and never own the observation lifetime.
class Recorder final : public Job
{
  public:
    explicit Recorder(SourceParent& parent) noexcept : _parent(parent) {}
    JobResult Run(JobContext& context) override
    {
        return _parent.Run(context);
    }
    void RetireWorkerStage(JobContext& context, JobResult result) noexcept override
    {
        _parent.RetireWorkerStage(context, result);
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        return _parent.DeferRetirement(result, ticket);
    }
    void Completed(JobResult result) noexcept override
    {
        _parent.Completed(result);
    }

  private:
    SourceParent& _parent;
};

class CapacityLeaf final : public OwnerWorkPump
{
  public:
    CapacityLeaf(GpuCommandPump& commands, SourceParent& waiting) noexcept : _commands(commands), _waiting(waiting) {}
    bool HasPending() const noexcept override
    {
        return !_pumped;
    }
    bool HasReady() const noexcept override
    {
        return !_pumped && _waiting.WaitingForSource() && _commands.HasSourceSlotOwner();
    }
    void Pump() noexcept override
    {
        ++_pumpCalls;
        if (!HasReady())
            return;
        _pumped = _waiting.AttachAndResume();
    }
    [[nodiscard]] unsigned int PumpCalls() const noexcept
    {
        return _pumpCalls;
    }
    [[nodiscard]] bool Pumped() const noexcept
    {
        return _pumped;
    }
    void Disable() noexcept
    {
        _pumped = true;
    }

  private:
    GpuCommandPump& _commands;
    SourceParent& _waiting;
    unsigned int _pumpCalls = 0;
    bool _pumped = false;
};

struct Shutdown final
{
    GpuCommandPump* commands = nullptr;
    OwnerWorkMultiplexer* routes = nullptr;
    CapacityLeaf* leaf = nullptr;
    std::array<SourceParent*, kParentCount> parents{};
};

void RunCapacityFixture() noexcept
{
    Warp warp;
    Warp foreignWarp;
    const HRESULT deviceResult = warp.Initialize();
    const HRESULT foreignDeviceResult = foreignWarp.Initialize();
    Expect(SUCCEEDED(deviceResult) && warp.device && warp.immediate && SUCCEEDED(foreignDeviceResult) &&
               foreignWarp.device,
           "real-warp-device-immediate-context-and-separate-preexecute-probe-device");
    if (FAILED(deviceResult) || !warp.device || !warp.immediate || FAILED(foreignDeviceResult) || !foreignWarp.device)
        return;

    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    if (FAILED(scheduler.Initialize(1, {}, {StableMemory, nullptr})) || FAILED(scheduler.EnableGpuRecordingLane()) ||
        !scheduler.RegisterOwnerWork(routes) || FAILED(queue.Initialize(scheduler, &routes)) ||
        FAILED(queue.Bind(warp.device.get(), warp.immediate.get())) ||
        FAILED(commands.InitializeOwner(scheduler, queue, routes)))
    {
        Expect(false, "real-scheduler-recording-lane-and-bound-warp-queue-initialize");
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        return;
    }
    Expect(true, "real-scheduler-recording-lane-and-bound-warp-queue-initialize");

    std::array<std::unique_ptr<SourceParent>, kParentCount> owned;
    std::array<SourceParent*, kParentCount> parents{};
    for (std::size_t index = 0; index < kParentCount; ++index)
    {
        owned[index] = std::make_unique<SourceParent>(scheduler, commands, *warp.device, *foreignWarp.device,
                                                      queue.DeviceGeneration(), index == 0);
        parents[index] = owned[index].get();
    }
    CapacityLeaf leaf(commands, *parents[2]);
    Shutdown shutdown{&commands, &routes, &leaf, parents};
    const auto close = wil::scope_exit(
        [&]() noexcept
        {
            for (auto* parent : parents)
            {
                (void)parent->AllowProbe();
                (void)parent->AllowRetirement();
            }
            commands.StopOwner();
            queue.StopNotifications();
            scheduler.Close({[](void* opaque) noexcept
                             {
                                 auto& state = *static_cast<Shutdown*>(opaque);
                                 state.commands->AcknowledgeJoinedShutdownOwner();
                                 for (auto* parent : state.parents)
                                     parent->ShutdownOwner();
                                 state.leaf->Disable();
                                 (void)state.routes->Detach(*state.leaf);
                                 (void)state.commands->CloseOwner();
                             },
                             [](void* opaque) noexcept
                             {
                                 auto& state = *static_cast<Shutdown*>(opaque);
                                 for (auto* parent : state.parents)
                                     parent->ShutdownWorker();
                             },
                             &shutdown});
            queue.CloseAfterWorkers();
        });

    Expect(routes.Attach(leaf), "one-stable-capacity-readiness-leaf-attached");
    std::array<JobTicket, kParentCount> tickets{};
    for (std::size_t index = 0; index < GpuCommandPump::kCapacity; ++index)
    {
        std::unique_ptr<Job> job = std::make_unique<Recorder>(*parents[index]);
        const HRESULT submitted = scheduler.Submit(job,
                                                   {.kind = JobClass::Commit,
                                                    .serialKey = 0xCA00 + index,
                                                    .memoryBytes = kParentCharge,
                                                    .deferredRetirement = true,
                                                    .executionLane = JobExecutionLane::Compute},
                                                   tickets[index]);
        Expect(SUCCEEDED(submitted) && !job, "source-parent-admitted-once");
        if (FAILED(submitted))
            return;
    }
    const bool bothAttached =
        Until(scheduler, [&]() { return commands.AttachedSourceCountOwnerForTest() == GpuCommandPump::kCapacity; });
    Expect(bothAttached && !commands.HasSourceSlotOwner(), "first-two-original-parents-fill-both-source-slots");
    Expect(parents[0]->AllowProbe(), "release-event-gates-preexecute-probe-until-both-slots-are-full");
    const bool firstPairJoined = Until(
        scheduler, [&]()
        { return parents[0]->ProducerJoined() && parents[1]->ProducerJoined() && parents[0]->SourceFailures() == 1; });
    Expect(firstPairJoined, "both-recording-workers-join-while-original-parents-remain-suspended");
    Expect(firstPairJoined && parents[0]->CallbackSawNoFreeSourceSlot() &&
               parents[0]->CallbackSawSameTicketAndFailure() && parents[0]->Acknowledgements() == 0,
           "actual-preexecute-command-failure-callback-observes-no-free-slot-and-retains-original-ticket");

    std::unique_ptr<Job> third = std::make_unique<Recorder>(*parents[2]);
    const HRESULT thirdSubmitted = scheduler.Submit(third,
                                                    {.kind = JobClass::Commit,
                                                     .serialKey = 0xCA02,
                                                     .memoryBytes = kParentCharge,
                                                     .deferredRetirement = true,
                                                     .executionLane = JobExecutionLane::Compute},
                                                    tickets[2]);
    Expect(SUCCEEDED(thirdSubmitted) && !third, "third-original-parent-admitted-once");
    if (FAILED(thirdSubmitted))
        return;
    const bool twoJoined = Until(
        scheduler, [&]()
        { return parents[0]->ProducerJoined() && parents[1]->ProducerJoined() && parents[2]->WaitingForSource(); });
    Expect(twoJoined, "two-original-recorders-joined-third-original-ticket-suspended");
    Expect(parents[0]->RecordThread() != 0 && parents[0]->RecordThread() != GetCurrentThreadId() &&
               parents[1]->RecordThread() != 0 && parents[1]->RecordThread() != GetCurrentThreadId() &&
               parents[0]->FinishProducerThread() == parents[0]->RecordThread() &&
               parents[1]->FinishProducerThread() == parents[1]->RecordThread(),
           "finish-producer-runs-on-each-actual-recording-worker");
    Expect(commands.AttachedSourceCountOwnerForTest() == 2 && !commands.HasSourceSlotOwner() && !leaf.HasReady() &&
               leaf.PumpCalls() == 0 && parents[2]->RecordThread() == 0,
           "both-source-slots-full-third-dormant-without-recording-or-readiness-pump");
    Expect(scheduler.ReservedMemoryBytes() == kParentCount * kParentCharge && parents[0]->Ticket() == tickets[0] &&
               parents[1]->Ticket() == tickets[1] && parents[2]->Ticket() == tickets[2],
           "three-original-tickets-retain-their-whole-admission-charge");

    bool offOwnerHasSlot = true;
    std::thread observer([&]() noexcept { offOwnerHasSlot = commands.HasSourceSlotOwner(); });
    observer.join();
    Expect(!offOwnerHasSlot, "source-slot-query-refuses-off-owner-thread");

    // This query is made from the real scheduler's owner completion callback while both slots are occupied.
    Expect(parents[2]->ObservedFullInOwnerCompletion(),
           "source-slot-query-false-inside-full-owner-completion-callback");
    Expect(ResetEvent(static_cast<HANDLE>(scheduler.WakeHandle())) != FALSE,
           "clear-quiescent-owner-wake-before-foreign-lease");
    GpuCommandSourceLease foreign = parents[0]->Source();
    ++foreign.generation;
    GpuCompletionContext rejected;
    const bool foreignRejected = !commands.DetachOwner(foreign, rejected) && !rejected;
    Expect(foreignRejected && !commands.HasSourceSlotOwner() && !leaf.HasReady() && leaf.PumpCalls() == 0 &&
               WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), 0) == WAIT_TIMEOUT,
           "foreign-lease-rejection-does-not-wake-or-release-capacity");

    const bool firstDetached = parents[0]->DetachAndRetire();
    Expect(firstDetached && commands.HasSourceSlotOwner(), "actual-joined-detach-opens-one-owner-capacity-slot");
    const bool thirdAttached = Until(scheduler, [&]() { return leaf.Pumped() && parents[2]->ProducerJoined(); });
    std::printf("INFO utility-source-capacity.transition attached=%u pumped=%u joined=%u calls=%u "
                "record=%lu finish=%lu reserved=%llu sources=%zu\n",
                thirdAttached ? 1u : 0u, leaf.Pumped() ? 1u : 0u, parents[2]->ProducerJoined() ? 1u : 0u,
                leaf.PumpCalls(), parents[2]->RecordThread(), parents[2]->FinishProducerThread(),
                static_cast<unsigned long long>(scheduler.ReservedMemoryBytes()),
                commands.AttachedSourceCountOwnerForTest());
    Expect(thirdAttached && parents[2]->Ticket() == tickets[2] &&
               scheduler.ReservedMemoryBytes() == kParentCount * kParentCharge &&
               commands.AttachedSourceCountOwnerForTest() == 2,
           "single-detach-notification-attaches-and-resumes-third-on-same-ticket-and-charge");
    Expect(parents[2]->RecordThread() != 0 && parents[2]->FinishProducerThread() == parents[2]->RecordThread() &&
               parents[2]->RecordThread() != GetCurrentThreadId() && leaf.PumpCalls() == 1,
           "third-recording-starts-only-after-owner-readiness-transition");

    Expect(parents[0]->AllowRetirement(), "release-first-worker-retirement-only-after-original-third-recorder-joins");
    const bool allJoined = Until(scheduler, [&]() { return parents[0]->Complete(); });
    Expect(allJoined, "third-producer-joins-and-first-original-retires");
    const bool secondDetached = parents[1]->DetachAndRetire();
    Expect(secondDetached, "second-joined-parent-detaches-without-gpu-use");
    const bool thirdDetached =
        Until(scheduler, [&]() { return commands.HasSourceSlotOwner() && parents[2]->ProducerJoined(); }) &&
        parents[2]->DetachAndRetire();
    Expect(thirdDetached, "third-joined-parent-detaches-after-its-own-recording-worker-joins");
    const bool allComplete =
        Until(scheduler, [&]() { return parents[0]->Complete() && parents[1]->Complete() && parents[2]->Complete(); });
    Expect(allComplete, "all-three-original-parents-complete-after-worker-retirement");
    Expect(parents[0]->Terminal().status == JobStatus::Failed && parents[0]->Terminal().error == E_INVALIDARG &&
               parents[1]->Terminal().status == JobStatus::Finished &&
               parents[2]->Terminal().status == JobStatus::Finished,
           "preexecution-source-failure-stays-failed-and-grants-no-useful-recovery");
    Expect(parents[0]->Retired() && parents[1]->Retired() && parents[2]->Retired() &&
               parents[0]->RetirementThread() != 0 && parents[0]->RetirementThread() != parents[2]->RecordThread() &&
               parents[0]->RetirementThread() != GetCurrentThreadId() &&
               parents[1]->RetirementThread() != GetCurrentThreadId() &&
               parents[2]->RetirementThread() != GetCurrentThreadId() && scheduler.ReservedMemoryBytes() == 0 &&
               scheduler.IsIdle(),
           "captured-contexts-and-empty-transport-release-on-original-retirement-workers-before-refund");
    Expect(!parents[0]->BadTicketOrCharge() && !parents[1]->BadTicketOrCharge() && !parents[2]->BadTicketOrCharge() &&
               parents[0]->Acknowledgements() == 0 && parents[1]->Acknowledgements() == 0 &&
               parents[2]->Acknowledgements() == 0,
           "same-original-records-finish-without-gpu-acknowledgement-or-fresh-admission");
    Expect(parents[0]->SourceFailures() == 1 && parents[1]->SourceFailures() == 0 && parents[2]->SourceFailures() == 0,
           "only-the-explicit-preexecute-probe-reports-a-source-failure");
}
} // namespace

int RunUtilitySourceCapacityTests()
{
    gFailures = 0;
    static_assert(GpuCommandPump::kCapacity == 2);
    const volatile std::size_t observedCapacity = GpuCommandPump::kCapacity;
    Expect(observedCapacity == 2, "literal-source-capacity-is-two");
    RunCapacityFixture();
    return gFailures;
}
