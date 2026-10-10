#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "KeyboardState.h"
#include "UiBenchmark.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

int Application::RunUiCancellationDispatchSelfTest() noexcept
{
    // The ordinary scheduler was initialized without a diagnostic observer. This fixture proves actual
    // owner/key/admission callers only; it cannot manufacture a worker-return sample or visible Present.
    const KeyboardState::ScopedSynthetic keyboard;
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    CancelColorEditForTest();
    CancelSizeCard();
    if (_chrome)
    {
        _chrome->CloseNotice();
        _chrome->CloseShortcuts();
        _chrome->CloseMoreTools();
        _chrome->SetCanvasFocused(true);
    }
    if (_uiBenchmark || !settle())
        return 5;
    auto previousBenchmark = std::move(_uiBenchmark);
    _uiBenchmark = std::make_unique<UiBenchmarkState>();
    _uiBenchmark->cancellation = std::make_unique<UiCancellationBenchmark>();
    if (FAILED(UiCancellationObservation::TryCreate(_uiBenchmark->cancellation->observation)))
    {
        _uiBenchmark = std::move(previousBenchmark);
        return 5;
    }
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            _hostAdmissionAllocationFailureForTest = false;
            if (_colorWorkTestGate)
                (void)SetEvent(_colorWorkTestGate->release.get());
            CancelHostContinuations();
            CancelColorCommit();
            if (!settle())
                std::_Exit(5);
            _colorWorkTestGate.reset();
            _session.CancelColorEdit();
            _uiBenchmark = std::move(previousBenchmark);
            SyncChrome();
        });
    unsigned int failures = 0;
    unsigned int checks = 0;
    unsigned int displayChecks = 0;
    unsigned int displayFailures = 0;
    const auto expectDisplay = [&](bool passed, const char* name, bool refused) noexcept
    {
        ++displayChecks;
        displayFailures += passed ? 0u : 1u;
        std::printf("%s ui-cancel-held-display.%s mode=%s\n", passed ? "PASS" : "FAIL", name,
                    refused ? "admission-refused" : "accepted");
        std::fflush(stdout);
    };
    const auto displayPixelsMatch = [this](bool preview) noexcept
    {
        const auto* shown = DirectDisplayDocumentForFrame();
        if (!shown || FAILED(_composition.Compose(*shown)))
            return false;
        std::vector<std::uint8_t> pixels;
        UINT width = 0;
        UINT height = 0;
        if (FAILED(_composition.Readback(pixels, width, height)) || width != 4 || height != 1 || pixels.size() != 16)
            return false;
        // Existing legacy Levels gamma reference (ColorEditWorkTests/ColorDisplayPublicationTests):
        // 0, 64, 128, 255 -> 0, 128, 181, 255, within one byte; source alpha remains opaque.
        const std::array<std::uint8_t, 4> expected =
            preview ? std::array<std::uint8_t, 4>{0, 128, 181, 255} : std::array<std::uint8_t, 4>{0, 64, 128, 255};
        for (std::size_t x = 0; x < expected.size(); ++x)
        {
            for (std::size_t channel = 0; channel < 3; ++channel)
                if (std::abs(static_cast<int>(pixels[x * 4 + channel]) - expected[x]) > 1)
                    return false;
            if (pixels[x * 4 + 3] != 255)
                return false;
        }
        return true;
    };
    const auto expect = [&](bool passed, const char* name, bool refused) noexcept
    {
        ++checks;
        failures += passed ? 0 : 1;
        std::printf("%s ui-cancel-dispatch.%s mode=%s\n", passed ? "PASS" : "FAIL", name,
                    refused ? "admission-refused" : "accepted");
        std::fflush(stdout);
    };
    constexpr std::array<std::uint8_t, 16> bytes{0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 255, 255, 255, 255};
    const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    for (const bool refused : std::array{false, true})
    {
        if (!settle())
            return 5;
        _colorWorkTestGate.reset();
        _session.CancelColorEdit();
        auto& state = *_uiBenchmark;
        auto& trial = *state.cancellation;
        trial.ResetTrialOwner();
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 1;
        auto layer = ImageLayer::Blank(L"Escape caller reference", Vec2{4, 1});
        const Guid active = layer.id;
        layer.asset = RasterAsset{source, {}, layer.name};
        document.layers.push_back(std::move(layer));
        ReplaceDocument(std::move(document).Seal(), active, {});
        _session.BeginColorEdit(FilterKind::Levels, true);
        if (!_session.PendingColorEdit())
            return 5;
        auto settings = _session.PendingColorEdit().value().settings;
        settings.levels.ranges[0].gamma = 2;
        _session.UpdateColorEdit(settings, true);
        DocumentChanged();
        if (!settle() || !_chrome || !_chrome->AdjustPanelOpen())
            return 5;
        expectDisplay(displayPixelsMatch(true), "original-completed-preview-has-legacy-gamma-pixels", refused);
        state.previousImage = source;
        state.previousRevision = _session.history.CurrentRevision();
        state.previousUndoCount = _session.history.UndoCount();
        trial.authority = _session.CaptureDocumentReadAuthority();
        trial.opening = _session.PendingColorEdit().value().openingSequence;
        trial.documentSerial = _session.DocumentSerial();
        trial.phase = UiCancellationBenchmark::Phase::AwaitingBody;
        auto gate = std::make_shared<ColorWorkTestGate>();
        gate->kind = ColorWorkTestKind::Commit;
        if (!gate->entered || !gate->release || !gate->computed)
            return 5;
        _colorWorkTestGate = gate;
        OnAdjustAction(AdjustAction::Apply, 0);
        if (!WaitForColorWorkTestEvent(_jobs, gate->entered.get(), DispatchMessageThunk, this))
            return 5;
        const auto ticket = _colorCommitTicket;
        const auto stamp = _colorCommitStamp;
        expect(ticket && trial.original == ticket && trial.body, "binds-original-admitted-ticket", refused);
        expect(trial.applyReceipt && trial.applyReceipt.value().Read().outcome == HostOutcome::Pending,
               "holds-original-ok-receipt", refused);
        trial.phase = UiCancellationBenchmark::Phase::EscapePosted;
        _hostAdmissionAllocationFailureForTest = refused;
        const LPARAM down = 1 | (static_cast<LPARAM>(MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC)) << 16);
        const LPARAM up = down | (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);
        // Actual HWND routing reaches OnKeyDown/HandleKey/QueuePanelCancel, including every early return.
        (void)SendMessageW(_window.get(), WM_KEYDOWN, VK_ESCAPE, down);
        _hostAdmissionAllocationFailureForTest = false;
        (void)SendMessageW(_window.get(), WM_KEYUP, VK_ESCAPE, up);
        expect(trial.escapeDowns == 1 && trial.escapeUps == 1 && !trial.ambientInput && !trial.dispatchActive &&
                   trial.phase == UiCancellationBenchmark::Phase::Settling,
               "actual-key-down-up-brackets-real-dispatch", refused);
        expect(trial.tokenAccepted && trial.dispatchBegin >= 0 && trial.stopBegin >= trial.dispatchBegin &&
                   trial.stopAccepted >= trial.stopBegin && trial.dispatchEnd >= trial.stopAccepted,
               "stamps-actual-original-cancel-call", refused);
        expect(trial.cancelReceipt &&
                   trial.cancelReceipt.value().Read().outcome ==
                       (refused ? HostOutcome::Failed : HostOutcome::Pending) &&
                   trial.cancelReceipt.value().Read().error == (refused ? static_cast<std::int32_t>(E_OUTOFMEMORY) : 0),
               "preserves-exact-cancel-admission-result", refused);
        expect(trial.cardHiddenAtAcceptance == !refused && _chrome->AdjustPanelOpen() == refused &&
                   (trial.receiptAccepted >= 0) == !refused,
               "hides-card-only-for-accepted-receipt", refused);
        expect(_session.HasColorEdit() && _session.ActiveLayer() && _session.ActiveLayer()->asset &&
                   _session.ActiveLayer()->asset->image == source,
               "holds-logical-panel-and-source-before-ack", refused);
        expect(stamp && _colorCommitStamp == stamp && _historyWork.IsCurrent(stamp.value()) &&
                   _historyWork.CancelRequested(stamp.value()),
               "retains-original-history-barrier-before-ack", refused);
        expect(ticket && _colorCommitTicket == ticket, "retains-original-ticket-before-ack", refused);
        expect(trial.authority && _session.IsDocumentReadCurrent(trial.authority.value()) &&
                   _session.history.CurrentRevision() == state.previousRevision &&
                   _session.history.UndoCount() == state.previousUndoCount,
               "preserves-document-and-history-authority-before-ack", refused);
        // Classification of an already-disarmed identity. This does not claim native timer admission.
        trial.nextTimer = 65;
        const auto receipt = trial.cancelReceipt;
        expect(OnUiCancellationTimer(64) && !trial.activeTimer && trial.cancelReceipt && receipt &&
                   trial.cancelReceipt.value().Read() == receipt.value().Read() && trial.original == ticket &&
                   trial.phase == UiCancellationBenchmark::Phase::Settling,
               "stale-disarmed-timer-cannot-repost-escape", refused);
        const auto& heldDocument = _session.Document();
        expectDisplay(heldDocument && DirectDisplayDocumentForFrame() ==
                                          (refused ? _session.DisplayDocument() : &heldDocument.value()),
                      "real-frame-selector-excludes-only-accepted-preview", refused);
        expectDisplay(displayPixelsMatch(refused), "actual-composition-restores-only-accepted-source-before-ack",
                      refused);
        expectDisplay(ticket && _colorCommitTicket == ticket && stamp && _colorCommitStamp == stamp &&
                          _historyWork.Busy() && !gate->payloadRetired.load() && _session.HasColorEdit() &&
                          _session.PendingColorEdit()->preview &&
                          _session.history.CurrentRevision() == state.previousRevision,
                      "drawing-does-not-retire-held-original-or-mutate-logical-preview", refused);
        (void)SetEvent(gate->release.get());
        if (!settle())
            return 5;
        if (!refused)
        {
            // This hidden caller fixture settles jobs without rendering. It proves acceptance only;
            // the actual direct/prepared Escape benchmarks prove the immediate visible frame ordinal.
            expect(trial.original && trial.acceptancePresentOrdinal == _renderer.PresentCount() &&
                       trial.firstFrame.Current() == UiCancellationFrame::State::AwaitingFirstVisibleFrame,
                   "accepted-escape-retains-baseline-and-original-identity", refused);
            expect(trial.firstFrame.QualifiedOrdinal() == 0 && trial.feedbackPresented < 0,
                   "settling-without-present-cannot-manufacture-first-frame-proof", refused);
        }
        expect(trial.applyReceipt && trial.applyReceipt.value().Read().outcome == HostOutcome::Cancelled &&
                   trial.applyReceipt.value().Read().error == E_ABORT,
               "original-ok-acknowledges-cancelled-result", refused);
        expect(!_colorCommitTicket && !_colorCommitStamp && !_historyWork.Busy() &&
                   _session.HasColorEdit() == refused && _chrome->AdjustPanelOpen() == refused,
               "retires-original-barrier-with-correct-panel-lifetime", refused);
        const auto& current = _session.Document();
        const auto* direct = _session.DisplayDocument();
        expect(current && UiCancellationSourceSelected(nullptr, direct) == !refused,
               "qualifies-only-actual-committed-render-argument", refused);
        const auto foreign = current;
        expect(foreign && !UiCancellationSourceSelected(nullptr, &foreign.value()),
               "rejects-equivalent-foreign-document-address", refused);
        expect(_session.ActiveLayer() && _session.ActiveLayer()->asset->image == source && trial.authority &&
                   _session.IsDocumentReadCurrent(trial.authority.value()) &&
                   _session.history.CurrentRevision() == state.previousRevision &&
                   _session.history.UndoCount() == state.previousUndoCount,
               "joined-cancellation-preserves-original-source-history", refused);
        expect(gate->payloadRetired.load() && gate->payloadRetiredOffOwner.load(),
               "original-payload-retires-only-on-joined-worker", refused);
    }
    constexpr unsigned int kExpectedChecks = 36;
    std::printf("%s ui-cancel-dispatch.summary controls=%u failed=%u visiblePresentMeasured=0 bodyReturnMeasured=0\n",
                failures == 0 && checks == kExpectedChecks ? "PASS" : "FAIL", checks, failures);
    std::fflush(stdout);
    const bool displayPassed = displayFailures == 0 && displayChecks == 8;
    std::printf("%s ui-cancel-held-display.summary controls=%u failed=%u visiblePresentMeasured=0\n",
                displayPassed ? "PASS" : "FAIL", displayChecks, displayFailures);
    std::fflush(stdout);
    unsigned int refusalChecks = 0;
    unsigned int refusalFailures = 0;
    struct BlurCase final
    {
        const char* name;
        int command;
        AdjustAction setting;
        double value;
    };
    constexpr std::array blurs{
        BlurCase{"gaussian", ChromeHost::kCommandGaussianBlur, AdjustAction::SetRadius, 250},
        BlurCase{"motion", ChromeHost::kCommandMotionBlur, AdjustAction::SetDistance, 2000},
    };
    const auto previousSettings = _session.filterSettings;
    const auto restoreSettings = wil::scope_exit([&]() noexcept { _session.filterSettings = previousSettings; });
    for (const bool openingRefusal : std::array{true, false})
    {
        for (const auto& blur : blurs)
        {
            if (!settle())
                return 5;
            _colorWorkTestGate.reset();
            _session.CancelColorEdit();
            auto& state = *_uiBenchmark;
            auto& trial = *state.cancellation;
            trial.ResetTrialOwner();
            DocumentFixture document;
            document.id = Guid::Generate();
            document.width = 4;
            document.height = 1;
            const auto addBudget = [&](int width, int height)
            {
                auto budget = ImageLayer::Blank(L"Refusal budget", Vec2{1, 1});
                budget.visible = false;
                budget.asset = RasterAsset{Raster::MakeSparse(width, height, RasterFormat::Bgra8888, {}, 0, 0, {}, 0),
                                           Raster::Make(1, 1, RasterFormat::Bgra8888), budget.name};
                document.layers.push_back(std::move(budget));
            };
            // Exact aggregate D7 limits without large allocations or uploads: opening has four pixels
            // left; settings has one million, enough for the default grid but not either extreme grid.
            addBudget(10000, openingRefusal ? 9999 : 9900);
            if (openingRefusal)
                addBudget(9996, 1);
            auto layer = ImageLayer::Blank(L"Refusal caller", Vec2{4, 1});
            const Guid active = layer.id;
            layer.asset = RasterAsset{source, {}, layer.name};
            document.layers.push_back(std::move(layer));
            ReplaceDocument(std::move(document).Seal(), active, {});
            state.activeLayer = active;
            state.previousImage = source;
            state.previousRevision = _session.history.CurrentRevision();
            state.previousUndoCount = _session.history.UndoCount();
            trial.documentSerial = _session.DocumentSerial();
            trial.preparationBudgetNotices = _budgetNotices;
            trial.expectedRefusal = openingRefusal ? UiCancellationRefusalKind::OpeningPixelBudget
                                                   : UiCancellationRefusalKind::SettingsPixelBudget;
            _session.filterSettings = {};
            OnCommand(blur.command); // Actual menu admission, worker opening and original budget notice.
            if (!settle())
                return 5;
            if (!openingRefusal)
            {
                OnAdjustAction(AdjustAction::SetPreview, 0);
                OnAdjustAction(blur.setting, blur.value); // Actual control caller consumes TakeBudgetRefusal.
                if (!settle())
                    return 5;
            }
            trial.phase = UiCancellationBenchmark::Phase::AwaitingRefusal;
            const auto checkRefusal = [&](bool passed, const char* name) noexcept
            {
                ++refusalChecks;
                refusalFailures += passed ? 0u : 1u;
                std::printf("%s ui-cancel-refusal-caller.%s.%s.%s\n", passed ? "PASS" : "FAIL",
                            openingRefusal ? "opening" : "settings", blur.name, name);
            };
            checkRefusal(_budgetNotices == trial.preparationBudgetNotices + 1, "original-budget-notice-once");
            checkRefusal(UiCancellationOwnersSettled() && !trial.original && !trial.body && !trial.applyReceipt &&
                             !trial.cancelReceipt && !trial.activeTimer,
                         "no-processing-or-timer-admitted");
            checkRefusal(_session.ActiveLayer()->asset.value().image == source &&
                             _session.history.CurrentRevision() == state.previousRevision &&
                             _session.history.UndoCount() == state.previousUndoCount,
                         "unchanged-source-and-history");
            checkRefusal(_session.HasColorEdit() == !openingRefusal, "actual-opening-or-settings-state");
            checkRefusal(UiCancellationRefusalReady(), "real-caller-qualifies-refusal");
            DocumentChanged();
            if (!settle())
                return 5;
            checkRefusal(_budgetNotices == trial.preparationBudgetNotices + 1 && UiCancellationRefusalReady(),
                         "notification-consumed-without-repeating");
        }
    }
    constexpr unsigned int kExpectedRefusalChecks = 24;
    std::printf(
        "%s ui-cancel-refusal-caller.summary controls=%u failed=%u visiblePresentMeasured=0 ownerCostMeasured=0\n",
        refusalFailures == 0 && refusalChecks == kExpectedRefusalChecks ? "PASS" : "FAIL", refusalChecks,
        refusalFailures);
    std::fflush(stdout);
    return static_cast<int>(failures + (checks != kExpectedChecks ? 1 : 0) + (displayPassed ? 0 : 1) + refusalFailures +
                            (refusalChecks != kExpectedRefusalChecks ? 1 : 0));
}

int Application::RunStandaloneUiCancellationCanvasSelfTest(HINSTANCE instance) noexcept
{
    struct Case final
    {
        const char* name;
        bool prepared;
        bool inject;
        Renderer::Stage stage;
        HRESULT injectedResult;
        HRESULT canvasFailure;
        bool drawn;
    };
    constexpr std::array cases{
        Case{"direct-draw", false, false, Renderer::Stage::Blit, S_OK, S_OK, true},
        Case{"direct-compose-failed", false, true, Renderer::Stage::Compose, E_FAIL, E_FAIL, false},
        Case{"direct-blit-failed", false, true, Renderer::Stage::Blit, E_FAIL, E_FAIL, false},
        Case{"direct-blit-skipped", false, true, Renderer::Stage::Blit, S_FALSE, S_OK, false},
        Case{"prepared-draw", true, false, Renderer::Stage::Blit, S_OK, S_OK, true},
        Case{"prepared-blit-failed", true, true, Renderer::Stage::Blit, E_FAIL, E_FAIL, false},
    };
    unsigned int checks = 0;
    unsigned int failures = 0;
    for (const auto& test : cases)
    {
        const auto expect = [&](bool passed, const char* name) noexcept
        {
            ++checks;
            failures += passed ? 0u : 1u;
            std::printf("%s ui-cancel-canvas.%s.%s\n", passed ? "PASS" : "FAIL", test.name, name);
            std::fflush(stdout);
            return passed;
        };
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto close = wil::scope_exit(
            [&]() noexcept
            {
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) &&
                        (!test.prepared || owner->EnableCommittedDisplayForTest(true)) &&
                        SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                        SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome()),
                    "actual-hidden-WARP-host-initializes"))
            return 5;
        constexpr std::array<std::uint8_t, 16> bytes{0,   0,   0,   255, 64,  64,  64,  255,
                                                     128, 128, 128, 255, 255, 255, 255, 255};
        const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 1;
        auto layer = ImageLayer::Blank(L"Canvas proof reference", Vec2{4, 1});
        const auto active = layer.id;
        layer.asset = RasterAsset{source, {}, layer.name};
        layer.transform.sampling = LayerSampling::Nearest;
        document.layers.push_back(std::move(layer));
        expect(source.HasStorage(), "literal-immutable-source-exists");
        owner->ReplaceDocument(std::move(document).Seal(), active, {});
        owner->DocumentChanged();
        if (!expect(SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled &&
                        (!test.prepared || owner->_acknowledgedCommittedDisplay),
                    "original-preparation-and-GPU-uses-settle"))
            return 5;
        owner->_uiBenchmark = std::make_unique<UiBenchmarkState>();
        auto& state = *owner->_uiBenchmark;
        state.preparedDisplay = test.prepared;
        // Deliberate canvas faults are expected. Suppress only FailUiBenchmark's report/WM_QUIT side effect;
        // the actual RenderFrame -> NoteUiCancellationPresentation reducer and feedback field still run.
        // This fixture tests the frame caller with an owned authority, not scheduler admission or Escape.
        state.failed = true;
        state.previousImage = source;
        state.previousRevision = owner->_session.history.CurrentRevision();
        state.previousUndoCount = owner->_session.history.UndoCount();
        state.cancellation = std::make_unique<UiCancellationBenchmark>();
        auto& trial = *state.cancellation;
        trial.original = JobTicket{7, 41};
        trial.opening = 29;
        trial.authority = owner->_session.CaptureDocumentReadAuthority();
        trial.receiptAccepted = 0;
        trial.acceptancePresentOrdinal = owner->_renderer.PresentCount();
        if (!expect(
                trial.firstFrame.Accept({trial.original.value(), owner->_session.DocumentSerial(), trial.opening,
                                         trial.acceptancePresentOrdinal, owner->UiCancellationOriginalStateCurrent(),
                                         owner->_chrome && !owner->_chrome->AdjustPanelOpen()}),
                "frame-fixture-accepts-exact-owned-source-and-baseline"))
            return 5;
        if (test.inject)
            owner->_renderer.FailNextStageForTest(test.stage, test.injectedResult);
        owner->RenderFrame();
        expect(owner->_renderer.PresentCount() == trial.acceptancePresentOrdinal + 1,
               "actual-chrome-Present-advances-first-visible-ordinal");
        expect(owner->_renderer.CanvasDrawn() == test.drawn, "actual-canvas-draw-is-distinct-from-chrome-Present");
        expect(owner->_renderer.CanvasFailure() == test.canvasFailure, "actual-canvas-HRESULT-is-preserved");
        const auto expected = test.drawn ? UiCancellationFrame::State::Qualified : UiCancellationFrame::State::Rejected;
        expect(trial.firstFrame.Current() == expected && (trial.feedbackPresented >= 0) == test.drawn,
               "actual-frame-caller-qualifies-only-successful-canvas-draw");
        const auto firstFeedback = trial.feedbackPresented;
        owner->RenderFrame();
        expect(owner->_renderer.PresentCount() == trial.acceptancePresentOrdinal + 2 && owner->_renderer.CanvasDrawn(),
               "next-actual-render-recovers-canvas-draw");
        expect(trial.firstFrame.Current() == expected && trial.feedbackPresented == firstFeedback &&
                   trial.firstFrame.QualifiedOrdinal() == (test.drawn ? trial.acceptancePresentOrdinal + 1 : 0),
               "later-restoration-cannot-replace-first-frame-proof");
        expect(owner->UiCancellationOriginalStateCurrent() && owner->_session.ActiveLayer() &&
                   owner->_session.ActiveLayer()->asset->image == source,
               "fault-and-retry-preserve-source-and-history-authority");
        owner->Shutdown();
        expect(owner->_shutdownComplete && owner->_jobs.IsIdle() && !owner->_jobs.WakeHandle(),
               "actual-owner-joins-and-retires-original-resources");
    }
    const bool passed = failures == 0 && checks == 72;
    std::printf("%s UI cancellation canvas caller: %u checks, %u failures.\n", passed ? "PASS" : "FAIL", checks,
                failures);
    std::fflush(stdout);
    return passed ? 0 : 5;
}
