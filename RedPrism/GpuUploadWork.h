#pragma once

#include "GpuCapabilities.h"
#include "GpuMipSlice.h"
#include "GpuResidencyBudget.h"
#include "Jobs.h"

#include <cstddef>
#include <cstdint>
#include <memory>

struct GpuUploadLease final
{
    std::size_t slot = 8;
    std::uint64_t generation = 0;
    std::uint64_t queueIdentity = 0;
    [[nodiscard]] bool operator==(const GpuUploadLease&) const noexcept = default;
};

struct GpuUploadRows final
{
    std::uint8_t* data = nullptr;
    std::size_t capacity = 0;
    std::size_t rowPitch = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    GpuUploadLease lease;
    std::uint64_t deviceGeneration = 0;
};

enum class GpuUploadFill : std::uint8_t
{
    Ready,
    Cancelled,
    Failed,
};

enum class GpuUploadFailurePoint : std::uint8_t
{
    None,
    ImplementationAllocation,
    EventCreation,
    WaitRegistration,
    OwnerRegistration,
    StagingCreation,
    Map,
    MappedPitch,
    WaitRearm,
    RecoverySignal,
    CopySignal,
    MipRecording,
    MipSignal,
};

class GpuUploadReceiver
{
  public:
    virtual ~GpuUploadReceiver() = default;
    // Owner callbacks. A row admission calls WritableRowsReady at most once, then UploadFinished exactly once.
    // Mip admission and failure before mapping call only UploadFinished. The receiver survives the final callback.
    virtual void WritableRowsReady(JobTicket ticket, GpuUploadRows rows) noexcept = 0;
    virtual void UploadFinished(JobTicket ticket, HRESULT result) noexcept = 0;
};

class OwnerWorkMultiplexer;

// A bounded chunk transport. It never publishes display/document owners or prepares pixels. A job owns the
// source and destination reservation; it must acknowledge each mapped loan even after cancellation/removal.
class GpuUploadQueue final : public OwnerWorkPump
{
  public:
    static constexpr std::size_t kCapacity = 8;
    static constexpr std::size_t kMaxInFlight = 2;
    static constexpr std::uint64_t kChunkBytes = 1024ULL * 1024;
    static constexpr std::uint64_t kMaxRowBytes = 128ULL * 1024;
    static constexpr std::uint64_t kStagingCapBytes = 16ULL * 1024 * 1024;

    GpuUploadQueue() noexcept;
    ~GpuUploadQueue();
    GpuUploadQueue(const GpuUploadQueue&) = delete;
    GpuUploadQueue& operator=(const GpuUploadQueue&) = delete;
    [[nodiscard]] HRESULT Initialize(JobScheduler& scheduler, OwnerWorkMultiplexer* routes = nullptr) noexcept;
    [[nodiscard]] HRESULT Bind(ID3D11Device* device, ID3D11DeviceContext* context,
                               bool forceEventForTest = false) noexcept;
    // Optional explicit charge for the reusable staging pool, independent of any adopted destination. Required
    // by the worker creation route. Admit before the first pool allocation; replace only after old storage drains.
    // Refusal leaves ownership with the caller. Includes the fixed staging cap and opaque driver headroom.
    [[nodiscard]] bool AdmitPersistentResidency(GpuResidencyReservation& reservation) noexcept;
    [[nodiscard]] bool HasAdmittedPersistentResidency() const noexcept;
    // Owner only. The precreated destination must belong to the bound device and be DEFAULT, single-sample,
    // BGRA8 or Gray8. One Pump transition maps or records at most one admitted chunk; no owner pixel copying.
    [[nodiscard]] HRESULT Enqueue(ID3D11Texture2D* destination, unsigned int subresource, unsigned int x,
                                  unsigned int y, unsigned int width, unsigned int height, JobTicket ticket,
                                  GpuUploadReceiver& receiver) noexcept;
    // Worker-prepared source/target/pipeline for one adjacent level. The parent must be initialized and its
    // previous completion acknowledged. Records one draw slice per pump, then a real completion event.
    // No mapped rows callback. The admitting destination graph survives this exact-ticket acknowledgement.
    // Pump outside frame recording. It changes the immediate-context pipeline/viewport; every subsequent
    // render or compute consumer must explicitly bind its own required state before recording work.
    [[nodiscard]] HRESULT EnqueueMipSlice(GpuMipSlice slice, JobTicket ticket, GpuUploadReceiver& receiver,
                                          CancelToken cancellation = {}) noexcept;
    // Worker-safe exact-generation acknowledgement. This is the final access to the lent rows. Duplicate,
    // foreign and unpublished leases cannot acknowledge a newer loan. Only atomics and a scheduler wake run.
    [[nodiscard]] bool PublishFilled(GpuUploadLease lease, GpuUploadFill result) noexcept;
    void Cancel(JobTicket ticket) noexcept;
    // After joining the producer, transfer its creation reservation if this exact request still owns a
    // mapped/quarantined context. It stays charged through owner Unmap/storage release at joined shutdown.
    // No match leaves the caller's reservation untouched. No other ticket/generation can take that charge.
    [[nodiscard]] bool RetainRetiredResidency(JobTicket ticket, GpuResidencyReservation& reservation) noexcept;
    void RetireDevice() noexcept;
    void StopNotifications() noexcept;
    // Must follow joining every filling worker. Acknowledges Unmap on the retained original contexts, even
    // after removal; callbacks are suppressed during shutdown. It never touches another generation's rows.
    void CloseAfterWorkers() noexcept;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;
    [[nodiscard]] std::uint64_t StagingBytes() const noexcept;
    [[nodiscard]] std::uint64_t RetiredResidencyBytes() const noexcept;
    [[nodiscard]] std::uint64_t RecordedMipSlices() const noexcept;
    [[nodiscard]] std::size_t InFlightCount() const noexcept;
    [[nodiscard]] std::size_t LentCount() const noexcept;
    [[nodiscard]] std::uint64_t DeviceGeneration() const noexcept;
    [[nodiscard]] GpuCapabilities::CompletionRoute CompletionRoute() const noexcept;
    void SetMapBusyForTest(unsigned int attempts) noexcept;
    [[nodiscard]] unsigned int MapAttemptsForTest() const noexcept;
    // Owner-only one-shot fault injection at the named real operation. Skip counts matching attempts only;
    // production leaves None. ImplementationAllocation exercises the existing bad_alloc boundary. MappedPitch
    // still performs a real Map and Unmap; its injected invalid pitch cannot escape into borrowed rows.
    void SetFailureForTest(GpuUploadFailurePoint point, HRESULT error = E_FAIL, unsigned int skip = 0) noexcept;
    [[nodiscard]] unsigned int FailureHitsForTest() const noexcept;

  private:
    [[nodiscard]] bool FailAt(GpuUploadFailurePoint point, HRESULT& result) noexcept;
    struct Impl;
    std::unique_ptr<Impl> _impl;
    GpuUploadFailurePoint _failurePoint = GpuUploadFailurePoint::None;
    HRESULT _failureResult = E_FAIL;
    unsigned int _failureSkip = 0;
    unsigned int _failureHits = 0;
};
