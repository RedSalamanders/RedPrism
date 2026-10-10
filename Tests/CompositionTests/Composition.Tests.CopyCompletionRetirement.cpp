#include "Composition.Tests.CopyCompletionRetirement.h"

#include "DeferredRelease.h"
#include "GpuWork.h"
#include "SharedPublicationMemory.h"

#include <Windows.h>
#include <array>
#include <cstdio>
#include <exception>
#include <memory>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace RedPrism::CompositionTests
{
namespace
{
constexpr std::uint64_t kCharge = 1024 * 1024;
constexpr DWORD kTimeoutMs = 5000;
unsigned failures = 0;
const char* scenario = nullptr;

void Expect(bool value, const char* name) noexcept
{
    std::printf("%s copy-completion-retirement.%s.%s\n", value ? "PASS" : "FAIL", scenario, name);
    failures += value ? 0u : 1u;
}

bool Memory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {8ull * 1024 * 1024 * 1024, 8ull * 1024 * 1024 * 1024};
    return true;
}

struct Device final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    bool Create() noexcept
    {
        constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        return SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                           &level, 1, D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
    }
};

struct Observation final
{
    const DWORD owner = GetCurrentThreadId();
    JobTicket original;
    std::weak_ptr<SharedPublicationMemory> weak;
    unsigned runs = 0;
    unsigned callbacks = 0;
    unsigned deliveries = 0;
    bool admittedCopy = false;
    bool exactOriginal = false;
    bool emptyRows = false;
    bool chargeHeldAtDelivery = false;
    bool retiredOffOwner = false;
    bool joinedBound = false;
    JobResult result;
    HRESULT delivered = S_OK;
};

#pragma warning(push)
#pragma warning(disable : 4324)
struct Payload final : DeferredReleaseNode
{
    explicit Payload(Observation& value) noexcept : observation(value)
    {
        bytes = kCharge;
        destroy = [](DeferredReleaseNode* node) noexcept { delete static_cast<Payload*>(node); };
    }
    ~Payload()
    {
        observation.retiredOffOwner = GetCurrentThreadId() != observation.owner;
    }
    Observation& observation;
    std::shared_ptr<SharedPublicationMemory> memory;
    wil::com_ptr_nothrow<ID3D11Texture2D> source;
    wil::com_ptr_nothrow<ID3D11Buffer> buffer;
};
#pragma warning(pop)

class Parent final : public Job,
                     public GpuReadbackReceiver,
                     public GpuBufferReadbackReceiver,
                     public GpuReadbackProducer
{
  public:
    Parent(JobScheduler& jobs, GpuWorkQueue& queue, ID3D11Texture2D* source, ID3D11Buffer* buffer,
           Observation& observation, bool failingProducer) noexcept
        : _jobs(jobs), _queue(queue), _source(source), _buffer(buffer), _observation(observation),
          _failingProducer(failingProducer)
    {
    }
    ~Parent() override
    {
        if (_payload && !TryDeferRelease(*_payload, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
    }
    JobResult Run(JobContext& context) override
    {
        ++_observation.runs;
        if (context.reservedMemoryBytes != kCharge)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (!_started)
        {
            auto payload = std::make_unique<Payload>(_observation);
            payload->memory = SharedPublicationMemory::CreateForWork();
            payload->source = std::move(_source);
            payload->buffer = std::move(_buffer);
            _payload = payload.release();
            return {};
        }
        return {JobStatus::Failed, _observation.delivered};
    }
    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        if (_cleanupReady)
        {
            delete _payload;
            _payload = nullptr;
        }
    }
    bool DeferCompletion(JobResult, JobTicket original) noexcept override
    {
        if (_started)
            return false;
        _started = true;
        _observation.original = original;
        _observation.weak = _payload->memory;
        const HRESULT result =
            _payload->buffer   ? _queue.EnqueueBufferReadback(_payload->buffer.get(), 4, 2, original, *this)
            : _failingProducer ? _queue.EnqueueProducedReadback(4, 2, original, *this, *this)
                               : _queue.EnqueueReadback(_payload->source.get(), {0, 0, 0, 4, 2, 1}, original, *this);
        _observation.admittedCopy = SUCCEEDED(result);
        if (FAILED(result))
            std::terminate(); // Setup failure is not simulated copy acknowledgement.
        return true;
    }
    HRESULT RecordReadbackSource(ID3D11Texture2D*& source, D3D11_BOX& box) noexcept override
    {
        source = nullptr;
        box = {};
        // Real original-context command before refusing a producer source. Its resource must remain owned.
        D3D11_BOX region{0, 0, 0, 4, 2, 1};
        constexpr std::array<std::uint8_t, 32> literal{1,   2,   3,  255, 4,   5,   6,  255, 7,   8,  9,
                                                       255, 10,  11, 12,  255, 13,  14, 15,  255, 16, 17,
                                                       18,  255, 19, 20,  21,  255, 22, 23,  24,  255};
        wil::com_ptr_nothrow<ID3D11Device> device;
        wil::com_ptr_nothrow<ID3D11DeviceContext> context;
        _payload->source->GetDevice(device.put());
        device->GetImmediateContext(context.put());
        context->UpdateSubresource(_payload->source.get(), 0, &region, literal.data(), 16, 0);
        return E_INVALIDARG;
    }
    void ReadbackReady(JobTicket original, GpuReadbackRows rows, HRESULT result) noexcept override
    {
        Ready(original, result, !rows.data && rows.width == 0 && rows.height == 0 && rows.lease.generation == 0);
    }
    void BufferReadbackReady(JobTicket original, GpuBufferReadbackValues values, HRESULT result) noexcept override
    {
        Ready(original, result,
              !values.data && values.count == 0 && values.width == 0 && values.height == 0 &&
                  values.lease.generation == 0);
    }
    void Ready(JobTicket original, HRESULT result, bool empty) noexcept
    {
        ++_observation.deliveries;
        _observation.exactOriginal = original == _observation.original;
        _observation.emptyRows = empty;
        _observation.chargeHeldAtDelivery = _jobs.ReservedMemoryBytes() == kCharge;
        _observation.delivered = result;
        // Drop this diagnostic weak control owner before the final charged worker stage destroys its graph.
        // Joined Close instead binds the original reservation through the weak owner until its explicit reset.
        _observation.weak.reset();
        _cleanupReady = true;
        if (!_jobs.Resume(original))
            std::terminate();
    }
    void RetainJoinedRetirement(JobScheduler& jobs, JobTicket original) noexcept override
    {
        if (_payload)
        {
            if (FAILED(_payload->memory->BindForRetirement(jobs, original)))
                std::terminate();
            _observation.joinedBound =
                _payload->memory->BoundTo(jobs, original) && _payload->memory->Bytes() == kCharge;
        }
    }
    void Completed(JobResult result) noexcept override
    {
        ++_observation.callbacks;
        _observation.result = result;
    }

  private:
    JobScheduler& _jobs;
    GpuWorkQueue& _queue;
    wil::com_ptr_nothrow<ID3D11Texture2D> _source;
    wil::com_ptr_nothrow<ID3D11Buffer> _buffer;
    Observation& _observation;
    Payload* _payload = nullptr;
    bool _failingProducer = false;
    bool _started = false;
    bool _cleanupReady = false;
};

bool WaitDormant(JobScheduler& jobs, GpuWorkQueue& queue, const Observation& observation) noexcept
{
    const auto deadline = GetTickCount64() + kTimeoutMs;
    for (;;)
    {
        (void)DrainCompletions(jobs);
        if (observation.admittedCopy && queue.InFlightCount() == 1 && !queue.HasReady())
            return true;
        const auto now = GetTickCount64();
        if (now < deadline && jobs.HasUiWork())
            continue;
        if (now >= deadline || !jobs.WakeHandle() ||
            WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()), static_cast<DWORD>(deadline - now)) !=
                WAIT_OBJECT_0)
            return false;
    }
}

enum class Action
{
    Recover,
    Replace,
    CancelReplace,
    Close,
    ProducerRefusal
};

void Run(Action action, bool forceEvent, const char* label, bool buffer = false)
{
    scenario = label;
    Device device, replacement;
    Expect(device.Create(), "actual-warp-device");
    if (!device.device)
        return;
    const bool replace = action == Action::Replace || action == Action::CancelReplace;
    if (replace)
        Expect(replacement.Create(), "actual-replacement-warp-device");
    JobScheduler jobs;
    GpuWorkQueue queue;
    Expect(SUCCEEDED(jobs.Initialize(1, {}, {Memory, nullptr})) && jobs.EnableDeferredRelease() &&
               SUCCEEDED(queue.Initialize(jobs)) &&
               SUCCEEDED(queue.Bind(device.device.get(), device.context.get(), forceEvent)),
           "actual-scheduler-queue-and-completion-route");
    if (forceEvent)
        Expect(queue.CompletionRoute() == GpuCapabilities::CompletionRoute::EnqueueSetEvent, "actual-event-fallback");
    constexpr std::array<std::uint8_t, 32> literal{1,  2,  3,  255, 4,  5,  6,  255, 7,  8,  9,  255, 10, 11, 12, 255,
                                                   13, 14, 15, 255, 16, 17, 18, 255, 19, 20, 21, 255, 22, 23, 24, 255};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 4;
    desc.Height = 2;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    const D3D11_SUBRESOURCE_DATA data{literal.data(), 16, 0};
    wil::com_ptr_nothrow<ID3D11Texture2D> source;
    Expect(SUCCEEDED(device.device->CreateTexture2D(&desc, &data, source.put())), "literal-source-created");
    wil::com_ptr_nothrow<ID3D11Buffer> bufferSource;
    if (buffer)
    {
        constexpr std::array<std::uint32_t, 8> values{1, 2, 3, 4, 5, 6, 7, 8};
        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.ByteWidth = 32;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = 4;
        const D3D11_SUBRESOURCE_DATA bufferData{values.data(), 0, 0};
        Expect(SUCCEEDED(device.device->CreateBuffer(&bufferDesc, &bufferData, bufferSource.put())),
               "literal-structured-buffer-created");
    }
    const bool producer = action == Action::ProducerRefusal;
    queue.SetCopyCompletionRefusalsForTest(E_FAIL, producer ? 0u : action == Action::Recover ? 1u : 2u);
    Observation observation;
    std::unique_ptr<Job> parent =
        std::make_unique<Parent>(jobs, queue, source.get(), bufferSource.get(), observation, producer);
    JobOptions options;
    options.memoryBytes = kCharge;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    JobTicket original;
    Expect(SUCCEEDED(jobs.Submit(parent, options, original)), "one-original-admission");
    source.reset(); // The original worker payload and then queue hold the real source.
    bufferSource.reset();
    if (replace || action == Action::Close)
    {
        Expect(WaitDormant(jobs, queue, observation), "two-refusals-reach-dormant-original-copy");
        Expect(queue.Statistics().copies == 1 && queue.Statistics().maps == 0 && queue.LentCount() == 0 &&
                   queue.InFlightCount() == 1 && queue.HasPending() && !queue.HasReady() &&
                   queue.StagingBytes() == 32 && jobs.ReservedMemoryBytes() == kCharge && observation.callbacks == 0 &&
                   observation.deliveries == 0 && !observation.weak.expired(),
               "healthy-quarantine-retains-real-source-staging-parent-and-charge");
        if (action == Action::CancelReplace)
        {
            Expect(jobs.Cancel(original), "real-original-cancel-accepted");
            Expect(observation.deliveries == 0 && jobs.ReservedMemoryBytes() == kCharge,
                   "cancel-does-not-acknowledge-copy-or-release-charge");
        }
        if (replace)
        {
            Expect(SUCCEEDED(queue.Bind(replacement.device.get(), replacement.context.get(), forceEvent)),
                   "actual-device-change-rearms-only-captured-retirement");
            Expect(queue.DeviceGeneration() == 2 && queue.InFlightCount() == 1 && queue.StagingBytes() == 32 &&
                       observation.deliveries == 0 && jobs.ReservedMemoryBytes() == kCharge,
                   "replacement-does-not-acknowledge-old-copy");
        }
    }
    if (action == Action::Close)
    {
        queue.StopNotifications();
        jobs.Close(
            {[](void* opaque) noexcept { static_cast<GpuWorkQueue*>(opaque)->CloseAfterWorkers(); }, nullptr, &queue});
        Expect(observation.joinedBound && observation.retiredOffOwner && observation.weak.expired() &&
                   !jobs.WakeHandle() && !queue.HasPending() && !queue.HasReady() && queue.InFlightCount() == 0,
               "joined-close-retires-original-graph-on-final-worker");
        Expect(observation.deliveries == 0 && observation.callbacks == 0 && observation.runs == 1,
               "joined-close-never-fabricates-copy-ack-or-useful-completion");
    }
    else
    {
        Expect(SettleJobs(jobs, kTimeoutMs) == SettleResult::Settled, "actual-original-driver-event-settles");
        Expect(observation.deliveries == 1 && observation.callbacks == 1 && observation.exactOriginal &&
                   observation.original == original && observation.emptyRows && observation.chargeHeldAtDelivery &&
                   observation.delivered == (producer ? E_INVALIDARG : E_FAIL),
               "actual-ack-delivers-original-failure-without-readable-rows");
        Expect(observation.retiredOffOwner && jobs.ReservedMemoryBytes() == 0 && queue.InFlightCount() == 0 &&
                   queue.LentCount() == 0 && !queue.HasPending() && queue.Statistics().maps == 0 &&
                   queue.Statistics().copies == (producer ? 0u : 1u),
               "worker-destroys-original-source-before-final-refund-without-recopy-or-map");
        Expect(observation.result.status ==
                       (action == Action::CancelReplace ? JobStatus::Cancelled : JobStatus::Failed) &&
                   observation.runs == (action == Action::CancelReplace ? 1u : 2u),
               "real-cancel-skips-useful-body-and-still-retires-worker-graph");
        queue.StopNotifications();
        jobs.Close();
        queue.CloseAfterWorkers();
    }
    observation.weak.reset();
}
} // namespace

int RunCopyCompletionRetirementTests()
{
    failures = 0;
    Run(Action::Recover, false, "selected-route");
    Run(Action::Recover, true, "event-route");
    Run(Action::Replace, false, "replace");
    Run(Action::CancelReplace, false, "cancel-replace");
    Run(Action::Close, false, "joined-close");
    Run(Action::ProducerRefusal, false, "producer-refusal");
    Run(Action::Recover, true, "buffer-event-route", true);
    Run(Action::CancelReplace, false, "buffer-cancel-replace", true);
    return failures ? 1 : 0;
}
} // namespace RedPrism::CompositionTests
