#include "Application.h"

#include "../Tests/ClipboardMergedBudgetTests.h"
#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationShutdownTestProbe.h"
#include "ChromeHost.h"
#include "ClipboardWorkTestGate.h"
#include "DeferredRelease.h"
#include "SystemClipboard.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
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
        UnavailableSequence,
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
        Scenario{"paste-unavailable-sequence", paste, Mode::UnavailableSequence, ClipboardWorkPhase::PasteProbe,
                 HostOutcome::Succeeded},
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
        DocumentFixture document;
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
            _session = DocumentSession{};              // independent owner without a previous private copy
            _session.SetColorDisplayJobsEnabled(true); // Keep the Application's publication policy on this owner.
            _session.SetBrushEngine(_brushEngine.Ready() ? &_brushEngine : nullptr);
        }
        _session.ReplaceDocument(std::move(document).Seal(), active);
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
        const bool retry = scenario.mode == Mode::Retry || scenario.mode == Mode::UnavailableSequence;
        const bool foreign =
            scenario.mode == Mode::Foreign || scenario.mode == Mode::ForeignNewCopy ||
            (scenario.command == paste && (retry || scenario.mode == Mode::Exhaust ||
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
        gate->forceUnavailableProbeSequence = scenario.mode == Mode::UnavailableSequence;
        if (scenario.mode == Mode::Failure)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (retry || scenario.mode == Mode::Exhaust)
            gate->openFailures.store(retry ? 2u : 6u);
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
        const bool retainedMerged = scenario.command == merged && scenario.outcome == HostOutcome::Succeeded;
        const auto retainedBytes = retainedMerged ? gate->reservationBytes : 0;
        expect(!_clipboardTicket && !_clipboardStamp && !_historyWork.Busy() && !_activeHostRequest &&
                   _hostRequests.Size() == 0 && !_hostInputBudget.Used() &&
                   _jobs.ReservedMemoryBytes() == retainedBytes && (!retainedMerged || retainedBytes != 0) &&
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
            if (retry || scenario.mode == Mode::Exhaust)
                expect(gate->forcedBusyAttempts.load() == (retry ? 2u : 6u) &&
                           gate->openAttempts.load() >= (retry ? 3u : 6u) && gate->openAttempts.load() <= 6 &&
                           gate->emptyAttempts.load() == (retry ? 1u : 0u) && _clipboardWriteSucceeded == retry,
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
            if (scenario.mode == Mode::Retry)
                std::printf("DIAG clipboard-paste retry forced=%u opens=%u layers=%zu selection=%d undo=%ls "
                            "origin=%.3f,%.3f pixels=%u,%u,%u,%u\n",
                            gate->forcedBusyAttempts.load(), gate->openAttempts.load(),
                            _session.Document()->layers.size(), _session.Selection().has_value(),
                            _session.history.UndoName().c_str(), pasted ? pasted->transform.origin.x : -1.0,
                            pasted ? pasted->transform.origin.y : -1.0, pixel[0], pixel[1], pixel[2], pixel[3]);
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
        if (scenario.mode == Mode::UnavailableSequence)
            expect(_clipboardSequence == 0 && gate->forceUnavailableProbeSequence &&
                       gate->forcedBusyAttempts.load() == 2 && gate->openAttempts.load() >= 4 &&
                       gate->openAttempts.load() <= 6,
                   scenario.name, "unavailable-query-inspects-foreign-pixels-under-original-retry-budget");
        if (scenario.command == paste && (retry || scenario.mode == Mode::Exhaust))
            expect(gate->forcedBusyAttempts.load() == (retry ? 2u : 6u) &&
                       gate->openAttempts.load() >= (retry ? 4u : 6u) && gate->openAttempts.load() <= 6,
                   scenario.name, "six-total-inspect-copy-attempts");
        if (retainedMerged)
        {
            // Private success is already authoritative, including late Cancel/exhausted OS publication.
            // Test final readers only after every unchanged private/OS receipt and literal predicate above.
            Raster reader = _session.Clipboard() ? _session.Clipboard()->image : Raster{};
            auto weak = reader.WeakStorageForTest();
            const auto literal = [&]() noexcept
            {
                constexpr std::array<std::uint8_t, 4> expected{20, 40, 80, 255};
                return reader.HasStorage() && reader.Width() == 1 && reader.Height() == 1 && reader.Stride() == 4 &&
                       reader.Data() && std::equal(expected.begin(), expected.end(), reader.Data());
            };
            const auto lifetime = [&](bool condition, const char* stage) noexcept
            {
                std::printf("%s clipboard-merged-lifetime.%s.%s\n", condition ? "PASS" : "FAIL", scenario.name, stage);
                failures += condition ? 0 : 1;
            };
            lifetime(literal() && _jobs.ReservedMemoryBytes() == retainedBytes && _jobs.ReleasingMemoryBytes() == 0,
                     "private-copy-retains-original-terminal-charge");
            _composition.ReleaseDocumentSurfaces();
            _session.ClearDocument(); // Closing preserves the private clipboard for later Paste.
            DocumentFixture replacement;
            replacement.id = Guid::Generate();
            replacement.width = replacement.height = 1;
            ImageLayer replacementLayer = ImageLayer::Blank(L"Replacement private copy", {1, 1});
            constexpr std::array<std::uint8_t, 4> replacementPixel{7, 8, 9, 255};
            replacementLayer.asset = RasterAsset{
                Raster::Make(1, 1, RasterFormat::Bgra8888, replacementPixel.data(), replacementPixel.size()),
                {},
                replacementLayer.name};
            const auto replacementActive = replacementLayer.id;
            replacement.layers.push_back(std::move(replacementLayer));
            _session.ReplaceDocument(std::move(replacement).Seal(), replacementActive);
            const bool replacedPrivate =
                _session.CopySelection(); // Real private-copy replacement drops the old reader.
            _session.ClearDocument();     // Release the replacement's live/history owners too.
            DocumentChanged();
            const bool released = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
            lifetime(released && replacedPrivate && !_session.Document() && _session.history.UndoCount() == 0 &&
                         !_session.history.CanRedo() && _session.Clipboard() && _session.Clipboard()->image != reader &&
                         literal() && _jobs.ReservedMemoryBytes() == retainedBytes && _jobs.ReleasingMemoryBytes() == 0,
                     "copied-reader-survives-private-clipboard-and-document-release");
            reader = {};
            const bool storageReleased = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
            lifetime(storageReleased && weak.expired() && _jobs.ReservedMemoryBytes() == retainedBytes &&
                         _jobs.ReleasingMemoryBytes() == 0,
                     "last-weak-storage-control-retains-original-charge");
            weak.reset();
            const bool acknowledged = SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled;
            lifetime(acknowledged && _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0 &&
                         _jobs.IsIdle(),
                     "final-control-acknowledges-original-refund");
        }
    }
    std::printf("clipboard-host summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}

int Application::RunStandaloneClipboardMergedShutdownSelfTest(HINSTANCE instance) noexcept
{
    unsigned checks = 0;
    unsigned failures = 0;
    for (const bool privateApplied : std::array{false, true})
    {
        const char* scenario = privateApplied ? "private-applied-before-encoding" : "source-snapshot-before-graphics";
        const auto expect = [&](bool condition, const char* label) noexcept
        {
            ++checks;
            failures += condition ? 0u : 1u;
            std::printf("%s clipboard-merged-close.%s.%s\n", condition ? "PASS" : "FAIL", scenario, label);
        };
        expect(!DeferredReleaseDetail::sink.enqueue, "no-outer-sink");
        auto gate = std::make_shared<ClipboardWorkTestGate>();
        gate->phase = privateApplied ? ClipboardWorkPhase::CopyPrivateApplied : ClipboardWorkPhase::MergedSnapshot;
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                    owner->Shutdown();
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!gate->release || FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
            return 5;
        owner->BindCompositionDevice();
        constexpr std::array<std::uint8_t, 12> source{11, 22, 33, 255, 20, 40, 80, 255, 99, 100, 101, 255};
        DocumentFixture fixture;
        fixture.id = Guid::Generate();
        fixture.width = 9;
        fixture.height = 7;
        ImageLayer layer = ImageLayer::Blank(L"Joined Copy Merged literal", {3, 1});
        const Guid active = layer.id;
        layer.asset =
            RasterAsset{Raster::Make(3, 1, RasterFormat::Bgra8888, source.data(), source.size()), {}, layer.name};
        fixture.layers.push_back(std::move(layer));
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
        owner->_session.SetSelection(
            DocumentSelection{false, SelectionLoops{{Vec2{1, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{1, 1}}}}, L"Fixture");
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->DocumentChanged();
        const auto root = owner->_session.Document()->layers.Identity();
        const auto generation = owner->_session.Generation();
        const auto revision = owner->_session.history.CurrentRevision();
        const auto serial = owner->_session.ClipboardSerial();
        owner->_clipboardWorkTestGate = gate;
        const auto receipt = owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandCopyMerged});
        struct State final
        {
            Application& owner;
            bool reached = false;
        } state{*owner};
        const auto dispatch = [](void* opaque, void* native) noexcept
        {
            auto& value = *static_cast<State*>(opaque);
            auto& message = *static_cast<MSG*>(native);
            if (message.message == ClipboardWorkTestGate::kMessage)
                value.reached = true;
            else
                value.owner.DispatchQueuedMessage(message);
        };
        const auto deadline = GetTickCount64() + 10000;
        while (!state.reached)
        {
            if (PumpMessages(dispatch, &state).quit)
                std::_Exit(5);
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (state.reached)
                break;
            const auto now = GetTickCount64();
            if (now >= deadline)
                std::_Exit(5);
            if (owner->_jobs.HasUiWork())
                continue;
            const HANDLE wake = owner->_jobs.WakeHandle();
            if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                std::_Exit(5);
        }
        expect(receipt.Read().outcome == HostOutcome::Pending && owner->_clipboardTicket &&
                   owner->_historyWork.Busy() && gate->captureReservationHeld && gate->reservationBytes != 0 &&
                   owner->_jobs.ReservedMemoryBytes() == gate->reservationBytes && gate->visits.load() == 1,
               "actual-original-ticket-and-full-admission-held");
        struct Observation final
        {
            Application& owner;
            const void* root;
            std::uint64_t generation;
            Guid revision;
            std::uint64_t serial;
            bool privateApplied;
            std::uint64_t bytes;
            bool exactSource = false;
            bool exactPrivate = false;
            bool charged = false;
        } observed{*owner, root, generation, revision, serial, privateApplied, gate->reservationBytes};
        ApplicationShutdownTestProbe probe;
        probe.checkContext = &observed;
        probe.checkOwner = [](void* opaque) noexcept
        {
            auto& value = *static_cast<Observation*>(opaque);
            value.exactSource = value.owner._session.Document() &&
                                value.owner._session.Document()->layers.Identity() == value.root &&
                                value.owner._session.Generation() == value.generation &&
                                value.owner._session.history.CurrentRevision() == value.revision &&
                                value.owner._session.history.UndoCount() == 0;
            const auto& copied = value.owner._session.Clipboard();
            constexpr std::array<std::uint8_t, 4> literal{20, 40, 80, 255};
            value.exactPrivate = value.privateApplied
                                     ? copied && copied->origin == Vec2{1, 0} && copied->image.Width() == 1 &&
                                           copied->image.Height() == 1 && copied->image.Data() &&
                                           std::equal(literal.begin(), literal.end(), copied->image.Data()) &&
                                           value.owner._session.ClipboardSerial() == value.serial + 1
                                     : !copied && value.owner._session.ClipboardSerial() == value.serial;
            value.charged = value.owner._jobs.ReservedMemoryBytes() == value.bytes && value.bytes != 0;
        };
        owner->_shutdownTestProbe = &probe;
        owner->Shutdown(); // Actual joined Close; never signal the worker's test gate or replay an encoder.
        expect(receipt.Read().outcome == HostOutcome::Cancelled && observed.exactSource && observed.exactPrivate &&
                   observed.charged && probe.graphicsDetachedOnOwner && probe.reservationAndWakeAlive,
               "joined-close-preserves-completed-private-truth-and-original-charge-at-detach");
        expect(gate->openAttempts.load() == 0 && gate->emptyAttempts.load() == 0 && gate->publications.load() == 0 &&
                   gate->receiptPlacements.load() == 0 && gate->pngPlacements.load() == 0 &&
                   gate->dibPlacements.load() == 0,
               "shutdown-does-not-enter-or-mutate-the-os-clipboard");
        expect(gate->retiredNodes.load() == 1 && gate->retiredOwnerNodes.load() == 0 &&
                   probe.hostCpuRootsRetired.load() && owner->_jobs.ReservedMemoryBytes() == 0 &&
                   owner->_jobs.ReleasingMemoryBytes() == 0 && owner->_jobs.IsIdle() && !owner->_clipboardTicket &&
                   !owner->_clipboardStamp && !owner->_historyWork.Busy() && !owner->_utilityReadback.HasPending() &&
                   owner->_graphGpuBudget.ReservedOwner() == 0,
               "actual-joined-cpu-graphics-and-private-root-retirement-refund");
        owner->_shutdownTestProbe = nullptr;
        owner.reset();
        MSG quit{};
        unsigned quitCount = 0;
        while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE))
            ++quitCount;
        expect(quitCount == 1 && !DeferredReleaseDetail::sink.enqueue,
               "fixture-quit-and-owner-sink-cleared-before-next-owner");
    }
    std::printf("%u clipboard-merged-close assertions; %u failed.\n", checks, failures);
    return failures == 0 ? 0 : 5;
}
