#pragma once

#include "Document.h"
#include "GpuWork.h"
#include "PixelBuffer.h"

class CanvasRenderer;

// One immutable snapshot and one scheduler reservation through every tile. Workers allocate/copy/adopt;
// owner stages alone compose, acknowledge mapped rows, rebind and publish the complete result.
class DocumentReadbackJob : public Job, public GpuReadbackReceiver, public GpuReadbackProducer
{
  public:
    DocumentReadbackJob(JobScheduler& scheduler, GpuWorkQueue& queue, CanvasRenderer& composition,
                        CanvasDocument document, D3D11_BOX region) noexcept;
    // Cheap descriptor: the first admitted owner continuation captures its document under the complete graph
    // reservation. Existing direct callers keep the snapshot constructor until their own capture migration.
    DocumentReadbackJob(JobScheduler& scheduler, GpuWorkQueue& queue, CanvasRenderer& composition,
                        D3D11_BOX region) noexcept;
    ~DocumentReadbackJob() override;
    [[nodiscard]] static std::uint64_t RequiredMemory(const D3D11_BOX& region) noexcept;
    [[nodiscard]] JobResult Run(JobContext& context) final;
    [[nodiscard]] bool DeferCompletion(JobResult, JobTicket ticket) noexcept final;
    void ReadbackReady(JobTicket ticket, GpuReadbackRows rows, HRESULT result) noexcept final;
    [[nodiscard]] HRESULT RecordReadbackSource(ID3D11Texture2D*& source, D3D11_BOX& box) noexcept final;
    void Completed(JobResult result) noexcept final;

  protected:
    [[nodiscard]] virtual HRESULT CaptureReadbackDocument(CanvasDocument&) noexcept
    {
        return E_UNEXPECTED;
    }
    // Worker-only preprocessing of the admitted snapshot before the first GPU tile. Called again on device
    // restart: reuse the same immutable original or make preprocessing idempotent. No live owner access.
    [[nodiscard]] virtual JobResult PrepareReadbackSnapshot(CanvasDocument&, JobContext&)
    {
        return {};
    }
    // Call only after worker acknowledgement/join. Destination owners must already exist and be empty; the
    // derived release aggregate keeps every CPU owner off the UI destructor path while GPU cleanup stays here.
    void TransferReadbackOwners(CanvasDocument& document, PixelBuffer& pixels, Raster& image) noexcept;
    // Harness-only observation of the actual borrowed lease during ProcessReadback. Never retain/read it
    // after the producer returns and owner acknowledgement; it transfers no context or texture ownership.
    [[nodiscard]] GpuReadbackRows MappedRowsForTest() const noexcept
    {
        return _rows;
    }
    // Optional worker-only final processing under this graph's reservation (for example merge crop/thumbnail).
    [[nodiscard]] virtual JobResult ProcessReadback(Raster&, JobContext&)
    {
        return {};
    }
    // Optional CPU-only follow-up under the same graph reservation. The owner validates the final GPU tile,
    // acknowledges its mapping and releases utility targets before this worker stage. Device changes after
    // that boundary cannot invalidate already copied bytes or restart an external writer.
    [[nodiscard]] virtual bool ProcessReadbackDetached() const noexcept
    {
        return false;
    }
    // Owner-only transition after the last map acknowledgement and utility release. The opening snapshot's
    // authority must still guard any input captured here. No extra admission or GPU restart follows it.
    [[nodiscard]] virtual HRESULT CaptureDetachedReadback(Raster&) noexcept
    {
        return S_OK;
    }
    // Optional further CPU/owner stages under this original record, ticket and reservation. The derived
    // owner callback resumes or registers one acknowledged event wait, never spins or drains completion.
    [[nodiscard]] virtual bool DeferDetachedCompletion(JobResult, JobTicket) noexcept
    {
        return false;
    }
    virtual void ResetReadback() noexcept {} // worker-only release of a previous attempt's prepared result
    // Owner thread. Ensure the composition and queue use the current device, recovering on a device-lost reason.
    // No waits, modal or message pump. At most two whole-graph restarts; no partial result is published.
    [[nodiscard]] virtual HRESULT PrepareReadbackDevice(HRESULT reason) noexcept = 0;
    virtual void ReadbackCompleted(JobResult result, Raster image, std::uint64_t deviceGeneration) noexcept = 0;

  private:
    enum class Phase
    {
        Capture,
        Prepare,
        Copy,
        ProcessDetached,
        Error
    };
    [[nodiscard]] bool ValidRegion() const noexcept;
    [[nodiscard]] bool LastTile() const noexcept;
    void Acknowledge() noexcept;
    void RequestTile(JobTicket ticket) noexcept;
    void ResumeError(JobTicket ticket, HRESULT error) noexcept;

    JobScheduler& _scheduler;
    GpuWorkQueue& _queue;
    CanvasRenderer& _composition;
    CanvasDocument _document;
    const D3D11_BOX _region;
    PixelBuffer _pixels;
    Raster _image;
    GpuReadbackRows _rows;
    CancelToken _cancellation;
    JobTicket _ticket;
    Phase _phase = Phase::Prepare;
    HRESULT _error = S_OK;
    std::uint64_t _deviceGeneration = 0;
    unsigned int _tileSide = 0;
    unsigned int _x = 0, _y = 0, _tileWidth = 0, _tileHeight = 0;
    unsigned int _restarts = 0;
    std::uint64_t _copiedPixels = 0;
    bool _utilityAcquired = false;
    bool _resetOutput = false;
};
