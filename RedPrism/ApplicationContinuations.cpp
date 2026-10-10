#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"

#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "FileDialogs.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"
#include "SelectionCommandWork.h"
#include "UiBenchmark.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <variant>

// The source owns the selected input across prompts, settlement, writes and dependent actions. Each callback
// only admits event-time arguments. Pixel/IO snapshots are acquired after preceding history work has finished.
void Application::ContinuationAction::Run() noexcept
{
    _owner.RunHostContinuation();
}

bool Application::InitializeHostContinuations() noexcept
{
    if (!_samplerState)
    {
        static_assert(std::is_nothrow_default_constructible_v<SamplerState>);
        _samplerState.reset(new (std::nothrow) SamplerState);
        if (!_samplerState)
            return false;
    }
    if (_hostSourceRegistered || !_jobs.RegisterUiActionSource(_continuationAction))
    {
        return false;
    }
    _hostSourceRegistered = true;
    return true;
}

void Application::RetireHostContinuation(HostRequestQueue::Entry&& entry, ContinuationRetirement reason) noexcept
{
    entry.input.Retire(reason);
}

ContinuationReceipt Application::QueuePanelCommit(std::uint64_t opening) noexcept
{
    const auto receipt = QueueHostInput(HostPanelOkInput{opening});
    if (receipt.Read().outcome == HostOutcome::Pending)
    {
        _pendingPanelOpening = opening;
        SyncAdjustPanel();
        InvalidateFrame();
    }
    return receipt;
}

ContinuationReceipt Application::QueuePanelCancel(std::uint64_t opening) noexcept
{
    if (_pendingCancelOpening == std::optional(opening) && _pendingCancelDocumentSerial == _session.DocumentSerial() &&
        _pendingCancelReceipt.Read().outcome == HostOutcome::Pending)
        return _pendingCancelReceipt;
    CancelHostContinuations();
    CancelColorCommit(); // direct engine acquisition can exist without a selected host input
    const auto receipt = QueueHostInput(HostPanelCancelInput{opening});
    if (receipt.Read().outcome == HostOutcome::Pending)
    {
        _pendingCancelOpening = opening;
        _pendingCancelDocumentSerial = _session.DocumentSerial();
        _pendingCancelReceipt = receipt;
        if (_pendingDisplayInitialized)
            _pendingDisplay.RequestOwner(); // Exclude/cancel useful preview; original owners retire after join.
        SyncAdjustPanel();
        InvalidateFrame();
    }
    return receipt;
}

ContinuationReceipt Application::QueueSelectionMoveClose(bool cancel) noexcept
{
    const auto opening = _session.SelectionMoveOpening();
    if (!opening)
        return ContinuationReceipt::Failure(E_ABORT);
    if (_pendingSelectionMoveCloseOpening == opening &&
        _pendingSelectionMoveCloseDocumentSerial == _session.DocumentSerial() &&
        _pendingSelectionMoveCloseCancel == cancel &&
        _pendingSelectionMoveCloseReceipt.Read().outcome == HostOutcome::Pending)
        return _pendingSelectionMoveCloseReceipt;
    if (cancel)
        CancelHostContinuations();
    // Stop pointer sampling even when admission refuses. The original outline/history survive for retry.
    DropDrag<OutlineDrag>();
    const auto receipt = QueueHostInput(HostSelectionMoveCloseInput{opening.value(), cancel});
    if (receipt.Read().outcome == HostOutcome::Pending)
    {
        _pendingSelectionMoveCloseOpening = opening;
        _pendingSelectionMoveCloseDocumentSerial = _session.DocumentSerial();
        _pendingSelectionMoveCloseCancel = cancel;
        _pendingSelectionMoveCloseReceipt = receipt;
    }
    ReportHostAdmission(receipt);
    return receipt;
}

bool Application::CanQueueSelectionMoveClick() const noexcept
{
    // Match the settled selection factory's gesture exclusions while this outline is still open.
    return !_session.HasColorEdit() && !_session.PendingGradient() && !_session.PendingTransformEdit() &&
           !_session.PendingBrush() && !_session.HasPendingWarp() && !_session.PendingPixelMove() &&
           !_session.PendingShape() && !_session.PendingLasso();
}

bool Application::CanQueueSelectionSizeOutline() const noexcept
{
    return CanQueueSelectionMoveClick() && !_opacityEdit && !EditUnderWay(CanvasEdit::Crop);
}

bool Application::CanQueueSelectionLifecycleOutline() const noexcept
{
    return CanQueueSelectionSizeOutline() && !SizeCardOpen();
}

std::optional<std::uint64_t> Application::SelectionLifecycleRequestOpening(const HostRequestInput& input) noexcept
{
    if (const auto* close = std::get_if<HostCloseProjectInput>(&input))
        return close->selectionMoveOpening;
    if (const auto* replacement = std::get_if<HostNewInput>(&input))
        return replacement->selectionMoveOpening;
    if (const auto* close = std::get_if<HostCloseWindowInput>(&input))
        return close->selectionMoveOpening;
    if (const auto* ending = std::get_if<HostEndSessionInput>(&input))
        return ending->selectionMoveOpening;
    return std::nullopt;
}

bool Application::SelectionLifecycleOutlineCurrent() const noexcept
{
    if (!_activeHostRequest || !CanQueueSelectionLifecycleOutline())
        return false;
    if (const auto* ending = std::get_if<HostEndSessionInput>(&_activeHostRequest->input.Input()))
        return _endSessionPending && ending->generation == _endSessionGeneration;
    return true;
}

ContinuationReceipt Application::QueueSelectionMoveClick(std::optional<HostWandLayerNewInput> wand) noexcept
{
    // One noncoalescing input owns End and its dependent action. A younger writer cannot pass either phase.
    DropDrag<OutlineDrag>();
    const auto opening = _session.SelectionMoveOpening();
    const auto receipt = !opening || !CanQueueSelectionMoveClick() ? ContinuationReceipt::Failure(E_ABORT)
                         : wand && wand->settings.sampleAllLayers
                             ? ContinuationReceipt::Failure(E_INVALIDARG)
                             : QueueHostInput(HostSelectionMoveClickInput{opening.value(), wand});
    ReportHostAdmission(receipt);
    return receipt;
}

bool Application::SelectionMoveHeaderAction(SelectionAction action) noexcept
{
    return action == SelectionAction::SetMarqueeKind || action == SelectionAction::SetLassoKind ||
           action == SelectionAction::Deselect;
}

ContinuationReceipt Application::QueueSelectionMoveHeader(SelectionAction action, double value) noexcept
{
    DropDrag<OutlineDrag>();
    const auto opening = _session.SelectionMoveOpening();
    const auto receipt = !opening || !CanQueueSelectionMoveClick() ? ContinuationReceipt::Failure(E_ABORT)
                         : !SelectionMoveHeaderAction(action)
                             ? ContinuationReceipt::Failure(E_INVALIDARG)
                             : QueueHostInput(HostSelectionMoveHeaderInput{opening.value(), action, value});
    ReportHostAdmission(receipt);
    return receipt;
}

bool Application::SelectionMoveEndCommand(int command) noexcept
{
    switch (command)
    {
    case ChromeHost::kCommandCut:
    case ChromeHost::kCommandPaste:
    case ChromeHost::kCommandLayerViaCopy:
    case ChromeHost::kCommandCurves:
    case ChromeHost::kCommandLevels:
    case ChromeHost::kCommandHueSaturation:
    case ChromeHost::kCommandExposure:
    case ChromeHost::kCommandGradientMap:
    case ChromeHost::kCommandGrain:
    case ChromeHost::kCommandGaussianBlur:
    case ChromeHost::kCommandMotionBlur:
    case ChromeHost::kCommandAddNoise:
    case ChromeHost::kCommandLensCorrection:
    case ChromeHost::kCommandContentAwareFill:
    case ChromeHost::kCommandNewLevelsAdjustment:
    case ChromeHost::kCommandNewCurvesAdjustment:
    case ChromeHost::kCommandNewHueSaturationAdjustment:
    case ChromeHost::kCommandNewExposureAdjustment:
    case ChromeHost::kCommandNewGradientMapAdjustment:
    case ChromeHost::kCommandNewGrainAdjustment:
    case ChromeHost::kCommandAddMask:
    case ChromeHost::kCommandAddBlackMask:
    case ChromeHost::kCommandSelectAll:
    case ChromeHost::kCommandDeselect:
    case ChromeHost::kCommandInverse:
    case ChromeHost::kCommandLoadLayerSelection:
    case ChromeHost::kCommandLoadMaskSelection:
        return true;
    default:
        return false;
    }
}

Application::SelectionMoveClosePolicy Application::SelectionMovePolicy(const HostRequestInput& input) const noexcept
{
    if (const auto* size = std::get_if<HostSizeApplyInput>(&input);
        size && size->selectionMoveOpening &&
        (size->draft.kind == SizeCardKind::CanvasSize || size->draft.kind == SizeCardKind::ImageSize))
        return SelectionMoveClosePolicy::Cancel;
    if (const auto* tool = std::get_if<HostToolInput>(&input))
        return tool->tool == _tool ? SelectionMoveClosePolicy::Preserve : SelectionMoveClosePolicy::End;
    if (const auto* command = std::get_if<HostCommandInput>(&input))
    {
        if (command->command == ChromeHost::kCommandUndo || command->command == ChromeHost::kCommandRedo)
        {
            // Size cards let history pass. Each earlier canvas edit consumes the first history command.
            for (int edit = static_cast<int>(CanvasEdit::Panel); edit < static_cast<int>(CanvasEdit::OutlineMove);
                 ++edit)
                if (EditUnderWay(static_cast<CanvasEdit>(edit)))
                    return SelectionMoveClosePolicy::Preserve;
            return SelectionMoveClosePolicy::Cancel;
        }
        const auto selected = ChromeHost::ToolForSelectCommand(command->command);
        if (selected != EditorTool::Count)
            return selected == _tool ? SelectionMoveClosePolicy::Preserve : SelectionMoveClosePolicy::End;
        if (SelectionMoveEndCommand(command->command))
            return SelectionMoveClosePolicy::End;
    }
    return SelectionMoveClosePolicy::Preserve;
}

std::optional<std::uint64_t> Application::SelectionMoveRequestOpening(const HostRequestInput& input) noexcept
{
    if (const auto* command = std::get_if<HostCommandInput>(&input))
        return command->selectionMoveOpening;
    if (const auto* tool = std::get_if<HostToolInput>(&input))
        return tool->selectionMoveOpening;
    if (const auto opening = SelectionLifecycleRequestOpening(input))
        return opening;
    if (const auto* size = std::get_if<HostSizeApplyInput>(&input))
        return size->selectionMoveOpening;
    return std::nullopt;
}

ContinuationReceipt Application::QueueHostInput(HostRequestInput input) noexcept
{
    if (_shutdownStarted || !_hostSourceRegistered)
    {
        return ContinuationReceipt::Failure(E_ABORT);
    }
    std::optional<OpacityInputOrder::Proposal> opacityProposal;
    if (auto* opacity = std::get_if<HostLayerOpacityInput>(&input))
    {
        if (!std::isfinite(opacity->opacity) || opacity->phase > ChromeEditPhase::Cancel)
            return ContinuationReceipt::Failure(E_INVALIDARG);
        const OpacityProducerKey key{_session.DocumentSerial(), opacity->id};
        const auto phase = opacity->phase == ChromeEditPhase::Preview  ? OpacityInputOrder::Phase::Preview
                           : opacity->phase == ChromeEditPhase::Commit ? OpacityInputOrder::Phase::Commit
                                                                       : OpacityInputOrder::Phase::Cancel;
        auto proposed = _opacityInputOrder.Propose(phase, _opacityProducerKey == std::optional(key));
        if (!proposed)
            return ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
        opacityProposal.emplace(std::move(proposed.value()));
        opacity->order = opacityProposal.value().Identity();
    }
    // These bounded scalar inputs bind the event's gesture before admission; later Execute cannot close a reopen.
    if (auto* command = std::get_if<HostCommandInput>(&input))
        command->selectionMoveOpening = _session.SelectionMoveOpening();
    else if (auto* tool = std::get_if<HostToolInput>(&input))
        tool->selectionMoveOpening = _session.SelectionMoveOpening();
    else if (auto* size = std::get_if<HostSizeApplyInput>(&input))
    {
        // Only valid Apply on this current card, with no coexisting canvas edit, migrates in this slice.
        size->selectionMoveOpening.reset();
        size->extensionArgb.reset();
        const bool valid = size->draft.kind == SizeCardKind::CanvasSize  ? size->draft.canvas.Valid()
                           : size->draft.kind == SizeCardKind::ImageSize ? size->draft.image.Valid()
                           : size->draft.kind == SizeCardKind::NewCanvas ? NewCanvasDimensions(size->draft).has_value()
                                                                         : false;
        if (valid && size->invocation == _sizeInvocation && SizeCardOpen() && CanQueueSelectionSizeOutline())
            size->selectionMoveOpening = _session.SelectionMoveOpening();
        if (size->selectionMoveOpening && size->draft.kind == SizeCardKind::CanvasSize)
        {
            if (size->draft.extension == 1)
                size->extensionArgb = _session.ForegroundColor();
            else if (size->draft.extension == 2)
                size->extensionArgb = _session.BackgroundColor();
        }
    }
    else if (auto* close = std::get_if<HostCloseProjectInput>(&input))
    {
        close->selectionMoveOpening.reset();
        if (CanQueueSelectionLifecycleOutline())
            close->selectionMoveOpening = _session.SelectionMoveOpening();
    }
    else if (auto* replacement = std::get_if<HostNewInput>(&input))
    {
        replacement->selectionMoveOpening.reset();
        if (CanQueueSelectionLifecycleOutline())
            replacement->selectionMoveOpening = _session.SelectionMoveOpening();
    }
    else if (auto* closingWindow = std::get_if<HostCloseWindowInput>(&input))
    {
        closingWindow->selectionMoveOpening.reset();
        if (CanQueueSelectionLifecycleOutline())
            closingWindow->selectionMoveOpening = _session.SelectionMoveOpening();
    }
    else if (auto* ending = std::get_if<HostEndSessionInput>(&input))
    {
        ending->selectionMoveOpening.reset();
        if (CanQueueSelectionLifecycleOutline() && _endSessionPending && ending->generation == _endSessionGeneration)
            ending->selectionMoveOpening = _session.SelectionMoveOpening();
    }
    if (_session.IsMovingSelection() && SelectionMovePolicy(input) != SelectionMoveClosePolicy::Preserve)
        DropDrag<OutlineDrag>(); // Refusal retains the origin/history, but never accepts later pointer samples.
    if (std::holds_alternative<HostSelectionMoveClickInput>(input) ||
        std::holds_alternative<HostSelectionMoveHeaderInput>(input))
        DropDrag<OutlineDrag>();
    try
    {
        auto owned = HostRequest::TryCreate(input, _hostInputBudget, _hostAdmissionAllocationFailureForTest);
        if (!owned)
        {
            return ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA));
        }
        const auto receipt = QueueHostRequest(std::move(owned.value()));
        if (opacityProposal && receipt.Read().outcome == HostOutcome::Pending)
        {
            // Queue admission and this acknowledgement cannot dispatch or reenter the owner.
            if (!_opacityInputOrder.Accept(opacityProposal.value()))
                std::terminate();
            const auto& opacity = std::get<HostLayerOpacityInput>(input);
            _opacityProducerKey = OpacityProducerKey{_session.DocumentSerial(), opacity.id};
        }
        return receipt;
    }
    catch (const std::bad_alloc&)
    {
        // No request was admitted. Fail the entire construction without allocating a replacement result.
        return ContinuationReceipt::Failure(E_OUTOFMEMORY);
    }
}

ContinuationReceipt Application::QueueHostRequest(HostRequest request) noexcept
{
    const auto receipt = request.Receipt();
    const auto* canvasInput = std::get_if<HostCanvasBrushInput>(&request.Input());
    const bool samplerInput = canvasInput && canvasInput->kind == HostCanvasInputKind::Sampler;
    const bool endsCanvasBrush = std::holds_alternative<HostToolInput>(request.Input()) ||
                                 std::holds_alternative<HostLayerSelectInput>(request.Input()) ||
                                 std::holds_alternative<HostLayerTargetInput>(request.Input()) ||
                                 std::holds_alternative<HostWriteInput>(request.Input());
    const auto ordinal = _historyWork.NextOrdinal();
    if (_shutdownStarted || !_hostSourceRegistered || !ordinal)
    {
        (void)request.Finish(HostOutcome::Failed, !ordinal ? HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW) : E_ABORT);
        return receipt;
    }
    const auto kind = std::holds_alternative<HostWandLayerNewInput>(request.Input()) ? ContinuationKind::WandNew
                                                                                     : ContinuationKind::Command;
    const auto* opacity = std::get_if<HostLayerOpacityInput>(&request.Input());
    const bool preview = opacity && opacity->phase == ChromeEditPhase::Preview;
    const bool fieldInput = opacity != nullptr;
    const auto admission =
        preview
            ? _hostRequests.EnqueueAdjacent(
                  _session.DocumentSerial(), ordinal.value(), kind, request, RetireHostContinuation,
                  [](const HostRequest& previous, const HostRequest& next) noexcept
                  {
                      const auto* older = std::get_if<HostLayerOpacityInput>(&previous.Input());
                      const auto* newer = std::get_if<HostLayerOpacityInput>(&next.Input());
                      return older && newer && older->phase == ChromeEditPhase::Preview &&
                             newer->phase == ChromeEditPhase::Preview && older->id == newer->id &&
                             older->order.stream != 0 && older->order.stream == newer->order.stream;
                  })
            : _hostRequests.Enqueue(_session.DocumentSerial(), ordinal.value(), kind, request, RetireHostContinuation);
    if ((admission == ContinuationAdmission::Queued || admission == ContinuationAdmission::Coalesced) && !fieldInput)
    {
        // Generic accepted inputs are barriers. Rejected inputs consume neither field sequence nor opening.
        (void)_opacityInputOrder.Barrier();
        _opacityProducerKey.reset();
    }
    if (admission != ContinuationAdmission::Queued && admission != ContinuationAdmission::Coalesced)
    {
        (void)request.Finish(HostOutcome::Failed,
                             admission == ContinuationAdmission::Full ? HRESULT_FROM_WIN32(ERROR_BUSY) : E_UNEXPECTED);
    }
    else if (!_jobs.RequestUiActionSource(_continuationAction))
    {
        (void)_hostRequests.Clear(RetireHostContinuation);
    }
    else if (endsCanvasBrush)
    {
        SealCanvasBrushForBoundary();
    }
    if (admission == ContinuationAdmission::Queued || admission == ContinuationAdmission::Coalesced)
    {
        if (!samplerInput)
            SealSamplerForBoundary();
    }
    return receipt;
}

ContinuationReceipt Application::QueueHostPaths(std::span<const std::wstring_view> paths, HostPathAction action,
                                                std::optional<Vec2> point) noexcept
{
    try
    {
        auto owned = HostRequest::TryCopyPaths(paths, action, point, _hostInputBudget);
        return owned ? QueueHostRequest(std::move(owned.value()))
                     : ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA));
    }
    catch (const std::bad_alloc&)
    {
        return ContinuationReceipt::Failure(E_OUTOFMEMORY);
    }
}

ContinuationReceipt Application::QueueHostWrite(HostWriteFormat format, bool saveAs) noexcept
{
    return QueueHostInput(HostWriteInput{format, saveAs, std::nullopt});
}

void Application::ReportHostAdmission(const ContinuationReceipt& receipt) noexcept
{
    if (_uiBenchmark && _uiBenchmark->captureGradientApplyReceipt && !_uiBenchmark->gradientApplyReceipt)
        _uiBenchmark->gradientApplyReceipt = receipt;
    if (receipt.Read().outcome == HostOutcome::Failed)
    {
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_STATUS_BUSY);
    }
}

void Application::FinishHostContinuation(ContinuationReceipt::Result result) noexcept
{
    if (!_activeHostRequest)
    {
        return;
    }
    if (const auto* ending = std::get_if<HostEndSessionInput>(&_activeHostRequest->input.Input());
        ending && ending->generation == _endSessionGeneration)
    {
        _endSessionPending = false;
        _endSessionPromptDeferred = false;
        if (_window)
            (void)ShutdownBlockReasonDestroy(_window.get());
    }
    const bool refreshCancelFeedback = ColorCancelPending();
    if (const auto* cancel = std::get_if<HostPanelCancelInput>(&_activeHostRequest->input.Input());
        cancel && _pendingCancelOpening == std::optional(cancel->opening) &&
        _pendingCancelDocumentSerial == _activeHostRequest->documentSerial)
    {
        _pendingCancelOpening.reset();
        _pendingCancelReceipt = {};
    }
    CompleteDuplicateGestureInput(result.outcome);
    if (const auto* canvas = std::get_if<HostCanvasBrushInput>(&_activeHostRequest->input.Input()))
    {
        if (canvas->kind == HostCanvasInputKind::Brush)
            RetireCanvasBrush(result.outcome, canvas->opening);
        else
            RetireSamplerOpening(canvas->opening);
    }
    if (const auto* close = std::get_if<HostSelectionMoveCloseInput>(&_activeHostRequest->input.Input());
        close && _pendingSelectionMoveCloseOpening == std::optional(close->opening) &&
        _pendingSelectionMoveCloseCancel == close->cancel &&
        _pendingSelectionMoveCloseDocumentSerial == _activeHostRequest->documentSerial)
    {
        _pendingSelectionMoveCloseOpening.reset();
        _pendingSelectionMoveCloseReceipt = {};
    }
    (void)_activeHostRequest->input.Finish(result.outcome, result.error);
    if (std::holds_alternative<HostShapeFinishInput>(_activeHostRequest->input.Input()))
    {
        _shapeCommitPreview.reset();
        SyncToolOverlays();
        InvalidateFrame();
    }
    _activeHostRequest.reset();
    SweepSamplerOpenings();
    _pendingPanelOpening.reset();
    _hostCommitStamp.reset();
    _hostFinishedStamp.reset();
    _hostPrerequisite = {HostOutcome::Pending, 0};
    _hostStage = HostStage::Begin;
    _hostPathIndex = 0;
    _hostSawProject = false;
    _hostImportNewProject = false;
    _hostSaveBeforeAction = false;
    _hostWriteInFlight = false;
    _hostBatchFailed = false;
    _hostBatchError = S_OK;
    if (refreshCancelFeedback && !_shutdownStarted)
    {
        SyncAdjustPanel();
        if (_pendingDisplayInitialized)
            _pendingDisplay.RequestOwner(); // Re-admit useful preview after failed/stale close acknowledgement.
        InvalidateFrame();
    }
    _pathClassifications = {};
    _historyResidencySource.RetryDeferred();
    if (!_shutdownStarted && (_hostRequests.Size() != 0 || _canvasLifetime))
    {
        (void)_jobs.RequestUiActionSource(_continuationAction);
    }
}

void Application::RunHostContinuation() noexcept
{
    if (_shutdownStarted)
    {
        return;
    }
    if (_canvasLifetime && _canvasLifetime->presenting)
        return; // A nested modal dispatch cannot select or destroy the presenter-owned request.
    RunCanvasLifetimePreflight();
    if (_shutdownStarted)
        return;
    SweepSamplerOpenings();
    if (_activeHostRequest)
    {
        const auto* canvas = std::get_if<HostCanvasBrushInput>(&_activeHostRequest->input.Input());
        if (canvas && canvas->kind == HostCanvasInputKind::Sampler)
        {
            if (_samplerHostTicket)
            {
                AdvanceSamplerReadback();
                return;
            }
            if (_hostPrerequisite.outcome != HostOutcome::Pending)
            {
                FinishHostContinuation(_hostPrerequisite); // Preserve the exact graph's failed/stale/cancel outcome.
                return;
            }
            if (_hostStage == HostStage::Sampler)
            {
                ReplaySamplerOpening(canvas->opening);
                return;
            }
        }
    }
    if (_canvasBrushInput.Busy() && _canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
        RetireCanvasBrush(_canvasBrushReceipt.Read().outcome, _canvasBrushInput.Opening());
    // Preview completion rearms this source so the old record and its full reservation retire before the next
    // capture is admitted. Accepted host mutations take precedence over starting another pending preview.
    if (!_activeHostRequest && _hostRequests.Size() == 0 && !_canvasLifetime)
    {
        LaunchGradientPreview();
        LaunchPixelMovePreview();
        LaunchTransformPreview();
        RefreshColorJobs();
    }
    if (_hostCommitStamp || _historyWork.Busy() || _pathClassificationTicket)
    {
        return; // consumed readiness stays disarmed until this prerequisite's exact UI completion
    }
    if (!_activeHostRequest)
    {
        const auto* front = _hostRequests.Front();
        const bool lifetimeNext = _canvasLifetime && (!front || _canvasLifetime->entry.ordinal < front->ordinal);
        if (lifetimeNext)
        {
            if (!CanvasLifetimeCurrent())
            {
                FinishCanvasLifetimePreflight({HostOutcome::Stale, E_ABORT});
                return;
            }
            const auto stroke = _canvasLifetime->brushReceipt.Read();
            if (stroke.outcome == HostOutcome::Pending)
                return;
            if (stroke.outcome != HostOutcome::Succeeded &&
                !(!_canvasLifetime->save && stroke.outcome == HostOutcome::Cancelled))
            {
                FinishCanvasLifetimePreflight(stroke);
                return;
            }
            _hostSaveBeforeAction = _canvasLifetime->save;
            _hostWriteFormat = HostWriteFormat::Project;
            _hostAfterPrerequisite = HostStage::Execute;
            _hostAfterPanelCancel = HostStage::TransformDiscard;
            _hostStage = _canvasLifetime->save     ? HostStage::Settle
                         : _session.HasColorEdit() ? HostStage::CancelPanel
                                                   : HostStage::TransformDiscard;
            _activeHostRequest.emplace(std::move(_canvasLifetime->entry));
            _canvasLifetime.reset();
        }
        auto taken = _activeHostRequest
                         ? HostRequestQueue::Taken{}
                         : _hostRequests.TakeReady(_session.DocumentSerial(), false, RetireHostContinuation);
        // A stale queued reader has no selected job destructor to release its scalar opening.
        SweepSamplerOpenings();
        // TakeReady may retire a stale queued brush. Its fixed stream must retire in this same action,
        // including when no selected request remains to rearm the source.
        if (_canvasBrushInput.Busy() && _canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
            RetireCanvasBrush(_canvasBrushReceipt.Read().outcome, _canvasBrushInput.Opening());
        if (taken.stale != 0)
        {
            _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_STATUS_QUEUED_STALE);
        }
        if (!_activeHostRequest)
            _activeHostRequest = std::move(taken.ready);
        if (!_activeHostRequest)
        {
            return;
        }
    }
    auto& active = _activeHostRequest.value();
    if (active.documentSerial != _session.DocumentSerial())
    {
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return;
    }
    if (const auto* ending = std::get_if<HostEndSessionInput>(&active.input.Input());
        ending && (!_endSessionPending || ending->generation != _endSessionGeneration))
    {
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return;
    }
    if (_hostPrerequisite.outcome != HostOutcome::Pending)
    {
        if (_hostPrerequisite.outcome != HostOutcome::Succeeded)
        {
            FinishHostContinuation(_hostPrerequisite);
            return;
        }
        _hostPrerequisite = {HostOutcome::Pending, 0};
        if (_hostWriteInFlight)
        {
            _hostWriteInFlight = false;
            if (!_hostSaveBeforeAction)
            {
                FinishHostContinuation({HostOutcome::Succeeded, S_OK});
                return;
            }
            _hostSaveBeforeAction = false;
            _hostStage = _hostAfterPrerequisite;
        }
        else if (_hostStage == HostStage::CancelPanel)
        {
            if (std::holds_alternative<HostPanelCancelInput>(active.input.Input()))
            {
                FinishHostContinuation({HostOutcome::Succeeded, S_OK});
                return;
            }
            _hostStage = _hostAfterPanelCancel;
        }
        else
            _hostStage = _hostStage == HostStage::OpacitySettle      ? _hostAfterOpacity
                         : _hostStage == HostStage::Settle           ? HostStage::GradientSettle
                         : _hostStage == HostStage::GradientSettle   ? HostStage::PixelMoveSettle
                         : _hostStage == HostStage::PixelMoveSettle  ? HostStage::TransformSettle
                         : _hostStage == HostStage::TransformDiscard ? HostStage::Execute
                         : _hostStage == HostStage::SelectionMoveSettle || _hostStage == HostStage::LifecycleOutline
                             ? _hostAfterPrerequisite
                         : _hostSaveBeforeAction || std::holds_alternative<HostWriteInput>(active.input.Input())
                             ? HostStage::Write
                             : _hostAfterPrerequisite;
    }
    // One source dispatch advances a bounded number of fixed stages. Each project in a batch returns to the outer
    // pump so errors/prompts and a younger Windows cancellation cannot destroy owners inside a callback stack.
    for (int transitions = 0; transitions < 8 && _activeHostRequest; ++transitions)
    {
        const auto& input = active.input.Input();
        const auto* paths = std::get_if<HostPathsInput>(&input);
        const auto* newCanvas = std::get_if<HostSizeApplyInput>(&input);
        const auto lifecycleOpening = SelectionLifecycleRequestOpening(input);
        const bool ownsLifecycleOutline =
            (newCanvas && newCanvas->draft.kind == SizeCardKind::NewCanvas && newCanvas->selectionMoveOpening) ||
            lifecycleOpening.has_value();
        if (ownsLifecycleOutline && _hostStage != HostStage::Complete)
        {
            // The prompt precedes closure. After closure, both save and replacement require it to stay closed.
            // A nested output dialog or worker wait can admit a newer edit without changing pixel generation.
            const bool needsOpening =
                _hostStage != HostStage::Write && _hostStage != HostStage::Execute && _hostStage != HostStage::Complete;
            const bool editorCurrent = lifecycleOpening ? SelectionLifecycleOutlineCurrent()
                                                        : newCanvas->invocation == _sizeInvocation && SizeCardOpen() &&
                                                              CanQueueSelectionSizeOutline();
            if (!editorCurrent || (needsOpening ? SelectionMoveRequestOpening(input) != _session.SelectionMoveOpening()
                                                : _session.IsMovingSelection()))
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
        }
        switch (_hostStage)
        {
        case HostStage::Begin:
            _hostAfterPrerequisite = HostStage::Execute;
            if (std::holds_alternative<HostCanvasBrushInput>(input))
            {
                _hostStage = HostStage::Execute;
                break; // Only its opacity prerequisite may run before the captured brush press.
            }
            if (const auto* size = std::get_if<HostSizeApplyInput>(&input);
                size && size->selectionMoveOpening &&
                (size->draft.kind == SizeCardKind::CanvasSize || size->draft.kind == SizeCardKind::ImageSize) &&
                (size->invocation != _sizeInvocation || !SizeCardOpen() || !CanQueueSelectionSizeOutline() ||
                 size->selectionMoveOpening != _session.SelectionMoveOpening()))
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            if (std::holds_alternative<HostSelectionNudgeInput>(input))
            {
                if (_session.IsMovingSelection() || !CanQueueSelectionMoveClick())
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                _hostStage = HostStage::Execute;
                break; // This settled input must not implicitly commit a newly opened unrelated edit.
            }
            if (_session.IsMovingSelection() && SelectionMovePolicy(input) != SelectionMoveClosePolicy::Preserve)
            {
                const auto expected = SelectionMoveRequestOpening(input);
                if (expected != _session.SelectionMoveOpening())
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return; // Refuse the event's replaced opening before settling another edit or capturing owners.
                }
            }
            if (const auto* close = std::get_if<HostSelectionMoveCloseInput>(&input))
            {
                if (_session.SelectionMoveOpening() != std::optional(close->opening))
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                // Closing this exact gesture must not implicitly settle a coexisting color/shape edit.
                _hostStage = HostStage::Execute;
                break;
            }
            if (const auto* header = std::get_if<HostSelectionMoveHeaderInput>(&input))
            {
                if (_session.SelectionMoveOpening() != std::optional(header->opening) || !CanQueueSelectionMoveClick())
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                if (!SelectionMoveHeaderAction(header->action))
                {
                    FinishHostContinuation({HostOutcome::Failed, E_INVALIDARG});
                    return;
                }
                _hostStage = HostStage::Execute;
                break; // Header End uses the original enclosing transaction, like pointer End.
            }
            if (const auto* click = std::get_if<HostSelectionMoveClickInput>(&input))
            {
                if (_session.SelectionMoveOpening() != std::optional(click->opening) || !CanQueueSelectionMoveClick())
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                if (click->wand && click->wand->settings.sampleAllLayers)
                {
                    FinishHostContinuation({HostOutcome::Failed, E_INVALIDARG});
                    return;
                }
                _hostStage = HostStage::Execute;
                break; // Preserve the reference's enclosing transaction through End and its dependent action.
            }
            if (const auto* size = std::get_if<HostSizeApplyInput>(&input);
                size && size->draft.kind == SizeCardKind::NewCanvas)
            {
                if (size->invocation != _sizeInvocation || !SizeCardOpen())
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                if (!NewCanvasDimensions(size->draft))
                {
                    FinishHostContinuation({HostOutcome::Failed, E_INVALIDARG});
                    return;
                }
            }
            if (const auto* duplicate = std::get_if<HostDuplicateBeginInput>(&input);
                duplicate &&
                (!_duplicateGesture || _duplicateGesture->serial != duplicate->gesture ||
                 _duplicateGesture->abandoned || _duplicateGesture->documentSerial != _session.DocumentSerial() ||
                 _session.ActiveLayerID() != duplicate->layer ||
                 _session.TransformDraftRevision() != duplicate->revision))
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            if (const auto* cancel = std::get_if<HostPanelCancelInput>(&input))
            {
                const auto& panel = _session.PendingColorEdit();
                if (!panel || panel->openingSequence != cancel->opening)
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                _hostAfterPanelCancel = HostStage::Complete;
                _hostStage = HostStage::CancelPanel;
            }
            else if (const auto* panel = std::get_if<HostPanelOkInput>(&input))
            {
                const auto& edit = _session.PendingColorEdit();
                if (!edit || panel->opening != edit->openingSequence)
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                _hostStage = HostStage::Settle;
            }
            else if (const auto* gradient = std::get_if<HostGradientApplyInput>(&input))
            {
                const auto* pending = _session.PendingGradient();
                if (!pending || pending->opening != gradient->opening)
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                _hostStage = HostStage::Settle;
            }
            else if (const auto* move = std::get_if<HostPixelMoveApplyInput>(&input))
            {
                const auto* pending = _session.PendingPixelMove();
                if (!pending || pending->opening != move->opening)
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                _hostStage = HostStage::Settle;
            }
            else if (const auto* write = std::get_if<HostWriteInput>(&input))
            {
                _hostWriteFormat = write->format;
                _hostStage = HostStage::Output;
            }
            else if (paths)
            {
                const auto classified = StartPathClassification(*paths);
                if (FAILED(classified))
                {
                    FinishHostContinuation({HostOutcome::Failed, classified});
                    return;
                }
                _hostStage =
                    paths->action == HostPathAction::OpenPaths ? HostStage::PathProject : HostStage::PathImages;
                if (_pathClassificationTicket)
                    return; // completion rearms this same owned request; no filesystem call runs in this source
            }
            else if (std::holds_alternative<HostNewInput>(input) ||
                     std::holds_alternative<HostCloseProjectInput>(input) ||
                     std::holds_alternative<HostCloseWindowInput>(input) ||
                     std::holds_alternative<HostEndSessionInput>(input) ||
                     (std::get_if<HostSizeApplyInput>(&input) &&
                      std::get<HostSizeApplyInput>(input).draft.kind == SizeCardKind::NewCanvas))
            {
                _hostStage = HostStage::Confirm;
            }
            else if (const auto* command = std::get_if<HostCommandInput>(&input);
                     command && (command->command == ChromeHost::kCommandCanvasSize ||
                                 command->command == ChromeHost::kCommandImageSize ||
                                 command->command == ChromeHost::kCommandNewCanvas))
            {
                _hostAfterPanelCancel = HostStage::GradientSettle;
                _hostStage = _session.HasColorEdit() ? HostStage::CancelPanel : HostStage::GradientSettle;
            }
            else
            {
                _hostStage = HostStage::Settle;
            }
            break;
        case HostStage::PathProject:
            while (_hostPathIndex < paths->paths.size() &&
                   (paths->paths[_hostPathIndex].empty() || !HostPathIsProject(_hostPathIndex)))
            {
                ++_hostPathIndex;
            }
            if (_hostPathIndex == paths->paths.size())
            {
                _hostStage = HostStage::PathImages;
            }
            else
            {
                _hostSawProject = true;
                _hostAfterPrerequisite = HostStage::Execute;
                _hostStage = HostStage::Confirm;
            }
            break;
        case HostStage::PathImages:
        {
            std::size_t count = 0;
            const std::wstring* first = nullptr;
            for (std::size_t index = 0; index < paths->paths.size(); ++index)
            {
                const auto& path = paths->paths[index];
                if (!path.empty() && (paths->action != HostPathAction::OpenPaths || !HostPathIsProject(index)))
                {
                    if (!first)
                        first = &path;
                    ++count;
                }
            }
            if (!first)
            {
                FinishHostContinuation(
                    {_hostBatchFailed ? HostOutcome::Failed : HostOutcome::Succeeded,
                     _hostBatchFailed ? (FAILED(_hostBatchError) ? _hostBatchError : E_FAIL) : S_OK});
                return;
            }
            const auto choice = paths->action == HostPathAction::OpenPaths ? PromptImageOpen(FileTitle(*first), count)
                                : paths->action == HostPathAction::ImagesAsProject ? ImageOpenChoice::NewProject
                                                                                   : ImageOpenChoice::AddLayer;
            if (choice == ImageOpenChoice::Cancel)
            {
                FinishHostContinuation({HostOutcome::Cancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)});
                return;
            }
            _hostImportNewProject = choice == ImageOpenChoice::NewProject || !_session.Document();
            _hostPathIndex = paths->paths.size();        // execute the image group, rather than another project
            _hostAfterPrerequisite = HostStage::Execute; // a preceding project worker left FileComplete selected
            _hostStage = _hostImportNewProject ? HostStage::Confirm : HostStage::Settle;
            break;
        }
        case HostStage::Confirm:
        {
            const auto answer = AskDiscardChanges();
            // A modal prompt can receive WM_ENDSESSION(FALSE); its answer belongs only to the same generation.
            if (const auto* ending = std::get_if<HostEndSessionInput>(&input);
                ending && (!_endSessionPending || ending->generation != _endSessionGeneration))
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            if (answer == Prompts::Answer::Cancel)
            {
                FinishHostContinuation({HostOutcome::Cancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)});
                return;
            }
            _hostSaveBeforeAction = answer == Prompts::Answer::First;
            _hostWriteFormat = HostWriteFormat::Project;
            const bool localReplacement = std::holds_alternative<HostNewInput>(input) ||
                                          std::holds_alternative<HostCloseProjectInput>(input) ||
                                          std::holds_alternative<HostCloseWindowInput>(input) ||
                                          std::holds_alternative<HostEndSessionInput>(input) ||
                                          (std::get_if<HostSizeApplyInput>(&input) &&
                                           std::get<HostSizeApplyInput>(input).draft.kind == SizeCardKind::NewCanvas);
            _hostAfterPanelCancel = localReplacement ? HostStage::TransformDiscard : HostStage::Execute;
            _hostStage = _hostSaveBeforeAction                         ? HostStage::Output
                         : localReplacement && _session.HasColorEdit() ? HostStage::CancelPanel
                         : localReplacement                            ? HostStage::TransformDiscard
                                                                       : HostStage::Execute;
            break;
        }
        case HostStage::Output:
        {
            const auto* write = std::get_if<HostWriteInput>(&input);
            const auto result = SelectHostOutput(_hostWriteFormat, write && write->saveAs);
            if (FAILED(result))
            {
                FinishHostContinuation(
                    {result == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? HostOutcome::Cancelled : HostOutcome::Failed,
                     result});
                return;
            }
            _hostStage = HostStage::Settle;
            break;
        }
        case HostStage::CancelPanel:
        {
            const auto outcome = StartColorCancellation(active.ordinal);
            if (outcome == HostOutcome::Pending)
            {
                _hostCommitStamp = _colorCommitStamp;
                return;
            }
            if (outcome != HostOutcome::Succeeded)
            {
                FinishHostContinuation({outcome, _colorCommitError});
                return;
            }
            if (std::holds_alternative<HostPanelCancelInput>(input))
            {
                FinishHostContinuation({HostOutcome::Succeeded, S_OK});
                return;
            }
            _hostStage = _hostAfterPanelCancel;
            break;
        }
        case HostStage::Settle:
        {
            const auto outcome = StartColorCommit(active.ordinal);
            if (outcome == HostOutcome::Pending)
            {
                _hostCommitStamp = _colorCommitStamp;
                return;
            }
            if (outcome != HostOutcome::Succeeded)
            {
                FinishHostContinuation({outcome, _colorCommitError});
                return;
            }
            _hostStage = HostStage::GradientSettle;
            break;
        }
        case HostStage::OpacitySettle:
            FinishHostContinuation({HostOutcome::Failed, E_UNEXPECTED});
            return; // Only acknowledgement may leave the held opacity prerequisite.
        case HostStage::CanvasBrush:
            ReplayCanvasBrush();
            return; // One sample per owner action; pending physical input rearms an otherwise idle stream.
        case HostStage::Sampler:
            if (const auto* canvas = std::get_if<HostCanvasBrushInput>(&input);
                canvas && canvas->kind == HostCanvasInputKind::Sampler)
                ReplaySamplerOpening(canvas->opening);
            else
                FinishHostContinuation({HostOutcome::Failed, E_UNEXPECTED});
            return;
        case HostStage::GradientSettle:
        {
            const auto* command = std::get_if<HostCommandInput>(&input);
            const bool dropOrRefresh = command && (command->command == ChromeHost::kCommandUndo ||
                                                   command->command == ChromeHost::kCommandRedo ||
                                                   command->command == ChromeHost::kCommandSwapPalette ||
                                                   command->command == ChromeHost::kCommandResetPalette);
            if (!dropOrRefresh)
            {
                if (const auto* apply = std::get_if<HostGradientApplyInput>(&input);
                    apply && (!_session.PendingGradient() || _session.PendingGradient()->opening != apply->opening))
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                const auto result = StartGradientCommit(active.ordinal);
                if (FAILED(result))
                {
                    ++_errorsShown;
                    _lastErrorId =
                        result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
                    _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
                    FinishHostContinuation({HostOutcome::Failed, result});
                    return;
                }
                if (_gradientCommitTicket)
                    return;
            }
            _hostStage = HostStage::PixelMoveSettle;
            break;
        }
        case HostStage::PixelMoveSettle:
        {
            // Pointer release alone commits a lifted-pixel gesture. Tool/history/lifetime termination keeps
            // the existing cancel semantics; it must never silently turn a drag into a committed move.
            if (const auto* apply = std::get_if<HostPixelMoveApplyInput>(&input))
            {
                const auto* pending = _session.PendingPixelMove();
                if (!pending || pending->opening != apply->opening)
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                const auto result = StartPixelMoveCommit(active.ordinal);
                if (FAILED(result))
                {
                    ++_errorsShown;
                    _lastErrorId =
                        result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
                    _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
                    CancelPixelDrag(); // admission failed; the released gesture cannot retain an unowned apply
                    FinishHostContinuation({HostOutcome::Failed, result});
                    return;
                }
                if (_pixelMoveCommitTicket)
                    return;
            }
            _hostStage = HostStage::TransformSettle;
            break;
        }
        case HostStage::TransformSettle:
        {
            const auto* duplicateFinish = std::get_if<HostDuplicateFinishInput>(&input);
            if (duplicateFinish)
            {
                if (!_duplicateGesture || _duplicateGesture->serial != duplicateFinish->gesture ||
                    _duplicateGesture->documentSerial != _session.DocumentSerial())
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                const auto& edit = _session.PendingTransformEdit();
                const bool matches =
                    !edit ||
                    (_duplicateGesture->copy
                         ? edit->layerID == _duplicateGesture->copy && edit->opening == _duplicateGesture->copyOpening
                         : edit->layerID == _duplicateGesture->source &&
                               edit->opening == _duplicateGesture->sourceOpening &&
                               _session.TransformDraftRevision() == _duplicateGesture->sourceRevision);
                if (!matches)
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                if (edit)
                {
                    const auto result = StartTransformWork(duplicateFinish->cancel ? TransformWorkKind::Cancel
                                                                                   : TransformWorkKind::Commit);
                    if (FAILED(result))
                    {
                        ++_errorsShown;
                        _lastErrorId = result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE
                                                                                          : IDS_ERROR_PIXEL_EDIT;
                        _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
                        FinishHostContinuation({HostOutcome::Failed, result});
                        return;
                    }
                    if (_transformWorkTicket)
                        return;
                }
                _hostStage = HostStage::Complete;
                break;
            }
            const auto* finish = std::get_if<HostTransformFinishInput>(&input);
            const auto* command = std::get_if<HostCommandInput>(&input);
            const auto& pending = _session.PendingTransformEdit();
            if (finish && finish->cancel && !pending)
            {
                FinishHostContinuation({HostOutcome::Succeeded, S_OK});
                return; // Its acknowledged cancelled merge may already have restored the exact opening.
            }
            if (finish && (!pending || pending->layerID != finish->layer ||
                           _session.TransformDraftRevision() != finish->revision))
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            const bool historyCancel =
                pending && command &&
                (command->command == ChromeHost::kCommandUndo || command->command == ChromeHost::kCommandRedo);
            if (historyCancel)
                _hostAfterPrerequisite = HostStage::Complete;
            if (pending && !std::holds_alternative<HostLayerNudgeInput>(input) &&
                (finish || historyCancel || !command || !SkipsTransformSettle(command->command)))
            {
                const auto result =
                    StartTransformWork((finish && finish->cancel) || historyCancel ? TransformWorkKind::Cancel
                                                                                   : TransformWorkKind::Commit);
                if (FAILED(result))
                {
                    ++_errorsShown;
                    _lastErrorId =
                        result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
                    _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
                    FinishHostContinuation({HostOutcome::Failed, result});
                    return;
                }
                if (_transformWorkTicket)
                    return;
            }
            _hostStage = _hostSaveBeforeAction && ownsLifecycleOutline ? HostStage::LifecycleOutline
                         : _hostSaveBeforeAction || std::holds_alternative<HostWriteInput>(input)
                             ? HostStage::Write
                             : _hostAfterPrerequisite;
            break;
        }
        case HostStage::TransformDiscard:
        {
            // The confirmed discard owns cancellation and waits for its exact acknowledgement before any
            // local replacement/teardown. The save-prompt Cancel never enters this stage.
            DropDrag<MoveToolDrag>();
            if (_session.PendingTransformEdit())
            {
                const auto result = StartTransformWork(TransformWorkKind::Cancel);
                if (FAILED(result))
                {
                    ++_errorsShown;
                    _lastErrorId =
                        result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
                    _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
                    FinishHostContinuation({HostOutcome::Failed, result});
                    return;
                }
                if (_transformWorkTicket)
                    return;
            }
            _hostStage = ownsLifecycleOutline ? HostStage::LifecycleOutline : HostStage::Execute;
            break;
        }
        case HostStage::Write:
        {
            if (OpacityEditLayer())
            {
                const auto result = StartOpacityClosure();
                if (FAILED(result))
                {
                    FinishHostContinuation({HostOutcome::Failed, result});
                    return;
                }
                if (_layerMetadataTicket)
                {
                    _hostAfterOpacity = HostStage::Write;
                    _hostStage = HostStage::OpacitySettle;
                    return;
                }
            }
            if (ownsLifecycleOutline)
                _hostAfterPrerequisite = HostStage::Execute; // The close's Write resume target must not replay save.
            const auto result = WriteHostOutput(_hostWriteFormat);
            if (FAILED(result))
            {
                const auto stringId = result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE
                                      : _hostWriteFormat == HostWriteFormat::Project     ? IDS_ERROR_SAVE_PROJECT
                                      : _hostWriteFormat == HostWriteFormat::Jpeg        ? IDS_ERROR_EXPORT_JPEG
                                                                                         : IDS_ERROR_EXPORT_PNG;
                ++_errorsShown;
                _lastErrorId = stringId;
                _prompts->PostNotice(_window.get(), kNoticeMessage, stringId);
                FinishHostContinuation({HostOutcome::Failed, result});
                return;
            }
            if (_hostCommitStamp)
                return; // the writer retains request/path/revision until publication or acknowledged cancellation
            if (!_hostSaveBeforeAction)
            {
                FinishHostContinuation({HostOutcome::Succeeded, S_OK});
                return;
            }
            _hostSaveBeforeAction = false;
            _hostStage = _hostAfterPrerequisite;
            break;
        }
        case HostStage::LifecycleOutline:
        {
            if (!ownsLifecycleOutline)
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            // Preserve prompt Cancel's capture/draft; only its acknowledged Save/Discard stops sampling.
            DropDrag<OutlineDrag>();
            _hostAfterPrerequisite = _hostSaveBeforeAction ? HostStage::Write : HostStage::Execute;
            const auto result =
                StartSelectionMoveClose(SelectionMoveRequestOpening(input).value(), !_hostSaveBeforeAction);
            if (FAILED(result))
            {
                _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
                FinishHostContinuation({HostOutcome::Failed, result});
                return;
            }
            if (_selectionMoveCloseTicket)
                return;
            _hostStage = _hostAfterPrerequisite;
            break;
        }
        case HostStage::SelectionMoveSettle:
        {
            const auto policy = SelectionMovePolicy(input);
            const auto opening = _session.SelectionMoveOpening();
            const auto expected = SelectionMoveRequestOpening(input);
            if (!opening || opening != expected || policy == SelectionMoveClosePolicy::Preserve)
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            _hostAfterPrerequisite =
                policy == SelectionMoveClosePolicy::Cancel && !std::holds_alternative<HostSizeApplyInput>(input)
                    ? HostStage::Complete
                    : HostStage::Execute;
            const auto result =
                opening ? StartSelectionMoveClose(opening.value(), policy == SelectionMoveClosePolicy::Cancel)
                        : E_ABORT;
            if (FAILED(result))
            {
                _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
                FinishHostContinuation({HostOutcome::Failed, result});
                return;
            }
            if (_selectionMoveCloseTicket)
                return;
            _hostStage = _hostAfterPrerequisite;
            break;
        }
        case HostStage::SelectionMoveFollowup:
        {
            const auto* click = std::get_if<HostSelectionMoveClickInput>(&input);
            if (!click || _session.IsMovingSelection())
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            const auto result = click->wand ? StartWandLayerSelection(click->wand.value())
                                            : StartSelectionCommand(SelectionCommandKind::Deselect);
            if (FAILED(result))
            {
                _prompts->PostNotice(_window.get(), kNoticeMessage,
                                     click->wand ? IDS_ERROR_WAND_MEMORY : IDS_ERROR_SELECTION_EDIT);
                FinishHostContinuation({HostOutcome::Failed, result});
                return;
            }
            if (_hostCommitStamp || _historyWork.Busy())
                return;
            _hostStage = HostStage::Complete;
            break;
        }
        case HostStage::SelectionHeaderFollowup:
        {
            const auto* header = std::get_if<HostSelectionMoveHeaderInput>(&input);
            if (!header || _session.IsMovingSelection())
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            if (header->action == SelectionAction::Deselect)
            {
                const auto result = StartSelectionCommand(SelectionCommandKind::Deselect);
                if (FAILED(result))
                {
                    _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
                    FinishHostContinuation({HostOutcome::Failed, result});
                    return;
                }
                if (_hostCommitStamp || _historyWork.Busy())
                    return;
            }
            else if (header->action == SelectionAction::SetMarqueeKind)
            {
                _session.marqueeKind = header->value >= 0.5 ? LassoKind::Ellipse : LassoKind::Rectangle;
                SelectionChanged();
            }
            else if (header->action == SelectionAction::SetLassoKind)
            {
                _session.lassoKind = header->value >= 0.5 ? LassoKind::Polygonal : LassoKind::Freehand;
                SelectionChanged();
            }
            else
            {
                FinishHostContinuation({HostOutcome::Failed, E_INVALIDARG});
                return;
            }
            _hostStage = HostStage::Complete;
            break;
        }
        case HostStage::Execute:
            if (const auto* brush = std::get_if<HostCanvasBrushInput>(&input);
                brush && brush->kind == HostCanvasInputKind::Brush)
            {
                const auto* captured = _canvasBrushInput.CapturedContext(brush->opening);
                if (!captured || captured->documentSerial != _session.DocumentSerial() ||
                    _session.ActiveLayerID() != captured->target || _session.IsMaskSelected() != captured->mask ||
                    _tool != captured->tool)
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
            }
            if (const auto* opacity = std::get_if<HostLayerOpacityInput>(&input);
                opacity && _session.ActiveLayerID() != opacity->id)
            {
                // A selected row closes the previous field. Its equal late terminal acknowledges that closure;
                // other old-target events are stale and must never edit the newly active layer.
                if (opacity->phase == ChromeEditPhase::Commit && _opacityRecorded &&
                    _opacityRecorded.value() == std::clamp(opacity->opacity, 0.0, 1.0))
                {
                    _opacityRecorded.reset();
                    FinishHostContinuation({HostOutcome::Succeeded, S_OK});
                }
                else
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            if (!paths &&
                !(std::get_if<HostCommandInput>(&input) &&
                  (std::get<HostCommandInput>(input).command == ChromeHost::kCommandZoomFit ||
                   std::get<HostCommandInput>(input).command == ChromeHost::kCommandZoomActual ||
                   std::get<HostCommandInput>(input).command == ChromeHost::kCommandZoomIn ||
                   std::get<HostCommandInput>(input).command == ChromeHost::kCommandZoomOut ||
                   std::get<HostCommandInput>(input).command == ChromeHost::kCommandAbout ||
                   std::get<HostCommandInput>(input).command == ChromeHost::kCommandSampleRing ||
                   std::get<HostCommandInput>(input).command == ChromeHost::kCommandKeyboardShortcuts)) &&
                !std::holds_alternative<HostLayerOpacityInput>(input) && !std::holds_alternative<HostNewInput>(input) &&
                !std::holds_alternative<HostCloseProjectInput>(input) &&
                !std::holds_alternative<HostCloseWindowInput>(input) &&
                !std::holds_alternative<HostEndSessionInput>(input) &&
                !(std::get_if<HostCanvasBrushInput>(&input) &&
                  std::get<HostCanvasBrushInput>(input).kind == HostCanvasInputKind::Sampler) &&
                OpacityEditLayer())
            {
                const auto result = StartOpacityClosure();
                if (FAILED(result))
                {
                    FinishHostContinuation({HostOutcome::Failed, result});
                    return;
                }
                if (_layerMetadataTicket)
                {
                    _hostAfterOpacity = HostStage::Execute;
                    _hostStage = HostStage::OpacitySettle;
                    return;
                }
            }
            if (const auto* size = std::get_if<HostSizeApplyInput>(&input);
                size && size->selectionMoveOpening &&
                (size->invocation != _sizeInvocation || !SizeCardOpen() || !CanQueueSelectionSizeOutline()))
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            if (_session.IsMovingSelection() && SelectionMovePolicy(input) != SelectionMoveClosePolicy::Preserve)
            {
                const auto expected = SelectionMoveRequestOpening(input);
                if (expected != _session.SelectionMoveOpening())
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                _hostStage = HostStage::SelectionMoveSettle;
                break;
            }
            _hostExecutionError = S_OK;
            if (const auto* canvas = std::get_if<HostCanvasBrushInput>(&input))
            {
                if (canvas->kind == HostCanvasInputKind::Sampler)
                {
                    ReplaySamplerOpening(canvas->opening);
                    return;
                }
                _hostStage = HostStage::CanvasBrush;
                ReplayCanvasBrush();
                return;
            }
            if (!ExecuteHostInput() || FAILED(_hostExecutionError))
            {
                FinishHostContinuation({HostOutcome::Failed, FAILED(_hostBatchError)       ? _hostBatchError
                                                             : FAILED(_hostExecutionError) ? _hostExecutionError
                                                                                           : E_FAIL});
                return;
            }
            if (_hostCommitStamp || _historyWork.Busy())
                return; // a utility graph retains this input and its ordinal through final apply/drop
            if (paths && _hostPathIndex < paths->paths.size())
            {
                ++_hostPathIndex;
                active.documentSerial = _session.DocumentSerial(); // only this batch can rebind after replacement
                _hostStage = HostStage::PathProject;
                (void)_jobs.RequestUiActionSource(_continuationAction);
                return;
            }
            FinishHostContinuation({_hostBatchFailed ? HostOutcome::Failed : HostOutcome::Succeeded,
                                    _hostBatchFailed ? (FAILED(_hostBatchError) ? _hostBatchError : E_FAIL) : S_OK});
            return;
        case HostStage::FileComplete:
            if (paths && _hostPathIndex < paths->paths.size())
            {
                ++_hostPathIndex;
                _hostStage = HostStage::PathProject;
                (void)_jobs.RequestUiActionSource(_continuationAction);
                return;
            }
            FinishHostContinuation({_hostBatchFailed ? HostOutcome::Failed : HostOutcome::Succeeded,
                                    _hostBatchFailed ? (FAILED(_hostBatchError) ? _hostBatchError : E_FAIL) : S_OK});
            return;
        case HostStage::Complete:
            FinishHostContinuation({HostOutcome::Succeeded, S_OK});
            return;
        }
    }
    if (_activeHostRequest)
    {
        (void)_jobs.RequestUiActionSource(_continuationAction);
    }
}

void Application::ResumeHostHistory(HistoryWorkStamp stamp, HostOutcome outcome, std::int32_t error) noexcept
{
    _historyResidencySource.RetryDeferred();
    if (_shutdownStarted || !_hostSourceRegistered)
    {
        return;
    }
    if (_activeHostRequest && _hostCommitStamp == stamp)
    {
        _hostFinishedStamp = stamp;
        _hostCommitStamp.reset();
        _hostPrerequisite = {outcome, error};
    }
    if (!_hostCommitStamp && (_activeHostRequest || _hostRequests.Size() != 0 || _canvasLifetime))
    {
        // A queued request may have disarmed behind any earlier history operation. Its finisher notifies here
        // after apply/drop and HistoryWork::Finish; operation migrations must wire this same hook.
        (void)_jobs.RequestUiActionSource(_continuationAction);
    }
}

void Application::CancelHostContinuations(bool preserveSealedStroke) noexcept
{
    const bool cancelFeedbackWasPending = ColorCancelPending();
    // Inspect at most the fixed eight slots before mutation. A sealed stroke may still be queued behind
    // opacity or sampling; only its scalar key survives ClearExcept. Active requests live outside that ring.
    const bool preserve = preserveSealedStroke && CanvasBrushSealedStrokePending();
    const auto serial = _session.DocumentSerial();
    const auto opening = _canvasBrushInput.Opening();
    const auto matchesStroke = [serial, opening](const HostRequestQueue::Entry& entry) noexcept
    {
        const auto* input = std::get_if<HostCanvasBrushInput>(&entry.input.Input());
        return entry.documentSerial == serial && input && input->kind == HostCanvasInputKind::Brush &&
               input->opening == opening;
    };
    const bool preserveActive = preserve && _activeHostRequest && matchesStroke(_activeHostRequest.value());
    std::uint64_t preservedOrdinal = 0;
    if (preserve && !preserveActive)
        for (std::size_t offset = 0; offset < _hostRequests.Size(); ++offset)
        {
            const auto* pending = _hostRequests.PendingAt(offset);
            if (pending && matchesStroke(*pending))
            {
                preservedOrdinal = pending->ordinal;
                break;
            }
        }
    FinishCanvasLifetimePreflight({HostOutcome::Cancelled, E_ABORT});
    _shapeCommitPreview.reset();
    SyncToolOverlays();
    if (preserve)
        (void)_hostRequests.ClearExcept(serial, preservedOrdinal, RetireHostContinuation);
    else
        (void)_hostRequests.Clear(RetireHostContinuation);
    _pendingPanelOpening.reset();
    // Clear publishes queued receipt retirement synchronously. A discarded Cancel never reaches the
    // selected finisher, so restore its still-open card here without releasing its live panel payload.
    if (_pendingCancelOpening && _pendingCancelReceipt.Read().outcome != HostOutcome::Pending)
    {
        _pendingCancelOpening.reset();
        _pendingCancelReceipt = {};
        if (cancelFeedbackWasPending && !_shutdownStarted)
        {
            SyncAdjustPanel();
            if (_pendingDisplayInitialized)
                _pendingDisplay.RequestOwner(); // The discarded receipt no longer excludes its live preview.
            InvalidateFrame();
        }
    }
    if (preserveActive)
    {
        SweepSamplerOpenings();
        return; // No cancellation, stage reset or receipt publication for this accepted stroke.
    }
    if (_pathClassificationTicket)
    {
        _pathClassificationCancelled = true;
        (void)_jobs.Cancel(_pathClassificationTicket.value());
        // The selected request and its receipt survive until the outstanding worker acknowledges cancellation.
    }
    else if (_hostCommitStamp)
    {
        if (_clipboardTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_clipboardTicket.value());
        }
        else if (_imageImportTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_imageImportTicket.value());
        }
        else if (_projectOpenTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_projectOpenTicket.value());
        }
        else if (_projectSaveTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_projectSaveTicket.value());
        }
        else if (_exportTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_exportTicket.value());
        }
        else if (_utilityTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_utilityTicket.value());
        }
        else if (_documentLifecycleTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_documentLifecycleTicket.value());
        }
        else if (_sizingTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_sizingTicket.value());
        }
        else if (_historyRestoreTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_historyRestoreTicket.value());
        }
        else if (_selectionNudgeTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_selectionNudgeTicket.value());
        }
        else if (_selectionCommandTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_selectionCommandTicket.value());
        }
        else if (_layerMetadataTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_layerMetadataTicket.value());
        }
        else if (_selectionMoveCloseTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_selectionMoveCloseTicket.value());
        }
        else if (_samplerHostTicket)
        {
            (void)_historyWork.RequestCancel();
            CancelSamplerReadback();
        }
        else if (_wandLayerHostTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_wandLayerHostTicket.value());
        }
        else if (_rasterEditTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_rasterEditTicket.value());
        }
        else if (_shapeTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_shapeTicket.value());
        }
        else if (_maskTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_maskTicket.value());
        }
        else if (_transformWorkTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_transformWorkTicket.value());
        }
        else if (_layerCopyTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_layerCopyTicket.value());
        }
        else if (_gradientCommitTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_gradientCommitTicket.value());
        }
        else if (_pixelMoveCommitTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_pixelMoveCommitTicket.value());
        }
        else
            CancelColorCommit(); // retain the active receipt until apply/drop acknowledges cancellation
    }
    else
    {
        // A panel's acknowledged pixels retain their result. Cancel suppresses any remaining write/replacement
        // stage of a multi-stage input, even if its prerequisite has already applied successfully.
        const auto result = _activeHostRequest && _activeHostRequest->documentSerial != _session.DocumentSerial()
                                ? ContinuationReceipt::Result{HostOutcome::Stale, E_ABORT}
                            : _activeHostRequest &&
                                    std::holds_alternative<HostPanelOkInput>(_activeHostRequest->input.Input()) &&
                                    _hostPrerequisite.outcome != HostOutcome::Pending
                                ? _hostPrerequisite
                                : ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT};
        FinishHostContinuation(result);
    }
    SweepSamplerOpenings(); // Also covers requests cancelled while still queued, before a source was captured.
}

void Application::StopHostContinuations() noexcept
{
    _canvasPanLast.reset();
    CancelSamplerReadback();
    FinishCanvasLifetimePreflight({HostOutcome::Cancelled, E_ABORT});
    _startupPaths.reset();
    if (_hostSourceRegistered)
    {
        (void)_jobs.UnregisterUiActionSource(_continuationAction);
        _hostSourceRegistered = false;
    }
    (void)_hostRequests.Clear(RetireHostContinuation);
    FinishHostContinuation({HostOutcome::Cancelled, E_ABORT});
    if (_canvasBrushInput.Busy())
        RetireCanvasBrush(_canvasBrushReceipt.Read().outcome, _canvasBrushInput.Opening());
    SweepSamplerOpenings();
}

namespace
{
class CountContinuationAction final : public UiAction
{
  public:
    explicit CountContinuationAction(unsigned int& count) noexcept : _count(count) {}
    void Run() noexcept override
    {
        ++_count;
    }

  private:
    unsigned int& _count;
};
} // namespace

int Application::RunHostContinuationSelfTest() noexcept
{
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    const auto settled = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    unsigned int ordinaryRuns = 0; // queued actions borrow this through cleanup's final settle on every exit
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            _hostAdmissionAllocationFailureForTest = false;
            if (_colorWorkTestGate)
            {
                (void)SetEvent(_colorWorkTestGate->release.get());
            }
            CancelHostContinuations();
            CancelColorEditForTest();
            if (!settled())
            {
                SelfTestFailure(L"Host continuation work did not join at its failure boundary.\n");
                std::_Exit(5);
            }
            _colorWorkTestGate.reset();
        });
    constexpr std::array<std::uint8_t, 16> bytes{0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 255, 255, 255, 255};
    const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, bytes.data(), 16);
    const auto prepare = [&]()
    {
        CancelHostContinuations();
        CancelColorEditForTest();
        if (!settled())
        {
            return false;
        }
        _colorWorkTestGate.reset();
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 1;
        ImageLayer layer = ImageLayer::Blank(L"Owned host commit", Vec2{4, 1});
        const Guid active = layer.id;
        layer.asset = RasterAsset{source, source, layer.name};
        document.layers.push_back(std::move(layer));
        ReplaceDocument(std::move(document).Seal(), active, {});
        if (!BeginColorEdit(ChromeHost::kCommandLevels))
        {
            return false;
        }
        OnAdjustAction(AdjustAction::SetInputGamma, 2);
        return settled();
    };
    const auto gate = [this]()
    {
        _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
        _colorWorkTestGate->kind = ColorWorkTestKind::Commit;
        return _colorWorkTestGate->entered && _colorWorkTestGate->release && _colorWorkTestGate->computed;
    };
    if (!prepare() || !_hostSourceRegistered || !_jobs.IsIdle() || _hostRequests.Size() != 0 || _activeHostRequest ||
        _hostInputBudget.Used() != 0)
    {
        return fail(L"The registered host source was not idle before request admission.\n");
    }
    const auto opening = _session.PendingColorEdit()->openingSequence;
    auto fullBudget = _hostInputBudget.TryReserve(HostInputBudget::kStorageLimit);
    const auto refusedBudget = QueuePanelCommit(opening);
    if (!fullBudget ||
        refusedBudget.Read() !=
            ContinuationReceipt::Result{HostOutcome::Failed, HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)} ||
        _hostRequests.Size() != 0 || !_jobs.IsIdle() || !_session.HasColorEdit() ||
        _hostInputBudget.Used() != HostInputBudget::kStorageLimit)
    {
        return fail(L"The host byte budget did not refuse admission without mutation or an unowned receipt.\n");
    }
    fullBudget.reset();
    if (_hostInputBudget.Used() != 0)
    {
        return fail(L"The host byte budget did not return its released reservation.\n");
    }
    std::printf("PASS continuation-host.byte-budget-refusal-and-release\n");
    _hostAdmissionAllocationFailureForTest = true;
    const auto refusedAllocation = QueuePanelCommit(opening);
    _hostAdmissionAllocationFailureForTest = false;
    if (refusedAllocation.Read() != ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY} ||
        _hostRequests.Size() != 0 || !_jobs.IsIdle() || !_session.HasColorEdit() || _hostInputBudget.Used() != 0)
    {
        return fail(L"The host owning construction boundary did not fail allocation without admission.\n");
    }
    std::array<ContinuationReceipt, HostRequestQueue::kCapacity> queued;
    for (auto& receipt : queued)
    {
        receipt = QueuePanelCommit(opening);
        if (receipt.Read().outcome != HostOutcome::Pending)
        {
            return fail(L"A numeric host request was not admitted Pending.\n");
        }
    }
    const auto ninth = QueuePanelCommit(opening);
    if (ninth.Read() != ContinuationReceipt::Result{HostOutcome::Failed, HRESULT_FROM_WIN32(ERROR_BUSY)} ||
        _hostRequests.Size() != HostRequestQueue::kCapacity || _session.history.UndoCount() != 0)
    {
        return fail(L"The ninth host request did not fail locally without history work.\n");
    }
    CancelHostContinuations();
    for (const auto& receipt : queued)
    {
        if (receipt.Read().outcome != HostOutcome::Cancelled)
        {
            return fail(L"Host Clear stranded an accepted receipt Pending.\n");
        }
    }
    if (!settled() || !_jobs.IsIdle() || _activeHostRequest || _hostRequests.Size() != 0 ||
        _hostInputBudget.Used() != 0)
    {
        return fail(L"An empty cleared host source kept waking.\n");
    }
    std::printf("PASS continuation-host.admission-full-allocation-clear-and-idle\n");

    if (!gate())
    {
        return fail(L"Could not create the actual host commit gate.\n");
    }
    for (std::size_t i = 0; i < JobScheduler::kUiActionCapacity; ++i)
    {
        std::unique_ptr<UiAction> action = std::make_unique<CountContinuationAction>(ordinaryRuns);
        if (!_jobs.PostUiAction(action))
        {
            return fail(L"Could not fill the ordinary host action queue.\n");
        }
    }
    const auto committed = QueuePanelCommit(opening);
    for (int pass = 0; pass < 2 && !ColorCommitBusy(); ++pass)
    {
        (void)RunUiActions(_jobs);
    }
    if (!ColorCommitBusy() || !_activeHostRequest || !_hostCommitStamp || ordinaryRuns > 1 ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this, false) ||
        committed.Read().outcome != HostOutcome::Pending || _session.ActiveLayer()->asset->image != source)
    {
        return fail(L"A full ordinary action queue blocked host admission or prematurely completed its receipt.\n");
    }
    const auto activeStamp = _hostCommitStamp.value();
    for (std::size_t i = ordinaryRuns; i < JobScheduler::kUiActionCapacity; ++i)
    {
        if (!RunUiActions(_jobs))
        {
            return fail(L"The waiting host source starved an ordinary action.\n");
        }
    }
    (void)DrainCompletions(_jobs);
    if (ordinaryRuns != JobScheduler::kUiActionCapacity || RunUiActions(_jobs) || _jobs.HasUiWork())
    {
        return fail(L"The waiting host source rearmed without a completion.\n");
    }
    HistoryWorkStamp wrongStamp = activeStamp;
    ++wrongStamp.acquisition;
    ResumeHostHistory(wrongStamp, HostOutcome::Succeeded, 0);
    if (_jobs.HasUiWork() || _hostCommitStamp != activeStamp || committed.Read().outcome != HostOutcome::Pending)
    {
        return fail(L"A mismatched host acquisition resumed the selected request.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    // This harness-only wait observes completion application without running its dependent UI action.
    const ULONGLONG completionDeadline = GetTickCount64() + 10000;
    while (ColorCommitBusy())
    {
        const ULONGLONG now = GetTickCount64();
        if (now >= completionDeadline ||
            WaitForSingleObject(_jobs.WakeHandle(), static_cast<DWORD>(completionDeadline - now)) != WAIT_OBJECT_0)
        {
            return fail(L"The host commit did not signal UI completion.\n");
        }
        (void)DrainCompletions(_jobs);
    }
    if (_historyWork.Busy() || !_jobs.HasUiWork() || committed.Read().outcome != HostOutcome::Pending ||
        _session.HasColorEdit() || _session.history.UndoCount() != 1 || !RunUiActions(_jobs) ||
        committed.Read() != ContinuationReceipt::Result{HostOutcome::Succeeded, 0} || _activeHostRequest)
    {
        return fail(L"The host did not publish success on a later UI action after apply/drop.\n");
    }
    std::array<std::uint8_t, 16> actual{};
    _session.ActiveLayer()->asset->image.ReadRegion(0, 0, 4, 1, actual.data(), 16, 0);
    constexpr std::array<std::uint8_t, 16> expected{0,   0,   0,   255, 128, 128, 128, 255,
                                                    181, 181, 181, 255, 255, 255, 255, 255};
    if (actual != expected || !settled() || !_jobs.IsIdle())
    {
        return fail(L"The resumed host result differed from the independent Gamma 2 pixels or did not become idle.\n");
    }
    std::printf("PASS continuation-host.full-action-queue-wait-exact-stamp-apply-and-receipt\n");

    if (!prepare() || !gate())
    {
        return fail(L"Could not prepare cancellation of an active host request.\n");
    }
    const auto cancelled = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || !ColorCommitBusy() ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this, false))
    {
        return fail(L"The cancelled host request did not reach the worker gate.\n");
    }
    CancelHostContinuations();
    if (cancelled.Read().outcome != HostOutcome::Pending || !_historyWork.Busy() || !_activeHostRequest)
    {
        return fail(L"Host cancellation released ownership or its barrier before worker acknowledgement.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || cancelled.Read() != ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT} ||
        _historyWork.Busy() || _activeHostRequest || _session.ActiveLayer()->asset->image != source ||
        _session.history.UndoCount() != 0 || !_session.HasColorEdit())
    {
        return fail(L"Cancelled host work changed pixels/history or stranded its observer.\n");
    }
    std::printf("PASS continuation-host.cancel-retains-input-through-acknowledged-drop\n");

    if (!prepare() || !gate())
    {
        return fail(L"Could not prepare the throwing host request.\n");
    }
    _colorWorkTestGate->throwAllocationFailure = true;
    const auto failed = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || !ColorCommitBusy())
    {
        return fail(L"The throwing host request did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || failed.Read() != ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY} ||
        _historyWork.Busy() || _activeHostRequest || !_session.HasColorEdit() ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != 0)
    {
        return fail(L"The scheduler's allocation failure was lost by the host receipt or changed surviving state.\n");
    }
    std::printf("PASS continuation-host.actual-runner-allocation-error-survives-publication\n");

    if (!prepare() || !gate())
    {
        return fail(L"Could not prepare an earlier independent color barrier.\n");
    }
    const auto earlierOrdinal = _historyWork.NextOrdinal();
    if (!earlierOrdinal || StartColorCommit(earlierOrdinal.value()) != HostOutcome::Pending ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this, false))
    {
        return fail(L"The earlier independent commit did not start.\n");
    }
    const auto behindEarlier = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || _activeHostRequest || _hostRequests.Size() != 1 ||
        behindEarlier.Read().outcome != HostOutcome::Pending || !_historyWork.Busy())
    {
        return fail(L"A queued request selected a snapshot before an earlier independent barrier finished.\n");
    }
    (void)DrainCompletions(_jobs);
    if (RunUiActions(_jobs) || _jobs.HasUiWork())
    {
        return fail(L"A queued request polled an unchanged earlier barrier.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || behindEarlier.Read().outcome != HostOutcome::Stale || _activeHostRequest ||
        _hostRequests.Size() != 0 || _historyWork.Busy() || _session.history.UndoCount() != 1)
    {
        return fail(L"An earlier independently completed commit lost the queued request's wake-up.\n");
    }
    std::printf("PASS continuation-host.queued-input-resumes-after-independent-history\n");

    constexpr std::array<HistoryWorkKind, 4> laterKinds{HistoryWorkKind::Selection, HistoryWorkKind::Stroke,
                                                        HistoryWorkKind::Open, HistoryWorkKind::Import};
    for (const auto kind : laterKinds)
    {
        if (!prepare())
        {
            return fail(L"Could not prepare the non-color history completion notification.\n");
        }
        const auto ordinal = _historyWork.NextOrdinal();
        const auto barrier =
            ordinal ? _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), kind) : std::nullopt;
        if (!barrier)
        {
            return fail(L"Could not acquire an independent non-color barrier.\n");
        }
        const auto behindOtherKind = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
        if (!RunUiActions(_jobs) || _activeHostRequest || RunUiActions(_jobs) || _jobs.HasUiWork() ||
            behindOtherKind.Read().outcome != HostOutcome::Pending)
        {
            const auto readiness = _jobs.UiReadinessForTest();
            std::printf("DIAG continuation-host.other-history kind=%u active=%u outcome=%u idle=%u "
                        "completions=%u actions=%u source=%u resumes=%u owner=%u memory=%u\n",
                        static_cast<unsigned int>(kind), static_cast<unsigned int>(_activeHostRequest.has_value()),
                        static_cast<unsigned int>(behindOtherKind.Read().outcome),
                        static_cast<unsigned int>(_jobs.IsIdle()), readiness.completions, readiness.actions,
                        static_cast<unsigned int>(readiness.source), static_cast<unsigned int>(readiness.resumes),
                        static_cast<unsigned int>(readiness.owner), static_cast<unsigned int>(readiness.memory));
            return fail(L"A host input selected or polled behind another history kind.\n");
        }
        // Finisher seam only; real selection/stroke/open/import jobs migrate in their own P1 slices.
        if (!_historyWork.Finish(barrier.value()))
        {
            return fail(L"The non-color finisher could not release its exact barrier.\n");
        }
        ResumeHostHistory(barrier.value(), HostOutcome::Succeeded, 0);
        if (!_jobs.HasUiWork() || !settled() || behindOtherKind.Read().outcome != HostOutcome::Succeeded ||
            _hostRequests.Size() != 0 || _activeHostRequest || _historyWork.Busy() ||
            _session.history.UndoCount() != 1 || !_jobs.IsIdle())
        {
            return fail(L"The common non-color completion hook lost a waiting owned request.\n");
        }
    }
    std::printf("PASS continuation-host.all-history-kind-completion-hooks\n");

    if (!prepare() || !gate())
    {
        return fail(L"Could not prepare late successful host cancellation.\n");
    }
    const auto lateCancelled = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || !ColorCommitBusy() ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this, false))
    {
        return fail(L"The late-cancelled host commit did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (WaitForSingleObject(_colorWorkTestGate->computed.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The host commit did not compute before late cancellation.\n");
    }
    CancelHostContinuations();
    if (lateCancelled.Read().outcome != HostOutcome::Pending || !_historyWork.Busy() || !settled() ||
        lateCancelled.Read() != ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT} ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != 0)
    {
        return fail(L"Late successful worker pixels crossed the host's acknowledged cancellation.\n");
    }
    std::printf("PASS continuation-host.late-success-drops-before-cancelled-publication\n");

    for (const bool failWorker : std::array<bool, 2>{false, true})
    {
        if (!prepare() || !gate())
        {
            return fail(L"Could not prepare cancellation after acknowledged terminal work.\n");
        }
        _colorWorkTestGate->throwAllocationFailure = failWorker;
        const auto acknowledged = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
        if (!RunUiActions(_jobs) || !ColorCommitBusy())
        {
            return fail(L"The acknowledged terminal host fixture did not start.\n");
        }
        (void)SetEvent(_colorWorkTestGate->release.get());
        const ULONGLONG terminalDeadline = GetTickCount64() + 10000;
        while (ColorCommitBusy())
        {
            const ULONGLONG now = GetTickCount64();
            if (now >= terminalDeadline ||
                WaitForSingleObject(_jobs.WakeHandle(), static_cast<DWORD>(terminalDeadline - now)) != WAIT_OBJECT_0)
            {
                return fail(L"The terminal host work did not acknowledge apply/drop.\n");
            }
            (void)DrainCompletions(_jobs);
        }
        const ContinuationReceipt::Result terminal =
            failWorker ? ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY}
                       : ContinuationReceipt::Result{HostOutcome::Succeeded, 0};
        if (acknowledged.Read().outcome != HostOutcome::Pending || _hostPrerequisite != terminal ||
            !_activeHostRequest || _historyWork.Busy())
        {
            return fail(L"The host receipt did not retain the acknowledged result before deferred publication.\n");
        }
        CancelHostContinuations();
        if (acknowledged.Read() != terminal || _activeHostRequest ||
            _session.history.UndoCount() != (failWorker ? 0 : 1) ||
            (failWorker && (_session.ActiveLayer()->asset->image != source || !_session.HasColorEdit())) ||
            (!failWorker && _session.HasColorEdit()) || !settled() || !_jobs.IsIdle() ||
            acknowledged.Read() != terminal)
        {
            return fail(L"Cancel after apply/drop relabelled an acknowledged outcome or changed its effect.\n");
        }
    }
    std::printf("PASS continuation-host.cancel-after-acknowledgement-preserves-terminal-outcome\n");

    if (!prepare() || !gate())
    {
        return fail(L"Could not prepare active host document replacement.\n");
    }
    const auto activeStale = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || !ColorCommitBusy() ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this, false))
    {
        return fail(L"The active stale host commit did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (WaitForSingleObject(_colorWorkTestGate->computed.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The stale host commit did not compute before replacement.\n");
    }
    const CanvasDocument activeReplacement = _session.Document().value();
    const auto activeReplacementLayer = _session.ActiveLayerID();
    ReplaceDocument(activeReplacement, activeReplacementLayer, {});
    if (!settled() || activeStale.Read().outcome != HostOutcome::Stale || _activeHostRequest || _historyWork.Busy() ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != 0)
    {
        return fail(L"An active host commit applied to a replacement with reused persisted IDs.\n");
    }
    std::printf("PASS continuation-host.active-document-replacement-drops-stale-pixels\n");

    if (!prepare())
    {
        return fail(L"Could not prepare the stale panel host request.\n");
    }
    const auto stalePanel = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    // External lifetime invalidation is distinct from the UI's explicit Cancel, which retires the queue.
    _session.CancelColorEdit();
    if (!BeginColorEdit(ChromeHost::kCommandLevels) || !settled() || stalePanel.Read().outcome != HostOutcome::Stale ||
        !_session.HasColorEdit() || _session.history.UndoCount() != 0)
    {
        return fail(L"An older queued panel OK settled a reopened panel.\n");
    }
    const auto staleDocument = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    const CanvasDocument replacement = _session.Document().value(); // deliberately retain persisted document/layer IDs
    const auto replacementActive = _session.ActiveLayerID();
    ReplaceDocument(replacement, replacementActive, {});
    if (!settled() || staleDocument.Read().outcome != HostOutcome::Stale || _activeHostRequest ||
        _session.history.UndoCount() != 0)
    {
        return fail(L"A queued host request crossed a replacement with reused persisted IDs.\n");
    }
    std::printf("PASS continuation-host.queued-panel-and-document-lifetimes\n");

    CancelColorEditForTest();
    if (!settled())
    {
        return fail(L"Could not settle the metadata host fixture.\n");
    }
    _session.AddAdjustment(AdjustmentKind::Levels);
    const Guid adjustment = _session.ActiveLayerID().value();
    _session.SetAdjustmentEditingID(adjustment);
    if (!_session.BeginAdjustmentEditing(adjustment, source, true))
    {
        return fail(L"Could not open the admitted adjustment metadata request.\n");
    }
    DocumentChanged();
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (!settled())
    {
        return fail(L"Could not settle metadata previews before host admission.\n");
    }
    const int beforeMetadataHistory = _session.history.UndoCount();
    _colorWorkTestGate.reset();
    const auto metadata = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (metadata.Read().outcome != HostOutcome::Pending || !RunUiActions(_jobs) ||
        metadata.Read().outcome != HostOutcome::Pending || !_activeHostRequest || !ColorCommitBusy() ||
        !_historyWork.Busy() || !_session.HasColorEdit() || !settled() ||
        metadata.Read() != ContinuationReceipt::Result{HostOutcome::Succeeded, 0} || _activeHostRequest ||
        _hostCommitStamp || _historyWork.Busy() || _session.HasColorEdit() ||
        _session.history.UndoCount() != beforeMetadataHistory + 1 ||
        _session.ActiveLayer()->adjustment->levels.ranges[0].gamma != 2 ||
        _session.Document()->layers.front().asset->image != source || !settled() || !_jobs.IsIdle() ||
        metadata.Read() != ContinuationReceipt::Result{HostOutcome::Succeeded, 0})
    {
        return fail(L"Admitted host metadata settlement lost its acknowledgement, history or idle state.\n");
    }
    std::printf("PASS continuation-host.admitted-metadata-settlement-and-terminal-receipt\n");

    // A separate owner can close without ending this harness. Its source is ready, but shutdown cannot run it.
    auto closing = std::make_unique<Application>(_instance, true, nullptr, 0);
    const auto closeCleanup = wil::scope_exit([&]() noexcept { closing->Shutdown(); });
    if (FAILED(closing->_jobs.Initialize(1)) || !closing->InitializeHostContinuations())
    {
        return fail(L"Could not prepare a separate host continuation shutdown owner.\n");
    }
    const auto closed = closing->QueuePanelCommit(1);
    closing->Shutdown();
    const auto afterClose = closing->QueuePanelCommit(2);
    if (closed.Read().outcome != HostOutcome::Cancelled ||
        afterClose.Read() != ContinuationReceipt::Result{HostOutcome::Failed, E_ABORT} ||
        closing->_hostSourceRegistered || !closing->_jobs.IsIdle() || closing->_activeHostRequest ||
        closing->_hostRequests.Size() != 0)
    {
        return fail(L"Host shutdown did not unregister readiness and retire each retained observer.\n");
    }
    std::printf("PASS continuation-host.shutdown-unregister-retirement-and-refusal\n");
    return 0;
}

int Application::RunHostFlowSelfTest() noexcept
{
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temporary))
        return fail(L"Host flow temporary directory unavailable.\n");
    const auto prefix = std::wstring(temporary) + L"RedPrism.host-flow-" + std::to_wstring(GetCurrentProcessId());
    const auto project = prefix + L"-source.prism";
    const auto imagePath = prefix + L"-source.png";
    const auto output = prefix + L"-output.prism";
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_colorWorkTestGate)
                (void)SetEvent(_colorWorkTestGate->release.get());
            CancelHostContinuations();
            CancelColorEditForTest();
            _colorWorkTestGate.reset();
            _forcedDiscardAnswer.reset();
            _endSessionPending = false;
            _closeConfirmed = false;
            if (_window)
                (void)ShutdownBlockReasonDestroy(_window.get());
            if (!settle())
                std::_Exit(5);
            (void)DeleteFileW(project.c_str());
            (void)DeleteFileW(imagePath.c_str());
            (void)DeleteFileW(output.c_str());
        });
    const auto source = SolidRaster(4, 1, 0xFF404040u);
    DocumentFixture fixtureDraft;
    fixtureDraft.id = Guid::Generate();
    fixtureDraft.width = 4;
    fixtureDraft.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Host flow", Vec2{4, 1});
    const auto id = layer.id;
    layer.asset = RasterAsset{source, source, layer.name};
    fixtureDraft.layers.push_back(std::move(layer));
    ImageLayer other = ImageLayer::Blank(L"Other host target", Vec2{4, 1});
    const auto otherId = other.id;
    other.asset = RasterAsset{source, source, other.name};
    other.visible = false;
    fixtureDraft.layers.push_back(std::move(other));
    const CanvasDocument fixture = std::move(fixtureDraft).Seal();
    if (SaveProject(project.c_str(), fixture, id, true) != ProjectIoStatus::Ok ||
        ExportDocumentPng(source, 72, imagePath.c_str()) != ProjectIoStatus::Ok)
        return fail(L"Host flow fixtures failed to encode.\n");
    const auto prepare = [&](bool failed) noexcept
    {
        CancelColorEditForTest();
        if (!settle())
            return false;
        _colorWorkTestGate.reset();
        ReplaceDocument(fixture, id, output);
        if (failed)
            _session.SelectAll();
        if (!BeginColorEdit(failed ? ChromeHost::kCommandContentAwareFill : ChromeHost::kCommandLevels))
            return false;
        if (!failed)
            OnAdjustAction(AdjustAction::SetInputGamma, 2);
        if (!settle())
            return false;
        _forcedDiscardAnswer = Prompts::Answer::First;
        _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
        _colorWorkTestGate->kind = ColorWorkTestKind::Commit;
        return _colorWorkTestGate->entered && _colorWorkTestGate->release && _colorWorkTestGate->computed;
    };
    const auto pumpReceipt = [&](const ContinuationReceipt& receipt) noexcept
    {
        for (int step = 0; step < 64 && receipt.Read().outcome == HostOutcome::Pending; ++step)
        {
            (void)DrainCompletions(_jobs);
            (void)RunUiActions(_jobs);
            // The actual close producer posts a callback-safe close. Observe and remove it before it can end the
            // parent harness; ordinary application pumping dispatches this message after the source returns.
            MSG close{};
            while (PeekMessageW(&close, _window.get(), WM_CLOSE, WM_CLOSE, PM_REMOVE))
            {
            }
            if (receipt.Read().outcome == HostOutcome::Pending && !_jobs.HasUiWork() &&
                WaitForSingleObject(_jobs.WakeHandle(), 10000) != WAIT_OBJECT_0)
                return false;
        }
        return receipt.Read().outcome != HostOutcome::Pending;
    };
    constexpr std::array<const char*, 21> names{"save",         "png",           "jpeg",
                                                "new",          "close-project", "close-window",
                                                "end-session",  "open-project",  "image-new-project",
                                                "image-layers", "canvas-size",   "image-size",
                                                "new-canvas",   "tool",          "select-layer",
                                                "target-layer", "invoke-layer",  "reorder-layer",
                                                "rename-layer", "opacity",       "command"};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        for (int mode = 0; mode < 4; ++mode)
        {
            const bool failure = mode == 0;
            (void)DeleteFileW(output.c_str());
            if (!prepare(failure))
                return fail(L"Could not prepare a staged host caller.\n");
            const auto serial = _session.DocumentSerial();
            const auto history = _session.history.UndoCount();
            HostRequestInput input = HostCommandInput{ChromeHost::kCommandNewLayer};
            switch (index)
            {
            case 0:
                input = HostWriteInput{HostWriteFormat::Project, false, output};
                break;
            case 1:
                input = HostWriteInput{HostWriteFormat::Png, false, output};
                break;
            case 2:
                input = HostWriteInput{HostWriteFormat::Jpeg, false, output};
                break;
            case 3:
                input = HostNewInput{};
                break;
            case 4:
                input = HostCloseProjectInput{};
                break;
            case 5:
                input = HostCloseWindowInput{};
                break;
            case 6:
                _endSessionPending = true;
                ++_endSessionGeneration;
                input = HostEndSessionInput{_endSessionGeneration, 0};
                break;
            case 7:
                input = HostPathsInput{{project}, HostPathAction::OpenPaths, {}};
                break;
            case 8:
                input = HostPathsInput{{imagePath}, HostPathAction::ImagesAsProject, {}};
                break;
            case 9:
                input = HostPathsInput{{imagePath}, HostPathAction::ImagesAsLayers, Vec2{2, .5}};
                break;
            case 10:
            case 11:
            case 12:
                _sizeDialog.kind = index == 10   ? SizeCardKind::CanvasSize
                                   : index == 11 ? SizeCardKind::ImageSize
                                                 : SizeCardKind::NewCanvas;
                _sizeDialog.canvas = CanvasSizeDraft(4, 1, 72);
                _sizeDialog.canvas.Set(8, true);
                _sizeDialog.image = ImageSizeDraft(4, 1, 72);
                _sizeDialog.image.Set(8, true);
                _sizeDialog.newWidth = 8;
                _sizeDialog.newHeight = 2;
                ++_sizeInvocation;
                input = HostSizeApplyInput{_sizeDialog, _sizeInvocation};
                break;
            case 13:
                input = HostToolInput{EditorTool::Hand};
                break;
            case 14:
                input = HostLayerSelectInput{otherId};
                break;
            case 15:
                input = HostLayerTargetInput{otherId, false, false};
                break;
            case 16:
                input = HostLayerInvokeInput{otherId};
                break;
            case 17:
                input = HostLayerReorderInput{otherId, id, {}, DxUi::TreeDropPlace::After};
                break;
            case 18:
                input = HostLayerRenameInput{id, L"Owned rename"};
                break;
            case 19:
                input = HostLayerOpacityInput{id, .5, ChromeEditPhase::Commit};
                break;
            }
            const auto receipt = QueueHostInput(std::move(input));
            // Classification is a nonmutating prerequisite before an Open request can select its color settle.
            // Pump its acknowledgement without releasing the actual color-worker gate or assuming a worker
            // finishes between a fixed number of UI actions. Every wait below is a deadlock failure bound.
            const auto startDeadline = GetTickCount64() + 10000;
            while (!ColorCommitBusy() && receipt.Read().outcome == HostOutcome::Pending)
            {
                (void)DrainCompletions(_jobs);
                (void)RunUiActions(_jobs);
                if (ColorCommitBusy())
                    break;
                const auto now = GetTickCount64();
                if (now >= startDeadline ||
                    (!_jobs.HasUiWork() &&
                     WaitForSingleObject(_jobs.WakeHandle(), static_cast<DWORD>(startDeadline - now)) != WAIT_OBJECT_0))
                    return fail(L"A host prerequisite did not acknowledge before the color-worker gate.\n");
            }
            if (!ColorCommitBusy() || receipt.Read().outcome != HostOutcome::Pending ||
                !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this,
                                           false) ||
                _session.DocumentSerial() != serial || _session.ActiveLayer()->asset->image != source ||
                GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                std::fprintf(stderr, "Host flow index=%zu mode=%d colorBusy=%u receipt=%u classifier=%u\n", index, mode,
                             ColorCommitBusy() ? 1U : 0U, static_cast<unsigned int>(receipt.Read().outcome),
                             _pathClassificationTicket ? 1U : 0U);
                return fail(L"A dependent host action ran before its commit acknowledgement.\n");
            }
            OnCommand(ChromeHost::kCommandZoomActual);
            OnTransformAction(TransformAction::FlipHorizontal, 1);
            OnSelectionAction(SelectionAction::SetMode, 2);
            OnGradientAction(GradientAction::Apply, 0);
            OnShapeAction(ShapeAction::SetKind, 1);
            OnCropAction(CropAction::Apply, 0);
            if (_session.Document()->layers.front() != fixture.layers.front() ||
                _session.history.UndoCount() != history)
                return fail(L"A header callback mutated the document beneath admitted history work.\n");
            if (!_window || !_historyWork.Busy())
                return fail(L"View input did not survive a pending host flow.\n");
            if (mode == 2)
                CancelHostContinuations();
            if (mode == 3)
            {
                if (index == 6)
                    OnEndSession(false);
                else
                    ReplaceDocument(fixture, id, {}); // reuse persisted document and layer IDs
            }
            (void)SetEvent(_colorWorkTestGate->release.get());
            if (!pumpReceipt(receipt))
                return fail(L"A staged host receipt did not terminate.\n");
            _closeConfirmed = false;
            if (mode == 0 || mode == 2)
            {
                if (receipt.Read().outcome != (mode == 0 ? HostOutcome::Failed : HostOutcome::Cancelled) ||
                    _session.DocumentSerial() != serial || !_session.HasColorEdit() ||
                    _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != history ||
                    GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
                    return fail(L"Failed/cancelled settlement authorized its dependent action or lost the panel.\n");
            }
            else if (mode == 3)
            {
                if (receipt.Read().outcome != HostOutcome::Stale ||
                    GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
                    return fail(L"A stale document/session-end flow authorized a write or dependent action.\n");
            }
            else
            {
                if (receipt.Read().outcome != HostOutcome::Succeeded || _session.HasColorEdit())
                    return fail(L"A successful host settlement failed to execute its dependent action.\n");
                if ((index >= 14 && index <= 16 && _session.ActiveLayerID() != otherId) ||
                    (index == 16 && (!_chrome->RenameCardOpen() || _renameLayer != otherId)) ||
                    (index == 17 && _session.Document()->layers.front().id != otherId) ||
                    (index == 18 && _session.ActiveLayer()->name != L"Owned rename") ||
                    (index == 19 && _session.ActiveLayer()->opacity != .5) ||
                    (index == 20 && _session.Document()->layers.size() != 3))
                {
                    SelfTestFailure(
                        std::format(L"Successful host caller '{}' did not apply its retained arguments.\n", index));
                    return 5;
                }
                if (index < 3)
                {
                    if (GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES)
                        return fail(L"A successful write receipt preceded actual output.\n");
                    if (index == 0)
                    {
                        const auto saved = LoadProject(output.c_str());
                        if (saved.status != ProjectIoStatus::Ok ||
                            saved.document.layers.front().asset->image.Data()[0] != 128)
                            return fail(L"Save captured pixels before successful gamma settlement.\n");
                    }
                }
            }
            if (!settle() || _historyWork.Busy() || _activeHostRequest || _hostInputBudget.Used())
                return fail(L"A completed staged host flow retained work, input or its byte reservation.\n");
            std::printf("PASS host-flow.%s.%s\n", names[index],
                        mode == 0   ? "failed-fill"
                        : mode == 1 ? "success"
                        : mode == 2 ? "cancel"
                                    : "stale");
        }
    }
    if (!prepare(false))
        return fail(L"Could not prepare Shape release behind a color commit.\n");
    _session.shapeKind = ShapeKind::Rectangle;
    _session.shapeCornerRadius = 0;
    _session.SetForegroundColor(0xFFFF0000u);
    _session.BeginShape(Vec2{0, 0});
    const auto color = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || !ColorCommitBusy() ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this, false))
        return fail(L"Shape release did not reach its preceding color gate.\n");
    DxUi::PointerEvent release{};
    release.action = DxUi::PointerAction::Up;
    if (!DragPointer(release, _viewport.ViewFromDocument(Vec2{2, 1}, DocumentSize())) || _session.PendingShape() ||
        _session.Document()->layers.size() != 2 || _session.history.UndoCount() != 0 || _hostRequests.Size() != 1)
        return fail(L"Shape release recorded history beneath a pending color commit.\n");
    _session.SetForegroundColor(0xFF00FF00u); // execution must use the event-time red, not this later palette value
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!pumpReceipt(color) || !settle() || _session.Document()->layers.size() != 3 ||
        _session.history.UndoCount() != 2 || _session.ActiveLayer()->asset->image.Width() != 2 ||
        _session.ActiveLayer()->asset->image.Data()[2] != 255 || _session.ActiveLayer()->asset->image.Data()[1] != 0 ||
        _hostInputBudget.Used())
        return fail(L"Shape release lost its captured draft/color or ordered history.\n");
    std::printf("PASS host-flow.shape-release-after-commit-with-owned-color\n");
    return 0;
}
