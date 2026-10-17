#include "PolygonalDrawingJob.h"

#include "BrushStroke.h"
#include "LassoFinishWork.h"
#include "LassoIntent.h"
#include "LassoOpeningWork.h"
#include "LocallyOwnedPublication.h"
#include "WarpStroke.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
#include <utility>
#include <windows.h>

struct PolygonalDrawingPayload final
{
    std::optional<LassoOpeningInput> input;
    std::optional<PreparedLassoOpening> opening;
    std::optional<LassoIntent> openingIntent;
    std::optional<LassoIntent> finishIntent;
    std::optional<LassoFinishInput> finishInput;
    std::optional<PreparedLassoFinish> finish;
};

std::optional<PolygonalDrawingPlan> DescribePolygonalDrawing(const DocumentSession& session,
                                                             SelectionMode mode) noexcept
{
    const auto& document = session.Document();
    const auto source = session.CaptureDocumentReadAuthority();
    const auto editor = session.CaptureHistoryRestoreAuthority();
    if (!document || !source || !session.CanApplyHistoryRestore(editor) || mode > SelectionMode::Subtract ||
        (mode == SelectionMode::New && document.value().selection && !document.value().selection->IsEmpty()))
        return std::nullopt;
    const auto points = document.value().selection ? document.value().selection->loops.PointCount() : 0;
    const auto loops = document.value().selection ? document.value().selection->loops.size() : 0;
    const auto geometry = LassoFinishGeometry::PolygonalLimits(mode, points, loops);
    const auto finish = LassoFinishGeometry::PlanPolygonal(mode, points, loops);
    const auto history = session.history.EditMemoryBytes(document);
    const auto retirement = session.HistoryRestoreRetirementMemoryBytes();
    const auto arena = PreparedLassoOpening::MemoryBytes(LassoFinishGeometry::kPolygonalArenaBlocks);
    if (!geometry || !finish || !history || !retirement || !arena)
        return std::nullopt;
    const auto add = [](std::uint64_t& target, std::uint64_t count, std::uint64_t bytes) noexcept
    {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if ((bytes && count > maximum / bytes) || count * bytes > maximum - target)
            return false;
        target += count * bytes;
        return true;
    };
    // Same conservative source/history construction overlap as the qualified drawing parent. The
    // temporary payload and every old/new graph remain charged after their semantic owner changes.
    constexpr std::uint64_t wrappers = 8192 + sizeof(PolygonalDrawingJob) + sizeof(PolygonalDrawingState) +
                                       sizeof(HostLassoInput) + sizeof(BrushStroke) + sizeof(WarpStroke);
    const auto ownership = SharedPublicationMemory::OwnershipAllowance();
    const auto payload = LocallyOwnedPublicationAllowance<PolygonalDrawingPayload>();
    if (!ownership || !payload)
        return std::nullopt;
    std::uint64_t opening = wrappers, final = wrappers, temporary = wrappers;
    if (!add(opening, 2, history) || !add(opening, 1, arena) || !add(opening, 1, ownership) ||
        !add(final, 4, history) || !add(final, 2, finish.value().outputBytes) || !add(final, 1, ownership) ||
        !add(temporary, 4, history) || !add(temporary, 2, retirement) ||
        !add(temporary, 1, finish.value().preparationBytes) || !add(temporary, 1, payload) ||
        !add(temporary, 3, ownership))
        return std::nullopt;
    std::uint64_t total = temporary;
    if (!add(total, 1, opening) || !add(total, 1, final))
        return std::nullopt;
    PolygonalDrawingPlan result;
    result._document = source.value();
    result._editor = editor;
    result._history = session.history.CaptureAuthority();
    result._drawing = {LassoKind::Polygonal,
                       mode,
                       LassoFinishGeometry::kPolygonalArenaBlocks,
                       LassoFinishGeometry::kPolygonalMaximumPoints,
                       geometry.value(),
                       finish.value()};
    result._total = total;
    result._opening = opening;
    result._final = final;
    return result;
}

PolygonalDrawingJob::PolygonalDrawingJob(PolygonalDrawingState& state) noexcept : _state(state) {}
std::int32_t PolygonalDrawingJob::Submit(PolygonalDrawingState& state) noexcept
{
    if (state.controller || state.original != JobTicket{} || !state.opening || !state.plan.TotalBytes())
        return E_INVALIDARG;
    try
    {
        std::unique_ptr<Job> owned = std::make_unique<PolygonalDrawingJob>(state);
        auto* controller = static_cast<PolygonalDrawingJob*>(owned.get());
        const auto result = state.scheduler.Submit(owned,
                                                   {.kind = JobClass::Selection,
                                                    .serialKey = state.plan._document.documentSerial,
                                                    .memoryBytes = state.plan.TotalBytes(),
                                                    .deferredRetirement = true,
                                                    .allowMemoryWaiting = false,
                                                    .reserveRetainedMemory = true,
                                                    .continuingOutputLimit = state.plan.OpeningBytes()},
                                                   state.original);
        if (SUCCEEDED(result))
            state.controller = controller;
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // Admission fails the entire operation before source capture.
    }
}
JobResult PolygonalDrawingJob::Run(JobContext& context)
{
    auto& s = _state;
    ++s.runs;
    if (context.cancellation.IsCancelled())
        return {JobStatus::Cancelled};
    if (_phase == Phase::Allocate)
    {
        s.firstBudget = context.reservedMemoryBytes;
        if (s.firstBudget != s.plan.TotalBytes())
            return {JobStatus::Failed, E_INVALIDARG};
        _opening = SharedPublicationMemory::CreateForWork(s.plan.OpeningBytes());
        _final = SharedPublicationMemory::CreateForWork(s.plan.TotalBytes());
        _temporary = SharedPublicationMemory::CreateForWork(s.plan.TotalBytes());
        _payload = MakeLocallyOwnedPublicationObject<PolygonalDrawingPayload>(_temporary, s.plan.TotalBytes(), 0);
        if (!_opening || !_final || !_temporary || !_payload)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        if (s.checkpoint)
            s.checkpoint(s.workObserver, PolygonalDrawingState::WorkStage::Allocated);
        return {};
    }
    if (_phase == Phase::Open)
    {
        if (!_payload || !_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        auto prepared = DocumentSession::PrepareLassoOpening(
            std::move(_payload->input.value()), s.plan._drawing.arenaBlocks, context.cancellation, _opening, true);
        _payload->input.reset();
        if (!prepared)
            return {JobStatus::Failed, E_FAIL};
        _payload->opening.emplace(std::move(prepared.value()));
        if (s.checkpoint)
            s.checkpoint(s.workObserver, PolygonalDrawingState::WorkStage::OpeningPrepared);
        return {};
    }
    if (_phase == Phase::Collect)
        return {}; // Owner metadata is inspected only after this event-driven acknowledgement.
    if (_phase == Phase::Refresh)
    {
        const bool ready =
            _payload && _payload->finish && _payload->finish.value().RefreshHistoryResidency(context.cancellation);
        if (s.checkpoint)
            s.checkpoint(s.workObserver, PolygonalDrawingState::WorkStage::ResidencyRefreshed);
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
               : ready                            ? JobResult{}
                                                  : JobResult{JobStatus::Failed, E_FAIL};
    }
    if (!_payload || !_payload->finishInput)
        return {JobStatus::Failed, E_UNEXPECTED};
    s.finishBudget = context.reservedMemoryBytes;
    LassoFinishObserver observer;
    observer.geometry.limits = s.plan._drawing.geometry;
    auto prepared = DocumentSession::PrepareLassoFinishPublication(std::move(_payload->finishInput.value()),
                                                                   context.cancellation, {}, &observer, _final);
    _payload->finishInput.reset();
    if (!prepared)
        return {JobStatus::Failed, E_FAIL};
    _payload->finish.emplace(std::move(prepared.value()));
    if (s.checkpoint)
        s.checkpoint(s.workObserver, PolygonalDrawingState::WorkStage::FinishPrepared);
    return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
}

bool PolygonalDrawingJob::DeferCompletion(JobResult result, JobTicket ticket) noexcept
{
    auto& s = _state;
    s.sameTicket = s.sameTicket && ticket == s.original;
    if (result.status != JobStatus::Finished)
        return false;
    if (_phase == Phase::Allocate)
    {
        const auto* arrival = s.input.CapturedContext(s.opening);
        const auto down = s.input.Peek(s.opening);
        if (!_payload || !arrival || !down || down->action != HostLassoInput::Action::Down ||
            arrival->kind != LassoKind::Polygonal || arrival->mode != s.plan.Mode() ||
            arrival->document != s.plan._document || arrival->active != s.session.ActiveLayerID() ||
            arrival->mask != s.session.IsMaskSelected() || arrival->crop != s.session.CropFrame() ||
            !s.session.IsDocumentReadCurrent(s.plan._document) || !s.session.CanApplyHistoryRestore(s.plan._editor) ||
            !s.session.history.IsCurrent(s.plan._history))
            return Refuse(E_ABORT);
        _lastOrdinal = arrival->ordinal;
        auto input = s.session.PrepareLassoOpeningInput(
            down->sample.point, s.plan.Mode(), LassoOpeningSettings{LassoKind::Polygonal, s.antialiasedAtDown});
        if (!input)
            return Refuse(E_ABORT);
        _payload->openingIntent = s.session.CaptureLassoIntent(input.value());
        if (!_payload->openingIntent)
            return Refuse(E_ABORT);
        _payload->input.emplace(std::move(input.value()));
        ++s.captures;
        _phase = Phase::Open;
        return Resume();
    }
    if (_phase == Phase::Open)
    {
        if (!_payload || !_payload->opening ||
            _opening->BindContinuingOutputForPublication(s.scheduler, ticket,
                                                         _opening->LocalAllocationBytes() +
                                                             SharedPublicationMemory::OwnershipAllowance()) != S_OK ||
            !s.session.ApplyPreparedLassoOpeningIfCurrent(_payload->opening.value()))
            return Refuse(E_ABORT);
        s.openingAllocation = _opening->Bytes();
        s.opened = true;
        _phase = Phase::Collect;
        s.Notify(PolygonalDrawingState::Event::Opened);
        return Consume();
    }
    if (_phase == Phase::Collect)
        return Consume();
    if (_phase == Phase::Refresh)
        _phase = Phase::Finish;
    if (!_payload || !_payload->finish || !s.session.IsPreparedLassoFinishCurrent(_payload->finish.value()))
        return Refuse(E_ABORT);
    const auto residency = _payload->finish.value().TryAcquireHistoryResidency(_residency);
    if (residency == DocumentHistory::ResidencyValidation::Invalid)
        return Refuse(E_ABORT);
    if (residency != DocumentHistory::ResidencyValidation::Ready)
    {
        _residency.Reset();
        ++s.residencyRefreshes;
        _phase = Phase::Refresh;
        return Resume();
    }
    return false;
}
bool PolygonalDrawingJob::Current() const noexcept
{
    return _payload && _payload->opening && _state.session.IsPreparedLassoGestureCurrent(_payload->opening.value());
}
bool PolygonalDrawingJob::Refuse(std::int32_t error) noexcept
{
    _error = error;
    return false;
}
bool PolygonalDrawingJob::Resume() noexcept
{
    return _state.scheduler.Resume(_state.original) || Refuse(E_ABORT);
}
bool PolygonalDrawingJob::NotifyInput() noexcept
{
    if (!_state.waiting || !_state.input.PendingCount())
        return false;
    _state.waiting = false;
    if (Resume())
        return true;
    _state.waiting = true;
    return false;
}
bool PolygonalDrawingJob::Finish(bool antialiased, std::uint64_t ordinal, bool cancelled) noexcept
{
    if (!ordinal || _terminal || _explicitTerminalOrdinal || !_state.input.Sampling())
        return false;
    const auto admitted = _state.input.Finish(_state.opening, antialiased, cancelled);
    if (admitted != HostLassoInput::Admission::Recorded)
        return false;
    _explicitTerminalOrdinal = ordinal;
    (void)NotifyInput();
    return true;
}
bool PolygonalDrawingJob::Consume() noexcept
{
    auto& s = _state;
    if (!Current())
        return Refuse(E_ABORT);
    unsigned consumed = 0;
    bool changed = false;
    while (const auto event = s.input.Take(s.opening))
    {
        if (event->action == HostLassoInput::Action::Down)
            continue;
        if (event->action == HostLassoInput::Action::Up || event->action == HostLassoInput::Action::Lost)
        {
            _terminal = true;
            s.cancelled = event->action == HostLassoInput::Action::Lost;
            const bool selectedClose = s.closingOrdinal != 0;
            if (!selectedClose)
                s.closingOrdinal = _explicitTerminalOrdinal;
            if (s.cancelled)
                return false;
            if (!s.closingOrdinal ||
                (selectedClose ? s.closingOrdinal != _lastOrdinal : s.closingOrdinal <= _lastOrdinal))
                return Refuse(E_INVALIDARG);
            if (changed)
                s.Notify(PolygonalDrawingState::Event::Changed);
            if (!Current())
                return Refuse(E_ABORT);
            auto terminal =
                s.session.ClosePreparedLassoGestureForWork(_payload->opening.value(), event->sample.antialiased);
            auto input =
                terminal ? s.session.PrepareClosedBoundedLassoFinishInput(std::move(terminal.value())) : std::nullopt;
            if (!input)
                return Refuse(E_ABORT);
            _payload->finishIntent = s.session.CaptureLassoIntent(input.value());
            if (!_payload->finishIntent)
                return Refuse(E_ABORT);
            _payload->finishInput.emplace(std::move(input.value()));
            _phase = Phase::Finish;
            return Resume();
        }
        const auto& sample = event->sample;
        if (!sample.ordinal || sample.ordinal <= _lastOrdinal)
            return Refuse(E_INVALIDARG);
        _lastOrdinal = sample.ordinal;
        if (sample.operation == HostLassoInput::Operation::PolygonPress &&
            (sample.doubleClick || (sample.nearFirst && s.session.PendingLasso().value().PointCount() >= 3)))
        {
            // The legacy selected terminal deliberately normalizes metadata. Retain the original
            // accepted event's physical identity before selecting it; no predecessor API/golden changes.
            if (!sample.ordinal || !s.input.SelectReplayedPolygonClose(s.opening))
                return Refuse(E_ABORT);
            s.closingOrdinal = sample.ordinal;
            s.closingIntent = sample.closingIntent;
            continue;
        }
        std::optional<bool> updated;
        if (sample.operation == HostLassoInput::Operation::Point ||
            sample.operation == HostLassoInput::Operation::PolygonPress)
        {
            updated = s.session.ExtendPreparedLassoPointIfCurrent(_payload->opening.value(), sample.point);
            ++s.points;
        }
        else if (sample.operation == HostLassoInput::Operation::Cursor)
        {
            updated = s.session.MovePreparedPolygonCursorIfCurrent(
                _payload->opening.value(), sample.cursorPresent ? std::optional{sample.point} : std::nullopt);
            ++s.cursors;
        }
        else if (sample.operation == HostLassoInput::Operation::Backspace)
        {
            const auto removal = s.session.RemovePreparedPolygonPointIfCurrent(_payload->opening.value());
            if (!removal)
                return Refuse(E_ABORT);
            ++s.backspaces;
            if (removal.value() == PreparedLassoRemoval::Ended)
            {
                s.endedByBackspace = true;
                return false;
            }
            updated = true;
        }
        else
            return Refuse(E_INVALIDARG);
        if (!updated)
            return Refuse(Current() ? E_OUTOFMEMORY : E_ABORT);
        changed = changed || updated.value();
        ++consumed;
        s.maximumSamples = (std::max)(s.maximumSamples, consumed);
        if (consumed == 16 && s.input.PendingCount())
        {
            if (changed)
                s.Notify(PolygonalDrawingState::Event::Changed);
            return Current() ? Resume() : Refuse(E_ABORT);
        }
    }
    if (changed)
        s.Notify(PolygonalDrawingState::Event::Changed);
    if (!Current())
        return Refuse(E_ABORT);
    s.waiting = true;
    return true;
}
void PolygonalDrawingJob::Abort() noexcept
{
    auto& s = _state;
    if (Current())
        (void)s.session.AbortPreparedLassoGestureIfCurrent(_payload->opening.value());
    if (_payload)
    {
        if (_payload->openingIntent)
            (void)s.session.DiscardLassoIntentIfCurrent(_payload->openingIntent.value());
        if (_payload->finishIntent)
            (void)s.session.DiscardLassoIntentIfCurrent(_payload->finishIntent.value());
        if (_payload->opening)
            (void)s.session.DiscardPreparedClosedLassoIfCurrent(_payload->opening.value());
    }
}
void PolygonalDrawingJob::BindRetirement(JobTicket ticket) noexcept
{
    // The original descriptor preclaims ONE terminal slot. Bind an output/temporary pair exactly
    // once, retaining the opening's already published partition when it exists. No fresh admission.
    if (_temporary)
    {
        if (_opening && !_opening->IsBound())
        {
            // No finish work can precede opening publication. This empty final-account wrapper owns
            // no graph; its constant metadata dies while the original whole reservation is still held.
            if (_final && _final->LocalAllocationBytes())
                std::terminate();
            _final.reset();
            if (_opening->BindPersistentOutputForRetirement(_state.scheduler, ticket, _state.plan.OpeningBytes(),
                                                            *_temporary) != S_OK)
                std::terminate();
        }
        else if (_final && !_final->IsBound())
        {
            if (_final->BindPersistentOutputForRetirement(_state.scheduler, ticket, _state.plan.FinalBytes(),
                                                          *_temporary) != S_OK)
                std::terminate();
        }
        else if (!_temporary->IsBound() && _temporary->BindForRetirement(_state.scheduler, ticket) != S_OK)
            std::terminate();
    }
    else if (_opening && !_opening->IsBound() && _opening->BindForRetirement(_state.scheduler, ticket) != S_OK)
        std::terminate(); // Partial Allocate: no payload/source/arena exists yet.
}
void PolygonalDrawingJob::RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept
{
    auto& s = _state;
    _residency.Reset();
    s.sameTicket = s.sameTicket && ticket == s.original;
    s.waiting = false;
    Abort();
    BindRetirement(ticket);
    s.inputAcknowledged = s.input.Drop(s.opening);
    s.controller = nullptr;
    s.outcome = {JobStatus::Cancelled};
}
void PolygonalDrawingJob::Completed(JobResult result) noexcept
{
    auto& s = _state;
    s.completed = true;
    s.waiting = false;
    s.outcome = s.cancelled                                      ? JobResult{JobStatus::Cancelled}
                : _error && result.status == JobStatus::Finished ? JobResult{JobStatus::Failed, _error}
                                                                 : result;
    if (s.outcome.status == JobStatus::Finished && !s.endedByBackspace)
    {
        const auto bytes = _payload && _payload->finish ? _payload->finish.value().OutputAllocationBytes() : 0;
        if (!_payload || !_payload->finish || !s.session.IsPreparedLassoFinishCurrent(_payload->finish.value()) ||
            bytes > s.plan.FinalBytes() ||
            _final->BindPersistentOutputAfterCompletedWork(s.scheduler, s.original, s.plan.FinalBytes(), *_temporary) !=
                S_OK)
            s.outcome = {JobStatus::Failed, E_ABORT};
        else
            s.published = s.session.ApplyPreparedLassoFinishIfCurrent(_payload->finish.value());
        if (!s.published && s.outcome.status == JobStatus::Finished)
            s.outcome = {JobStatus::Failed, E_ABORT};
    }
    _residency.Reset();
    if (s.outcome.status != JobStatus::Finished)
        Abort();
    BindRetirement(s.original);
    s.inputAcknowledged = _terminal ? s.input.Acknowledge(s.opening) : s.input.Drop(s.opening);
    s.controller = nullptr;
    s.Notify(PolygonalDrawingState::Event::Terminal); // Final access: the observer may start its next owned state.
}
