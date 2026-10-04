#include "WandLayerSelectionWork.h"

#include "Selection.h"

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
bool Checkpoint(const WandLayerSelectionObserver* observer, WandLayerSelectionStage stage,
                const CancelToken& cancellation) noexcept
{
    if (cancellation.IsCancelled())
        return false;
    if (observer && observer->checkpoint)
        observer->checkpoint(observer->context, stage);
    return !cancellation.IsCancelled();
}
bool ValidPoint(Vec2 point, int width, int height) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0 && point.y >= 0 && point.x < width &&
           point.y < height;
}
bool ValidSettings(const WandSettings& settings) noexcept
{
    return !settings.sampleAllLayers && settings.sampleSize <= WandSampleSize::FiveByFive;
}
const ImageLayer* CapturedWandLayer(const CanvasDocument& document, const std::optional<Guid>& id) noexcept
{
    if (id)
        for (const auto& layer : document.layers)
            if (layer.id == id.value())
                return &layer;
    return nullptr;
}
class WandByteSum final
{
  public:
    [[nodiscard]] bool Add(std::uint64_t count, std::uint64_t size = 1) noexcept
    {
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if ((size && count > maximum / size) || count * size > maximum - value)
            return false;
        value += count * size;
        return true;
    }
    std::uint64_t value = 0;
};
} // namespace

std::optional<WandLayerSelectionInput> DocumentSession::PrepareWandLayerSelectionInput(Vec2 point,
                                                                                       WandSettings settings,
                                                                                       bool antialiased) const
{
    if (!_document || !ValidSettings(settings) || !ValidPoint(point, _document->width, _document->height))
        return std::nullopt;
    auto authority = CaptureHistoryRestoreAuthority();
    if (!CanApplyHistoryRestore(authority))
        return std::nullopt;
    auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    return WandLayerSelectionInput(point, settings, antialiased, std::move(authority), std::move(token.value()),
                                   history.CaptureState(), _document.value(), _activeLayerID);
}

std::optional<PreparedWandLayerSelection> DocumentSession::PrepareWandLayerSelectionPublication(
    WandLayerSelectionInput input, WandFailure* failure, CancelToken cancellation,
    DocumentHistory::PrepareObserver historyObserver, const WandLayerSelectionObserver* observer)
{
    const auto refused = [&](WandFailure reason) -> std::optional<PreparedWandLayerSelection>
    {
        if (failure)
            *failure = reason;
        return std::nullopt;
    };
    if (cancellation.IsCancelled())
        return refused(WandFailure::Cancelled);
    if (!input._valid || !input._document || input._token.target || !ValidSettings(input._settings) ||
        input._document->width != input._token.canvasWidth || input._document->height != input._token.canvasHeight ||
        !ValidPoint(input._point, input._document->width, input._document->height))
        return refused(WandFailure::Memory);
    if (failure)
        *failure = WandFailure::None;
    if (!Checkpoint(observer, WandLayerSelectionStage::BeforeSource, cancellation))
        return refused(WandFailure::Cancelled);
    const auto& before = input._document.value();
    const ImageLayer* target = CapturedWandLayer(before, input._active);
    const Raster source = target && target->asset ? target->asset->image : Raster{};
    std::optional<std::vector<SelectionLoop>> outline;
    if (!source.HasStorage())
    {
        // Every sampled pixel is transparent, so every sample size, tolerance and connectivity selects the
        // complete canvas. Construct its exact clockwise rectangle without document-sized pixel/trace buffers.
        if (!Checkpoint(observer, WandLayerSelectionStage::SourceReady, cancellation))
            return refused(WandFailure::Cancelled);
        outline = SelectionGeometry::Rect(0, 0, before.width, before.height);
    }
    else
    {
        if (!target)
            return refused(WandFailure::Memory);
        const Raster sampled = RenderLayerPixels(source, target->transform, before.width, before.height, cancellation);
        if (cancellation.IsCancelled())
            return refused(WandFailure::Cancelled);
        if (!sampled.HasStorage())
            return refused(WandFailure::Memory);
        if (!Checkpoint(observer, WandLayerSelectionStage::SourceReady, cancellation))
            return refused(WandFailure::Cancelled);
        WandFailure tracedFailure = WandFailure::None;
        outline = MagicWand::Select(sampled, input._point, input._settings, &tracedFailure, cancellation);
        if (tracedFailure != WandFailure::None)
            return refused(tracedFailure);
    }
    if (!Checkpoint(observer, WandLayerSelectionStage::OutlineReady, cancellation))
        return refused(WandFailure::Cancelled);
    std::optional<DocumentSelection> selection;
    const bool matched = outline.has_value();
    if (matched)
        selection = DocumentSelection{input._antialiased, std::move(outline.value())};
    if (!Checkpoint(observer, WandLayerSelectionStage::SelectionReady, cancellation))
        return refused(WandFailure::Cancelled);
    const bool unchanged = selection == before.selection;
    PreparedWandLayerSelection result(std::move(input));
    if (unchanged)
        return result;
    if (historyObserver.checkpoint)
        historyObserver.checkpoint(historyObserver.context, DocumentHistory::PrepareStage::SelectionSnapshot);
    if (cancellation.IsCancelled())
        return refused(WandFailure::Cancelled);
    const auto& original = result._input._document.value();
    CanvasDocument after;
    after.id = original.id;
    after.width = original.width;
    after.height = original.height;
    after.resolution = original.resolution;
    after.selection = std::move(selection);
    if (cancellation.IsCancelled())
        return refused(WandFailure::Cancelled);
    after.layers = original.layers;
    if (!Checkpoint(observer, WandLayerSelectionStage::DocumentReady, cancellation))
        return refused(WandFailure::Cancelled);
    if (ValidateDocument(after) != DocumentStatus::Ok)
        return refused(WandFailure::Memory);
    result._document = std::move(after);
    result._history = DocumentHistory::PrepareEdit(
        result._input._history, result._input._document, result._input._active, result._document, result._input._active,
        matched ? L"Magic Wand" : L"Deselect", cancellation, historyObserver);
    if (cancellation.IsCancelled())
        return refused(WandFailure::Cancelled);
    if (!result._history)
        return refused(WandFailure::Memory);
    return result;
}

bool DocumentSession::IsPreparedWandLayerSelectionCurrent(const PreparedWandLayerSelection& prepared) const noexcept
{
    return !prepared._applied && prepared._input._valid && CanApplyHistoryRestore(prepared._input._authority) &&
           CanApplyJobResult(prepared._input._token, 0) &&
           (prepared._history ? history.CanPublish(prepared._history.value())
                              : history.IsCurrent(prepared._input._history));
}

bool DocumentSession::ApplyPreparedWandLayerSelectionIfCurrent(PreparedWandLayerSelection& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (!IsPreparedWandLayerSelectionCurrent(prepared))
        return false;
    if (prepared._history)
    {
        if (!prepared._document || !history.Publish(prepared._history.value()))
            return false;
        std::swap(_document, prepared._document);
        std::swap(_displayCopy, prepared._retiredDisplay);
        NoteDocumentMutation();
    }
    prepared._applied = true;
    return true;
}

std::optional<WandLayerSelectionMemoryPlan> DocumentSession::PlanWandLayerSelectionMemory(
    Vec2 point, WandSettings settings) const noexcept
{
    if (!_document || !ValidSettings(settings) || !ValidPoint(point, _document->width, _document->height) ||
        _document->width > kMaxCanvasSide || _document->height > kMaxCanvasSide ||
        !CanApplyHistoryRestore(CaptureHistoryRestoreAuthority()))
        return std::nullopt;
    WandLayerSelectionMemoryPlan plan;
    const auto historyBytes = history.EditMemoryBytes(_document);
    auto retirement = history.RetirementMemoryBytes(_document);
    if (!historyBytes || !retirement)
        return std::nullopt;
    WandByteSum retained;
    if (!retained.Add(retirement))
        return std::nullopt;
    if (_displayCopy)
    {
        retirement = history.RetirementMemoryBytes(_displayCopy);
        if (!retirement || !retained.Add(retirement))
            return std::nullopt;
    }
    WandByteSum base;
    // Existing history bound includes eight complete metadata copies plus shared stack/trim construction.
    // This graph creates four document copies; remaining allowance covers the retired copies after publish.
    if (!base.Add(historyBytes) || !base.Add(1, sizeof(WandLayerSelectionInput) + sizeof(PreparedWandLayerSelection)) ||
        !base.Add(4096))
        return std::nullopt;
    plan.captureHistoryBytes = base.value;
    const ImageLayer* target = CapturedWandLayer(_document.value(), _activeLayerID);
    const Raster source = target && target->asset ? target->asset->image : Raster{};
    plan.transparentShortcut = !source.HasStorage();
    if (plan.transparentShortcut)
    {
        // Exact one-loop/four-point result, vector proxies and immutable root/control included.
        plan.geometryGrowthBytes = 4096;
    }
    else
    {
        const auto width = static_cast<std::uint64_t>(_document->width);
        const auto height = static_cast<std::uint64_t>(_document->height);
        const auto pixels = width * height; // positive sides are bounded by kMaxCanvasSide above
        plan.sampledPixelsBytes = PixelBuffer::RequiredBytes(static_cast<std::size_t>(4 * pixels));
        if (!plan.sampledPixelsBytes)
            return std::nullopt;
        // Always charge the sparse copy even when its immutable cache was already materialized: admission
        // must not become unsafe if cache residency changes before this graph starts sampling.
        if (source.IsSparse())
        {
            plan.privateSourceBytes = PixelBuffer::RequiredBytes(static_cast<std::size_t>(source.Stride()) *
                                                                 static_cast<std::size_t>(source.Height()));
            if (!plan.privateSourceBytes)
                return std::nullopt;
        }
        plan.maskBytes = pixels; // reserve exactly N once, then initialize within that capacity
        plan.vertexBytes = (width + 1) * (height + 1);
        if (settings.contiguous)
        {
            // Each filled run owns previously unselected pixels. Above/below scans total at most 2N,
            // and each scan step pushes at most one seed, plus the initial seed. Pinned MSVC geometric
            // growth has retained capacity <=1.5*size; old+new allocations together are below 3*size.
            const auto seeds = std::max<std::uint64_t>(4096, 2 * pixels + 1);
            plan.floodGrowthBytes = 3 * seeds * sizeof(std::pair<int, int>) + 256;
        }
        const auto edges = std::min<std::uint64_t>(4 * pixels, MagicWand::kTraceEdgeLimit);
        const auto loops = (edges + 2) / 3;
        // Every coordinate push consumes an edge, even for a discarded short loop. Accepted loops own
        // >=3 points. Charge 3x coordinates and outer headers for all old/new growth overlap, plus 128
        // bytes per potential moved vector for Debug iterator proxies; allocator rounding is headroom.
        plan.geometryGrowthBytes = 3 * edges * sizeof(Vec2) + 3 * loops * (sizeof(SelectionLoop) + 128) + 4096;
    }
    WandByteSum sourcePhase, maskPhase, tracePhase;
    if (!sourcePhase.Add(plan.sampledPixelsBytes) || !sourcePhase.Add(plan.privateSourceBytes) ||
        !maskPhase.Add(plan.sampledPixelsBytes) || !maskPhase.Add(plan.privateSourceBytes) ||
        !maskPhase.Add(plan.maskBytes) || !maskPhase.Add(plan.floodGrowthBytes) ||
        !tracePhase.Add(plan.sampledPixelsBytes) || !tracePhase.Add(plan.privateSourceBytes) ||
        !tracePhase.Add(plan.maskBytes) || !tracePhase.Add(plan.vertexBytes) ||
        !tracePhase.Add(plan.geometryGrowthBytes))
        return std::nullopt;
    plan.sourcePhaseBytes = sourcePhase.value;
    // The private sparse-copy owner ends after sampling, but its aligned pixels can remain in the global
    // recycle pool. Carry that capacity through later phases instead of assuming the process has freed it.
    plan.maskPhaseBytes = maskPhase.value;
    plan.tracePhaseBytes = tracePhase.value;
    if (!base.Add(std::max({plan.sourcePhaseBytes, plan.maskPhaseBytes, plan.tracePhaseBytes})))
        return std::nullopt;
    plan.preparationBytes = base.value;
    // Existing source/document/history/display owners may be last references by acknowledgement. Charge their
    // entire graph, copied metadata and new geometry; no assumption that a current live document retains them.
    if (!retained.Add(plan.captureHistoryBytes) || !retained.Add(plan.geometryGrowthBytes))
        return std::nullopt;
    plan.retirementBytes = retained.value;
    if (!base.Add(plan.retirementBytes))
        return std::nullopt;
    plan.reservationBytes = base.value;
    return plan;
}
