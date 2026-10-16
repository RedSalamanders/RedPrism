#include "SelectionOutlineGestureJob.h"

#include <cmath>
#include <exception>
#include <windows.h>

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
        return {};
    }
    if (_phase == Phase::Open)
    {
        ++s.hits;
        const auto hit = ContainsSelectionForWork(_display, _point, context.cancellation);
        if (!hit || !hit.value() || !_input)
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
        return {};
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
    return {};
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
            arrival->document.documentSerial != s.session.DocumentSerial() || arrival->mode != SelectionMode::New ||
            arrival->tool == HostSelectionPressInput::Tool::Wand || down->sample.modifiers.control)
            return Refuse(E_ABORT);
        // Geometry/history and full source authority are captured only here, after this original admission.
        _source = s.session.CaptureDocumentReadAuthority();
        if (!_source || arrival->document.sessionIdentity != _source->sessionIdentity)
            return Refuse(E_ABORT);
        _display = s.session.DisplayedSelectionValue();
        _point = down->sample.point;
        auto input = s.session.PrepareDeferredSelectionMoveOpeningInput();
        if (!input || !_source)
            return Refuse(E_ABORT);
        _input.emplace(std::move(input.value()));
        const auto& loops = _display.selection.value().loops;
        _geometryEnvelope = loops.StorageBytes() + SelectionLoops::WorkOwnershipAllowance(loops.size());
        ++s.captures;
        _phase = Phase::Open;
        return Resume();
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
    if (_phase == Phase::Close && _click)
    {
        s.closed = _close && s.session.ApplyPreparedSelectionMoveClosureIfCurrent(_close.value());
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
    if (s.outcome.status == JobStatus::Finished)
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
    if (s.outcome.status != JobStatus::Finished)
    {
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
    s.sameTicket = s.sameTicket && ticket == s.original;
    s.waiting = false;
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
