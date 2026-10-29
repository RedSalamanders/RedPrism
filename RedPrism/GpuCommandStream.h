#pragma once

#include "Jobs.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <d3d11.h>
#include <memory>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

class GpuWorkQueue;
class GpuCompletionContext;

struct GpuCommandLease final
{
    JobTicket source;
    std::uint64_t deviceGeneration = 0;
    std::uint64_t ordinal = 0;
    std::size_t slot = 2;
    [[nodiscard]] bool operator==(const GpuCommandLease&) const noexcept = default;
};

// One private recording producer and one immediate-context owner. This transport never records/executes GPU
// work, mutates documents, owns a thread, or acknowledges a GPU use. The parent owns complete graph admission,
// producer lifetime, command-size limits and actual original-stream completion. Two published slots plus the
// producer's incoming command/context are at most three command-shadow owners; all three belong in admission.
class GpuCommandStream final
{
  public:
    static constexpr std::size_t kCapacity = 2;
    struct Ready final
    {
        ID3D11CommandList* command = nullptr; // Borrowed through the matching actual acknowledgment.
        GpuCommandLease lease;
    };
    GpuCommandStream() = default;
    ~GpuCommandStream();
    GpuCommandStream(const GpuCommandStream&) = delete;
    GpuCommandStream& operator=(const GpuCommandStream&) = delete;
    [[nodiscard]] HRESULT InitializeOwner(JobScheduler& scheduler, JobTicket source,
                                          std::uint64_t deviceGeneration) noexcept;
    // Utility opening: bind the actual original queue identity before any worker or command.
    // The numeric-only overload remains for existing callers and cannot authorize device recovery.
    [[nodiscard]] HRESULT InitializeOwner(JobScheduler& scheduler, JobTicket source, const GpuWorkQueue& queue,
                                          const GpuCompletionContext& current) noexcept;
    // One producer only. Capacity blocks this dedicated recorder on events, never the general compute pool.
    [[nodiscard]] bool CanRecordForWork() const noexcept;
    // Fresh private recording stage only; the original joined RestartOwner advances this epoch.
    [[nodiscard]] std::uint64_t ProductionEpochForWork() const noexcept;
    [[nodiscard]] bool BelongsToOwner(const JobScheduler& scheduler, JobTicket source,
                                      std::uint64_t deviceGeneration) const noexcept;
    [[nodiscard]] HRESULT PublishForWork(wil::com_ptr_nothrow<ID3D11CommandList> command,
                                         CancelToken cancellation = {}) noexcept;
    // Last useful producer action, including cancellation/failure. Publishes result, then owner wake; no later
    // Publish. The parent's joined callback/stage acknowledgment remains necessary before retirement/destruction.
    void FinishProducerForWork(HRESULT result) noexcept;
    [[nodiscard]] Ready NextOwner() const noexcept;
    // Call before ExecuteCommandList. After this transition even cancellation cannot drop the slot.
    [[nodiscard]] bool BeginExecuteOwner(GpuCommandLease lease) noexcept;
    // Call only after an actual original-stream completion, confirmed original-device removal or joined shutdown.
    // A failed admission/record/signal and a requested cancellation are never such an acknowledgment.
    [[nodiscard]] bool AcknowledgeOwner(GpuCommandLease lease) noexcept;
    void RequestStopOwner() noexcept;
    [[nodiscard]] bool ProducerFinishedOwner(HRESULT& result) const noexcept;
    [[nodiscard]] bool CanRetireOwner() const noexcept;
    // Parent has separately acknowledged the producer callback, and excludes future recording/execution.
    [[nodiscard]] bool AuthorizeRetirementOwner() noexcept;
    // Destruction-only Close owner hook after every scheduler producer has actually joined. An initialized
    // source whose recorder never ran has no producer/thread/commands to finish. A started/unjoined source
    // cannot use this path. Marks only cancelled-empty retirement, never successful useful completion.
    [[nodiscard]] bool AcknowledgeEmptyJoinedShutdownOwner() noexcept;
    // Owner has observed producer completion and all submitted uses have acknowledged. Worker drops remaining
    // unsubmitted/acknowledged commands; the scheduler/parent retains this object until that worker acknowledges.
    [[nodiscard]] bool RetireForWork() noexcept;
    // Same admitted original parent, in its successful joined retirement callback only. Reuse the two
    // existing events/slots for another recorder stage without resetting command ordinals. Failed/stopped,
    // unjoined, foreign or exhausted streams refuse; late previous leases never name a new command.
    [[nodiscard]] HRESULT RestartOwner() noexcept;
    // Device recovery only after original transport retirement and a successful original joined stage.
    // Validate an actual current captured queue context and a strictly newer generation. Keep source,
    // command ordinals and both events; this acknowledges no copy/map, graph destruction or GPU charge.
    // Parent separately proves those boundaries and never calls this after CPU/external-write handoff.
    [[nodiscard]] HRESULT RestartDeviceOwner(const GpuWorkQueue& queue, const GpuCompletionContext& current) noexcept;
    [[nodiscard]] bool WasBackpressured() const noexcept
    {
        return _backpressured.load(std::memory_order_acquire);
    }

  private:
    enum class State : std::uint8_t
    {
        Free,
        Ready,
        Executing,
        Acknowledged
    };
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
    struct Slot final
    {
        std::atomic<State> state{State::Free};
        wil::com_ptr_nothrow<ID3D11CommandList> command;
        GpuCommandLease lease;
    };
#pragma warning(pop)

    [[nodiscard]] bool IsOwner() const noexcept;
    [[nodiscard]] HRESULT RestartOwnerImpl(std::uint64_t nextGeneration, bool deviceReplacement) noexcept;
    [[nodiscard]] bool Matches(const Slot& slot, GpuCommandLease lease) const noexcept;
    [[nodiscard]] bool IsStopped() const noexcept;
    std::array<Slot, kCapacity> _slots;
    JobScheduler* _scheduler = nullptr; // Parent excludes scheduler Close until this producer finishes.
    JobTicket _source;
    std::shared_ptr<const std::uint8_t> _queueOrigin; // Identity only; owns no GPU endpoint or command.
    std::uint64_t _deviceGeneration = 0;
    std::uint64_t _produced = 0; // Single producer only, read on owner after _finished acquire.
    std::uint64_t _consumed = 0; // Owner only; callbacks acknowledge the exact ordinal.
    std::atomic<std::uint32_t> _producerThread{0};
    std::atomic<std::uint64_t> _productionEpoch{0};
    std::uint32_t _ownerThread = 0;
    std::atomic<bool> _finished{false};
    std::atomic<bool> _retired{false};
    std::atomic<bool> _retirementAuthorized{false};
    std::atomic<bool> _backpressured{false};
    HRESULT _result = E_PENDING; // Published by _finished.
    wil::unique_handle _available;
    wil::unique_handle _stopped;
};
