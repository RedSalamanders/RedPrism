#include "Application.h"

#include "ChromeHost.h"
#include "DocumentLifecycleTestGate.h"
#include "ProjectSaveTestGate.h"
#include "ProjectStore.h"
#include "Prompts.h"
#include "SelectionMoveCloseTestGate.h"
#include "SizingWorkTestGate.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

namespace
{
DocumentSelection NewCanvasOutlineSquare(double left, double top)
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{left, top}, Vec2{left + 20, top},
                                                                 Vec2{left + 20, top + 20}, Vec2{left, top + 20}}}};
}
class OccupyNewCanvasOutlineSlot final : public Job
{
  public:
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult) noexcept override {}
};
} // namespace

int Application::RunSelectionNewCanvasFaultSelfTest() noexcept
{
    enum class Mode
    {
        Discard,
        Save,
        PromptCancel,
        OrderedDiscard,
        OrderedSave,
        Pump,
        CloseCancel,
        CloseFailure,
        CloseCapture,
        CloseHistory,
        LifecycleCancel,
        LifecycleFailure,
        LifecycleCapture,
        LifecycleAdmission,
        LifecycleHistory,
        SaveCancel,
        SaveFailure,
        SaveAllocation,
        SaveAdmission,
        SavePublishedCancel,
        HeldOpening,
        HeldCard,
        HeldColor,
        HeldOpacity,
        LifecycleColor,
        LifecycleOpacity,
        SaveColor,
        SelectedClosed,
        SelectedOpening,
        SelectedCard,
        SelectedColor,
        SelectedOpacity,
        HeldCrop,
        LifecycleCrop,
        SelectedCrop,
        CopiedDraft,
        CopiedSavePath
    };
    struct Case final
    {
        const char* name;
        Mode mode;
        bool save = false;
        std::optional<DocumentHistory::PrepareStage> history;
        SizeCardKind kind = SizeCardKind::NewCanvas;
    };
    constexpr std::array cases{
        Case{"discard", Mode::Discard},
        Case{"save", Mode::Save, true},
        Case{"prompt-cancel", Mode::PromptCancel},
        Case{"ordered-discard", Mode::OrderedDiscard},
        Case{"ordered-save", Mode::OrderedSave, true},
        Case{"pump", Mode::Pump},
        Case{"close-cancel", Mode::CloseCancel},
        Case{"close-failure", Mode::CloseFailure},
        Case{"close-capture", Mode::CloseCapture},
        Case{"end-before-snapshot-not-emitted", Mode::CloseHistory, true,
             DocumentHistory::PrepareStage::BeforeSnapshot},
        Case{"end-after-snapshot", Mode::CloseHistory, true, DocumentHistory::PrepareStage::AfterSnapshot},
        Case{"end-entry", Mode::CloseHistory, true, DocumentHistory::PrepareStage::Entry},
        Case{"end-stack-growth", Mode::CloseHistory, true, DocumentHistory::PrepareStage::StackGrowth},
        Case{"end-trim-index", Mode::CloseHistory, true, DocumentHistory::PrepareStage::TrimIndex},
        Case{"lifecycle-cancel", Mode::LifecycleCancel},
        Case{"lifecycle-failure", Mode::LifecycleFailure},
        Case{"lifecycle-capture", Mode::LifecycleCapture},
        Case{"lifecycle-admission", Mode::LifecycleAdmission},
        Case{"lifecycle-history", Mode::LifecycleHistory, false, DocumentHistory::PrepareStage::AfterSnapshot},
        Case{"save-cancel", Mode::SaveCancel, true},
        Case{"save-failure", Mode::SaveFailure, true},
        Case{"save-allocation", Mode::SaveAllocation, true},
        Case{"save-admission", Mode::SaveAdmission, true},
        Case{"save-published-cancel", Mode::SavePublishedCancel, true},
        Case{"held-opening", Mode::HeldOpening},
        Case{"held-card", Mode::HeldCard},
        Case{"held-color", Mode::HeldColor},
        Case{"held-opacity", Mode::HeldOpacity},
        Case{"lifecycle-color", Mode::LifecycleColor},
        Case{"lifecycle-opacity", Mode::LifecycleOpacity},
        Case{"save-color", Mode::SaveColor, true},
        Case{"selected-closed", Mode::SelectedClosed},
        Case{"selected-opening", Mode::SelectedOpening},
        Case{"selected-card", Mode::SelectedCard},
        Case{"selected-color", Mode::SelectedColor},
        Case{"selected-opacity", Mode::SelectedOpacity},
        Case{"held-crop", Mode::HeldCrop},
        Case{"lifecycle-crop", Mode::LifecycleCrop},
        Case{"selected-crop", Mode::SelectedCrop},
        Case{"copied-draft", Mode::CopiedDraft},
        Case{"copied-save-path", Mode::CopiedSavePath, true},
        Case{"canvas-held-opacity", Mode::HeldOpacity, false, std::nullopt, SizeCardKind::CanvasSize},
        Case{"canvas-held-crop", Mode::HeldCrop, false, std::nullopt, SizeCardKind::CanvasSize},
        Case{"image-held-opacity", Mode::HeldOpacity, false, std::nullopt, SizeCardKind::ImageSize},
        Case{"image-held-crop", Mode::HeldCrop, false, std::nullopt, SizeCardKind::ImageSize},
        Case{"canvas-selected-opacity", Mode::SelectedOpacity, false, std::nullopt, SizeCardKind::CanvasSize},
        Case{"canvas-selected-crop", Mode::SelectedCrop, false, std::nullopt, SizeCardKind::CanvasSize},
        Case{"image-selected-opacity", Mode::SelectedOpacity, false, std::nullopt, SizeCardKind::ImageSize},
        Case{"image-selected-crop", Mode::SelectedCrop, false, std::nullopt, SizeCardKind::ImageSize}};
    struct State final
    {
        Application& owner;
        Mode mode;
        bool save;
        SizeCardKind kind;
        ContinuationReceipt receipt;
        std::optional<ContinuationReceipt> younger;
        std::shared_ptr<SelectionMoveCloseTestGate> close;
        std::shared_ptr<DocumentLifecycleTestGate> lifecycle;
        std::shared_ptr<ProjectSaveTestGate> writer;
        std::shared_ptr<SizingWorkTestGate> sizingGuard;
        std::optional<CanvasDocument> before, mutated;
        DocumentHistory::StateView history, mutatedHistory;
        std::optional<std::uint64_t> opening, ordinal;
        std::uint64_t generation = 0, mutatedGeneration = 0;
        std::wstring output;
        bool closeVisited = false, closeHeld = false, lifecycleVisited = false, lifecycleHeld = false;
        bool saveVisited = false, saveHeld = false, cancelPending = false, mutationReached = false;
        bool youngerHeld = true, zoomChanged = false, messagePumped = false;
    };
    constexpr UINT kNewCanvasOutlinePump = WM_APP + 0x4ce;
    constexpr std::size_t temporarySize = 32768;
    auto temporary = std::make_unique<wchar_t[]>(temporarySize);
    const auto units = GetTempPathW(static_cast<DWORD>(temporarySize), temporary.get());
    if (units == 0 || units >= temporarySize)
        return 5;
    const auto root =
        std::filesystem::path(temporary.get()) / (L"RedPrism.NewCanvasOutline." + Guid::Generate().ToString());
    if (!CreateDirectoryW(root.c_str(), nullptr))
        return 5;
    const auto originalViewport = _viewport;
    const auto originalTool = _tool;
    const auto originalPath = _documentPath;
    const auto originalAnswer = _forcedDiscardAnswer;
    const auto restore = wil::scope_exit(
        [&]() noexcept
        {
            if (_selectionMoveCloseTestGate && _selectionMoveCloseTestGate->release)
                (void)SetEvent(_selectionMoveCloseTestGate->release.get());
            if (_documentLifecycleTestGate)
                (void)SetEvent(_documentLifecycleTestGate->release.get());
            if (_projectSaveTestGate)
                (void)SetEvent(_projectSaveTestGate->release.get());
            if (_sizingWorkTestGate)
                (void)SetEvent(_sizingWorkTestGate->release.get());
            CancelHostContinuations();
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5); // Borrowed test state cannot survive a failed acknowledgement.
            _selectionMoveCloseTestGate.reset();
            _documentLifecycleTestGate.reset();
            _projectSaveTestGate.reset();
            _sizingWorkTestGate.reset();
            _hostAdmissionAllocationFailureForTest = false;
            _session.CancelColorEdit();
            CloseOpacityEdit();
            if (_session.IsMovingSelection())
            {
                DropDrag<OutlineDrag>();
                _session.CancelSelectionMove();
            }
            CancelSizeCard();
            _viewport = originalViewport;
            _tool = originalTool;
            _documentPath = originalPath;
            _forcedDiscardAnswer = originalAnswer;
            std::error_code error;
            const auto target = std::filesystem::weakly_canonical(root, error);
            const auto base = std::filesystem::weakly_canonical(std::filesystem::path(temporary.get()), error);
            if (!error && target.parent_path() == base)
                std::filesystem::remove_all(target, error);
        });
    int assertions = 0, failures = 0, modes = 0;
    const auto expect = [&](const char* mode, const char* check, bool value) noexcept
    {
        ++assertions;
        failures += value ? 0 : 1;
        std::fprintf(stderr, "%s selection-newcanvas-fault.%s.%s\n", value ? "PASS" : "FAIL", mode, check);
        return value;
    };
    const auto prepare = [&](SizeCardKind kind = SizeCardKind::NewCanvas) -> bool
    {
        _session.ClearDocument();
        _tool = EditorTool::Marquee;
        _session.CreateDocument(40, 40, false);
        (void)InsertPaintedLayer(_session, L"New Canvas outline pixels", 40, 40, {}, 0, 0xFF808080);
        _session.SetSelection(NewCanvasOutlineSquare(10, 10), L"New Canvas outline fixture");
        _session.history.Reset();
        _session.history.MarkSaved();
        _session.SetOpacity(0.5);
        DocumentChanged();
        OpenSizeCard(kind);
        if (kind != SizeCardKind::NewCanvas)
            OnSizeAction(SizeAction::SetLocked, 0);
        OnSizeAction(SizeAction::SetWidth, 20);
        OnSizeAction(SizeAction::SetHeight, 30);
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled ||
            !_session.BeginSelectionMove())
            return false;
        _session.MoveSelection(5, 7);
        _drag = OutlineDrag{Vec2{20, 20}, Vec2{5, 7}};
        return _session.history.IsModified() && _session.history.UndoCount() == 1 &&
               _session.Selection() == std::optional(NewCanvasOutlineSquare(15, 17));
    };
    const auto mutate = [](State& state, Mode mode) noexcept
    {
        auto& owner = state.owner;
        if (mode == Mode::HeldOpening || mode == Mode::SelectedClosed || mode == Mode::SelectedOpening)
        {
            owner._session.CancelSelectionMove();
            state.mutationReached = !owner._session.IsMovingSelection();
            if (mode != Mode::SelectedClosed)
            {
                state.mutationReached = owner._session.BeginSelectionMove();
                owner._session.MoveSelection(-2, 1);
            }
        }
        else if (mode == Mode::HeldCard || mode == Mode::SelectedCard)
        {
            // Exact invocation change without admitting clipboard metadata work during the held worker.
            ++owner._sizeInvocation;
            state.mutationReached = owner.SizeCardOpen();
        }
        else if (mode == Mode::SelectedCrop)
        {
            owner._session.SetCropFrame(CropRect{0, 0, 1, 1});
            state.mutationReached = owner._session.CropFrame().has_value();
        }
        else if (mode == Mode::HeldOpacity || mode == Mode::LifecycleOpacity || mode == Mode::SelectedOpacity)
        {
            state.mutationReached = owner.OpenOpacityEditForTest(0.5);
        }
        else
        {
            owner._session.BeginColorEdit(FilterKind::GaussianBlur, true);
            state.mutationReached = owner._session.HasColorEdit();
        }
        state.mutated = owner._session.Document();
        state.mutatedHistory = owner._session.history.CaptureState();
        state.mutatedGeneration = owner._session.Generation();
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == kNewCanvasOutlinePump)
        {
            state.messagePumped = state.receipt.Read().outcome == HostOutcome::Pending;
            return;
        }
        if (message.message == SelectionMoveCloseTestGate::kMessage)
        {
            state.closeVisited = true;
            const auto* input = owner._activeHostRequest
                                    ? std::get_if<HostSizeApplyInput>(&owner._activeHostRequest->input.Input())
                                    : nullptr;
            state.ordinal = owner._activeHostRequest ? std::optional(owner._activeHostRequest->ordinal) : std::nullopt;
            state.closeHeld = input && input->draft.kind == state.kind &&
                              (state.kind != SizeCardKind::NewCanvas ||
                               (input->draft.newWidth == 20 && input->draft.newHeight == 30)) &&
                              input->selectionMoveOpening == state.opening && owner._selectionMoveCloseTicket &&
                              !owner._documentLifecycleTicket && !owner._projectSaveTicket && !owner._sizingTicket &&
                              owner._historyWork.Busy() && std::holds_alternative<std::monostate>(owner._drag) &&
                              owner._session.Document() == state.before &&
                              owner._session.history.IsCurrent(state.history) &&
                              state.receipt.Read().outcome == HostOutcome::Pending;
            if (state.younger)
                state.youngerHeld = state.youngerHeld && state.younger->Read().outcome == HostOutcome::Pending;
            if (state.mode == Mode::CloseCancel)
            {
                owner.CancelHostContinuations();
                state.cancelPending = owner._selectionMoveCloseTicket && owner._activeHostRequest &&
                                      state.receipt.Read().outcome == HostOutcome::Pending;
            }
            if (state.mode == Mode::Pump)
            {
                const double before = owner._viewport.zoom;
                owner.OnCommand(ChromeHost::kCommandZoomIn);
                state.zoomChanged = owner._viewport.zoom != before;
                (void)PostThreadMessageW(GetCurrentThreadId(), kNewCanvasOutlinePump, 0, 0);
            }
            if (state.mode == Mode::CopiedDraft)
            {
                owner._sizeDialog.newWidth = 70;
                owner._sizeDialog.newHeight = 80;
                state.mutationReached = owner._sizeDialog.newWidth == 70 && owner._sizeDialog.newHeight == 80;
            }
            if (state.mode == Mode::CopiedSavePath)
            {
                owner._documentPath = state.output + L".different.prism";
                state.mutationReached = owner._documentPath != state.output;
            }
            if (state.mode == Mode::HeldOpening || state.mode == Mode::HeldCard || state.mode == Mode::HeldColor ||
                state.mode == Mode::HeldOpacity || state.mode == Mode::HeldCrop)
            {
                // Keep this fixture mutation local to the dispatch, with literal current-state capture below.
                if (state.mode == Mode::HeldOpening)
                {
                    owner._session.CancelSelectionMove();
                    state.mutationReached = owner._session.BeginSelectionMove();
                    owner._session.MoveSelection(-2, 1);
                }
                else if (state.mode == Mode::HeldCard)
                {
                    ++owner._sizeInvocation;
                    state.mutationReached = owner.SizeCardOpen();
                }
                else if (state.mode == Mode::HeldCrop)
                {
                    owner._session.SetCropFrame(CropRect{0, 0, 1, 1});
                    state.mutationReached = owner._session.CropFrame().has_value();
                }
                else if (state.mode == Mode::HeldOpacity)
                {
                    state.mutationReached = owner.OpenOpacityEditForTest(0.5);
                }
                else
                {
                    owner._session.BeginColorEdit(FilterKind::GaussianBlur, true);
                    state.mutationReached = owner._session.HasColorEdit();
                }
                state.mutated = owner._session.Document();
                state.mutatedHistory = owner._session.history.CaptureState();
                state.mutatedGeneration = owner._session.Generation();
            }
            (void)SetEvent(state.close->release.get());
            return;
        }
        if (message.message == ProjectSaveTestGate::kMessage || message.message == DocumentLifecycleTestGate::kMessage)
        {
            const bool writing = message.message == ProjectSaveTestGate::kMessage;
            const bool held =
                owner._activeHostRequest && std::optional(owner._activeHostRequest->ordinal) == state.ordinal &&
                !owner._selectionMoveCloseTicket && !owner._session.IsMovingSelection() &&
                owner._session.Document()->width == 40 && owner._session.Document()->height == 40 &&
                owner._session.Selection() ==
                    std::optional(NewCanvasOutlineSquare(state.save ? 15 : 10, state.save ? 17 : 10)) &&
                owner._session.history.UndoCount() == (state.save ? 2 : 1) &&
                owner._session.history.UndoName() == (state.save ? L"Move Selection" : L"Layer Opacity") &&
                owner._historyWork.Busy() && owner._jobs.ReservedMemoryBytes() != 0 &&
                state.receipt.Read().outcome == HostOutcome::Pending;
            if (writing)
            {
                state.saveVisited = true;
                state.saveHeld = held && owner._projectSaveTicket && !owner._documentLifecycleTicket &&
                                 owner._hostWriteInFlight && owner._session.history.IsModified();
            }
            else
            {
                state.lifecycleVisited = true;
                state.lifecycleHeld = held && owner._documentLifecycleTicket && !owner._projectSaveTicket &&
                                      (!state.save || !owner._session.history.IsModified());
            }
            if (state.younger)
                state.youngerHeld = state.youngerHeld && state.younger->Read().outcome == HostOutcome::Pending;
            if (state.mode == Mode::SaveCancel || state.mode == Mode::SavePublishedCancel ||
                state.mode == Mode::LifecycleCancel)
            {
                owner.CancelHostContinuations();
                state.cancelPending =
                    owner._activeHostRequest && state.receipt.Read().outcome == HostOutcome::Pending &&
                    (writing ? owner._projectSaveTicket.has_value() : owner._documentLifecycleTicket.has_value());
            }
            if (state.mode == Mode::SaveColor || state.mode == Mode::LifecycleColor ||
                state.mode == Mode::LifecycleOpacity || state.mode == Mode::LifecycleCrop)
            {
                if (state.mode == Mode::LifecycleCrop)
                {
                    owner._session.SetCropFrame(CropRect{0, 0, 1, 1});
                    state.mutationReached = owner._session.CropFrame().has_value();
                }
                else if (state.mode == Mode::LifecycleOpacity)
                {
                    state.mutationReached = owner.OpenOpacityEditForTest(0.5);
                }
                else
                {
                    owner._session.BeginColorEdit(FilterKind::GaussianBlur, true);
                    state.mutationReached = owner._session.HasColorEdit();
                }
                state.mutated = owner._session.Document();
                state.mutatedHistory = owner._session.history.CaptureState();
                state.mutatedGeneration = owner._session.Generation();
            }
            (void)SetEvent(writing ? state.writer->release.get() : state.lifecycle->release.get());
            return;
        }
        owner.DispatchQueuedMessage(message);
    };
    for (const auto& test : cases)
    {
        ++modes;
        if (!expect(test.name, "modified-live-source-fixture", prepare(test.kind)))
            return 5;
        _forcedDiscardAnswer = test.mode == Mode::PromptCancel ? Prompts::Answer::Cancel
                               : test.save                     ? Prompts::Answer::First
                                                               : Prompts::Answer::Second;
        State state{*this, test.mode, test.save, test.kind};
        state.before = _session.Document();
        state.history = _session.history.CaptureState();
        state.generation = _session.Generation();
        state.opening = _session.SelectionMoveOpening();
        state.output = (root / (std::to_wstring(modes) + L".prism")).wstring();
        _documentPath = state.output;
        state.close = std::make_shared<SelectionMoveCloseTestGate>();
        state.close->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        state.lifecycle = std::make_shared<DocumentLifecycleTestGate>();
        state.writer = std::make_shared<ProjectSaveTestGate>();
        if (!state.close->release || !state.lifecycle->release || !state.writer->release)
            return 5;
        state.close->throwCaptureFailure = test.mode == Mode::CloseCapture;
        if (test.mode == Mode::CloseHistory)
            state.close->throwHistoryStage = test.history;
        if (test.mode == Mode::CloseFailure)
            state.close->result = {JobStatus::Failed, E_ACCESSDENIED};
        state.lifecycle->throwCaptureFailure = test.mode == Mode::LifecycleCapture;
        state.lifecycle->throwPreparationFailure = test.mode == Mode::LifecycleFailure;
        state.lifecycle->throwAdmissionFailure = test.mode == Mode::LifecycleAdmission;
        if (test.mode == Mode::LifecycleHistory)
            state.lifecycle->historyFailure = test.history;
        state.lifecycle->afterPreparation = test.mode == Mode::LifecycleColor || test.mode == Mode::LifecycleOpacity ||
                                            test.mode == Mode::LifecycleCrop;
        state.writer->throwAllocationFailure = test.mode == Mode::SaveAllocation;
        state.writer->throwAdmissionFailure = test.mode == Mode::SaveAdmission;
        state.writer->afterPublication = test.mode == Mode::SavePublishedCancel;
        if (test.mode == Mode::SaveFailure)
            state.writer->result = {JobStatus::Failed, E_ACCESSDENIED};
        _selectionMoveCloseTestGate = state.close;
        _documentLifecycleTestGate = state.lifecycle;
        _projectSaveTestGate = state.writer;
        if (test.kind != SizeCardKind::NewCanvas)
        {
            state.sizingGuard = std::make_shared<SizingWorkTestGate>();
            if (!state.sizingGuard->release)
                return 5;
            state.sizingGuard->throwCaptureFailure = true;
            (void)SetEvent(
                state.sizingGuard->release.get()); // An unintended resize must fail without holding the pump.
            _sizingWorkTestGate = state.sizingGuard;
        }
        state.receipt = QueueHostInput(HostSizeApplyInput{_sizeDialog, _sizeInvocation});
        if (test.mode == Mode::OrderedDiscard || test.mode == Mode::OrderedSave)
            state.younger = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
        const bool selectedStale = test.mode == Mode::SelectedClosed || test.mode == Mode::SelectedOpening ||
                                   test.mode == Mode::SelectedCard || test.mode == Mode::SelectedColor ||
                                   test.mode == Mode::SelectedCrop || test.mode == Mode::SelectedOpacity;
        if (selectedStale)
            mutate(state, test.mode);
        if (!expect(test.name, "all-phases-settled", SettleJobs(_jobs, 15000, pump, &state) == SettleResult::Settled))
            return 5;
        const bool heldCloseStale = test.mode == Mode::HeldOpening || test.mode == Mode::HeldCard ||
                                    test.mode == Mode::HeldColor || test.mode == Mode::HeldOpacity ||
                                    test.mode == Mode::HeldCrop;
        const bool lifecycleStale = test.mode == Mode::LifecycleColor || test.mode == Mode::LifecycleOpacity ||
                                    test.mode == Mode::LifecycleCrop;
        const bool stale = selectedStale || heldCloseStale || lifecycleStale || test.mode == Mode::SaveColor;
        // PrepareEnd reuses the existing opening snapshot. Its first failure checkpoint is AfterSnapshot;
        // BeforeSnapshot belongs to preparation that creates an opening and must remain unvisited here.
        const bool endHistoryFailure =
            test.mode == Mode::CloseHistory && test.history != DocumentHistory::PrepareStage::BeforeSnapshot;
        const bool closeFailure =
            test.mode == Mode::CloseFailure || test.mode == Mode::CloseCapture || endHistoryFailure;
        const bool writerFailure =
            test.mode == Mode::SaveFailure || test.mode == Mode::SaveAllocation || test.mode == Mode::SaveAdmission;
        const bool lifecycleFailure = test.mode == Mode::LifecycleFailure || test.mode == Mode::LifecycleCapture ||
                                      test.mode == Mode::LifecycleAdmission || test.mode == Mode::LifecycleHistory;
        const bool cancelled = test.mode == Mode::PromptCancel || test.mode == Mode::CloseCancel ||
                               test.mode == Mode::LifecycleCancel || test.mode == Mode::SaveCancel ||
                               test.mode == Mode::SavePublishedCancel;
        const auto expected = stale                                               ? HostOutcome::Stale
                              : cancelled                                         ? HostOutcome::Cancelled
                              : closeFailure || writerFailure || lifecycleFailure ? HostOutcome::Failed
                                                                                  : HostOutcome::Succeeded;
        expect(test.name, "terminal-outcome", state.receipt.Read().outcome == expected);
        expect(test.name, "complete-owner-and-budget-retirement",
               !_historyWork.Busy() && !_selectionMoveCloseTicket && !_selectionMoveCloseStamp &&
                   !_documentLifecycleTicket && !_documentLifecycleStamp && !_projectSaveTicket && !_projectSaveStamp &&
                   !_sizingTicket && !_sizingStamp && !_activeHostRequest && !_hostCommitStamp && !_hostWriteInFlight &&
                   _hostRequests.Size() == 0 && _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0 &&
                   _hostInputBudget.Used() == 0);
        if (state.sizingGuard)
            expect(test.name, "new-edit-refuses-before-resize-admission-or-capture",
                   state.sizingGuard->reservationBytes == 0 && !state.sizingGuard->captureReservationHeld.load() &&
                       state.sizingGuard->visits.load() == 0 && !state.sizingGuard->payloadRetired.load());
        const bool reachedClose = !selectedStale && test.mode != Mode::PromptCancel;
        if (reachedClose)
        {
            expect(test.name, "close-reservation-and-off-owner-disposal",
                   state.close->reservationHeld.load() && state.close->retired.load() &&
                       state.close->retiredOffOwner.load());
            expect(test.name, "close-worker-boundary",
                   test.mode == Mode::CloseCapture ? !state.closeVisited && state.close->captures.load() == 0
                   : endHistoryFailure ? !state.closeVisited && state.close->captures.load() == 1 &&
                                             state.close->historyFaultHits.load() == 1
                                       : state.closeVisited && state.closeHeld && state.close->captures.load() == 1);
        }
        else
            expect(test.name, "refused-before-outline-capture", !state.closeVisited && !state.close->retired.load());
        const bool closeRefused = !reachedClose || closeFailure || test.mode == Mode::CloseCancel || heldCloseStale;
        if (closeRefused)
        {
            expect(test.name, "no-write-or-replacement-after-close-refusal",
                   !state.saveVisited && !state.lifecycleVisited && !state.lifecycle->retired.load() &&
                       GetFileAttributesW(state.output.c_str()) == INVALID_FILE_ATTRIBUTES);
            if (stale)
                expect(test.name, "new-authority-preserved",
                       state.mutationReached && _session.Document() == state.mutated &&
                           _session.history.IsCurrent(state.mutatedHistory) &&
                           _session.Generation() == state.mutatedGeneration);
            else
                expect(test.name, "live-outline-history-and-card-preserved",
                       _session.Document() == state.before && _session.history.IsCurrent(state.history) &&
                           _session.Generation() == state.generation &&
                           _session.SelectionMoveOpening() == state.opening && SizeCardOpen());
        }
        else
        {
            const bool writeRefused = writerFailure || test.mode == Mode::SaveCancel ||
                                      test.mode == Mode::SavePublishedCancel || test.mode == Mode::SaveColor;
            if (test.save)
                expect(test.name, "same-ordinal-save-after-end",
                       test.mode == Mode::SaveAdmission ? !state.saveVisited : state.saveVisited && state.saveHeld);
            else
                expect(test.name, "discard-never-writes", !state.saveVisited);
            if (!writeRefused)
            {
                expect(test.name, "same-ordinal-lifecycle-after-acknowledged-close-and-save",
                       test.mode == Mode::LifecycleCapture || test.mode == Mode::LifecycleAdmission
                           ? !state.lifecycleVisited
                           : state.lifecycleVisited && state.lifecycleHeld);
                expect(test.name, "lifecycle-capture-and-off-owner-disposal",
                       test.mode == Mode::LifecycleAdmission
                           ? state.lifecycle->retired.load() == 0
                           : state.lifecycle->captureReservationHeld.load() && state.lifecycle->retired.load() == 1 &&
                                 state.lifecycle->retiredOffOwner.load());
            }
            else
                expect(test.name, "no-replacement-after-write-refusal",
                       !state.lifecycleVisited && state.lifecycle->retired.load() == 0);
            if (expected == HostOutcome::Succeeded)
                expect(test.name, "literal-new-document-and-history",
                       _session.Document() && _session.Document()->width == 20 && _session.Document()->height == 30 &&
                           !_session.Selection() && !_session.IsMovingSelection() && !SizeCardOpen() &&
                           _session.history.UndoCount() == 1 && _session.history.UndoName() == L"New Canvas" &&
                           !_session.history.IsModified() && _documentPath.empty());
            else
                expect(test.name, "refusal-preserves-acknowledged-old-document",
                       _session.Document() && _session.Document()->width == 40 && _session.Document()->height == 40 &&
                           !_session.IsMovingSelection() && _session.history.UndoCount() == (test.save ? 2 : 1) &&
                           _session.Selection() ==
                               std::optional(NewCanvasOutlineSquare(test.save ? 15 : 10, test.save ? 17 : 10)));
            if (lifecycleStale || test.mode == Mode::SaveColor)
                expect(test.name, "new-edit-preserved-after-phase-refusal",
                       state.mutationReached && _session.Document() == state.mutated &&
                           _session.Generation() == state.mutatedGeneration &&
                           (test.mode == Mode::LifecycleOpacity ? _opacityEdit.has_value()
                            : test.mode == Mode::LifecycleCrop  ? _session.CropFrame().has_value()
                                                                : _session.HasColorEdit()));
        }
        if (test.mode == Mode::CloseHistory)
            expect(test.name,
                   endHistoryFailure ? "end-history-fault-reached-before-publication-gate"
                                     : "end-reuses-opening-without-before-snapshot-checkpoint",
                   state.close->historyFaultHits.load() == (endHistoryFailure ? 1u : 0u));
        if (test.mode == Mode::LifecycleHistory)
            expect(test.name, "lifecycle-history-fault-reached", state.lifecycle->historyFaultHits.load() == 1);
        if (cancelled && test.mode != Mode::PromptCancel)
            expect(test.name, "cancel-remains-pending-until-worker-ack", state.cancelPending);
        if (test.mode == Mode::PromptCancel)
            expect(test.name, "prompt-cancel-keeps-literal-pointer-draft",
                   std::holds_alternative<OutlineDrag>(_drag) && std::get<OutlineDrag>(_drag).start == Vec2{20, 20} &&
                       std::get<OutlineDrag>(_drag).offset == Vec2{5, 7});
        if (closeFailure || writerFailure || lifecycleFailure)
            expect(test.name, "exact-failure-error",
                   state.receipt.Read().error ==
                       static_cast<std::int32_t>(test.mode == Mode::CloseFailure || test.mode == Mode::SaveFailure
                                                     ? E_ACCESSDENIED
                                                     : E_OUTOFMEMORY));
        if (test.mode == Mode::Pump)
            expect(test.name, "view-and-posted-message-pump", state.zoomChanged && state.messagePumped);
        if (test.mode == Mode::CopiedDraft)
            expect(test.name, "event-time-dimensions-survive-reached-card-edit",
                   state.mutationReached && _session.Document()->width == 20 && _session.Document()->height == 30);
        if (test.mode == Mode::CopiedSavePath)
            expect(test.name, "selected-save-path-survives-reached-host-path-edit",
                   state.mutationReached &&
                       GetFileAttributesW((state.output + L".different.prism").c_str()) == INVALID_FILE_ATTRIBUTES);
        if (test.mode == Mode::HeldColor || test.mode == Mode::SelectedColor)
            expect(test.name, "new-color-opening-survives", _session.HasColorEdit());
        if (test.mode == Mode::HeldOpacity || test.mode == Mode::SelectedOpacity)
            expect(test.name, "new-opacity-opening-with-unchanged-generation-survives",
                   _opacityEdit.has_value() && state.mutatedGeneration == state.generation);
        if (test.mode == Mode::HeldCrop || test.mode == Mode::SelectedCrop || test.mode == Mode::LifecycleCrop)
            expect(test.name, "literal-new-crop-opening-survives",
                   _session.CropFrame() == std::optional(CropRect{0, 0, 1, 1}));
        const bool published = test.save && !closeRefused && !writerFailure && test.mode != Mode::SaveCancel;
        if (published)
        {
            const auto loaded = LoadProject(state.output.c_str());
            expect(test.name, "saved-file-is-original-40x40-before-replacement",
                   loaded.status == ProjectIoStatus::Ok && loaded.document.width == 40 &&
                       loaded.document.height == 40 && loaded.document.layers.size() == 1 &&
                       loaded.document.layers.front().opacity == 0.5 && !loaded.document.selection);
            if (test.mode == Mode::SavePublishedCancel || test.mode == Mode::SaveColor)
                expect(test.name, "published-save-marker-survives-replacement-refusal", !_session.history.IsModified());
        }
        else
            expect(test.name, "no-unacknowledged-output",
                   GetFileAttributesW(state.output.c_str()) == INVALID_FILE_ATTRIBUTES);
        if (state.younger)
            expect(test.name, "younger-old-lifetime-undo-waits-and-refuses",
                   state.youngerHeld && state.younger->Read().outcome == HostOutcome::Stale &&
                       _session.Document()->width == 20 && _session.history.UndoName() == L"New Canvas");
        _selectionMoveCloseTestGate.reset();
        _documentLifecycleTestGate.reset();
        _projectSaveTestGate.reset();
        _sizingWorkTestGate.reset();
        _session.CancelColorEdit();
        CloseOpacityEdit();
        if (_session.IsMovingSelection())
        {
            DropDrag<OutlineDrag>();
            _session.CancelSelectionMove();
        }
        CancelSizeCard();
    }
    for (const bool allocation : {false, true})
    {
        ++modes;
        const char* name = allocation ? "input-allocation" : "input-budget";
        if (!prepare())
            return 5;
        _forcedDiscardAnswer = Prompts::Answer::Second;
        const auto document = _session.Document();
        const auto history = _session.history.CaptureState();
        const auto generation = _session.Generation();
        std::optional<HostInputBudget::Reservation> reservation;
        if (allocation)
            _hostAdmissionAllocationFailureForTest = true;
        else
            reservation = _hostInputBudget.TryReserve(HostInputBudget::kStorageLimit);
        ApplySizeCard();
        _hostAdmissionAllocationFailureForTest = false;
        expect(name, "actual-apply-refuses-before-prompt-or-close",
               (allocation || reservation) && _session.Document() == document && _session.history.IsCurrent(history) &&
                   _session.Generation() == generation && !_activeHostRequest && _hostRequests.Size() == 0 &&
                   SizeCardOpen());
        reservation.reset();
        ApplySizeCard();
        if (!expect(name, "fresh-retry-settles",
                    SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled))
            return 5;
        expect(name, "fresh-retry-replaces",
               _session.Document()->width == 20 && !_session.IsMovingSelection() &&
                   _session.history.UndoName() == L"New Canvas" && _hostInputBudget.Used() == 0);
    }
    ++modes;
    if (!prepare())
        return 5;
    _forcedDiscardAnswer = Prompts::Answer::Second;
    const auto before = _session.Document();
    const auto history = _session.history.CaptureState();
    const auto generation = _session.Generation();
    const auto gate = std::make_shared<SelectionMoveCloseTestGate>();
    _selectionMoveCloseTestGate = gate;
    for (std::size_t record = 0; record < JobScheduler::kCapacity; ++record)
    {
        std::unique_ptr<Job> job = std::make_unique<OccupyNewCanvasOutlineSlot>();
        JobTicket ticket;
        if (FAILED(_jobs.Submit(job, {JobClass::Background}, ticket)))
            return 5;
    }
    const auto receipt = QueueHostInput(HostSizeApplyInput{_sizeDialog, _sizeInvocation});
    for (unsigned int pass = 0; pass < 4 && receipt.Read().outcome == HostOutcome::Pending; ++pass)
        (void)RunUiActions(_jobs);
    expect("scheduler-admission", "full-record-table-refuses-before-capture",
           receipt.Read().outcome == HostOutcome::Failed &&
               receipt.Read().error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
               _session.Document() == before && _session.Generation() == generation &&
               _session.history.IsCurrent(history) && gate->captures.load() == 0 && !gate->retired.load() &&
               !_historyWork.Busy() && SizeCardOpen());
    if (!expect("scheduler-admission", "occupied-records-settle",
                SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled))
        return 5;
    _selectionMoveCloseTestGate.reset();
    ApplySizeCard();
    if (!expect("scheduler-admission", "fresh-retry-settles",
                SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled))
        return 5;
    expect("scheduler-admission", "fresh-retry-replaces",
           _session.Document()->width == 20 && !_session.IsMovingSelection() &&
               _session.history.UndoName() == L"New Canvas" && _jobs.ReservedMemoryBytes() == 0 &&
               _hostInputBudget.Used() == 0);
    std::fprintf(stderr, "SUMMARY selection-newcanvas-fault assertions=%d failures=%d modes=%d\n", assertions, failures,
                 modes);
    return failures ? 5 : 0;
}
