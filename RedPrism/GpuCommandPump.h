#pragma once

#include "GpuCommandStream.h"
#include "GpuWork.h"

#include <array>

class OwnerWorkMultiplexer;

struct GpuCommandSourceLease final
{
    std::size_t slot = 2;
    std::uint64_t generation = 0;
    [[nodiscard]] bool operator==(const GpuCommandSourceLease&) const noexcept = default;
};

// A retryable utility may stop recording without permanently cancelling its original scheduler token.
// This choice supplies no acknowledgement, restart/publication authority or admission refund.
// Real user/shutdown cancellation still prevents useful continuation. Default consumers keep cancellation.
enum class GpuCommandFailureDisposition : std::uint8_t
{
    CancelOriginal,
    RetainOriginalForRecovery
};

class GpuCommandSourceReceiver
{
  public:
    virtual ~GpuCommandSourceReceiver() = default;
    // Reports an original GPU use acknowledged by a real queue callback, confirmed original-device removal,
    // or joined shutdown. S_OK additionally means current. Cancellation/failed enqueue never calls this alone.
    virtual void CommandAcknowledgedOwner(JobTicket source, GpuCommandLease command, HRESULT result) noexcept = 0;
    // Local recording/execution failure. It conveys no GPU acknowledgement or retirement authority.
    virtual void CommandSourceFailedOwner(JobTicket source, HRESULT result) noexcept = 0;
    // Both callbacks may mark state/notify/resume, but never detach, destroy receivers, close or pump recursively.
};

// Stable startup-registered owner pump: at most two borrowers, one bounded ExecuteCommandList per pump.
// The parent's original admitted record/receiver/stream outlive every callback and this registration.
// Parent charges all graph/three shadow owners, joins the recorder, and retires transferred capture references
// on a worker before refunding admission. This object owns no worker, swap chain, Present or document.
class GpuCommandPump final : public OwnerWorkPump
{
  public:
    static constexpr std::size_t kCapacity = 2;
    GpuCommandPump() = default;
    ~GpuCommandPump();
    GpuCommandPump(const GpuCommandPump&) = delete;
    GpuCommandPump& operator=(const GpuCommandPump&) = delete;
    [[nodiscard]] HRESULT InitializeOwner(JobScheduler& scheduler, GpuWorkQueue& queue,
                                          OwnerWorkMultiplexer& routes) noexcept;
    // After opening producer acknowledgement, before recording resumes. Capture binds the queue's actual
    // original immediate context/device, not a caller-supplied context or generation.
    [[nodiscard]] HRESULT AttachOwner(
        GpuCommandStream& stream, JobTicket source, GpuCommandSourceReceiver& receiver, GpuCommandSourceLease& empty,
        GpuCommandFailureDisposition failure = GpuCommandFailureDisposition::CancelOriginal) noexcept;
    // Owner-only free source capacity. No capture, query, command, allocation or attachment occurs here.
    // A suspended original parent waits on existing release notifications while false, never polls.
    [[nodiscard]] bool HasSourceSlotOwner() const noexcept;
    // Exclude future execution. Pending GPU uses/strong references remain retained until acknowledgement.
    [[nodiscard]] bool StopSourceOwner(GpuCommandSourceLease lease) noexcept;
    // Parent has joined the producer. Transfer every captured reference into an empty worker-retired owner;
    // never silently drop the potentially last device/context reference on the UI thread.
    [[nodiscard]] bool DetachOwner(GpuCommandSourceLease lease, GpuCompletionContext& empty) noexcept;
    // Call only on an actual device-generation change. At most one recovery attempt is enabled by each change.
    void DeviceChangedOwner() noexcept;
    void StopOwner() noexcept;
    // All command/wait producers joined and future GPU use excluded by the parent's shutdown sequence.
    // Acknowledges uncertain slots for destruction only; this never qualifies their pixels as completed.
    void AcknowledgeJoinedShutdownOwner() noexcept;
    [[nodiscard]] bool CloseOwner() noexcept;
    // One-shot test refusals after actual Execute, and before a captured retirement barrier. They never
    // substitute a completion signal or acknowledge the original use. Production leaves both S_OK.
    void SetCompletionRefusalsForTest(HRESULT first, HRESULT recovery) noexcept;
    // Read-only fixed-array observations. Refuse off-owner and during callbacks; never acknowledge a use.
    [[nodiscard]] std::size_t AttachedSourceCountOwnerForTest() const noexcept;
    [[nodiscard]] std::size_t QuarantinedSourceCountOwnerForTest() const noexcept;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;

  private:
    struct Source final : GpuCompletionReceiver
    {
        GpuCommandPump* parent = nullptr;
        GpuCommandStream* stream = nullptr;
        GpuCommandSourceReceiver* receiver = nullptr;
        GpuCompletionContext captured;
        GpuCommandLease executing;
        JobTicket ticket;
        std::uint64_t generation = 0;
        HRESULT error = S_OK;
        GpuCommandFailureDisposition failure = GpuCommandFailureDisposition::CancelOriginal;
        bool waiting = false;
        bool quarantined = false;
        bool recoveryArmed = false;
        bool failureReported = false;
        void GpuCompletionReady(JobTicket ticket, std::uint64_t deviceGeneration, HRESULT result) noexcept override;
    };
    [[nodiscard]] bool IsOwner() const noexcept;
    [[nodiscard]] Source* FindOwner(GpuCommandSourceLease lease) noexcept;
    void FailOwner(Source& source, HRESULT error) noexcept;
    void AcknowledgeOwner(Source& source, HRESULT result) noexcept;
    std::array<Source, kCapacity> _sources;
    JobScheduler* _scheduler = nullptr;
    GpuWorkQueue* _queue = nullptr;
    OwnerWorkMultiplexer* _routes = nullptr;
    std::uint64_t _nextGeneration = 0;
    std::uint32_t _ownerThread = 0;
    std::size_t _next = 0;
    bool _stopped = false;
    bool _inCallback = false;
    HRESULT _completionRefusal = S_OK;
    HRESULT _recoveryRefusal = S_OK;
};
