#pragma once

#include "GpuCapabilities.h"
#include "Jobs.h"

#include <cstddef>
#include <cstdint>
#include <memory>

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
    [[nodiscard]] HRESULT Initialize(JobScheduler& scheduler) noexcept;
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
    [[nodiscard]] bool ReleaseMapped(GpuReadbackLease lease) noexcept;
    // Device loss/adapter change retires lent owners. No commands are recorded on a retired context.
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
    [[nodiscard]] GpuReadbackStatistics Statistics() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
