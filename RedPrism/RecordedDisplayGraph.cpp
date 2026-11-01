#include "RecordedDisplayGraph.h"
#include "RecordedPixelPreview.h"
#include "WarpPreviewWork.h"

#include <cmath>
#include <exception>
#include <utility>

namespace
{
bool SupportedAdjustment(AdjustmentKind kind) noexcept
{
    switch (kind)
    {
    case AdjustmentKind::Levels:
    case AdjustmentKind::Curves:
    case AdjustmentKind::Exposure:
    case AdjustmentKind::GradientMap:
    case AdjustmentKind::Hsv:
    case AdjustmentKind::Grain:
        return true;
    default:
        return false;
    }
}
} // namespace

RecordedDisplayGraph::RecordedDisplayGraph(std::uint32_t originalOwnerThread) noexcept
    : _ownerThread(originalOwnerThread ? originalOwnerThread : GetCurrentThreadId())
{
}

RecordedDisplayGraph::~RecordedDisplayGraph()
{
    if (IsOwner() && _phase != Phase::Empty && _phase != Phase::Retired)
        std::terminate(); // Never move final CPU/graphics releases back to the UI through a wrapper destructor.
}

bool RecordedDisplayGraph::IsOwner() const noexcept
{
    return _ownerThread == GetCurrentThreadId();
}

HRESULT RecordedDisplayGraph::PrepareForWork(ID3D11Device* device, CanvasDocument source, GpuCommandStream& stream,
                                             CancelToken cancellation,
                                             const RecordedDisplayGraph* publishedSeed) noexcept
{
    return PrepareInternalForWork(device, std::move(source), stream, std::move(cancellation), publishedSeed, nullptr,
                                  {}, nullptr);
}

HRESULT RecordedDisplayGraph::PrepareStrokeForWork(ID3D11Device* device, CanvasDocument source,
                                                   const CanvasRenderer::StrokePreview& preview,
                                                   std::span<const RasterTile> tiles, GpuCommandStream& stream,
                                                   CancelToken cancellation,
                                                   const RecordedDisplayGraph* publishedSeed) noexcept
{
    return PrepareInternalForWork(device, std::move(source), stream, std::move(cancellation), publishedSeed, &preview,
                                  tiles, nullptr);
}

HRESULT RecordedDisplayGraph::PrepareAdjustmentForWork(ID3D11Device* device, CanvasDocument source,
                                                       const CanvasRenderer::AdjustmentPreview& preview,
                                                       GpuCommandStream& stream, CancelToken cancellation,
                                                       const RecordedDisplayGraph* publishedSeed) noexcept
{
    if (IsOwner() || _phase != Phase::Empty || !device)
        return E_INVALIDARG; // Refuse the owner before any metadata traversal or settings validation.
    const HRESULT recordingStatus = stream.RecordingStatusForWork();
    if (FAILED(recordingStatus))
        return recordingStatus;
    if (cancellation.IsCancelled())
        return E_ABORT;
    if (!std::isfinite(preview.previewScale) || !(preview.previewScale > 0 && preview.previewScale <= 1) ||
        (preview.lookup && !preview.lookup->adjustment.IsValid()) ||
        (preview.lookup && preview.lookup->adjustment.kind == AdjustmentKind::Hsv &&
         preview.lookup->cube.size() != 33u * 33u * 33u * 4u))
        return E_INVALIDARG;
    if ((preview.lookup && !SupportedAdjustment(preview.lookup->adjustment.kind)) ||
        (preview.liveOriginal && !SupportedAdjustment(preview.liveOriginal->kind)))
        return E_INVALIDARG;
    const ImageLayer* target = nullptr;
    for (const auto& layer : source.layers)
    {
        if (cancellation.IsCancelled())
            return E_ABORT;
        if (layer.id == preview.layerId)
        {
            target = &layer;
            break;
        }
    }
    if (!target)
        return E_INVALIDARG;
    if (preview.liveOriginal)
    {
        if (!preview.liveOriginal->IsValid() || !target->adjustment || target->asset ||
            target->adjustment.value() != preview.liveOriginal.value() || preview.source.HasStorage() ||
            preview.coverage.HasStorage() || preview.previewScale != 1 ||
            (preview.lookup && preview.lookup->adjustment.kind != preview.liveOriginal->kind))
            return E_INVALIDARG;
    }
    else if (!preview.lookup || !target->asset || target->adjustment || !preview.source.HasStorage() ||
             target->asset->image != preview.source || preview.source.Format() != RasterFormat::Bgra8888 ||
             preview.source.Width() > 16384 || preview.source.Height() > 16384 ||
             (preview.coverage.HasStorage() &&
              (preview.coverage.Format() != RasterFormat::Gray8 || preview.coverage.Width() != preview.source.Width() ||
               preview.coverage.Height() != preview.source.Height())))
        return E_INVALIDARG;
    return PrepareInternalForWork(device, std::move(source), stream, std::move(cancellation), publishedSeed, nullptr,
                                  {}, &preview);
}

HRESULT RecordedDisplayGraph::PrepareInternalForWork(ID3D11Device* device, CanvasDocument source,
                                                     GpuCommandStream& stream, CancelToken cancellation,
                                                     const RecordedDisplayGraph* publishedSeed,
                                                     const CanvasRenderer::StrokePreview* preview,
                                                     std::span<const RasterTile> tiles,
                                                     const CanvasRenderer::AdjustmentPreview* adjustment) noexcept
{
    if (IsOwner() || _phase != Phase::Empty || !device)
        return E_INVALIDARG;
    const HRESULT recordingStatus = stream.RecordingStatusForWork();
    if (FAILED(recordingStatus))
        return recordingStatus;
    if (cancellation.IsCancelled())
        return E_ABORT;
    if (preview && preview->pixelSource && (!tiles.empty() || !RecordedPixelPreviewMatchesForWork(source, *preview)))
        return E_INVALIDARG; // Before any context/resource prefix or source ownership acquisition.
    if (preview && preview->warpSource)
    {
        // Reject an invalid immutable Warp descriptor before creating a private resource prefix.
        // The original parent has already admitted all phase/source/transport overlap on its worker.
        const auto* target = source.layers.FindKey(preview->layerID);
        const auto& warp = preview->warpSource->source;
        WarpPreviewReader reader(preview->warpSource);
        const LayerTransform grid{{0, 0}, {static_cast<double>(source.width), static_cast<double>(source.height)}};
        const Raster original = target && target->asset ? target->asset->image : Raster{};
        if (!target || target->isGroup || target->adjustment || preview->mask || !reader.IsValidForWork() ||
            source.width != warp.width || source.height != warp.height || warp.image != original ||
            warp.placement != target->transform || preview->source != original || preview->width != source.width ||
            preview->height != source.height || preview->transform != grid || preview->sourceX || preview->sourceY ||
            preview->sourceWidth || preview->sourceHeight || preview->fill)
            return E_INVALIDARG;
    }
    D3D11_FEATURE_DATA_THREADING threading{};
    const HRESULT capabilities = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    if (FAILED(capabilities) || !threading.DriverConcurrentCreates)
        return FAILED(capabilities) ? capabilities : E_NOTIMPL;
    _phase = Phase::Preparing;
    _device = device;
    _source = std::move(source);
    _recording.emplace();
    HRESULT result = device->CreateDeferredContext(0, _deferred.put());
    if (SUCCEEDED(result))
        result = _recording.value().InitializeForWork(_deferred.get(), stream, cancellation);
    if (SUCCEEDED(result))
        result = _composition.CreateDeviceResources(device, _deferred.get(), &_recording.value());
    if (SUCCEEDED(result) && publishedSeed)
    {
        if (publishedSeed->_phase != Phase::Published || publishedSeed->_device.get() != device)
            result = E_INVALIDARG;
        else
            result = _composition.SeedRasterResidencyForWork(publishedSeed->_composition);
    }
    if (SUCCEEDED(result) && preview)
        result = _composition.PrepareRecordedStrokeForWork(
            _source, *preview, tiles, publishedSeed ? &publishedSeed->_composition : nullptr, _inheritedStroke);
    if (SUCCEEDED(result) && adjustment)
        _composition.SetAdjustmentPreview(*adjustment); // Copies owning handles only on the admitted worker.
    if (SUCCEEDED(result))
        result = _composition.Compose(_source);
    if (SUCCEEDED(result))
        result = _composition.SealRecordedDisplayForWork();
    if (SUCCEEDED(result))
        _phase = Phase::Sealed;
    return result;
}

HRESULT RecordedDisplayGraph::PublishOwner(ID3D11DeviceContext* originalContext) noexcept
{
    if (!IsOwner() || _phase != Phase::Sealed || !originalContext)
        return E_INVALIDARG;
    const HRESULT result = _composition.BindAcknowledgedDisplayOwner(originalContext);
    if (SUCCEEDED(result))
    {
        _original = originalContext;
        _phase = Phase::Published;
    }
    return result;
}

HRESULT RecordedDisplayGraph::BlitOwner(ID3D11RenderTargetView* target, unsigned int width, unsigned int height,
                                        const CanvasViewport& view, float holeLeft, float holeTop, float holeWidth,
                                        float holeHeight) noexcept
{
    if (!IsPublishedOwner() || !target)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    wil::com_ptr_nothrow<ID3D11Device> targetDevice;
    target->GetDevice(targetDevice.put());
    if (targetDevice.get() != _device.get())
        return E_INVALIDARG;
    return _composition.BlitToTarget(target, width, height, view, holeLeft, holeTop, holeWidth, holeHeight);
}

bool RecordedDisplayGraph::DetachOwner() noexcept
{
    if (!IsOwner() || _phase == Phase::Detached || _phase == Phase::Retired)
        return false;
    _phase = Phase::Detached;
    return true;
}

bool RecordedDisplayGraph::RetireForWork() noexcept
{
    if (IsOwner() || _phase != Phase::Detached)
        return false;
    ReleaseForWork();
    return true;
}

bool RecordedDisplayGraph::RetireUnpublishedForWork() noexcept
{
    if (IsOwner() || _phase == Phase::Published || _phase == Phase::Detached || _phase == Phase::Retired)
        return false;
    ReleaseForWork();
    return true;
}

void RecordedDisplayGraph::ReleaseForWork() noexcept
{
    _composition.ReleaseDeviceResources();
    _original.reset();
    _recording.reset();
    _deferred.reset();
    _device.reset();
    _source = {};
    _phase = Phase::Retired;
}

std::uint64_t RecordedDisplayGraph::SurfaceBytesOwner() const noexcept
{
    return IsPublishedOwner() ? _composition.CanvasSurfaceBytes() : 0;
}

bool RecordedDisplayGraph::IsPublishedOwner() const noexcept
{
    return IsOwner() && _phase == Phase::Published;
}

bool RecordedDisplayGraph::IsPublishedOnOwner(ID3D11Device* device, ID3D11DeviceContext* context) const noexcept
{
    return IsPublishedOwner() && device == _device.get() && context == _original.get();
}

CanvasRenderer* RecordedDisplayGraph::CompositionForTestOwner() noexcept
{
    return IsPublishedOwner() ? &_composition : nullptr;
}

ID3D11Device* RecordedDisplayGraph::DeviceForTestOwner() const noexcept
{
    return IsPublishedOwner() ? _device.get() : nullptr;
}

ID3D11DeviceContext* RecordedDisplayGraph::ContextForTestOwner() const noexcept
{
    return IsPublishedOwner() ? _original.get() : nullptr;
}

bool RecordedDisplayGraph::InheritedStrokeForTestOwner() const noexcept
{
    return IsPublishedOwner() && _inheritedStroke;
}
