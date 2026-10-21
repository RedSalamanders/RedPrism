#include "InverseSelectionWork.h"
#include "Selection.h"
#include "SharedPublicationMemory.h"

#include <limits>
#include <type_traits>

namespace
{
struct InverseByteSum final
{
    std::uint64_t value = 0;
    [[nodiscard]] bool Add(std::uint64_t bytes) noexcept
    {
        if (bytes > (std::numeric_limits<std::uint64_t>::max)() - value)
            return false;
        value += bytes;
        return true;
    }
    [[nodiscard]] bool Add(std::uint64_t count, std::uint64_t width) noexcept
    {
        return width != 0 && count <= (std::numeric_limits<std::uint64_t>::max)() / width && Add(count * width);
    }
};
[[nodiscard]] bool ReducedLimits(const BoundedSelectionBoolean::Limits& value) noexcept
{
    const BoundedSelectionBoolean::Limits production;
    return value.workspaceBytes != 0 && value.workspaceBytes <= production.workspaceBytes && value.inputPoints != 0 &&
           value.inputPoints <= production.inputPoints && value.inputLoops != 0 &&
           value.inputLoops <= production.inputLoops && value.outputPoints != 0 &&
           value.outputPoints <= production.outputPoints && value.outputLoops != 0 &&
           value.outputLoops <= production.outputLoops;
}
[[nodiscard]] std::optional<bool> EqualGeometry(const SelectionLoops& left, const SelectionLoops& right,
                                                CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return std::nullopt;
    if (left.Identity() == right.Identity())
        return true;
    if (left.size() != right.size())
        return false;
    std::uint64_t work = 0;
    for (std::size_t loop = 0; loop < left.size(); ++loop)
    {
        const auto& a = left.Get()[loop];
        const auto& b = right.Get()[loop];
        if (((work++ & 63) == 0) && cancellation.IsCancelled())
            return std::nullopt;
        if (a.size() != b.size())
            return false;
        for (std::size_t point = 0; point < a.size(); ++point)
        {
            if (((work++ & 63) == 0) && cancellation.IsCancelled())
                return std::nullopt;
            if (a[point] != b[point])
                return false;
        }
    }
    return cancellation.IsCancelled() ? std::nullopt : std::optional(true);
}
} // namespace

std::optional<InverseSelectionMemoryPlan> DocumentSession::PlanInverseSelectionMemory() const noexcept
{
    if (!_document || !CanApplyHistoryRestore(CaptureHistoryRestoreAuthority()))
        return std::nullopt;
    InverseSelectionMemoryPlan result;
    const auto historyBytes = history.EditMemoryBytes(_document);
    const auto retiredBytes = HistoryRestoreRetirementMemoryBytes();
    if (!historyBytes || !retiredBytes)
        return std::nullopt;
    InverseByteSum capture;
    // Keep the original candidate and one refreshed resident index/stack concurrently.
    if (!capture.Add(2, historyBytes) || !capture.Add(8, 4096) ||
        !capture.Add(sizeof(InverseSelectionInput) + sizeof(PreparedInverseSelection) + 4096))
        return std::nullopt;
    result.captureHistoryBytes = capture.value;
    result.hasSelection = _document->selection.has_value();
    if (result.hasSelection)
    {
        const BoundedSelectionBoolean::Limits limits;
        const auto& input = _document->selection->loops;
        if (input.PointCount() > limits.inputPoints - 4 || input.size() >= limits.inputLoops)
            return std::nullopt;
        InverseByteSum output;
        const auto ownership = SelectionLoops::WorkOwnershipAllowance(limits.outputLoops);
        if (!ownership || !output.Add(limits.outputPoints, sizeof(Vec2)) ||
            !output.Add(limits.outputLoops, sizeof(SelectionLoop)) || !output.Add(sizeof(std::vector<SelectionLoop>)) ||
            !output.Add(ownership))
            return std::nullopt;
        result.geometryOutputBytes = output.value;
        result.workspaceBytes = PixelBuffer::RequiredBytes(limits.workspaceBytes);
        if (!result.workspaceBytes)
            return std::nullopt;
    }
    InverseByteSum preparing;
    // The arena paths and conversion output coexist. Adoption moves the sole ordinary vector output.
    if (!preparing.Add(result.captureHistoryBytes) || !preparing.Add(result.workspaceBytes) ||
        !preparing.Add(result.geometryOutputBytes) || !preparing.Add(4096))
        return std::nullopt;
    result.preparationBytes = preparing.value;
    InverseByteSum retiring;
    if (!retiring.Add(retiredBytes) || !retiring.Add(result.captureHistoryBytes) ||
        !retiring.Add(result.geometryOutputBytes) || !retiring.Add(result.workspaceBytes))
        return std::nullopt;
    result.retirementBytes = retiring.value;
    if (!preparing.Add(result.retirementBytes))
        return std::nullopt;
    result.reservationBytes = preparing.value;
    return result;
}

std::optional<InverseSelectionInput> DocumentSession::PrepareInverseSelectionInput() const
{
    if (!_document)
        return std::nullopt;
    auto authority = CaptureHistoryRestoreAuthority();
    auto token = CaptureJobToken();
    std::uint64_t historyBytes = 0;
    const auto currentHistoryBytes = history.EditMemoryBytes(_document);
    if (!CanApplyHistoryRestore(authority) || !token || !currentHistoryBytes ||
        !TryAddWandLayerSelectionBytes(currentHistoryBytes, currentHistoryBytes, historyBytes) ||
        !TryAddWandLayerSelectionBytes(historyBytes, 8 * 4096, historyBytes))
        return std::nullopt;
    return InverseSelectionInput(WandLayerSelectionInput({}, {}, true, SelectionMode::New, std::move(authority),
                                                         std::move(*token), history.CaptureState(), *_document,
                                                         _activeLayerID, false, historyBytes));
}

std::optional<PreparedInverseSelection> DocumentSession::PrepareInverseSelectionPublication(
    InverseSelectionInput captured, CancelToken cancellation, DocumentHistory::PrepareObserver historyObserver,
    std::shared_ptr<const SharedPublicationMemory> memory, const InverseSelectionObserver* observer)
{
    auto& input = captured._input;
    if (cancellation.IsCancelled() || !input._valid || !input._document || input._token.target ||
        input._document->width != input._token.canvasWidth || input._document->height != input._token.canvasHeight ||
        (observer && !ReducedLimits(observer->limits)))
        return std::nullopt;
    PreparedWandLayerSelection result(std::move(input));
    const auto& before = *result._input._document;
    // Legacy Inverse with absent selection is an exact no-op, retaining redo and the original generation.
    if (!before.selection)
        return PreparedInverseSelection(std::move(result));
    const auto rectangle = SelectionGeometry::Rect(0, 0, before.width, before.height);
    auto changed = BoundedSelectionBoolean::Run(
        rectangle, before.selection->loops.Get(), BoundedSelectionBoolean::Operation::Subtract, cancellation,
        observer ? observer->limits : BoundedSelectionBoolean::Limits{}, observer ? observer->statistics : nullptr,
        observer ? observer->checkpoint : nullptr, observer ? observer->context : nullptr);
    if (!changed || cancellation.IsCancelled())
        return std::nullopt;
    auto geometry = SelectionLoops::AdoptForWork(std::move(*changed), cancellation, memory);
    if (!geometry)
        return std::nullopt;
    DocumentSelection selected{before.selection->antialiased, std::move(*geometry)};
    const auto unchanged = EqualGeometry(selected.loops, before.selection->loops, cancellation);
    if (!unchanged)
        return std::nullopt;
    if (*unchanged)
        return PreparedInverseSelection(std::move(result));
    if (historyObserver.checkpoint)
        historyObserver.checkpoint(historyObserver.context, DocumentHistory::PrepareStage::SelectionSnapshot);
    if (cancellation.IsCancelled())
        return std::nullopt;
    CanvasDocument after;
    after.id = before.id;
    after.width = before.width;
    after.height = before.height;
    after.resolution = before.resolution;
    after.selection = std::move(selected);
    after.layers = before.layers;
    if (cancellation.IsCancelled() || ValidateDocument(after) != DocumentStatus::Ok)
        return std::nullopt;
    result._document = std::move(after);
    result._history = DocumentHistory::PrepareEdit(result._input._history, result._input._document,
                                                   result._input._active, result._document, result._input._active,
                                                   L"Inverse", cancellation, historyObserver, memory);
    if (!result._history || cancellation.IsCancelled())
        return std::nullopt;
    if (memory.use_count() != 0)
    {
        InverseByteSum output;
        if (!output.Add(result._input._outputHistoryBytes) ||
            !output.Add(SharedPublicationMemory::OwnershipAllowance()))
            return std::nullopt;
        if (result._document->selection->loops.Identity())
        {
            const auto& loops = result._document->selection->loops;
            const auto allowance = SelectionLoops::WorkOwnershipAllowance(loops.size());
            if (!allowance || !output.Add(loops.StorageBytes()) || !output.Add(allowance))
                return std::nullopt;
        }
        result._outputBytes = output.value;
    }
    return PreparedInverseSelection(std::move(result));
}

bool DocumentSession::IsPreparedInverseSelectionCurrent(const PreparedInverseSelection& prepared) const noexcept
{
    return IsPreparedWandLayerSelectionCurrent(prepared._publication);
}

bool DocumentSession::ApplyPreparedInverseSelectionIfCurrent(PreparedInverseSelection& prepared) noexcept
{
    return ApplyPreparedWandLayerSelectionIfCurrent(prepared._publication);
}

DocumentHistory::ResidencyValidation PreparedInverseSelection::TryAcquireHistoryResidency(
    DocumentHistory::ResidencyPermit& permit) noexcept
{
    auto& prepared = _publication;
    if (prepared._applied || !prepared._input._valid)
        return DocumentHistory::ResidencyValidation::Invalid;
    return prepared._history ? permit.TryAcquire(*prepared._history) : DocumentHistory::ResidencyValidation::Ready;
}

bool PreparedInverseSelection::RefreshHistoryResidency(CancelToken cancellation,
                                                       DocumentHistory::PrepareObserver observer)
{
    auto& prepared = _publication;
    if (prepared._applied || !prepared._input._valid || cancellation.IsCancelled())
        return false;
    return !prepared._history ||
           (prepared._document &&
            DocumentHistory::RefreshPreparedResidency(*prepared._history, prepared._document, cancellation, observer));
}
