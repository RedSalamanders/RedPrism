#include "GpuUploadWork.h"
#include "OwnerWorkMultiplexer.h"
#include "UiTiming.h"

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

namespace
{
std::atomic<std::uint64_t> gNextUploadQueue{1};
std::uint64_t AllocateUploadQueueIdentity() noexcept
{
    auto candidate = gNextUploadQueue.load(std::memory_order_relaxed);
    while (candidate != std::numeric_limits<std::uint64_t>::max())
        if (gNextUploadQueue.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed))
            return candidate;
    return 0;
}
} // namespace

struct GpuUploadQueue::Impl final
{
    explicit Impl(GpuUploadQueue& uploadQueue) noexcept : queue(uploadQueue) {}
    enum class State : std::uint8_t
    {
        Free,
        Pending,
        MapBarrier,
        Lent,
        CopyBarrier,
    };
    static constexpr std::uint64_t kLent = 1;
    static constexpr std::uint64_t kFilled = 2;
    static constexpr std::uint64_t kCancelled = 3;
    static constexpr std::uint64_t kFailed = 4;
    static constexpr std::uint64_t kGenerationLimit = std::numeric_limits<std::uint64_t>::max() >> 3;

    struct Slot final
    {
        Impl* owner = nullptr;
        State state = State::Free;
        std::uint64_t generation = 0;
        std::uint64_t deviceGeneration = 0;
        // Generation and result share one atomic so a delayed duplicate cannot race reuse into a new loan.
        std::atomic<std::uint64_t> publication{0};
        std::atomic<bool> signalled{false};
        bool cancelled = false;
        bool retired = false;
        bool poisoned = false;
        bool retry = false;
        wil::unique_handle event;
        JobEventTicket wait;
        GpuResidencyReservation retirementReservation; // last release after retained graphics storage
        wil::com_ptr_nothrow<ID3D11Texture2D> destination;
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> mipView;
        CancelToken mipCancellation;
        wil::com_ptr_nothrow<ID3D11RenderTargetView> mipTarget;
        wil::com_ptr_nothrow<ID3D11VertexShader> mipVertex;
        wil::com_ptr_nothrow<ID3D11PixelShader> mipPixel;
        wil::com_ptr_nothrow<ID3D11SamplerState> mipSampler;
        wil::com_ptr_nothrow<ID3D11RasterizerState> mipRasterizer;
        wil::com_ptr_nothrow<ID3D11DepthStencilState> mipDepth;
        unsigned int mipWidth = 0;
        unsigned int mipHeight = 0;
        wil::com_ptr_nothrow<ID3D11Texture2D> staging;
        wil::com_ptr_nothrow<ID3D11DeviceContext> context;
        wil::com_ptr_nothrow<ID3D11Fence> completionFence;
        unsigned int subresource = 0;
        unsigned int x = 0;
        unsigned int y = 0;
        unsigned int width = 0;
        unsigned int height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        std::uint64_t bytes = 0;
        JobTicket ticket;
        GpuUploadReceiver* receiver = nullptr;
    };

    DWORD ownerThread = GetCurrentThreadId();
    GpuUploadQueue& queue;
    const std::uint64_t identity = AllocateUploadQueueIdentity();
    JobScheduler* scheduler = nullptr;
    OwnerWorkMultiplexer* routes = nullptr;
    GpuResidencyReservation persistentReservation; // survives every reusable/retired staging allocation
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
    std::uint64_t mipSlices = 0;
    std::size_t inFlight = 0;
    std::size_t next = 0;
    bool registered = false;
    bool stopped = false;
    unsigned int busyMaps = 0;
    unsigned int mapAttempts = 0;

    void CheckOwner() const noexcept
    {
        assert(ownerThread == GetCurrentThreadId());
    }
    static unsigned int Channels(DXGI_FORMAT format) noexcept
    {
        return format == DXGI_FORMAT_R8_UNORM ? 1U : 4U;
    }
    static void Signalled(void* pointer) noexcept
    {
        auto& slot = *static_cast<Slot*>(pointer);
        slot.signalled.store(true, std::memory_order_release);
        slot.owner->scheduler->NotifyOwnerWork();
    }
    static void DropMipState(Slot& slot) noexcept
    {
        slot.mipView.reset();
        slot.mipTarget.reset();
        slot.mipVertex.reset();
        slot.mipPixel.reset();
        slot.mipSampler.reset();
        slot.mipRasterizer.reset();
        slot.mipDepth.reset();
        slot.mipCancellation = {};
    }
    void DropStorage(Slot& slot) noexcept
    {
        assert(slot.state != State::Lent);
        stagingBytes -= slot.bytes;
        slot.bytes = 0;
        slot.staging.reset();
        DropMipState(slot);
        slot.context.reset();
        slot.completionFence.reset();
        slot.poisoned = false;
        slot.retirementReservation.ResetOwner();
    }
    void Finish(Slot& slot, HRESULT result) noexcept
    {
        auto* receiver = slot.receiver;
        const auto ticket = slot.ticket;
        if (slot.state != State::Pending)
        {
            assert(inFlight != 0);
            --inFlight;
        }
        slot.state = State::Free;
        slot.publication.store(0, std::memory_order_release);
        slot.receiver = nullptr;
        slot.destination.reset();
        if (!slot.poisoned)
        {
            DropMipState(slot);
            slot.completionFence.reset();
        }
        if (slot.retired && !slot.poisoned)
            DropStorage(slot);
        receiver->UploadFinished(ticket, result);
    }
    HRESULT PrepareStorage(Slot& slot) noexcept
    {
        const auto width = std::bit_ceil(slot.width);
        const auto height = std::bit_ceil(slot.height);
        if (slot.staging)
        {
            D3D11_TEXTURE2D_DESC old{};
            slot.staging->GetDesc(&old);
            if (old.Width == width && old.Height == height && old.Format == slot.format)
                return S_OK;
        }
        DropStorage(slot);
        const std::uint64_t required = static_cast<std::uint64_t>(width) * height * Channels(slot.format);
        if (required > kStagingCapBytes - stagingBytes)
        {
            for (auto& candidate : slots)
                if (candidate.state == State::Free && !candidate.poisoned)
                    DropStorage(candidate);
        }
        if (required > kStagingCapBytes - stagingBytes)
            return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = slot.format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HRESULT result = S_OK;
        if (!queue.FailAt(GpuUploadFailurePoint::StagingCreation, result))
            result = device->CreateTexture2D(&desc, nullptr, slot.staging.put());
        if (SUCCEEDED(result))
        {
            slot.bytes = required;
            stagingBytes += required;
        }
        return result;
    }
    // Rearm before recording. A failed post-copy signal quarantines the staging allocation until retirement;
    // a fence failure can never authorize reuse of storage the GPU might still be reading.
    HRESULT Arm(Slot& slot, bool recovery, bool alreadyRearmed = false) noexcept
    {
        slot.signalled.store(false, std::memory_order_relaxed);
        slot.completionFence = fence;
        HRESULT injected = E_FAIL;
        if (!alreadyRearmed)
        {
            if (queue.FailAt(GpuUploadFailurePoint::WaitRearm, injected))
                return injected;
            if (!scheduler->RearmEventWait(slot.wait))
                return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        }
        const auto signalPoint = recovery       ? GpuUploadFailurePoint::RecoverySignal
                                 : slot.mipView ? GpuUploadFailurePoint::MipSignal
                                                : GpuUploadFailurePoint::CopySignal;
        if (queue.FailAt(signalPoint, injected))
            return injected;
        if (recovery)
            return dxgiDevice2 ? dxgiDevice2->EnqueueSetEvent(slot.event.get()) : E_NOINTERFACE;
        if (route == GpuCapabilities::CompletionRoute::MonitoredFence)
        {
            if (fenceValue == std::numeric_limits<std::uint64_t>::max())
                return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
            HRESULT result = context4->Signal(fence.get(), ++fenceValue);
            if (SUCCEEDED(result))
                result = fence->SetEventOnCompletion(fenceValue, slot.event.get());
            if (SUCCEEDED(result))
                context->Flush();
            return result;
        }
        return dxgiDevice2->EnqueueSetEvent(slot.event.get());
    }
    void RecordMip(Slot& slot) noexcept
    {
        HRESULT result = device->GetDeviceRemovedReason();
        if (FAILED(result) || queue.FailAt(GpuUploadFailurePoint::MipRecording, result))
        {
            Finish(slot, result);
            return;
        }
        if (queue.FailAt(GpuUploadFailurePoint::WaitRearm, result))
        {
            Finish(slot, result);
            return;
        }
        if (!scheduler->RearmEventWait(slot.wait))
        {
            Finish(slot, HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
            return;
        }
        slot.context = context;
        ++inFlight;
        slot.state = State::CopyBarrier;
        {
            const ScopedUiTiming timing{UiTimingKind::MipGeneration};
            auto* source = slot.mipView.get();
            auto* target = slot.mipTarget.get();
            auto* sampler = slot.mipSampler.get();
            const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(slot.mipWidth), static_cast<float>(slot.mipHeight),
                                          0, 1};
            slot.context->IASetInputLayout(nullptr);
            slot.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            slot.context->VSSetShader(slot.mipVertex.get(), nullptr, 0);
            slot.context->HSSetShader(nullptr, nullptr, 0);
            slot.context->DSSetShader(nullptr, nullptr, 0);
            slot.context->GSSetShader(nullptr, nullptr, 0);
            slot.context->PSSetShader(slot.mipPixel.get(), nullptr, 0);
            slot.context->RSSetState(slot.mipRasterizer.get());
            slot.context->RSSetViewports(1, &viewport);
            slot.context->OMSetDepthStencilState(slot.mipDepth.get(), 0);
            slot.context->OMSetBlendState(nullptr, nullptr, 0xffffffffU);
            slot.context->OMSetRenderTargets(1, &target, nullptr);
            slot.context->PSSetShaderResources(0, 1, &source);
            slot.context->PSSetSamplers(0, 1, &sampler);
            slot.context->Draw(3, 0);
            // Remove persistent context references; the admitted queue/creator holds objects until the fence.
            source = nullptr;
            sampler = nullptr;
            slot.context->PSSetShaderResources(0, 1, &source);
            slot.context->PSSetSamplers(0, 1, &sampler);
            slot.context->OMSetRenderTargets(0, nullptr, nullptr);
            slot.context->VSSetShader(nullptr, nullptr, 0);
            slot.context->PSSetShader(nullptr, nullptr, 0);
            slot.context->RSSetState(nullptr);
            slot.context->OMSetDepthStencilState(nullptr, 0);
        }
        if (mipSlices != std::numeric_limits<std::uint64_t>::max())
            ++mipSlices;
        result = Arm(slot, false, true);
        if (FAILED(result))
        {
            slot.poisoned = true; // the command may still reference the exact view/resource/context
            Finish(slot, result);
        }
    }
    void Map(Slot& slot, std::size_t index) noexcept
    {
        HRESULT result = slot.retry ? S_OK : PrepareStorage(slot);
        if (FAILED(result))
        {
            Finish(slot, result);
            return;
        }
        if (slot.state == State::Pending)
            ++inFlight;
        slot.state = State::MapBarrier; // charged even if this map fails
        slot.context = context;
        if (mapAttempts != std::numeric_limits<unsigned int>::max())
            ++mapAttempts;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (busyMaps != 0)
        {
            --busyMaps;
            result = DXGI_ERROR_WAS_STILL_DRAWING;
        }
        else if (!queue.FailAt(GpuUploadFailurePoint::Map, result))
            result = slot.context->Map(slot.staging.get(), 0, D3D11_MAP_WRITE, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (result == DXGI_ERROR_WAS_STILL_DRAWING && !slot.retry)
        {
            slot.retry = true;
            result = Arm(slot, true);
            if (SUCCEEDED(result))
                return;
            slot.poisoned = true; // A failed signal may leave an old event producer; never reuse its wait class.
        }
        if (FAILED(result))
        {
            Finish(slot, result);
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        slot.staging->GetDesc(&desc);
        HRESULT pitchFailure = E_FAIL;
        if (queue.FailAt(GpuUploadFailurePoint::MappedPitch, pitchFailure))
            mapped.RowPitch = static_cast<UINT>(kMaxRowBytes + 1);
        const std::uint64_t actualBytes = static_cast<std::uint64_t>(mapped.RowPitch) * desc.Height;
        const std::uint64_t retained = stagingBytes - slot.bytes;
        if (!mapped.pData || mapped.RowPitch < static_cast<std::uint64_t>(slot.width) * Channels(slot.format) ||
            mapped.RowPitch > kMaxRowBytes || actualBytes > kStagingCapBytes - retained)
        {
            slot.context->Unmap(slot.staging.get(), 0);
            DropStorage(slot); // An oversized/invalid driver pitch must not remain in the idle cache.
            Finish(slot, HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA));
            return;
        }
        stagingBytes = retained + actualBytes;
        slot.bytes = actualBytes;
        slot.state = State::Lent;
        slot.publication.store((slot.generation << 3) | kLent, std::memory_order_release);
        const GpuUploadRows rows{static_cast<std::uint8_t*>(mapped.pData),
                                 static_cast<std::size_t>(mapped.RowPitch) * slot.height,
                                 mapped.RowPitch,
                                 slot.width,
                                 slot.height,
                                 {index, slot.generation, identity},
                                 slot.deviceGeneration};
        slot.receiver->WritableRowsReady(slot.ticket, rows);
    }
    bool Ready(const Slot& slot) const noexcept
    {
        switch (slot.state)
        {
        case State::Free:
            return false;
        case State::Pending:
            return slot.retired || slot.cancelled || (slot.mipView && slot.mipCancellation.IsCancelled()) ||
                   inFlight < kMaxInFlight;
        case State::MapBarrier:
        case State::CopyBarrier:
            return slot.retired || slot.signalled.load(std::memory_order_acquire);
        case State::Lent:
            return (slot.publication.load(std::memory_order_acquire) & 7U) != kLent;
        }
        return false;
    }
};

GpuUploadQueue::GpuUploadQueue() noexcept = default;
GpuUploadQueue::~GpuUploadQueue()
{
    if (_impl)
    {
        assert(LentCount() == 0);
        StopNotifications();
        CloseAfterWorkers();
    }
}

HRESULT GpuUploadQueue::Initialize(JobScheduler& scheduler, OwnerWorkMultiplexer* routes) noexcept
{
    if (_impl || !scheduler.WakeHandle() || scheduler.InUiCallback())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    try
    {
        HRESULT injected = E_OUTOFMEMORY;
        if (FailAt(GpuUploadFailurePoint::ImplementationAllocation, injected))
            throw std::bad_alloc{}; // Test the same explicit noexcept allocation boundary as make_unique.
        _impl = std::make_unique<Impl>(*this);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    auto& self = *_impl;
    self.scheduler = &scheduler;
    self.routes = routes;
    if (self.identity == 0)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    for (auto& slot : self.slots)
    {
        slot.owner = &self;
        HRESULT injected = E_FAIL;
        if (FailAt(GpuUploadFailurePoint::EventCreation, injected))
            return injected;
        slot.event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!slot.event)
            return HRESULT_FROM_WIN32(GetLastError());
        HRESULT result = S_OK;
        if (!FailAt(GpuUploadFailurePoint::WaitRegistration, result))
            result = scheduler.RegisterEventWait(slot.event.get(), Impl::Signalled, &slot, slot.wait);
        if (FAILED(result))
            return result;
    }
    HRESULT injected = E_FAIL;
    if (FailAt(GpuUploadFailurePoint::OwnerRegistration, injected))
        return injected;
    if (!(routes ? routes->Attach(*this) : scheduler.RegisterOwnerWork(*this)))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    self.registered = true;
    return S_OK;
}

HRESULT GpuUploadQueue::Bind(ID3D11Device* device, ID3D11DeviceContext* context, bool forceEventForTest) noexcept
{
    if (!_impl || _impl->stopped)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    if (self.deviceGeneration == std::numeric_limits<std::uint64_t>::max())
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    GpuCapabilities::Report report;
    HRESULT result = GpuCapabilities::Probe(device, context, report);
    if (FAILED(result))
        return result;
    const auto route = forceEventForTest ? report.dxgiDevice2Available && SUCCEEDED(report.dxgiDevice2QueryResult)
                                               ? GpuCapabilities::CompletionRoute::EnqueueSetEvent
                                               : GpuCapabilities::CompletionRoute::None
                                         : GpuCapabilities::SelectCompletionRoute(report);
    if (route == GpuCapabilities::CompletionRoute::None)
        return E_NOINTERFACE;
    wil::com_ptr_nothrow<ID3D11DeviceContext4> context4;
    wil::com_ptr_nothrow<ID3D11Fence> fence;
    wil::com_ptr_nothrow<IDXGIDevice2> dxgiDevice2;
    (void)device->QueryInterface(IID_PPV_ARGS(dxgiDevice2.put()));
    if (route == GpuCapabilities::CompletionRoute::MonitoredFence)
    {
        wil::com_ptr_nothrow<ID3D11Device5> device5;
        result = device->QueryInterface(IID_PPV_ARGS(device5.put()));
        if (SUCCEEDED(result))
            result = context->QueryInterface(IID_PPV_ARGS(context4.put()));
        if (SUCCEEDED(result))
            result = device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()));
        if (FAILED(result))
            return result;
    }
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

bool GpuUploadQueue::AdmitPersistentResidency(GpuResidencyReservation& reservation) noexcept
{
    if (!_impl || !reservation)
        return false;
    auto& self = *_impl;
    self.CheckOwner();
    if (self.stopped || self.stagingBytes || self.inFlight || HasPending() ||
        reservation.DeviceGeneration() != self.deviceGeneration ||
        reservation.Bytes() < kStagingCapBytes + GpuResidencyBudget::kMinimumHeadroom)
        return false;
    self.persistentReservation = std::move(reservation);
    return true;
}

bool GpuUploadQueue::HasAdmittedPersistentResidency() const noexcept
{
    if (!_impl)
        return false;
    _impl->CheckOwner();
    return !_impl->stopped && _impl->persistentReservation &&
           _impl->persistentReservation.DeviceGeneration() == _impl->deviceGeneration;
}

HRESULT GpuUploadQueue::Enqueue(ID3D11Texture2D* destination, unsigned int subresource, unsigned int x, unsigned int y,
                                unsigned int width, unsigned int height, JobTicket ticket,
                                GpuUploadReceiver& receiver) noexcept
{
    if (!_impl || _impl->stopped || !_impl->device)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    if (!destination || width == 0 || height == 0 || ticket.slot >= JobScheduler::kCapacity || ticket.generation == 0)
        return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc{};
    destination->GetDesc(&desc);
    if (desc.Usage != D3D11_USAGE_DEFAULT || desc.SampleDesc.Count != 1 || desc.MipLevels == 0 ||
        (desc.Format != DXGI_FORMAT_R8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) ||
        subresource >= static_cast<std::uint64_t>(desc.MipLevels) * desc.ArraySize)
        return E_INVALIDARG;
    const auto mip = subresource % desc.MipLevels;
    const auto mipWidth = mip < 32 ? std::max(1U, desc.Width >> mip) : 1U;
    const auto mipHeight = mip < 32 ? std::max(1U, desc.Height >> mip) : 1U;
    const auto rowBytes = static_cast<std::uint64_t>(width) * Impl::Channels(desc.Format);
    if (x > mipWidth || y > mipHeight || width > mipWidth - x || height > mipHeight - y || rowBytes > kMaxRowBytes ||
        rowBytes * height > kChunkBytes)
        return E_INVALIDARG;
    wil::com_ptr_nothrow<ID3D11Device> destinationDevice;
    destination->GetDevice(destinationDevice.put());
    if (destinationDevice.get() != self.device.get())
        return E_INVALIDARG;
    for (auto& slot : self.slots)
    {
        if (slot.state != Impl::State::Free || slot.poisoned || slot.generation == Impl::kGenerationLimit)
            continue;
        slot.destination = destination;
        slot.mipView.reset();
        slot.subresource = subresource;
        slot.x = x;
        slot.y = y;
        slot.width = width;
        slot.height = height;
        slot.format = desc.Format;
        slot.ticket = ticket;
        slot.receiver = &receiver;
        slot.cancelled = false;
        slot.retired = false;
        slot.retry = false;
        slot.deviceGeneration = self.deviceGeneration;
        ++slot.generation;
        slot.state = Impl::State::Pending;
        self.scheduler->NotifyOwnerWork();
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
}

HRESULT GpuUploadQueue::EnqueueMipSlice(GpuMipSlice slice, JobTicket ticket, GpuUploadReceiver& receiver,
                                        CancelToken cancellation) noexcept
{
    if (!_impl || _impl->stopped || !_impl->device || !HasAdmittedPersistentResidency())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    if (!slice.source || !slice.destination || !slice.vertex || !slice.pixel || !slice.sampler || !slice.rasterizer ||
        !slice.depth || !slice.width || !slice.height || ticket.slot >= JobScheduler::kCapacity || !ticket.generation)
        return E_INVALIDARG;
    if (slice.deviceGeneration != self.deviceGeneration)
        return E_ABORT;
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    slice.source->GetDesc(&view);
    D3D11_RENDER_TARGET_VIEW_DESC target{};
    slice.destination->GetDesc(&target);
    if (view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || view.Texture2D.MipLevels != 1 ||
        target.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D ||
        target.Texture2D.MipSlice != view.Texture2D.MostDetailedMip + 1 || target.Format != view.Format)
        return E_INVALIDARG;
    wil::com_ptr_nothrow<ID3D11Resource> sourceResource;
    wil::com_ptr_nothrow<ID3D11Resource> targetResource;
    slice.source->GetResource(sourceResource.put());
    slice.destination->GetResource(targetResource.put());
    if (!sourceResource || sourceResource.get() != targetResource.get())
        return E_INVALIDARG;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    if (FAILED(sourceResource->QueryInterface(IID_PPV_ARGS(texture.put()))))
        return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    const auto first = view.Texture2D.MostDetailedMip;
    if (description.Usage != D3D11_USAGE_DEFAULT || description.SampleDesc.Count != 1 || description.ArraySize != 1 ||
        description.MipLevels < 2 || first >= description.MipLevels - 1 || first >= 31 ||
        (std::max)(1U, description.Width >> (first + 1)) != slice.width ||
        (std::max)(1U, description.Height >> (first + 1)) != slice.height || view.Format != description.Format ||
        (description.Format != DXGI_FORMAT_R8_UNORM && description.Format != DXGI_FORMAT_B8G8R8A8_UNORM) ||
        (description.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)) !=
            (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE))
        return E_INVALIDARG;
    const std::array<ID3D11DeviceChild*, 8> children{texture.get(), slice.source,  slice.destination, slice.vertex,
                                                     slice.pixel,   slice.sampler, slice.rasterizer,  slice.depth};
    for (auto* child : children)
    {
        wil::com_ptr_nothrow<ID3D11Device> device;
        child->GetDevice(device.put());
        if (device.get() != self.device.get())
            return E_INVALIDARG;
    }
    for (auto& slot : self.slots)
    {
        if (slot.state != Impl::State::Free || slot.poisoned || slot.generation == Impl::kGenerationLimit)
            continue;
        slot.destination = texture.get();
        slot.mipView = slice.source;
        slot.mipTarget = slice.destination;
        slot.mipVertex = slice.vertex;
        slot.mipPixel = slice.pixel;
        slot.mipSampler = slice.sampler;
        slot.mipRasterizer = slice.rasterizer;
        slot.mipDepth = slice.depth;
        slot.mipWidth = slice.width;
        slot.mipHeight = slice.height;
        slot.mipCancellation = std::move(cancellation);
        slot.ticket = ticket;
        slot.receiver = &receiver;
        slot.cancelled = false;
        slot.retired = false;
        slot.retry = false;
        slot.deviceGeneration = self.deviceGeneration;
        ++slot.generation;
        slot.state = Impl::State::Pending;
        self.scheduler->NotifyOwnerWork();
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
}

bool GpuUploadQueue::PublishFilled(GpuUploadLease lease, GpuUploadFill result) noexcept
{
    if (!_impl || lease.queueIdentity != _impl->identity || lease.slot >= kCapacity || lease.generation == 0 ||
        lease.generation > Impl::kGenerationLimit ||
        (result != GpuUploadFill::Ready && result != GpuUploadFill::Cancelled && result != GpuUploadFill::Failed))
        return false;
    auto& slot = _impl->slots[lease.slot];
    const auto status = result == GpuUploadFill::Ready       ? Impl::kFilled
                        : result == GpuUploadFill::Cancelled ? Impl::kCancelled
                                                             : Impl::kFailed;
    std::uint64_t expected = (lease.generation << 3) | Impl::kLent;
    if (!slot.publication.compare_exchange_strong(expected, (lease.generation << 3) | status, std::memory_order_acq_rel,
                                                  std::memory_order_relaxed))
        return false;
    _impl->scheduler->NotifyOwnerWork();
    return true;
}

void GpuUploadQueue::Cancel(JobTicket ticket) noexcept
{
    if (!_impl)
        return;
    _impl->CheckOwner();
    for (auto& slot : _impl->slots)
        if (slot.state != Impl::State::Free && slot.ticket == ticket)
            slot.cancelled = true;
    _impl->scheduler->NotifyOwnerWork();
}

bool GpuUploadQueue::RetainRetiredResidency(JobTicket ticket, GpuResidencyReservation& reservation) noexcept
{
    if (!_impl || !reservation || ticket.slot >= JobScheduler::kCapacity || !ticket.generation)
        return false;
    _impl->CheckOwner();
    for (auto& slot : _impl->slots)
    {
        if (slot.ticket == ticket && slot.deviceGeneration == reservation.DeviceGeneration() &&
            (slot.poisoned || (_impl->stopped && slot.state != Impl::State::Free)) && !slot.retirementReservation)
        {
            slot.retirementReservation = std::move(reservation);
            return true;
        }
    }
    return false;
}

void GpuUploadQueue::RetireDevice() noexcept
{
    if (!_impl)
        return;
    auto& self = *_impl;
    self.CheckOwner();
    for (auto& slot : self.slots)
    {
        slot.retired = true;
        if (slot.state == Impl::State::Free && !slot.poisoned)
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

void GpuUploadQueue::StopNotifications() noexcept
{
    if (!_impl || _impl->stopped)
        return;
    _impl->CheckOwner();
    _impl->stopped = true;
    for (auto& slot : _impl->slots)
        if (slot.wait.generation != 0)
            (void)_impl->scheduler->RemoveEventWait(slot.wait);
}

void GpuUploadQueue::CloseAfterWorkers() noexcept
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
        slot.receiver = nullptr;
        slot.destination.reset();
        self.DropStorage(slot);
    }
    if (self.registered)
        (void)(self.routes ? self.routes->Detach(*this) : self.scheduler->UnregisterOwnerWork(*this));
    _impl.reset();
}

bool GpuUploadQueue::HasPending() const noexcept
{
    if (!_impl)
        return false;
    _impl->CheckOwner();
    return std::any_of(_impl->slots.begin(), _impl->slots.end(),
                       [](const Impl::Slot& slot) noexcept { return slot.state != Impl::State::Free; });
}
bool GpuUploadQueue::HasReady() const noexcept
{
    if (!_impl || _impl->stopped)
        return false;
    _impl->CheckOwner();
    return std::any_of(_impl->slots.begin(), _impl->slots.end(),
                       [this](const Impl::Slot& slot) noexcept { return _impl->Ready(slot); });
}

void GpuUploadQueue::Pump() noexcept
{
    if (!_impl || _impl->stopped)
        return;
    auto& self = *_impl;
    self.CheckOwner();
    for (std::size_t offset = 0; offset < kCapacity; ++offset)
    {
        const auto index = (self.next + offset) % kCapacity;
        auto& slot = self.slots[index];
        if (!self.Ready(slot))
            continue;
        self.next = (index + 1) % kCapacity;
        if (slot.state == Impl::State::Lent)
        {
            const auto filled = slot.publication.load(std::memory_order_acquire) & 7U;
            slot.context->Unmap(slot.staging.get(), 0);
            if (slot.retired || slot.cancelled || filled != Impl::kFilled)
                self.Finish(slot, filled == Impl::kFailed ? E_FAIL : E_ABORT);
            else
            {
                const HRESULT removed = self.device->GetDeviceRemovedReason();
                if (FAILED(removed))
                    self.Finish(slot, removed);
                else
                {
                    // Refusal to arm causes no copy. Arm() rearms as well, which is harmless before signalling.
                    HRESULT rearmFailure = E_FAIL;
                    if (self.queue.FailAt(GpuUploadFailurePoint::WaitRearm, rearmFailure))
                        self.Finish(slot, rearmFailure);
                    else if (!self.scheduler->RearmEventWait(slot.wait))
                        self.Finish(slot, HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
                    else
                    {
                        const D3D11_BOX box{0, 0, 0, slot.width, slot.height, 1};
                        slot.context->CopySubresourceRegion(slot.destination.get(), slot.subresource, slot.x, slot.y, 0,
                                                            slot.staging.get(), 0, &box);
                        const HRESULT result = self.Arm(slot, false, true);
                        if (FAILED(result))
                        {
                            slot.poisoned = true;
                            self.Finish(slot, result);
                        }
                        else
                            slot.state = Impl::State::CopyBarrier;
                    }
                }
            }
        }
        else if (slot.state == Impl::State::Pending)
        {
            if (slot.cancelled || slot.retired || (slot.mipView && slot.mipCancellation.IsCancelled()))
                self.Finish(slot, E_ABORT);
            else if (slot.mipView)
                self.RecordMip(slot);
            else
                self.Map(slot, index);
        }
        else
        {
            slot.signalled.store(false, std::memory_order_relaxed);
            if (slot.retired || slot.cancelled)
            {
                // Retiring before the event arrived retains this wait/resource class until producer join;
                // a late old-generation event can never authorize a reused slot's new copy.
                if (slot.retired)
                    slot.poisoned = true;
                self.Finish(slot, E_ABORT);
            }
            else
            {
                const HRESULT removed = self.device->GetDeviceRemovedReason();
                if (FAILED(removed))
                    self.Finish(slot, removed);
                else if (slot.state == Impl::State::MapBarrier)
                    self.Map(slot, index);
                else
                    self.Finish(slot, S_OK);
            }
        }
        return; // one transition, one map/copy chunk at most per outer pump
    }
}

std::uint64_t GpuUploadQueue::StagingBytes() const noexcept
{
    return _impl ? _impl->stagingBytes : 0;
}
std::uint64_t GpuUploadQueue::RecordedMipSlices() const noexcept
{
    if (!_impl)
        return 0;
    _impl->CheckOwner();
    return _impl->mipSlices;
}
std::uint64_t GpuUploadQueue::RetiredResidencyBytes() const noexcept
{
    if (!_impl)
        return 0;
    _impl->CheckOwner();
    std::uint64_t bytes = 0;
    for (const auto& slot : _impl->slots)
    {
        const auto retained = slot.retirementReservation.Bytes();
        if (retained > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return (std::numeric_limits<std::uint64_t>::max)(); // distinct admitting domains fail closed
        bytes += retained;
    }
    return bytes;
}
std::size_t GpuUploadQueue::InFlightCount() const noexcept
{
    return _impl ? _impl->inFlight : 0;
}
std::size_t GpuUploadQueue::LentCount() const noexcept
{
    return _impl ? static_cast<std::size_t>(std::count_if(_impl->slots.begin(), _impl->slots.end(),
                                                          [](const Impl::Slot& slot) noexcept
                                                          { return slot.state == Impl::State::Lent; }))
                 : 0;
}
std::uint64_t GpuUploadQueue::DeviceGeneration() const noexcept
{
    return _impl ? _impl->deviceGeneration : 0;
}
GpuCapabilities::CompletionRoute GpuUploadQueue::CompletionRoute() const noexcept
{
    return _impl ? _impl->route : GpuCapabilities::CompletionRoute::None;
}
void GpuUploadQueue::SetMapBusyForTest(unsigned int attempts) noexcept
{
    if (!_impl)
        return;
    _impl->CheckOwner();
    _impl->busyMaps = attempts;
    _impl->mapAttempts = 0;
}
unsigned int GpuUploadQueue::MapAttemptsForTest() const noexcept
{
    return _impl ? _impl->mapAttempts : 0;
}

void GpuUploadQueue::SetFailureForTest(GpuUploadFailurePoint point, HRESULT error, unsigned int skip) noexcept
{
    if (_impl)
        _impl->CheckOwner();
    assert(FAILED(error));
    _failurePoint = point;
    _failureResult = FAILED(error) ? error : E_FAIL;
    _failureSkip = skip;
    _failureHits = 0;
}

bool GpuUploadQueue::FailAt(GpuUploadFailurePoint point, HRESULT& result) noexcept
{
    if (_failurePoint != point)
        return false;
    if (_failureSkip != 0)
    {
        --_failureSkip;
        return false;
    }
    ++_failureHits;
    _failurePoint = GpuUploadFailurePoint::None;
    result = _failureResult;
    return true;
}

unsigned int GpuUploadQueue::FailureHitsForTest() const noexcept
{
    return _failureHits;
}
