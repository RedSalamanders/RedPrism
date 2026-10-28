#pragma once

#include "DeferredRelease.h"
#include "Document.h"
#include "GpuWork.h"
#include "OwnerWorkMultiplexer.h"
#include "PreparedLassoDraftGpu.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

struct LassoDraftDisplayKey final
{
    DocumentReadAuthority document;
    BoundedLassoGesture::Key draft;
    const void* arenaIdentity = nullptr; // The admitted cell retains the immutable point capture.
    std::uint64_t pointCount = 0;
    LassoKind kind = LassoKind::Freehand;
    SelectionMode mode = SelectionMode::New;
    bool antialiased = true;
    std::optional<Vec2> cursor;
    std::optional<Vec2> anchor;
    PreparedLassoDraftRaster::GridKey grid;
    std::uint64_t deviceGeneration = 0;
    std::uint64_t viewEpoch = 0; // Host-owned monotonic view authority; exhaustion must refuse before mutation.
    [[nodiscard]] bool operator==(const LassoDraftDisplayKey&) const noexcept = default;
};

struct LassoDraftDisplayDescriptor final
{
    LassoDraftDisplayKey key;
    const BoundedLassoGesture::Display* source = nullptr; // Borrowed until complete original admission.
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    bool software = false;
};

class LassoDraftDisplayClient
{
  public:
    virtual ~LassoDraftDisplayClient() = default;
    [[nodiscard]] virtual bool DescribeLassoDraftOwner(LassoDraftDisplayDescriptor&) noexcept = 0;
    // Nonreentrant bounded invalidation only. No ownership, callback authority or frame borrow is transferred.
    virtual void LassoDraftChangedOwner() noexcept = 0;
    virtual void LassoDraftFailedOwner(LassoDraftDisplayKey, HRESULT) noexcept = 0;
};

// One cached graph and one replacement/retiring graph. Source/grid/DPI/view/device changes exclude stale
// frame borrows immediately. CPU rasterization and immutable GPU initialization run on the original job;
// cached graphs count as resident memory, never pending useful work. Failed barriers stay charged/dormant.
// This child never registers a multiplexer route. The existing overlay owner pumps selection and draft
// children fairly, keeping the audited owner membership fixed. Startup enables the original release sink.
// The parent stops frames before StopOwner and uses
// the exact joined JobScheduler::Close owner/final-worker hooks before CloseAfterWorkersOwner.
// The embedded deferred-release cells intentionally impose their cache-line alignment on this owner.
#pragma warning(push)
#pragma warning(disable : 4324)
class LassoDraftDisplayController final : public OwnerWorkPump
{
  public:
    LassoDraftDisplayController() noexcept;
    ~LassoDraftDisplayController();
    LassoDraftDisplayController(const LassoDraftDisplayController&) = delete;
    LassoDraftDisplayController& operator=(const LassoDraftDisplayController&) = delete;
    [[nodiscard]] HRESULT InitializeOwner(JobScheduler&, GpuWorkQueue&, GpuResidencyBudget&,
                                          LassoDraftDisplayClient&) noexcept;
    void RequestOwner() noexcept;
    void RetryOwner() noexcept;
    void SetSuspendedOwner(bool suspended) noexcept;
    // Exact current key and actual current context only. No source walk, creation, copying, allocation or wait.
    [[nodiscard]] PreparedLassoDraftGpu::Views BorrowFrameOwner() const noexcept;
    void StopOwner() noexcept;
    void JoinedShutdownOwner() noexcept;
    void RetireShutdownForWork() noexcept;
    [[nodiscard]] bool CloseAfterWorkersOwner() noexcept;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;

  private:
    class DraftJob;
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
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
    struct Cell final : DeferredReleaseNode, GpuCompletionReceiver
    {
        LassoDraftDisplayController* owner = nullptr;
        PreparedLassoDraftGpu gpu;
        LassoDraftDisplayKey key;
        PreparedLassoDraftRaster::MemoryPlan plan;
        BoundedLassoGesture::Display source;
        GpuCompletionContext completion;
        GpuResidencyReservation initialGpu;
        RetainedMemoryTicket cpu;
        SharedRetainedMemoryLease shutdownCpu;
        JobTicket ticket;
        DraftJob* job = nullptr;
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

#pragma warning(pop)
    [[nodiscard]] bool IsOwner() const noexcept;
    [[nodiscard]] bool DescribeOwner(LassoDraftDisplayDescriptor&) const noexcept;
    [[nodiscard]] bool CurrentOwner(const Cell&, LassoDraftDisplayDescriptor* = nullptr) const noexcept;
    void WakeOwner() noexcept;
    void BeginOwner(const LassoDraftDisplayDescriptor&) noexcept;
    void RetireCacheOwner(Cell&) noexcept;
    void DispatchRetirementOwner(Cell&) noexcept;
    void FinishOwner(Cell&, bool closed = false) noexcept;
    static void RetireCellForWork(DeferredReleaseNode*) noexcept;
    void RetireForWork(Cell&) noexcept;
    std::array<Cell, 2> _cells;
    JobScheduler* _jobs = nullptr;
    GpuWorkQueue* _queue = nullptr;
    GpuResidencyBudget* _budget = nullptr;
    LassoDraftDisplayClient* _client = nullptr;
    Cell* _published = nullptr;
    std::optional<LassoDraftDisplayKey> _failed;
    std::uint32_t _ownerThread = 0;
    bool _ready = false;
    bool _requested = false;
    bool _suspended = false;
    bool _stopped = false;
    bool _joined = false;
};
#pragma warning(pop)
