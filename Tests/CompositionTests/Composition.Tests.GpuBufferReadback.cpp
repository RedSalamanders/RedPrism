#include "Composition.Tests.GpuBufferReadback.h"

#include "BrushCompute.h"
#include "GpuWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s gpu-buffer-readback.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void ExpectStroke(bool condition, const char* scenario, const char* control) noexcept
{
    std::printf("%s gpu-buffer-readback.deferred.%s.%s\n", condition ? "PASS" : "FAIL", scenario, control);
    gFailures += condition ? 0 : 1;
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

struct Device final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;

    HRESULT Create() noexcept
    {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    }
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class Rig final
{
  public:
    ~Rig()
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
    }

    bool Initialize(bool forceEvent = true) noexcept
    {
        const HRESULT deviceResult = device.Create();
        Expect(SUCCEEDED(deviceResult), "setup.warp-device");
        if (FAILED(deviceResult))
            return false;

        const std::int32_t schedulerResult = scheduler.Initialize(1, {}, {StableMemory, nullptr});
        Expect(schedulerResult == S_OK, "setup.one-bounded-worker-stable-memory");
        if (schedulerResult != S_OK)
            return false;

        const HRESULT queueResult = queue.Initialize(scheduler);
        Expect(SUCCEEDED(queueResult), "setup.queue-initialize");
        if (FAILED(queueResult))
            return false;

        const HRESULT bindResult = queue.Bind(device.device.get(), device.context.get(), forceEvent);
        Expect(SUCCEEDED(bindResult), "setup.bind-own-warp-device");
        if (FAILED(bindResult))
            return false;
        if (forceEvent)
            Expect(queue.CompletionRoute() == GpuCapabilities::CompletionRoute::EnqueueSetEvent,
                   "setup.actual-enqueue-set-event-route");
        return true;
    }

    Device device;
    JobScheduler scheduler;
    GpuWorkQueue queue;
};
#pragma warning(pop)

constexpr std::array<std::uint32_t, 6> kExpectedValues{
    0u, 1u, 255u, 256u, 65535u, UINT32_MAX,
};
constexpr std::array<std::uint8_t, 6> kExpectedClamped{
    0u, 1u, 255u, 255u, 255u, 255u,
};
constexpr unsigned int kWidth = 3;
constexpr unsigned int kHeight = 2;
constexpr std::size_t kCount = static_cast<std::size_t>(kWidth) * kHeight;
constexpr std::uint64_t kJobMemoryBytes = kCount * (sizeof(std::uint32_t) + sizeof(std::uint8_t));

wil::com_ptr_nothrow<ID3D11Buffer> MakeBuffer(Device& device, UINT byteWidth, UINT stride, UINT miscFlags,
                                              const std::uint32_t* values) noexcept
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = byteWidth;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = miscFlags;
    desc.StructureByteStride = stride;
    const D3D11_SUBRESOURCE_DATA data{values, 0, 0};
    wil::com_ptr_nothrow<ID3D11Buffer> buffer;
    if (FAILED(device.device->CreateBuffer(&desc, values ? &data : nullptr, buffer.put())))
        return {};
    return buffer;
}

wil::com_ptr_nothrow<ID3D11Buffer> MakeValidBuffer(Device& device) noexcept
{
    return MakeBuffer(device, static_cast<UINT>(kCount * sizeof(std::uint32_t)), sizeof(std::uint32_t),
                      D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, kExpectedValues.data());
}

constexpr std::array<std::uint32_t, 6> kMixedTexturePixels{
    0x10203040u, 0x55667788u, 0x90ABCDEFu, 0x01020304u, 0x7F80FEFFu, 0xDEADBEEFu,
};

wil::com_ptr_nothrow<ID3D11Texture2D> MakeMixedTexture(Device& device) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = 0;
    const D3D11_SUBRESOURCE_DATA data{kMixedTexturePixels.data(), kWidth * sizeof(std::uint32_t), 0};
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    if (FAILED(device.device->CreateTexture2D(&desc, &data, texture.put())))
        return {};
    return texture;
}

struct ReadbackState final
{
    unsigned int runs = 0;
    unsigned int deliveries = 0;
    unsigned int callbacks = 0;
    HRESULT enqueueResult = E_PENDING;
    HRESULT callbackResult = E_PENDING;
    HRESULT bindResult = E_PENDING;
    JobStatus status = JobStatus::Failed;
    JobTicket submittedTicket{};
    GpuBufferReadbackValues values{};
    GpuReadbackLease staleLease{};
    bool hasStaleLease = false;
    bool ticketMatches = false;
    bool exactDimensionsAndCount = false;
    bool ownerSawExactValues = false;
    bool workerThread = false;
    bool workerSawExactValues = false;
    bool workerClampedExact = false;
    bool resumeAccepted = false;
    bool cancelWhileLent = false;
    bool cancelAccepted = false;
    bool oldMappingRetainedOnRebind = false;
    bool staleReleaseRejected = false;
    bool releaseAccepted = false;
    bool duplicateReleaseRejected = false;
    std::uint64_t bytesAtDelivery = 0;
    std::uint64_t bytesAfterRebind = 0;
    std::uint64_t bytesAfterRelease = 0;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class BufferReadbackJob final : public Job, public GpuBufferReadbackReceiver
{
  public:
    BufferReadbackJob(JobScheduler& scheduler, GpuWorkQueue& queue, ID3D11Buffer* source, ReadbackState& state,
                      DWORD ownerThread, Device* replacement = nullptr) noexcept
        : _scheduler(scheduler), _queue(queue), _source(source), _state(state), _ownerThread(ownerThread),
          _replacement(replacement)
    {
    }

    JobResult Run(JobContext&) override
    {
        ++_state.runs;
        if (!_submitted)
            return {};
        if (FAILED(_state.enqueueResult))
            return {JobStatus::Failed, _state.enqueueResult};
        if (!_state.values.data || _state.values.count != kCount)
            return {JobStatus::Failed, E_UNEXPECTED};

        _state.workerThread = GetCurrentThreadId() != _ownerThread;
        _state.workerSawExactValues = true;
        _state.workerClampedExact = true;
        for (std::size_t i = 0; i < kCount; ++i)
        {
            const std::uint32_t value = _state.values.data[i];
            _state.workerSawExactValues = _state.workerSawExactValues && value == kExpectedValues[i];
            _converted[i] = static_cast<std::uint8_t>((std::min)(value, 255u));
            _state.workerClampedExact = _state.workerClampedExact && _converted[i] == kExpectedClamped[i];
        }
        return {};
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_submitted)
            return false;
        _submitted = true;
        _state.enqueueResult = _queue.EnqueueBufferReadback(_source.get(), kWidth, kHeight, ticket, *this);
        if (FAILED(_state.enqueueResult))
            _state.resumeAccepted = _scheduler.Resume(ticket);
        return true;
    }

    void BufferReadbackReady(JobTicket ticket, GpuBufferReadbackValues values, HRESULT result) noexcept override
    {
        ++_state.deliveries;
        _state.ticketMatches = ticket == _state.submittedTicket;
        _state.callbackResult = result;
        _state.values = values;
        _state.bytesAtDelivery = _queue.StagingBytes();
        _state.exactDimensionsAndCount = SUCCEEDED(result) && values.data && values.width == kWidth &&
                                         values.height == kHeight && values.count == kCount;
        _state.ownerSawExactValues = _state.exactDimensionsAndCount;
        if (_state.ownerSawExactValues)
            for (std::size_t i = 0; i < kCount; ++i)
                _state.ownerSawExactValues = _state.ownerSawExactValues && values.data[i] == kExpectedValues[i];

        if (_state.hasStaleLease)
            _state.staleReleaseRejected = !_queue.ReleaseMapped(_state.staleLease);
        if (_replacement)
        {
            _state.bindResult = _queue.Bind(_replacement->device.get(), _replacement->context.get());
            _state.bytesAfterRebind = _queue.StagingBytes();
            _state.oldMappingRetainedOnRebind = SUCCEEDED(_state.bindResult) && values.data &&
                                                _state.bytesAfterRebind == _state.bytesAtDelivery &&
                                                _state.bytesAfterRebind != 0;
        }
        if (_state.cancelWhileLent)
            _state.cancelAccepted = _scheduler.Cancel(ticket);
        _state.resumeAccepted = _scheduler.Resume(ticket);
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        if (_state.values.data)
        {
            if (_state.values.deviceGeneration != _queue.DeviceGeneration() && result.status == JobStatus::Finished)
                result = {JobStatus::Cancelled};
            _state.releaseAccepted = _queue.ReleaseMapped(_state.values.lease);
            _state.duplicateReleaseRejected = !_queue.ReleaseMapped(_state.values.lease);
            _state.bytesAfterRelease = _queue.StagingBytes();
        }
        _state.status = result.status;
    }

  private:
    JobScheduler& _scheduler;
    GpuWorkQueue& _queue;
    wil::com_ptr_nothrow<ID3D11Buffer> _source;
    ReadbackState& _state;
    DWORD _ownerThread;
    Device* _replacement;
    std::array<std::uint8_t, kCount> _converted{};
    bool _submitted = false;
};
#pragma warning(pop)

bool SubmitAndSettle(Rig& rig, ID3D11Buffer* source, ReadbackState& state, Device* replacement = nullptr)
{
    std::unique_ptr<Job> job =
        std::make_unique<BufferReadbackJob>(rig.scheduler, rig.queue, source, state, GetCurrentThreadId(), replacement);
    const HRESULT submit =
        static_cast<HRESULT>(rig.scheduler.Submit(job, {JobClass::Commit, 1, kJobMemoryBytes}, state.submittedTicket));
    Expect(submit == S_OK, "job.submit-bounded-reservation");
    if (submit != S_OK)
        return false;
    const SettleResult settled = SettleJobs(rig.scheduler, 10000);
    Expect(settled == SettleResult::Settled, "job.event-pumped-settle-bounded");
    if (settled != SettleResult::Settled)
    {
        std::fflush(stdout);
        std::_Exit(1); // The admitted job still borrows this case's stack state; never unwind it early.
    }
    return true;
}

bool TestValuesEventRouteAndStaleLease(Rig& rig, ID3D11Buffer* source)
{
    rig.queue.SetMapBusyForTest(1);
    ReadbackState first;
    if (!SubmitAndSettle(rig, source, first))
        return false;
    Expect(first.deliveries == 1 && first.callbacks == 1 && first.ticketMatches && first.callbackResult == S_OK,
           "values.single-successful-ticketed-delivery");
    Expect(first.exactDimensionsAndCount && first.ownerSawExactValues, "values.literal-uint32-payload-and-3x2-shape");
    Expect(first.runs == 2 && first.workerThread && first.workerSawExactValues && first.workerClampedExact,
           "values.worker-only-uint8-clamp-0-1-255-256-65535-uint32-max");
    Expect(first.resumeAccepted && first.status == JobStatus::Finished, "values.resume-and-finish");
    Expect(first.releaseAccepted && first.duplicateReleaseRejected && first.bytesAtDelivery == kCount * 4 &&
               first.bytesAfterRelease == first.bytesAtDelivery && rig.queue.LentCount() == 0 &&
               rig.queue.InFlightCount() == 0 && rig.scheduler.ReservedMemoryBytes() == 0,
           "values-exact-lease-release-duplicate-refused-and-reusable-staging-retained");
    const GpuReadbackStatistics stats = rig.queue.Statistics();
    Expect(stats.copies == 1 && stats.readinessBarriers == 1 && stats.maps == 2 && rig.queue.MapAttemptsForTest() == 2,
           "event-route.one-real-enqueue-set-event-barrier-after-injected-busy-map");

    ReadbackState second;
    second.hasStaleLease = true;
    second.staleLease = first.values.lease;
    if (!SubmitAndSettle(rig, source, second))
        return false;
    Expect(second.deliveries == 1 && second.callbacks == 1 && second.staleReleaseRejected,
           "lease.stale-generation-cannot-release-reused-slot");
    Expect(second.values.lease.slot == first.values.lease.slot &&
               second.values.lease.generation != first.values.lease.generation && second.releaseAccepted &&
               second.duplicateReleaseRejected && rig.queue.LentCount() == 0,
           "lease.reused-slot-has-new-generation-and-releases-once");
    return true;
}

struct MixedReadbackState final
{
    JobTicket submittedTicket{};
    HRESULT enqueueResult = E_PENDING;
    HRESULT callbackResult = E_PENDING;
    JobStatus status = JobStatus::Failed;
    GpuReadbackLease lease{};
    std::uint64_t bytesAtDelivery = 0;
    std::uint64_t bytesAfterRelease = 0;
    unsigned int deliveries = 0;
    unsigned int runs = 0;
    unsigned int callbacks = 0;
    bool ticketMatches = false;
    bool ownerExact = false;
    bool workerExact = false;
    bool workerThread = false;
    bool resumeAccepted = false;
    bool releaseAccepted = false;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class MixedReadbackJob final : public Job, public GpuBufferReadbackReceiver, public GpuReadbackReceiver
{
  public:
    MixedReadbackJob(JobScheduler& scheduler, GpuWorkQueue& queue, ID3D11Buffer* source, const std::uint32_t* expected,
                     MixedReadbackState& state) noexcept
        : _scheduler(scheduler), _queue(queue), _buffer(source), _expected(expected), _state(state),
          _ownerThread(GetCurrentThreadId()), _bufferReadback(true)
    {
    }

    MixedReadbackJob(JobScheduler& scheduler, GpuWorkQueue& queue, ID3D11Texture2D* source,
                     const std::uint32_t* expected, MixedReadbackState& state) noexcept
        : _scheduler(scheduler), _queue(queue), _texture(source), _expected(expected), _state(state),
          _ownerThread(GetCurrentThreadId())
    {
        _box = {0, 0, 0, kWidth, kHeight, 1};
    }

    JobResult Run(JobContext&) override
    {
        ++_state.runs;
        if (!_submitted)
            return {};
        if (FAILED(_state.callbackResult) || !_state.ownerExact)
            return {JobStatus::Failed, FAILED(_state.callbackResult) ? _state.callbackResult : E_UNEXPECTED};
        _state.workerThread = GetCurrentThreadId() != _ownerThread;
        _state.workerExact = true;
        if (_bufferReadback)
        {
            if (!_bufferValues.data || _bufferValues.count != kCount)
                return {JobStatus::Failed, E_UNEXPECTED};
            for (std::size_t i = 0; i < kCount; ++i)
                _state.workerExact = _state.workerExact && _bufferValues.data[i] == _expected[i];
        }
        else
        {
            if (!_rows.data || _rows.width != kWidth || _rows.height != kHeight ||
                _rows.rowPitch < kWidth * sizeof(std::uint32_t))
                return {JobStatus::Failed, E_UNEXPECTED};
            for (unsigned int y = 0; y < kHeight; ++y)
            {
                const auto* row = _rows.data + static_cast<std::size_t>(y) * _rows.rowPitch;
                _state.workerExact =
                    _state.workerExact && std::memcmp(row, _expected + static_cast<std::size_t>(y) * kWidth,
                                                      kWidth * sizeof(std::uint32_t)) == 0;
            }
        }
        return _state.workerExact ? JobResult{} : JobResult{JobStatus::Failed, E_UNEXPECTED};
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_submitted)
            return false;
        _submitted = true;
        _state.submittedTicket = ticket;
        _state.enqueueResult = _bufferReadback
                                   ? _queue.EnqueueBufferReadback(_buffer.get(), kWidth, kHeight, ticket, *this)
                                   : _queue.EnqueueReadback(_texture.get(), _box, ticket, *this);
        if (FAILED(_state.enqueueResult))
            _state.resumeAccepted = _scheduler.Resume(ticket);
        return true;
    }

    void BufferReadbackReady(JobTicket ticket, GpuBufferReadbackValues values, HRESULT result) noexcept override
    {
        _bufferValues = values;
        _state.lease = values.lease;
        _state.ownerExact = SUCCEEDED(result) && values.data && values.width == kWidth && values.height == kHeight &&
                            values.count == kCount;
        if (_state.ownerExact)
            for (std::size_t i = 0; i < kCount; ++i)
                _state.ownerExact = _state.ownerExact && values.data[i] == _expected[i];
        OnDelivery(ticket, result);
    }

    void ReadbackReady(JobTicket ticket, GpuReadbackRows rows, HRESULT result) noexcept override
    {
        _rows = rows;
        _state.lease = rows.lease;
        _state.ownerExact = SUCCEEDED(result) && rows.data && rows.width == kWidth && rows.height == kHeight &&
                            rows.rowPitch >= kWidth * sizeof(std::uint32_t);
        if (_state.ownerExact)
            for (unsigned int y = 0; y < kHeight; ++y)
            {
                const auto* row = rows.data + static_cast<std::size_t>(y) * rows.rowPitch;
                _state.ownerExact =
                    _state.ownerExact && std::memcmp(row, _expected + static_cast<std::size_t>(y) * kWidth,
                                                     kWidth * sizeof(std::uint32_t)) == 0;
            }
        OnDelivery(ticket, result);
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        _state.releaseAccepted = _queue.ReleaseMapped(_state.lease);
        _state.bytesAfterRelease = _queue.StagingBytes();
        _state.status = result.status;
    }

  private:
    void OnDelivery(JobTicket ticket, HRESULT result) noexcept
    {
        ++_state.deliveries;
        _state.ticketMatches = ticket == _state.submittedTicket;
        _state.callbackResult = result;
        _state.bytesAtDelivery = _queue.StagingBytes();
        _state.resumeAccepted = _scheduler.Resume(ticket);
    }

    JobScheduler& _scheduler;
    GpuWorkQueue& _queue;
    wil::com_ptr_nothrow<ID3D11Buffer> _buffer;
    wil::com_ptr_nothrow<ID3D11Texture2D> _texture;
    const std::uint32_t* _expected;
    MixedReadbackState& _state;
    DWORD _ownerThread;
    D3D11_BOX _box{};
    GpuBufferReadbackValues _bufferValues{};
    GpuReadbackRows _rows{};
    bool _bufferReadback = false;
    bool _submitted = false;
};
#pragma warning(pop)

bool SubmitAndSettleMixed(Rig& rig, std::unique_ptr<Job> job, MixedReadbackState& state)
{
    const HRESULT submit = static_cast<HRESULT>(
        rig.scheduler.Submit(job, {JobClass::Commit, 1, kCount * sizeof(std::uint32_t)}, state.submittedTicket));
    Expect(submit == S_OK, "mixed.submit-bounded-reservation");
    if (submit != S_OK)
        return false;
    const SettleResult settled = SettleJobs(rig.scheduler, 10000);
    Expect(settled == SettleResult::Settled, "mixed.event-pumped-settle-bounded");
    if (settled != SettleResult::Settled)
    {
        std::fflush(stdout);
        std::_Exit(1); // The admitted job still borrows this case's stack state; never unwind it early.
    }
    return true;
}

bool TestMixedTextureBufferReuse(Rig& rig, ID3D11Buffer* buffer)
{
    auto texture = MakeMixedTexture(rig.device);
    Expect(bool(texture), "mixed.create-literal-bgra-texture");
    if (!texture)
        return false;

    constexpr std::uint64_t bufferBytes = kCount * sizeof(std::uint32_t);
    constexpr std::uint64_t textureBytes = 4u * 2u * sizeof(std::uint32_t);
    const std::uint64_t baselineBytes = rig.queue.StagingBytes();
    Expect(baselineBytes >= bufferBytes, "mixed.starts-with-retained-buffer-staging-baseline");

    MixedReadbackState bufferFirst;
    auto bufferFirstJob =
        std::make_unique<MixedReadbackJob>(rig.scheduler, rig.queue, buffer, kExpectedValues.data(), bufferFirst);
    if (!SubmitAndSettleMixed(rig, std::move(bufferFirstJob), bufferFirst))
        return false;
    Expect(bufferFirst.deliveries == 1 && bufferFirst.callbacks == 1 && bufferFirst.ticketMatches &&
               bufferFirst.callbackResult == S_OK && bufferFirst.status == JobStatus::Finished &&
               bufferFirst.ownerExact && bufferFirst.workerExact && bufferFirst.workerThread &&
               bufferFirst.resumeAccepted && bufferFirst.releaseAccepted &&
               bufferFirst.bytesAtDelivery == baselineBytes && bufferFirst.bytesAfterRelease == baselineBytes &&
               rig.queue.LentCount() == 0 && rig.queue.InFlightCount() == 0,
           "mixed.buffer-first-exact-data-and-retained-idle-accounting");

    MixedReadbackState textureMiddle;
    auto textureJob = std::make_unique<MixedReadbackJob>(rig.scheduler, rig.queue, texture.get(),
                                                         kMixedTexturePixels.data(), textureMiddle);
    if (!SubmitAndSettleMixed(rig, std::move(textureJob), textureMiddle))
        return false;
    Expect(textureMiddle.deliveries == 1 && textureMiddle.callbacks == 1 && textureMiddle.ticketMatches &&
               textureMiddle.callbackResult == S_OK && textureMiddle.status == JobStatus::Finished &&
               textureMiddle.ownerExact && textureMiddle.workerExact && textureMiddle.workerThread &&
               textureMiddle.resumeAccepted && textureMiddle.releaseAccepted &&
               textureMiddle.lease.slot == bufferFirst.lease.slot &&
               textureMiddle.lease.generation != bufferFirst.lease.generation &&
               textureMiddle.bytesAtDelivery == baselineBytes - bufferBytes + textureBytes &&
               textureMiddle.bytesAfterRelease == textureMiddle.bytesAtDelivery && rig.queue.LentCount() == 0 &&
               rig.queue.InFlightCount() == 0,
           "mixed.texture-middle-exact-bytes-same-slot-kind-transition-retained-staging");

    MixedReadbackState bufferLast;
    auto bufferLastJob =
        std::make_unique<MixedReadbackJob>(rig.scheduler, rig.queue, buffer, kExpectedValues.data(), bufferLast);
    if (!SubmitAndSettleMixed(rig, std::move(bufferLastJob), bufferLast))
        return false;
    Expect(bufferLast.deliveries == 1 && bufferLast.callbacks == 1 && bufferLast.ticketMatches &&
               bufferLast.callbackResult == S_OK && bufferLast.status == JobStatus::Finished && bufferLast.ownerExact &&
               bufferLast.workerExact && bufferLast.workerThread && bufferLast.resumeAccepted &&
               bufferLast.releaseAccepted && bufferLast.lease.slot == textureMiddle.lease.slot &&
               bufferLast.lease.generation != textureMiddle.lease.generation &&
               bufferLast.bytesAtDelivery == baselineBytes && bufferLast.bytesAfterRelease == baselineBytes &&
               rig.queue.LentCount() == 0 && rig.queue.InFlightCount() == 0 && rig.scheduler.ReservedMemoryBytes() == 0,
           "mixed.buffer-last-exact-data-same-slot-kind-transition-accounting-restored");
    return true;
}

bool TestCancelWhileLent(Rig& rig, ID3D11Buffer* source)
{
    ReadbackState state;
    state.cancelWhileLent = true;
    if (!SubmitAndSettle(rig, source, state))
        return false;
    Expect(state.deliveries == 1 && state.callbacks == 1 && state.cancelAccepted && state.resumeAccepted,
           "cancel.request-and-resume-while-mapping-lent");
    Expect(state.runs == 1 && !state.workerSawExactValues && state.status == JobStatus::Cancelled,
           "cancel.skips-worker-read-of-lent-values");
    Expect(state.releaseAccepted && state.duplicateReleaseRejected && state.bytesAtDelivery == kCount * 4 &&
               state.bytesAfterRelease == state.bytesAtDelivery && rig.queue.LentCount() == 0 &&
               rig.queue.InFlightCount() == 0 && rig.scheduler.ReservedMemoryBytes() == 0,
           "cancel.owner-acknowledges-lent-mapping-after-worker-cancellation");
    return true;
}

bool TestRebindWhileLent(Rig& rig, ID3D11Buffer* source)
{
    Device replacement;
    const HRESULT replacementResult = replacement.Create();
    Expect(SUCCEEDED(replacementResult), "rebind.create-replacement-warp-device");
    if (FAILED(replacementResult))
        return false;

    ReadbackState state;
    if (!SubmitAndSettle(rig, source, state, &replacement))
        return false;
    Expect(state.deliveries == 1 && state.callbacks == 1 && SUCCEEDED(state.bindResult),
           "rebind.new-device-bound-during-lent-delivery");
    Expect(state.oldMappingRetainedOnRebind && state.workerThread && state.workerSawExactValues &&
               state.workerClampedExact,
           "rebind.old-lent-values-remain-readable-through-worker-ack");
    Expect(state.status == JobStatus::Cancelled && state.releaseAccepted && state.duplicateReleaseRejected &&
               state.bytesAfterRelease == 0 && rig.queue.StagingBytes() == 0 && rig.queue.LentCount() == 0 &&
               rig.queue.InFlightCount() == 0,
           "rebind.retired-generation-not-published-and-retired-storage-dropped-at-ack");
    return true;
}

class DeliveryProbe final : public GpuBufferReadbackReceiver
{
  public:
    void BufferReadbackReady(JobTicket, GpuBufferReadbackValues, HRESULT) noexcept override
    {
        ++deliveries;
    }
    unsigned int deliveries = 0;
};

void CheckInvalid(GpuWorkQueue& queue, ID3D11Buffer* source, unsigned int width, unsigned int height,
                  DeliveryProbe& receiver, const char* label)
{
    const HRESULT result = queue.EnqueueBufferReadback(source, width, height, JobTicket{0, 1}, receiver);
    Expect(result == E_INVALIDARG, label);
}

bool TestInvalidInputs(Rig& rig, ID3D11Buffer* valid)
{
    Device foreign;
    const HRESULT foreignResult = foreign.Create();
    Expect(SUCCEEDED(foreignResult), "invalid.create-foreign-warp-device");
    if (FAILED(foreignResult))
        return false;

    auto wrongBytes = MakeBuffer(rig.device, 5u * sizeof(std::uint32_t), sizeof(std::uint32_t),
                                 D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, kExpectedValues.data());
    auto wrongStride =
        MakeBuffer(rig.device, static_cast<UINT>(kCount * sizeof(std::uint32_t)), 2u * sizeof(std::uint32_t),
                   D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, kExpectedValues.data());
    auto nonStructured =
        MakeBuffer(rig.device, static_cast<UINT>(kCount * sizeof(std::uint32_t)), 0, 0, kExpectedValues.data());
    auto foreignSource = MakeValidBuffer(foreign);
    Expect(bool(wrongBytes) && bool(wrongStride) && bool(nonStructured) && bool(foreignSource),
           "invalid.construct-negative-case-buffers");
    if (!wrongBytes || !wrongStride || !nonStructured || !foreignSource)
        return false;

    const auto retainedBefore = rig.queue.StagingBytes();
    DeliveryProbe receiver;
    CheckInvalid(rig.queue, nullptr, kWidth, kHeight, receiver, "invalid.null-source-e-invalidarg");
    CheckInvalid(rig.queue, valid, 0, kHeight, receiver, "invalid.zero-width-e-invalidarg");
    CheckInvalid(rig.queue, valid, kWidth, 0, receiver, "invalid.zero-height-e-invalidarg");
    CheckInvalid(rig.queue, valid, GpuWorkQueue::kMaxReadbackSide + 1, 1, receiver,
                 "invalid.width-over-limit-e-invalidarg");
    CheckInvalid(rig.queue, valid, 1, GpuWorkQueue::kMaxReadbackSide + 1, receiver,
                 "invalid.height-over-limit-e-invalidarg");
    CheckInvalid(rig.queue, wrongBytes.get(), kWidth, kHeight, receiver, "invalid.byte-width-mismatch-e-invalidarg");
    CheckInvalid(rig.queue, wrongStride.get(), kWidth, kHeight, receiver,
                 "invalid.structured-stride-mismatch-e-invalidarg");
    CheckInvalid(rig.queue, nonStructured.get(), kWidth, kHeight, receiver,
                 "invalid.nonstructured-buffer-e-invalidarg");
    CheckInvalid(rig.queue, foreignSource.get(), kWidth, kHeight, receiver,
                 "invalid.foreign-device-source-e-invalidarg");
    Expect(receiver.deliveries == 0 && !rig.queue.HasPending() && !rig.queue.HasReady() &&
               rig.queue.StagingBytes() == retainedBefore && rig.queue.LentCount() == 0 &&
               rig.queue.InFlightCount() == 0,
           "invalid.immediate-errors-never-admit-or-deliver");
    return true;
}

constexpr unsigned int kStrokeWidth = 7;
constexpr unsigned int kStrokeHeight = 5;
constexpr std::size_t kStrokeCount = static_cast<std::size_t>(kStrokeWidth) * kStrokeHeight;
constexpr std::uint64_t kStrokeKey = 0x5354524f4b45ULL;
constexpr std::uint8_t kPreviewSentinel = 0xA5;

struct DeferredStrokeState final
{
    std::array<std::uint8_t, kStrokeCount> gpuReference{};
    std::array<std::uint8_t, kStrokeCount> cpuReference{};
    std::array<std::uint8_t, kStrokeCount> workerConverted{};
    GpuReadbackLease lease{};
    GpuBufferReadbackValues values{};
    JobTicket submittedTicket{};
    HRESULT enqueueResult = E_PENDING;
    HRESULT deliveryResult = E_PENDING;
    JobStatus status = JobStatus::Failed;
    unsigned int runs = 0;
    unsigned int deliveries = 0;
    unsigned int callbacks = 0;
    bool ticketMatches = false;
    bool workerThread = false;
    bool workerGpuExact = false;
    bool workerCpuWithinTolerance = false;
    bool workerSawZero = false;
    bool workerSawCoverage = false;
    bool engineClearedAfterAdmission = false;
    bool resumeAccepted = false;
    bool releaseAccepted = false;
    bool duplicateReleaseRejected = false;
    int maxCpuDifference = 0;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class DeferredStrokeJob final : public Job, public GpuBufferReadbackReceiver
{
  public:
    DeferredStrokeJob(JobScheduler& scheduler, GpuWorkQueue& queue, GpuBrushCoverage& engine,
                      DeferredStrokeState& state) noexcept
        : _scheduler(scheduler), _queue(queue), _engine(engine), _state(state), _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext&) override
    {
        ++_state.runs;
        if (!_submitted)
            return {};
        if (FAILED(_state.deliveryResult) || !_state.values.data || _state.values.count != kStrokeCount)
            return {JobStatus::Failed, FAILED(_state.deliveryResult) ? _state.deliveryResult : E_UNEXPECTED};

        _state.workerThread = GetCurrentThreadId() != _ownerThread;
        _state.workerGpuExact = true;
        _state.workerCpuWithinTolerance = true;
        for (std::size_t i = 0; i < kStrokeCount; ++i)
        {
            const std::uint32_t mapped = _state.values.data[i];
            const std::uint8_t converted = static_cast<std::uint8_t>((std::min)(mapped, 255u));
            _state.workerConverted[i] = converted;
            _state.workerGpuExact =
                _state.workerGpuExact && mapped == _state.gpuReference[i] && converted == _state.gpuReference[i];
            const int difference = std::abs(static_cast<int>(converted) - static_cast<int>(_state.cpuReference[i]));
            _state.maxCpuDifference = (std::max)(_state.maxCpuDifference, difference);
            _state.workerCpuWithinTolerance = _state.workerCpuWithinTolerance && difference <= 2;
            _state.workerSawZero = _state.workerSawZero || converted == 0;
            _state.workerSawCoverage = _state.workerSawCoverage || converted != 0;
        }
        return {};
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_submitted)
            return false;
        _submitted = true;
        ID3D11Buffer* source =
            _engine.CoverageBuffer(kStrokeKey, static_cast<int>(kStrokeWidth), static_cast<int>(kStrokeHeight));
        _state.enqueueResult = _queue.EnqueueBufferReadback(source, kStrokeWidth, kStrokeHeight, ticket, *this);
        if (SUCCEEDED(_state.enqueueResult))
        {
            // Queue admission owns the source before EndStroke drops the brush engine's final reference.
            _engine.EndStroke();
            _state.engineClearedAfterAdmission =
                _engine.TileCount() == 0 && _engine.CoverageBuffer(kStrokeKey, static_cast<int>(kStrokeWidth),
                                                                   static_cast<int>(kStrokeHeight)) == nullptr;
        }
        else
        {
            _state.resumeAccepted = _scheduler.Resume(ticket);
        }
        return true;
    }

    void BufferReadbackReady(JobTicket ticket, GpuBufferReadbackValues values, HRESULT result) noexcept override
    {
        ++_state.deliveries;
        _state.ticketMatches = ticket == _state.submittedTicket;
        _state.values = values;
        _state.lease = values.lease;
        _state.deliveryResult = result;
        if (SUCCEEDED(result) && (!values.data || values.width != kStrokeWidth || values.height != kStrokeHeight ||
                                  values.count != kStrokeCount))
            _state.deliveryResult = E_UNEXPECTED;
        _state.resumeAccepted = _scheduler.Resume(ticket);
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        if (_state.values.data)
        {
            _state.releaseAccepted = _queue.ReleaseMapped(_state.lease);
            _state.duplicateReleaseRejected = !_queue.ReleaseMapped(_state.lease);
        }
        _state.status = result.status;
    }

  private:
    JobScheduler& _scheduler;
    GpuWorkQueue& _queue;
    GpuBrushCoverage& _engine;
    DeferredStrokeState& _state;
    DWORD _ownerThread;
    bool _submitted = false;
};
#pragma warning(pop)

bool SettleDeferredStroke(Rig& rig, GpuBrushCoverage& engine, DeferredStrokeState& state, const char* scenario)
{
    std::unique_ptr<Job> job = std::make_unique<DeferredStrokeJob>(rig.scheduler, rig.queue, engine, state);
    const HRESULT submitted = static_cast<HRESULT>(
        rig.scheduler.Submit(job, {JobClass::Commit, 1, kStrokeCount * (sizeof(std::uint32_t) + sizeof(std::uint8_t))},
                             state.submittedTicket));
    if (submitted != S_OK)
    {
        ExpectStroke(false, scenario, "submit-and-event-settle");
        return false;
    }
    const SettleResult settled = SettleJobs(rig.scheduler, 10000);
    ExpectStroke(settled == SettleResult::Settled, scenario, "submit-and-event-settle");
    if (settled != SettleResult::Settled)
    {
        std::fflush(stdout);
        std::_Exit(1); // An admitted job still borrows the case state and receiver.
    }
    return true;
}

bool TestDeferredStrokeCase(Rig& rig, bool hard, bool replaceTail)
{
    GpuBrushCoverage gpuReference;
    GpuBrushCoverage gpuDeferred;
    const HRESULT referenceInit = gpuReference.Initialize(rig.device.device.get(), rig.device.context.get());
    const HRESULT deferredInit = gpuDeferred.Initialize(rig.device.device.get(), rig.device.context.get());
    if (FAILED(referenceInit) || FAILED(deferredInit))
    {
        ExpectStroke(false, "engine-initialize", "initialize-and-record-controls");
        return false;
    }

    BrushKernelParams params;
    params.radius = 2.25;
    params.hardness = hard ? 1.0 : 0.0;
    params.canvasWidth = 256;
    params.canvasHeight = 256;
    params.antialiasWidth = 1;
    params.spacing = hard ? 1.0 : 2.5;

    std::array<std::uint8_t, kStrokeCount> gpuPreview{};
    std::array<std::uint8_t, kStrokeCount> cpuPreview{};
    std::array<std::uint8_t, kStrokeCount> deferredSentinel{};
    deferredSentinel.fill(kPreviewSentinel);
    std::array<float, kStrokeCount> cpuPermanent{};
    const BrushKernelTile gpuTile{
        kStrokeKey, 0, 0, static_cast<int>(kStrokeWidth), static_cast<int>(kStrokeHeight), nullptr, gpuPreview.data()};
    const BrushKernelTile cpuTile{
        kStrokeKey,       0, 0, static_cast<int>(kStrokeWidth), static_cast<int>(kStrokeHeight), cpuPermanent.data(),
        cpuPreview.data()};
    const BrushKernelTile deferredTile{kStrokeKey,
                                       0,
                                       0,
                                       static_cast<int>(kStrokeWidth),
                                       static_cast<int>(kStrokeHeight),
                                       nullptr,
                                       deferredSentinel.data()};
    const std::array<BrushKernelTile, 1> gpuTiles{gpuTile};
    const std::array<BrushKernelTile, 1> cpuTiles{cpuTile};
    const std::array<BrushKernelTile, 1> deferredTiles{deferredTile};
    const std::array<BrushSegment, 1> dab{BrushSegment{Vec2{2.0, 2.0}, Vec2{2.0, 2.0}}};
    const std::array<BrushSegment, 1> firstTail{BrushSegment{Vec2{2.0, 2.0}, Vec2{6.0, 2.0}}};
    const std::array<BrushSegment, 1> replacementTail{BrushSegment{Vec2{4.0, 2.0}, Vec2{4.0, 4.0}}};
    std::array<BrushSegment, 1> settledAfterFirst{firstTail[0]};
    const char* scenario = hard ? (replaceTail ? "hard-tail-replacement" : "hard-settled-tail")
                                : (replaceTail ? "soft-tail-replacement" : "soft-settled-tail");

    gpuReference.BeginStroke();
    gpuDeferred.BeginStroke();
    CpuBrushCoverage cpuReference;
    cpuReference.BeginStroke();
    const auto renderStep = [&](std::span<const BrushSegment> settled, std::span<const BrushSegment> tail)
    {
        const bool gpuOk = gpuReference.Render(gpuTiles, settled, tail, params);
        const bool cpuOk = cpuReference.Render(cpuTiles, settled, tail, params);
        const bool recorded = gpuDeferred.RecordCoverage(deferredTiles, settled, tail, params);
        const bool sentinelUnchanged = std::all_of(deferredSentinel.begin(), deferredSentinel.end(),
                                                   [](std::uint8_t value) { return value == kPreviewSentinel; });
        return gpuOk && cpuOk && recorded && sentinelUnchanged;
    };

    bool rendered = false;
    if (replaceTail)
    {
        rendered = renderStep(dab, firstTail);
        if (rendered)
            rendered = renderStep(settledAfterFirst, replacementTail);
    }
    else
    {
        rendered = renderStep(dab, firstTail);
    }
    ExpectStroke(rendered, scenario, "gpu-cpu-record-dispatch-and-unchanged-preview-sentinel");
    if (!rendered)
        return false;

    DeferredStrokeState state;
    state.gpuReference = gpuPreview;
    state.cpuReference = cpuPreview;
    if (!SettleDeferredStroke(rig, gpuDeferred, state, scenario))
        return false;

    const bool payloadValid = SUCCEEDED(state.deliveryResult) && state.values.data &&
                              state.values.width == kStrokeWidth && state.values.height == kStrokeHeight &&
                              state.values.count == kStrokeCount;
    const ID3D11Buffer* wrongKey = gpuReference.CoverageBuffer(kStrokeKey + 1, kStrokeWidth, kStrokeHeight);
    const ID3D11Buffer* wrongSize = gpuReference.CoverageBuffer(kStrokeKey, kStrokeWidth - 1, kStrokeHeight);
    ExpectStroke(payloadValid && state.ticketMatches && state.deliveries == 1 && state.runs == 2 &&
                     state.workerThread && state.workerGpuExact && state.workerCpuWithinTolerance &&
                     state.maxCpuDifference <= 2 && state.workerSawZero && state.workerSawCoverage &&
                     state.resumeAccepted && state.callbacks == 1 && state.status == JobStatus::Finished,
                 scenario, "worker-converts-exact-gpu-reference-and-matches-cpu-within-2-of-255");
    ExpectStroke(state.engineClearedAfterAdmission && gpuDeferred.TileCount() == 0 && wrongKey == nullptr &&
                     wrongSize == nullptr && state.releaseAccepted && state.duplicateReleaseRejected &&
                     rig.queue.LentCount() == 0 && rig.queue.InFlightCount() == 0 &&
                     rig.scheduler.ReservedMemoryBytes() == 0,
                 scenario, "endstroke-retention-lookup-validation-and-worker-lease-acknowledgement");
    std::printf("INFO gpu-buffer-readback.deferred-case=%s max-cpu-difference=%d\n", scenario, state.maxCpuDifference);
    gpuReference.EndStroke();
    cpuReference.EndStroke();
    return true;
}

bool TestDeferredStrokeCoverage(Rig& rig)
{
    return TestDeferredStrokeCase(rig, true, false) && TestDeferredStrokeCase(rig, false, false) &&
           TestDeferredStrokeCase(rig, true, true) && TestDeferredStrokeCase(rig, false, true);
}
} // namespace

int RunGpuBufferReadbackTests()
{
    gFailures = 0;
    Rig rig;
    if (!rig.Initialize())
        return 1;
    auto source = MakeValidBuffer(rig.device);
    Expect(bool(source), "setup.create-structured-uint32-source");
    if (!source)
        return gFailures;
    if (!TestValuesEventRouteAndStaleLease(rig, source.get()) || !TestMixedTextureBufferReuse(rig, source.get()) ||
        !TestCancelWhileLent(rig, source.get()) || !TestInvalidInputs(rig, source.get()) ||
        !TestDeferredStrokeCoverage(rig) || !TestRebindWhileLent(rig, source.get()))
        return gFailures;
    return gFailures;
}
