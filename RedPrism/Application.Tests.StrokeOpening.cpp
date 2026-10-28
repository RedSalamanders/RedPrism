#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "Runtime.Tests.StrokeOpeningTestGate.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

namespace
{
using Pixel = std::array<std::uint8_t, 4>; // BGRA
constexpr int kSide = 8;
constexpr Pixel kTransparent{0, 0, 0, 0};
constexpr Pixel kCapturedRed{0, 0, 255, 255};

// This is the receipt contract: each row is emitted in this order for each case, followed by
// the cases in declaration order. It is deliberately literal so archived host output is auditable.
constexpr std::array<const char*, 7> kControlOrder{
    "opening-held-preserves-document-history-pending-brush",
    "opening-held-retains-down-up-prefix",
    "opening-held-zoom-and-pan-remain-live",
    "opening-worker-prepares-once-offowner-and-retires",
    "finish-publishes-one-sparse-edit-and-literal-pixel",
    "undo-restores-literal-before-pixel",
    "redo-restores-literal-edited-pixel",
};

enum class Case : std::uint8_t
{
    PaintSelection,
    BlurImpulse,
    MaskPaint,
    CapturedSettings,
};

struct CaseSpec final
{
    const char* name;
    Case kind;
    bool selection;
    bool mask;
    bool impulse;
    bool changesSettingsAfterDown;
    Vec2 strokePoint;
    Pixel editedPixel;
    Pixel beforePixel;
};

constexpr std::array<CaseSpec, 4> kCases{{
    {"paint-selection", Case::PaintSelection, true, false, false, false, {3, 3}, kCapturedRed, kTransparent},
    {"blur-impulse-neighbor", Case::BlurImpulse, false, false, true, false, {3, 3}, {0, 0, 2, 2}, {0, 0, 255, 255}},
    {"mask-paint", Case::MaskPaint, true, true, false, false, {3, 3}, {0, 0, 0, 0}, {128, 128, 128, 128}},
    {"captured-settings", Case::CapturedSettings, false, false, false, true, {3, 3}, kCapturedRed, kTransparent},
}};

CanvasDocument MakeDocument(const CaseSpec& spec, Guid& active)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;

    std::array<std::uint8_t, kSide * kSide * 4> image{};
    if (spec.impulse)
    {
        // Independent source literal: one opaque red impulse at (3,3), transparent finite edges.
        const std::size_t offset = (static_cast<std::size_t>(3) * kSide + 3u) * 4u;
        image[offset + 2] = 255;
        image[offset + 3] = 255;
    }
    ImageLayer layer = ImageLayer::Blank(L"Stroke opening host fixture", {kSide, kSide});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset =
        RasterAsset{Raster::Make(kSide, kSide, RasterFormat::Bgra8888, image.data(), image.size()), {}, layer.name};
    active = layer.id;

    if (spec.mask)
    {
        std::array<std::uint8_t, kSide * kSide> mask{};
        mask.fill(128);
        LayerMask target;
        target.asset = RasterAsset{
            Raster::Make(kSide, kSide, RasterFormat::Gray8, mask.data(), mask.size()), {}, L"Literal 128 mask"};
        target.asset.thumbnail = Raster::MakeThumbnail(target.asset.image, 96);
        layer.mask = std::move(target);
    }
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

bool ReadPixel(const DocumentSession& session, int x, int y, Pixel& pixel) noexcept
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

bool ReadMask(const DocumentSession& session, int x, int y, std::uint8_t& pixel) noexcept
{
    const ImageLayer* const layer = session.ActiveLayer();
    if (!layer || !layer->mask)
        return false;
    const Raster& image = layer->mask->asset.image;
    if (!image.HasStorage() || image.Format() != RasterFormat::Gray8 || x < 0 || y < 0 || x >= image.Width() ||
        y >= image.Height())
        return false;
    image.ReadRegion(x, y, 1, 1, &pixel, 1, 0);
    return true;
}
} // namespace

int Application::RunStrokeOpeningHostSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto originalTool = _tool;
    const auto originalBrushSettings = _session.brushSettings;
    const auto originalBrushMode = _session.brushMode;
    const auto originalSmearMode = _session.smearMode;
    const bool originalMaskWhite = _session.maskPaintWhite;
    const bool originalSpacePan = _spacePan;
    std::array<BYTE, 256> originalKeyboard{};
    const bool keyboardCaptured = KeyboardState::Read(originalKeyboard);
    KeyboardState::Write({});

    int failures = 0;
    const auto expect = [&failures](const char* scenario, const char* control, bool condition) noexcept
    {
        std::fprintf(stderr, "%s stroke-opening-host.%s.%s\n", condition ? "PASS" : "FAIL", scenario, control);
        failures += condition ? 0 : 1;
        return condition;
    };
    const auto settle = [this](auto dispatch, void* context) noexcept
    { return SettleJobs(_jobs, 15000, dispatch, context) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalTool, originalBrushSettings, originalBrushMode, originalSmearMode,
         originalMaskWhite, originalSpacePan, keyboardCaptured, &originalKeyboard]() noexcept
        {
            if (_strokeOpeningTestGate)
                (void)SetEvent(_strokeOpeningTestGate->release.get());
            CancelHostContinuations();
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            if (_session.PendingBrush())
                _session.CancelBrush();
            _strokeOpeningTestGate.reset();
            _tool = originalTool;
            _viewport = originalViewport;
            _session.brushSettings = originalBrushSettings;
            _session.brushMode = originalBrushMode;
            _session.smearMode = originalSmearMode;
            _session.maskPaintWhite = originalMaskWhite;
            _spacePan = originalSpacePan;
            if (keyboardCaptured)
                KeyboardState::Write(originalKeyboard);
        });

    const auto clientAt = [this](Vec2 documentPoint) noexcept
    {
        const auto hole = _chrome->HolePixels();
        const auto view = _viewport.ViewFromDocument(documentPoint, DocumentSize());
        return MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                          static_cast<int>(std::lround(hole.top + view.y)));
    };
    const auto pointer = [this, &clientAt](UINT message, WPARAM modifiers, Vec2 point) noexcept
    { OnPointer(message, modifiers, clientAt(point)); };

    for (const CaseSpec& spec : kCases)
    {
        if (!settle(DispatchMessageThunk, this))
            return 5;

        Guid active{};
        CanvasDocument document = MakeDocument(spec, active);
        ReplaceDocument(std::move(document), active, {});
        _tool = spec.kind == Case::BlurImpulse ? EditorTool::Smear : EditorTool::Brush;
        _spacePan = false;
        _drag = PointerDrag{};
        _canvasPanLast.reset();
        _session.brushMode = BrushToolMode::Paint;
        _session.smearMode = SmearMode::Blur;
        _session.maskPaintWhite = false;
        _session.brushSettings =
            spec.kind == Case::BlurImpulse ? BrushSettings{40, 1, 1, 0, 0, 1} : BrushSettings{2000, 1, 1, 0, 0, 1};
        if (spec.mask)
            _session.SelectLayerTarget(active, true);
        if (spec.selection)
            _session.SetSelection(
                DocumentSelection{false, SelectionLoops{{Vec2{2, 2}, Vec2{6, 2}, Vec2{6, 6}, Vec2{2, 6}}}},
                L"Stroke opening host selection");
        _session.history.Reset();
        _session.history.MarkSaved();
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        DocumentChanged();
        SyncChrome();

        auto gate = std::make_shared<StrokeOpeningTestGate>();
        if (!gate->release)
            return 5;
        gate->holdAfterPreparation = true;
        _strokeOpeningTestGate = gate;

        const Guid revisionBefore = _session.history.CurrentRevision();
        const void* const documentRecordsBefore =
            _session.Document() ? _session.Document()->layers.Identity() : nullptr;
        Pixel before{};
        const bool beforeRead = spec.mask ? true
                                          : ReadPixel(_session, static_cast<int>(spec.strokePoint.x),
                                                      static_cast<int>(spec.strokePoint.y), before);
        if (spec.mask)
            before = spec.beforePixel;
        if (!beforeRead)
            return 5;

        pointer(WM_LBUTTONDOWN, MK_LBUTTON, spec.strokePoint);
        const std::uint64_t opening = _canvasBrushInput.Opening();
        if (opening == 0)
            return 5;

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
        struct State final
        {
            Application& owner;
            const CaseSpec& spec;
            std::shared_ptr<StrokeOpeningTestGate> gate;
            Guid revision;
            const void* documentRecords;
            std::uint64_t opening;
            Pixel before;
            bool visited = false;
            bool held = false;
            bool prefixRetained = false;
            bool viewResponsive = false;
            bool settingsCaptured = false;
        }
#pragma warning(pop)
        state{*this, spec, gate, revisionBefore, documentRecordsBefore, opening, before};

        const auto dispatch = [](void* opaque, void* native) noexcept
        {
            auto& state = *static_cast<State*>(opaque);
            auto& owner = state.owner;
            auto& message = *static_cast<MSG*>(native);
            if (message.message != StrokeOpeningTestGate::kMessage)
            {
                owner.DispatchQueuedMessage(message);
                return;
            }
            state.visited = true;
            const auto* const active = owner._session.ActiveLayer();
            Pixel current{};
            std::uint8_t maskPixel = 0;
            const bool sourceUnchanged = state.spec.mask
                                             ? (ReadMask(owner._session, 3, 3, maskPixel) && maskPixel == 128 &&
                                                ReadMask(owner._session, 0, 0, maskPixel) && maskPixel == 128)
                                             : (ReadPixel(owner._session, static_cast<int>(state.spec.strokePoint.x),
                                                          static_cast<int>(state.spec.strokePoint.y), current) &&
                                                current == state.before);
            state.held = state.gate->captures.load() == 1 && state.gate->prepares.load() == 1 && active &&
                         owner._session.history.CurrentRevision() == state.revision && owner._session.Document() &&
                         owner._session.Document()->layers.Identity() == state.documentRecords &&
                         owner._session.history.UndoCount() == 0 && owner._session.PendingBrush() == nullptr &&
                         sourceUnchanged && owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;

            // Up must append to the already accepted opening while the worker still owns the prepared publication.
            const std::size_t prefixBeforeUp = owner._canvasBrushInput.PendingCount();
            const auto hole = owner._chrome->HolePixels();
            const auto view = owner._viewport.ViewFromDocument(state.spec.strokePoint, owner.DocumentSize());
            owner.OnPointer(WM_LBUTTONUP, 0,
                            MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                       static_cast<int>(std::lround(hole.top + view.y))));
            state.prefixRetained = prefixBeforeUp == 1 && owner._canvasBrushInput.PendingCount() == 2 &&
                                   owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                                   owner._session.PendingBrush() == nullptr;

            const auto captured = owner._canvasBrushInput.CapturedContext(state.opening);
            if (state.spec.changesSettingsAfterDown)
            {
                owner._session.brushSettings = BrushSettings{1, 1, 0, 1, 0, 1}; // Green, one-pixel replacement.
                state.settingsCaptured = captured && captured->settings == BrushSettings{2000, 1, 1, 0, 0, 1};
            }
            else
                state.settingsCaptured = captured != nullptr;

            const double zoomBefore = owner._viewport.zoom;
            owner.OnCommand(ChromeHost::kCommandZoomIn);
            const bool zoomWorked = owner._viewport.zoom > zoomBefore && owner._session.PendingBrush() == nullptr &&
                                    owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;

            // Exercise the existing deferred Space-pan endpoint while the original Down/Up prefix is queued.
            // The host flag and thread keyboard state mirror a Space key held by the self-test driver.
            std::array<BYTE, 256> panKeys{};
            panKeys[VK_SPACE] = 0x80;
            KeyboardState::Write(panKeys);
            owner._spacePan = true;
            const Vec2 oldPan = owner._viewport.pan;
            const auto panHole = owner._chrome->HolePixels();
            const int panX = static_cast<int>(std::lround(panHole.left + 8));
            const int panY = static_cast<int>(std::lround(panHole.top + 8));
            owner.OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(panX, panY));
            owner.OnPointer(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(panX + 9, panY + 5));
            owner.OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(panX + 9, panY + 5));
            owner._spacePan = false;
            KeyboardState::Write({});
            state.viewResponsive = zoomWorked && owner._viewport.pan != oldPan &&
                                   owner._session.PendingBrush() == nullptr &&
                                   owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;

            (void)SetEvent(state.gate->release.get());
        };

        const SettleResult openingResult = SettleJobs(_jobs, 15000, dispatch, &state);
        if (openingResult != SettleResult::Settled)
            return 5;
        if (!expect(spec.name, kControlOrder[0], state.visited && state.held))
            return 5;
        if (!expect(spec.name, kControlOrder[1], state.prefixRetained))
            return 5;
        if (!expect(spec.name, kControlOrder[2], state.viewResponsive))
            return 5;
        if (!expect(spec.name, kControlOrder[3],
                    gate->captures.load() == 1 && gate->prepares.load() == 1 && gate->offowner.load() == 1 &&
                        gate->retired.load()))
            return 5;

        const HostOutcome receipt = _canvasBrushReceipt.Read().outcome;
        const bool finished = receipt == HostOutcome::Succeeded && !_canvasBrushInput.Busy() &&
                              _session.PendingBrush() == nullptr && _session.history.UndoCount() == 1;
        if (spec.mask)
        {
            std::uint8_t inside = 255, outside = 0;
            const bool pixels =
                ReadMask(_session, 3, 3, inside) && ReadMask(_session, 0, 0, outside) && inside == 0 && outside == 128;
            if (!expect(spec.name, kControlOrder[4], finished && pixels))
                return 5;
        }
        else
        {
            Pixel actual{};
            const int resultX = spec.kind == Case::BlurImpulse ? 4 : 3;
            const bool pixels = ReadPixel(_session, resultX, 3, actual) && actual == spec.editedPixel;
            const ImageLayer* const layer = _session.ActiveLayer();
            const bool sparse = layer && layer->asset && !layer->asset->image.IsMaterialized();
            if (!expect(spec.name, kControlOrder[4], finished && pixels && sparse && state.settingsCaptured))
                return 5;
        }

        // The same numeric edit must be exactly one history entry in both directions.
        if (spec.mask)
        {
            OnCommand(ChromeHost::kCommandUndo);
            if (!settle(DispatchMessageThunk, this) ||
                !expect(spec.name, kControlOrder[5],
                        _session.history.UndoCount() == 0 && _session.history.RedoName() == L"Paint Mask"))
                return 5;
            OnCommand(ChromeHost::kCommandRedo);
            std::uint8_t inside = 255, outside = 0;
            if (!settle(DispatchMessageThunk, this) ||
                !expect(spec.name, kControlOrder[6],
                        _session.history.UndoCount() == 1 && ReadMask(_session, 3, 3, inside) && inside == 0 &&
                            ReadMask(_session, 0, 0, outside) && outside == 128))
                return 5;
        }
        else
        {
            OnCommand(ChromeHost::kCommandUndo);
            Pixel undone{};
            if (!settle(DispatchMessageThunk, this) ||
                !expect(spec.name, kControlOrder[5],
                        _session.history.UndoCount() == 0 &&
                            _session.history.RedoName() ==
                                (spec.kind == Case::BlurImpulse ? L"Blur" : L"Brush Stroke") &&
                            ReadPixel(_session, 3, 3, undone) && undone == spec.beforePixel))
                return 5;
            OnCommand(ChromeHost::kCommandRedo);
            Pixel redone{};
            const int resultX = spec.kind == Case::BlurImpulse ? 4 : 3;
            if (!settle(DispatchMessageThunk, this) ||
                !expect(spec.name, kControlOrder[6],
                        _session.history.UndoCount() == 1 && ReadPixel(_session, resultX, 3, redone) &&
                            redone == spec.editedPixel))
                return 5;
        }
        _strokeOpeningTestGate.reset();
    }

    // Failure-path additions deliberately run after the unchanged 28-control success matrix above. They still
    // enter through OnPointer -> host continuation -> StartStrokeOpening and use the real worker preparation,
    // gate, original scheduler record, owner completion and deferred whole-payload retirement.
    enum class Fault : std::uint8_t
    {
        PreparedFailure,
        EscapeBeforeApply,
        CoreReplacement,
    };
    const auto runFault = [this, &expect, &settle, &pointer](const char* scenario, Fault fault) noexcept
    {
        if (!settle(DispatchMessageThunk, this))
            return false;

        Guid active{};
        CanvasDocument document = MakeDocument(kCases[0], active);
        ReplaceDocument(std::move(document), active, {});
        _tool = EditorTool::Brush;
        _spacePan = false;
        _drag = PointerDrag{};
        _canvasPanLast.reset();
        _session.brushMode = BrushToolMode::Paint;
        _session.maskPaintWhite = false;
        _session.brushSettings = BrushSettings{2000, 1, 1, 0, 0, 1};
        _session.history.Reset();
        _session.history.MarkSaved();
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        DocumentChanged();
        SyncChrome();

        auto gate = std::make_shared<StrokeOpeningTestGate>();
        if (!gate->release)
            return false;
        gate->holdAfterPreparation = true;
        if (fault == Fault::PreparedFailure)
            gate->failureResult = JobResult{JobStatus::Failed, E_FAIL};
        _strokeOpeningTestGate = gate;

        constexpr Vec2 strokePoint{3, 3};
        const Guid revisionBefore = _session.history.CurrentRevision();
        const void* const recordsBefore = _session.Document()->layers.Identity();
        const std::uint64_t serialBefore = _session.DocumentSerial();
        Pixel before{};
        if (!ReadPixel(_session, 3, 3, before))
            return false;
        pointer(WM_LBUTTONDOWN, MK_LBUTTON, strokePoint);
        const std::uint64_t opening = _canvasBrushInput.Opening();
        if (opening == 0)
            return false;

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
        struct FaultState final
        {
            Application& owner;
            Fault fault;
            std::shared_ptr<StrokeOpeningTestGate> gate;
            Guid revision;
            const void* records;
            std::uint64_t serial;
            std::uint64_t opening;
            Pixel before;
            bool reached = false;
            bool held = false;
            bool prefix = false;
            bool action = false;
            bool replacement = false;
        }
#pragma warning(pop)
        state{*this, fault, gate, revisionBefore, recordsBefore, serialBefore, opening, before};

        const auto dispatch = [](void* opaque, void* native) noexcept
        {
            auto& state = *static_cast<FaultState*>(opaque);
            Application& owner = state.owner;
            auto& message = *static_cast<MSG*>(native);
            if (message.message != StrokeOpeningTestGate::kMessage)
            {
                owner.DispatchQueuedMessage(message);
                return;
            }
            state.reached = true;
            Pixel pixel{};
            state.held = state.gate->captures.load() == 1 && state.gate->prepares.load() == 1 &&
                         state.gate->offowner.load() == 1 && owner._session.PendingBrush() == nullptr &&
                         owner._session.Document() && owner._session.Document()->layers.Identity() == state.records &&
                         owner._session.history.CurrentRevision() == state.revision &&
                         owner._session.history.UndoCount() == 0 && ReadPixel(owner._session, 3, 3, pixel) &&
                         pixel == state.before && owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                         owner._canvasBrushInput.PendingCount() == 1;

            const std::size_t beforeUp = owner._canvasBrushInput.PendingCount();
            const auto hole = owner._chrome->HolePixels();
            const auto view = owner._viewport.ViewFromDocument({3, 3}, owner.DocumentSize());
            // Escape cancellation is exercised while the physical pointer is still down. A sealed Up/Lost
            // stroke is separately protected by the P1 acceptance contract and must finish, not be cancelled.
            if (state.fault != Fault::EscapeBeforeApply)
                owner.OnPointer(WM_LBUTTONUP, 0,
                                MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                           static_cast<int>(std::lround(hole.top + view.y))));
            state.prefix =
                beforeUp == 1 &&
                owner._canvasBrushInput.PendingCount() == (state.fault == Fault::EscapeBeforeApply ? 1u : 2u) &&
                owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                owner._session.PendingBrush() == nullptr;

            if (state.fault == Fault::EscapeBeforeApply)
            {
                std::array<BYTE, 256> saved{};
                const bool restore = KeyboardState::Read(saved);
                std::array<BYTE, 256> keys{};
                keys[VK_ESCAPE] = 0x80;
                KeyboardState::Write(keys);
                const LPARAM key = 1 | (static_cast<LPARAM>(MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC)) << 16);
                (void)SendMessageW(owner._window.get(), WM_KEYDOWN, VK_ESCAPE, key);
                state.action = owner._lastKeyHandled && owner._canvasBrushCancelledOpening == state.opening &&
                               owner._utilityStamp && owner._historyWork.CancelRequested(owner._utilityStamp.value()) &&
                               owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
                keys[VK_ESCAPE] = 0;
                KeyboardState::Write(keys);
                (void)SendMessageW(owner._window.get(), WM_KEYUP, VK_ESCAPE, key | (1ll << 31));
                if (restore)
                    KeyboardState::Write(saved);
            }
            else if (state.fault == Fault::CoreReplacement)
            {
                Guid replacementActive{};
                CanvasDocument replacement = MakeDocument(kCases[0], replacementActive);
                owner._session.ReplaceDocument(std::move(replacement), replacementActive);
                owner.DocumentChanged();
                owner.SyncChrome();
                const ImageLayer* const layer = owner._session.ActiveLayer();
                Pixel replacementPixel{};
                state.replacement = owner._session.DocumentSerial() != state.serial &&
                                    owner._session.ActiveLayerID() == replacementActive && layer &&
                                    owner._session.Document()->layers.Identity() != state.records &&
                                    ReadPixel(owner._session, 3, 3, replacementPixel) &&
                                    replacementPixel == kTransparent;
                state.action = state.replacement && owner._session.PendingBrush() == nullptr &&
                               owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
            }
            else
                state.action = state.gate->failureResult && state.gate->failureResult->status == JobStatus::Failed &&
                               state.gate->failureResult->error == E_FAIL;

            (void)SetEvent(state.gate->release.get());
        };

        const SettleResult openingResult = SettleJobs(_jobs, 15000, dispatch, &state);
        if (openingResult != SettleResult::Settled)
            return false;
        const char* const heldControl = "worker-prepared-real-opening-held-before-owner-apply";
        const char* const prefixControl = "original-accepted-prefix-retained-until-terminal-ack";
        const char* const actionControl =
            fault == Fault::PreparedFailure     ? "injected-job-failure-after-real-preparation"
            : fault == Fault::EscapeBeforeApply ? "physical-escape-cancels-before-source-apply"
                                                : "core-document-replacement-invalidates-prepared-authority";
        if (!expect(scenario, heldControl, state.reached && state.held) ||
            !expect(scenario, prefixControl, state.prefix) || !expect(scenario, actionControl, state.action))
            return false;

        const ContinuationReceipt::Result expected =
            fault == Fault::PreparedFailure     ? ContinuationReceipt::Result{HostOutcome::Failed, E_FAIL}
            : fault == Fault::EscapeBeforeApply ? ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT}
                                                : ContinuationReceipt::Result{HostOutcome::Stale, E_ABORT};
        const ContinuationReceipt::Result receipt = _canvasBrushReceipt.Read();
        Pixel after{};
        const bool sourceState =
            fault == Fault::CoreReplacement
                ? state.replacement && _session.DocumentSerial() != serialBefore && _session.history.UndoCount() == 0
                : _session.Document() && _session.Document()->layers.Identity() == recordsBefore &&
                      _session.history.CurrentRevision() == revisionBefore && _session.history.UndoCount() == 0 &&
                      ReadPixel(_session, 3, 3, after) && after == before;
        if (!expect(scenario, "terminal-receipt-and-source-or-replacement-preserved",
                    receipt == expected && sourceState && _session.PendingBrush() == nullptr))
            return false;
        if (!expect(scenario, "prepared-payload-retired-offowner-after-drain",
                    gate->prepares.load() == 1 && gate->retired.load() && gate->retiredOffOwner.load()))
            return false;
        const bool drained = !_canvasBrushInput.Busy() && _canvasBrushInput.PendingCount() == 0 && !_utilityTicket &&
                             !_historyWork.Busy() && !_activeHostRequest && _hostRequests.Size() == 0 &&
                             _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0;
        if (!expect(scenario, "original-prefix-acknowledged-and-retained-charge-released-after-drain", drained))
            return false;
        _strokeOpeningTestGate.reset();
        return true;
    };

    if (!runFault("opening-preparation-failure", Fault::PreparedFailure) ||
        !runFault("escape-before-apply", Fault::EscapeBeforeApply) ||
        !runFault("stale-core-replacement", Fault::CoreReplacement))
        return 5;

    std::fprintf(stderr, "%s stroke-opening-host.summary cases=4 failures=%d\n", failures == 0 ? "PASS" : "FAIL",
                 failures);
    std::fprintf(stderr, "%s stroke-opening-host.failure-summary cases=3 failures=%d\n",
                 failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
