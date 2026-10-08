#pragma once

#include "GpuCommandPump.h"
#include "GpuResidencyBudget.h"
#include "OwnerWorkMultiplexer.h"
#include "PendingDisplaySource.h"
#include "RasterResidencyManifestWork.h"
#include "RecordedDisplayGraph.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <atomic>
#include <memory>
#include <optional>

enum class DisplaySourcePurpose : std::uint8_t
{
    Committed,
    Transform,
    Color,
    Adjustment
};

struct CommittedDisplayKey final
{
    DocumentReadAuthority document;
    std::uint64_t deviceGeneration = 0;
    DisplaySourcePurpose purpose = DisplaySourcePurpose::Committed;
    std::uint64_t opening = 0;
    std::uint64_t publication = 0;
    const void* sourceIdentity = nullptr; // A published root holds this immutable source, never an address-only cache.
    const void* lookupIdentity = nullptr; // Its original admitted root holds this lookup.
    [[nodiscard]] bool operator==(const CommittedDisplayKey&) const noexcept = default;
};

// Descriptor-only borrows. No source/device/context ownership is captured until the original admission stage
// acknowledges. The client supplies its actual device generation/driver kind, never a guessed adapter budget.
struct CommittedDisplayDescriptor final
{
    CommittedDisplayKey key;
    const CanvasDocument* document = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    bool software = false;
    std::optional<PendingDisplaySource> pending; // Scalar borrows only; copied owners follow original CPU admission.
};

class CommittedDisplayController;

// One bounded external recorder borrow per cached cell. This guard owns no additional graph/reference.
// Its original controller retains all cache charges until the consumer's actual recording join.
// Release explicitly on the owner after that join, including cancelled and joined-shutdown paths.
// Destruction does not perform an implicit owner action from an arbitrary worker.
class CommittedDisplaySeedLease final
{
  public:
    CommittedDisplaySeedLease() noexcept = default;
    ~CommittedDisplaySeedLease();
    CommittedDisplaySeedLease(const CommittedDisplaySeedLease&) = delete;
    CommittedDisplaySeedLease& operator=(const CommittedDisplaySeedLease&) = delete;
    CommittedDisplaySeedLease(CommittedDisplaySeedLease&&) = delete;
    CommittedDisplaySeedLease& operator=(CommittedDisplaySeedLease&&) = delete;
    [[nodiscard]] const RecordedDisplayGraph* GraphForWork() const noexcept
    {
        return _graph;
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return _controller != nullptr;
    }

  private:
    friend class CommittedDisplayController;
    CommittedDisplayController* _controller = nullptr;
    const RecordedDisplayGraph* _graph = nullptr;
    std::size_t _cell = 2;
    std::uint64_t _generation = 0;
};

class CommittedDisplayClient
{
  public:
    virtual ~CommittedDisplayClient() = default;
    [[nodiscard]] virtual bool DescribeCommittedDisplayOwner(CommittedDisplayDescriptor& descriptor) noexcept = 0;
    // Bounded pointer adoption only. The controller still owns the graph through its last frame/seed borrow.
    virtual void CommittedDisplayPublishedOwner(RecordedDisplayGraph* graph) noexcept = 0;
    virtual void CommittedDisplayFailedOwner(CommittedDisplayKey key, HRESULT failure) noexcept = 0;
};

class PreparedCommittedDisplay final
{
    struct Storage;
    friend class CommittedDisplayController;

  public:
    // Worker factory under the original complete CPU admission. Captures no owner/controller pointer.
    [[nodiscard]] static std::unique_ptr<PreparedCommittedDisplay> PrepareForWork(
        std::uint32_t ownerThread, CanvasDocument source, RasterResidencyCpuAdmission admission, std::uint64_t cpuBound,
        ID3D11Device* device, ID3D11DeviceContext* originalContext, GpuCompletionContext captured,
        CancelToken cancellation);
    ~PreparedCommittedDisplay();
    PreparedCommittedDisplay(const PreparedCommittedDisplay&) = delete;
    PreparedCommittedDisplay& operator=(const PreparedCommittedDisplay&) = delete;
    [[nodiscard]] const RasterResidencyManifestWork* ManifestAfterJoin() const noexcept;
    // Actual original GPU admission precedes this dedicated recorder call. The caller owns stream retirement.
    [[nodiscard]] HRESULT RecordForWork(GpuCommandStream& stream, CancelToken cancellation,
                                        const RecordedDisplayGraph* seed = nullptr) noexcept;
    [[nodiscard]] bool BindMemoryOwner(const std::shared_ptr<const SharedPublicationMemory>& memory) noexcept;
    [[nodiscard]] HRESULT PublishOwner() noexcept;
    // Failed parent: exclude future frame use on owner, acknowledge all original submitted uses, retire worker.
    [[nodiscard]] bool DetachOwner() noexcept;
    [[nodiscard]] bool RetireForWork() noexcept;

  private:
    explicit PreparedCommittedDisplay(Storage* storage) noexcept : _storage(storage) {}
    Storage* _storage = nullptr; // Consumed into the controller or explicitly retired after real join/barrier.
};

class CommittedDisplayPublicationSlot final
{
  public:
    CommittedDisplayPublicationSlot() noexcept = default;
    ~CommittedDisplayPublicationSlot();
    CommittedDisplayPublicationSlot(const CommittedDisplayPublicationSlot&) = delete;
    CommittedDisplayPublicationSlot& operator=(const CommittedDisplayPublicationSlot&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return _owner != nullptr;
    }

  private:
    friend class CommittedDisplayController;
    CommittedDisplayController* _owner = nullptr;
    std::size_t _cell = 2;
    std::uint64_t _generation = 0;
    JobTicket _original;
};

// At most one cache and one admitted replacement/retiring root per instance. Pending instances have distinct
// keys and preview admission; they cannot provide a committed seed or accept original-parent final publication.
// Shared transport lifetime is closed by its application owner after ALL instances detach.
class CommittedDisplayController final : public OwnerWorkPump
{
  public:
    CommittedDisplayController() noexcept;
    ~CommittedDisplayController();
    CommittedDisplayController(const CommittedDisplayController&) = delete;
    CommittedDisplayController& operator=(const CommittedDisplayController&) = delete;
    [[nodiscard]] HRESULT InitializeOwner(JobScheduler& jobs, GpuWorkQueue& queue, GpuCommandPump& commands,
                                          OwnerWorkMultiplexer& routes, CommittedDisplayClient& client,
                                          GpuResidencyBudget* sharedBudget = nullptr, bool pending = false,
                                          bool ownsCommandPump = true) noexcept;
    // Bounded current-opening validation immediately before a frame; no fallback derivation or GPU work.
    [[nodiscard]] RecordedDisplayGraph* GraphForFrameOwner() const noexcept;
    // Visibility transition only. Suspension excludes pixels immediately, cancels useful preparation and
    // retires existing roots through their original acknowledgements. Repeated suspension does not rearm.
    // Resume requests the current immutable document; no hidden replacement or synchronous fallback.
    void SetSuspendedOwner(bool suspended) noexcept;
    // Exact current published key only; descriptor/actual context and cache state must still match.
    // No allocation/query/charge transfer. A second external borrower refuses locally.
    [[nodiscard]] bool AcquirePublishedSeedOwner(CommittedDisplayKey key, CommittedDisplaySeedLease& lease) noexcept;
    [[nodiscard]] bool ReleasePublishedSeedOwner(CommittedDisplaySeedLease& lease) noexcept;
    // Preclaim one existing cell for an admitted original parent. No younger display job or ordinal.
    [[nodiscard]] bool ClaimPublicationSlotOwner(JobTicket original, CommittedDisplayPublicationSlot& slot) noexcept;
    // Parent owns the returned charge through every real GPU/worker acknowledgement, then moves it to Adopt.
    [[nodiscard]] HRESULT ReservePublicationGpuOwner(CommittedDisplayPublicationSlot& slot, ID3D11Device* device,
                                                     bool software, std::uint64_t bytes,
                                                     GpuResidencyReservation& reservation) noexcept;
    [[nodiscard]] bool PublicationSlotCurrentOwner(const CommittedDisplayPublicationSlot& slot) const noexcept;
    // Failed/stale parent releases only after it joins and retires its own graph; this never destroys it.
    [[nodiscard]] bool ReleasePublicationSlotOwner(CommittedDisplayPublicationSlot& slot) noexcept;
    // Before the bounded, nonreentrant document/history swap: verify the exact prepared final root,
    // original charge and device. This grants no document/history authority and consumes no owners.
    [[nodiscard]] bool CanAdoptPublicationOwner(const CommittedDisplayPublicationSlot& slot,
                                                const PreparedCommittedDisplay& prepared,
                                                const GpuResidencyReservation& reservation,
                                                const CanvasDocument& finalDocument) const noexcept;
    // Root already published privately on the original context; document/history and rows are current.
    // All validation precedes pointer adoption. On refusal neither root, reservation nor slot is consumed.
    [[nodiscard]] bool AdoptPublicationOwner(CommittedDisplayPublicationSlot& slot, PreparedCommittedDisplay& prepared,
                                             GpuResidencyReservation& reservation) noexcept;
    void RequestOwner() noexcept;
    void RetryOwner() noexcept;
    void DeviceChangedOwner() noexcept;
    void StopOwner() noexcept;
    // In actual JobScheduler::Close hooks: owner after all producers/waits join, worker before memory refund.
    // The queue must remain alive until JoinedShutdownOwner; no ordinary receiver callback runs here.
    void JoinedShutdownOwner() noexcept;
    void RetireShutdownForWork() noexcept;
    [[nodiscard]] bool CloseAfterWorkersOwner() noexcept;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;

  private:
    friend class PreparedCommittedDisplay;
    class DisplayJob;
    using Root = PreparedCommittedDisplay::Storage;
    enum class Phase
    {
        Empty,
        Publication,
        Active,
        Cached,
        RetiringGpu,
        RetiringWorker
    };
    struct Cell final : GpuCommandSourceReceiver, GpuCompletionReceiver
    {
        CommittedDisplayController* owner = nullptr;
        Root* root = nullptr;      // Sole owning pointer; final destruction only on an acknowledged worker.
        DisplayJob* job = nullptr; // Borrowed until original final callback/Close wrapper destruction.
        Cell* seed = nullptr;      // Owner-only borrow guard, never dereference a concurrently retired worker payload.
        CommittedDisplayKey key;
        JobTicket ticket;
        RetainedMemoryTicket retained;
        GpuResidencyReservation gpu;
        GpuCommandSourceLease source;
        Phase phase = Phase::Empty;
        std::atomic<bool> workerRetired{false};
        HRESULT terminal = S_OK;
        std::uint64_t externalBorrowGeneration = 0;
        bool externalBorrow = false;
        std::uint64_t publicationGeneration = 0;
        bool recordingJoined = false;
        bool cleanupDispatched = false;
        bool retirementRequested = false;
        bool waiting = false;
        bool quarantine = false;
        bool recoveryArmed = false;
        void CommandAcknowledgedOwner(JobTicket, GpuCommandLease, HRESULT) noexcept override;
        void CommandSourceFailedOwner(JobTicket, HRESULT) noexcept override;
        void GpuCompletionReady(JobTicket, std::uint64_t, HRESULT) noexcept override;
    };
    [[nodiscard]] bool IsOwner() const noexcept;
    [[nodiscard]] Cell* PublicationCellOwner(const CommittedDisplayPublicationSlot& slot) noexcept;
    [[nodiscard]] const Cell* PublicationCellOwner(const CommittedDisplayPublicationSlot& slot) const noexcept;
    [[nodiscard]] bool CurrentOwner(const Cell& cell, CommittedDisplayDescriptor* descriptor = nullptr) const noexcept;
    [[nodiscard]] bool SeedBorrowedOwner(const Cell& cell) const noexcept;
    void WakeOwner() noexcept;
    void BeginOwner(const CommittedDisplayDescriptor& descriptor) noexcept;
    [[nodiscard]] HRESULT AdmitRecordingOwner(Cell& cell, const CommittedDisplayDescriptor& descriptor) noexcept;
    void RetireCacheOwner(Cell& cell) noexcept;
    void RetireCacheForWorkOwner(Cell& cell) noexcept;
    void FinishCellOwner(Cell& cell) noexcept;
    void PublishedOwner(Cell& cell) noexcept;
    static void DestroyRootForWork(DeferredReleaseNode* node) noexcept;
    std::array<Cell, 2> _cells;
    GpuResidencyBudget _ownedGpuBudget;
    GpuResidencyBudget* _gpuBudget = &_ownedGpuBudget; // Borrowed shared ledger outlives every graph.
    JobScheduler* _jobs = nullptr;
    GpuWorkQueue* _queue = nullptr;
    GpuCommandPump* _commands = nullptr;
    OwnerWorkMultiplexer* _routes = nullptr;
    CommittedDisplayClient* _client = nullptr;
    Cell* _published = nullptr;
    std::optional<CommittedDisplayKey> _failed;
    std::uint32_t _ownerThread = 0;
    bool _pending = false;
    bool _ownsCommandPump = true;
    bool _requested = false;
    bool _ready = false;
    bool _suspended = false;
    bool _stopped = false;
    bool _joined = false;
};
