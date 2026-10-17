#include "SelectionOutlineGestureJob.h"
#include "BrushStroke.h"
#include "LassoFinishWork.h"
#include "LassoIntent.h"
#include "LassoOpeningWork.h"
#include "LocallyOwnedPublication.h"
#include "SelectionOutlineGestureMemory.h"
#include "WarpStroke.h"

#include <limits>

#include <cmath>
#include <exception>
#include <windows.h>

// Allocated on the original worker before source capture. Its last owner retires through the original
// temporary partition; no owner callback allocates or destroys a large prepared geometry/history graph.
struct SelectionOutlineDrawing final
{
    LassoOpeningSettings settings;
    BoundedSelectionBoolean::Limits geometryLimits;
    SelectionMode mode = SelectionMode::New;
    std::optional<LassoIntent> openingIntent, finishIntent;
    std::optional<HistoryRestoreAuthority> editor;
    std::optional<DocumentHistory::Authority> history;
    std::optional<LassoOpeningInput> openingInput;
    std::optional<PreparedLassoOpening> opening;
    std::optional<LassoFinishInput> finishInput;
    std::optional<PreparedLassoFinish> finish;
    bool chosen = false, constrainArmed = false;
};

namespace
{
std::optional<SelectionOutlineGestureMemory> DescribeMarqueeMemory(const DocumentSession& session,
                                                                   std::optional<LassoKind> newMarqueeKind,
                                                                   SelectionMode mode = SelectionMode::New) noexcept
{
    const auto& document = session.Document();
    const auto source = session.CaptureDocumentReadAuthority();
    const auto editor = session.CaptureHistoryRestoreAuthority();
    if (!source || !document || ((!document->selection || document->selection->IsEmpty()) && !newMarqueeKind) ||
        !session.CanApplyHistoryRestore(editor))
        return std::nullopt;
    const auto history = session.history.EditMemoryBytes(document);
    const auto retirement = session.HistoryRestoreRetirementMemoryBytes();
    const auto storage = document->selection ? document->selection->loops.StorageBytes() : 0;
    const auto controls =
        document->selection ? SelectionLoops::WorkOwnershipAllowance(document->selection->loops.size()) : 0;
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (!history || !retirement || ((!storage || !controls) && !newMarqueeKind) || controls > maximum - storage)
        return std::nullopt;
    const auto geometry = storage + controls;
    // One original descriptor covers every branch peak, including retained earlier stage payloads.
    // H is the complete eight-copy/index/trim envelope of the source. The scalar Begin/End graph adds
    // at most one pending snapshot and one entry with the same layer/raster occurrence bounds. Two H
    // cover its future cached metadata envelope; four H cover overlapping candidate/refresh records.
    // Geometry translation needs one new point/vector graph and one construction overlap. Deselect
    // allocates no geometry. The old geometry, display, pending/past/future and selected-id owners are
    // charged by R, even when a foreign edit drops their last live owner during cancellation.
    constexpr std::uint64_t wrappers = 4096 + sizeof(SelectionOutlineGestureJob) +
                                       sizeof(SelectionOutlineGestureState) + sizeof(BrushStroke) + sizeof(WarpStroke);
    const auto owner = SharedPublicationMemory::OwnershipAllowance();
    if (!owner)
        return std::nullopt;
    const auto add = [](std::uint64_t& target, std::uint64_t count, std::uint64_t bytes) noexcept
    {
        constexpr auto limit = (std::numeric_limits<std::uint64_t>::max)();
        if ((bytes && count > limit / bytes) || count * bytes > limit - target)
            return false;
        target += count * bytes;
        return true;
    };
    // The opening owner survives useful work. Abort history belongs to the original temporary payload.
    std::uint64_t opening = wrappers, final = wrappers, temporary = wrappers;
    if (!add(opening, 2, history) || !add(opening, 1, owner) || !add(final, 4, history) || !add(final, 2, geometry) ||
        !add(final, 1, owner) || !add(temporary, 4, history) || !add(temporary, 2, retirement) ||
        !add(temporary, 2, geometry) || !add(temporary, 3, owner))
        return std::nullopt;
    if (newMarqueeKind)
    {
        const auto sourcePoints = document->selection ? document->selection.value().loops.PointCount() : 0;
        const auto sourceLoops = document->selection ? document->selection.value().loops.size() : 0;
        const auto marquee = LassoFinishGeometry::PlanMarquee(newMarqueeKind.value(), mode, sourcePoints, sourceLoops);
        const auto geometryLimits =
            LassoFinishGeometry::MarqueeLimits(newMarqueeKind.value(), mode, sourcePoints, sourceLoops);
        if (!geometryLimits || (mode != SelectionMode::New && document->selection &&
                                (document->selection->loops.PointCount() > geometryLimits.value().outputPoints ||
                                 document->selection->loops.size() >= geometryLimits.value().inputLoops)))
            return std::nullopt;
        const auto arena = PreparedLassoOpening::MemoryBytes(1);
        // Keep both possible branch peaks admitted at the original Down. No hit-dependent re-admission.
        // The source R already retains old selection/history; New does not feed the old outline to Clipper.
        if (!marquee || !arena || !add(opening, 1, arena) || !add(final, 2, marquee->outputBytes) ||
            !add(final, 8, 4096) || !add(temporary, 1, marquee->preparationBytes) ||
            !add(temporary, 1, LocallyOwnedPublicationAllowance<SelectionOutlineDrawing>()) || !add(temporary, 8, 4096))
            return std::nullopt;
    }
    std::uint64_t total = temporary;
    if (!add(total, 1, opening) || !add(total, 1, final))
        return std::nullopt;
    return SelectionOutlineGestureMemory{{*source, editor, session.history.CaptureAuthority(), newMarqueeKind, mode},
                                         {total, opening, final},
                                         retirement,
                                         history,
                                         geometry};
}

} // namespace
std::optional<SelectionOutlineGestureMemory> DescribeSelectionOutlineGestureMemory(const DocumentSession& session,
                                                                                   bool allowNewRectangle) noexcept
{
    return DescribeMarqueeMemory(session, allowNewRectangle ? std::optional{LassoKind::Rectangle} : std::nullopt);
}
std::optional<SelectionOutlineGestureMemory> DescribeSelectionOutlineGestureMemory(const DocumentSession& session,
                                                                                   LassoKind newMarqueeKind) noexcept
{
    if (!LassoFinishGeometry::NewMarqueeLimits(newMarqueeKind))
        return std::nullopt;
    return DescribeMarqueeMemory(session, newMarqueeKind);
}

std::optional<SelectionOutlineGestureMemory> DescribeSelectionOutlineGestureMemory(const DocumentSession& session,
                                                                                   LassoKind marqueeKind,
                                                                                   SelectionMode mode) noexcept
{
    if (!LassoFinishGeometry::MarqueeLimits(marqueeKind, mode))
        return std::nullopt;
    return DescribeMarqueeMemory(session, marqueeKind, mode);
}

SelectionOutlineGestureJob::SelectionOutlineGestureJob(SelectionOutlineGestureState& state) : _state(state)
{
    state.controller = this;
}

JobResult SelectionOutlineGestureJob::Run(JobContext& context)
{
    auto& s = _state;
    ++s.runs;
    if (_phase == Phase::Allocate)
    {
        s.firstBudget = context.reservedMemoryBytes;
        if (s.firstBudget != s.limits.totalBytes || !s.limits.continuingOutputBytes ||
            s.limits.continuingOutputBytes >= s.limits.totalBytes || !s.limits.finalOutputBytes ||
            s.limits.finalOutputBytes > s.limits.totalBytes - s.limits.continuingOutputBytes)
            return {JobStatus::Failed, E_INVALIDARG};
        _output = SharedPublicationMemory::CreateForWork(s.limits.continuingOutputBytes);
        _final = SharedPublicationMemory::CreateForWork(s.limits.totalBytes);
        _temporary = SharedPublicationMemory::CreateForWork(s.limits.totalBytes);
        if (s.allowNewRectangle || s.allowNewEllipse)
            _drawing = MakeLocallyOwnedPublicationObject<SelectionOutlineDrawing>(_temporary, s.limits.totalBytes, 0);
        return {};
    }
    if (_phase == Phase::Open)
    {
        if (_drawing && _drawing->mode != SelectionMode::New)
        {
            _drawing->chosen = true; // Add/Subtract always draw, including a Down inside the old outline.
            return {};
        }
        ++s.hits;
        const auto hit = ContainsSelectionForWork(_display, _point, context.cancellation);
        if (!hit)
            return {JobStatus::Failed, E_INVALIDARG};
        if (!hit.value() && _drawing && (s.allowNewRectangle || s.allowNewEllipse))
        {
            _drawing->chosen = true;
            return {};
        }
        if (!hit.value() || !_input)
            return {JobStatus::Failed, E_INVALIDARG};
        auto prepared = DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(_input.value()),
                                                                             context.cancellation, {}, _output, true);
        _input.reset();
        if (!prepared)
            return {JobStatus::Failed, E_FAIL};
        _opening.emplace(std::move(prepared.value()));
        return {};
    }
    s.remainingBudget = context.reservedMemoryBytes;
    if (_phase == Phase::DrawOpen)
    {
        if (!_drawing || !_drawing->openingInput)
            return {JobStatus::Failed, E_UNEXPECTED};
        auto prepared = DocumentSession::PrepareLassoOpening(std::move(*_drawing->openingInput), 1,
                                                             context.cancellation, _output, true);
        _drawing->openingInput.reset();
        if (!prepared)
            return {JobStatus::Failed, E_FAIL};
        _drawing->opening.emplace(std::move(*prepared));
        return {};
    }
    if (_phase == Phase::DrawFinish)
    {
        ++s.closurePrepares;
        if (!_drawing || !_drawing->finishInput)
            return {JobStatus::Failed, E_UNEXPECTED};
        LassoFinishObserver observer;
        observer.geometry.limits = _drawing->geometryLimits;
        auto prepared = DocumentSession::PrepareLassoFinishPublication(std::move(*_drawing->finishInput),
                                                                       context.cancellation, {}, &observer, _final);
        _drawing->finishInput.reset();
        if (!prepared)
            return {JobStatus::Failed, E_FAIL};
        _drawing->finish.emplace(std::move(*prepared));
        s.NotifyWork(SelectionOutlineGestureState::WorkStage::DrawingPrepared);
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
    }
    if (_phase == Phase::DrawRefresh)
    {
        ++s.residencyRefreshes;
        s.NotifyWork(SelectionOutlineGestureState::WorkStage::ResidencyRefreshStarted);
        const bool refreshed =
            _drawing && _drawing->finish && _drawing->finish->RefreshHistoryResidency(context.cancellation);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        return refreshed ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
    }
    if (_phase == Phase::RefreshClose || _phase == Phase::RefreshClick)
    {
        ++s.residencyRefreshes;
        s.NotifyWork(SelectionOutlineGestureState::WorkStage::ResidencyRefreshStarted);
        const bool refreshed = _phase == Phase::RefreshClose
                                   ? _close && _close->RefreshHistoryResidency(context.cancellation)
                                   : _command && _command->RefreshHistoryResidency(context.cancellation);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        return refreshed ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
    }
    if (_phase == Phase::Collect)
        return {}; // Event-triggered acknowledgement only; never read mutable input/session on workers.
    if (_phase == Phase::Close)
    {
        ++s.closurePrepares;
        if (!_closeInput)
            return {JobStatus::Failed, E_UNEXPECTED};
        auto memory = _click ? std::shared_ptr<const SharedPublicationMemory>{} : _final;
        auto prepared = DocumentSession::PrepareSelectionMoveClosurePublication(
            std::move(_closeInput.value()), context.cancellation, {}, nullptr, {}, memory);
        _closeInput.reset();
        if (!prepared)
            return {JobStatus::Failed, E_FAIL};
        _close.emplace(std::move(prepared.value()));
        s.NotifyWork(SelectionOutlineGestureState::WorkStage::ClosePrepared);
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
    }
    ++s.clickPrepares;
    if (!_commandInput)
        return {JobStatus::Failed, E_UNEXPECTED};
    auto prepared = DocumentSession::PrepareSelectionCommandPublication(std::move(_commandInput.value()),
                                                                        context.cancellation, {}, _final);
    _commandInput.reset();
    if (!prepared)
        return {JobStatus::Failed, E_FAIL};
    _command.emplace(std::move(prepared.value()));
    s.NotifyWork(SelectionOutlineGestureState::WorkStage::ClickPrepared);
    return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
}

bool SelectionOutlineGestureJob::DeferCompletion(JobResult result, JobTicket ticket) noexcept
{
    auto& s = _state;
    s.sameTicket = s.sameTicket && ticket == s.original;
    if (result.status != JobStatus::Finished)
        return false;
    if (_phase == Phase::Allocate)
    {
        const auto* arrival = s.prefix.CapturedContext(s.opening);
        const auto down = s.prefix.Peek(s.opening);
        if (!arrival || !down || down->action != HostSelectionPressInput::Action::Down ||
            arrival->document.documentSerial != s.session.DocumentSerial() ||
            (arrival->mode != SelectionMode::New &&
             (!s.allowPreservingMarquee ||
              (arrival->mode != SelectionMode::Add && arrival->mode != SelectionMode::Subtract))) ||
            arrival->tool == HostSelectionPressInput::Tool::Wand || down->sample.modifiers.control)
            return Refuse(E_ABORT);
        if (arrival->mode != SelectionMode::New &&
            (!_drawing || !((arrival->kind == LassoKind::Rectangle && s.allowNewRectangle) ||
                            (arrival->kind == LassoKind::Ellipse && s.allowNewEllipse))))
            return Refuse(E_ABORT);
        if (s.requireCapturedTarget &&
            (arrival->active != s.session.ActiveLayerID() || arrival->crop != s.session.CropFrame() ||
             arrival->mask != s.session.IsMaskSelected()))
            return Refuse(E_ABORT);
        if (s.admissionSource && (!s.session.IsDocumentReadCurrent(s.admissionSource->document) ||
                                  !s.session.CanApplyHistoryRestore(s.admissionSource->editor) ||
                                  !s.session.history.IsCurrent(s.admissionSource->history)))
            return Refuse(E_ABORT);
        if (_drawing && s.admissionSource &&
            (s.admissionSource->newMarqueeKind != std::optional{arrival->kind} ||
             s.admissionSource->marqueeMode != arrival->mode))
            return Refuse(E_ABORT); // Never prepare a different kind or mode under a smaller admission.
        // Geometry/history and full source authority are captured only here, after this original admission.
        _source = s.session.CaptureDocumentReadAuthority();
        if (!_source || arrival->document.sessionIdentity != _source->sessionIdentity)
            return Refuse(E_ABORT);
        _display = s.session.DisplayedSelectionValue();
        _point = down->sample.point;
        if (_drawing)
        {
            if (arrival->tool != HostSelectionPressInput::Tool::Marquee ||
                !((arrival->kind == LassoKind::Rectangle && s.allowNewRectangle) ||
                  (arrival->kind == LassoKind::Ellipse && s.allowNewEllipse)))
                return Refuse(E_ABORT);
            _drawing->settings = {arrival->kind, down->sample.antialiased};
            _drawing->mode = arrival->mode;
            const auto sourcePoints = _display.selection ? _display.selection.value().loops.PointCount() : 0;
            const auto sourceLoops = _display.selection ? _display.selection.value().loops.size() : 0;
            const auto geometryLimits =
                LassoFinishGeometry::MarqueeLimits(arrival->kind, arrival->mode, sourcePoints, sourceLoops);
            if (!geometryLimits)
                return Refuse(E_OUTOFMEMORY);
            _drawing->geometryLimits = geometryLimits.value();
            _drawing->constrainArmed = !down->sample.modifiers.shift;
            _drawing->editor = s.session.CaptureHistoryRestoreAuthority();
            _drawing->history = s.session.history.CaptureAuthority();
        }
        if (_display.selection && !_display.selection->IsEmpty() && (!_drawing || _drawing->mode == SelectionMode::New))
        {
            auto input = s.session.PrepareDeferredSelectionMoveOpeningInput();
            if (!input)
                return Refuse(E_ABORT);
            _input.emplace(std::move(*input));
            const auto& loops = _display.selection->loops;
            _geometryEnvelope = loops.StorageBytes() + SelectionLoops::WorkOwnershipAllowance(loops.size());
        }
        else if (!_drawing)
            return Refuse(E_ABORT);
        ++s.captures;
        _phase = Phase::Open;
        return Resume();
    }
    if (_phase == Phase::Open && _drawing && _drawing->chosen)
    {
        s.sourceCurrent = _source && s.session.IsDocumentReadCurrent(*_source);
        if (!s.sourceCurrent || !_drawing->editor || !_drawing->history ||
            !s.session.CanApplyHistoryRestore(*_drawing->editor) || !s.session.history.IsCurrent(*_drawing->history))
            return Refuse(E_ABORT);
        auto input = s.session.PrepareLassoOpeningInput(_point, _drawing->mode, _drawing->settings);
        if (!input)
            return Refuse(E_ABORT);
        _drawing->openingIntent = s.session.CaptureLassoIntent(*input);
        if (!_drawing->openingIntent)
            return Refuse(E_ABORT);
        _drawing->openingInput.emplace(std::move(*input));
        _phase = Phase::DrawOpen;
        return Resume();
    }
    if (_phase == Phase::DrawOpen)
    {
        if (!_drawing || !_drawing->opening)
            return Refuse(E_ABORT);
        s.openingBytes = _output->LocalAllocationBytes() + SharedPublicationMemory::OwnershipAllowance();
        if (_output->BindContinuingOutputForPublication(s.scheduler, ticket, s.openingBytes) != S_OK)
            return Refuse(E_FAIL);
        s.partition = s.scheduler.ReservedMemoryBytes() >= s.limits.totalBytes && _output->BoundTo(s.scheduler, ticket);
        s.opened = s.session.ApplyPreparedLassoOpeningIfCurrent(*_drawing->opening);
        s.drawing = true;
        if (!s.opened)
            return Refuse(E_ABORT);
        s.Notify(SelectionOutlineGestureState::Event::Opened);
        _phase = Phase::Collect;
        return Consume();
    }
    if (_phase == Phase::DrawRefresh)
        _phase = Phase::DrawFinish;
    if (_phase == Phase::DrawFinish)
    {
        if (!_drawing || !_drawing->finish || !s.session.IsPreparedLassoFinishCurrent(*_drawing->finish))
            return Refuse(E_ABORT);
        ++s.residencyTries;
        const auto decision = _drawing->finish->TryAcquireHistoryResidency(_residency);
        if (decision == DocumentHistory::ResidencyValidation::Invalid)
            return Refuse(E_ABORT);
        if (decision != DocumentHistory::ResidencyValidation::Ready)
        {
            _residency.Reset();
            _phase = Phase::DrawRefresh;
            return Resume();
        }
        return false;
    }
    if (_phase == Phase::Open)
    {
        s.sourceCurrent = _source && s.session.IsDocumentReadCurrent(_source.value());
        if (!s.sourceCurrent || !_opening)
            return Refuse(E_ABORT);
        s.openingBytes = _output->LocalAllocationBytes() + SharedPublicationMemory::OwnershipAllowance();
        if (_output->BindContinuingOutputForPublication(s.scheduler, ticket, s.openingBytes) != S_OK)
            return Refuse(E_FAIL);
        s.partition = s.scheduler.ReservedMemoryBytes() >= s.limits.totalBytes && _output->BoundTo(s.scheduler, ticket);
        s.opened = s.session.ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(_opening.value());
        if (!s.opened)
            return Refuse(E_ABORT);
        s.Notify(SelectionOutlineGestureState::Event::Opened);
        _phase = Phase::Collect;
        return Consume();
    }
    if (_phase == Phase::Collect)
    {
        ++s.collectAcks;
        return Consume();
    }
    if (_phase == Phase::RefreshClose)
        _phase = Phase::Close;
    else if (_phase == Phase::RefreshClick)
        _phase = Phase::Click;
    if (_phase == Phase::Close || _phase == Phase::Click)
    {
        const bool close = _phase == Phase::Close;
        // A foreign editor may open while the original ticket waits for residency without changing
        // document generation or history. Retain the opening's complete opaque gesture authority.
        if ((close && (!_opening || !s.session.IsPreparedDeferredSelectionMoveGestureCurrent(*_opening))) ||
            (close ? !_close || !s.session.IsPreparedSelectionMoveClosureCurrent(*_close)
                   : !_command || !s.session.IsPreparedSelectionCommandCurrent(*_command)))
            return Refuse(E_ABORT);
        ++s.residencyTries;
        const auto decision =
            close ? _close->TryAcquireHistoryResidency(_residency) : _command->TryAcquireHistoryResidency(_residency);
        if (decision == DocumentHistory::ResidencyValidation::Invalid)
            return Refuse(E_ABORT);
        if (decision != DocumentHistory::ResidencyValidation::Ready)
        {
            _residency.Reset();
            _phase = close ? Phase::RefreshClose : Phase::RefreshClick;
            return Resume(); // Original admission/ticket; no final transfer until the witness is Ready.
        }
    }
    if (_phase == Phase::Close && _click)
    {
        s.closed = _close && s.session.ApplyPreparedSelectionMoveClosureIfCurrent(_close.value());
        _residency.Reset(); // No lease crosses the next useful stage or source capture.
        if (!s.closed)
            return Refuse(E_ABORT);
        auto input = s.session.PrepareSelectionCommandInput(SelectionCommandKind::Deselect);
        if (!input)
            return Refuse(E_ABORT);
        _commandInput.emplace(std::move(input.value()));
        _phase = Phase::Click;
        return Resume();
    }
    return false;
}

bool SelectionOutlineGestureJob::NotifyInput() noexcept
{
    auto& s = _state;
    if (!s.waiting || s.prefix.PendingCount() == 0)
        return false;
    s.waiting = false;
    if (s.scheduler.Resume(s.original))
        return true;
    s.waiting = true;
    return false;
}

void SelectionOutlineGestureJob::Completed(JobResult result) noexcept
{
    auto& s = _state;
    s.completed = true;
    s.waiting = false;
    s.outcome = result;
    if (_error && result.status == JobStatus::Finished)
        s.outcome = {JobStatus::Failed, _error};
    if (s.drawing && s.terminalLost)
        s.outcome = {JobStatus::Cancelled};
    if (s.outcome.status == JobStatus::Finished && s.drawing)
    {
        const auto required = _drawing && _drawing->finish ? _drawing->finish->OutputAllocationBytes() : 0;
        if (!_drawing || !_drawing->finish || !s.session.IsPreparedLassoFinishCurrent(*_drawing->finish) ||
            required > s.limits.finalOutputBytes ||
            _final->BindPersistentOutputAfterCompletedWork(s.scheduler, s.original, s.limits.finalOutputBytes,
                                                           *_temporary) != S_OK)
            s.outcome = {JobStatus::Failed, E_FAIL};
        else
            s.closed = s.session.ApplyPreparedLassoFinishIfCurrent(*_drawing->finish);
        if (!s.closed && s.outcome.status == JobStatus::Finished)
            s.outcome = {JobStatus::Failed, E_ABORT};
    }
    else if (s.outcome.status == JobStatus::Finished)
    {
        const auto required = _command ? _command->OutputMemoryBytes()
                                       : _final->LocalAllocationBytes() +
                                             SharedPublicationMemory::OwnershipAllowance() + _geometryEnvelope;
        if (!required || required > s.limits.finalOutputBytes ||
            _final->BindPersistentOutputAfterCompletedWork(s.scheduler, s.original, s.limits.finalOutputBytes,
                                                           *_temporary) != S_OK)
            s.outcome = {JobStatus::Failed, E_FAIL};
        else if (_click)
            s.clicked = _command && s.session.ApplyPreparedSelectionCommandIfCurrent(_command.value());
        else
            s.closed = _close && s.session.ApplyPreparedSelectionMoveClosureIfCurrent(_close.value());
        if (!s.closed || (_click && !s.clicked))
            s.outcome = {JobStatus::Failed, E_ABORT};
    }
    _residency.Reset(); // Before rollback, retirement binding, prefix acknowledgement or host callbacks.
    if (s.outcome.status != JobStatus::Finished)
    {
        AbortDrawing();
        if (s.opened && !s.closed && _opening)
            s.aborted = s.session.AbortPreparedDeferredSelectionMoveIfCurrent(_opening.value());
        if (_temporary && !_temporary->IsBound() && _temporary->BindForRetirement(s.scheduler, s.original) != S_OK)
            std::terminate();
    }
    s.overflowObserved = s.prefix.Overflowed();
    s.inputAcknowledged = _terminal ? s.prefix.Acknowledge(s.opening) : s.prefix.Drop(s.opening);
    s.Notify(SelectionOutlineGestureState::Event::Terminal);
    s.controller = nullptr;
    s.parentAcknowledged.store(true, std::memory_order_release);
}

void SelectionOutlineGestureJob::RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept
{
    auto& s = _state;
    _residency.Reset();
    s.sameTicket = s.sameTicket && ticket == s.original;
    s.waiting = false;
    AbortDrawing();
    if (s.opened && !s.closed && _opening)
        s.aborted = s.session.AbortPreparedDeferredSelectionMoveIfCurrent(_opening.value());
    if (_temporary && !_temporary->IsBound() && _temporary->BindForRetirement(s.scheduler, ticket) != S_OK)
        std::terminate();
    s.inputAcknowledged = s.prefix.Drop(s.opening);
    s.controller = nullptr;
    s.outcome = {JobStatus::Cancelled};
}

bool SelectionOutlineGestureJob::Refuse(std::int32_t error) noexcept
{
    _error = error;
    return false;
}

bool SelectionOutlineGestureJob::Resume() noexcept
{
    if (!_state.scheduler.Resume(_state.original))
        return Refuse(E_UNEXPECTED);
    return true;
}

bool SelectionOutlineGestureJob::Consume() noexcept
{
    if (_drawing && _drawing->chosen)
        return ConsumeDrawing();
    auto& s = _state;
    if (!_opening || !s.session.IsPreparedDeferredSelectionMoveGestureCurrent(_opening.value()))
        return Refuse(E_ABORT);
    bool changed = false;
    while (auto event = s.prefix.Take(s.opening))
    {
        if (event->action == HostSelectionPressInput::Action::Down)
            continue;
        if (event->action == HostSelectionPressInput::Action::Move)
        {
            double dx = event->sample.point.x - _point.x, dy = event->sample.point.y - _point.y;
            if (event->sample.modifiers.shift)
            {
                if (std::fabs(dx) >= std::fabs(dy))
                    dy = 0;
                else
                    dx = 0;
            }
            if (!s.session.MovePreparedDeferredSelectionMoveIfCurrent(_opening.value(), dx, dy))
                return Refuse(E_ABORT);
            _offset = {std::round(dx), std::round(dy)};
            ++s.moves;
            changed = true;
            continue;
        }
        _terminal = true;
        s.terminalLost = event->action == HostSelectionPressInput::Action::Lost;
        s.terminalOrdinal = event->sample.ordinal;
        _click = !s.terminalLost && _offset == Vec2{};
        if (!NotifyMoves(changed))
            return Refuse(E_ABORT);
        auto input = s.session.PrepareSelectionMoveClosureInput(false, s.session.SelectionMoveOpening());
        if (!input)
            return Refuse(E_ABORT);
        _closeInput.emplace(std::move(input.value()));
        _phase = Phase::Close;
        return Resume();
    }
    if (!NotifyMoves(changed))
        return Refuse(E_ABORT);
    s.waiting = true;
    return true; // No self-rearm or periodic work. The next accepted input supplies readiness.
}

bool SelectionOutlineGestureJob::NotifyMoves(bool changed) noexcept
{
    if (changed)
        _state.Notify(SelectionOutlineGestureState::Event::Moved);
    // Host callbacks may publish UI Automation events and reenter the owner. A foreign pending edit can
    // appear without changing document generation; never capture a closure under that foreign authority.
    return _opening && _state.session.IsPreparedDeferredSelectionMoveGestureCurrent(*_opening);
}

bool SelectionOutlineGestureJob::DrawingCurrent() const noexcept
{
    return _drawing && _drawing->opening && _state.session.IsPreparedLassoGestureCurrent(*_drawing->opening);
}
void SelectionOutlineGestureJob::AbortDrawing() noexcept
{
    if (_state.opened && !_state.closed && DrawingCurrent())
        _state.aborted = _state.session.AbortPreparedLassoGestureIfCurrent(*_drawing->opening);
    if (_drawing)
    {
        if (_drawing->openingIntent)
            (void)_state.session.DiscardLassoIntentIfCurrent(*_drawing->openingIntent);
        if (_drawing->finishIntent)
            (void)_state.session.DiscardLassoIntentIfCurrent(*_drawing->finishIntent);
        if (_drawing->opening)
            (void)_state.session.DiscardPreparedClosedLassoIfCurrent(*_drawing->opening);
    }
}
bool SelectionOutlineGestureJob::ConsumeDrawing() noexcept
{
    auto& s = _state;
    if (!DrawingCurrent())
        return Refuse(E_ABORT);
    bool changed = false;
    while (auto event = s.prefix.Take(s.opening))
    {
        if (event->action == HostSelectionPressInput::Action::Down)
            continue;
        if (event->action == HostSelectionPressInput::Action::Move)
        {
            // EditorCanvas.dragMarqueeDraft: Option selects Subtract at Down, never fromCenter.
            // A Shift already held at Down becomes a square modifier only after it is released.
            if (!event->sample.modifiers.shift)
                _drawing->constrainArmed = true;
            if (!s.session.DragPreparedMarqueeIfCurrent(*_drawing->opening, event->sample.point,
                                                        _drawing->constrainArmed && event->sample.modifiers.shift,
                                                        false))
                return Refuse(E_ABORT);
            ++s.moves;
            changed = true;
            continue;
        }
        _terminal = true;
        s.terminalLost = event->action == HostSelectionPressInput::Action::Lost;
        s.terminalOrdinal = event->sample.ordinal;
        if (changed)
            s.Notify(SelectionOutlineGestureState::Event::Moved);
        if (!DrawingCurrent())
            return Refuse(E_ABORT);
        if (s.terminalLost)
        {
            s.aborted = s.session.AbortPreparedLassoGestureIfCurrent(*_drawing->opening);
            return s.aborted ? false : Refuse(E_ABORT);
        }
        auto terminal = s.session.ClosePreparedLassoGestureForWork(*_drawing->opening, event->sample.antialiased);
        if (!terminal)
            return Refuse(E_ABORT);
        auto input = s.session.PrepareClosedBoundedLassoFinishInput(std::move(*terminal));
        if (!input)
            return Refuse(E_ABORT);
        _drawing->finishIntent = s.session.CaptureLassoIntent(*input);
        if (!_drawing->finishIntent)
            return Refuse(E_ABORT);
        _drawing->finishInput.emplace(std::move(*input));
        _phase = Phase::DrawFinish;
        return Resume();
    }
    if (changed)
        s.Notify(SelectionOutlineGestureState::Event::Moved);
    if (!DrawingCurrent())
        return Refuse(E_ABORT);
    s.waiting = true;
    return true;
}
