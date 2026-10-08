// This runner follows
// RedPrism/Application.Tests.CloneReadback.cpp::RunStandaloneCloneReadbackShutdownSelfTest.
// It uses the existing hidden WARP host, ApplicationShutdownTestProbe, and real Application::Shutdown path.
#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationShutdownTestProbe.h"
#include "BrushStroke.h"
#include "ChromeHost.h"
#include "CloneReadbackTestGate.h"
#include "DeferredRelease.h"
#include "KeyboardState.h"
#include "ModalProbePrompts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

namespace
{
using ClonePixel = std::array<std::uint8_t, 4>;

const ImageLayer* FindFixtureLayer(const DocumentSession& session, const Guid& id) noexcept
{
    const auto& document = session.Document();
    if (document)
        for (const ImageLayer& layer : document->layers)
            if (layer.id == id)
                return &layer;
    return nullptr;
}

ClonePixel ReadCloneLayerPixel(const ImageLayer* layer, int x, int y) noexcept
{
    ClonePixel pixel{};
    if (layer && layer->asset)
    {
        const int localX = static_cast<int>(std::lround(static_cast<double>(x) - layer->transform.origin.x));
        const int localY = static_cast<int>(std::lround(static_cast<double>(y) - layer->transform.origin.y));
        layer->asset->image.ReadRegion(localX, localY, 1, 1, pixel.data(), 4, 0);
    }
    return pixel;
}
} // namespace

int Application::RunStandaloneCloneLayerSourceShutdownSelfTest(HINSTANCE instance) noexcept
{
    KeyboardState::UseSynthetic(true);
    const auto keyboard = wil::scope_exit([]() noexcept { KeyboardState::UseSynthetic(false); });
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool value, const char* scenario, const char* fact) noexcept
    {
        ++checks;
        failures += value ? 0u : 1u;
        std::printf("%s clone-layer-source-shutdown.%s.%s\n", value ? "PASS" : "FAIL", scenario, fact);
    };
    const auto pointer = [](Application& owner, UINT message, WPARAM modifiers, Vec2 point) noexcept
    {
        const auto hole = owner._chrome->HolePixels();
        const auto view = owner._viewport.ViewFromDocument(point, owner.DocumentSize());
        owner.OnPointer(message, modifiers,
                        MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                   static_cast<int>(std::lround(hole.top + view.y))));
    };

    for (const bool live : std::array{false, true})
    {
        const char* const scenario = live ? "adopted-live-lease" : "worker-copy-held";
        expect(!DeferredReleaseDetail::sink.enqueue, scenario, "isolated-retirement-sink-starts-empty");
        ApplicationShutdownTestProbe probe;
        auto gate = std::make_shared<CloneReadbackTestGate>();
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

        constexpr int width = 16;
        constexpr int height = 12;
        constexpr ClonePixel red{0, 0, 255, 255};
        constexpr ClonePixel blue{255, 0, 0, 255};
        constexpr ClonePixel green{0, 255, 0, 255};
        std::vector<std::uint8_t> pixels(width * height * 4);
        std::vector<std::uint8_t> overlayPixels(width * height * 4);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto offset = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4;
                const auto& color = x < 8 ? red : blue;
                std::copy(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
                std::copy(green.begin(), green.end(), overlayPixels.begin() + static_cast<std::ptrdiff_t>(offset));
            }
        DocumentFixture fixture;
        fixture.id = Guid::Generate();
        fixture.width = width;
        fixture.height = height;
        ImageLayer source = ImageLayer::Blank(L"Clone active source", {width, height});
        source.transform.sampling = LayerSampling::Nearest;
        source.asset = RasterAsset{
            Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, source.name};
        const Guid sourceId = source.id;
        fixture.layers.push_back(std::move(source));
        ImageLayer overlay = ImageLayer::Blank(L"Unrelated green overlay", {width, height});
        overlay.transform.sampling = LayerSampling::Nearest;
        overlay.asset =
            RasterAsset{Raster::Make(width, height, RasterFormat::Bgra8888, overlayPixels.data(), overlayPixels.size()),
                        {},
                        overlay.name};
        const Guid overlayId = overlay.id;
        fixture.layers.push_back(std::move(overlay));
        owner->ReplaceDocument(std::move(fixture).Seal(), sourceId, {});
        owner->_tool = EditorTool::Clone;
        owner->_session.brushSettings = BrushSettings{1, 1, 0, 0, 0, 1};
        owner->_session.cloneSettings = CloneSettings{false, false};
        owner->_session.SetCloneSource({4, 6});
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        owner->SyncChrome();
        const auto hole = owner->_chrome->HolePixels();
        const auto origin = owner->_viewport.ViewFromDocument({}, owner->DocumentSize());
        owner->_viewport.pan.x += std::round(hole.left + origin.x) - (hole.left + origin.x);
        owner->_viewport.pan.y += std::round(hole.top + origin.y) - (hole.top + origin.y);
        const Guid revision = owner->_session.history.CurrentRevision();
        gate->holdAfterCopy = true;
        gate->observeAdoption = live;
        owner->_cloneReadbackTestGate = gate;
        pointer(*owner, WM_LBUTTONDOWN, MK_LBUTTON, {12, 6});
        const auto receipt = owner->_canvasBrushReceipt;
        const auto* const accepted = owner->_hostRequests.PendingAt(0);
        const std::uint64_t ordinal = accepted ? accepted->ordinal : 0;
        const std::uint64_t opening = owner->_canvasBrushInput.Opening();

        struct State final
        {
            Application& owner;
            const std::shared_ptr<CloneReadbackTestGate> gate;
            bool live;
            std::uint64_t ordinal;
            std::uint64_t opening;
            std::optional<JobTicket> ticket;
            std::optional<HistoryWorkStamp> stamp;
            std::uint64_t reservation = 0;
            std::uint64_t openingBytes = 0;
            bool workerHeld = false;
            bool adopted = false;
            bool reached = false;
        } state{*owner, gate, live, ordinal, opening};
        const auto dispatch = [](void* opaque, void* native) noexcept
        {
            auto& state = *static_cast<State*>(opaque);
            const auto& message = *static_cast<MSG*>(native);
            if (message.message == CloneReadbackTestGate::kMessage && message.wParam == 1)
            {
                state.ticket = state.owner._utilityTicket;
                state.stamp = state.owner._utilityStamp;
                state.reservation = state.owner._jobs.ReservedMemoryBytes();
                state.workerHeld =
                    state.ordinal != 0 && state.opening != 0 && state.ticket && state.stamp &&
                    state.owner._activeHostRequest && state.owner._activeHostRequest->ordinal == state.ordinal &&
                    state.owner._canvasBrushInput.Opening() == state.opening && state.owner._historyWork.Busy() &&
                    state.reservation != 0 && state.owner._utilityTicket == state.ticket &&
                    state.owner._utilityStamp == state.stamp && state.owner._session.history.UndoCount() == 0;
                if (state.live)
                    (void)SetEvent(state.gate->release.get());
                else
                    state.reached = true;
                return;
            }
            if (message.message == CloneReadbackTestGate::kMessage && message.wParam == 2)
            {
                state.adopted = true;
                return;
            }
            if (message.message == CloneReadbackTestGate::kMessage && message.wParam == 3)
            {
                const auto* draft = state.owner._session.PendingBrush();
                state.openingBytes = draft ? draft->OpeningRetainedMemoryBytes() : 0;
                state.reached = state.live;
                return;
            }
            state.owner.DispatchQueuedMessage(*static_cast<MSG*>(native));
        };
        const ULONGLONG deadline = GetTickCount64() + 10000;
        while (!state.reached)
        {
            if (PumpMessages(dispatch, &state).quit)
                std::_Exit(5);
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (state.reached)
                break;
            if (owner->_jobs.HasUiWork())
                continue;
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline)
                std::_Exit(5);
            const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
            if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                std::_Exit(5);
        }

        const bool chargedBoundary =
            state.workerHeld && state.ordinal == ordinal && state.opening == opening && state.ticket.has_value() &&
            state.stamp.has_value() && state.reservation != 0 && owner->_activeHostRequest &&
            owner->_activeHostRequest->ordinal == ordinal && owner->_canvasBrushInput.Opening() == opening &&
            receipt.Read().outcome == HostOutcome::Pending && gate->captures.load() == 1 &&
            owner->_jobs.ReservedMemoryBytes() == state.reservation + state.openingBytes &&
            (live ? state.adopted && owner->_canvasCloneSample && owner->_canvasCloneSample->lease &&
                        owner->_canvasCloneSample->lease.Bytes() == state.reservation &&
                        owner->_session.PendingBrush() && state.openingBytes != 0
                  : owner->_utilityTicket == state.ticket && owner->_utilityStamp == state.stamp &&
                        owner->_historyWork.Busy() && !owner->_canvasCloneSample && !owner->_session.PendingBrush());
        expect(chargedBoundary, scenario, "original-ordinal-opening-ticket-stamp-and-charge-retained");
        const auto* const sourceBeforeShutdown = FindFixtureLayer(owner->_session, sourceId);
        const auto* const overlayBeforeShutdown = FindFixtureLayer(owner->_session, overlayId);
        expect(ReadCloneLayerPixel(sourceBeforeShutdown, 4, 6) == red &&
                   ReadCloneLayerPixel(sourceBeforeShutdown, 12, 6) == blue &&
                   ReadCloneLayerPixel(overlayBeforeShutdown, 4, 6) == green &&
                   ReadCloneLayerPixel(overlayBeforeShutdown, 12, 6) == green &&
                   owner->_session.history.CurrentRevision() == revision && owner->_session.history.UndoCount() == 0 &&
                   gate->preparations.load() == 0 && gate->recoveries.load() == 0 && owner->_gpuWork.LentCount() == 0,
               scenario, "literal-red-blue-green-pixels-history-and-no-gpu-preparation-before-shutdown");

        probe.draft = owner->_session.WeakPendingBrushForTest();
        struct DetachState final
        {
            Application& owner;
            ApplicationShutdownTestProbe& probe;
            bool live;
            Guid revision;
            Guid sourceId;
            Guid overlayId;
            std::uint64_t reservation;
            std::uint64_t sourceReservation;
            bool pixelsAndHistoryUnchanged = false;
            bool chargeRetained = false;
            bool liveDraftAlive = false;
        } detach{*owner,           probe, live, revision, sourceId, overlayId, state.reservation + state.openingBytes,
                 state.reservation};
        probe.checkContext = &detach;
        probe.checkOwner = [](void* opaque) noexcept
        {
            auto& state = *static_cast<DetachState*>(opaque);
            const auto* source = FindFixtureLayer(state.owner._session, state.sourceId);
            const auto* overlay = FindFixtureLayer(state.owner._session, state.overlayId);
            state.pixelsAndHistoryUnchanged = ReadCloneLayerPixel(source, 4, 6) == ClonePixel{0, 0, 255, 255} &&
                                              ReadCloneLayerPixel(source, 12, 6) == ClonePixel{255, 0, 0, 255} &&
                                              ReadCloneLayerPixel(overlay, 4, 6) == ClonePixel{0, 255, 0, 255} &&
                                              ReadCloneLayerPixel(overlay, 12, 6) == ClonePixel{0, 255, 0, 255} &&
                                              state.owner._session.history.CurrentRevision() == state.revision &&
                                              state.owner._session.history.UndoCount() == 0;
            state.chargeRetained =
                state.owner._jobs.ReservedMemoryBytes() == state.reservation && state.reservation != 0 &&
                (!state.live || (state.owner._canvasCloneSample && state.owner._canvasCloneSample->lease &&
                                 state.owner._canvasCloneSample->lease.Bytes() == state.sourceReservation &&
                                 state.reservation > state.sourceReservation));
            state.liveDraftAlive = state.live && state.probe.draftAliveAtDetach && !state.probe.draft.expired();
        };
        owner->_shutdownTestProbe = &probe;
        owner->Shutdown(); // Actual shutdown suppresses completion callbacks and joins the gated worker.
        expect(receipt.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT} &&
                   detach.pixelsAndHistoryUnchanged,
               scenario, "shutdown-cancels-original-receipt-with-literal-pixels-and-history-unchanged");
        expect(detach.chargeRetained && probe.graphicsDetachedOnOwner && probe.reservationAndWakeAlive &&
                   (live ? detach.liveDraftAlive : probe.draft.expired()),
               scenario, "charge-and-live-draft-retained-at-owner-detach");
        expect(gate->retired.load() && gate->retiredOffOwner.load() && probe.workerCalls.load() == 1 &&
                   probe.offOwner.load() && probe.mta.load() && probe.draft.expired() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   owner->_jobs.IsIdle() && !owner->_canvasCloneSample && !owner->_utilityTicket &&
                   !owner->_utilityStamp && !owner->_historyWork.Busy() && !owner->_gpuWork.HasPending(),
               scenario, "joined-worker-retires-draft-lease-and-entire-reservation");
        owner->_shutdownTestProbe = nullptr;
        owner.reset();
        expect(!DeferredReleaseDetail::sink.enqueue, scenario, "retirement-sink-cleared-after-owner-destruction");
        MSG quit{};
        unsigned int quitCount = 0;
        while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE))
            ++quitCount;
        expect(quitCount == 1, scenario, "isolated-owner-quit-drained");
    }
    std::printf("clone-layer-source-shutdown summary: %u assertions, %u failures\n", checks, failures);
    return checks == 16 && failures == 0 ? 0 : 5;
}
