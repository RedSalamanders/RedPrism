#pragma once

#include "GpuCapabilities.h"
#include "Jobs.h"

#include <cstddef>
#include <cstdint>
#include <memory>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

struct GpuReadbackLease final
{
    std::size_t slot = 8;
    std::uint64_t generation = 0;
    [[nodiscard]] bool operator==(const GpuReadbackLease&) const noexcept = default;
};

// Borrowed rows, valid only until the owner acknowledges the exact lease. Workers may read/copy, never call
// the context. The queue retains the mapped texture/context even when its device generation is retired.
struct GpuReadbackRows final
{
    const std::uint8_t* data = nullptr;
    std::size_t rowPitch = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    GpuReadbackLease lease;
    std::uint64_t deviceGeneration = 0;
};

// Structured uint32 coverage is borrowed separately from BGRA rows. No texture pitch or byte
// conversion is implied. The owner releases the same exact queue lease after the worker acknowledges.
struct GpuBufferReadbackValues final
{
    const std::uint32_t* data = nullptr;
    std::size_t count = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    GpuReadbackLease lease;
    std::uint64_t deviceGeneration = 0;
};

// Every admitted callback acknowledges the captured command stream, including a stale-generation E_ABORT.
// S_OK additionally means the device/generation is still current. Failed admission/recording is
// NOT a GPU retirement acknowledgement: callers must retain or quarantine their graphics charge until a real
// completion, confirmed device removal, or joined shutdown. Receiver outlives delivery/queue close.
class GpuCompletionReceiver
{
  public:
    virtual ~GpuCompletionReceiver() = default;
    virtual void GpuCompletionReady(JobTicket ticket, std::uint64_t deviceGeneration, HRESULT result) noexcept = 0;
};

class GpuBufferReadbackReceiver
{
  public:
    virtual ~GpuBufferReadbackReceiver() = default;
    virtual void BufferReadbackReady(JobTicket ticket, GpuBufferReadbackValues values, HRESULT result) noexcept = 0;
};

class GpuReadbackReceiver
{
  public:
    virtual ~GpuReadbackReceiver() = default;
    // Owner pump callback, not message dispatch. Exactly once per admitted read. A failed read has no lease.
    // The receiver remains alive through delivery and, for successful reads, ReleaseMapped acknowledgement.
    virtual void ReadbackReady(JobTicket ticket, GpuReadbackRows rows, HRESULT result) noexcept = 0;
};

class GpuReadbackProducer
{
  public:
    virtual ~GpuReadbackProducer() = default;
    // Owner pump only. The borrowed texture remains valid through the immediately following staging copy.
    // Width/height must match admission. Recording the source here prevents a reusable utility target from
    // being overwritten by another request between composition and its copy. The job owns this producer.
    [[nodiscard]] virtual HRESULT RecordReadbackSource(ID3D11Texture2D*& source, D3D11_BOX& box) noexcept = 0;
};

struct GpuReadbackStatistics final
{
    std::uint64_t copies = 0;
    std::uint64_t maps = 0;
    std::uint64_t readinessBarriers = 0;
    std::uint64_t busyFailures = 0;
};

class OwnerWorkMultiplexer;

// Immutable owner-captured completion stream. Strong references retain the actual device/context/route;
// the origin token prevents a closed or reused queue address from accepting this stream. Capture creates
// no resource and records no command. Release on the final worker after acknowledgement or joined shutdown.
class GpuCompletionContext final
{
  public:
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return bool(_origin);
    }
    [[nodiscard]] std::uint64_t DeviceGeneration() const noexcept
    {
        return _deviceGeneration;
    }
    // Thread-independent identity check against the immutable owner capture. No COM calls, current queue
    // status, GPU completion or permission to use the immediate context is implied. Bind validated its type
    // and device on the owner; workers must not repeat GetType/GetDevice on that live immediate context.
    [[nodiscard]] bool MatchesBindingForWork(std::uint32_t ownerThread, ID3D11Device* device,
                                             ID3D11DeviceContext* immediate) const noexcept
    {
        return _origin && _deviceGeneration && ownerThread && _ownerThread == ownerThread && device && immediate &&
               _device.get() == device && _context.get() == immediate;
    }
    // Owner-only fresh status of the captured device. A barrier refusal is not a status query or acknowledgement.
    [[nodiscard]] HRESULT DeviceStatusOwner() const noexcept;

  private:
    friend class GpuWorkQueue;
    friend class GpuCommandPump;
    friend class GpuCommandStream;
    std::shared_ptr<const std::uint8_t> _origin;
    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _context;
    wil::com_ptr_nothrow<ID3D11DeviceContext4> _context4;
    wil::com_ptr_nothrow<IDXGIDevice2> _dxgiDevice2;
    wil::com_ptr_nothrow<ID3D11Fence> _fence;
    GpuCapabilities::CompletionRoute _route = GpuCapabilities::CompletionRoute::None;
    std::uint64_t _deviceGeneration = 0;
    std::uint32_t _ownerThread = 0;
};

class GpuWorkQueue final : public OwnerWorkPump
{
  public:
    static constexpr std::size_t kCapacity = 8;
    static constexpr std::size_t kMaxInFlight = 2;
    static constexpr std::uint64_t kStagingCapBytes = 64ULL * 1024 * 1024;
    static constexpr unsigned int kMaxReadbackSide = 2048;

    GpuWorkQueue() noexcept;
    ~GpuWorkQueue();
    GpuWorkQueue(const GpuWorkQueue&) = delete;
    GpuWorkQueue& operator=(const GpuWorkQueue&) = delete;
    [[nodiscard]] HRESULT Initialize(JobScheduler& scheduler, OwnerWorkMultiplexer* routes = nullptr) noexcept;
    // The test override exercises the supported event fallback even when this device also supports fences.
    [[nodiscard]] HRESULT Bind(ID3D11Device* device, ID3D11DeviceContext* context,
                               bool forceEventForTest = false) noexcept;
    // Owner thread. The source and request are retained without waiting; box is a BGRA8 mip-0 region of at
    // most 2048 squared. Larger callers traverse tiles under their original job reservation.
    [[nodiscard]] HRESULT EnqueueReadback(ID3D11Texture2D* source, const D3D11_BOX& box, JobTicket ticket,
                                          GpuReadbackReceiver& receiver) noexcept;
    [[nodiscard]] HRESULT EnqueueProducedReadback(unsigned int width, unsigned int height, JobTicket ticket,
                                                  GpuReadbackReceiver& receiver,
                                                  GpuReadbackProducer& producer) noexcept;
    // A structured uint32 buffer with exactly width*height elements on the bound device.
    // Shares the texture queue's slots, staging cap, in-flight limit, events and retirement policy.
    // Source reference survives until the owner records its copy; no Map or conversion happens here.
    [[nodiscard]] HRESULT EnqueueBufferReadback(ID3D11Buffer* source, unsigned int width, unsigned int height,
                                                JobTicket ticket, GpuBufferReadbackReceiver& receiver) noexcept;
    // Owner-only command completion, sharing the fixed readback slots and event wait pool. It creates no
    // staging storage, records no copy, maps no resource and returns no row lease. Admission immediately records one
    // real event on the original context. Refusal under the two-in-flight limit produces no callback; callers await
    // existing release events before offering again.
    [[nodiscard]] HRESULT EnqueueCompletion(JobTicket ticket, GpuCompletionReceiver& receiver) noexcept;
    [[nodiscard]] bool HasCompletionSlot() const noexcept;
    // Capture only into an empty value on the owner before recording dependent work. One immutable stream
    // may acknowledge multiple serial stages. A failed capture leaves the supplied empty value unchanged.
    [[nodiscard]] HRESULT CaptureCompletionContext(GpuCompletionContext& empty) const noexcept;
    // Owner-only borrows from this exact current capture. Queue origin, actual device/context and generation
    // must all still match. No AddRef, query, source capture, resource/command creation or GPU acknowledgment.
    // The original admitted parent retains the strong capture through worker/command/copy/row retirement.
    [[nodiscard]] HRESULT BorrowCurrentContextOwner(const GpuCompletionContext& captured, ID3D11Device*& device,
                                                    ID3D11DeviceContext*& immediate) const noexcept;
    [[nodiscard]] bool HasRetirementCompletionSlot(const GpuCompletionContext& captured) const noexcept;
    // Retirement barrier only: may signal the original healthy stream after Bind/RetireDevice. No useful
    // draw, compute, copy or Map is recorded on that retired stream. S_OK callback is current; E_ABORT is
    // acknowledged retired generation. Failed admission/recording creates no callback and is not proof.
    [[nodiscard]] HRESULT EnqueueRetirementCompletion(const GpuCompletionContext& captured, JobTicket ticket,
                                                      GpuCompletionReceiver& receiver) noexcept;
    // Owner-only bounded readiness query for an already admitted job's next copy. Free slots only:
    // no Map, allocation or context commands, and no reservation. The same owner pass must then enqueue.
    // A suspended source waits on existing release/device notifications while false, never polls the pump.
    [[nodiscard]] bool HasReadbackSlot() const noexcept;
    [[nodiscard]] bool ReleaseMapped(GpuReadbackLease lease) noexcept;
    // Device loss/adapter change retires lent owners. Only an explicit captured retirement barrier may record
    // on a healthy retired context; no useful image work is recorded there.
    void RetireDevice() noexcept;
    // Shutdown order: stop/join wait producers, close the scheduler workers, then release all lent owners.
    void StopNotifications() noexcept;
    void CloseAfterWorkers() noexcept;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;
    [[nodiscard]] std::uint64_t StagingBytes() const noexcept;
    [[nodiscard]] std::size_t InFlightCount() const noexcept;
    [[nodiscard]] std::size_t LentCount() const noexcept;
    [[nodiscard]] std::uint64_t DeviceGeneration() const noexcept;
    [[nodiscard]] GpuCapabilities::CompletionRoute CompletionRoute() const noexcept;
    // Inject only failed nonblocking maps; the following successful map always uses the real context.
    void SetMapBusyForTest(unsigned int attempts, unsigned int afterSuccessfulMaps = 0) noexcept;
    [[nodiscard]] unsigned int MapAttemptsForTest() const noexcept;
    void HoldMapRecoveryForTest(bool hold) noexcept;
    void SetMapRecoveryFailuresForTest(HRESULT rearm, HRESULT enqueue, HRESULT removed) noexcept;
    // Refuse a fixed number of retirement barriers before admitting/recording any event. No callback or
    // completion proof is fabricated. Subsequent offers use the real captured context and event wait. An
    // empty target (generation 0) preserves global behavior; an exact ticket isolates one original admission
    // from unrelated cache barriers.
    void SetRetirementCompletionRefusalsForTest(HRESULT error, unsigned int attempts, JobTicket target = {}) noexcept;
    // Refuse before signaling an actual recorded copy/producer. No completion callback or event is fabricated.
    // Failed copies retain the original request until a real original-context retirement event or joined close.
    void SetCopyCompletionRefusalsForTest(HRESULT error, unsigned int attempts) noexcept;
    [[nodiscard]] GpuReadbackStatistics Statistics() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
