#include "SelectionNudgeWork.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

// Only this producer may adopt its construction-cached geometry. No uncancellable second path traversal.
class SelectionNudgeBuilder final
{
  public:
    static std::optional<SelectionLoops> Build(const SelectionLoops& source, int dx, int dy, CancelToken cancellation,
                                               SelectionNudgeObserver observer)
    {
        std::uint64_t work = 0;
        const auto checkpoint = [&](SelectionNudgeStage stage)
        {
            if (observer.checkpoint)
                observer.checkpoint(observer.context, stage, work);
            return !cancellation.IsCancelled();
        };
        if (!checkpoint(SelectionNudgeStage::Begin))
            return std::nullopt;
        if (dx == 0 && dy == 0)
            return checkpoint(SelectionNudgeStage::Complete) ? std::optional(source) : std::nullopt;
        std::vector<SelectionLoop> loops;
        loops.reserve(source.size());
        SelectionLoops result;
        for (const auto& loop : source)
        {
            if ((work++ % 1024) == 0 && !checkpoint(SelectionNudgeStage::Translate))
                return std::nullopt;
            SelectionLoop translated;
            translated.reserve(loop.size());
            for (const auto& point : loop)
            {
                if ((work++ % 1024) == 0 && !checkpoint(SelectionNudgeStage::Translate))
                    return std::nullopt;
                const Vec2 next{point.x + static_cast<double>(dx), point.y + static_cast<double>(dy)};
                translated.push_back(next);
                if (std::isfinite(next.x) && std::isfinite(next.y))
                {
                    if (!result._bounds)
                        result._bounds = SelectionBounds{next.x, next.y, next.x, next.y};
                    else
                    {
                        result._bounds.value().minX = std::min(result._bounds.value().minX, next.x);
                        result._bounds.value().minY = std::min(result._bounds.value().minY, next.y);
                        result._bounds.value().maxX = std::max(result._bounds.value().maxX, next.x);
                        result._bounds.value().maxY = std::max(result._bounds.value().maxY, next.y);
                    }
                }
            }
            result._pointCount += translated.size();
            result._storageBytes += translated.capacity() * sizeof(Vec2);
            loops.push_back(std::move(translated));
        }
        if (!checkpoint(SelectionNudgeStage::Complete))
            return std::nullopt;
        if (!loops.empty())
        {
            result._storageBytes += sizeof(loops) + loops.capacity() * sizeof(SelectionLoop);
            result._loops = std::make_shared<const std::vector<SelectionLoop>>(std::move(loops));
        }
        return !cancellation.IsCancelled() ? std::optional(std::move(result)) : std::nullopt;
    }
};

std::optional<SelectionNudgeInput> DocumentSession::PrepareSelectionNudgeInput(int dx, int dy) const
{
    if (!_document || !_document.value().selection || _document.value().selection.value().IsEmpty() ||
        _selectionMoveOrigin || _selectionMoveOpening == (std::numeric_limits<std::uint64_t>::max)())
        return std::nullopt;
    auto authority = CaptureHistoryRestoreAuthority();
    if (!CanApplyHistoryRestore(authority))
        return std::nullopt;
    auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    return SelectionNudgeInput(std::move(authority), std::move(token.value()), history.CaptureState(),
                               _document.value(), _activeLayerID, _selectionMoveOpening, dx, dy);
}

std::optional<PreparedSelectionNudge> DocumentSession::PrepareSelectionNudgePublication(
    SelectionNudgeInput input, CancelToken cancellation, DocumentHistory::PrepareObserver observer,
    const SelectionNudgeObserver* geometryObserver)
{
    if (cancellation.IsCancelled() || !input._valid || !input._document || !input._document.value().selection ||
        input._document.value().selection.value().IsEmpty() || input._token.target ||
        input._document.value().width != input._token.canvasWidth ||
        input._document.value().height != input._token.canvasHeight)
        return std::nullopt;
    auto loops =
        SelectionNudgeBuilder::Build(input._document.value().selection.value().loops, input._dx, input._dy,
                                     cancellation, geometryObserver ? *geometryObserver : SelectionNudgeObserver{});
    if (!loops || cancellation.IsCancelled())
        return std::nullopt;
    PreparedSelectionNudge result(std::move(input));
    if (result._input._dx == 0 && result._input._dy == 0)
    {
        result._history = DocumentHistory::PrepareEdit(
            result._input._history, result._input._document, result._input._active, result._input._document,
            result._input._active, L"Move Selection", cancellation, observer);
        return result._history && !cancellation.IsCancelled() ? std::optional(std::move(result)) : std::nullopt;
    }
    const auto& before = result._input._document.value();
    CanvasDocument after;
    after.id = before.id;
    after.width = before.width;
    after.height = before.height;
    after.resolution = before.resolution;
    after.selection = DocumentSelection{before.selection.value().antialiased, std::move(loops.value())};
    if (cancellation.IsCancelled())
        return std::nullopt;
    after.layers = before.layers;
    if (cancellation.IsCancelled() || ValidateDocument(after) != DocumentStatus::Ok)
        return std::nullopt;
    result._document = std::move(after);
    result._history = DocumentHistory::PrepareEdit(result._input._history, result._input._document,
                                                   result._input._active, result._document, result._input._active,
                                                   L"Move Selection", cancellation, observer);
    return result._history && !cancellation.IsCancelled() ? std::optional(std::move(result)) : std::nullopt;
}

DocumentHistory::ResidencyValidation PreparedSelectionNudge::TryAcquireHistoryResidency(
    DocumentHistory::ResidencyPermit& permit) noexcept
{
    if (_applied || !_input._valid)
        return DocumentHistory::ResidencyValidation::Invalid;
    return _history ? permit.TryAcquire(_history.value()) : DocumentHistory::ResidencyValidation::Ready;
}

bool PreparedSelectionNudge::RefreshHistoryResidency(CancelToken cancellation,
                                                     DocumentHistory::PrepareObserver observer)
{
    if (_applied || !_input._valid || cancellation.IsCancelled())
        return false;
    // A real nudge sampled the after-document's outline; the zero offset has no after-document and sampled
    // no new trim decision. Preserve each publication's exact current-topology witness during refresh.
    return !_history || DocumentHistory::RefreshPreparedResidency(
                            _history.value(), _document ? _document : _input._document, cancellation, observer);
}

bool DocumentSession::IsPreparedSelectionNudgeCurrent(const PreparedSelectionNudge& prepared) const noexcept
{
    return !prepared._applied && prepared._input._valid && prepared._history &&
           (prepared._document || (prepared._input._dx == 0 && prepared._input._dy == 0)) && !_selectionMoveOrigin &&
           _selectionMoveOpening == prepared._input._opening &&
           _selectionMoveOpening != (std::numeric_limits<std::uint64_t>::max)() &&
           CanApplyHistoryRestore(prepared._input._authority) && CanApplyJobResult(prepared._input._token, 0) &&
           history.CanPublish(prepared._history.value());
}

bool DocumentSession::ApplyPreparedSelectionNudgeIfCurrent(PreparedSelectionNudge& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (!IsPreparedSelectionNudgeCurrent(prepared) || !history.Publish(prepared._history.value()))
        return false;
    if (prepared._document)
        std::swap(_document, prepared._document);
    std::swap(_displayCopy, prepared._retiredDisplay);
    ++_selectionMoveOpening; // The direct Nudge opens and closes one distinct outline operation.
    NoteDocumentMutation();  // MoveSelection, including a zero offset.
    NoteDocumentMutation();  // EndEdit.
    prepared._applied = true;
    return true;
}

std::uint64_t DocumentSession::SelectionNudgeMemoryBytes() const noexcept
{
    if (!_document || !_document.value().selection || _document.value().selection.value().IsEmpty())
        return 0;
    auto bytes = history.EditMemoryBytes(_document);
    // Preparation may overlap the original history and a refreshed index/container set.
    if (!bytes || bytes > (std::numeric_limits<std::uint64_t>::max)() / 2)
        return 0;
    bytes *= 2;
    const auto add = [&](std::uint64_t count, std::uint64_t size)
    {
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if (size && count > maximum / size)
            return false;
        const auto extra = count * size;
        if (extra > maximum - bytes)
            return false;
        bytes += extra;
        return true;
    };
    const auto& loops = _document.value().selection.value().loops;
    // Reserve-only output, one new allocation per loop; allow a second full geometry-sized peak and allocator
    // overhead. Shared snapshots do not duplicate these points. Existing graph retirement is charged separately.
    return add(loops.PointCount(), 2 * sizeof(Vec2)) && add(loops.size(), 2 * sizeof(SelectionLoop) + 128) &&
                   add(1, 4096 + sizeof(SelectionNudgeInput) + sizeof(PreparedSelectionNudge))
               ? bytes
               : 0;
}
