#include "Application.h"

#include "Adjust.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "UiBenchmark.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <utility>

namespace
{
struct CancelOperation final
{
    const char* name;
    int command;
    AdjustAction setting;
    double value;
};
constexpr std::array<CancelOperation, 10> kCancelOperations{{
    {"levels", ChromeHost::kCommandLevels, AdjustAction::SetInputGamma, 1.25},
    {"hsv", ChromeHost::kCommandHueSaturation, AdjustAction::SetSaturation, 20},
    {"exposure", ChromeHost::kCommandExposure, AdjustAction::SetExposure, 0.5},
    {"curves", ChromeHost::kCommandCurves, AdjustAction::SetChannel, 0},
    {"gradient-map", ChromeHost::kCommandGradientMap, AdjustAction::SetReversed, 1},
    {"grain", ChromeHost::kCommandGrain, AdjustAction::SetGrainAmount, 40},
    {"gaussian250", ChromeHost::kCommandGaussianBlur, AdjustAction::SetRadius, 250},
    {"motion2000", ChromeHost::kCommandMotionBlur, AdjustAction::SetDistance, 2000},
    {"noise", ChromeHost::kCommandAddNoise, AdjustAction::SetAmount, 20},
    {"lens", ChromeHost::kCommandLensCorrection, AdjustAction::SetDistortion, 20},
}};

std::int64_t CancelClock() noexcept
{
    LARGE_INTEGER counter{};
    return QueryPerformanceCounter(&counter) && counter.QuadPart >= 0 ? counter.QuadPart : -1;
}

// Diagnostic strings are emitted outside measured work, with JSON quoting for Windows paths/signatures.
void PrintAttributionString(const char* value) noexcept
{
    std::fputc('"', stdout);
    for (; *value; ++value)
    {
        if (*value == '\\' || *value == '"')
            std::fputc('\\', stdout);
        std::fputc(*value, stdout);
    }
    std::fputc('"', stdout);
}

HRESULT ArmCancellationDeadline(HWND window, UiCancellationBenchmark& trial, std::int64_t now,
                                std::uint64_t frequency) noexcept
{
    if (!window || now < 0 || trial.deadline < 0 || trial.activeTimer ||
        trial.nextTimer == (std::numeric_limits<std::uintptr_t>::max)())
        return E_UNEXPECTED;
    const auto remaining = trial.deadline > now
                               ? UiDurationMicroseconds(now, trial.deadline, static_cast<std::int64_t>(frequency))
                               : std::optional<std::uint64_t>{0};
    if (!remaining || remaining.value() > 20000)
        return E_FAIL;
    const auto milliseconds = static_cast<UINT>((remaining.value() + 999) / 1000);
    trial.activeTimer = trial.nextTimer++;
    if (SetTimer(window, trial.activeTimer, (std::max)(milliseconds, static_cast<UINT>(USER_TIMER_MINIMUM)), nullptr))
        return S_OK;
    const auto error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}
} // namespace

const char* Application::UiCancellationOperationName() const noexcept
{
    return _uiBenchmark && _uiBenchmark->operation < kCancelOperations.size()
               ? kCancelOperations[_uiBenchmark->operation].name
               : "unknown";
}

HRESULT Application::PrepareUiCancellationObservation() noexcept
{
    return IsUiCancellationBenchmark(_bench) ? UiCancellationObservation::TryCreate(_preUiCancellationObservation)
                                             : S_OK;
}

JobExecutionObserver Application::UiCancellationObserver() noexcept
{
    if (_preUiCancellationObservation)
        return _preUiCancellationObservation->Observer();
    return _uiBenchmark && _uiBenchmark->cancellation && _uiBenchmark->cancellation->observation
               ? _uiBenchmark->cancellation->observation->Observer()
               : JobExecutionObserver{};
}

HANDLE Application::UiCancellationWakeHandle() const noexcept
{
    if (_preUiCancellationObservation)
        return _preUiCancellationObservation->WakeHandle();
    return _uiBenchmark && _uiBenchmark->cancellation && _uiBenchmark->cancellation->observation
               ? _uiBenchmark->cancellation->observation->WakeHandle()
               : nullptr;
}

void Application::DisarmUiCancellationTimer() noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return;
    auto& trial = *_uiBenchmark->cancellation;
    const auto timer = std::exchange(trial.activeTimer, 0);
    if (timer && _window)
        (void)KillTimer(_window.get(), timer);
}

bool Application::UiCancellationOwnersSettled() const noexcept
{
    // Published rows retain their completed original charge identity in _layerRowsCacheTicket.
    // Their immutable ownership stays live; _layerRowsTicket tracks actual preparation work.
    return _jobs.IsIdle() && !_ownerWork.HasPending() && !_ownerWork.HasReady() && !_activeHostRequest &&
           !_hostRequests.Size() && !_historyWork.Busy() && !_colorCommitTicket && !_colorPreviewTicket &&
           !_colorHistogramTicket && !_layerRowsTicket;
}

void Application::PrepareUiCancellationStep() noexcept
{
    auto& state = *_uiBenchmark;
    auto& trial = *state.cancellation;
    // Every replacement follows joined original work. No observer cell or event is recreated/reset here.
    if (((trial.original || trial.applyReceipt || trial.cancelReceipt) && !UiCancellationOwnersSettled()) ||
        trial.activeTimer)
    {
        FailUiBenchmark("cancel-prepare-before-original-settlement");
        return;
    }
    trial.ResetTrialOwner();
    if (_chrome)
    {
        // Untimed fixture context: an earlier reported refusal must not leave a floating notice over Escape.
        _chrome->CloseNotice();
        _chrome->SetCanvasFocused(true);
    }
    _session.CancelColorEdit();
    _session.ReplaceDocument(state.fixture, state.activeLayer);
    _session.filterSettings = {};
    OnTool(EditorTool::Move);
    _viewport.Fit(DocumentSize());
    _documentPath.clear();
    // Capture refusal authority before the real command. Budget contexts are reported only after its
    // original notice, source/history preservation, owner settlement and a subsequent visible frame.
    state.previousImage =
        _session.ActiveLayer() && _session.ActiveLayer()->asset ? _session.ActiveLayer()->asset->image : Raster{};
    state.previousRevision = _session.history.CurrentRevision();
    state.previousUndoCount = _session.history.UndoCount();
    trial.documentSerial = _session.DocumentSerial();
    trial.preparationBudgetNotices = _budgetNotices;
    trial.expectedRefusal = ExpectedUiCancellationRefusal(state.fixture.width, state.fixture.height, state.operation);
    OnCommand(kCancelOperations[state.operation].command);
    state.previousPresents = _renderer.PresentCount();
    state.phase = UiBenchmarkState::Phase::Preparing;
    DocumentChanged();
}

void Application::ExecuteUiCancellationStep() noexcept
{
    auto& state = *_uiBenchmark;
    auto& trial = *state.cancellation;
    if (trial.phase != UiCancellationBenchmark::Phase::ApplyPosted)
        return;
    trial.phase = UiCancellationBenchmark::Phase::AwaitingBody;
    OnAdjustAction(AdjustAction::Apply, 0); // The actual OK caller owns admission and ordered original capture.
    InvalidateFrame();
}

void Application::NoteUiCancellationApplyReceipt(std::uint64_t opening, ContinuationReceipt receipt) noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return;
    auto& trial = *_uiBenchmark->cancellation;
    if (trial.phase == UiCancellationBenchmark::Phase::AwaitingBody && opening == trial.opening &&
        _session.DocumentSerial() == trial.documentSerial)
        trial.applyReceipt = std::move(receipt);
}

void Application::NoteUiCancellationCommitAdmitted(JobTicket original, std::uint64_t opening) noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return;
    auto& trial = *_uiBenchmark->cancellation;
    if (trial.phase != UiCancellationBenchmark::Phase::AwaitingBody || opening != trial.opening ||
        _session.DocumentSerial() != trial.documentSerial)
        return;
    if (trial.original)
    {
        FailUiBenchmark("cancel-duplicate-original-commit");
        return;
    }
    trial.original = original;
    trial.body.emplace(original, JobClass::Commit, JobExecutionLane::Compute, 2);
}

std::int64_t Application::BeginUiCancellationStop(JobTicket original) noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return -1;
    const auto& trial = *_uiBenchmark->cancellation;
    return trial.dispatchActive && trial.original == original && trial.stopBegin < 0 ? CancelClock() : -1;
}

void Application::NoteUiCancellationToken(JobTicket original, bool accepted, std::int64_t begin) noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return;
    auto& trial = *_uiBenchmark->cancellation;
    if (!trial.dispatchActive || trial.original != original || trial.stopBegin >= 0)
        return;
    trial.stopBegin = begin;
    trial.stopAccepted = CancelClock();
    trial.tokenAccepted = accepted;
}

void Application::NoteUiCancellationReceipt(std::uint64_t opening, ContinuationReceipt receipt) noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return;
    auto& trial = *_uiBenchmark->cancellation;
    if (!trial.dispatchActive || trial.opening != opening || _session.DocumentSerial() != trial.documentSerial)
        return;
    trial.cancelReceipt = std::move(receipt);
    if (trial.cancelReceipt.value().Read().outcome == HostOutcome::Pending)
    {
        trial.receiptAccepted = CancelClock();
        trial.acceptancePresentOrdinal = _renderer.PresentCount();
        // Exact receipt admission is stamped before SyncAdjustPanel; actual hidden-card state is read
        // separately at the end of the real Escape dispatch.
    }
}

void Application::BeginUiCancellationKey(UINT message, WPARAM key, LPARAM flags) noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return;
    auto& trial = *_uiBenchmark->cancellation;
    if (trial.phase != UiCancellationBenchmark::Phase::EscapePosted || message != WM_KEYDOWN || key != VK_ESCAPE ||
        (flags & (static_cast<LPARAM>(1) << 30)) || KeyboardState::Held(VK_CONTROL) || KeyboardState::Held(VK_MENU) ||
        KeyboardState::Held(VK_SHIFT) || ++trial.escapeDowns != 1)
    {
        trial.ambientInput = true;
        return;
    }
    trial.dispatchBegin = CancelClock();
    trial.dispatchActive = true;
}

void Application::EndUiCancellationKey() noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return;
    auto& trial = *_uiBenchmark->cancellation;
    if (trial.dispatchActive)
    {
        trial.dispatchEnd = CancelClock();
        trial.cardHiddenAtAcceptance =
            trial.receiptAccepted >= 0 && _chrome && !_chrome->AdjustPanelOpen() && ColorCancelPending();
        trial.dispatchActive = false;
        trial.phase = UiCancellationBenchmark::Phase::Settling;
        if (trial.receiptAccepted >= 0 &&
            (!trial.original || _renderer.PresentCount() != trial.acceptancePresentOrdinal ||
             !trial.firstFrame.Accept({trial.original.value(), trial.documentSerial, trial.opening,
                                       trial.acceptancePresentOrdinal, UiCancellationOriginalStateCurrent(),
                                       trial.cardHiddenAtAcceptance})))
        {
            FailUiBenchmark("cancel-frame-acceptance-identity-or-intervening-present");
            return;
        }
    }
}

void Application::NoteUiCancellationKeyUp(UINT message, WPARAM key) noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return;
    auto& trial = *_uiBenchmark->cancellation;
    if (message != WM_KEYUP || key != VK_ESCAPE || trial.escapeDowns != 1 || ++trial.escapeUps != 1)
        trial.ambientInput = true;
}

bool Application::OnUiCancellationTimer(std::uintptr_t timer) noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return false;
    auto& trial = *_uiBenchmark->cancellation;
    if (timer < 64 || timer >= trial.nextTimer)
        return false;
    if (timer != trial.activeTimer)
        return true; // Already disarmed original timer; a stale queued message cannot affect a newer trial.
    DisarmUiCancellationTimer();
    if (trial.phase != UiCancellationBenchmark::Phase::DelayArmed)
        return true;
    if (KeyboardState::Held(VK_CONTROL) || KeyboardState::Held(VK_MENU) || KeyboardState::Held(VK_SHIFT) ||
        KeyboardState::Held(VK_ESCAPE))
    {
        FailUiBenchmark("cancel-ambient-keyboard-state");
        return true;
    }
    const auto now = CancelClock();
    if (now < 0)
    {
        FailUiBenchmark("cancel-deadline-clock");
        return true;
    }
    if (now < trial.deadline)
    {
        // WM_TIMER is a wake-up, not QPC deadline authority. At most two fresh one-shots, never polling.
        if (trial.deadlineRearms >= 2)
            FailUiBenchmark("cancel-deadline-rearm-exhausted");
        else
        {
            ++trial.deadlineRearms;
            if (FAILED(ArmCancellationDeadline(_window.get(), trial, now, _jobQpcFrequency)))
                FailUiBenchmark("cancel-deadline-rearm-admission");
        }
        return true;
    }
    trial.postedAt = now;
    const LPARAM down = 1 | (static_cast<LPARAM>(MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC)) << 16);
    const LPARAM up = down | (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);
    trial.phase = UiCancellationBenchmark::Phase::EscapePosted;
    if (!PostMessageW(_window.get(), WM_KEYDOWN, VK_ESCAPE, down) ||
        !PostMessageW(_window.get(), WM_KEYUP, VK_ESCAPE, up))
        FailUiBenchmark("cancel-real-escape-message-post");
    return true;
}

bool Application::UiCancellationSourceSelected(RecordedDisplayGraph* graph, const CanvasDocument* direct) const noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return false;
    const auto& state = *_uiBenchmark;
    const auto& trial = *state.cancellation;
    const auto* layer = _session.ActiveLayer();
    if (trial.receiptAccepted < 0 || trial.feedbackPresented >= 0 || !trial.authority ||
        !_session.IsDocumentReadCurrent(trial.authority.value()) || !layer || !layer->asset ||
        layer->asset->image != state.previousImage || _session.history.CurrentRevision() != state.previousRevision ||
        _session.history.UndoCount() != state.previousUndoCount || !_chrome || _chrome->AdjustPanelOpen())
        return false;
    if (state.preparedDisplay)
        return graph && graph == _acknowledgedCommittedDisplay && !_acknowledgedStrokePreview &&
               !_pendingDisplay.GraphForFrameOwner() && _committedDisplay.HasCurrentPublicationOwner();
    const auto& document = _session.Document();
    return !graph && document && direct == &document.value();
}

bool Application::UiCancellationOriginalStateCurrent() const noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return false;
    const auto& state = *_uiBenchmark;
    const auto& trial = *state.cancellation;
    const auto* layer = _session.ActiveLayer();
    return trial.authority && _session.IsDocumentReadCurrent(trial.authority.value()) && layer && layer->asset &&
           layer->asset->image == state.previousImage && _session.history.CurrentRevision() == state.previousRevision &&
           _session.history.UndoCount() == state.previousUndoCount;
}

void Application::NoteUiCancellationPresentation(bool sourceSelected) noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return;
    auto& trial = *_uiBenchmark->cancellation;
    if (trial.receiptAccepted < 0 || trial.feedbackPresented >= 0)
        return;
    // Every successful visible Present reaches this path, including an intervening stale frame. Recheck
    // exact owned authority/raster/history after Render returns; a later restore cannot hide that frame.
    const auto result = trial.firstFrame.Observe(
        {trial.original.value_or(JobTicket{}), _session.DocumentSerial(), trial.opening, _renderer.PresentCount(),
         UiCancellationFrame::PresentAttempt::SuccessfulVisible, UiCancellationOriginalStateCurrent(), sourceSelected,
         !sourceSelected, _chrome && !_chrome->AdjustPanelOpen(), _renderer.CanvasDrawn()});
    if (result != UiCancellationFrame::State::Qualified)
    {
        std::printf(
            "UI CANCEL FRAME REFUSAL acceptance=%llu current=%llu sourceSelected=%u sourceCurrent=%u "
            "canvasDrawn=%u cardHidden=%u proofState=%u\n",
            static_cast<unsigned long long>(trial.acceptancePresentOrdinal),
            static_cast<unsigned long long>(_renderer.PresentCount()), static_cast<unsigned int>(sourceSelected),
            static_cast<unsigned int>(UiCancellationOriginalStateCurrent()),
            static_cast<unsigned int>(_renderer.CanvasDrawn()),
            static_cast<unsigned int>(_chrome && !_chrome->AdjustPanelOpen()), static_cast<unsigned int>(result));
        std::fflush(stdout);
        FailUiBenchmark("cancel-first-successful-present-did-not-restore-original-source");
        return;
    }
    trial.feedbackPresented = CancelClock();
}

void Application::AdvanceUiCancellationBenchmark() noexcept
{
    auto& state = *_uiBenchmark;
    auto& trial = *state.cancellation;
    if (trial.phase == UiCancellationBenchmark::Phase::Done)
        return;
    auto& transport = *trial.observation;
    if (FAILED(transport.BeginOwnerDrain()))
    {
        FailUiBenchmark("cancel-observer-transport");
        return;
    }
    for (std::size_t index = 0; index < UiCancellationObservation::kOwnerBatch; ++index)
    {
        UiCancellationObservation::Sample sample;
        if (transport.Read(trial.cursor, sample) != UiCancellationObservation::ReadResult::Published)
            break;
        ++trial.cursor;
        if (trial.body)
            trial.body.value().Observe(sample.notice, sample.ticks);
    }
    if (FAILED(transport.EndOwnerBatch(trial.cursor)) || trial.ambientInput)
    {
        FailUiBenchmark(trial.ambientInput ? "cancel-ambient-input" : "cancel-observer-transport");
        return;
    }
    if (trial.phase == UiCancellationBenchmark::Phase::Preparing)
    {
        if (!UiCancellationOwnersSettled() || _frameInvalidated || _renderer.PresentCount() <= state.previousPresents)
            return;
        const auto& edit = _session.PendingColorEdit();
        if (trial.expectedRefusal == UiCancellationRefusalKind::OpeningPixelBudget)
        {
            trial.phase = UiCancellationBenchmark::Phase::AwaitingRefusal;
            state.previousPresents = _renderer.PresentCount();
            InvalidateFrame();
            return;
        }
        if (!edit)
        {
            FailUiBenchmark("cancel-unexpected-command-refusal");
            return;
        }
        // These extreme full-commit cases must not first run the unrelated expanded preview kernel.
        // Use the actual Preview checkbox outside timing; other cases retain their changed preview.
        if (state.operation == 6 || state.operation == 7)
            OnAdjustAction(AdjustAction::SetPreview, 0);
        OnAdjustAction(kCancelOperations[state.operation].setting, kCancelOperations[state.operation].value);
        if (state.operation == 3)
            OnCurveChanged({CurvePoint{0, 0}, CurvePoint{128, 160}, CurvePoint{255, 255}});
        if (trial.expectedRefusal == UiCancellationRefusalKind::SettingsPixelBudget)
        {
            trial.phase = UiCancellationBenchmark::Phase::AwaitingRefusal;
            state.previousPresents = _renderer.PresentCount();
            InvalidateFrame();
            return;
        }
        if (!_session.PendingColorEdit() ||
            (state.operation == 6 && _session.PendingColorEdit().value().settings.radius != 250) ||
            (state.operation == 7 && _session.PendingColorEdit().value().settings.distance != 2000))
        {
            FailUiBenchmark("cancel-unexpected-settings-refusal");
            return;
        }
        trial.phase = UiCancellationBenchmark::Phase::AwaitingPreview;
        state.previousPresents = _renderer.PresentCount();
        InvalidateFrame();
        return;
    }
    if (trial.phase == UiCancellationBenchmark::Phase::AwaitingRefusal)
    {
        if (!UiCancellationOwnersSettled() || _frameInvalidated || _renderer.PresentCount() <= state.previousPresents)
            return;
        FinishUiCancellationRefusal();
        return;
    }
    if (trial.phase == UiCancellationBenchmark::Phase::AwaitingPreview)
    {
        if (!UiCancellationOwnersSettled() || _frameInvalidated || _renderer.PresentCount() <= state.previousPresents)
            return;
        const auto* layer = _session.ActiveLayer();
        if (!layer || !layer->asset || !_session.PendingColorEdit())
        {
            FailUiBenchmark("cancel-missing-original-source");
            return;
        }
        trial.previewEnabled = _session.PendingColorEdit().value().preview;
        if (trial.previewEnabled != (state.operation != 6 && state.operation != 7))
        {
            FailUiBenchmark("cancel-preview-context-mismatch");
            return;
        }
        if (state.preparedDisplay &&
            (!_committedDisplay.HasCurrentPublicationOwner() ||
             (trial.previewEnabled ? !_pendingDisplay.HasCurrentPublicationOwner()
                                   : _pendingDisplay.GraphForFrameOwner() != nullptr) ||
             !_layerRowsPublishedKey || _layerRowsPublishedKey.value() != CurrentLayerRowsKey()))
        {
            FailUiBenchmark("cancel-prepared-preview-not-current");
            return;
        }
        trial.opening = _session.PendingColorEdit().value().openingSequence;
        trial.documentSerial = _session.DocumentSerial();
        trial.authority = _session.CaptureDocumentReadAuthority();
        state.previousImage = layer->asset->image;
        state.previousRevision = _session.history.CurrentRevision();
        state.previousUndoCount = _session.history.UndoCount();
        const bool attribution = state.timing.attributionEnabled;
        state.timing = {};
        state.timing.attributionEnabled = attribution;
        trial.phase = UiCancellationBenchmark::Phase::ApplyPosted;
        if (!PostMessageW(_window.get(), kUiBenchmarkMessage, 0, 0))
            FailUiBenchmark("cancel-apply-message-post");
        return;
    }
    if (trial.phase == UiCancellationBenchmark::Phase::AwaitingBody)
    {
        if (trial.body && trial.body.value().EntryTicks() >= 0)
        {
            const auto delay = static_cast<std::int64_t>(
                _jobQpcFrequency / 50 + (_jobQpcFrequency % 50 != 0)); // Declared 20 ms after useful entry.
            const auto entry = trial.body.value().EntryTicks();
            const auto now = CancelClock();
            if (delay <= 0 || entry > (std::numeric_limits<std::int64_t>::max)() - delay || now < entry ||
                trial.nextTimer == (std::numeric_limits<std::uintptr_t>::max)())
            {
                FailUiBenchmark("cancel-deadline-clock-or-identity");
                return;
            }
            trial.deadline = entry + delay;
            trial.phase = UiCancellationBenchmark::Phase::DelayArmed;
            if (FAILED(ArmCancellationDeadline(_window.get(), trial, now, _jobQpcFrequency)))
                FailUiBenchmark("cancel-native-deadline-admission");
        }
        else if (UiCancellationOwnersSettled() && trial.applyReceipt &&
                 trial.applyReceipt.value().Read().outcome != HostOutcome::Pending)
            FinishUiCancellationTrial("useful-body-not-run", false);
        return;
    }
    if (trial.phase != UiCancellationBenchmark::Phase::Settling || !UiCancellationOwnersSettled() ||
        trial.escapeUps != 1 || !trial.applyReceipt ||
        trial.applyReceipt.value().Read().outcome == HostOutcome::Pending)
        return;
    if (!trial.body || trial.body.value().Current() == UiCancellationBody::State::Malformed ||
        trial.body.value().Current() == UiCancellationBody::State::ClockFailed || trial.dispatchBegin < 0 ||
        trial.dispatchEnd < trial.dispatchBegin)
    {
        FailUiBenchmark("cancel-original-body-or-dispatch-clock");
        return;
    }
    if (!trial.cancelReceipt)
    {
        if (trial.body.value().ExitTicks() >= 0 && trial.body.value().ExitTicks() < trial.dispatchBegin &&
            trial.applyReceipt.value().Read().outcome == HostOutcome::Succeeded)
            FinishUiCancellationTrial("finished-before-escape", false);
        else
            FailUiBenchmark("cancel-escape-consumed-without-matching-admission");
        return;
    }
    if (trial.cancelReceipt.value().Read().outcome == HostOutcome::Pending)
        return;
    if (trial.cancelReceipt.value().Read().outcome != HostOutcome::Succeeded)
    {
        FailUiBenchmark("cancel-terminal-receipt-failed");
        return;
    }
    if (trial.feedbackPresented < 0)
        return;
    const auto* layer = _session.ActiveLayer();
    if (trial.cancelReceipt.value().Read().outcome != HostOutcome::Succeeded || !trial.tokenAccepted ||
        !trial.cardHiddenAtAcceptance || !layer || !layer->asset || layer->asset->image != state.previousImage ||
        _session.history.CurrentRevision() != state.previousRevision ||
        _session.history.UndoCount() != state.previousUndoCount || _session.HasColorEdit() || !trial.authority ||
        !_session.IsDocumentReadCurrent(trial.authority.value()))
    {
        FailUiBenchmark("cancel-source-history-receipt-or-card");
        return;
    }
    const auto outcome = trial.body.value().ClassifyStop(trial.stopBegin, trial.stopAccepted);
    if (outcome == UiCancellationBody::StopOutcome::Pending)
        return;
    if (outcome == UiCancellationBody::StopOutcome::ReturnedAfterAcceptance)
    {
        const auto raw = trial.body.value().RawResult();
        if (!raw)
            FailUiBenchmark("cancel-missing-original-raw-result");
        else
            FinishUiCancellationTrial(raw.value().status == JobStatus::Failed ? "worker-failed" : "returned-after-stop",
                                      raw.value().status != JobStatus::Failed);
    }
    else if (outcome == UiCancellationBody::StopOutcome::FinishOverlap)
        FinishUiCancellationTrial("finish-overlap", false);
    else if (outcome == UiCancellationBody::StopOutcome::AlreadyFinished)
        FinishUiCancellationTrial("finished-before-stop", false);
    else
        FailUiBenchmark("cancel-invalid-stop-classification");
}

void Application::FinishUiCancellationTrial(const char* outcome, bool measured) noexcept
{
    auto& state = *_uiBenchmark;
    auto& trial = *state.cancellation;
    if (!UiCancellationOwnersSettled() || trial.activeTimer)
    {
        FailUiBenchmark("cancel-result-before-owners-settle");
        return;
    }
    trial.settledAt = CancelClock();
    const auto duration = [this](std::int64_t begin, std::int64_t end) noexcept
    { return UiDurationMicroseconds(begin, end, static_cast<std::int64_t>(_jobQpcFrequency)); };
    const auto returned = duration(trial.stopAccepted, trial.body ? trial.body.value().ExitTicks() : -1);
    const auto feedback = duration(trial.dispatchBegin, trial.feedbackPresented);
    const auto accepted = duration(trial.dispatchBegin, trial.receiptAccepted);
    const auto dispatch = duration(trial.dispatchBegin, trial.dispatchEnd);
    const auto entryToPosted = duration(trial.body ? trial.body.value().EntryTicks() : -1, trial.postedAt);
    const auto postedToDispatch = duration(trial.postedAt, trial.dispatchBegin);
    const auto entryToDispatch = duration(trial.body ? trial.body.value().EntryTicks() : -1, trial.dispatchBegin);
    const auto settled = duration(trial.dispatchBegin, trial.settledAt);
    if (measured && (!returned || !feedback || !accepted || !dispatch || !settled || !entryToPosted ||
                     !postedToDispatch || !entryToDispatch || state.timing.clockFailures))
    {
        FailUiBenchmark("cancel-measured-clock");
        return;
    }
    const bool checkKnown = trial.body && trial.body.value().CheckTicks() >= 0;
    const auto checkOutcome = checkKnown ? trial.body.value().ClassifyCheck(trial.stopBegin, trial.stopAccepted)
                                         : UiCancellationBody::CheckOutcome::Unobserved;
    if (checkOutcome == UiCancellationBody::CheckOutcome::ClockFailed ||
        checkOutcome == UiCancellationBody::CheckOutcome::Malformed)
    {
        FailUiBenchmark("cancel-invalid-first-check-observation");
        return;
    }
    const auto acceptedToCheck = checkOutcome == UiCancellationBody::CheckOutcome::ObservedAfterAcceptance
                                     ? duration(trial.stopAccepted, trial.body.value().CheckTicks())
                                     : std::nullopt;
    if ((checkOutcome == UiCancellationBody::CheckOutcome::ObservedAfterAcceptance && !acceptedToCheck) ||
        (acceptedToCheck && (!returned || acceptedToCheck.value() > returned.value())) ||
        (measured && trial.firstFrame.Current() != UiCancellationFrame::State::Qualified))
    {
        FailUiBenchmark("cancel-first-check-or-first-frame-proof");
        return;
    }
    const auto checkName = [](UiCancellationBody::CheckOutcome outcome) noexcept
    {
        switch (outcome)
        {
        case UiCancellationBody::CheckOutcome::Unobserved:
            return "unobserved";
        case UiCancellationBody::CheckOutcome::BeforeStop:
            return "before-stop";
        case UiCancellationBody::CheckOutcome::StopOverlap:
            return "stop-overlap";
        case UiCancellationBody::CheckOutcome::ObservedAfterAcceptance:
            return "after-acceptance";
        default:
            return "malformed";
        }
    };
    const auto raw = trial.body ? trial.body.value().RawResult() : std::nullopt;
    const auto apply = trial.applyReceipt ? std::optional{trial.applyReceipt.value().Read()} : std::nullopt;
    const auto cancel = trial.cancelReceipt ? std::optional{trial.cancelReceipt.value().Read()} : std::nullopt;
    std::printf(
        "UI CANCEL name=%s trial=%u warmup=%d width=%d height=%d previewEnabled=%d outcome=%s measured=%d "
        "requestedDelayUs=20000 deadlineRearms=%u entryToPostedUs=%llu postedToDispatchUs=%llu entryToDispatchUs=%llu "
        "delayKnown=%d "
        "ticketSlot=%zu ticketGeneration=%llu usefulOrdinal=2 "
        "dispatchUs=%llu admissionUs=%llu stopToReturnUs=%llu escapeToSourcePresentUs=%llu settleUs=%llu "
        "rawKnown=%d rawStatus=%u rawError=0x%08x applyKnown=%d applyOutcome=%u applyError=0x%08x "
        "cancelKnown=%d cancelOutcome=%u cancelError=0x%08x sourcePresent=%d hiddenAtAcceptance=%d "
        "diagnosticBytes=%zu ownerBatch=32 actionP99UpperUs=%llu actionMaxUs=%llu applyMaxUs=%llu ordinaryActivation=0 "
        "g1=0 checkKnown=%d checkOutcome=%s acceptedToCheckUs=%llu checkLatencyKnown=%d "
        "acceptancePresentOrdinal=%llu sourcePresentOrdinal=%llu firstFrameProved=%d\n",
        UiCancellationOperationName(), state.trial, state.trial == 0, state.fixture.width, state.fixture.height,
        trial.previewEnabled, outcome, measured && state.trial != 0, trial.deadlineRearms,
        static_cast<unsigned long long>(entryToPosted.value_or(0)),
        static_cast<unsigned long long>(postedToDispatch.value_or(0)),
        static_cast<unsigned long long>(entryToDispatch.value_or(0)),
        entryToPosted && postedToDispatch && entryToDispatch,
        trial.original ? trial.original.value().slot : JobScheduler::kCapacity,
        static_cast<unsigned long long>(trial.original ? trial.original.value().generation : 0),
        static_cast<unsigned long long>(dispatch.value_or(0)), static_cast<unsigned long long>(accepted.value_or(0)),
        static_cast<unsigned long long>(returned.value_or(0)), static_cast<unsigned long long>(feedback.value_or(0)),
        static_cast<unsigned long long>(settled.value_or(0)), raw.has_value(),
        raw ? static_cast<unsigned int>(raw.value().status) : 3, raw ? static_cast<unsigned int>(raw.value().error) : 0,
        apply.has_value(), apply ? static_cast<unsigned int>(apply.value().outcome) : 0,
        apply ? static_cast<unsigned int>(apply.value().error) : 0, cancel.has_value(),
        cancel ? static_cast<unsigned int>(cancel.value().outcome) : 0,
        cancel ? static_cast<unsigned int>(cancel.value().error) : 0, trial.feedbackPresented >= 0,
        trial.cardHiddenAtAcceptance, sizeof(UiCancellationObservation),
        static_cast<unsigned long long>(
            state.timing.durations[static_cast<std::size_t>(UiTimingKind::UiAction)].PercentileUpperBound(99)),
        static_cast<unsigned long long>(
            state.timing.durations[static_cast<std::size_t>(UiTimingKind::UiAction)].MaximumMicroseconds()),
        static_cast<unsigned long long>(
            state.timing.durations[static_cast<std::size_t>(UiTimingKind::ResultApply)].MaximumMicroseconds()),
        checkKnown, checkName(checkOutcome), static_cast<unsigned long long>(acceptedToCheck.value_or(0)),
        acceptedToCheck.has_value(), static_cast<unsigned long long>(trial.acceptancePresentOrdinal),
        static_cast<unsigned long long>(trial.firstFrame.QualifiedOrdinal()),
        trial.firstFrame.Current() == UiCancellationFrame::State::Qualified);
    if (state.timing.attributionEnabled)
    {
        for (std::uint32_t index = 0; index < state.timing.attributionCursor; ++index)
        {
            const auto& record = state.timing.attributionRecords[index];
            std::printf("UI ATTRIBUTION name=%s trial=%u warmup=%d index=%u kind=%u phase=%u detail=%u "
                        "slot=%zu generation=%llu wallUs=%llu qpcBegin=%lld qpcEnd=%lld qpcFrequency=%lld "
                        "cyclesKnown=%d threadCycles=%llu sourceLine=%u source=",
                        UiCancellationOperationName(), state.trial, state.trial == 0, index,
                        static_cast<unsigned int>(record.kind), static_cast<unsigned int>(record.phase), record.detail,
                        record.identity.slot, static_cast<unsigned long long>(record.identity.generation),
                        static_cast<unsigned long long>(record.wallMicroseconds),
                        static_cast<long long>(record.qpcBegin), static_cast<long long>(record.qpcEnd),
                        static_cast<long long>(record.qpcFrequency), record.threadCycleKnown,
                        static_cast<unsigned long long>(record.threadCycleDelta),
                        static_cast<unsigned int>(record.line));
            PrintAttributionString(record.file);
            std::printf(" function=");
            PrintAttributionString(record.function);
            std::fputc('\n', stdout);
        }
        std::printf("UI ATTRIBUTION SUMMARY name=%s trial=%u warmup=%d enabled=1 count=%u overflow=%llu "
                    "clockFailures=%llu capacity=%zu minimumUs=%llu\n",
                    UiCancellationOperationName(), state.trial, state.trial == 0, state.timing.attributionCursor,
                    static_cast<unsigned long long>(state.timing.attributionOverflowCount),
                    static_cast<unsigned long long>(state.timing.attributionClockFailures),
                    kUiTimingAttributionCapacity,
                    static_cast<unsigned long long>(kUiTimingAttributionMinimumMicroseconds));
    }
    std::fflush(stdout);
    AdvanceUiCancellationTrial();
}

bool Application::UiCancellationRefusalReady() const noexcept
{
    if (!_uiBenchmark || !_uiBenchmark->cancellation)
        return false;
    const auto& state = *_uiBenchmark;
    const auto& trial = *state.cancellation;
    const auto* layer = _session.ActiveLayer();
    const auto& edit = _session.PendingColorEdit();
    const auto notices = static_cast<std::int64_t>(_budgetNotices) - trial.preparationBudgetNotices;
    const UiCancellationRefusalFacts facts{
        trial.expectedRefusal,
        notices >= 0 ? static_cast<std::uint64_t>(notices) : UINT64_MAX,
        UiCancellationOwnersSettled(),
        _session.DocumentSerial() == trial.documentSerial && _session.ActiveLayerID() == state.activeLayer && layer &&
            layer->asset && layer->asset->image == state.previousImage,
        _session.history.CurrentRevision() == state.previousRevision &&
            _session.history.UndoCount() == state.previousUndoCount,
        trial.original || trial.body || trial.applyReceipt || trial.cancelReceipt || _colorCommitTicket,
        trial.activeTimer != 0,
        edit.has_value(),
        edit && Adjust::BlurMargin(edit->kind, edit->settings) > edit->grownMargin,
    };
    return trial.phase == UiCancellationBenchmark::Phase::AwaitingRefusal && IsUiCancellationBudgetRefusal(facts) &&
           (!state.preparedDisplay ||
            (_committedDisplay.HasCurrentPublicationOwner() && _pendingDisplay.GraphForFrameOwner() == nullptr &&
             _layerRowsPublishedKey && _layerRowsPublishedKey.value() == CurrentLayerRowsKey()));
}

void Application::FinishUiCancellationRefusal() noexcept
{
    if (!UiCancellationRefusalReady())
    {
        FailUiBenchmark("cancel-expected-pixel-budget-refusal-not-observed");
        return;
    }
    const auto& state = *_uiBenchmark;
    const auto& trial = *state.cancellation;
    std::printf("UI CANCEL REFUSAL name=%s trial=%u warmup=%d width=%d height=%d stage=%s "
                "reportedBudgetNotices=1 sourceUnchanged=1 historyUnchanged=1 commitWorkerAdmitted=0 kernelMeasured=0 "
                "cancelMeasured=0 ownerCostMeasured=0 ordinaryActivation=0 g1=0\n",
                UiCancellationOperationName(), state.trial, state.trial == 0, state.fixture.width, state.fixture.height,
                trial.expectedRefusal == UiCancellationRefusalKind::OpeningPixelBudget ? "opening" : "settings");
    std::fflush(stdout);
    AdvanceUiCancellationTrial();
}

void Application::AdvanceUiCancellationTrial() noexcept
{
    auto& state = *_uiBenchmark;
    auto& trial = *state.cancellation;
    if (++state.trial > state.runs)
    {
        if (state.operation + 1 == kCancelOperations.size() && !ValidateUiBenchmarkDisplayAuthority())
            return;
        state.trial = 0;
        if (++state.operation == kCancelOperations.size())
        {
            trial.phase = UiCancellationBenchmark::Phase::Done;
            state.phase = UiBenchmarkState::Phase::Done;
            state.complete = true;
            (void)KillTimer(_window.get(), kUiBenchmarkTimerId);
            std::printf("UI CANCEL REPORT status=ok operations=10 width=%d height=%d smoke=%d g1=0 "
                        "smokeCorrectnessOnly=%d displayAuthorityKnown=%d displayChanged=%d\n",
                        state.fixture.width, state.fixture.height, state.smoke, state.smoke,
                        state.displayAuthorityKnown, state.displayContinuity.invalidated);
            std::fflush(stdout);
            PostQuitMessage(0);
            return;
        }
    }
    PrepareUiCancellationStep();
}
