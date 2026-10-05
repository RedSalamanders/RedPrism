#pragma once

#include "DeferredRelease.h"
#include "GpuRasterTextureCreation.h"
#include "GpuResidencyBudget.h"
#include "GpuUploadWork.h"
#include "RasterUploadLayout.h"

#include <memory>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable : 4324)
// Prepare this held CPU owner on a worker under the caller's admitted complete graph. Its byte estimate is
// scratch/retirement only; it is not CPU/VRAM admission for a document, destination, mips or display resources.
struct GpuRasterUploadInput final : DeferredReleaseNode
{
    [[nodiscard]] static std::unique_ptr<GpuRasterUploadInput> Make(RasterUploadLayout layout);
    [[nodiscard]] const RasterUploadLayout& Layout() const noexcept
    {
        return _layout;
    }
    [[nodiscard]] std::uint64_t WorkerAllocationBytes() const noexcept;

  private:
    friend class GpuRasterUploadJob;
    RasterUploadLayout _layout;
    std::vector<std::uint8_t> _sourceRow;
    std::vector<std::uint64_t> _channelSums;
};
#pragma warning(pop)

struct GpuRasterCreationRequest final
{
    ID3D11Device* device = nullptr; // borrowed for immediate owner capture only
    GpuResidencyReservation reservation;
    bool completeMips = false;
};

// Adopted raster owner. Its reservation and held Raster survive the producing job. MipsComplete reports only
// acknowledged mip initialization; future graphics users still need their own completion before retirement.
// Borrowed views may not escape their admitted owner. Reset/replacement happens only after those users join.
class GpuRasterCreatedResidency final
{
  public:
    GpuRasterCreatedResidency() noexcept = default;
    ~GpuRasterCreatedResidency();
    GpuRasterCreatedResidency(const GpuRasterCreatedResidency&) = delete;
    GpuRasterCreatedResidency& operator=(const GpuRasterCreatedResidency&) = delete;
    GpuRasterCreatedResidency(GpuRasterCreatedResidency&& other) noexcept;
    GpuRasterCreatedResidency& operator=(GpuRasterCreatedResidency&& other) noexcept;
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(_reservation);
    }
    [[nodiscard]] ID3D11Texture2D* TextureOwner() const noexcept;
    [[nodiscard]] ID3D11ShaderResourceView* ShaderViewOwner() const noexcept;
    [[nodiscard]] const Raster& Source() const noexcept
    {
        return _source;
    }
    [[nodiscard]] std::uint64_t DeviceGeneration() const noexcept
    {
        return _reservation.DeviceGeneration();
    }
    [[nodiscard]] std::uint64_t ReservedBytes() const noexcept
    {
        return _reservation.Bytes();
    }
    [[nodiscard]] bool MipsComplete() const noexcept
    {
        return _mipsComplete;
    }
    void ResetOwner() noexcept;

  private:
    friend class GpuRasterUploadJob;
    GpuResidencyReservation _reservation; // last release, after graphics and CPU source ownership
    Raster _source;
    wil::com_ptr_nothrow<ID3D11Texture2D> _texture;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _view;
    std::uint32_t _ownerThread = 0;
    bool _mipsComplete = false;
};

// One DEFAULT destination, one original scheduler record and one serial base-level chunk at a time. The
// optional creation route retains an actual GPU reservation and exact device, creates texture/view on a
// worker only when reported concurrency permits, and publishes no resource before owner acknowledgement.
// Optional completeMips records/fences one parent-to-child draw per owner pump before final adoption. This leaf
// never publishes a display or mutates a document. Caller options MUST set deferredRetirement and reserve the
// complete CPU graph; externally created resources keep external GPU admission.
// Both the queue and the caller's device owner survive this job and the shutdown owner cleanup hook.
class GpuRasterUploadJob : public Job, public GpuUploadReceiver
{
  public:
    GpuRasterUploadJob(JobScheduler& scheduler, GpuUploadQueue& queue, std::unique_ptr<GpuRasterUploadInput> input,
                       ID3D11Texture2D* destination, unsigned int subresource = 0, unsigned int x = 0,
                       unsigned int y = 0) noexcept;
    GpuRasterUploadJob(JobScheduler& scheduler, GpuUploadQueue& queue, std::unique_ptr<GpuRasterUploadInput> input,
                       GpuRasterCreationRequest creation) noexcept;
    ~GpuRasterUploadJob() override;
    [[nodiscard]] JobResult Run(JobContext& context) final;
    [[nodiscard]] bool DeferRetirement(JobResult result, JobTicket ticket) noexcept final;
    [[nodiscard]] bool DeferCompletion(JobResult result, JobTicket ticket) noexcept final;
    void WritableRowsReady(JobTicket ticket, GpuUploadRows rows) noexcept final;
    void UploadFinished(JobTicket ticket, HRESULT result) noexcept final;
    void Completed(JobResult result) noexcept final;

  protected:
    // Valid only during this owner's final callback. Borrow/adopt only after a Finished result. CPU input and
    // destination ownership retire through their respective worker and owner lanes after the callback.
    [[nodiscard]] ID3D11Texture2D* Destination() const noexcept
    {
        return _destination.get();
    }
    [[nodiscard]] std::uint64_t UploadedChunks() const noexcept
    {
        return _chunk;
    }
    // Complete sampling is allowed only when the adopted residency reports MipsComplete. A base-only request
    // leaves lower levels undefined. Null for an externally created destination.
    [[nodiscard]] ID3D11ShaderResourceView* DestinationShaderView() const noexcept
    {
        return _creationMode ? _creation.ShaderViewOwner() : nullptr;
    }
    // Exactly one successful final callback may take the complete created destination/source/reservation.
    // Refused/outside/failed/duplicate adoption returns an empty owner. New consumers use this transfer rather
    // than retaining a borrowed texture/view after the job's GPU reservation would have ended.
    [[nodiscard]] GpuRasterCreatedResidency TakeCreatedResidencyOwner() noexcept;
    [[nodiscard]] JobTicket UploadTicket() const noexcept
    {
        return _ticket;
    }
    virtual void RasterUploadCompleted(JobResult result) noexcept = 0;
    // Test-only deterministic worker row boundary. It may not access UI/device owners or retain the lease.
    void SetRowObserverForTest(RasterUploadRowObserver observer, void* context) noexcept;
    using PublicationObserver = void (*)(void*, JobContext&) noexcept;
    // This observes the final row-lease publication while the producer is still running. It may wait on test
    // gates/shutdown but must not access rows, UI or graphics. Production leaves both observers empty.
    void SetPublicationObserverForTest(PublicationObserver observer, void* context) noexcept;
    [[nodiscard]] bool SetMipViewFailureForTest(unsigned int slice, HRESULT error) noexcept;
    [[nodiscard]] bool SetCreationFailureForTest(GpuRasterCreationFailure point, HRESULT error) noexcept;

  private:
    enum class Phase
    {
        Prepare,
        Ready,
        AwaitRows,
        Filling,
        AwaitCopy,
        AfterChunk,
        AwaitMip,
        AfterMip,
    };
    struct RetireInput final
    {
        void operator()(GpuRasterUploadInput* input) const noexcept;
    };
    [[nodiscard]] bool RequestChunk(JobTicket ticket) noexcept;
    [[nodiscard]] bool RequestMip(JobTicket ticket) noexcept;
    void AcknowledgeUnpublished(JobResult result) noexcept;

    JobScheduler& _scheduler;
    GpuUploadQueue& _queue;
    std::unique_ptr<GpuRasterUploadInput, RetireInput> _input;
    GpuResidencyReservation _gpuReservation; // destroyed AFTER the exact created graphics owners
    GpuRasterTextureCreation _creation;
    CancelToken _cancellation;
    wil::com_ptr_nothrow<ID3D11Texture2D> _destination;
    JobTicket _ticket;
    GpuUploadRows _rows;
    RasterUploadRowObserver _observer = nullptr;
    void* _observerContext = nullptr;
    PublicationObserver _publicationObserver = nullptr;
    void* _publicationContext = nullptr;
    JobResult _workerResult;
    HRESULT _queueResult = S_OK;
    HRESULT _creationCaptureResult = S_OK;
    std::uint64_t _chunk = 0;
    unsigned int _mipSlice = 0;
    std::uint64_t _deviceGeneration = 0;
    unsigned int _subresource = 0;
    unsigned int _x = 0;
    unsigned int _y = 0;
    Phase _phase = Phase::Prepare;
    bool _queueFinished = true;    // owner-only; a receiver never reads the unfinished worker's result
    bool _workerPublished = false; // read on owner only after scheduler producer acknowledgement
    bool _creationMode = false;
    bool _canAdoptCreated = false;
    bool _completeMips = false;
    bool _mipsComplete = false;
};
