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
    ~DocumentReadbackJob() override;
    [[nodiscard]] static std::uint64_t RequiredMemory(const D3D11_BOX& region) noexcept;
    [[nodiscard]] JobResult Run(JobContext& context) final;
    [[nodiscard]] bool DeferCompletion(JobResult, JobTicket ticket) noexcept final;
    void ReadbackReady(JobTicket ticket, GpuReadbackRows rows, HRESULT result) noexcept final;
    [[nodiscard]] HRESULT RecordReadbackSource(ID3D11Texture2D*& source, D3D11_BOX& box) noexcept final;
    void Completed(JobResult result) noexcept final;

  protected:
    // Optional worker-only final processing under this graph's reservation (for example merge crop/thumbnail).
    [[nodiscard]] virtual JobResult ProcessReadback(Raster&, JobContext&)
    {
        return {};
    }
    virtual void ResetReadback() noexcept {} // worker-only release of a previous attempt's prepared result
    // Owner thread. Ensure the composition and queue use the current device, recovering on a device-lost reason.
    // No waits, modal or message pump. At most two whole-graph restarts; no partial result is published.
    [[nodiscard]] virtual HRESULT PrepareReadbackDevice(HRESULT reason) noexcept = 0;
    virtual void ReadbackCompleted(JobResult result, Raster image, std::uint64_t deviceGeneration) noexcept = 0;

  private:
    enum class Phase
    {
        Prepare,
        Copy,
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
    const CanvasDocument _document;
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
