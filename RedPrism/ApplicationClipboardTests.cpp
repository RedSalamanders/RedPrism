#include "Application.h"

#include "../Tests/ClipboardMergedBudgetTests.h"
#include "ChromeHost.h"
#include "ClipboardWorkTestGate.h"
#include "SystemClipboard.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
bool OpenClipboardFixture(HWND owner) noexcept
{
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return false;
    for (unsigned int attempt = 0; attempt < 30; ++attempt)
    {
        if (OpenClipboard(owner))
            return true;
        if (attempt == 29)
            return false;
        LARGE_INTEGER due{};
        due.QuadPart = -1'000'000;
        if (!SetWaitableTimer(timer.get(), &due, 0, nullptr, nullptr, FALSE) ||
            WaitForSingleObject(timer.get(), 1000) != WAIT_OBJECT_0)
            return false;
    }
    return false;
}
bool ClearClipboardFixture(HWND owner) noexcept
{
    if (!OpenClipboardFixture(owner))
        return false;
    const auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
    return EmptyClipboard() != FALSE;
}
bool InstallForeignClipboard(HWND owner, bool malformed = false) noexcept
{
    constexpr std::array<std::uint8_t, 44> literal{40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0,  0,  1,  0,  32,
                                                   0,  0, 0, 0, 0, 4, 0, 0, 0, 0, 0,  0,  0,  0,  0,
                                                   0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 17, 33, 65, 255};
    wil::unique_hglobal block(GlobalAlloc(GMEM_MOVEABLE, literal.size()));
    if (!block)
        return false;
    {
        const wil::unique_hglobal_locked locked(block.get());
        if (!locked.get())
            return false;
        std::memset(locked.get(), 0, GlobalSize(block.get()));
        std::memcpy(locked.get(), literal.data(), literal.size());
        if (malformed)
            static_cast<std::uint8_t*>(locked.get())[12] = 0; // invalid planes, still an offered image format
    }
    if (!OpenClipboardFixture(owner))
        return false;
    auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
    if (!EmptyClipboard() || !SetClipboardData(CF_DIB, block.get()))
        return false;
    (void)block.release();
    return true;
}
} // namespace

int Application::RunClipboardWorkSelfTest() noexcept
{
    if (RunClipboardMergedBudgetTests() != 0)
        return 5;
    int failures = 0;
    unsigned int checks = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* predicate) noexcept
    {
        ++checks;
        std::printf("%s clipboard-host.%s.%s\n", condition ? "PASS" : "FAIL", scenario, predicate);
        failures += condition ? 0 : 1;
    };
    SystemClipboard::SavedContents saved;
    if (!SystemClipboard::SaveContents(_window.get(), saved))
    {
        const DWORD error = GetLastError();
        DWORD clipboardProcess = 0;
        const HWND clipboardOwner = GetOpenClipboardWindow();
        if (clipboardOwner)
            (void)GetWindowThreadProcessId(clipboardOwner, &clipboardProcess);
        std::printf("FAIL clipboard-host.save-user-clipboard error=%lu window-valid=%d open-owner=%p "
                    "open-process=%lu sequence=%lu\n",
                    error, IsWindow(_window.get()) != FALSE, static_cast<void*>(clipboardOwner), clipboardProcess,
                    GetClipboardSequenceNumber());
        return 5;
    }
    auto restore = wil::scope_exit(
        [&]() noexcept
        {
            _clipboardWorkTestGate.reset();
            (void)SystemClipboard::RestoreContents(_window.get(), saved);
        });
    enum class Mode
    {
        Success,
        Cancel,
        LateCancel,
        Failure,
        WorkerAllocation,
        Admission,
        MainAdmission,
        Capture,
        Generation,
        Serial,
        History,
        Foreign,
        ForeignNewCopy,
        NoFormats,
        Empty,
        Malformed,
        Retry,
        Exhaust,
        EncodeFailure,
        ReceiptFailure,
        YoungerUndo,
        ReceiptOrigin,
        Rebind,
        Replacement,
    };
    struct Scenario
    {
        const char* name;
        int command;
        Mode mode;
        ClipboardWorkPhase phase;
        HostOutcome outcome;
    };
    constexpr int copy = ChromeHost::kCommandCopy, cut = ChromeHost::kCommandCut, paste = ChromeHost::kCommandPaste;
    constexpr int merged = ChromeHost::kCommandCopyMerged;
    constexpr std::array scenarios{
        Scenario{"copy", copy, Mode::Success, ClipboardWorkPhase::CopyPrepared, HostOutcome::Succeeded},
        Scenario{"cut", cut, Mode::Success, ClipboardWorkPhase::CopyPrepared, HostOutcome::Succeeded},
        Scenario{"copy-cancel", copy, Mode::Cancel, ClipboardWorkPhase::CopyPrepared, HostOutcome::Cancelled},
        Scenario{"cut-cancel", cut, Mode::Cancel, ClipboardWorkPhase::CopyPrepared, HostOutcome::Cancelled},
        Scenario{"copy-late-cancel", copy, Mode::LateCancel, ClipboardWorkPhase::CopyPrivateApplied,
                 HostOutcome::Succeeded},
        Scenario{"cut-late-cancel", cut, Mode::LateCancel, ClipboardWorkPhase::CopyPrivateApplied,
                 HostOutcome::Succeeded},
        Scenario{"copy-worker-failure", copy, Mode::Failure, ClipboardWorkPhase::CopyPrepared, HostOutcome::Failed},
        Scenario{"cut-worker-allocation", cut, Mode::WorkerAllocation, ClipboardWorkPhase::CopyPrepared,
                 HostOutcome::Failed},
        Scenario{"copy-admission", copy, Mode::Admission, ClipboardWorkPhase::CopyPrepared, HostOutcome::Failed},
        Scenario{"cut-capture", cut, Mode::Capture, ClipboardWorkPhase::CopyPrepared, HostOutcome::Failed},
        Scenario{"copy-stale", copy, Mode::Generation, ClipboardWorkPhase::CopyPrepared, HostOutcome::Stale},
        Scenario{"cut-stale", cut, Mode::Generation, ClipboardWorkPhase::CopyPrepared, HostOutcome::Stale},
        Scenario{"copy-retry", copy, Mode::Retry, ClipboardWorkPhase::CopyEncoded, HostOutcome::Succeeded},
        Scenario{"copy-exhaust", copy, Mode::Exhaust, ClipboardWorkPhase::CopyEncoded, HostOutcome::Succeeded},
        Scenario{"cut-encode-failure", cut, Mode::EncodeFailure, ClipboardWorkPhase::CopyPrivateApplied,
                 HostOutcome::Succeeded},
        Scenario{"copy-receipt-failure", copy, Mode::ReceiptFailure, ClipboardWorkPhase::CopyEncoded,
                 HostOutcome::Succeeded},
        Scenario{"cut-younger-undo", cut, Mode::YoungerUndo, ClipboardWorkPhase::CopyPrepared, HostOutcome::Succeeded},
        Scenario{"paste-private", paste, Mode::Success, ClipboardWorkPhase::PastePrepared, HostOutcome::Succeeded},
        Scenario{"paste-foreign", paste, Mode::Foreign, ClipboardWorkPhase::PastePrepared, HostOutcome::Succeeded},
        Scenario{"paste-foreign-new-private", paste, Mode::ForeignNewCopy, ClipboardWorkPhase::PasteCopied,
                 HostOutcome::Succeeded},
        Scenario{"paste-cancel", paste, Mode::Cancel, ClipboardWorkPhase::PasteCaptured, HostOutcome::Cancelled},
        Scenario{"paste-late-cancel", paste, Mode::Cancel, ClipboardWorkPhase::PastePrepared, HostOutcome::Cancelled},
        Scenario{"paste-foreign-cancel", paste, Mode::LateCancel, ClipboardWorkPhase::PasteCopied,
                 HostOutcome::Cancelled},
        Scenario{"paste-worker-allocation", paste, Mode::WorkerAllocation, ClipboardWorkPhase::PastePrepared,
                 HostOutcome::Failed},
        Scenario{"paste-admission", paste, Mode::Admission, ClipboardWorkPhase::PasteProbe, HostOutcome::Failed},
        Scenario{"paste-main-admission", paste, Mode::MainAdmission, ClipboardWorkPhase::PasteInspected,
                 HostOutcome::Failed},
        Scenario{"paste-capture", paste, Mode::Capture, ClipboardWorkPhase::PasteCaptured, HostOutcome::Failed},
        Scenario{"paste-stale-generation", paste, Mode::Generation, ClipboardWorkPhase::PastePrepared,
                 HostOutcome::Stale},
        Scenario{"paste-stale-private", paste, Mode::Serial, ClipboardWorkPhase::PastePrepared, HostOutcome::Stale},
        Scenario{"paste-stale-history", paste, Mode::History, ClipboardWorkPhase::PastePrepared, HostOutcome::Stale},
        Scenario{"paste-no-formats", paste, Mode::NoFormats, ClipboardWorkPhase::PastePrepared, HostOutcome::Succeeded},
        Scenario{"paste-empty", paste, Mode::Empty, ClipboardWorkPhase::PasteProbe, HostOutcome::Succeeded},
        Scenario{"paste-malformed", paste, Mode::Malformed, ClipboardWorkPhase::PasteProbe, HostOutcome::Failed},
        Scenario{"paste-retry", paste, Mode::Retry, ClipboardWorkPhase::PasteProbe, HostOutcome::Succeeded},
        Scenario{"paste-exhaust", paste, Mode::Exhaust, ClipboardWorkPhase::PasteProbe, HostOutcome::Failed},
        Scenario{"paste-owned-receipt", paste, Mode::ReceiptOrigin, ClipboardWorkPhase::PastePrepared,
                 HostOutcome::Succeeded},
        Scenario{"merged", merged, Mode::Success, ClipboardWorkPhase::CopyPrepared, HostOutcome::Succeeded},
        Scenario{"merged-cancel", merged, Mode::Cancel, ClipboardWorkPhase::MergedSnapshot, HostOutcome::Cancelled},
        Scenario{"merged-late-cancel", merged, Mode::LateCancel, ClipboardWorkPhase::CopyPrivateApplied,
                 HostOutcome::Succeeded},
        Scenario{"merged-failure", merged, Mode::Failure, ClipboardWorkPhase::CopyPrepared, HostOutcome::Failed},
        Scenario{"merged-allocation", merged, Mode::WorkerAllocation, ClipboardWorkPhase::CopyPrepared,
                 HostOutcome::Failed},
        Scenario{"merged-admission", merged, Mode::Admission, ClipboardWorkPhase::CopyPrepared, HostOutcome::Failed},
        Scenario{"merged-capture", merged, Mode::Capture, ClipboardWorkPhase::CopyPrepared, HostOutcome::Failed},
        Scenario{"merged-stale-opening", merged, Mode::Generation, ClipboardWorkPhase::MergedSnapshot,
                 HostOutcome::Stale},
        Scenario{"merged-stale-publication", merged, Mode::Generation, ClipboardWorkPhase::CopyPrepared,
                 HostOutcome::Stale},
        Scenario{"merged-device-before-copy", merged, Mode::Rebind, ClipboardWorkPhase::MergedSnapshot,
                 HostOutcome::Succeeded},
        Scenario{"merged-device-after-copy", merged, Mode::Rebind, ClipboardWorkPhase::CopyEncoded,
                 HostOutcome::Succeeded},
        Scenario{"merged-retry", merged, Mode::Retry, ClipboardWorkPhase::CopyEncoded, HostOutcome::Succeeded},
        Scenario{"merged-exhaust", merged, Mode::Exhaust, ClipboardWorkPhase::CopyEncoded, HostOutcome::Succeeded},
        Scenario{"copy-replacement", copy, Mode::Replacement, ClipboardWorkPhase::CopyPrepared, HostOutcome::Stale},
        Scenario{"paste-replacement", paste, Mode::Replacement, ClipboardWorkPhase::PastePrepared, HostOutcome::Stale},
        Scenario{"merged-replacement", merged, Mode::Replacement, ClipboardWorkPhase::MergedSnapshot,
                 HostOutcome::Stale},
    };
    struct State
    {
        Application& owner;
        const Scenario& scenario;
        std::shared_ptr<ClipboardWorkTestGate> gate;
        ContinuationReceipt receipt, younger;
        Guid replacementRevision;
        bool visited = false, held = false, cancelAcknowledged = false;
    };
    const auto dispatch = [](void* raw, void* native) noexcept
    {
        auto& state = *static_cast<State*>(raw);
        auto& message = *static_cast<MSG*>(native);
        auto& owner = state.owner;
        if (message.message != ClipboardWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        owner.OnCommand(ChromeHost::kCommandZoomIn); // an independent owner action dispatches during the held job
        const auto reserved = owner._jobs.ReservedMemoryBytes();
        state.held = owner._clipboardTicket && owner._historyWork.Busy() &&
                     state.receipt.Read().outcome == HostOutcome::Pending && reserved != 0 &&
                     (state.gate->reservationBytes
                          ? state.gate->captureReservationHeld && reserved >= state.gate->reservationBytes
                          : reserved >= state.gate->probeReservationBytes);
        switch (state.scenario.mode)
        {
        case Mode::Cancel:
        case Mode::LateCancel:
            owner.CancelHostContinuations();
            state.cancelAcknowledged = owner._clipboardTicket && owner._historyWork.Busy() &&
                                       state.receipt.Read().outcome == HostOutcome::Pending;
            break;
        case Mode::Generation:
            owner._session.EditForTest([](CanvasDocument&) {});
            break;
        case Mode::Serial:
        case Mode::ForeignNewCopy:
            (void)owner._session.CopySelection();
            break;
        case Mode::History:
            owner._session.history.Begin(L"Epoch only", owner._session.Document(), owner._session.ActiveLayerID());
            owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
            break;
        case Mode::Rebind:
            state.held = state.held && SUCCEEDED(owner._renderer.RecoverDeviceForTest());
            owner.BindCompositionDevice();
            break;
        case Mode::Replacement:
            owner._session.ReplaceDocument(owner._session.Document().value(), owner._session.ActiveLayerID());
            state.replacementRevision = owner._session.history.CurrentRevision();
            break;
        case Mode::YoungerUndo:
            state.younger = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
            state.cancelAcknowledged =
                state.younger.Read().outcome == HostOutcome::Pending && owner._session.history.UndoCount() == 0;
            break;
        default:
            break;
        }
        (void)SetEvent(state.gate->release.get());
    };
    for (const auto& scenario : scenarios)
    {
        _clipboardWorkTestGate.reset();
        CanvasDocument document;
        document.id = Guid::Generate();
        document.width = 9;
        document.height = 7;
        constexpr std::array<std::uint8_t, 12> bytes{11, 22, 33, 255, 20, 40, 80, 255, 99, 100, 101, 255};
        ImageLayer layer = ImageLayer::Blank(L"Clipboard source", Vec2{3, 1});
        layer.asset =
            RasterAsset{Raster::Make(3, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size()), {}, layer.name};
        const auto source = layer.asset->image;
        const auto active = layer.id;
        document.layers.push_back(std::move(layer));
        if (scenario.mode == Mode::Empty)
        {
            _session = DocumentSession{}; // independent owner without a previous private copy
            _session.SetBrushEngine(_brushEngine.Ready() ? &_brushEngine : nullptr);
        }
        _session.ReplaceDocument(std::move(document), active);
        _session.SetSelection(
            DocumentSelection{false, SelectionLoops{{Vec2{1, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{1, 1}}}}, L"Fixture");
        if (scenario.mode != Mode::Empty && !_session.CopySelection())
        {
            std::printf("FAIL clipboard-host.%s.fixture-private-copy\n", scenario.name);
            return 5;
        }
        _session.history.Reset();
        _session.history.MarkSaved();
        _clipboardBinding.reset();
        const bool foreign =
            scenario.mode == Mode::Foreign || scenario.mode == Mode::ForeignNewCopy ||
            (scenario.command == paste && (scenario.mode == Mode::Retry || scenario.mode == Mode::Exhaust ||
                                           scenario.mode == Mode::LateCancel || scenario.mode == Mode::Malformed));
        if (!(foreign ? InstallForeignClipboard(_window.get(), scenario.mode == Mode::Malformed)
                      : ClearClipboardFixture(_window.get())))
        {
            std::printf("FAIL clipboard-host.%s.fixture-clipboard-install error=%lu\n", scenario.name, GetLastError());
            return 5;
        }
        _clipboardSequence = foreign || scenario.mode == Mode::NoFormats || scenario.mode == Mode::Empty
                                 ? 0
                                 : SystemClipboard::SequenceNumber();
        DocumentChanged();
        if (scenario.mode == Mode::ReceiptOrigin)
        {
            const auto copied = QueueHostInput(HostCommandInput{copy});
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled ||
                copied.Read().outcome != HostOutcome::Succeeded || !_clipboardBinding)
                return 5;
            _clipboardSequence = 0; // force owned receipt/payload proof after immediate-owner window destruction
        }
        const auto serial = _session.ClipboardSerial();
        const auto revision = _session.history.CurrentRevision();
        _clipboardWorkTestGate = std::make_shared<ClipboardWorkTestGate>();
        auto gate = _clipboardWorkTestGate;
        if (!gate->release)
            return 5;
        gate->phase = scenario.phase;
        gate->throwAdmissionFailure = scenario.mode == Mode::Admission;
        gate->throwMainAdmissionFailure = scenario.mode == Mode::MainAdmission;
        gate->throwCaptureFailure = scenario.mode == Mode::Capture;
        gate->throwWorkerFailure = scenario.mode == Mode::WorkerAllocation;
        if (scenario.mode == Mode::Failure)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (scenario.mode == Mode::Retry || scenario.mode == Mode::Exhaust)
            gate->openFailures.store(scenario.mode == Mode::Retry ? 2u : 6u);
        if (scenario.mode == Mode::EncodeFailure)
            gate->fault = ClipboardTransfer::Fault::PngGlobal;
        if (scenario.mode == Mode::ReceiptFailure)
            gate->fault = ClipboardTransfer::Fault::ReceiptPlace;
        State state{*this, scenario, gate, QueueHostInput(HostCommandInput{scenario.command})};
        const auto settled = SettleJobs(_jobs, 15000, dispatch, &state);
        if (settled != SettleResult::Settled)
        {
            SelfTestFailure(L"Clipboard host work did not acknowledge within the bounded wait.\n");
            std::_Exit(5);
        }
        expect(settled == SettleResult::Settled && state.receipt.Read().outcome == scenario.outcome, scenario.name,
               "receipt");
        const bool noGate = scenario.mode == Mode::Admission || scenario.mode == Mode::Capture;
        expect(noGate ? !state.visited : state.visited && state.held, scenario.name, "admitted-pump");
        expect(!_clipboardTicket && !_clipboardStamp && !_historyWork.Busy() && !_activeHostRequest &&
                   _hostRequests.Size() == 0 && !_hostInputBudget.Used() && _jobs.ReservedMemoryBytes() == 0 &&
                   _jobs.ReleasingMemoryBytes() == 0 && gate->retiredOwnerNodes.load() == 0,
               scenario.name, "acknowledged-retirement");
        if (scenario.mode == Mode::Cancel || scenario.mode == Mode::LateCancel)
            expect(state.cancelAcknowledged, scenario.name, "cancel-retains-barrier-until-ack");
        const bool succeeded = scenario.outcome == HostOutcome::Succeeded;
        if (scenario.command != paste)
        {
            expect(_session.ClipboardSerial() == serial + (succeeded ? 1 : 0), scenario.name, "private-serial");
            if (scenario.command == cut && succeeded && scenario.mode != Mode::YoungerUndo)
            {
                std::array<std::uint8_t, 4> pixel{};
                _session.ActiveLayer()->asset->image.ReadRegion(1, 0, 1, 1, pixel.data(), 4, 0);
                expect(pixel == std::array<std::uint8_t, 4>{0, 0, 0, 0} && _session.history.UndoName() == L"Clear",
                       scenario.name, "atomic-clear-and-history");
            }
            else
                expect(_session.ActiveLayer() && _session.ActiveLayer()->asset &&
                           _session.ActiveLayer()->asset->image == source && _session.history.UndoCount() == 0,
                       scenario.name, "unchanged-source-history");
            if (scenario.mode == Mode::Retry || scenario.mode == Mode::Exhaust)
                expect(gate->forcedBusyAttempts.load() == (scenario.mode == Mode::Retry ? 2u : 6u) &&
                           gate->openAttempts.load() >= (scenario.mode == Mode::Retry ? 3u : 6u) &&
                           gate->openAttempts.load() <= 6 &&
                           gate->emptyAttempts.load() == (scenario.mode == Mode::Retry ? 1u : 0u) &&
                           _clipboardWriteSucceeded == (scenario.mode == Mode::Retry),
                       scenario.name, "bounded-write-attempts");
            if (scenario.mode == Mode::LateCancel || scenario.mode == Mode::EncodeFailure ||
                scenario.mode == Mode::Exhaust)
                expect(!gate->emptyAttempts.load() && !_clipboardWriteSucceeded, scenario.name, "no-os-mutation");
            if (scenario.mode == Mode::YoungerUndo)
                expect(state.cancelAcknowledged && state.younger.Read().outcome == HostOutcome::Succeeded,
                       scenario.name, "younger-undo-after-clear");
            if (scenario.command == merged && succeeded)
                expect(gate->detachedCaptures == 1 && gate->utilityReleasedBeforeEncoding &&
                           _gpuWork.LentCount() == 0 && _gpuWork.InFlightCount() == 0 && !_gpuWork.HasPending() &&
                           _composition.UtilitySurfaceBytes() == 0 && _session.Clipboard() &&
                           _session.Clipboard()->origin == Vec2{1, 0} && _session.Clipboard()->image.Width() == 1 &&
                           _session.Clipboard()->image.Height() == 1 && _session.Clipboard()->image.Data()[0] == 20 &&
                           _session.Clipboard()->image.Data()[1] == 40 && _session.Clipboard()->image.Data()[2] == 80 &&
                           _session.Clipboard()->image.Data()[3] == 255,
                       scenario.name, "one-detached-readback-selected-literal-pixels");
        }
        else if (succeeded && scenario.mode != Mode::Empty)
        {
            const auto* pasted = _session.ActiveLayer();
            std::array<std::uint8_t, 4> pixel{};
            if (pasted && pasted->asset)
                pasted->asset->image.ReadRegion(0, 0, 1, 1, pixel.data(), 4, 0);
            expect(pasted && pasted->asset && _session.Document()->layers.size() == 2 && !_session.Selection() &&
                       _session.history.UndoName() == L"Paste" &&
                       pasted->transform.origin == (foreign ? Vec2{4, 3} : Vec2{1, 0}) &&
                       pixel == (foreign ? std::array<std::uint8_t, 4>{17, 33, 65, 255}
                                         : std::array<std::uint8_t, 4>{20, 40, 80, 255}),
                   scenario.name, "complete-paste-origin-pixels-history");
        }
        else
            expect(_session.Document()->layers.size() == 1 && _session.ActiveLayer() && _session.ActiveLayer()->asset &&
                       _session.ActiveLayer()->asset->image == source &&
                       _session.history.CurrentRevision() ==
                           (scenario.mode == Mode::Replacement ? state.replacementRevision : revision) &&
                       _session.history.UndoCount() == 0,
                   scenario.name, "refusal-preserves-source-history");
        if (scenario.command == paste && (scenario.mode == Mode::Retry || scenario.mode == Mode::Exhaust))
            expect(gate->forcedBusyAttempts.load() == (scenario.mode == Mode::Retry ? 2u : 6u) &&
                       gate->openAttempts.load() >= (scenario.mode == Mode::Retry ? 4u : 6u) &&
                       gate->openAttempts.load() <= 6,
                   scenario.name, "six-total-inspect-copy-attempts");
    }
    std::printf("clipboard-host summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}
