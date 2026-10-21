#include "Application.h"

#include "ChromeHost.h"
#include "Prompts.h"
#include "Runtime.Tests.ApplicationShutdownTestProbe.h"
#include "Runtime.Tests.ColorWorkTestGate.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

int Application::RunStandaloneColorDisplayShutdownSelfTest(HINSTANCE instance) noexcept
{
    // These owners run before the main self-test Application installs its release sink. Keep each gate alive
    // through actual shutdown, which must release it with the scheduler shutdown event, not a harness signal.
    for (const bool ready : std::array{false, true})
    {
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        const auto shutdown = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                    owner->Shutdown();
            });
        if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
        {
            SelfTestFailure(L"Color display shutdown fixture could not initialize its hidden WARP owner.\n");
            return 5;
        }
        owner->BindCompositionDevice();
        owner->_session.CreateDocument(4, 1, false);
        const Guid target = InsertPaintedLayer(owner->_session, L"Shutdown target", 4, 1, {}, 0, 0xFF808080u);
        if (!owner->_session.ActiveLayer() || !owner->_session.ActiveLayer()->asset)
        {
            SelfTestFailure(L"Color display shutdown fixture could not create its source image.\n");
            return 5;
        }
        const auto revision = owner->_session.history.CurrentRevision();
        const int undoCount = owner->_session.history.UndoCount();
        const Raster source = owner->_session.ActiveLayer()->asset->image;
        auto gate = std::make_shared<ColorWorkTestGate>();
        gate->afterPreparation = ready;
        owner->_colorWorkTestGate = gate;
        owner->_session.BeginColorEdit(FilterKind::Exposure, true);
        if (!owner->_session.PendingColorEdit())
        {
            SelfTestFailure(L"Color display shutdown fixture could not open its panel.\n");
            return 5;
        }
        auto settings = owner->_session.PendingColorEdit()->settings;
        settings.exposure.exposure = 1;
        owner->_session.UpdateColorEdit(settings, true);
        owner->DocumentChanged();
        if (!WaitForColorWorkTestEvent(owner->_jobs, gate->entered.get(), DispatchMessageThunk, owner.get()))
        {
            SelfTestFailure(L"Color display shutdown fixture did not reach its worker gate.\n");
            return 5;
        }
        struct JoinedState final
        {
            Application* owner;
            Guid target;
            Guid revision;
            int undoCount;
            Raster source;
            bool unchanged = false;
            unsigned int visits = 0;
        } joined{owner.get(), target, revision, undoCount, source};
        ApplicationShutdownTestProbe probe;
        probe.checkContext = &joined;
        probe.checkJoined = [](void* context) noexcept
        {
            auto& state = *static_cast<JoinedState*>(context);
            const auto& session = state.owner->_session;
            const auto* shown = state.owner->_session.DisplayDocument();
            ++state.visits;
            state.unchanged = session.ActiveLayer() && session.ActiveLayer()->id == state.target &&
                              session.ActiveLayer()->asset->image == state.source &&
                              session.history.CurrentRevision() == state.revision &&
                              session.history.UndoCount() == state.undoCount && session.PendingColorEdit() &&
                              session.PendingColorEdit()->previewSequence == 0 && shown && session.Document() &&
                              shown->layers.Identity() == session.Document()->layers.Identity();
        };
        owner->_shutdownTestProbe = &probe;
        const bool admitted = owner->_colorPreviewTicket && gate->captureReservationHeld.load() &&
                              owner->_jobs.ReservedMemoryBytes() >= gate->reservationBytes &&
                              gate->visits.load() == 1 &&
                              (!ready || WaitForSingleObject(gate->computed.get(), 0) == WAIT_OBJECT_0);
        owner->Shutdown();
        const bool passed = admitted && joined.visits == 1 && joined.unchanged && owner->_shutdownComplete &&
                            !owner->_colorPreviewTicket && !owner->_colorHistogramTicket &&
                            owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                            gate->payloadRetired.load() && gate->payloadRetiredOffOwner.load() &&
                            probe.workerCalls.load() == 1 && probe.mta.load() && probe.offOwner.load();
        std::fprintf(stderr, "%s color-display-host.shutdown.%s-publication-dropped-and-retired\n",
                     passed ? "PASS" : "FAIL", ready ? "ready" : "running");
        owner->_shutdownTestProbe = nullptr;
        owner.reset();
        // Destroying this fixture's HWND posts Quit. Consume it before the next independent owner is initialized.
        while (PumpMessages().quit)
        {
        }
        if (!passed)
        {
            SelfTestFailure(L"Color display shutdown published a late result or failed to acknowledge retirement.\n");
            return 5;
        }
    }

    for (const bool metadata : {false, true})
    {
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        const auto finishOwner = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                    owner->Shutdown();
            });
        if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
        {
            SelfTestFailure(L"Cancellation feedback shutdown fixture could not initialize its independent owner.\n");
            return 5;
        }
        owner->BindCompositionDevice();
        owner->_session.CreateDocument(4, 1, false);
        (void)InsertPaintedLayer(owner->_session, L"Cancellation shutdown source", 4, 1, {}, 0, 0xFF808080u);
        const auto* painted = owner->_session.ActiveLayer();
        if (!painted || !painted->asset)
        {
            SelfTestFailure(L"Cancellation shutdown could not prepare its independent source image.\n");
            return 5;
        }
        const Raster originalImage = painted->asset->image;
        if (metadata)
        {
            owner->_session.AddAdjustment(AdjustmentKind::Exposure);
            const auto adjustment = owner->_session.ActiveLayerID();
            if (!adjustment)
            {
                SelfTestFailure(L"Cancellation shutdown could not add its independent adjustment layer.\n");
                return 5;
            }
            owner->_session.SetAdjustmentEditingID(adjustment);
            if (!owner->_session.BeginAdjustmentEditing(adjustment.value(), originalImage, true))
            {
                SelfTestFailure(L"Cancellation shutdown could not open its adjustment metadata panel.\n");
                return 5;
            }
        }
        else
            owner->_session.BeginColorEdit(FilterKind::Exposure, true);
        owner->DocumentChanged();
        if (SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) != SettleResult::Settled ||
            !owner->_session.PendingColorEdit() || !owner->_chrome->AdjustPanelOpen())
        {
            SelfTestFailure(L"Cancellation feedback shutdown fixture did not prepare its live card.\n");
            return 5;
        }
        const auto originalDocument = owner->_session.Document();
        const auto originalActive = owner->_session.ActiveLayerID();
        const auto originalRevision = owner->_session.history.CurrentRevision();
        const int originalDepth = owner->_session.history.UndoCount();
        const auto originalOpening = owner->_session.PendingColorEdit()->openingSequence;
        auto gate = std::make_shared<ColorWorkTestGate>();
        gate->kind = ColorWorkTestKind::Commit;
        owner->_colorWorkTestGate = gate;
        const auto receipt = owner->QueuePanelCancel(originalOpening);
        if (!WaitForColorWorkTestEvent(owner->_jobs, gate->entered.get(), DispatchMessageThunk, owner.get()) ||
            receipt.Read().outcome != HostOutcome::Pending || !owner->_colorCommitStamp ||
            owner->_chrome->AdjustPanelOpen() || !owner->_session.HasColorEdit())
        {
            SelfTestFailure(L"Cancellation feedback shutdown fixture did not retain its hidden original closure.\n");
            return 5;
        }
        struct JoinedFeedback final
        {
            Application* owner;
            Guid revision;
            int depth;
            std::uint64_t opening;
            std::uint64_t chromeSyncs;
            std::optional<CanvasDocument> document;
            std::optional<Guid> active;
            unsigned int visits = 0;
            bool unchangedAndHidden = false;
        } joined{owner.get(),         originalRevision, originalDepth, originalOpening,
                 owner->_chromeSyncs, originalDocument, originalActive};
        ApplicationShutdownTestProbe probe;
        probe.checkContext = &joined;
        probe.checkJoined = [](void* opaque) noexcept
        {
            auto& state = *static_cast<JoinedFeedback*>(opaque);
            auto& current = *state.owner;
            ++state.visits;
            state.unchangedAndHidden =
                current._shutdownStarted && current._chrome && !current._chrome->AdjustPanelOpen() &&
                !current.ColorCancelPending() && current._chromeSyncs == state.chromeSyncs &&
                current._session.PendingColorEdit() &&
                current._session.PendingColorEdit()->openingSequence == state.opening &&
                current._session.Document() == state.document && current._session.ActiveLayerID() == state.active &&
                current._session.history.CurrentRevision() == state.revision &&
                current._session.history.UndoCount() == state.depth;
        };
        owner->_shutdownTestProbe = &probe;
        owner->Shutdown(); // The real shutdown event releases the held original worker; the harness never does.
        const bool hiddenAtJoin = joined.visits == 1 && joined.unchangedAndHidden;
        std::fprintf(stderr, "%s color-cancel-feedback.shutdown-joined metadata=%u\n", hiddenAtJoin ? "PASS" : "FAIL",
                     static_cast<unsigned int>(metadata));
        const bool retired = owner->_shutdownComplete && !owner->_chrome &&
                             receipt.Read().outcome == HostOutcome::Cancelled && gate->payloadRetired.load() &&
                             gate->payloadRetiredOffOwner.load() && owner->_jobs.ReservedMemoryBytes() == 0 &&
                             owner->_jobs.ReleasingMemoryBytes() == 0;
        std::fprintf(stderr, "%s color-cancel-feedback.shutdown-retired metadata=%u\n", retired ? "PASS" : "FAIL",
                     static_cast<unsigned int>(metadata));
        owner->_shutdownTestProbe = nullptr;
        owner.reset();
        while (PumpMessages().quit)
        {
        }
        if (!hiddenAtJoin || !retired)
        {
            SelfTestFailure(L"Shutdown restored cancellation chrome or lost its original joined ownership.\n");
            return 5;
        }
    }
    return RunStandaloneColorDisplayAdmissionSelfTest(instance);
}

int Application::RunColorDisplayHostSelfTest() noexcept
{
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    int checks = 0;
    const auto expect = [&checks](const char* name, bool condition) noexcept
    {
        std::fprintf(stderr, "%s color-display-host.%s\n", condition ? "PASS" : "FAIL", name);
        checks += condition ? 1 : 0;
        return condition;
    };

    constexpr std::array<std::uint8_t, 16> sourceBytes{0,  0,  0,  255, 128, 128, 128, 255,
                                                       64, 64, 64, 255, 255, 255, 255, 255};
    constexpr std::array<std::uint8_t, 16> siblingBytes{10, 20, 230, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    constexpr std::array<std::uint8_t, 4> maskBytes{255, 255, 255, 255};
    const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, sourceBytes.data(), sourceBytes.size());
    const Raster siblingImage = Raster::Make(4, 1, RasterFormat::Bgra8888, siblingBytes.data(), siblingBytes.size());
    const Raster mask = Raster::Make(4, 1, RasterFormat::Gray8, maskBytes.data(), maskBytes.size());
    Guid sourceId;
    Guid siblingId;

    const auto cleanup = wil::scope_exit(
        [this]() noexcept
        {
            if (_colorWorkTestGate)
                (void)SetEvent(_colorWorkTestGate->release.get());
            CancelColorEdit();
            _colorWorkTestGate.reset();
            if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
            {
                SelfTestFailure(L"Color display host work did not settle during cleanup.\n");
                std::_Exit(5);
            }
        });

    const auto prepare =
        [this, &settle, &source, &siblingImage, &mask, &sourceId, &siblingId](std::size_t extraRecords = 0) noexcept
    {
        if (_colorWorkTestGate)
            (void)SetEvent(_colorWorkTestGate->release.get());
        if (!settle())
            return false;
        _colorWorkTestGate.reset();
        _session.CancelColorEdit();
        DocumentFixture fixture;
        fixture.id = Guid::Generate();
        fixture.width = 4;
        fixture.height = 1;
        fixture.selection = DocumentSelection{false, {{Vec2{1, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{1, 1}}}};
        ImageLayer target = ImageLayer::Blank(L"Display target", Vec2{4, 1});
        sourceId = target.id;
        target.asset = RasterAsset{source, {}, target.name};
        target.mask = LayerMask{RasterAsset{mask, mask, L"Display mask"}, true, {}, true};
        ImageLayer sibling = ImageLayer::Blank(L"Unaffected sibling", Vec2{4, 1});
        siblingId = sibling.id;
        sibling.asset = RasterAsset{siblingImage, {}, sibling.name};
        fixture.layers.push_back(std::move(target));
        fixture.layers.push_back(std::move(sibling));
        for (std::size_t index = 0; index < extraRecords; ++index)
        {
            ImageLayer empty = ImageLayer::Blank(L"Untouched metadata record", Vec2{4, 1});
            empty.visible = false;
            empty.opacity = 0.75;
            fixture.layers.push_back(std::move(empty));
        }
        ReplaceDocument(std::move(fixture).Seal(), sourceId, {});
        _session.history.Reset();
        _session.history.MarkSaved();
        _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
        _session.BeginColorEdit(FilterKind::Exposure, true);
        return _colorWorkTestGate->entered && _colorWorkTestGate->release && _colorWorkTestGate->computed &&
               _session.HasColorEdit();
    };
    const auto start = [this](double exposure) noexcept
    {
        if (!_session.PendingColorEdit())
            return false;
        auto settings = _session.PendingColorEdit()->settings;
        settings.exposure.exposure = exposure;
        _session.UpdateColorEdit(settings, true);
        DocumentChanged();
        return true;
    };
    const auto waitEntered = [this]() noexcept
    {
        return _colorWorkTestGate &&
               WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this);
    };
    const auto preparedDisplayStillCurrent = [this, &source, &siblingImage]() noexcept
    {
        const CanvasDocument* display = _session.DisplayDocument();
        if (!display || display->layers.size() != 2 || !display->layers[0].asset || !display->layers[1].asset ||
            display->layers[0].asset->image != source || display->layers[1].asset->image != siblingImage ||
            display->layers[1].name != L"Unaffected sibling" ||
            display->layers[1].visible != _session.Document()->layers[1].visible ||
            display->layers[1].opacity != _session.Document()->layers[1].opacity)
            return false;
        return true;
    };
    const auto displayMatchesDocument = [this]() noexcept
    {
        const CanvasDocument* current = _session.Document().has_value() ? &_session.Document().value() : nullptr;
        const CanvasDocument* displayDocument = _session.DisplayDocument();
        return current && displayDocument && *current == *displayDocument;
    };
    const auto hasRetiredOffOwner = [this]() noexcept
    {
        return _colorWorkTestGate && _colorWorkTestGate->payloadRetired.load() &&
               _colorWorkTestGate->payloadRetiredOffOwner.load() && _jobs.ReservedMemoryBytes() == 0 &&
               !_colorPreviewTicket;
    };

    if (!source.HasStorage() || !siblingImage.HasStorage() || !mask.HasStorage() || !prepare() || !start(1.0) ||
        !waitEntered())
        return fail(L"The literal masked display fixture did not reach the production preview worker.\n");
    const auto successGate = _colorWorkTestGate;
    const auto openingRevision = _session.history.CurrentRevision();
    const bool admitted = successGate->captureReservationHeld.load() &&
                          _jobs.ReservedMemoryBytes() >= successGate->reservationBytes && _colorPreviewTicket;
    (void)SetEvent(successGate->release.get());
    if (!settle())
        return fail(L"The full display preview did not acknowledge.\n");
    const CanvasDocument* display = _session.DisplayDocument();
    std::vector<std::uint8_t> composed;
    UINT width = 0;
    UINT height = 0;
    // ImageAdjustmentTests.exposureWorksInLinearLightWithOffsetAndGamma: +1 stop maps gray 128 to 176 +/-2.
    // Only x=1 is selected; the opaque sibling at x=0 and the two unselected target pixels remain literal.
    constexpr std::array<std::uint8_t, 16> expectedDisplay{10, 20, 230, 255, 176, 176, 176, 255,
                                                           64, 64, 64,  255, 255, 255, 255, 255};
    bool pixelsMatch = display && SUCCEEDED(_composition.Compose(*display)) &&
                       SUCCEEDED(_composition.Readback(composed, width, height)) && width == 4 && height == 1 &&
                       composed.size() == expectedDisplay.size();
    if (pixelsMatch)
    {
        for (std::size_t index = 0; index < expectedDisplay.size(); ++index)
        {
            const int tolerance = index % 4 == 3 ? 0 : 2;
            if (std::abs(static_cast<int>(composed[index]) - expectedDisplay[index]) > tolerance)
            {
                pixelsMatch = false;
                break;
            }
        }
    }
    if (!expect("success.full-display-and-sibling", admitted && pixelsMatch && display->layers.size() == 2 &&
                                                        display->layers[1].asset->image == siblingImage &&
                                                        display->layers[1].name == L"Unaffected sibling" &&
                                                        _session.history.CurrentRevision() == openingRevision &&
                                                        hasRetiredOffOwner()))
    {
        std::fprintf(stderr,
                     "color-display-host.success diagnostic admitted=%u pixels=%u layers=%zu retired=%u "
                     "offOwner=%u reserved=%llu sequence=%llu lookupReady=%u\n",
                     admitted ? 1U : 0U, pixelsMatch ? 1U : 0U, display ? display->layers.size() : 0,
                     successGate->payloadRetired.load() ? 1U : 0U, successGate->payloadRetiredOffOwner.load() ? 1U : 0U,
                     static_cast<unsigned long long>(_jobs.ReservedMemoryBytes()),
                     static_cast<unsigned long long>(_session.PendingColorEdit()->previewSequence),
                     _session.ColorLookupPreviewReady() ? 1U : 0U);
        for (std::size_t index = 0; index < composed.size() && index < expectedDisplay.size(); ++index)
            std::fprintf(stderr, "color-display-host.success byte=%zu actual=%u expected=%u\n", index,
                         static_cast<unsigned int>(composed[index]), static_cast<unsigned int>(expectedDisplay[index]));
        return fail(L"The preview failed its literal full-display, sibling, history or retirement checks.\n");
    }

    if (!prepare() || !start(0.5) || !waitEntered())
        return fail(L"The coalesced latest-sequence fixture did not reach its worker gate.\n");
    const auto coalescedGate = _colorWorkTestGate;
    const auto coalescedSequence = _session.ColorEditSequence();
    for (double value : {0.75, 1.25, 1.5})
    {
        auto settings = _session.PendingColorEdit()->settings;
        settings.exposure.exposure = value;
        _session.UpdateColorEdit(settings, true);
        DocumentChanged();
    }
    const auto latestSequence = _session.ColorEditSequence();
    const bool oneCapture = coalescedGate->visits.load() == 1 && _colorPreviewTicket && _colorPreviews.HasPending() &&
                            latestSequence > coalescedSequence;
    (void)SetEvent(coalescedGate->release.get());
    if (!settle() ||
        !expect("coalesced.latest-sequence", oneCapture && coalescedGate->visits.load() == 2 &&
                                                 _session.PendingColorEdit()->previewSequence == latestSequence &&
                                                 _session.PendingColorEdit()->settings.exposure.exposure == 1.5 &&
                                                 hasRetiredOffOwner()))
        return fail(L"Coalesced preview work did not publish only the latest sequence.\n");

    if (!prepare() || !start(0.5) || !waitEntered())
        return fail(L"The preview off/on fixture did not reach its worker gate.\n");
    const auto toggleGate = _colorWorkTestGate;
    auto settings = _session.PendingColorEdit()->settings;
    _session.UpdateColorEdit(settings, false);
    DocumentChanged();
    const bool retainedWhileOff = _colorPreviewTicket.has_value();
    _session.UpdateColorEdit(settings, true);
    DocumentChanged();
    const bool retainedAfterOn = _colorPreviewTicket.has_value() && toggleGate->visits.load() == 1;
    (void)SetEvent(toggleGate->release.get());
    if (!settle() || !expect("preview.off-on-retains-ticket",
                             retainedWhileOff && retainedAfterOn && hasRetiredOffOwner() &&
                                 _session.PendingColorEdit()->previewSequence == _session.ColorEditSequence()))
        return fail(L"Preview off/on overlapped capture or lost the exact job acknowledgement.\n");

    if (!prepare())
        return fail(L"The capture failure fixture did not open.\n");
    const auto captureGate = _colorWorkTestGate;
    captureGate->throwCaptureFailure = true;
    if (!start(1.0))
        return fail(L"The capture failure fixture could not submit.\n");
    if (!settle() || !expect("capture.failure-preserves-source",
                             captureGate->captureReservationHeld.load() && captureGate->payloadRetired.load() &&
                                 captureGate->payloadRetiredOffOwner.load() && !_colorPreviewTicket &&
                                 _jobs.ReservedMemoryBytes() == 0 && _session.ActiveLayer()->asset->image == source &&
                                 _session.history.UndoCount() == 0 && preparedDisplayStillCurrent()))
        return fail(L"Owner capture failure changed the source/history or retained preview ownership.\n");

    if (!prepare())
        return fail(L"The worker allocation failure fixture did not reach its gate.\n");
    const auto allocationGate = _colorWorkTestGate;
    allocationGate->throwAllocationFailure = true;
    if (!start(1.0) || !waitEntered())
        return fail(L"The worker allocation failure fixture could not submit.\n");
    (void)SetEvent(allocationGate->release.get());
    if (!settle() || !expect("worker.allocation-failure-retires-off-owner",
                             _session.ActiveLayer()->asset->image == source && _session.history.UndoCount() == 0 &&
                                 preparedDisplayStillCurrent() && hasRetiredOffOwner()))
        return fail(L"Worker allocation failure published stale pixels or retained its payload.\n");

    const auto stalePreparedCase = [this, &prepare, &start, &waitEntered, &settle, &displayMatchesDocument,
                                    &hasRetiredOffOwner, &expect](const char* name, auto&& invalidate) noexcept
    {
        if (!prepare())
            return false;
        _colorWorkTestGate->afterPreparation = true;
        if (!start(1.0))
            return false;
        const auto gate = _colorWorkTestGate;
        if (!waitEntered() || WaitForSingleObject(gate->computed.get(), 0) != WAIT_OBJECT_0)
            return false;
        if (!std::forward<decltype(invalidate)>(invalidate)())
            return false; // An injected mutation must actually reach this freshly constructed fixture.
        (void)SetEvent(gate->release.get());
        return settle() &&
               expect(name, displayMatchesDocument() && _session.history.UndoCount() == 0 && hasRetiredOffOwner());
    };

    if (!stalePreparedCase("identity.cancel-refuses-prepared-publication",
                           [this, sourceBytes]
                           {
                               const auto targetId = _session.ActiveLayerID();
                               const Raster original = _session.ActiveLayer()->asset->image;
                               const Raster replacement =
                                   Raster::Make(4, 1, RasterFormat::Bgra8888, sourceBytes.data(), sourceBytes.size());
                               _session.EditForTest(
                                   [&](CanvasDocument& document)
                                   {
                                       auto fixture = DocumentFixture::Copy(document);
                                       for (auto& layer : fixture.layers)
                                           if (layer.id == targetId)
                                               layer.asset->image = replacement;
                                       document = std::move(fixture).Seal();
                                   });
                               return _session.ActiveLayer()->asset->image == replacement && replacement != original;
                           }))
        return fail(L"Changed source identity accepted a prepared display publication.\n");

    if (!prepare())
        return fail(L"The prepared Cancel/reopen fixture could not open.\n");
    _colorWorkTestGate->afterPreparation = true;
    if (!start(1.0) || !waitEntered())
        return fail(L"The prepared Cancel/reopen fixture did not reach its worker gate.\n");
    const auto cancelledGate = _colorWorkTestGate;
    const auto cancelledTicket = _colorPreviewTicket;
    const auto cancel = QueuePanelCancel(_session.PendingColorEdit()->openingSequence);
    const auto deadline = GetTickCount64() + 10000;
    while (cancel.Read().outcome == HostOutcome::Pending)
    {
        if (PumpMessages(DispatchMessageThunk, this).quit)
            return fail(L"The prepared Cancel/reopen fixture received Quit.\n");
        (void)DrainCompletions(_jobs);
        (void)RunUiActions(_jobs);
        if (cancel.Read().outcome != HostOutcome::Pending)
            break;
        const auto now = GetTickCount64();
        if (now >= deadline || !_jobs.WakeHandle())
            return fail(L"The prepared Cancel/reopen receipt did not acknowledge.\n");
        if (!_jobs.HasUiWork())
        {
            const HANDLE wake = static_cast<HANDLE>(_jobs.WakeHandle());
            const auto waited = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                            MWMO_INPUTAVAILABLE);
            if (waited > WAIT_OBJECT_0 + 1)
                return fail(L"The prepared Cancel/reopen event wait failed.\n");
        }
    }
    if (cancel.Read().outcome != HostOutcome::Succeeded || _session.HasColorEdit() ||
        _colorPreviewTicket != cancelledTicket)
        return fail(L"Cancel lost the old preview acknowledgement or failed to reopen.\n");
    _session.BeginColorEdit(FilterKind::Exposure, true);
    if (!_session.HasColorEdit())
        return fail(L"Cancel did not leave the source available for reopen.\n");
    DocumentChanged();
    (void)SetEvent(cancelledGate->release.get());
    if (!settle() || !expect("cancel-reopen.late-success-refused",
                             displayMatchesDocument() && !_session.PendingColorEdit()->previewSequence &&
                                 cancelledGate->visits.load() == 1 && hasRetiredOffOwner()))
        return fail(L"A late successful publication crossed Cancel and reopen.\n");

    if (!stalePreparedCase("document.same-id-replacement-refused",
                           [this]
                           {
                               const auto targetId = _session.ActiveLayerID();
                               const auto serial = _session.DocumentSerial();
                               const CanvasDocument sameContents = _session.Document().value();
                               auto replacement = DocumentFixture::Copy(sameContents);
                               ReplaceDocument(std::move(replacement).Seal(), targetId, {});
                               return _session.DocumentSerial() != serial && _session.ActiveLayerID() == targetId &&
                                      _session.Document().value() == sameContents;
                           }))
        return fail(L"A same-ID document replacement accepted a late display publication.\n");

    if (!stalePreparedCase("history.reset-refusal",
                           [this]
                           {
                               _session.history.Reset();
                               return true;
                           }))
        return fail(L"A publication prepared under an old history epoch was accepted.\n");

    if (!stalePreparedCase("sibling.metadata-refusal",
                           [this]
                           {
                               const Guid currentSibling = _session.Document()->layers[1].id;
                               _session.EditForTest(
                                   [currentSibling](CanvasDocument& document)
                                   {
                                       auto fixture = DocumentFixture::Copy(document);
                                       for (auto& layer : fixture.layers)
                                           if (layer.id == currentSibling)
                                           {
                                               layer.name = L"Changed sibling metadata";
                                               layer.opacity = 0.5;
                                           }
                                       document = std::move(fixture).Seal();
                                   });
                               return _session.Document()->layers[1].name == L"Changed sibling metadata" &&
                                      _session.Document()->layers[1].opacity == 0.5;
                           }))
        return fail(L"A sibling metadata change accepted a stale full-display publication.\n");

    if (!prepare())
        return fail(L"The source metadata reoffer fixture did not open.\n");
    _colorWorkTestGate->afterPreparation = true;
    if (!start(1.0) || !waitEntered())
        return fail(L"The source metadata reoffer fixture did not prepare its first result.\n");
    const auto reofferGate = _colorWorkTestGate;
    const auto precedingTicket = _colorPreviewTicket;
    const auto panelOpening = _session.PendingColorEdit()->openingSequence;
    const auto panelSequence = _session.ColorEditSequence();
    _session.EditForTest(
        [](CanvasDocument& document)
        {
            auto fixture = DocumentFixture::Copy(document);
            fixture.layers[1].name = L"Current source sibling";
            fixture.layers[1].opacity = 0.5;
            document = std::move(fixture).Seal();
        });
    DocumentChanged();
    const auto* duringChange = _session.DisplayDocument();
    const bool boundedPending = _colorPreviewTicket == precedingTicket && _colorPreviews.HasPending() &&
                                reofferGate->visits.load() == 1 && !_session.ColorLookupPreviewReady() &&
                                duringChange && duringChange->layers[1].name == L"Current source sibling" &&
                                duringChange->layers[1].opacity == 0.5;
    (void)SetEvent(reofferGate->release.get());
    if (!settle())
        return fail(L"The source metadata reoffer did not acknowledge both records.\n");
    const auto* currentDisplay = _session.DisplayDocument();
    if (!expect("source.metadata-change-reoffers-current-display",
                boundedPending && reofferGate->visits.load() == 2 && hasRetiredOffOwner() && currentDisplay &&
                    currentDisplay->layers[1].name == L"Current source sibling" &&
                    currentDisplay->layers[1].opacity == 0.5 &&
                    currentDisplay->layers[1].asset->image == siblingImage && _session.ColorLookupPreviewReady() &&
                    _session.PendingColorEdit()->openingSequence == panelOpening &&
                    _session.PendingColorEdit()->previewSequence == panelSequence &&
                    _session.ActiveLayer()->asset->image == source && _session.history.UndoCount() == 0))
        return fail(L"A changed source was not rebuilt with its current fields and the same panel settings.\n");

    // CPU preparation owns no GPU resources. Rebuilding the application's actual WARP device before or after
    // preparation must preserve the admitted CPU authority and render the eventual preview on the new device.
    const auto deviceCase = [this, &prepare, &start, &waitEntered, &settle, &hasRetiredOffOwner, &expect, &source,
                             &siblingImage, &expectedDisplay](bool afterPreparation) noexcept
    {
        if (!prepare())
            return false;
        _colorWorkTestGate->afterPreparation = afterPreparation;
        if (!start(1.0) || !waitEntered())
            return false;
        const auto gate = _colorWorkTestGate;
        const auto sequence = _session.ColorEditSequence();
        const auto revision = _session.history.CurrentRevision();
        const auto generation = _renderer.DeviceGeneration();
        if (FAILED(_renderer.RecoverDeviceForTest()))
            return false;
        BindCompositionDevice();
        const bool rebound = _renderer.DeviceGeneration() > generation && _composition.Device() == _renderer.Device() &&
                             SUCCEEDED(_gpuBindingError);
        (void)SetEvent(gate->release.get());
        if (!settle())
            return false;
        const auto* shown = _session.DisplayDocument();
        std::vector<std::uint8_t> pixels;
        UINT columns = 0, rows = 0;
        bool matches = shown && SUCCEEDED(_composition.Compose(*shown)) &&
                       SUCCEEDED(_composition.Readback(pixels, columns, rows)) && columns == 4 && rows == 1 &&
                       pixels.size() == expectedDisplay.size();
        if (matches)
            for (std::size_t index = 0; index < expectedDisplay.size(); ++index)
                if (std::abs(static_cast<int>(pixels[index]) - expectedDisplay[index]) > (index % 4 == 3 ? 0 : 2))
                    matches = false;
        return expect(afterPreparation ? "device.ready-publication-survives-recovery"
                                       : "device.running-preparation-survives-recovery",
                      rebound && matches && gate->visits.load() == 1 && hasRetiredOffOwner() &&
                          _session.PendingColorEdit()->previewSequence == sequence &&
                          _session.ActiveLayer()->asset->image == source &&
                          shown->layers[1].asset->image == siblingImage &&
                          _session.history.CurrentRevision() == revision && _session.history.UndoCount() == 0);
    };
    if (!deviceCase(false) || !deviceCase(true))
        return fail(L"A device recovery invalidated CPU color authority or failed to render the prepared display.\n");

    if (!prepare(kMaxLayerCount - 2))
        return fail(L"The maximum-record display fixture did not open.\n");
    const auto committedRecords = _session.Document()->layers;
    const auto metadataRevision = _session.history.CurrentRevision();
    _colorWorkTestGate->afterPreparation = true;
    if (!start(1.0) || !waitEntered())
        return fail(L"The maximum-record display fixture did not prepare on the worker.\n");
    const auto metadataGate = _colorWorkTestGate;
    const bool invisibleUntilComplete = _session.DisplayDocument()->layers.Identity() == committedRecords.Identity();
    (void)SetEvent(metadataGate->release.get());
    if (!settle())
        return fail(L"The maximum-record display fixture did not acknowledge.\n");
    const auto* metadataDisplay = _session.DisplayDocument();
    bool untouched = metadataDisplay && metadataDisplay->layers.size() == kMaxLayerCount &&
                     _session.Document()->layers.Identity() == committedRecords.Identity();
    if (untouched)
        for (std::size_t index = 1; index < committedRecords.size(); ++index)
            if (metadataDisplay->layers[index] != committedRecords[index])
                untouched = false;
    if (!expect("metadata.maximum-records-complete-publication",
                invisibleUntilComplete && untouched && metadataGate->captureReservationHeld.load() &&
                    metadataGate->visits.load() == 1 && hasRetiredOffOwner() &&
                    _session.history.CurrentRevision() == metadataRevision && _session.history.UndoCount() == 0))
        return fail(L"Maximum-record display preparation changed committed or unrelated records.\n");

    std::fprintf(stderr, "PASS color-display-host.summary checks=%d\n", checks);
    return 0;
}
