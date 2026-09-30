#include "GpuWork.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
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
        bool retired = false;
        std::atomic<bool> ready{false};
        wil::unique_handle event;
        JobEventTicket wait;
        wil::com_ptr_nothrow<ID3D11Texture2D> source;
        wil::com_ptr_nothrow<ID3D11Texture2D> staging;
        wil::com_ptr_nothrow<ID3D11DeviceContext> context;
        wil::com_ptr_nothrow<ID3D11Fence> completionFence;
        D3D11_BOX box{};
        unsigned int width = 0;
        unsigned int height = 0;
        std::uint64_t bytes = 0;
        JobTicket ticket;
        GpuReadbackReceiver* receiver = nullptr;
        GpuReadbackProducer* producer = nullptr;
    };

    DWORD ownerThread = GetCurrentThreadId();
    JobScheduler* scheduler = nullptr;
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
        slot.context.reset();
    }

    void Free(Slot& slot) noexcept
    {
        slot.source.reset();
        slot.completionFence.reset();
        slot.receiver = nullptr;
        slot.producer = nullptr;
        slot.state = State::Free;
        if (slot.retired)
            DropStorage(slot);
    }

    void DeliverFailure(Slot& slot, HRESULT error) noexcept
    {
        auto* receiver = slot.receiver;
        const auto ticket = slot.ticket;
        Free(slot);
        receiver->ReadbackReady(ticket, {}, error);
    }

    bool CanRecord(const Slot& slot) const noexcept
    {
        const auto width = std::bit_ceil(slot.width);
        const auto height = std::bit_ceil(slot.height);
        if (slot.staging)
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
        const std::uint64_t needed = static_cast<std::uint64_t>(width) * height * 4;
        return needed <= kStagingCapBytes - (stagingBytes - reclaimable);
    }

    HRESULT PrepareStorage(Slot& slot) noexcept
    {
        const auto width = std::bit_ceil(slot.width);
        const auto height = std::bit_ceil(slot.height);
        if (slot.staging && slot.deviceGeneration == deviceGeneration)
        {
            D3D11_TEXTURE2D_DESC existing{};
            slot.staging->GetDesc(&existing);
            if (existing.Width == width && existing.Height == height)
                return S_OK;
        }
        DropStorage(slot);
        const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 4;
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
        HRESULT result = PrepareStorage(slot);
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
        slot.context->CopySubresourceRegion(slot.staging.get(), 0, 0, 0, 0, slot.source.get(), 0, &slot.box);
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
            ++inFlight;
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

    HRESULT Admit(unsigned int width, unsigned int height, JobTicket ticket, GpuReadbackReceiver& receiver,
                  ID3D11Texture2D* source, const D3D11_BOX& box, GpuReadbackProducer* producer) noexcept
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
            slot.receiver = &receiver;
            slot.producer = producer;
            slot.retired = false;
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

HRESULT GpuWorkQueue::Initialize(JobScheduler& scheduler) noexcept
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
    if (!scheduler.RegisterOwnerWork(*this))
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
    self.device = device;
    self.context = context;
    self.context4 = std::move(context4);
    self.fence = std::move(fence);
    self.dxgiDevice2 = std::move(dxgiDevice2);
    self.route = route;
    self.fenceValue = 0;
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
               : self.Admit(box.right - box.left, box.bottom - box.top, ticket, receiver, source, box, nullptr);
}

HRESULT GpuWorkQueue::EnqueueProducedReadback(unsigned int width, unsigned int height, JobTicket ticket,
                                              GpuReadbackReceiver& receiver, GpuReadbackProducer& producer) noexcept
{
    if (!_impl || _impl->stopped || !_impl->device)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    _impl->CheckOwner();
    return _impl->Admit(width, height, ticket, receiver, nullptr, {}, &producer);
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
    slot.context->Unmap(slot.staging.get(), 0);
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
            slot.context->Unmap(slot.staging.get(), 0);
        slot.state = Impl::State::Free;
        slot.source.reset();
        slot.completionFence.reset();
        slot.receiver = nullptr;
        slot.producer = nullptr;
        self.DropStorage(slot);
    }
    if (self.registered)
        (void)self.scheduler->UnregisterOwnerWork(*this);
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
            (slot.state == Impl::State::Waiting && slot.ready.load(std::memory_order_acquire)))
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
        if (slot.state != Impl::State::Waiting || !slot.ready.exchange(false, std::memory_order_acquire))
            continue;
        --self.inFlight;
        if (slot.retired)
        {
            self.DeliverFailure(slot, E_ABORT);
            continue;
        }
        const HRESULT removed = self.device->GetDeviceRemovedReason();
        if (FAILED(removed))
        {
            self.DeliverFailure(slot, removed);
            continue;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT result = slot.context->Map(slot.staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (FAILED(result))
        {
            // WAS_STILL_DRAWING after completion is a diagnostic failure, never a retry/poll path.
            if (result == DXGI_ERROR_WAS_STILL_DRAWING)
                OutputDebugStringW(L"RedPrism: staging readback was still drawing after its completion event.\n");
            self.DeliverFailure(slot, result);
            continue;
        }
        if (!mapped.pData || mapped.RowPitch < static_cast<std::size_t>(slot.width) * 4)
        {
            slot.context->Unmap(slot.staging.get(), 0);
            self.DeliverFailure(slot, E_UNEXPECTED);
            continue;
        }
        slot.state = Impl::State::Lent;
        const GpuReadbackRows rows{static_cast<const std::uint8_t*>(mapped.pData),
                                   mapped.RowPitch,
                                   slot.width,
                                   slot.height,
                                   {index, slot.generation},
                                   slot.deviceGeneration};
        slot.receiver->ReadbackReady(slot.ticket, rows, S_OK);
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
