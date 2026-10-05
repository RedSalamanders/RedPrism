#pragma once

#include "DeferredRelease.h"
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

// One precreated DEFAULT destination, one original scheduler record and one serial base-level chunk at a
// time. No useful work runs on the owner. This leaf does not create device resources, build mips, publish a
// display or mutate a document. Caller options MUST set deferredRetirement and reserve the complete graph.
// Both the queue and the caller's device owner survive this job and the shutdown owner cleanup hook.
class GpuRasterUploadJob : public Job, public GpuUploadReceiver
{
  public:
    GpuRasterUploadJob(JobScheduler& scheduler, GpuUploadQueue& queue, std::unique_ptr<GpuRasterUploadInput> input,
                       ID3D11Texture2D* destination, unsigned int subresource = 0, unsigned int x = 0,
                       unsigned int y = 0) noexcept;
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

  private:
    enum class Phase
    {
        Prepare,
        Ready,
        AwaitRows,
        Filling,
        AwaitCopy,
        AfterChunk,
    };
    struct RetireInput final
    {
        void operator()(GpuRasterUploadInput* input) const noexcept;
    };
    [[nodiscard]] bool RequestChunk(JobTicket ticket) noexcept;
    void AcknowledgeUnpublished(JobResult result) noexcept;

    JobScheduler& _scheduler;
    GpuUploadQueue& _queue;
    std::unique_ptr<GpuRasterUploadInput, RetireInput> _input;
    wil::com_ptr_nothrow<ID3D11Texture2D> _destination;
    JobTicket _ticket;
    GpuUploadRows _rows;
    RasterUploadRowObserver _observer = nullptr;
    void* _observerContext = nullptr;
    PublicationObserver _publicationObserver = nullptr;
    void* _publicationContext = nullptr;
    JobResult _workerResult;
    HRESULT _queueResult = S_OK;
    std::uint64_t _chunk = 0;
    std::uint64_t _deviceGeneration = 0;
    unsigned int _subresource = 0;
    unsigned int _x = 0;
    unsigned int _y = 0;
    Phase _phase = Phase::Prepare;
    bool _queueFinished = true;    // owner-only; a receiver never reads the unfinished worker's result
    bool _workerPublished = false; // read on owner only after scheduler producer acknowledgement
};
