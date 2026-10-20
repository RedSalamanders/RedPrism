#pragma once

#include "CommittedDisplayController.h"
#include "PreparedLayerRows.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// One original job drives this payload through acknowledged worker/owner stages. It creates no job,
// pump or device. Before CaptureOwner, its caller admits the complete CPU/output overlap, including
// this wrapper, final pixels/history, rows, source/retired readers, command shadows and WARP storage.
// This component's bounds are ceilings within that admission, never a substitute for whole-job planning.
class OriginalDisplayPublication final : public GpuCommandSourceReceiver
{
  public:
    struct Bounds final
    {
        std::uint64_t cpuBytes = 0;
        std::uint64_t gpuBytes = 0;
    };
    struct RowInput final
    {
        std::vector<Guid> collapsed;
        std::wstring hiddenPattern;
        std::shared_ptr<const PreparedLayerRows> previous;
        std::uint64_t documentSerial = 0;
        std::uint64_t firstItemId = 0;
    };

    // Allocate on the admitted worker; its final destruction also belongs to a worker.
    // Optional borrowed readiness signal, invoked only on the owner after real progress/failure/join.
    // It may request the existing application action source; it cannot pump, resume, capture or publish.
    using OwnerReady = void (*)(void*) noexcept;
    OriginalDisplayPublication(std::uint32_t ownerThread, Bounds bounds, OwnerReady ownerReady = nullptr,
                               void* context = nullptr) noexcept;
    ~OriginalDisplayPublication() override;
    OriginalDisplayPublication(const OriginalDisplayPublication&) = delete;
    OriginalDisplayPublication& operator=(const OriginalDisplayPublication&) = delete;

    // Successful original stage acknowledgement only. The slot and original queue context stay fixed.
    [[nodiscard]] HRESULT CaptureOwner(JobScheduler& jobs, JobTicket original, GpuWorkQueue& queue,
                                       GpuCommandPump& commands, CommittedDisplayController& display,
                                       const CommittedDisplayDescriptor& current) noexcept;
    // Worker only, while the caller retains the exact immutable final closure. This owns no session/UI.
    [[nodiscard]] JobResult PrepareForWork(CanvasDocument final, RowInput rows, CancelToken cancellation);
    [[nodiscard]] HRESULT AdmitRecordingOwner() noexcept;
    // A stream may exist even if Attach refuses. Still run/join one (possibly skipped) recorder stage
    // and call FinishRecordingForWork; admission refusal cannot invent producer acknowledgement.
    [[nodiscard]] bool HasRecorderOwner() const noexcept;
    [[nodiscard]] JobResult RecordForWork(CancelToken cancellation) noexcept;
    // Exact RetireWorkerStage, including a cancelled/skipped recorder. It finishes the producer once.
    void FinishRecordingForWork(JobResult result) noexcept;
    // Original joined owner callback only; a producer flag alone does not grant this acknowledgement.
    [[nodiscard]] bool RecordingJoinedOwner() noexcept;
    [[nodiscard]] bool TransportReadyOwner() const noexcept;
    [[nodiscard]] bool AuthorizeTransportRetirementOwner() noexcept;
    [[nodiscard]] bool RetireTransportForWork() noexcept;

    // After all useful workers and real original GPU uses joined, with memory already bound to original.
    // No document/history mutation. A failure leaves the committed CPU document unchanged.
    [[nodiscard]] HRESULT PreparePublicationOwner(const std::shared_ptr<const SharedPublicationMemory>& memory,
                                                  const CanvasDocument& final) noexcept;
    // Invoke only in the same nonreentrant action, immediately after the checked CPU/history swap.
    // An empty output avoids dropping an old row graph on the owner; RowInput retains its predecessor.
    // Optional hook updates cached row epochs/keys and their warmed prepared chrome consumer before the graph callback.
    // It is nonfailing/nonreentrant: no message pumping, document mutation, source capture or allocation.
    using RowsAdopted = void (*)(void*) noexcept;
    [[nodiscard]] bool AdoptOwner(std::shared_ptr<const PreparedLayerRows>& empty, RowsAdopted rowsAdopted = nullptr,
                                  void* context = nullptr) noexcept;

    // Failure/cancel requests no completion. Wait for TransportReadyOwner before authorizing retirement.
    void StopOwner() noexcept;
    // After transport retirement (or before any recorder was admitted), detach future private graph use.
    [[nodiscard]] bool DetachForRetirementOwner() noexcept;
    [[nodiscard]] bool RetireGraphForWork() noexcept;
    // Exact joined graph-retirement callback; only now refund the failed graph's GPU charge/fixed slot.
    [[nodiscard]] bool FinishRetirementOwner() noexcept;

    // Actual scheduler Close after all producers joined. Destruction authority only, never publication.
    // GPU charge moves to a preexisting owner hook; that owner refunds only after final worker retirement.
    [[nodiscard]] bool JoinedShutdownOwner(GpuResidencyReservation& empty) noexcept;
    [[nodiscard]] bool RetireJoinedShutdownForWork() noexcept;

    // Describes whether CaptureOwner reached original-job ownership, including a partial failed capture.
    // This is no successful-work, join or publication authority.
    [[nodiscard]] JobTicket OriginalOwner() const noexcept;
    [[nodiscard]] HRESULT FailureOwner() const noexcept;
    [[nodiscard]] const CanvasDocument* SourceAfterJoin() const noexcept;
    [[nodiscard]] bool AdoptedOwner() const noexcept;

    void CommandAcknowledgedOwner(JobTicket source, GpuCommandLease command, HRESULT result) noexcept override;
    void CommandSourceFailedOwner(JobTicket source, HRESULT result) noexcept override;

  private:
    [[nodiscard]] bool IsOwner() const noexcept;
    [[nodiscard]] bool IsWorker() const noexcept;
    void FailOwner(HRESULT result) noexcept;
    void SignalOwnerReady() noexcept;

    const std::uint32_t _ownerThread;
    const Bounds _bounds;
    const OwnerReady _ownerReady;
    void* const _ownerReadyContext;
    JobScheduler* _jobs = nullptr;
    GpuWorkQueue* _queue = nullptr;
    GpuCommandPump* _commands = nullptr;
    CommittedDisplayController* _display = nullptr;
    JobTicket _original;
    CommittedDisplayPublicationSlot _slot;
    CommittedDisplaySeedLease _seed;
    GpuResidencyReservation _gpu;
    GpuCommandSourceLease _commandSource;
    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _context;
    GpuCompletionContext _captured, _transportCapture;
    GpuCommandStream _stream;
    std::optional<CanvasDocument> _source;
    std::unique_ptr<PreparedCommittedDisplay> _prepared;
    std::shared_ptr<PreparedLayerRows> _rows;
    std::shared_ptr<const PreparedLayerRows> _previousRows;
    std::uint64_t _deviceGeneration = 0, _documentSerial = 0;
    HRESULT _failure = S_OK;
    bool _software = false, _capturedOwner = false, _prepareAttempted = false;
    bool _streamInitialized = false, _producerFinished = false, _recordingJoined = false;
    bool _transportAuthorized = false, _transportRetired = false;
    bool _publicationReady = false, _adopted = false, _detached = false, _graphRetired = false;
    bool _shutdownJoined = false;
};
