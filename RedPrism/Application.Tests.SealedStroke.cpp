#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"
#include "Runtime.Tests.StrokeOpeningTestGate.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

namespace
{
using Pixel = std::array<std::uint8_t, 4>; // BGRA
constexpr int kSide = 8;
constexpr Pixel kTransparent{0, 0, 0, 0};
constexpr Pixel kOpaqueRed{0, 0, 255, 255};
constexpr Vec2 kStrokePoint{3, 3};

CanvasDocument MakeSealedStrokeDocument(Guid& active)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;

    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    ImageLayer layer = ImageLayer::Blank(L"Sealed stroke host fixture", {kSide, kSide});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset =
        RasterAsset{Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, layer.name};
    active = layer.id;
    fixture.layers.push_back(std::move(layer));
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

int Application::RunSealedStrokeHostSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto originalTool = _tool;
    const auto originalBrushSettings = _session.brushSettings;
    const auto originalBrushMode = _session.brushMode;
    const auto originalForeground = _session.ForegroundColor();
    const bool originalSpacePan = _spacePan;
    std::array<BYTE, 256> originalKeyboard{};
    const bool keyboardCaptured = KeyboardState::Read(originalKeyboard);
    KeyboardState::Write({});
    int failures = 0;
    const auto expect = [&failures](const char* scenario, const char* control, bool condition) noexcept
    {
        std::fprintf(stderr, "%s sealed-stroke-host.%s.%s\n", condition ? "PASS" : "FAIL", scenario, control);
        failures += condition ? 0 : 1;
        return condition;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalTool, originalBrushSettings, originalBrushMode, originalForeground,
         originalSpacePan, keyboardCaptured, &originalKeyboard]() noexcept
        {
            if (_strokeOpeningTestGate)
                (void)SetEvent(_strokeOpeningTestGate->release.get());
            if (_layerMetadataTestGate)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            CancelHostContinuations();
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            if (_canvasBrushInput.Busy())
            {
                (void)CancelCanvasBrush();
                if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                    std::_Exit(5);
            }
            _strokeOpeningTestGate.reset();
            _layerMetadataTestGate.reset();
            _opacityEdit.reset();
            _opacityRecorded.reset();
            _canvasBrushReceipt = {};
            _viewport = originalViewport;
            _tool = originalTool;
            _session.brushSettings = originalBrushSettings;
            _session.brushMode = originalBrushMode;
            _session.SetForegroundColor(originalForeground);
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
    const auto findStrokeOrdinal = [](const Application& owner,
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
    using FindStrokeOrdinal = std::optional<std::uint64_t> (*)(const Application&, std::uint64_t) noexcept;
    const auto pointer = [this, &clientAt](UINT message, WPARAM modifiers, Vec2 point) noexcept
    { OnPointer(message, modifiers, clientAt(point)); };
    const auto prepare = [this]() noexcept
    {
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
            return false;
        Guid active{};
        CanvasDocument document = MakeSealedStrokeDocument(active);
        ReplaceDocument(std::move(document), active, {});
        _tool = EditorTool::Brush;
        _spacePan = false;
        _drag = PointerDrag{};
        _canvasPanLast.reset();
        _session.brushMode = BrushToolMode::Paint;
        _session.brushSettings = BrushSettings{2000, 1, 1, 0, 0, 1};
        _session.SetForegroundColor(0xFFFF0000u);
        _session.history.Reset();
        _session.history.MarkSaved();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        DocumentChanged();
        SyncChrome();
        return _session.ActiveLayer() && _session.ActiveLayer()->asset && _session.history.UndoCount() == 0;
    };
    const auto heldOpeningGate = [this, &pointer](std::shared_ptr<StrokeOpeningTestGate>& gate) noexcept
    {
        gate = std::make_shared<StrokeOpeningTestGate>();
        if (!gate->release)
            return false;
        gate->holdAfterPreparation = true;
        _strokeOpeningTestGate = gate;
        pointer(WM_LBUTTONDOWN, MK_LBUTTON, kStrokePoint);
        return _canvasBrushInput.Busy() && _canvasBrushInput.Sampling() &&
               _canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
    };

    // Escape after accepted Up keeps the exact opening alive until its original worker acknowledges.
    if (!prepare())
        return 5;
    std::shared_ptr<StrokeOpeningTestGate> escapeGate;
    if (!heldOpeningGate(escapeGate))
        return 5;
    struct EscapeState final
    {
        Application& owner;
        std::shared_ptr<StrokeOpeningTestGate> gate;
        ContinuationReceipt originalReceipt;
        JobTicket originalTicket{};
        std::uint64_t opening = 0;
        std::uint64_t strokeOrdinal = 0;
        ContinuationReceipt youngerOne;
        ContinuationReceipt youngerTwo;
        FindStrokeOrdinal findOrdinal;
        LPARAM upPosition;
        bool visited = false;
        bool workerHeld = false;
        bool upSealed = false;
        bool escapePreservedOriginal = false;
        bool youngerCancelled = false;
    } escape{*this, escapeGate,        _canvasBrushReceipt,   {}, _canvasBrushInput.Opening(), 0, {},
             {},    findStrokeOrdinal, clientAt(kStrokePoint)};
    const auto escapePump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<EscapeState*>(opaque);
        Application& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != StrokeOpeningTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        state.workerHeld =
            state.gate->captures.load() == 1 && state.gate->prepares.load() == 1 && state.gate->offowner.load() == 1 &&
            owner._utilityTicket && state.originalReceipt.Read().outcome == HostOutcome::Pending &&
            owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending && owner._canvasBrushInput.Busy() &&
            owner._canvasBrushInput.Sampling() && owner.CanvasBrushSealedStrokePending() == false;
        if (owner._utilityTicket)
            state.originalTicket = owner._utilityTicket.value();
        owner.OnPointer(WM_LBUTTONUP, 0, state.upPosition);
        state.opening = owner._canvasBrushInput.Opening();
        const auto ordinal = state.findOrdinal(owner, state.opening);
        state.strokeOrdinal = ordinal.value_or(0);
        state.upSealed = ordinal && owner._canvasBrushInput.Busy() && !owner._canvasBrushInput.Sampling() &&
                         owner._canvasBrushInput.PendingCount() == 2 && owner.CanvasBrushSealedStrokePending() &&
                         owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
        state.youngerOne = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
        state.youngerTwo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandFillForeground});
        const bool queuedYounger = state.youngerOne.Read().outcome == HostOutcome::Pending &&
                                   state.youngerTwo.Read().outcome == HostOutcome::Pending &&
                                   owner._hostRequests.Size() == 2;
        const bool escapeHandled = owner.HandleAccelerator(VK_ESCAPE, 0);
        const auto* const active = owner._activeHostRequest
                                       ? std::get_if<HostCanvasBrushInput>(&owner._activeHostRequest->input.Input())
                                       : nullptr;
        state.escapePreservedOriginal = escapeHandled && queuedYounger && active && active->opening == state.opening &&
                                        owner._activeHostRequest->ordinal == state.strokeOrdinal &&
                                        owner._canvasBrushInput.Opening() == state.opening &&
                                        state.originalReceipt.Read().outcome == HostOutcome::Pending &&
                                        owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                                        owner._utilityTicket && owner._utilityTicket.value() == state.originalTicket &&
                                        owner.CanvasBrushSealedStrokePending();
        state.youngerCancelled = state.youngerOne.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, 0} &&
                                 state.youngerTwo.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, 0} &&
                                 owner._hostRequests.Size() == 0;
        (void)SetEvent(state.gate->release.get());
    };
    if (SettleJobs(_jobs, 15000, escapePump, &escape) != SettleResult::Settled)
        return 5;
    if (!expect("active-up-escape", "real-opening-worker-held-before-ack", escape.visited && escape.workerHeld) ||
        !expect("active-up-escape", "up-seals-current-pending-stroke", escape.upSealed) ||
        !expect("active-up-escape", "escape-retains-exact-request-ticket-and-ordinal",
                escape.escapePreservedOriginal) ||
        !expect("active-up-escape", "escape-cancels-both-younger-requests", escape.youngerCancelled))
        return 5;
    Pixel activeEscapePixel{};
    if (!expect("active-up-escape", "original-receipt-succeeds-one-edit-and-literal-red",
                escape.originalReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Succeeded, S_OK} &&
                    !_canvasBrushInput.Busy() && !_canvasBrushCancelledOpening && _session.history.UndoCount() == 1 &&
                    _session.ActiveLayer() && ReadActivePixel(_session, 3, 3, activeEscapePixel) &&
                    activeEscapePixel == kOpaqueRed && _jobs.ReservedMemoryBytes() == 0 &&
                    _jobs.ReleasingMemoryBytes() == 0))
        return 5;
    _strokeOpeningTestGate.reset();

    // Undo admitted after Up remains pending behind the original stroke, then restores the independent transparent
    // literal; Redo reapplies the same opaque red pixel.
    if (!prepare())
        return 5;
    std::shared_ptr<StrokeOpeningTestGate> undoGate;
    if (!heldOpeningGate(undoGate))
        return 5;
    struct UndoState final
    {
        Application& owner;
        std::shared_ptr<StrokeOpeningTestGate> gate;
        ContinuationReceipt strokeReceipt;
        ContinuationReceipt undoReceipt;
        std::uint64_t opening = 0;
        std::uint64_t strokeOrdinal = 0;
        std::uint64_t undoOrdinal = 0;
        FindStrokeOrdinal findOrdinal;
        LPARAM upPosition;
        bool visited = false;
        bool undoWaitsBehindSealedStroke = false;
    } undo{*this, undoGate,          _canvasBrushReceipt,   {}, _canvasBrushInput.Opening(), 0,
           0,     findStrokeOrdinal, clientAt(kStrokePoint)};
    const auto undoPump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<UndoState*>(opaque);
        Application& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != StrokeOpeningTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = state.gate->captures.load() == 1 && state.gate->prepares.load() == 1;
        owner.OnPointer(WM_LBUTTONUP, 0, state.upPosition);
        const auto strokeOrdinal = state.findOrdinal(owner, state.opening);
        state.strokeOrdinal = strokeOrdinal.value_or(0);
        owner.OnCommand(ChromeHost::kCommandUndo);
        const auto* const active = owner._activeHostRequest
                                       ? std::get_if<HostCanvasBrushInput>(&owner._activeHostRequest->input.Input())
                                       : nullptr;
        const auto* const pendingUndo = owner._hostRequests.Front();
        if (pendingUndo)
            state.undoReceipt = pendingUndo->input.Receipt();
        const auto* const undoInput =
            pendingUndo ? std::get_if<HostCommandInput>(&pendingUndo->input.Input()) : nullptr;
        state.undoOrdinal = pendingUndo ? pendingUndo->ordinal : 0;
        state.undoWaitsBehindSealedStroke =
            strokeOrdinal && active && active->opening == state.opening &&
            state.undoReceipt.Read().outcome == HostOutcome::Pending &&
            owner._activeHostRequest->ordinal == state.strokeOrdinal && undoInput &&
            undoInput->command == ChromeHost::kCommandUndo && state.undoOrdinal > state.strokeOrdinal &&
            owner._canvasBrushInput.PendingCount() == 2 &&
            owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
            owner.CanvasBrushSealedStrokePending() && owner._canvasBrushCancelledOpening == 0;
        (void)SetEvent(state.gate->release.get());
    };
    if (SettleJobs(_jobs, 15000, undoPump, &undo) != SettleResult::Settled)
        return 5;
    if (!expect("active-up-undo", "undo-stays-pending-behind-original-stroke",
                undo.visited && undo.undoWaitsBehindSealedStroke) ||
        !expect("active-up-undo", "stroke-then-exact-undo-restores-transparent-pixel",
                undo.strokeReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Succeeded, S_OK} &&
                    undo.undoReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Succeeded, S_OK} &&
                    !_canvasBrushInput.Busy() && _session.history.UndoCount() == 0 &&
                    _session.history.RedoName() == L"Brush Stroke" &&
                    ReadActivePixel(_session, 3, 3, activeEscapePixel) && activeEscapePixel == kTransparent))
        return 5;
    const auto redoReceipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandRedo});
    if (!settle() ||
        !expect("active-up-undo", "redo-reapplies-literal-red-one-edit",
                redoReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Succeeded, S_OK} &&
                    _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Brush Stroke" &&
                    ReadActivePixel(_session, 3, 3, activeEscapePixel) && activeEscapePixel == kOpaqueRed &&
                    _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0))
        return 5;
    _strokeOpeningTestGate.reset();

    // A sealed stroke queued behind a held opacity worker keeps its original ordinal when Escape cancels that older
    // commit. The older worker must acknowledge cancellation before the retained stroke can run.
    if (!prepare())
        return 5;
    _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
    _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!_layerMetadataTestGate->release)
        return 5;
    OnLayerOpacity(.25, ChromeEditPhase::Preview);
    struct OpacityState final
    {
        Application& owner;
        std::shared_ptr<LayerMetadataTestGate> gate;
        ContinuationReceipt opacityReceipt;
        ContinuationReceipt strokeReceipt;
        ContinuationReceipt youngerOne;
        ContinuationReceipt youngerTwo;
        HistoryWorkStamp opacityStamp{};
        std::uint64_t strokeOpening = 0;
        std::uint64_t strokeOrdinal = 0;
        FindStrokeOrdinal findOrdinal;
        LPARAM downPosition;
        LPARAM upPosition;
        bool visited = false;
        bool opacityHeld = false;
        bool strokeQueuedSealed = false;
        bool escapeRetainedStroke = false;
        bool youngerCancelled = false;
    } opacity{*this,
              _layerMetadataTestGate,
              {},
              {},
              {},
              {},
              {},
              0,
              0,
              findStrokeOrdinal,
              clientAt(kStrokePoint),
              clientAt(kStrokePoint)};
    const auto opacityPump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<OpacityState*>(opaque);
        Application& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerMetadataTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        // The retained stroke closes the cancelled opacity edit through another metadata worker. That worker shares
        // this gate, but must not replay the physical gesture or replace the original cancellation witnesses.
        if (state.visited)
            return;
        state.visited = true;
        state.opacityHeld = owner._layerMetadataTicket && owner._layerMetadataStamp && owner._historyWork.Busy() &&
                            state.gate->captures.load() == 1 && state.gate->reservationHeld.load() &&
                            owner._activeHostRequest.has_value();
        if (owner._activeHostRequest)
            state.opacityReceipt = owner._activeHostRequest->input.Receipt();
        if (owner._layerMetadataStamp)
            state.opacityStamp = owner._layerMetadataStamp.value();
        owner.OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, state.downPosition);
        owner.OnPointer(WM_LBUTTONUP, 0, state.upPosition);
        state.strokeOpening = owner._canvasBrushInput.Opening();
        state.strokeReceipt = owner._canvasBrushReceipt;
        const auto strokeOrdinal = state.findOrdinal(owner, state.strokeOpening);
        state.strokeOrdinal = strokeOrdinal.value_or(0);
        const auto* const queuedStroke = owner._hostRequests.Front();
        const auto* const brush =
            queuedStroke ? std::get_if<HostCanvasBrushInput>(&queuedStroke->input.Input()) : nullptr;
        state.strokeQueuedSealed = strokeOrdinal && queuedStroke && brush && brush->opening == state.strokeOpening &&
                                   queuedStroke->ordinal == state.strokeOrdinal && owner._canvasBrushInput.Busy() &&
                                   !owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.PendingCount() == 2 &&
                                   owner.CanvasBrushSealedStrokePending() &&
                                   state.strokeReceipt.Read().outcome == HostOutcome::Pending;
        state.youngerOne = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
        state.youngerTwo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandFillForeground});
        const bool commandsPending = state.youngerOne.Read().outcome == HostOutcome::Pending &&
                                     state.youngerTwo.Read().outcome == HostOutcome::Pending;
        const bool escaped = owner.HandleAccelerator(VK_ESCAPE, 0);
        const auto* const frontAfterEscape = owner._hostRequests.Front();
        const auto* const retained =
            frontAfterEscape ? std::get_if<HostCanvasBrushInput>(&frontAfterEscape->input.Input()) : nullptr;
        state.escapeRetainedStroke =
            escaped && commandsPending && retained && retained->opening == state.strokeOpening &&
            frontAfterEscape->ordinal == state.strokeOrdinal && owner.CanvasBrushSealedStrokePending() &&
            state.strokeReceipt.Read().outcome == HostOutcome::Pending &&
            owner._historyWork.CancelRequested(state.opacityStamp);
        state.youngerCancelled = state.youngerOne.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, 0} &&
                                 state.youngerTwo.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, 0};
        (void)SetEvent(state.gate->release.get());
    };
    if (SettleJobs(_jobs, 15000, opacityPump, &opacity) != SettleResult::Settled)
        return 5;
    if (!expect("queued-behind-opacity", "real-opacity-worker-held-with-original-commit",
                opacity.visited && opacity.opacityHeld) ||
        !expect("queued-behind-opacity", "up-queues-sealed-stroke-at-original-ordinal", opacity.strokeQueuedSealed) ||
        !expect("queued-behind-opacity", "escape-cancels-older-work-and-keeps-stroke-ordinal",
                opacity.escapeRetainedStroke) ||
        !expect("queued-behind-opacity", "escape-cancels-younger-commands", opacity.youngerCancelled))
        return 5;
    Pixel queuedPixel{};
    if (!expect("queued-behind-opacity", "older-cancellation-ack-precedes-one-red-stroke",
                opacity.opacityReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT} &&
                    opacity.gate->retired.load() && opacity.gate->retiredOffOwner.load() &&
                    opacity.strokeReceipt.Read() == ContinuationReceipt::Result{HostOutcome::Succeeded, S_OK} &&
                    !_canvasBrushInput.Busy() && !_opacityEdit && _session.ActiveLayer()->opacity == 1.0 &&
                    _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Brush Stroke" &&
                    ReadActivePixel(_session, 3, 3, queuedPixel) && queuedPixel == kOpaqueRed &&
                    MetadataAccountingSettledForTest() && _jobs.ReservedMemoryBytes() == 0 &&
                    _jobs.ReleasingMemoryBytes() == 0))
        return 5;
    _layerMetadataTestGate.reset();

    // Keep the pre-Up cancellation contract covered with one actual held opening; the legacy full suite owns the
    // rest of the physically-down cases.
    if (!prepare())
        return 5;
    std::shared_ptr<StrokeOpeningTestGate> unsealedGate;
    if (!heldOpeningGate(unsealedGate))
        return 5;
    struct UnsealedState final
    {
        Application& owner;
        std::shared_ptr<StrokeOpeningTestGate> gate;
        ContinuationReceipt receipt;
        std::uint64_t opening = 0;
        bool visited = false;
        bool remainedUnsealed = false;
        bool cancellationRecorded = false;
    } unsealed{*this, unsealedGate, _canvasBrushReceipt, _canvasBrushInput.Opening()};
    const auto unsealedPump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<UnsealedState*>(opaque);
        Application& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != StrokeOpeningTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = state.gate->captures.load() == 1 && state.gate->prepares.load() == 1;
        state.remainedUnsealed = owner._canvasBrushInput.Busy() && owner._canvasBrushInput.Sampling() &&
                                 !owner.CanvasBrushSealedStrokePending();
        const bool handled = owner.HandleAccelerator(VK_ESCAPE, 0);
        state.cancellationRecorded = handled && owner._canvasBrushCancelledOpening == state.opening &&
                                     owner._canvasBrushInput.Busy() && owner._canvasBrushInput.Sampling() &&
                                     owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
        (void)SetEvent(state.gate->release.get());
    };
    if (SettleJobs(_jobs, 15000, unsealedPump, &unsealed) != SettleResult::Settled)
        return 5;
    if (!expect("unsealed-down-escape", "physical-down-remains-cancellable",
                unsealed.visited && unsealed.remainedUnsealed && unsealed.cancellationRecorded) ||
        !expect("unsealed-down-escape", "original-cancel-receipt-and-empty-history",
                unsealed.receipt.Read() == ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT} &&
                    !_canvasBrushInput.Busy() && !_canvasBrushCancelledOpening && _session.history.UndoCount() == 0 &&
                    ReadActivePixel(_session, 3, 3, queuedPixel) && queuedPixel == kTransparent &&
                    _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0))
        return 5;
    _strokeOpeningTestGate.reset();

    std::fprintf(stderr, "%s sealed-stroke-host.summary cases=4 failures=%d\n", failures == 0 ? "PASS" : "FAIL",
                 failures);
    return failures;
}
