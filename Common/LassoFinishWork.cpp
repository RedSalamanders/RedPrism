#include "LassoFinishWork.h"

#include "SharedPublicationMemory.h"

#include <limits>
#include <type_traits>
#include <utility>

namespace
{
[[nodiscard]] bool SameSelection(const std::optional<DocumentSelection>& a, const std::optional<DocumentSelection>& b,
                                 const CancelToken& cancellation) noexcept
{
    if (a.has_value() != b.has_value())
        return false;
    if (!a)
        return true;
    if (a->antialiased != b->antialiased || a->loops.size() != b->loops.size())
        return false;
    std::uint64_t work = 0;
    for (std::size_t loop = 0; loop < a->loops.size(); ++loop)
    {
        if ((work++ & 63) == 0 && cancellation.IsCancelled())
            return false;
        const auto& left = a->loops[loop];
        const auto& right = b->loops[loop];
        if (left.size() != right.size())
            return false;
        for (std::size_t point = 0; point < left.size(); ++point)
        {
            if ((work++ & 63) == 0 && cancellation.IsCancelled())
                return false;
            if (left[point] != right[point])
                return false;
        }
    }
    return true;
}
[[nodiscard]] const wchar_t* FinishName(LassoKind kind) noexcept
{
    switch (kind)
    {
    case LassoKind::Freehand:
        return L"Lasso";
    case LassoKind::Polygonal:
        return L"Polygonal Lasso";
    case LassoKind::Rectangle:
        return L"Rectangular Marquee";
    case LassoKind::Ellipse:
        return L"Elliptical Marquee";
    }
    return L"Lasso";
}
} // namespace

std::optional<LassoFinishInput> DocumentSession::PrepareLassoFinishInput(LassoFinishTerminal terminal)
{
    if (_lassoFinishSerial == (std::numeric_limits<std::uint64_t>::max)() || !_document ||
        terminal.display.key.opening != _lassoOpening || !terminal.display.key.revision ||
        !LassoFinishGeometry::Plan(terminal.display, _document->selection))
        return std::nullopt;
    auto authority = CaptureHistoryRestoreAuthority();
    if (!CanApplyHistoryRestore(authority))
        return std::nullopt; // The selected terminal must first close its actual draft after full admission.
    auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto historyBytes = history.EditMemoryBytes(_document);
    if (historyBytes > maximum - 8 * 4096)
        return std::nullopt;
    // AA is event-time Finish state, matching the original ApplySelection caller.
    terminal.display.antialiased = selectionAntialiased;
    LassoFinishInput result(std::move(terminal), std::move(authority), std::move(token.value()), history.CaptureState(),
                            _document.value(), _activeLayerID, historyBytes + 8 * 4096, _lassoFinishSerial + 1);
    ++_lassoFinishSerial;
    _lassoFinishPendingSerial = _lassoFinishSerial;
    return result;
}

std::optional<PreparedLassoFinish> DocumentSession::PrepareLassoFinishPublication(
    LassoFinishInput input, CancelToken cancellation, DocumentHistory::PrepareObserver historyObserver,
    const LassoFinishObserver* observer, std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (!input._valid || !input._document || input._token.target ||
        input._document->width != input._token.canvasWidth || input._document->height != input._token.canvasHeight ||
        cancellation.IsCancelled())
        return std::nullopt;
    auto geometry = LassoFinishGeometry::Prepare(input._terminal.display, input._document->width,
                                                 input._document->height, input._document->selection, cancellation,
                                                 observer ? &observer->geometry : nullptr, memory);
    if (!geometry || cancellation.IsCancelled())
        return std::nullopt;
    const auto disposition = geometry->disposition;
    const auto kind = geometry->kind;
    PreparedLassoFinish result(std::move(input));
    if (disposition == LassoFinishGeometry::Disposition::Keep)
        return result;
    auto selection = std::move(geometry->selection);
    const bool unchanged = SameSelection(selection, result._input._document->selection, cancellation);
    if (cancellation.IsCancelled())
        return std::nullopt;
    if (unchanged)
        return result;
    if (historyObserver.checkpoint)
        historyObserver.checkpoint(historyObserver.context, DocumentHistory::PrepareStage::SelectionSnapshot);
    if (cancellation.IsCancelled())
        return std::nullopt;
    const auto& before = result._input._document.value();
    CanvasDocument after;
    after.id = before.id;
    after.width = before.width;
    after.height = before.height;
    after.resolution = before.resolution;
    after.selection = std::move(selection);
    after.layers = before.layers;
    if (cancellation.IsCancelled() || ValidateDocument(after) != DocumentStatus::Ok)
        return std::nullopt;
    result._document = std::move(after);
    result._history = DocumentHistory::PrepareEdit(
        result._input._history, result._input._document, result._input._active, result._document, result._input._active,
        disposition == LassoFinishGeometry::Disposition::Clear ? L"Deselect" : FinishName(kind), cancellation,
        historyObserver, memory);
    if (!result._history || cancellation.IsCancelled())
        return std::nullopt;
    if (memory.use_count() != 0)
    {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        std::uint64_t bytes = result._input._outputHistoryBytes;
        const auto add = [&](std::uint64_t extra)
        {
            if (extra > maximum - bytes)
                return false;
            bytes += extra;
            return true;
        };
        if (!bytes || !add(SharedPublicationMemory::OwnershipAllowance()))
            return std::nullopt;
        if (result._document->selection && result._document->selection->loops.Identity())
        {
            const auto& loops = result._document->selection->loops;
            const auto allowance = SelectionLoops::WorkOwnershipAllowance(loops.size());
            if (!allowance || !add(loops.StorageBytes()) || !add(allowance))
                return std::nullopt;
        }
        result._outputBytes = bytes;
    }
    return result;
}

DocumentHistory::ResidencyValidation PreparedLassoFinish::TryAcquireHistoryResidency(
    DocumentHistory::ResidencyPermit& permit) noexcept
{
    if (_applied || !_input._valid)
        return DocumentHistory::ResidencyValidation::Invalid;
    return _history ? permit.TryAcquire(_history.value()) : DocumentHistory::ResidencyValidation::Ready;
}
bool PreparedLassoFinish::RefreshHistoryResidency(CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (_applied || !_input._valid || cancellation.IsCancelled())
        return false;
    return !_history || (_document && DocumentHistory::RefreshPreparedResidency(_history.value(), _document,
                                                                                cancellation, observer));
}
bool DocumentSession::IsPreparedLassoFinishCurrent(const PreparedLassoFinish& prepared) const noexcept
{
    return !prepared._applied && prepared._input._valid &&
           prepared._input._terminal.display.key.opening == _lassoOpening && prepared._input._intent &&
           prepared._input._intent == _lassoFinishPendingSerial && CanApplyHistoryRestore(prepared._input._authority) &&
           CanApplyJobResult(prepared._input._token, 0) &&
           (prepared._history ? history.CanPublish(prepared._history.value())
                              : history.IsCurrent(prepared._input._history));
}
bool DocumentSession::ApplyPreparedLassoFinishIfCurrent(PreparedLassoFinish& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (!IsPreparedLassoFinishCurrent(prepared))
        return false;
    if (prepared._history)
    {
        if (!prepared._document || !history.Publish(prepared._history.value()))
            return false;
        std::swap(_document, prepared._document);
        std::swap(_displayCopy, prepared._retiredDisplay);
        NoteDocumentMutation();
    }
    _lassoFinishPendingSerial = 0;
    prepared._applied = true;
    return true;
}
