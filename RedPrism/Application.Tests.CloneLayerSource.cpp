#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "CloneReadbackTestGate.h"
#include "KeyboardState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Pixel = std::array<std::uint8_t, 4>; // BGRA

constexpr Pixel kRed{0, 0, 255, 255};
constexpr Pixel kGreen{0, 255, 0, 255};
constexpr Pixel kBlue{255, 0, 0, 255};

const ImageLayer* FindFixtureLayer(const DocumentSession& session, const Guid& id) noexcept
{
    const auto& document = session.Document();
    if (document)
        for (const ImageLayer& layer : document->layers)
            if (layer.id == id)
                return &layer;
    return nullptr;
}

DocumentFixture CloneLayerSourceFixture(bool greenOverlay, Guid& targetId, Guid& overlayId)
{
    constexpr int width = 16;
    constexpr int height = 12;
    std::vector<std::uint8_t> activePixels(static_cast<std::size_t>(width * height * 4));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const Pixel color = x < 8 ? kRed : kBlue;
            const std::size_t offset = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
            std::copy(color.begin(), color.end(), activePixels.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }

    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = width;
    fixture.height = height;
    ImageLayer active = ImageLayer::Blank(L"Clone active source", {width, height});
    active.transform.sampling = LayerSampling::Nearest;
    active.asset = RasterAsset{
        Raster::Make(width, height, RasterFormat::Bgra8888, activePixels.data(), activePixels.size()), {}, active.name};
    targetId = active.id;
    fixture.layers.push_back(std::move(active));

    if (greenOverlay)
    {
        std::vector<std::uint8_t> overlayPixels(static_cast<std::size_t>(width * height * 4));
        for (std::size_t i = 0; i < overlayPixels.size(); i += 4u)
            std::copy(kGreen.begin(), kGreen.end(), overlayPixels.begin() + static_cast<std::ptrdiff_t>(i));
        ImageLayer overlay = ImageLayer::Blank(L"Unrelated opaque overlay", {width, height});
        overlayId = overlay.id;
        overlay.transform.sampling = LayerSampling::Nearest;
        overlay.asset =
            RasterAsset{Raster::Make(width, height, RasterFormat::Bgra8888, overlayPixels.data(), overlayPixels.size()),
                        {},
                        overlay.name};
        fixture.layers.push_back(std::move(overlay));
    }
    return fixture;
}
} // namespace

// The worker-source job renders the
// captured active image with its captured DisplayedTransform into a full-canvas Raster before this gate is reached.
int Application::RunCloneLayerSourceSelfTest() noexcept
{
    const auto originalTool = _tool;
    const auto originalViewport = _viewport;
    const auto originalBrushSettings = _session.brushSettings;
    const auto originalCloneSettings = _session.cloneSettings;
    const auto originalCloneSource = _session.cloneSource;
    const bool originalSpacePan = _spacePan;
    std::array<BYTE, 256> originalKeyboard{};
    const bool keyboardCaptured = KeyboardState::Read(originalKeyboard);
    // RunSelfTest already owns the synthetic keyboard; preserve its mode and restore only this fixture's keys.
    KeyboardState::Write({});

    struct Mode final
    {
        const char* name;
        bool liveLease;
        bool cancel;
        bool fail;
        bool stale;
        bool captureLoss;
    };
    constexpr std::array<Mode, 6> modes{{
        {"success", false, false, false, false, false},
        {"cancel", false, true, false, false, false},
        {"failure", false, false, true, false, false},
        {"stale", false, false, false, true, false},
        {"live-lease", true, false, false, false, false},
        {"capture-loss", false, false, false, false, true},
    }};

    int assertions = 0;
    int failures = 0;
    const auto expect = [&assertions, &failures](const char* mode, const char* name, bool condition) noexcept
    {
        ++assertions;
        failures += condition ? 0 : 1;
        std::printf("%s clone-layer-source.%s.%s\n", condition ? "PASS" : "FAIL", mode, name);
    };
    const auto settle = [this](auto dispatch, void* context) noexcept
    { return SettleJobs(_jobs, 15000, dispatch, context) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_cloneReadbackTestGate)
                (void)SetEvent(_cloneReadbackTestGate->release.get());
            CancelHostContinuations();
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            if (_session.PendingBrush())
            {
                _session.CancelBrush();
                _composition.EndStroke();
                _previewSource.reset();
            }
            _cloneReadbackTestGate.reset();
            _canvasCloneSample.reset();
            _tool = originalTool;
            _viewport = originalViewport;
            _session.brushSettings = originalBrushSettings;
            _session.cloneSettings = originalCloneSettings;
            _session.cloneSource = originalCloneSource;
            _spacePan = originalSpacePan;
            if (keyboardCaptured)
                KeyboardState::Write(originalKeyboard);
        });

    const auto install = [this](Guid& targetId, Guid& overlayId) noexcept
    {
        auto fixture = CloneLayerSourceFixture(true, targetId, overlayId);
        ReplaceDocument(std::move(fixture).Seal(), targetId, {});
        _tool = EditorTool::Clone;
        _spacePan = false;
        _drag = PointerDrag{};
        _canvasPanLast.reset();
        // A diameter-three hard tip fully covers the (12.5,6.5) pixel center from the integer (12,6) press.
        // A diameter-one tip is only partially opaque there; the literal reference remains full red.
        _session.brushSettings = BrushSettings{3, 1, 0, 0, 0, 1};
        _session.cloneSettings = CloneSettings{false, false}; // Unaligned, active layer only.
        _session.SetCloneSource({4, 6});
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        DocumentChanged();
        SyncChrome();
        // Pointer messages contain whole client pixels; align the document grid after chrome layout.
        const auto hole = _chrome->HolePixels();
        const auto origin = _viewport.ViewFromDocument({}, DocumentSize());
        _viewport.pan.x += std::round(hole.left + origin.x) - (hole.left + origin.x);
        _viewport.pan.y += std::round(hole.top + origin.y) - (hole.top + origin.y);
    };
    const auto pointer = [](Application& owner, UINT message, WPARAM modifiers, Vec2 pixel) noexcept
    {
        const auto hole = owner._chrome->HolePixels();
        const auto view = owner._viewport.ViewFromDocument(pixel, owner.DocumentSize());
        owner.OnPointer(message, modifiers,
                        MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                   static_cast<int>(std::lround(hole.top + view.y))));
    };
    const auto activePixel = [this](int x, int y) noexcept
    {
        Pixel pixel{};
        const ImageLayer* const active = _session.ActiveLayer();
        if (active && active->asset)
        {
            const int localX = static_cast<int>(std::lround(static_cast<double>(x) - active->transform.origin.x));
            const int localY = static_cast<int>(std::lround(static_cast<double>(y) - active->transform.origin.y));
            active->asset->image.ReadRegion(localX, localY, 1, 1, pixel.data(), 4, 0);
        }
        return pixel;
    };

    for (const Mode& mode : modes)
    {
        if (!settle(DispatchMessageThunk, this))
            return 5;
        Guid targetId{};
        Guid overlayId{};
        install(targetId, overlayId);
        const Guid revision = _session.history.CurrentRevision();
        const std::uint64_t presentCount = _renderer.PresentCount();
        const Pixel targetBefore = activePixel(12, 6);
        const Pixel sourceBefore = activePixel(4, 6);

        auto gate = std::make_shared<CloneReadbackTestGate>();
        if (!gate->release)
            return 5;
        gate->holdAfterCopy = true;
        gate->observeAdoption = mode.liveLease;
        if (mode.fail)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        _cloneReadbackTestGate = gate;

        struct State final
        {
            Application& owner;
            const Mode& mode;
            std::shared_ptr<CloneReadbackTestGate> gate;
            std::optional<JobTicket> ticket;
            std::optional<HistoryWorkStamp> stamp;
            std::uint64_t ordinal = 0;
            std::uint64_t opening = 0;
            std::uint64_t reservation = 0;
            std::uint64_t presents = 0;
            Guid revision{};
            Guid targetId{};
            Guid overlayId{};
            Pixel targetBefore{};
            Pixel sourceBefore{};
            bool copied = false;
            bool held = false;
            bool responsive = false;
            bool modeChangedWhileHeld = false;
            bool leaseAdopted = false;
            bool captureLost = false;
        } state{*this,        mode,     gate,     {},        {},           0,           0, 0,
                presentCount, revision, targetId, overlayId, targetBefore, sourceBefore};

        const auto dispatch = [](void* opaque, void* native) noexcept
        {
            auto& state = *static_cast<State*>(opaque);
            auto& owner = state.owner;
            auto& message = *static_cast<MSG*>(native);
            if (message.message != CloneReadbackTestGate::kMessage)
            {
                owner.DispatchQueuedMessage(message);
                return;
            }

            const auto receipt = owner._canvasBrushReceipt.Read();
            if (message.wParam == 2)
            {
                state.leaseAdopted =
                    state.mode.liveLease && owner._canvasCloneSample && owner._canvasCloneSample->lease &&
                    owner._canvasCloneSample->lease.Bytes() == state.reservation &&
                    owner._canvasCloneSample->opening == state.opening &&
                    owner._jobs.ReservedMemoryBytes() == state.reservation && !owner._utilityTicket &&
                    !owner._utilityStamp && owner._session.PendingBrush() && receipt.outcome == HostOutcome::Pending &&
                    owner._session.history.CurrentRevision() == state.revision;
                const auto hole = owner._chrome->HolePixels();
                const auto view = owner._viewport.ViewFromDocument({12, 6}, owner.DocumentSize());
                owner.OnPointer(WM_LBUTTONUP, 0,
                                MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                           static_cast<int>(std::lround(hole.top + view.y))));
                return;
            }
            if (message.wParam != 1)
            {
                state.held = false;
                return;
            }
            state.copied = true;
            state.ticket = owner._utilityTicket;
            state.stamp = owner._utilityStamp;
            state.reservation = owner._jobs.ReservedMemoryBytes();
            const auto* const active = owner._session.ActiveLayer();
            const auto* const captured = owner._canvasBrushInput.CapturedContext(state.opening);
            state.held = state.ticket && state.stamp && state.ordinal != 0 && state.reservation != 0 &&
                         owner._utilityTicket == state.ticket && owner._utilityStamp == state.stamp &&
                         owner._historyWork.Busy() && owner._activeHostRequest &&
                         owner._activeHostRequest->ordinal == state.ordinal && active && active->asset &&
                         owner._session.history.CurrentRevision() == state.revision &&
                         owner._session.history.UndoCount() == 0 && !owner._session.PendingBrush() && captured &&
                         !captured->cloneAllLayers && captured->cloneOffset == std::optional<Vec2>(Vec2{-8, 0}) &&
                         owner._gpuWork.LentCount() == 0 && owner._renderer.PresentCount() == state.presents;

            const double zoomBefore = owner._viewport.zoom;
            owner.OnCommand(ChromeHost::kCommandZoomIn);
            state.responsive = owner._viewport.zoom > zoomBefore && owner._utilityTicket == state.ticket &&
                               owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;

            // The admitted request must retain sampleAllLayers=false after this live option changes.
            owner._session.cloneSettings.sampleAllLayers = true;
            state.modeChangedWhileHeld = owner._session.cloneSettings.sampleAllLayers && captured &&
                                         !captured->cloneAllLayers && owner._utilityTicket == state.ticket &&
                                         owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
            if (state.mode.stale)
            {
                owner._session.SelectLayerTarget(state.overlayId, false);
            }
            else if (state.mode.captureLoss)
            {
                (void)SendMessageW(owner._window.get(), WM_CAPTURECHANGED, 0, 0);
                state.captureLost = !owner._canvasBrushInput.Sampling() &&
                                    owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
            }
            else if (state.mode.cancel)
                (void)owner.CancelCanvasBrush();
            (void)SetEvent(state.gate->release.get());
        };

        pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON, {12, 6});
        state.opening = _canvasBrushInput.Opening();
        if (const auto* accepted = _hostRequests.PendingAt(0))
            state.ordinal = accepted->ordinal;
        expect(mode.name, "press-admits-before-capture-without-owner-pixels",
               _canvasBrushInput.PendingCount() == 1 && _canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   !_session.PendingBrush() && _session.history.UndoCount() == 0 && gate->captures.load() == 0 &&
                   !_utilityTicket && !_canvasCloneSample && activePixel(12, 6) == kBlue && activePixel(4, 6) == kRed);
        if (!mode.liveLease && !mode.captureLoss)
            pointer(*this, WM_LBUTTONUP, 0, {12, 6});

        if (!settle(dispatch, &state))
            return 5;
        if (mode.liveLease && _canvasBrushReceipt.Read().outcome == HostOutcome::Pending && !settle(dispatch, &state))
            return 5;

        const auto receipt = _canvasBrushReceipt.Read();
        const HostOutcome expected = mode.cancel  ? HostOutcome::Cancelled
                                     : mode.fail  ? HostOutcome::Failed
                                     : mode.stale ? HostOutcome::Stale
                                                  : HostOutcome::Succeeded;
        const ImageLayer* const activeAfter = _session.ActiveLayer();
        expect(mode.name, "held-worker-preserves-original-ticket-stamp-ordinal-and-charge",
               state.copied && state.held && state.ticket.has_value() && state.stamp.has_value() &&
                   state.ordinal != 0 && state.reservation != 0 && _renderer.PresentCount() == presentCount);
        expect(mode.name, "event-time-mode-is-stable-and-view-remains-responsive",
               state.modeChangedWhileHeld && state.responsive && gate->preparations.load() == 0 &&
                   gate->recoveries.load() == 0);
        expect(mode.name, "one-worker-source-capture-and-one-owner-visit-no-gpu-preparation",
               gate->captures.load() == 1 && gate->visits.load() == 1 && gate->preparations.load() == 0 &&
                   gate->recoveries.load() == 0);
        expect(mode.name, "exact-terminal-outcome-and-no-unintended-history",
               receipt.outcome == expected && (mode.fail ? receipt.error == E_ACCESSDENIED : true) &&
                   (mode.cancel || mode.stale ? receipt.error == E_ABORT : true) &&
                   (mode.stale ? activeAfter && activeAfter->id == overlayId : true) &&
                   (mode.stale || mode.cancel || mode.fail ? _session.history.UndoCount() == 0
                                                           : _session.history.UndoCount() == 1));
        expect(mode.name, "ticket-and-reservation-retire-once-without-hidden-present",
               !_canvasBrushInput.Busy() && !_canvasCloneSample && !_utilityTicket && !_utilityStamp &&
                   !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == 0 && _gpuWork.LentCount() == 0 &&
                   _gpuWork.InFlightCount() == 0 && !_gpuWork.HasPending() && _composition.UtilitySurfaceBytes() == 0 &&
                   _renderer.PresentCount() == presentCount && gate->retired.load() && gate->retiredOffOwner.load());

        const auto* originalTarget = FindFixtureLayer(_session, targetId);
        const auto* originalOverlay = FindFixtureLayer(_session, overlayId);
        Pixel finalTarget{};
        Pixel finalSource{};
        Pixel finalOverlay{};
        if (originalTarget && originalTarget->asset)
        {
            originalTarget->asset->image.ReadRegion(12, 6, 1, 1, finalTarget.data(), 4, 0);
            originalTarget->asset->image.ReadRegion(4, 6, 1, 1, finalSource.data(), 4, 0);
        }
        if (originalOverlay && originalOverlay->asset)
            originalOverlay->asset->image.ReadRegion(12, 6, 1, 1, finalOverlay.data(), 4, 0);
        expect(mode.name, "literal-target-source-and-unrelated-overlay",
               finalTarget == (expected == HostOutcome::Succeeded ? kRed : kBlue) && finalSource == kRed &&
                   finalOverlay == kGreen);

        if (mode.name == std::string_view{"success"})
        {
            expect(mode.name, "single-layer-literal-source-target-and-history-entry",
                   activeAfter && activeAfter->asset && activePixel(12, 6) == kRed &&
                       activePixel(4, 6) == sourceBefore && sourceBefore == kRed && targetBefore == kBlue &&
                       _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Clone Stamp");
            OnCommand(ChromeHost::kCommandUndo);
            expect(mode.name, "undo-restores-blue-target-and-keeps-red-source",
                   settle(DispatchMessageThunk, this) && activePixel(12, 6) == kBlue && activePixel(4, 6) == kRed &&
                       _session.history.UndoCount() == 0 && _session.history.RedoName() == L"Clone Stamp");
            OnCommand(ChromeHost::kCommandRedo);
            expect(mode.name, "redo-restores-red-target-and-one-clone-entry",
                   settle(DispatchMessageThunk, this) && activePixel(12, 6) == kRed && activePixel(4, 6) == kRed &&
                       _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Clone Stamp");
        }
        else if (mode.stale)
            expect(mode.name, "active-layer-switch-rejects-old-source-with-no-pixel-history",
                   activeAfter && activeAfter->id == overlayId && activeAfter->asset && activePixel(4, 6) == kGreen &&
                       activePixel(12, 6) == kGreen && _session.history.UndoCount() == 0);
        else if (mode.liveLease)
            expect(mode.name, "original-charge-transfers-through-live-stroke-final-retirement",
                   state.leaseAdopted && gate->retired.load() && gate->retiredOffOwner.load());
        else if (mode.captureLoss)
            expect(mode.name, "capture-loss-seals-request-before-worker-acknowledgement", state.captureLost);
        else if (mode.cancel)
            expect(mode.name, "cancelled-held-source-publishes-no-pixels",
                   activeAfter && activeAfter->asset && activePixel(12, 6) == targetBefore);
        else if (mode.fail)
            expect(mode.name, "failed-held-source-preserves-target-and-reports-access-denied",
                   activeAfter && activeAfter->asset && activePixel(12, 6) == targetBefore &&
                       receipt.error == E_ACCESSDENIED);
        _cloneReadbackTestGate.reset();
    }

    std::printf("%s clone-layer-source.summary assertions=%d failures=%d cases=6\n", failures == 0 ? "PASS" : "FAIL",
                assertions, failures);
    return failures;
}

// Capture refusal has its own case so the six held-worker scenarios remain independently exercised.
int Application::RunCloneLayerSourceCaptureFailureSelfTest() noexcept
{
    const auto originalTool = _tool;
    const auto originalViewport = _viewport;
    const auto originalBrushSettings = _session.brushSettings;
    const auto originalCloneSettings = _session.cloneSettings;
    const auto originalCloneSource = _session.cloneSource;
    const bool originalSpacePan = _spacePan;
    std::array<BYTE, 256> originalKeyboard{};
    const bool keyboardCaptured = KeyboardState::Read(originalKeyboard);
    KeyboardState::Write({});
    int assertions = 0;
    int failures = 0;
    const auto expectCaptureFailure = [&assertions, &failures](const char* name, bool condition) noexcept
    {
        ++assertions;
        failures += condition ? 0 : 1;
        std::printf("%s clone-layer-source-capture-failure.%s\n", condition ? "PASS" : "FAIL", name);
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_cloneReadbackTestGate)
                (void)SetEvent(_cloneReadbackTestGate->release.get());
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
            _cloneReadbackTestGate.reset();
            _canvasCloneSample.reset();
            _tool = originalTool;
            _viewport = originalViewport;
            _session.brushSettings = originalBrushSettings;
            _session.cloneSettings = originalCloneSettings;
            _session.SetCloneSource(originalCloneSource.value_or(Vec2{}));
            _session.cloneSource = originalCloneSource;
            _spacePan = originalSpacePan;
            if (keyboardCaptured)
                KeyboardState::Write(originalKeyboard);
        });

    Guid targetId{};
    Guid overlayId{};
    auto fixture = CloneLayerSourceFixture(true, targetId, overlayId);
    ReplaceDocument(std::move(fixture).Seal(), targetId, {});
    _tool = EditorTool::Clone;
    _spacePan = false;
    _drag = PointerDrag{};
    _canvasPanLast.reset();
    _session.brushSettings = BrushSettings{1, 1, 0, 0, 0, 1};
    _session.cloneSettings = CloneSettings{false, false};
    _session.SetCloneSource({4, 6});
    _viewport.zoom = 1;
    _viewport.pan = {};
    _viewport.followsFit = false;
    DocumentChanged();
    SyncChrome();
    const auto hole = _chrome->HolePixels();
    const auto origin = _viewport.ViewFromDocument({}, DocumentSize());
    _viewport.pan.x += std::round(hole.left + origin.x) - (hole.left + origin.x);
    _viewport.pan.y += std::round(hole.top + origin.y) - (hole.top + origin.y);

    const auto readPixel = [](const ImageLayer* layer, int x, int y) noexcept
    {
        std::array<std::uint8_t, 4> pixel{};
        if (layer && layer->asset)
        {
            const int localX = static_cast<int>(std::lround(static_cast<double>(x) - layer->transform.origin.x));
            const int localY = static_cast<int>(std::lround(static_cast<double>(y) - layer->transform.origin.y));
            layer->asset->image.ReadRegion(localX, localY, 1, 1, pixel.data(), 4, 0);
        }
        return pixel;
    };
    constexpr std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    constexpr std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    constexpr std::array<std::uint8_t, 4> green{0, 255, 0, 255};
    const Guid revision = _session.history.CurrentRevision();
    auto gate = std::make_shared<CloneReadbackTestGate>();
    if (!gate->release)
        return 5;
    gate->throwCaptureFailure = true; // Existing injected std::bad_alloc before the CPU payload allocation.
    _cloneReadbackTestGate = gate;
    const auto pointer = [this](UINT message, WPARAM modifiers, Vec2 point) noexcept
    {
        const auto clientHole = _chrome->HolePixels();
        const auto view = _viewport.ViewFromDocument(point, DocumentSize());
        OnPointer(message, modifiers,
                  MAKELPARAM(static_cast<int>(std::lround(clientHole.left + view.x)),
                             static_cast<int>(std::lround(clientHole.top + view.y))));
    };
    pointer(WM_LBUTTONDOWN, MK_LBUTTON, {12, 6});
    const auto receipt = _canvasBrushReceipt;
    pointer(WM_LBUTTONUP, 0, {12, 6});
    if (!settle())
        return 5;

    const auto* const active = FindFixtureLayer(_session, targetId);
    const auto* const overlay = FindFixtureLayer(_session, overlayId);
    const auto overlaySource = readPixel(overlay, 4, 6);
    const auto overlayTarget = readPixel(overlay, 12, 6);
    expectCaptureFailure("admitted-capture-fails-eoutofmemory-before-payload-and-copy",
                         receipt.Read() == ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY} &&
                             gate->captures.load() == 0 && gate->visits.load() == 0);
    expectCaptureFailure("literal-active-source-target-and-overlay-remain-red-blue-green",
                         readPixel(active, 4, 6) == red && readPixel(active, 12, 6) == blue && overlay &&
                             overlay->asset && overlaySource == green && overlayTarget == green);
    expectCaptureFailure("capture-failure-preserves-history-revision-and-empty-undo",
                         _session.history.CurrentRevision() == revision && _session.history.UndoCount() == 0);
    expectCaptureFailure("capture-failure-has-no-gpu-preparation-or-recovery",
                         gate->preparations.load() == 0 && gate->recoveries.load() == 0 && _gpuWork.LentCount() == 0 &&
                             _gpuWork.InFlightCount() == 0);
    expectCaptureFailure("capture-failure-retires-ticket-stamp-history-and-memory-charge",
                         !_utilityTicket && !_utilityStamp && !_historyWork.Busy() &&
                             _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0 &&
                             !_canvasCloneSample && !_session.PendingBrush());
    std::printf("clone-layer-source-capture-failure summary assertions=%d failures=%d\n", assertions, failures);
    return assertions == 5 && failures == 0 ? 0 : 5;
}
