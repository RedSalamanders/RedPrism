#include "GpuWork.h"
#include "OwnerWorkMultiplexer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <exception>
#include <limits>
#include <new>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

struct GpuWorkQueue::Impl final
{
    enum class State
    {
        Free,
        Pending,
        Waiting,
        Lent
    };
    struct Slot final
    {
        Impl* owner = nullptr;
        State state = State::Free;
        std::uint64_t generation = 0;
        std::uint64_t deviceGeneration = 0;
        bool mapDeferred = false;
        bool retired = false;
        std::atomic<bool> ready{false};
        wil::unique_handle event;
        JobEventTicket wait;
        wil::com_ptr_nothrow<ID3D11Texture2D> source;
        wil::com_ptr_nothrow<ID3D11Texture2D> staging;
        wil::com_ptr_nothrow<ID3D11Buffer> bufferSource;
        wil::com_ptr_nothrow<ID3D11Buffer> bufferStaging;
        bool bufferReadback = false;
        wil::com_ptr_nothrow<ID3D11DeviceContext> context;
        wil::com_ptr_nothrow<ID3D11Fence> completionFence;
        D3D11_BOX box{};
        unsigned int width = 0;
        unsigned int height = 0;
        std::uint64_t bytes = 0;
        JobTicket ticket;
        GpuReadbackReceiver* receiver = nullptr;
        GpuBufferReadbackReceiver* bufferReceiver = nullptr;
        GpuCompletionReceiver* completionReceiver = nullptr;
        GpuReadbackProducer* producer = nullptr;
    };

    // Allocated once inside Initialize's construction boundary, never during capture/recording.
    const std::shared_ptr<const std::uint8_t> identity = std::make_shared<const std::uint8_t>(std::uint8_t{0});
    DWORD ownerThread = GetCurrentThreadId();
    JobScheduler* scheduler = nullptr;
    OwnerWorkMultiplexer* routes = nullptr;
    std::array<Slot, kCapacity> slots;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    wil::com_ptr_nothrow<ID3D11DeviceContext4> context4;
    wil::com_ptr_nothrow<IDXGIDevice2> dxgiDevice2;
    wil::com_ptr_nothrow<ID3D11Fence> fence;
    GpuCapabilities::CompletionRoute route = GpuCapabilities::CompletionRoute::None;
    std::uint64_t deviceGeneration = 0;
    std::uint64_t fenceValue = 0;
    std::uint64_t stagingBytes = 0;
    std::size_t inFlight = 0;
    bool registered = false;
    bool stopped = false;
    unsigned int testBusyMaps = 0;
    unsigned int testSkipSuccessfulMaps = 0;
    unsigned int testMapAttempts = 0;
    bool testHoldMapRecovery = false;
    HRESULT testRearmFailure = S_OK;
    HRESULT testEnqueueFailure = S_OK;
    HRESULT testRemovedFailure = S_OK;
    GpuReadbackStatistics statistics;

    void CheckOwner() const noexcept
    {
        assert(ownerThread == GetCurrentThreadId());
    }

    static void Signalled(void* pointer) noexcept
    {
        auto& slot = *static_cast<Slot*>(pointer);
        slot.ready.store(true, std::memory_order_release);
        slot.owner->scheduler->NotifyOwnerWork();
    }

    void DropStorage(Slot& slot) noexcept
    {
        stagingBytes -= slot.bytes;
        slot.bytes = 0;
        slot.staging.reset();
        slot.bufferStaging.reset();
        slot.context.reset();
    }

    void Free(Slot& slot) noexcept
    {
        slot.source.reset();
        slot.bufferSource.reset();
        slot.completionFence.reset();
        slot.receiver = nullptr;
        slot.bufferReceiver = nullptr;
        slot.completionReceiver = nullptr;
        slot.producer = nullptr;
        slot.state = State::Free;
        if (slot.retired)
            DropStorage(slot);
    }

    void DeliverFailure(Slot& slot, HRESULT error) noexcept
    {
        auto* receiver = slot.receiver;
        auto* bufferReceiver = slot.bufferReceiver;
        auto* completionReceiver = slot.completionReceiver;
        const auto ticket = slot.ticket;
        const auto generation = slot.deviceGeneration;
        Free(slot);
        if (completionReceiver)
            completionReceiver->GpuCompletionReady(ticket, generation, error);
        else if (bufferReceiver)
            bufferReceiver->BufferReadbackReady(ticket, {}, error);
        else
            receiver->ReadbackReady(ticket, {}, error);
    }

    static ID3D11Resource* StagingResource(const Slot& slot) noexcept
    {
        return slot.bufferReadback ? static_cast<ID3D11Resource*>(slot.bufferStaging.get())
                                   : static_cast<ID3D11Resource*>(slot.staging.get());
    }

    static std::uint64_t StorageBytes(const Slot& slot) noexcept
    {
        const auto width = slot.bufferReadback ? slot.width : std::bit_ceil(slot.width);
        const auto height = slot.bufferReadback ? slot.height : std::bit_ceil(slot.height);
        return static_cast<std::uint64_t>(width) * height * sizeof(std::uint32_t);
    }

    bool CanRecord(const Slot& slot) const noexcept
    {
        const auto width = std::bit_ceil(slot.width);
        const auto height = std::bit_ceil(slot.height);
        if (slot.bufferReadback && slot.bufferStaging && slot.bytes == StorageBytes(slot))
            return true;
        if (!slot.bufferReadback && slot.staging)
        {
            D3D11_TEXTURE2D_DESC existing{};
            slot.staging->GetDesc(&existing);
            if (existing.Width == width && existing.Height == height)
                return true;
        }
        std::uint64_t reclaimable = slot.bytes;
        for (const auto& candidate : slots)
        {
            if (candidate.state == State::Free)
                reclaimable += candidate.bytes;
        }
        const std::uint64_t needed = StorageBytes(slot);
        return needed <= kStagingCapBytes - (stagingBytes - reclaimable);
    }

    HRESULT PrepareStorage(Slot& slot) noexcept
    {
        const auto width = std::bit_ceil(slot.width);
        const auto height = std::bit_ceil(slot.height);
        if (slot.bufferReadback && slot.bufferStaging && slot.deviceGeneration == deviceGeneration &&
            slot.bytes == StorageBytes(slot))
            return S_OK;
        if (!slot.bufferReadback && slot.staging && slot.deviceGeneration == deviceGeneration)
        {
            D3D11_TEXTURE2D_DESC existing{};
            slot.staging->GetDesc(&existing);
            if (existing.Width == width && existing.Height == height)
                return S_OK;
        }
        DropStorage(slot);
        const std::uint64_t bytes = StorageBytes(slot);
        if (bytes > kStagingCapBytes - stagingBytes)
        {
            // Evict only idle cache entries. Lent/retiring textures count against the same cap.
            for (auto& candidate : slots)
            {
                if (candidate.state == State::Free)
                    DropStorage(candidate);
            }
        }
        if (bytes > kStagingCapBytes - stagingBytes)
            return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
        if (slot.bufferReadback)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = static_cast<UINT>(bytes);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            const HRESULT result = device->CreateBuffer(&desc, nullptr, slot.bufferStaging.put());
            if (FAILED(result))
                return result;
            slot.bytes = bytes;
            stagingBytes += bytes;
            return S_OK;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        const HRESULT result = device->CreateTexture2D(&desc, nullptr, slot.staging.put());
        if (FAILED(result))
            return result;
        slot.bytes = bytes;
        stagingBytes += bytes;
        return S_OK;
    }

    HRESULT Record(Slot& slot) noexcept
    {
        HRESULT result = slot.completionReceiver ? S_OK : PrepareStorage(slot);
        if (FAILED(result))
            return result;
        if (slot.producer)
        {
            ID3D11Texture2D* source = nullptr;
            D3D11_BOX box{};
            result = slot.producer->RecordReadbackSource(source, box);
            if (FAILED(result))
                return result;
            result = ValidateSource(source, box);
            if (FAILED(result) || box.right - box.left != slot.width || box.bottom - box.top != slot.height)
                return FAILED(result) ? result : E_INVALIDARG;
            slot.source = source;
            slot.box = box;
        }
        slot.context = context;
        slot.completionFence = fence; // an old-generation event registration retains its fence until delivery
        slot.ready.store(false, std::memory_order_relaxed);
        if (!scheduler->RearmEventWait(slot.wait))
            return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        if (!slot.completionReceiver)
        {
            if (slot.bufferReadback)
                slot.context->CopyResource(slot.bufferStaging.get(), slot.bufferSource.get());
            else
                slot.context->CopySubresourceRegion(slot.staging.get(), 0, 0, 0, 0, slot.source.get(), 0, &slot.box);
        }
        if (route == GpuCapabilities::CompletionRoute::MonitoredFence)
        {
            if (fenceValue >= std::numeric_limits<std::uint64_t>::max() - 1)
                return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
            result = context4->Signal(fence.get(), ++fenceValue);
            if (SUCCEEDED(result))
                result = fence->SetEventOnCompletion(fenceValue, slot.event.get());
            if (SUCCEEDED(result))
                context->Flush();
        }
        else
            result = dxgiDevice2->EnqueueSetEvent(slot.event.get()); // includes the submit/flush
        if (SUCCEEDED(result))
        {
            slot.state = State::Waiting;
            slot.source.reset();
            slot.bufferSource.reset();
            ++inFlight;
            if (!slot.completionReceiver)
                ++statistics.copies;
        }
        return result;
    }

    HRESULT ValidateSource(ID3D11Texture2D* source, const D3D11_BOX& box) const noexcept
    {
        if (!source || box.front != 0 || box.back != 1 || box.right <= box.left || box.bottom <= box.top ||
            box.right - box.left > kMaxReadbackSide || box.bottom - box.top > kMaxReadbackSide)
            return E_INVALIDARG;
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.SampleDesc.Count != 1 || box.right > desc.Width ||
            box.bottom > desc.Height || desc.ArraySize != 1 || desc.MipLevels == 0)
            return E_INVALIDARG;
        wil::com_ptr_nothrow<ID3D11Device> sourceDevice;
        source->GetDevice(sourceDevice.put());
        return sourceDevice.get() == device.get() ? S_OK : E_INVALIDARG;
    }

    HRESULT ValidateBuffer(ID3D11Buffer* source, unsigned int width, unsigned int height) const noexcept
    {
        if (!source || width == 0 || height == 0 || width > kMaxReadbackSide || height > kMaxReadbackSide)
            return E_INVALIDARG;
        D3D11_BUFFER_DESC desc{};
        source->GetDesc(&desc);
        const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * sizeof(std::uint32_t);
        if (desc.ByteWidth != bytes || desc.StructureByteStride != sizeof(std::uint32_t) ||
            (desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) == 0 ||
            (desc.Usage != D3D11_USAGE_DEFAULT && desc.Usage != D3D11_USAGE_IMMUTABLE))
            return E_INVALIDARG;
        wil::com_ptr_nothrow<ID3D11Device> sourceDevice;
        source->GetDevice(sourceDevice.put());
        return sourceDevice.get() == device.get() ? S_OK : E_INVALIDARG;
    }

    HRESULT Admit(unsigned int width, unsigned int height, JobTicket ticket, GpuReadbackReceiver* receiver,
                  ID3D11Texture2D* source, const D3D11_BOX& box, GpuReadbackProducer* producer,
                  GpuBufferReadbackReceiver* bufferReceiver = nullptr, ID3D11Buffer* bufferSource = nullptr) noexcept
    {
        if (width == 0 || height == 0 || width > kMaxReadbackSide || height > kMaxReadbackSide ||
            ticket.slot >= JobScheduler::kCapacity || ticket.generation == 0)
            return E_INVALIDARG;
        for (auto& slot : slots)
        {
            if (slot.state != State::Free)
                continue;
            slot.source = source;
            slot.box = box;
            slot.width = width;
            slot.height = height;
            slot.ticket = ticket;
            slot.receiver = receiver;
            slot.bufferReceiver = bufferReceiver;
            slot.bufferSource = bufferSource;
            slot.bufferReadback = bufferReceiver != nullptr;
            slot.producer = producer;
            slot.retired = false;
            slot.mapDeferred = false;
            slot.deviceGeneration = deviceGeneration;
            ++slot.generation;
            slot.state = State::Pending;
            scheduler->NotifyOwnerWork();
            return S_OK;
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    }
};

GpuWorkQueue::GpuWorkQueue() noexcept = default;
GpuWorkQueue::~GpuWorkQueue()
{
    if (_impl)
    {
        // The host must join copying workers before destroying this owner. A forgotten lent lease is a bug,
        // not permission to free the worker's borrowed pointer from a destructor.
        assert(LentCount() == 0);
        StopNotifications();
        CloseAfterWorkers();
    }
}

HRESULT GpuWorkQueue::Initialize(JobScheduler& scheduler, OwnerWorkMultiplexer* routes) noexcept
{
    if (_impl || !scheduler.WakeHandle() || scheduler.InUiCallback())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    try
    {
        _impl = std::make_unique<Impl>();
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    auto& self = *_impl;
    self.scheduler = &scheduler;
    self.routes = routes;
    HRESULT result = S_OK;
    for (auto& slot : self.slots)
    {
        slot.owner = &self;
        slot.event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!slot.event)
            return HRESULT_FROM_WIN32(GetLastError());
        result = scheduler.RegisterEventWait(slot.event.get(), Impl::Signalled, &slot, slot.wait);
        if (FAILED(result))
            return result;
    }
    if (!(routes ? routes->Attach(*this) : scheduler.RegisterOwnerWork(*this)))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    self.registered = true;
    return S_OK;
}

HRESULT GpuWorkQueue::Bind(ID3D11Device* device, ID3D11DeviceContext* context, bool forceEventForTest) noexcept
{
    if (!_impl || _impl->stopped)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    GpuCapabilities::Report report;
    HRESULT result = GpuCapabilities::Probe(device, context, report);
    if (FAILED(result))
        return result;
    const auto route = forceEventForTest && report.dxgiDevice2Available && SUCCEEDED(report.dxgiDevice2QueryResult)
                           ? GpuCapabilities::CompletionRoute::EnqueueSetEvent
                       : forceEventForTest ? GpuCapabilities::CompletionRoute::None
                                           : GpuCapabilities::SelectCompletionRoute(report);
    if (route == GpuCapabilities::CompletionRoute::None)
        return E_NOINTERFACE;
    wil::com_ptr_nothrow<ID3D11DeviceContext4> context4;
    wil::com_ptr_nothrow<ID3D11Fence> fence;
    wil::com_ptr_nothrow<IDXGIDevice2> dxgiDevice2;
    if (route == GpuCapabilities::CompletionRoute::MonitoredFence)
    {
        wil::com_ptr_nothrow<ID3D11Device5> device5;
        result = device->QueryInterface(IID_PPV_ARGS(device5.put()));
        if (SUCCEEDED(result))
            result = context->QueryInterface(IID_PPV_ARGS(context4.put()));
        if (SUCCEEDED(result))
            result = device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()));
    }
    else
        result = device->QueryInterface(IID_PPV_ARGS(dxgiDevice2.put()));
    if (FAILED(result))
        return result;
    RetireDevice();
    // Fence completion can precede driver staging-map readiness. Keep the optional DXGI route for one
    // additional event barrier. An unsupported recovery route still fails the operation locally.
    if (!dxgiDevice2)
        (void)device->QueryInterface(IID_PPV_ARGS(dxgiDevice2.put()));
    self.device = device;
    self.context = context;
    self.context4 = std::move(context4);
    self.fence = std::move(fence);
    self.dxgiDevice2 = std::move(dxgiDevice2);
    self.route = route;
    // Values increase across rebinds: captured old fences and the current fence share this owner sequence.
    // Resetting on Bind could signal an old captured fence below its already completed value.
    ++self.deviceGeneration;
    return S_OK;
}

HRESULT GpuWorkQueue::EnqueueReadback(ID3D11Texture2D* source, const D3D11_BOX& box, JobTicket ticket,
                                      GpuReadbackReceiver& receiver) noexcept
{
    if (!_impl || _impl->stopped || !_impl->device)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    const HRESULT result = self.ValidateSource(source, box);
    return FAILED(result)
               ? result
               : self.Admit(box.right - box.left, box.bottom - box.top, ticket, &receiver, source, box, nullptr);
}

HRESULT GpuWorkQueue::EnqueueProducedReadback(unsigned int width, unsigned int height, JobTicket ticket,
                                              GpuReadbackReceiver& receiver, GpuReadbackProducer& producer) noexcept
{
    if (!_impl || _impl->stopped || !_impl->device)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    _impl->CheckOwner();
    return _impl->Admit(width, height, ticket, &receiver, nullptr, {}, &producer);
}

HRESULT GpuWorkQueue::EnqueueBufferReadback(ID3D11Buffer* source, unsigned int width, unsigned int height,
                                            JobTicket ticket, GpuBufferReadbackReceiver& receiver) noexcept
{
    if (!_impl || _impl->stopped || !_impl->device)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    const HRESULT result = self.ValidateBuffer(source, width, height);
    return FAILED(result) ? result
                          : self.Admit(width, height, ticket, nullptr, nullptr, {}, nullptr, &receiver, source);
}

bool GpuWorkQueue::HasCompletionSlot() const noexcept
{
    return HasReadbackSlot() && _impl->inFlight < kMaxInFlight;
}

HRESULT GpuWorkQueue::EnqueueCompletion(JobTicket ticket, GpuCompletionReceiver& receiver) noexcept
{
    if (!_impl || _impl->stopped || !_impl->device)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    if (!HasCompletionSlot())
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    // Admission captures only fixed descriptor storage. Record immediately so the real signal order and
    // monotonically increasing fence value cannot be overtaken by another copy or a subsequent Bind.
    const HRESULT admitted = self.Admit(1, 1, ticket, nullptr, nullptr, {}, nullptr);
    if (FAILED(admitted))
        return admitted;
    for (auto& slot : self.slots)
    {
        if (slot.state != Impl::State::Pending || slot.ticket != ticket || slot.receiver || slot.bufferReceiver ||
            slot.producer || slot.source || slot.bufferSource)
            continue;
        slot.completionReceiver = &receiver;
        const HRESULT result = self.Record(slot);
        if (FAILED(result))
            self.Free(slot); // Failed recording is a local refusal, never a callback or a retirement proof.
        return result;
    }
    std::terminate(); // This owner just admitted the sole matching descriptor; no callback can intervene.
}

HRESULT GpuCompletionContext::DeviceStatusOwner() const noexcept
{
    return _origin && _device && _ownerThread == GetCurrentThreadId() ? _device->GetDeviceRemovedReason()
                                                                      : HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
}

HRESULT GpuWorkQueue::CaptureCompletionContext(GpuCompletionContext& empty) const noexcept
{
    if (!_impl || _impl->stopped || !_impl->device || empty)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    const auto& self = *_impl;
    self.CheckOwner();
    const HRESULT removed = self.device->GetDeviceRemovedReason();
    if (FAILED(removed))
        return removed;
    if (self.route == GpuCapabilities::CompletionRoute::None || !self.context)
        return E_NOINTERFACE;
    empty._device = self.device;
    empty._context = self.context;
    empty._context4 = self.context4;
    empty._dxgiDevice2 = self.dxgiDevice2;
    empty._fence = self.fence;
    empty._route = self.route;
    empty._deviceGeneration = self.deviceGeneration;
    empty._ownerThread = self.ownerThread;
    empty._origin = self.identity; // Publish validity last; no callback or allocating operation intervenes.
    return S_OK;
}

bool GpuWorkQueue::HasRetirementCompletionSlot(const GpuCompletionContext& captured) const noexcept
{
    if (!_impl || _impl->stopped || !captured || captured._origin != _impl->identity ||
        captured._ownerThread != GetCurrentThreadId())
        return false;
    _impl->CheckOwner();
    return _impl->inFlight < kMaxInFlight &&
           std::any_of(
               _impl->slots.begin(), _impl->slots.end(),
               [](const Impl::Slot& slot) noexcept { return slot.state == Impl::State::Free; });
}

HRESULT GpuWorkQueue::EnqueueRetirementCompletion(const GpuCompletionContext& captured, JobTicket ticket,
                                                  GpuCompletionReceiver& receiver) noexcept
{
    if (!_impl || _impl->stopped || !captured || captured._origin != _impl->identity ||
        captured._ownerThread != GetCurrentThreadId())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    if (!HasRetirementCompletionSlot(captured))
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    const HRESULT removed = captured._device->GetDeviceRemovedReason();
    if (FAILED(removed))
        return removed; // Caller may separately treat confirmed removal as retirement, never this refusal callback.
    const HRESULT admitted = self.Admit(1, 1, ticket, nullptr, nullptr, {}, nullptr);
    if (FAILED(admitted))
        return admitted;
    for (auto& slot : self.slots)
    {
        if (slot.state != Impl::State::Pending || slot.ticket != ticket || slot.receiver || slot.bufferReceiver ||
            slot.producer || slot.source || slot.bufferSource)
            continue;
        slot.completionReceiver = &receiver;
        slot.deviceGeneration = captured._deviceGeneration;
        slot.retired =
            captured._context.get() != self.context.get() || captured._deviceGeneration != self.deviceGeneration;
        slot.context = captured._context;
        slot.completionFence = captured._fence;
        slot.ready.store(false, std::memory_order_relaxed);
        HRESULT result = self.scheduler->RearmEventWait(slot.wait) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        if (SUCCEEDED(result) && captured._route == GpuCapabilities::CompletionRoute::MonitoredFence)
        {
            if (self.fenceValue >= (std::numeric_limits<std::uint64_t>::max)() - 1)
                result = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
            else
            {
                result = captured._context4->Signal(captured._fence.get(), ++self.fenceValue);
                if (SUCCEEDED(result))
                    result = captured._fence->SetEventOnCompletion(self.fenceValue, slot.event.get());
                if (SUCCEEDED(result))
                    captured._context->Flush();
            }
        }
        else if (SUCCEEDED(result))
            result = captured._dxgiDevice2->EnqueueSetEvent(slot.event.get());
        if (SUCCEEDED(result))
        {
            slot.state = Impl::State::Waiting;
            ++self.inFlight;
        }
        else
            self.Free(slot); // This is a refusal, not a retirement acknowledgement.
        return result;
    }
    std::terminate();
}

bool GpuWorkQueue::HasReadbackSlot() const noexcept
{
    if (!_impl || _impl->stopped || !_impl->device)
        return false;
    _impl->CheckOwner();
    return std::any_of(_impl->slots.begin(), _impl->slots.end(),
                       [](const Impl::Slot& slot) noexcept { return slot.state == Impl::State::Free; });
}

bool GpuWorkQueue::ReleaseMapped(GpuReadbackLease lease) noexcept
{
    if (!_impl || lease.slot >= kCapacity)
        return false;
    auto& self = *_impl;
    self.CheckOwner();
    auto& slot = self.slots[lease.slot];
    if (slot.state != Impl::State::Lent || slot.generation != lease.generation)
        return false;
    slot.context->Unmap(Impl::StagingResource(slot), 0);
    self.Free(slot);
    self.scheduler->NotifyOwnerWork();
    return true;
}

void GpuWorkQueue::RetireDevice() noexcept
{
    if (!_impl)
        return;
    auto& self = *_impl;
    self.CheckOwner();
    for (auto& slot : self.slots)
    {
        slot.retired = true;
        if (slot.state == Impl::State::Free)
            self.DropStorage(slot);
    }
    self.device.reset();
    self.context.reset();
    self.context4.reset();
    self.dxgiDevice2.reset();
    self.fence.reset();
    self.route = GpuCapabilities::CompletionRoute::None;
    self.scheduler->NotifyOwnerWork();
}

void GpuWorkQueue::StopNotifications() noexcept
{
    if (_impl && !_impl->stopped)
    {
        _impl->CheckOwner();
        _impl->stopped = true;
        for (auto& slot : _impl->slots)
        {
            if (slot.wait.generation != 0)
                (void)_impl->scheduler->RemoveEventWait(slot.wait);
        }
    }
}

void GpuWorkQueue::CloseAfterWorkers() noexcept
{
    if (!_impl)
        return;
    auto& self = *_impl;
    self.CheckOwner();
    assert(self.stopped);
    for (auto& slot : self.slots)
    {
        if (slot.state == Impl::State::Lent)
            slot.context->Unmap(Impl::StagingResource(slot), 0);
        slot.state = Impl::State::Free;
        slot.source.reset();
        slot.bufferSource.reset();
        slot.completionFence.reset();
        slot.receiver = nullptr;
        slot.bufferReceiver = nullptr;
        slot.completionReceiver = nullptr;
        slot.producer = nullptr;
        self.DropStorage(slot);
    }
    if (self.registered)
        (void)(self.routes ? self.routes->Detach(*this) : self.scheduler->UnregisterOwnerWork(*this));
    _impl.reset();
}

bool GpuWorkQueue::HasPending() const noexcept
{
    if (!_impl)
        return false;
    _impl->CheckOwner();
    return std::any_of(_impl->slots.begin(), _impl->slots.end(),
                       [](const Impl::Slot& slot) noexcept { return slot.state != Impl::State::Free; });
}

bool GpuWorkQueue::HasReady() const noexcept
{
    if (!_impl || _impl->stopped)
        return false;
    _impl->CheckOwner();
    for (const auto& slot : _impl->slots)
    {
        if ((slot.state == Impl::State::Pending &&
             (slot.retired || (_impl->inFlight < kMaxInFlight && _impl->CanRecord(slot)))) ||
            (slot.state == Impl::State::Waiting && !(slot.mapDeferred && _impl->testHoldMapRecovery) &&
             slot.ready.load(std::memory_order_acquire)))
            return true;
    }
    return false;
}

void GpuWorkQueue::Pump() noexcept
{
    if (!_impl || _impl->stopped)
        return;
    auto& self = *_impl;
    self.CheckOwner();
    for (std::size_t index = 0; index < self.slots.size(); ++index)
    {
        auto& slot = self.slots[index];
        if (slot.state != Impl::State::Waiting || (slot.mapDeferred && self.testHoldMapRecovery) ||
            !slot.ready.exchange(false, std::memory_order_acquire))
            continue;
        if (slot.completionReceiver)
        {
            --self.inFlight;
            // A healthy retired generation also waits for its original real event. Query only that captured
            // context's device, never the replacement. The event acknowledges command retirement even when
            // its result is stale or the old device was removed; no rows are mapped or lent here.
            wil::com_ptr_nothrow<ID3D11Device> originalDevice;
            slot.context->GetDevice(originalDevice.put());
            const HRESULT removed = originalDevice->GetDeviceRemovedReason();
            self.DeliverFailure(slot, FAILED(removed) ? removed : slot.retired ? E_ABORT : S_OK);
            continue;
        }
        if (slot.retired)
        {
            --self.inFlight;
            self.DeliverFailure(slot, E_ABORT);
            continue;
        }
        const HRESULT removed = slot.mapDeferred && FAILED(self.testRemovedFailure)
                                    ? self.testRemovedFailure
                                    : self.device->GetDeviceRemovedReason();
        if (FAILED(removed))
        {
            --self.inFlight;
            self.DeliverFailure(slot, removed);
            continue;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (self.testMapAttempts != (std::numeric_limits<unsigned int>::max)())
            ++self.testMapAttempts;
        ++self.statistics.maps;
        HRESULT result = DXGI_ERROR_WAS_STILL_DRAWING;
        if (self.testBusyMaps != 0 && self.testSkipSuccessfulMaps == 0)
            --self.testBusyMaps;
        else
        {
            result =
                slot.context->Map(Impl::StagingResource(slot), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (SUCCEEDED(result) && self.testSkipSuccessfulMaps != 0)
                --self.testSkipSuccessfulMaps;
        }
        if (result == DXGI_ERROR_WAS_STILL_DRAWING && !slot.mapDeferred && self.dxgiDevice2)
        {
            // Retain the original copy and in-flight charge through one new driver event. There is no recopy,
            // timer or polling loop; a second busy Map fails the operation.
            slot.mapDeferred = true;
            const HRESULT rearmed = FAILED(self.testRearmFailure) ? self.testRearmFailure
                                    : self.scheduler->RearmEventWait(slot.wait)
                                        ? S_OK
                                        : HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
            const HRESULT barrier = FAILED(rearmed) ? rearmed
                                    : FAILED(self.testEnqueueFailure)
                                        ? self.testEnqueueFailure
                                        : self.dxgiDevice2->EnqueueSetEvent(slot.event.get());
            if (SUCCEEDED(barrier))
            {
                ++self.statistics.readinessBarriers;
                continue;
            }
            result = barrier;
        }
        --self.inFlight;
        if (FAILED(result))
        {
            // Persistent driver busy remains a local failure after the single allowed event barrier.
            if (result == DXGI_ERROR_WAS_STILL_DRAWING)
            {
                ++self.statistics.busyFailures;
                OutputDebugStringW(L"RedPrism: staging readback remained busy after its bounded readiness barrier.\n");
            }
            self.DeliverFailure(slot, result);
            continue;
        }
        if (!mapped.pData || (!slot.bufferReadback && mapped.RowPitch < static_cast<std::size_t>(slot.width) * 4))
        {
            slot.context->Unmap(Impl::StagingResource(slot), 0);
            self.DeliverFailure(slot, E_UNEXPECTED);
            continue;
        }
        slot.state = Impl::State::Lent;
        if (slot.bufferReadback)
        {
            const GpuBufferReadbackValues values{static_cast<const std::uint32_t*>(mapped.pData),
                                                 static_cast<std::size_t>(slot.width) * slot.height,
                                                 slot.width,
                                                 slot.height,
                                                 {index, slot.generation},
                                                 slot.deviceGeneration};
            slot.bufferReceiver->BufferReadbackReady(slot.ticket, values, S_OK);
        }
        else
        {
            const GpuReadbackRows rows{static_cast<const std::uint8_t*>(mapped.pData),
                                       mapped.RowPitch,
                                       slot.width,
                                       slot.height,
                                       {index, slot.generation},
                                       slot.deviceGeneration};
            slot.receiver->ReadbackReady(slot.ticket, rows, S_OK);
        }
    }
    for (auto& slot : self.slots)
    {
        if (slot.state != Impl::State::Pending)
            continue;
        if (slot.retired)
            self.DeliverFailure(slot, E_ABORT);
        else if (self.inFlight < kMaxInFlight && self.CanRecord(slot))
        {
            const HRESULT result = self.Record(slot);
            if (FAILED(result))
                self.DeliverFailure(slot, result);
        }
    }
}

std::uint64_t GpuWorkQueue::StagingBytes() const noexcept
{
    return _impl ? _impl->stagingBytes : 0;
}
std::size_t GpuWorkQueue::InFlightCount() const noexcept
{
    return _impl ? _impl->inFlight : 0;
}
std::size_t GpuWorkQueue::LentCount() const noexcept
{
    return _impl ? static_cast<std::size_t>(std::count_if(_impl->slots.begin(), _impl->slots.end(),
                                                          [](const Impl::Slot& slot) noexcept
                                                          { return slot.state == Impl::State::Lent; }))
                 : 0;
}
std::uint64_t GpuWorkQueue::DeviceGeneration() const noexcept
{
    return _impl ? _impl->deviceGeneration : 0;
}
GpuCapabilities::CompletionRoute GpuWorkQueue::CompletionRoute() const noexcept
{
    return _impl ? _impl->route : GpuCapabilities::CompletionRoute::None;
}

void GpuWorkQueue::SetMapBusyForTest(unsigned int attempts, unsigned int afterSuccessfulMaps) noexcept
{
    if (_impl)
    {
        _impl->CheckOwner();
        _impl->testBusyMaps = attempts;
        _impl->testSkipSuccessfulMaps = afterSuccessfulMaps;
        _impl->testMapAttempts = 0;
    }
}

unsigned int GpuWorkQueue::MapAttemptsForTest() const noexcept
{
    return _impl ? _impl->testMapAttempts : 0;
}

void GpuWorkQueue::HoldMapRecoveryForTest(bool hold) noexcept
{
    if (_impl)
    {
        _impl->CheckOwner();
        _impl->testHoldMapRecovery = hold;
        if (!hold)
            _impl->scheduler->NotifyOwnerWork();
    }
}

void GpuWorkQueue::SetMapRecoveryFailuresForTest(HRESULT rearm, HRESULT enqueue, HRESULT removed) noexcept
{
    if (_impl)
    {
        _impl->CheckOwner();
        _impl->testRearmFailure = rearm;
        _impl->testEnqueueFailure = enqueue;
        _impl->testRemovedFailure = removed;
    }
}

GpuReadbackStatistics GpuWorkQueue::Statistics() const noexcept
{
    return _impl ? _impl->statistics : GpuReadbackStatistics{};
}
