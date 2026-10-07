#include "Application.h"

#include "ChromeHost.h"
#include "CloneReadbackTestGate.h"
#include "KeyboardState.h"
#include "LayerMetadataTestGate.h"

#include "../Tests/Support/DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{
using Pixel = std::array<std::uint8_t, 4>; // BGRA
constexpr int kWidth = 16;
constexpr int kHeight = 12;
constexpr Vec2 kCloneSource{4, 6};
constexpr Vec2 kCloneDestination{12, 6};
constexpr Pixel kTransparent{0, 0, 0, 0};
constexpr Pixel kRed{0, 0, 255, 255};
constexpr Pixel kBlue{255, 0, 0, 255};

CanvasDocument MakeSealedCloneDocument(Guid& targetId)
{
    std::vector<std::uint8_t> sourcePixels(static_cast<std::size_t>(kWidth * kHeight * 4));
    std::vector<std::uint8_t> targetPixels(static_cast<std::size_t>(kWidth * kHeight * 4));
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4u;
            const Pixel color = x < 8 ? kRed : kBlue;
            std::copy(color.begin(), color.end(), sourcePixels.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }

    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kWidth;
    fixture.height = kHeight;
    ImageLayer source = ImageLayer::Blank(L"Sealed clone source", {kWidth, kHeight});
    source.transform.sampling = LayerSampling::Nearest;
    source.asset =
        RasterAsset{Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888, sourcePixels.data(), sourcePixels.size()),
                    {},
                    source.name};
    fixture.layers.push_back(std::move(source));

    ImageLayer target = ImageLayer::Blank(L"Sealed clone target", {kWidth, kHeight});
    target.transform.sampling = LayerSampling::Nearest;
    target.asset =
        RasterAsset{Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888, targetPixels.data(), targetPixels.size()),
                    {},
                    target.name};
    targetId = target.id;
    fixture.layers.push_back(std::move(target));
    return std::move(fixture).Seal();
}

bool ReadActivePixel(const DocumentSession& session, int x, int y, Pixel& pixel) noexcept
{
    const ImageLayer* const layer = session.ActiveLayer();
    if (!layer || !layer->asset)
        return false;
    const Raster& image = layer->asset->image;
    const int sourceX = x - static_cast<int>(std::lround(layer->transform.origin.x));
    const int sourceY = y - static_cast<int>(std::lround(layer->transform.origin.y));
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 || sourceX < 0 || sourceY < 0 ||
        sourceX >= image.Width() || sourceY >= image.Height())
        return false;
    image.ReadRegion(sourceX, sourceY, 1, 1, pixel.data(), 4, 0);
    return true;
}
} // namespace

int Application::RunSealedCloneHostSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto originalTool = _tool;
    const auto originalBrushSettings = _session.brushSettings;
    const auto originalCloneSettings = _session.cloneSettings;
    const auto originalCloneSource = _session.cloneSource;
    const bool originalSpacePan = _spacePan;
    std::array<BYTE, 256> originalKeyboard{};
    const bool keyboardCaptured = KeyboardState::Read(originalKeyboard);
    std::array<BYTE, 256> noKeys{};
    KeyboardState::Write(noKeys);

    int failures = 0;
    const auto expect = [&failures](const char* scenario, const char* control, bool condition) noexcept
    {
        std::fprintf(stderr, "%s sealed-clone-host.%s.%s\n", condition ? "PASS" : "FAIL", scenario, control);
        failures += condition ? 0 : 1;
        return condition;
    };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalTool, originalBrushSettings, originalCloneSettings, originalCloneSource,
         originalSpacePan, keyboardCaptured, &originalKeyboard]() noexcept
        {
            if (_cloneReadbackTestGate)
                (void)SetEvent(_cloneReadbackTestGate->release.get());
            if (_layerMetadataTestGate && _layerMetadataTestGate->release)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            CancelHostContinuations();
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            _cloneReadbackTestGate.reset();
            _layerMetadataTestGate.reset();
            _canvasCloneSample.reset();
            _opacityEdit.reset();
            _opacityRecorded.reset();
            _canvasBrushReceipt = {};
            _viewport = originalViewport;
            _tool = originalTool;
            _session.brushSettings = originalBrushSettings;
            _session.cloneSettings = originalCloneSettings;
            _session.cloneSource = originalCloneSource;
            _spacePan = originalSpacePan;
            if (keyboardCaptured)
                KeyboardState::Write(originalKeyboard);
        });

    const auto clientAt = [this](Vec2 point) noexcept
    {
        const auto hole = _chrome->HolePixels();
        const auto view = _viewport.ViewFromDocument(point, DocumentSize());
        return MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                          static_cast<int>(std::lround(hole.top + view.y)));
    };
    const auto pointer = [this, &clientAt](UINT message, WPARAM modifiers, Vec2 point) noexcept
    { OnPointer(message, modifiers, clientAt(point)); };
    const auto findCloneOrdinal = [](const Application& owner,
                                     std::uint64_t opening) noexcept -> std::optional<std::uint64_t>
    {
        if (owner._activeHostRequest)
        {
            const auto* const input = std::get_if<HostCanvasBrushInput>(&owner._activeHostRequest->input.Input());
            if (input && input->kind == HostCanvasInputKind::Brush && input->opening == opening)
                return owner._activeHostRequest->ordinal;
        }
        for (std::size_t index = 0; index < owner._hostRequests.Size(); ++index)
        {
            const auto* const entry = owner._hostRequests.PendingAt(index);
            const auto* const input = entry ? std::get_if<HostCanvasBrushInput>(&entry->input.Input()) : nullptr;
            if (entry && input && input->kind == HostCanvasInputKind::Brush && input->opening == opening)
                return entry->ordinal;
        }
        return std::nullopt;
    };
    using FindCloneOrdinal = std::optional<std::uint64_t> (*)(const Application&, std::uint64_t) noexcept;
    const auto installCloneFixture = [this]() noexcept
    {
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
            return false;
        Guid target{};
        CanvasDocument document = MakeSealedCloneDocument(target);
        ReplaceDocument(std::move(document), target, {});
        _tool = EditorTool::Clone;
        _spacePan = false;
        _drag = PointerDrag{};
        _canvasPanLast.reset();
        _session.brushSettings = BrushSettings{3, 1, 0, 0, 0, 1};
        _session.cloneSettings = CloneSettings{true, true}; // Aligned, Sample All Layers.
        _session.SetCloneSource(kCloneSource);
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        DocumentChanged();
        SyncChrome();
        const auto hole = _chrome->HolePixels();
        const auto origin = _viewport.ViewFromDocument({}, DocumentSize());
        _viewport.pan.x += std::round(hole.left + origin.x) - (hole.left + origin.x);
        _viewport.pan.y += std::round(hole.top + origin.y) - (hole.top + origin.y);
        Pixel source{};
        Pixel targetPixel{};
        const auto& fixtureDocument = _session.Document();
        bool sourceIsRed = false;
        if (fixtureDocument && fixtureDocument.value().layers.size() == 2 &&
            fixtureDocument.value().layers.front().asset)
        {
            fixtureDocument.value().layers.front().asset->image.ReadRegion(
                static_cast<int>(kCloneSource.x), static_cast<int>(kCloneSource.y), 1, 1, source.data(), 4, 0);
            sourceIsRed = source == kRed;
        }
        return _session.ActiveLayer() && _session.history.UndoCount() == 0 && sourceIsRed &&
               ReadActivePixel(_session, static_cast<int>(kCloneDestination.x), static_cast<int>(kCloneDestination.y),
                               targetPixel) &&
               targetPixel == kTransparent;
    };

    enum class SealedAction
    {
        Escape,
        Undo,
    };
    struct CloneGateState final
    {
        explicit CloneGateState(Application& application, SealedAction requested) noexcept
            : owner(application), action(requested)
        {
        }
        Application& owner;
        SealedAction action;
        std::shared_ptr<CloneReadbackTestGate> gate;
        ContinuationReceipt strokeReceipt;
        ContinuationReceipt undoReceipt;
        ContinuationReceipt youngerOne;
        ContinuationReceipt youngerTwo;
        JobTicket workerTicket{};
        std::uint64_t opening = 0;
        std::uint64_t strokeOrdinal = 0;
        std::uint64_t undoOrdinal = 0;
        FindCloneOrdinal findOrdinal = nullptr;
        LPARAM upPosition = 0;
        bool gateVisited = false;
        bool originalWorkerHeld = false;
        bool upSealed = false;
        bool policyPreservedOriginal = false;
        bool youngerCancelled = false;
        bool undoQueuedBehindStroke = false;
    };
    const auto cloneGatePump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<CloneGateState*>(opaque);
        Application& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != CloneReadbackTestGate::kMessage || message.wParam != 1)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        if (state.gateVisited)
            return; // Do not replay Up or Escape/Undo if this test gate message is delivered again.
        state.gateVisited = true;
        state.originalWorkerHeld = state.gate->visits.load() == 1 && state.gate->captures.load() == 1 &&
                                   owner._utilityTicket && owner._historyWork.Busy() &&
                                   owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                                   owner._canvasBrushInput.Busy() && owner._canvasBrushInput.Sampling() &&
                                   !owner._session.PendingBrush() && owner._session.history.UndoCount() == 0 &&
                                   WaitForSingleObject(state.gate->release.get(), 0) == WAIT_TIMEOUT;
        if (owner._utilityTicket)
            state.workerTicket = owner._utilityTicket.value();

        owner.OnPointer(WM_LBUTTONUP, 0, state.upPosition);
        const auto ordinal = state.findOrdinal(owner, state.opening);
        state.strokeOrdinal = ordinal.value_or(0);
        const auto* const active = owner._activeHostRequest
                                       ? std::get_if<HostCanvasBrushInput>(&owner._activeHostRequest->input.Input())
                                       : nullptr;
        state.upSealed = ordinal && owner._canvasBrushInput.Opening() == state.opening &&
                         owner._canvasBrushInput.Busy() && !owner._canvasBrushInput.Sampling() &&
                         owner._canvasBrushInput.PendingCount() == 2 && owner.CanvasBrushSealedStrokePending() &&
                         state.strokeReceipt.Read().outcome == HostOutcome::Pending && active &&
                         active->opening == state.opening && owner._activeHostRequest->ordinal == state.strokeOrdinal;

        if (state.action == SealedAction::Escape)
        {
            state.youngerOne = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
            state.youngerTwo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandFillForeground});
            const bool youngerPending = state.youngerOne.Read().outcome == HostOutcome::Pending &&
                                        state.youngerTwo.Read().outcome == HostOutcome::Pending &&
                                        owner._hostRequests.Size() == 2;
            const bool handled = owner.HandleAccelerator(VK_ESCAPE, 0);
            const auto* const stillActive =
                owner._activeHostRequest ? std::get_if<HostCanvasBrushInput>(&owner._activeHostRequest->input.Input())
                                         : nullptr;
            state.policyPreservedOriginal =
                handled && youngerPending && stillActive && stillActive->opening == state.opening &&
                owner._activeHostRequest->ordinal == state.strokeOrdinal &&
                owner._canvasBrushInput.Opening() == state.opening && owner._utilityTicket &&
                owner._utilityTicket.value() == state.workerTicket &&
                owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                state.strokeReceipt.Read().outcome == HostOutcome::Pending && owner.CanvasBrushSealedStrokePending() &&
                owner._hostRequests.Size() == 0;
            state.youngerCancelled =
                state.youngerOne.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, 0} &&
                state.youngerTwo.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, 0};
        }
        else
        {
            owner.OnCommand(ChromeHost::kCommandUndo);
            const auto* const pending = owner._hostRequests.Front();
            const auto* const command = pending ? std::get_if<HostCommandInput>(&pending->input.Input()) : nullptr;
            if (pending)
            {
                state.undoReceipt = pending->input.Receipt();
                state.undoOrdinal = pending->ordinal;
            }
            const auto* const stillActive =
                owner._activeHostRequest ? std::get_if<HostCanvasBrushInput>(&owner._activeHostRequest->input.Input())
                                         : nullptr;
            state.undoQueuedBehindStroke =
                stillActive && stillActive->opening == state.opening && command &&
                command->command == ChromeHost::kCommandUndo && state.undoOrdinal > state.strokeOrdinal &&
                owner._activeHostRequest->ordinal == state.strokeOrdinal && owner._utilityTicket &&
                owner._utilityTicket.value() == state.workerTicket &&
                state.undoReceipt.Read().outcome == HostOutcome::Pending &&
                owner._canvasBrushInput.PendingCount() == 2 &&
                owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                state.strokeReceipt.Read().outcome == HostOutcome::Pending && owner.CanvasBrushSealedStrokePending() &&
                owner._canvasBrushCancelledOpening == 0 && owner._hostRequests.Size() == 1;
        }
        (void)SetEvent(state.gate->release.get());
    };

    constexpr std::array<SealedAction, 2> cloneActions{SealedAction::Escape, SealedAction::Undo};
    constexpr std::array<const char*, 2> cloneNames{"escape-after-up", "undo-after-up"};
    for (std::size_t index = 0; index < cloneActions.size(); ++index)
    {
        if (!installCloneFixture())
            return 5;
        auto gate = std::make_shared<CloneReadbackTestGate>();
        if (!gate->release)
            return 5;
        gate->holdAfterCopy = true;
        _cloneReadbackTestGate = gate;
        pointer(WM_LBUTTONDOWN, MK_LBUTTON, kCloneDestination);
        CloneGateState state(*this, cloneActions[index]);
        state.gate = gate;
        state.strokeReceipt = _canvasBrushReceipt;
        state.opening = _canvasBrushInput.Opening();
        state.findOrdinal = findCloneOrdinal;
        state.upPosition = clientAt(kCloneDestination);
        if (SettleJobs(_jobs, 15000, cloneGatePump, &state) != SettleResult::Settled)
            return 5;

        const char* const scenario = cloneNames[index];
        if (!expect(scenario, "original-clone-readback-worker-held-after-gpu-copy",
                    state.gateVisited && state.originalWorkerHeld && state.workerTicket.generation != 0) ||
            !expect(scenario, "physical-up-seals-the-pending-clone-request", state.upSealed))
            return 5;
        if (cloneActions[index] == SealedAction::Escape &&
            (!expect(scenario, "escape-keeps-exact-receipt-ticket-opening-and-ordinal",
                     state.policyPreservedOriginal) ||
             !expect(scenario, "escape-cancels-only-younger-queued-commands", state.youngerCancelled)))
            return 5;
        if (cloneActions[index] == SealedAction::Undo &&
            !expect(scenario, "actual-undo-queues-after-original-stroke-ordinal", state.undoQueuedBehindStroke))
            return 5;

        Pixel destination{};
        Pixel source{};
        const auto& document = _session.Document();
        const bool sourceAvailable =
            document && document.value().layers.size() == 2 && document.value().layers.front().asset;
        if (sourceAvailable)
            document.value().layers.front().asset->image.ReadRegion(
                static_cast<int>(kCloneSource.x), static_cast<int>(kCloneSource.y), 1, 1, source.data(), 4, 0);
        const bool sourceStillLiteralRed = sourceAvailable && source == kRed;
        const bool targetReadable = ReadActivePixel(_session, static_cast<int>(kCloneDestination.x),
                                                    static_cast<int>(kCloneDestination.y), destination);
        if (cloneActions[index] == SealedAction::Escape)
        {
            if (!expect(scenario, "same-receipt-succeeds-with-one-clone-history-entry-and-literal-red",
                        state.strokeReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Succeeded, S_OK} &&
                            _canvasBrushReceipt.Read() == state.strokeReceipt.Read() && targetReadable &&
                            destination == kRed && sourceStillLiteralRed && _session.history.UndoCount() == 1 &&
                            _session.history.UndoName() == L"Clone Stamp"))
                return 5;
        }
        else if (!expect(scenario, "clone-then-queued-undo-restores-original-transparent-pixel",
                         state.strokeReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Succeeded, S_OK} &&
                             state.undoReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Succeeded, S_OK} &&
                             _canvasBrushReceipt.Read() == state.strokeReceipt.Read() && targetReadable &&
                             destination == kTransparent && sourceStillLiteralRed &&
                             _session.history.UndoCount() == 0 && _session.history.RedoName() == L"Clone Stamp"))
            return 5;
        if (!expect(scenario, "worker-retires-with-no-reservations-or-live-stroke",
                    gate->retired.load() && gate->retiredOffOwner.load() && !_utilityTicket && !_utilityStamp &&
                        !_historyWork.Busy() && !_canvasBrushInput.Busy() && !_session.PendingBrush() &&
                        _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0 &&
                        _gpuWork.LentCount() == 0 && _gpuWork.InFlightCount() == 0 && !_gpuWork.HasPending()))
            return 5;
        _cloneReadbackTestGate.reset();
    }

    // Reuse the established owner/worker opacity gate: a sealed Clone queued behind it keeps its ordinal while
    // Escape cancels the older opacity commit, then runs only after that worker acknowledges cancellation.
    if (!installCloneFixture())
        return 5;
    _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
    _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!_layerMetadataTestGate->release)
        return 5;
    auto ungatedCloneReadback = std::make_shared<CloneReadbackTestGate>();
    if (!ungatedCloneReadback->release)
        return 5;
    _cloneReadbackTestGate = ungatedCloneReadback;
    OnLayerOpacity(.25, ChromeEditPhase::Preview);

    struct OpacityCloneState final
    {
        explicit OpacityCloneState(Application& application) noexcept : owner(application) {}
        Application& owner;
        std::shared_ptr<LayerMetadataTestGate> opacityGate;
        ContinuationReceipt opacityReceipt;
        ContinuationReceipt strokeReceipt;
        ContinuationReceipt youngerOne;
        ContinuationReceipt youngerTwo;
        HistoryWorkStamp opacityStamp{};
        std::uint64_t opening = 0;
        std::uint64_t strokeOrdinal = 0;
        FindCloneOrdinal findOrdinal = nullptr;
        LPARAM downPosition = 0;
        LPARAM upPosition = 0;
        bool gateVisited = false;
        bool opacityWorkerHeld = false;
        bool strokeQueuedSealed = false;
        bool escapeRetainedStroke = false;
        bool youngerCancelled = false;
    } opacity(*this);
    opacity.opacityGate = _layerMetadataTestGate;
    opacity.findOrdinal = findCloneOrdinal;
    opacity.downPosition = clientAt(kCloneDestination);
    opacity.upPosition = opacity.downPosition;
    const auto opacityPump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<OpacityCloneState*>(opaque);
        Application& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerMetadataTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        if (state.gateVisited)
            return; // The retained Clone may close the cancelled opacity edit through this same worker gate.
        state.gateVisited = true;
        state.opacityWorkerHeld = owner._layerMetadataTicket && owner._layerMetadataStamp &&
                                  owner._historyWork.Busy() && state.opacityGate->captures.load() == 1 &&
                                  state.opacityGate->reservationHeld.load() && owner._activeHostRequest.has_value() &&
                                  WaitForSingleObject(state.opacityGate->release.get(), 0) == WAIT_TIMEOUT;
        if (owner._activeHostRequest)
            state.opacityReceipt = owner._activeHostRequest->input.Receipt();
        if (owner._layerMetadataStamp)
            state.opacityStamp = owner._layerMetadataStamp.value();

        owner.OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, state.downPosition);
        owner.OnPointer(WM_LBUTTONUP, 0, state.upPosition);
        state.opening = owner._canvasBrushInput.Opening();
        state.strokeReceipt = owner._canvasBrushReceipt;
        const auto ordinal = state.findOrdinal(owner, state.opening);
        state.strokeOrdinal = ordinal.value_or(0);
        const auto* const queued = owner._hostRequests.Front();
        const auto* const cloneInput = queued ? std::get_if<HostCanvasBrushInput>(&queued->input.Input()) : nullptr;
        state.strokeQueuedSealed = ordinal && queued && cloneInput && cloneInput->opening == state.opening &&
                                   queued->ordinal == state.strokeOrdinal && owner._canvasBrushInput.Busy() &&
                                   !owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.PendingCount() == 2 &&
                                   owner.CanvasBrushSealedStrokePending() &&
                                   state.strokeReceipt.Read().outcome == HostOutcome::Pending;

        state.youngerOne = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
        state.youngerTwo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandFillForeground});
        const bool youngerPending = state.youngerOne.Read().outcome == HostOutcome::Pending &&
                                    state.youngerTwo.Read().outcome == HostOutcome::Pending;
        const bool escaped = owner.HandleAccelerator(VK_ESCAPE, 0);
        const auto* const front = owner._hostRequests.Front();
        const auto* const retained = front ? std::get_if<HostCanvasBrushInput>(&front->input.Input()) : nullptr;
        state.escapeRetainedStroke = escaped && youngerPending && retained && retained->opening == state.opening &&
                                     front->ordinal == state.strokeOrdinal && owner.CanvasBrushSealedStrokePending() &&
                                     state.strokeReceipt.Read().outcome == HostOutcome::Pending &&
                                     owner._historyWork.CancelRequested(state.opacityStamp);
        state.youngerCancelled = state.youngerOne.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, 0} &&
                                 state.youngerTwo.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, 0};
        (void)SetEvent(state.opacityGate->release.get());
    };
    if (SettleJobs(_jobs, 15000, opacityPump, &opacity) != SettleResult::Settled)
        return 5;
    if (!expect("queued-behind-opacity", "real-opacity-worker-held-before-clone-up",
                opacity.gateVisited && opacity.opacityWorkerHeld) ||
        !expect("queued-behind-opacity", "up-seals-clone-at-its-original-ordinal", opacity.strokeQueuedSealed) ||
        !expect("queued-behind-opacity", "escape-cancels-older-opacity-and-retains-clone",
                opacity.escapeRetainedStroke) ||
        !expect("queued-behind-opacity", "escape-cancels-younger-commands", opacity.youngerCancelled))
        return 5;
    Pixel opacityTarget{};
    if (!expect("queued-behind-opacity", "older-cancel-ack-precedes-literal-red-clone",
                opacity.opacityReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT} &&
                    _layerMetadataTestGate->retired.load() && _layerMetadataTestGate->retiredOffOwner.load() &&
                    opacity.strokeReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Succeeded, S_OK} &&
                    _session.ActiveLayer() && _session.ActiveLayer()->opacity == 1.0 &&
                    ReadActivePixel(_session, static_cast<int>(kCloneDestination.x),
                                    static_cast<int>(kCloneDestination.y), opacityTarget) &&
                    opacityTarget == kRed && _session.history.UndoCount() == 1 &&
                    _session.history.UndoName() == L"Clone Stamp" && _jobs.ReservedMemoryBytes() == 0 &&
                    _jobs.ReleasingMemoryBytes() == 0))
        return 5;
    _cloneReadbackTestGate.reset();
    _layerMetadataTestGate.reset();

    std::fprintf(stderr, "%s sealed-clone-host.summary failures=%d cases=3\n", failures == 0 ? "PASS" : "FAIL",
                 failures);
    return failures;
}
