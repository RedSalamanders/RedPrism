#include "WandLayerSelectionWork.h"

#include "BoundedClipper/BoundedSelectionBoolean.h"
#include "Selection.h"
#include "SharedPublicationMemory.h"

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
bool ValidSettings(const WandSettings& settings, bool composite = false) noexcept
{
    return settings.sampleAllLayers == composite && settings.sampleSize <= WandSampleSize::FiveByFive;
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

bool TryAddWandLayerSelectionBytes(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept
{
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (right > maximum - left)
        return false;
    result = left + right;
    return true;
}

WandLayerSelectionAdmission EvaluateWandLayerSelectionAdmission(const WandLayerSelectionMemoryPlan& plan,
                                                                SelectionMode mode, bool hasCurrentSelection,
                                                                std::uint64_t& requiredBytes) noexcept
{
    requiredBytes = 0;
    if (mode > SelectionMode::Subtract)
        return WandLayerSelectionAdmission::InvalidMode;
    if (plan.captureHistoryBytes == 0 || plan.preparationBytes < plan.captureHistoryBytes ||
        plan.retirementBytes == 0 || plan.reservationBytes == 0)
        return WandLayerSelectionAdmission::IncompletePlan;

    std::uint64_t completeReservation = 0;
    if (!TryAddWandLayerSelectionBytes(plan.preparationBytes, plan.retirementBytes, completeReservation))
        return WandLayerSelectionAdmission::Overflow;
    if (completeReservation != plan.reservationBytes)
        return WandLayerSelectionAdmission::IncompletePlan;

    // Preserve refusal for the old incomplete New-only plan. The bounded adapter uses a separately
    // admitted fixed arena and exact-reserve output vectors; none may escape the worker arena.
    if (mode == SelectionMode::Add || (mode == SelectionMode::Subtract && hasCurrentSelection))
    {
        if (!plan.booleanWorkspaceBytes || !plan.booleanOutputBytes || !plan.booleanPhaseBytes)
            return WandLayerSelectionAdmission::UnknownClipperWorkspace;
        WandByteSum boolean;
        if (!boolean.Add(plan.tracePhaseBytes) || !boolean.Add(plan.booleanWorkspaceBytes) ||
            !boolean.Add(2, plan.booleanOutputBytes))
            return WandLayerSelectionAdmission::Overflow;
        const BoundedSelectionBoolean::Limits limits;
        WandByteSum output;
        const auto ownership = SelectionLoops::WorkOwnershipAllowance(limits.outputLoops);
        if (!ownership || !output.Add(limits.outputPoints, sizeof(Vec2)) ||
            !output.Add(limits.outputLoops, sizeof(SelectionLoop)) || !output.Add(sizeof(std::vector<SelectionLoop>)) ||
            !output.Add(ownership))
            return WandLayerSelectionAdmission::Overflow;
        if (boolean.value != plan.booleanPhaseBytes || plan.booleanOutputBytes != output.value ||
            plan.booleanWorkspaceBytes !=
                PixelBuffer::RequiredBytes(BoundedSelectionBoolean::Limits{}.workspaceBytes) ||
            plan.booleanPhaseBytes > plan.preparationBytes - plan.captureHistoryBytes)
            return WandLayerSelectionAdmission::IncompletePlan;
    }

    requiredBytes = completeReservation;
    return WandLayerSelectionAdmission::Ready;
}

std::optional<WandLayerSelectionInput> DocumentSession::PrepareWandLayerSelectionInput(Vec2 point,
                                                                                       WandSettings settings,
                                                                                       bool antialiased,
                                                                                       SelectionMode mode) const
{
    return PrepareWandSelectionInput(point, settings, antialiased, mode, false);
}

std::optional<WandLayerSelectionInput> DocumentSession::PrepareWandCompositeSelectionInput(Vec2 point,
                                                                                           WandSettings settings,
                                                                                           bool antialiased,
                                                                                           SelectionMode mode) const
{
    return PrepareWandSelectionInput(point, settings, antialiased, mode, true);
}

std::optional<WandLayerSelectionInput> DocumentSession::PrepareWandSelectionInput(Vec2 point, WandSettings settings,
                                                                                  bool antialiased, SelectionMode mode,
                                                                                  bool composite) const
{
    if (!_document || mode > SelectionMode::Subtract || !ValidSettings(settings, composite) ||
        !ValidPoint(point, _document->width, _document->height))
        return std::nullopt;
    auto authority = CaptureHistoryRestoreAuthority();
    if (!CanApplyHistoryRestore(authority))
        return std::nullopt;
    auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    std::uint64_t outputHistoryBytes = 0;
    if (!TryAddWandLayerSelectionBytes(history.EditMemoryBytes(_document), 8 * 4096, outputHistoryBytes))
        return std::nullopt;
    return WandLayerSelectionInput(point, settings, antialiased, mode, std::move(authority), std::move(token.value()),
                                   history.CaptureState(), _document.value(), _activeLayerID, composite,
                                   outputHistoryBytes);
}

std::optional<PreparedWandLayerSelection> DocumentSession::PrepareWandLayerSelectionPublication(
    WandLayerSelectionInput input, WandFailure* failure, CancelToken cancellation,
    DocumentHistory::PrepareObserver historyObserver, const WandLayerSelectionObserver* observer,
    std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (input._usesComposite)
    {
        if (failure)
            *failure = WandFailure::Memory;
        return std::nullopt;
    }
    return PrepareWandSelectionPublication(std::move(input), {}, failure, cancellation, historyObserver, observer,
                                           std::move(memory));
}

std::optional<PreparedWandLayerSelection> DocumentSession::PrepareWandCompositeSelectionPublication(
    WandLayerSelectionInput input, Raster composite, WandFailure* failure, CancelToken cancellation,
    DocumentHistory::PrepareObserver historyObserver, const WandLayerSelectionObserver* observer,
    std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (!input._usesComposite)
    {
        if (failure)
            *failure = WandFailure::Memory;
        return std::nullopt;
    }
    return PrepareWandSelectionPublication(std::move(input), std::move(composite), failure, cancellation,
                                           historyObserver, observer, std::move(memory));
}

std::optional<PreparedWandLayerSelection> DocumentSession::PrepareWandSelectionPublication(
    WandLayerSelectionInput input, Raster composite, WandFailure* failure, CancelToken cancellation,
    DocumentHistory::PrepareObserver historyObserver, const WandLayerSelectionObserver* observer,
    std::shared_ptr<const SharedPublicationMemory> memory)
{
    const auto refused = [&](WandFailure reason) -> std::optional<PreparedWandLayerSelection>
    {
        if (failure)
            *failure = reason;
        return std::nullopt;
    };
    if (cancellation.IsCancelled())
        return refused(WandFailure::Cancelled);
    if (!input._valid || !input._document || input._token.target || input._mode > SelectionMode::Subtract ||
        !ValidSettings(input._settings, input._usesComposite) || input._document->width != input._token.canvasWidth ||
        input._document->height != input._token.canvasHeight ||
        !ValidPoint(input._point, input._document->width, input._document->height))
        return refused(WandFailure::Memory);
    if (input._usesComposite &&
        (!composite.HasStorage() || composite.Format() != RasterFormat::Bgra8888 ||
         composite.Width() != input._document->width || composite.Height() != input._document->height))
        return refused(WandFailure::Memory);
    if (failure)
        *failure = WandFailure::None;
    if (!Checkpoint(observer, WandLayerSelectionStage::BeforeSource, cancellation))
        return refused(WandFailure::Cancelled);
    const auto& before = input._document.value();
    const ImageLayer* target = input._usesComposite ? nullptr : CapturedWandLayer(before, input._active);
    const Raster source = input._usesComposite ? composite : target && target->asset ? target->asset->image : Raster{};
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
        if (!target && !input._usesComposite)
            return refused(WandFailure::Memory);
        const Raster sampled = input._usesComposite ? source
                                                    : RenderLayerPixels(source, target->transform, before.width,
                                                                        before.height, cancellation);
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
    const bool matched = outline.has_value();
    std::optional<DocumentSelection> selection = before.selection;
    if (matched)
    {
        std::optional<std::vector<SelectionLoop>> changed;
        if (input._mode == SelectionMode::New)
            changed = std::move(outline.value());
        else if (input._mode == SelectionMode::Add || before.selection)
        {
            auto clipped = BoundedSelectionBoolean::Run(outline.value(),
                                                        SelectionGeometry::Rect(0, 0, before.width, before.height),
                                                        BoundedSelectionBoolean::Operation::Intersect, cancellation);
            if (!clipped)
                return refused(cancellation.IsCancelled() ? WandFailure::Cancelled : WandFailure::Memory);
            if (before.selection)
            {
                changed = BoundedSelectionBoolean::Run(before.selection->loops.Get(), clipped.value(),
                                                       input._mode == SelectionMode::Add
                                                           ? BoundedSelectionBoolean::Operation::Union
                                                           : BoundedSelectionBoolean::Operation::Subtract,
                                                       cancellation);
                if (!changed)
                    return refused(cancellation.IsCancelled() ? WandFailure::Cancelled : WandFailure::Memory);
            }
            else
                changed = std::move(clipped.value());
        }
        // Subtract with no selection remains the exact reference no-op, after tracing the original click.
        if (changed)
        {
            auto geometry = SelectionLoops::AdoptForWork(std::move(changed.value()), cancellation, memory);
            if (!geometry)
                return refused(cancellation.IsCancelled() ? WandFailure::Cancelled : WandFailure::Memory);
            selection = DocumentSelection{input._antialiased, std::move(geometry.value())};
        }
        else if (cancellation.IsCancelled())
            return refused(WandFailure::Cancelled);
    }
    else if (input._mode == SelectionMode::New)
        selection.reset();
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
        matched ? L"Magic Wand" : L"Deselect", cancellation, historyObserver, memory);
    if (cancellation.IsCancelled())
        return refused(WandFailure::Cancelled);
    if (!result._history)
        return refused(WandFailure::Memory);
    if (memory.use_count() != 0)
    {
        WandByteSum output;
        if (!result._input._outputHistoryBytes || !output.Add(result._input._outputHistoryBytes) ||
            !output.Add(SharedPublicationMemory::OwnershipAllowance()))
            return refused(WandFailure::Memory);
        if (result._document->selection && result._document->selection->loops.Identity())
        {
            const auto& loops = result._document->selection->loops;
            const auto allowance = SelectionLoops::WorkOwnershipAllowance(loops.size());
            if (!allowance || !output.Add(loops.StorageBytes()) || !output.Add(allowance))
                return refused(WandFailure::Memory);
        }
        result._outputBytes = output.value;
    }
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
    Vec2 point, WandSettings settings, SelectionMode mode) const noexcept
{
    return PlanWandSelectionMemory(point, settings, false, mode);
}

std::optional<WandLayerSelectionMemoryPlan> DocumentSession::PlanWandCompositeSelectionMemory(
    Vec2 point, WandSettings settings, SelectionMode mode) const noexcept
{
    return PlanWandSelectionMemory(point, settings, true, mode);
}

std::optional<WandLayerSelectionMemoryPlan> DocumentSession::PlanWandSelectionMemory(Vec2 point, WandSettings settings,
                                                                                     bool composite,
                                                                                     SelectionMode mode) const noexcept
{
    if (!_document || mode > SelectionMode::Subtract || !ValidSettings(settings, composite) ||
        !ValidPoint(point, _document->width, _document->height) || _document->width > kMaxCanvasSide ||
        _document->height > kMaxCanvasSide || !CanApplyHistoryRestore(CaptureHistoryRestoreAuthority()))
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
        !base.Add(4096) || !base.Add(8, 4096))
        return std::nullopt;
    plan.captureHistoryBytes = base.value;
    const ImageLayer* target = composite ? nullptr : CapturedWandLayer(_document.value(), _activeLayerID);
    const Raster source = target && target->asset ? target->asset->image : Raster{};
    plan.transparentShortcut = !composite && !source.HasStorage();
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
        if (!composite && source.IsSparse())
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
    const bool boolean = mode == SelectionMode::Add || (mode == SelectionMode::Subtract && _document->selection);
    if (boolean)
    {
        const BoundedSelectionBoolean::Limits limits;
        // The existing selection is already held/charged by capture. Refuse before copying a source when
        // it cannot enter the adapter. The four canvas corners need the input bound's final four points.
        if (_document->selection && _document->selection->loops.PointCount() > limits.outputPoints)
            return std::nullopt;
        WandByteSum output;
        const auto ownership = SelectionLoops::WorkOwnershipAllowance(limits.outputLoops);
        if (!ownership || !output.Add(limits.outputPoints, sizeof(Vec2)) ||
            !output.Add(limits.outputLoops, sizeof(SelectionLoop)) || !output.Add(sizeof(std::vector<SelectionLoop>)) ||
            !output.Add(ownership))
            return std::nullopt;
        plan.booleanOutputBytes = output.value;
        plan.booleanWorkspaceBytes = PixelBuffer::RequiredBytes(limits.workspaceBytes);
        // Include capacity rounding and the owned PixelAllocation record; the engine sees only the
        // requested arena capacity. A zero aligned-allocation envelope refuses before source capture.
        if (!plan.booleanWorkspaceBytes)
            return std::nullopt;
        WandByteSum phase;
        if (!phase.Add(plan.tracePhaseBytes) || !phase.Add(plan.booleanWorkspaceBytes) ||
            !phase.Add(2, plan.booleanOutputBytes))
            return std::nullopt;
        plan.booleanPhaseBytes = phase.value;
    }
    if (!base.Add(std::max({plan.sourcePhaseBytes, plan.maskPhaseBytes, plan.tracePhaseBytes, plan.booleanPhaseBytes})))
        return std::nullopt;
    plan.preparationBytes = base.value;
    // Existing source/document/history/display owners may be last references by acknowledgement. Charge their
    // entire graph, copied metadata and new geometry; no assumption that a current live document retains them.
    if (!retained.Add(plan.captureHistoryBytes) || !retained.Add(plan.geometryGrowthBytes) ||
        !retained.Add(plan.booleanWorkspaceBytes) || !retained.Add(2, plan.booleanOutputBytes) ||
        (composite && !retained.Add(plan.sampledPixelsBytes)))
        return std::nullopt;
    plan.retirementBytes = retained.value;
    if (!base.Add(plan.retirementBytes))
        return std::nullopt;
    plan.reservationBytes = base.value;
    return plan;
}
