#pragma once

#include "Document.h"
#include "GpuCommandPump.h"
#include "GpuResidencyBudget.h"
#include "GpuWork.h"
#include "PixelBuffer.h"
#include "UtilityReadbackAdmission.h"
#include "UtilityReadbackOwnerService.h"

#include <memory>

class SharedPublicationMemory;

enum class RecordedReadbackCaptureKind : std::uint8_t
{
    Gpu,
    CpuOnly
};

// The submitted descriptor contains no captured document or graphics resource. The caller admits its
// complete CPU/GPU envelope before construction/submission, including its own processing/output owners.
// This original job then owns capture, private recording, serial copies and final joined retirement.
class RecordedDocumentReadbackJob : public Job,
                                    public GpuCommandSourceReceiver,
                                    public GpuReadbackReceiver,
                                    public GpuReadbackProducer,
                                    public UtilityReadbackOwner
{
  public:
    RecordedDocumentReadbackJob(JobScheduler& scheduler, GpuWorkQueue& queue, GpuCommandPump& commands,
                                UtilityReadbackOwnerService& owners, GpuResidencyBudget& gpuBudget,
                                UtilityReadbackAdmission::Envelope envelope, GpuResidencyReservation originalGpuCharge,
                                D3D11_BOX region, bool software,
                                RecordedReadbackCaptureKind captureKind = RecordedReadbackCaptureKind::Gpu) noexcept;
    ~RecordedDocumentReadbackJob() override;
    [[nodiscard]] static std::uint64_t RequiredMemory(D3D11_BOX region) noexcept;
    [[nodiscard]] JobResult Run(JobContext& context) final;
    void RetireWorkerStage(JobContext& context, JobResult result) noexcept final;
    [[nodiscard]] bool DeferRetirement(JobResult result, JobTicket original) noexcept final;
    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket original) noexcept final;
    void Completed(JobResult result) noexcept final;
    void CommandAcknowledgedOwner(JobTicket original, GpuCommandLease command, HRESULT result) noexcept final;
    void CommandSourceFailedOwner(JobTicket original, HRESULT result) noexcept final;
    void ReadbackReady(JobTicket original, GpuReadbackRows rows, HRESULT result) noexcept final;
    [[nodiscard]] HRESULT RecordReadbackSource(ID3D11Texture2D*& source, D3D11_BOX& box) noexcept final;
    [[nodiscard]] bool HasUtilityOwnerWork() const noexcept final;
    [[nodiscard]] bool IsUtilityOwnerReady() const noexcept final;
    void PumpUtilityOwner() noexcept final;
    void StopUtilityOwner() noexcept final;

  protected:
    // Original admitted Compute opening, before the owner captures any document/history references.
    [[nodiscard]] virtual JobResult PrepareReadbackOpening(JobContext&)
    {
        return {};
    }
    [[nodiscard]] virtual HRESULT CaptureReadbackDocument(CanvasDocument&) noexcept = 0;
    [[nodiscard]] virtual JobResult PrepareReadbackSnapshot(CanvasDocument&, JobContext&)
    {
        return {};
    }
    [[nodiscard]] virtual JobResult ProcessReadback(Raster&, JobContext&)
    {
        return {};
    }
    [[nodiscard]] virtual bool ProcessReadbackDetached() const noexcept
    {
        return false;
    }
    [[nodiscard]] virtual HRESULT CaptureDetachedReadback(Raster&) noexcept
    {
        return S_OK;
    }
    [[nodiscard]] virtual bool DeferDetachedCompletion(JobResult, JobTicket) noexcept
    {
        return false;
    }
    virtual void ResetReadback() noexcept {}
    [[nodiscard]] virtual HRESULT PrepareReadbackDevice(HRESULT reason) noexcept = 0;
    virtual void ReadbackCompleted(JobResult result, Raster image, std::uint64_t deviceGeneration) noexcept = 0;
    // Only the admitted sampler opts in. No source slot/recorder is held between acknowledged 1x1 results.
    [[nodiscard]] virtual bool KeepReadbackGraphForPixelContinuation() const noexcept
    {
        return false;
    }
    [[nodiscard]] bool ContinuePixelReadback(JobTicket original, unsigned int x, unsigned int y) noexcept;
    // Derived CPU/event stages use this acknowledgement-preserving route rather than Resume directly.
    // Clear suspended readiness before admitting the next Compute stage; refusal restores it.
    [[nodiscard]] bool ResumeDetachedReadback(JobTicket original) noexcept;
    [[nodiscard]] GpuReadbackRows MappedRowsForTest() const noexcept
    {
        return _rows;
    }
    // Owner-only observation after the actual graphics retirement acknowledgement. CPU continuation
    // registration can remain live for cancellation/retries; it is not a graphics resource or GPU charge.
    [[nodiscard]] bool ReadbackGraphicsRetiredOwner() const noexcept;
    // Precreated accounting handle for newly allocated CPU output within this admitted original job.
    // This confers no publication/binding authority and creates no reservation. The caller includes output bounds.
    [[nodiscard]] std::shared_ptr<const void> ReadbackOutputLifetime() const noexcept
    {
        return _memory;
    }
    // Called in the exact terminal Completed callback, after binding this original complete admission.
    // The derived retirement/adoption aggregate retains the SAME memory owner, including weak lifetime.
    void TransferReadbackOwners(CanvasDocument& document, PixelBuffer& pixels, Raster& image,
                                std::shared_ptr<SharedPublicationMemory>& memory) noexcept;

  private:
    struct Root;
    enum class Phase : std::uint8_t
    {
        Opening,
        Preparing,
        AwaitSource,
        AttachSource,
        Recording,
        AwaitCommands,
        RetireTransport,
        AwaitCopySlot,
        AwaitRows,
        Copying,
        RetireGraphics,
        Detached,
        Complete
    };
    enum class AfterGraphics : std::uint8_t
    {
        Complete,
        Detached,
        Recover
    };
    [[nodiscard]] bool IsOwner() const noexcept;
    [[nodiscard]] bool ValidRegion() const noexcept;
    [[nodiscard]] bool LastRegion() const noexcept;
    [[nodiscard]] D3D11_BOX CurrentRegion() const noexcept;
    [[nodiscard]] HRESULT CaptureCurrentGraphicsOwner(bool recovery) noexcept;
    [[nodiscard]] HRESULT ReserveCurrentGraphicsOwner(ID3D11Device* current) noexcept;
    void ResumeOwner(Phase next, JobExecutionLane lane = JobExecutionLane::Compute) noexcept;
    void RememberFailureOwner(JobResult result) noexcept;
    void ReleaseRowsOwner() noexcept;
    void ExcludeGraphOwner() noexcept;
    void BeginGraphicsRetirementOwner(AfterGraphics next) noexcept;
    void BeginFailureRetirementOwner() noexcept;
    void FinishOwner(JobResult result) noexcept;
    void RetireGraphicsForWork() noexcept;
    void DetachServiceOwner() noexcept;

    JobScheduler& _scheduler;
    GpuWorkQueue& _queue;
    GpuCommandPump& _commands;
    UtilityReadbackOwnerService& _owners;
    GpuResidencyBudget& _gpuBudget;
    const UtilityReadbackAdmission::Envelope _envelope;
    GpuResidencyReservation _gpuCharge;
    const RecordedReadbackCaptureKind _captureKind;
    const std::uint32_t _ownerThread;
    std::uint64_t _cpuAdmissionBytes;
    D3D11_BOX _region;
    std::shared_ptr<SharedPublicationMemory> _memory; // Created BEFORE Root, under original admission.
    Root* _root = nullptr;                            // Owns its intrusive worker-destruction node.
    JobTicket _ticket;
    CancelToken _cancellation;
    GpuCommandSourceLease _source;
    GpuReadbackRows _rows;
    Phase _phase = Phase::Opening;
    AfterGraphics _afterGraphics = AfterGraphics::Complete;
    JobResult _terminal;
    HRESULT _attemptFailure = S_OK;
    HRESULT _recordingFailure = S_OK; // Immutable from Resume(Recording) through that actual worker ack.
    std::uint64_t _deviceGeneration = 0;
    std::uint64_t _lastCommandOrdinal = 0;
    std::uint64_t _copiedPixels = 0;
    unsigned int _x = 0, _y = 0, _width = 0, _height = 0, _restarts = 0;
    bool _serviceAttached = false;
    bool _streamInitialized = false;
    bool _streamRetired = false;
    bool _transportAuthorized = false;
    bool _sourceAttached = false;
    bool _graphPrepared = false;
    bool _graphPublished = false;
    bool _rowsReady = false;
    bool _stopOffered = false;
    bool _resetOutput = false;
    bool _detachedReady = false;
    bool _graphicsRetired = true;
    bool _joinedDestruction = false;
    bool _softwareMemoryAdmitted;
    bool _deviceSoftware = false;
};
