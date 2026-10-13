#pragma once

#include "DeferredRelease.h"
#include "Document.h"
#include "GpuWork.h"
#include "OwnerWorkMultiplexer.h"
#include "PreparedSelectionOutlineGpu.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

struct SelectionOutlineDisplayKey final
{
    DocumentReadAuthority document;
    const void* sourceIdentity = nullptr; // The admitted cell holds the actual immutable selection.
    PreparedSelectionOutlineRaster::GridKey grid;
    std::uint64_t deviceGeneration = 0;
    std::uint64_t viewEpoch = 0; // Host-owned monotonic view authority; exhaustion must refuse before mutation.
    [[nodiscard]] bool operator==(const SelectionOutlineDisplayKey&) const noexcept = default;
};

struct SelectionOutlineDisplayDescriptor final
{
    SelectionOutlineDisplayKey key;
    const SelectionLoops* source = nullptr; // Borrowed only until original CPU/GPU admission acknowledges.
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    bool software = false;
};

class SelectionOutlineDisplayClient
{
  public:
    virtual ~SelectionOutlineDisplayClient() = default;
    [[nodiscard]] virtual bool DescribeSelectionOutlineOwner(SelectionOutlineDisplayDescriptor&) noexcept = 0;
    // Nonreentrant bounded invalidation only. No ownership, callback authority or frame borrow is transferred.
    virtual void SelectionOutlineChangedOwner() noexcept = 0;
    virtual void SelectionOutlineFailedOwner(SelectionOutlineDisplayKey, HRESULT) noexcept = 0;
};

// One cached graph and one replacement/retiring graph. Source/grid/DPI/view/device changes exclude stale
// frame borrows immediately. CPU rasterization and immutable GPU initialization run on the original job;
// cached graphs count as resident memory, never pending useful work. Failed barriers stay charged/dormant.
// Startup enables the original scheduler release sink. The parent stops frames before StopOwner and uses
// the exact joined JobScheduler::Close owner/final-worker hooks before CloseAfterWorkersOwner.
// The embedded deferred-release cells intentionally impose their cache-line alignment on this owner.
#pragma warning(push)
#pragma warning(disable : 4324)
class SelectionOutlineDisplayController final : public OwnerWorkPump
{
  public:
    SelectionOutlineDisplayController() noexcept;
    ~SelectionOutlineDisplayController();
    SelectionOutlineDisplayController(const SelectionOutlineDisplayController&) = delete;
    SelectionOutlineDisplayController& operator=(const SelectionOutlineDisplayController&) = delete;
    [[nodiscard]] HRESULT InitializeOwner(JobScheduler&, GpuWorkQueue&, OwnerWorkMultiplexer&, GpuResidencyBudget&,
                                          SelectionOutlineDisplayClient&) noexcept;
    void RequestOwner() noexcept;
    void RetryOwner() noexcept;
    void SetSuspendedOwner(bool suspended) noexcept;
    // Exact current key and actual current context only. No source walk, creation, copying, allocation or wait.
    [[nodiscard]] PreparedSelectionOutlineGpu::Views BorrowFrameOwner(unsigned int antPhase) const noexcept;
    void StopOwner() noexcept;
    void JoinedShutdownOwner() noexcept;
    void RetireShutdownForWork() noexcept;
    [[nodiscard]] bool CloseAfterWorkersOwner() noexcept;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;

  private:
    class OutlineJob;
    enum class Phase : std::uint8_t
    {
        Empty,
        Active,
        Cached,
        RetiringGpu,
        RetiringWorker
    };
#pragma warning(push)
#pragma warning(disable : 4324)
    struct Cell final : DeferredReleaseNode, GpuCompletionReceiver
    {
        SelectionOutlineDisplayController* owner = nullptr;
        PreparedSelectionOutlineGpu gpu;
        SelectionOutlineDisplayKey key;
        PreparedSelectionOutlineRaster::MemoryPlan plan;
        SelectionLoops source;
        GpuCompletionContext completion;
        GpuResidencyReservation initialGpu;
        RetainedMemoryTicket cpu;
        SharedRetainedMemoryLease shutdownCpu;
        JobTicket ticket;
        OutlineJob* job = nullptr;
        std::atomic<bool> workerRetired{false};
        Phase phase = Phase::Empty;
        HRESULT terminal = S_OK;
        bool capturedGpu = false;
        bool detached = false;
        bool waiting = false;
        bool recoveryArmed = false;
        bool deferredWorker = false;
        void GpuCompletionReady(JobTicket, std::uint64_t, HRESULT) noexcept override;
    };
#pragma warning(pop)
    [[nodiscard]] bool IsOwner() const noexcept;
    [[nodiscard]] bool DescribeOwner(SelectionOutlineDisplayDescriptor&) const noexcept;
    [[nodiscard]] bool CurrentOwner(const Cell&, SelectionOutlineDisplayDescriptor* = nullptr) const noexcept;
    void WakeOwner() noexcept;
    void BeginOwner(const SelectionOutlineDisplayDescriptor&) noexcept;
    void RetireCacheOwner(Cell&) noexcept;
    void DispatchRetirementOwner(Cell&) noexcept;
    void FinishOwner(Cell&, bool closed = false) noexcept;
    static void RetireCellForWork(DeferredReleaseNode*) noexcept;
    void RetireForWork(Cell&) noexcept;
    std::array<Cell, 2> _cells;
    JobScheduler* _jobs = nullptr;
    GpuWorkQueue* _queue = nullptr;
    GpuResidencyBudget* _budget = nullptr;
    OwnerWorkMultiplexer* _routes = nullptr;
    SelectionOutlineDisplayClient* _client = nullptr;
    Cell* _published = nullptr;
    std::optional<SelectionOutlineDisplayKey> _failed;
    std::uint32_t _ownerThread = 0;
    bool _ready = false;
    bool _requested = false;
    bool _suspended = false;
    bool _stopped = false;
    bool _joined = false;
};
#pragma warning(pop)
