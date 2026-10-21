#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "Runtime.Tests.StrokeFinishTestGate.h"
#include "Selection.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr int kSide = 8;
constexpr DWORD kWaitMs = 10000;
using Pixel = std::array<std::uint8_t, 4>;
constexpr Pixel kTransparent{0, 0, 0, 0};
constexpr Pixel kRed{0, 0, 255, 255};

CanvasDocument MakeInputScene(Guid& active, bool clipped = false)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;

    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    auto layer = ImageLayer::Blank(L"Stroke input fixture", {kSide, kSide});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset =
        RasterAsset{Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, layer.name};
    active = layer.id;
    fixture.layers.push_back(std::move(layer));
    if (clipped)
        fixture.selection = DocumentSelection{false, SelectionGeometry::Rect(2, 2, 4, 4)};
    return std::move(fixture).Seal();
}

bool ReadPixel(const DocumentSession& session, int x, int y, Pixel& pixel) noexcept
{
    const auto* const layer = session.ActiveLayer();
    if (!layer || !layer->asset || x < 0 || y < 0 || x >= kSide || y >= kSide)
        return false;
    const auto& image = layer->asset->image;
    if (image.Format() != RasterFormat::Bgra8888 || x >= image.Width() || y >= image.Height())
        return false;
    image.ReadRegion(x, y, 1, 1, pixel.data(), 4, 0);
    return true;
}

bool ReadDisplayPixel(RecordedDisplayGraph* graph, int x, int y, Pixel& pixel) noexcept
{
    auto* const composition = graph ? graph->CompositionForTestOwner() : nullptr;
    if (!composition || x < 0 || y < 0 || x >= kSide || y >= kSide)
        return false;
    std::vector<std::uint8_t> bytes;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(composition->Readback(bytes, width, height)) || width != kSide || height != kSide ||
        bytes.size() != static_cast<std::size_t>(kSide * kSide * 4))
        return false;
    const auto offset = static_cast<std::size_t>((y * kSide + x) * 4);
    std::copy_n(bytes.data() + offset, pixel.size(), pixel.data());
    return true;
}
} // namespace

int Application::RunStandaloneStrokeSampleInputSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* control) noexcept
    {
        std::printf("%s application-stroke-sample-input.%s.%s\n", condition ? "PASS" : "FAIL", scenario, control);
        std::fflush(stdout);
        ++checks;
        failures += condition ? 0u : 1u;
    };
    const auto setupFailure = [](const char* control) noexcept
    {
        std::printf("FAIL application-stroke-sample-input.setup.%s\n", control);
        std::fflush(stdout);
        return 5;
    };
    const auto makeGate = [](bool holdFinal = false) noexcept -> std::shared_ptr<StrokeFinishTestGate>
    {
        // Gate construction is a fixture-admission boundary; allocation failure becomes a named setup failure.
        try
        {
            return std::make_shared<StrokeFinishTestGate>(holdFinal);
        }
        catch (const std::bad_alloc&)
        {
            return {};
        }
    };

    std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
    if (!owner)
        return setupFailure("actual-hidden-owner-allocation");
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (owner->_strokeFinishTestGate)
                (void)SetEvent(owner->_strokeFinishTestGate->release.get());
            if (!owner->_shutdownComplete)
            {
                owner->CancelHostContinuations();
                if (SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
                    std::_Exit(5);
                owner->Shutdown();
                (void)PumpMessages(); // Consume WM_QUIT from this fixture's own hidden window.
            }
        });
    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };

    // Wait only on the real scheduler event and message queue. The deadline diagnoses a stuck test; it is not a
    // latency assertion. Drain/apply is the actual Application owner path used by the hidden harness.
    const auto pumpUntil =
        [&](const std::shared_ptr<StrokeFinishTestGate>& gate, HANDLE target, unsigned acknowledged) noexcept
    {
        const ULONGLONG deadline = GetTickCount64() + kWaitMs;
        HANDLE waits[]{static_cast<HANDLE>(owner->_jobs.WakeHandle()), target};
        for (;;)
        {
            if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                return false;
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if ((target && WaitForSingleObject(target, 0) == WAIT_OBJECT_0) ||
                (gate && acknowledged != 0 &&
                 gate->samplesAcknowledged.load(std::memory_order_acquire) >= acknowledged))
                return true;
            if (owner->_canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
                return false;
            if (owner->_jobs.HasUiWork() || owner->_ownerWork.HasReady())
                continue;
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline)
                return false;
            const DWORD count = target ? 2u : 1u;
            const DWORD waited = MsgWaitForMultipleObjectsEx(count, waits, static_cast<DWORD>(deadline - now),
                                                             QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (waited == WAIT_OBJECT_0 + count || waited < WAIT_OBJECT_0 + count)
                continue;
            return false;
        }
    };
    const auto waitForSamples = [&](const std::shared_ptr<StrokeFinishTestGate>& gate, unsigned count) noexcept
    { return pumpUntil(gate, nullptr, count); };
    const auto waitForGate = [&](const std::shared_ptr<StrokeFinishTestGate>& gate) noexcept
    { return gate && gate->reached && pumpUntil(gate, gate->reached.get(), 0); };

    const auto installScene = [&](Guid& active, bool clipped = false) noexcept
    {
        auto scene = MakeInputScene(active, clipped);
        owner->ReplaceDocument(std::move(scene), active, {});
        owner->_tool = EditorTool::Brush;
        owner->_spacePan = false;
        owner->_drag = {};
        owner->_session.brushMode = BrushToolMode::Paint;
        owner->_session.brushSettings = BrushSettings{4, 1, 1, 0, 0, 1};
        owner->_session.SetForegroundColor(0xFFFF0000u);
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        owner->SyncChrome();
        return settle();
    };
    const auto down = [&](Vec2 point, unsigned int modifiers = MK_LBUTTON) noexcept
    {
        DxUi::PointerEvent event{};
        event.action = DxUi::PointerAction::Down;
        event.modifiers = modifiers;
        return owner->QueueCanvasBrush(event, owner->_viewport.ViewFromDocument(point, owner->DocumentSize()));
    };
    const auto move = [&](Vec2 point, unsigned int modifiers = MK_LBUTTON) noexcept
    {
        DxUi::PointerEvent event{};
        event.action = DxUi::PointerAction::Move;
        event.modifiers = modifiers;
        owner->RecordCanvasBrush(event, owner->_viewport.ViewFromDocument(point, owner->DocumentSize()));
    };
    const auto up = [&](Vec2 point) noexcept
    {
        DxUi::PointerEvent event{};
        event.action = DxUi::PointerAction::Up;
        owner->RecordCanvasBrush(event, owner->_viewport.ViewFromDocument(point, owner->DocumentSize()));
    };
    const auto displayIs = [&](int x, int y, const Pixel& expected) noexcept
    {
        Pixel pixel{};
        return ReadDisplayPixel(owner->_acknowledgedStrokePreview, x, y, pixel) && pixel == expected;
    };

    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return setupFailure("hidden-warp-application-initialize");
    owner->BindCompositionDevice();
    Guid initial{};
    auto firstScene = MakeInputScene(initial);
    owner->ReplaceDocument(std::move(firstScene), initial, {});
    owner->DocumentChanged();
    if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
        !owner->_strokeFinishSource.Start() || !settle() || !owner->_renderer.IsWarp())
        return setupFailure("warp-committed-display-rows-and-owner-source");
    owner->_asyncStrokeFinishEnabled = true;
    owner->_asyncStrokeSamplesEnabled = true;

    // A Shift-click is two independently acknowledged live samples: the previous target endpoint starts the line,
    // then the physical Down point extends it. Fixed pixel expectations are the legacy opaque-red brush result.
    {
        Guid active{};
        if (!installScene(active))
            return setupFailure("shift-scene");
        auto gate = makeGate();
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("shift-events");
        owner->_strokeFinishTestGate = gate;
        if (!down({1, 3}) || !waitForSamples(gate, 1))
            return setupFailure("shift-first-down-ack");
        move({2, 3});
        if (!waitForSamples(gate, 2))
            return setupFailure("shift-first-endpoint-ack");
        up({2, 3});
        if (!settle() || owner->_session.ShiftLineStart() != std::optional<Vec2>{Vec2{2, 3}})
            return setupFailure("shift-first-stroke-commit");

        gate = makeGate();
        if (!gate)
            return setupFailure("shift-second-events");
        owner->_strokeFinishTestGate = gate;
        if (!down({6, 6}, MK_LBUTTON | MK_SHIFT) || !waitForSamples(gate, 1))
            return setupFailure("shift-previous-endpoint-sample-ack");
        expect(displayIs(2, 3, kRed) && displayIs(6, 6, kTransparent), "shift",
               "first-of-two-new-stroke-samples-paints-previous-endpoint-only");
        if (!waitForSamples(gate, 2))
            return setupFailure("shift-physical-down-sample-ack");
        expect(displayIs(6, 6, kRed), "shift", "second-new-stroke-sample-paints-click-point");
        up({6, 6});
        Pixel previous{}, clicked{};
        expect(settle() && owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                   ReadPixel(owner->_session, 2, 3, previous) && previous == kRed &&
                   ReadPixel(owner->_session, 6, 6, clicked) && clicked == kRed &&
                   owner->_session.history.UndoCount() == 2,
               "shift", "shift-uses-same-target-prior-endpoint-and-commits-literal-pixels");
        owner->_strokeFinishTestGate.reset();
    }

    // Capture loss seals and commits precisely the accepted Down/Move prefix, with no synthetic point added.
    {
        Guid active{};
        if (!installScene(active))
            return setupFailure("lost-scene");
        auto gate = makeGate(true);
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("lost-events");
        owner->_strokeFinishTestGate = gate;
        if (!down({1, 1}))
            return setupFailure("lost-down");
        move({3, 1});
        owner->LoseCanvasBrush();
        if (!waitForGate(gate) || owner->_canvasBrushInput.Sampling() || !gate->holdAfterPreparation)
            return setupFailure("lost-real-final-worker-held");
        expect(owner->_canvasBrushInput.Overflowed() == false && owner->_session.history.UndoCount() == 0, "lost",
               "capture-loss-terminal-retains-accepted-prefix-before-publication");
        if (!SetEvent(gate->release.get()) || !settle())
            return setupFailure("lost-final-worker-release");
        Pixel accepted{}, prefix{}, absent{};
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                   owner->_session.history.UndoCount() == 1 && ReadPixel(owner->_session, 1, 1, accepted) &&
                   accepted == kRed && ReadPixel(owner->_session, 3, 1, prefix) && prefix == kRed &&
                   ReadPixel(owner->_session, 7, 7, absent) && absent == kTransparent,
               "lost", "capture-loss-commits-literal-accepted-prefix-only");
        owner->_strokeFinishTestGate.reset();
    }

    // The first Undo cancels an open gesture and consumes the command without changing history (RF-904).
    {
        Guid active{};
        if (!installScene(active))
            return setupFailure("live-undo-scene");
        auto gate = makeGate();
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("live-undo-events");
        gate->holdSample = StrokeFinishTestGate::SampleBoundary::Prepared;
        gate->holdSampleNumber = 1;
        owner->_strokeFinishTestGate = gate;
        if (!down({2, 3}) || !waitForGate(gate))
            return setupFailure("live-undo-first-sample-held");
        move({4, 3});
        move({5, 3});
        owner->OnCommand(ChromeHost::kCommandUndo);
        expect(owner->_canvasBrushInput.Sampling() && owner->_canvasBrushInput.PendingCount() == 2 &&
                   owner->_canvasBrushCancelledOpening == owner->_canvasBrushInput.Opening() &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   owner->_session.history.UndoCount() == 0 && owner->_hostRequests.Size() == 0,
               "undo-live", "undo-with-live-pointer-cancels-original-before-queueing-history");
        if (!SetEvent(gate->release.get()) || !settle())
            return setupFailure("live-undo-drain-and-queued-command");
        Pixel undone{};
        const auto receipt = owner->_canvasBrushReceipt.Read();
        expect(receipt.outcome == HostOutcome::Cancelled && receipt.error == static_cast<std::int32_t>(E_ABORT) &&
                   owner->_session.history.UndoCount() == 0 && !owner->_session.history.CanRedo() &&
                   ReadPixel(owner->_session, 4, 3, undone) && undone == kTransparent,
               "undo-live", "live-undo-drops-private-stroke-and-preserves-history");
        owner->_strokeFinishTestGate.reset();
    }

    // Physical Up seals the gesture even when its accepted samples have not yet been prepared. Undo then queues
    // behind the same original ticket and observes its committed history entry.
    {
        Guid active{};
        if (!installScene(active))
            return setupFailure("sealed-undo-scene");
        auto gate = makeGate();
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("sealed-undo-events");
        gate->holdSample = StrokeFinishTestGate::SampleBoundary::Prepared;
        gate->holdSampleNumber = 1;
        owner->_strokeFinishTestGate = gate;
        if (!down({2, 3}) || !waitForGate(gate))
            return setupFailure("sealed-undo-first-sample-held");
        move({4, 3});
        move({5, 3});
        up({5, 3});
        owner->OnCommand(ChromeHost::kCommandUndo);
        expect(!owner->_canvasBrushInput.Sampling() && owner->_canvasBrushInput.PendingCount() == 3 &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   owner->_session.history.UndoCount() == 0 && owner->_hostRequests.Size() == 1,
               "undo-sealed", "sealed-undo-queues-behind-original-pending-samples");
        if (!SetEvent(gate->release.get()) || !settle())
            return setupFailure("sealed-undo-drain-and-queued-command");
        Pixel undone{};
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                   owner->_session.history.UndoCount() == 0 && owner->_session.history.RedoName() == L"Brush Stroke" &&
                   ReadPixel(owner->_session, 4, 3, undone) && undone == kTransparent,
               "undo-sealed", "sealed-undo-runs-after-original-samples-commit");
        owner->_strokeFinishTestGate.reset();
    }

    // The same continuation ordering applies after physical Up: Undo queues behind the real asynchronous finish.
    {
        Guid active{};
        if (!installScene(active))
            return setupFailure("up-undo-scene");
        auto gate = makeGate(true);
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("up-undo-events");
        owner->_strokeFinishTestGate = gate;
        if (!down({2, 4}))
            return setupFailure("up-undo-down");
        move({4, 4});
        up({4, 4});
        if (!waitForGate(gate) || owner->_canvasBrushInput.Sampling())
            return setupFailure("up-undo-real-finish-held");
        owner->OnCommand(ChromeHost::kCommandUndo);
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   owner->_session.history.UndoCount() == 0 && owner->_activeHostRequest.has_value(),
               "undo-after-up", "undo-remains-queued-on-original-up-ticket");
        if (!SetEvent(gate->release.get()) || !settle())
            return setupFailure("up-undo-final-and-queued-command");
        Pixel undone{};
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                   owner->_session.history.UndoCount() == 0 && owner->_session.history.RedoName() == L"Brush Stroke" &&
                   ReadPixel(owner->_session, 3, 4, undone) && undone == kTransparent,
               "undo-after-up", "queued-undo-removes-the-just-published-up-stroke");
        owner->_strokeFinishTestGate.reset();
    }

    // Fill the actual fixed 512-event move ring before pumping any owner work. Up/Lost has its independent terminal
    // slot; the refused point is not substituted for the last accepted physical sample.
    {
        Guid active{};
        if (!installScene(active))
            return setupFailure("overflow-scene");
        auto gate = makeGate();
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("overflow-events");
        owner->_strokeFinishTestGate = gate;
        if (!down({3, 3}))
            return setupFailure("overflow-down");
        for (std::size_t index = 1; index < Application::CanvasBrushInput::kCapacity; ++index)
            move({4, 3});
        move({7, 7});
        expect(owner->_canvasBrushInput.Overflowed() && !owner->_canvasBrushInput.Sampling() &&
                   owner->_canvasBrushInput.PendingCount() == Application::CanvasBrushInput::kCapacity + 1 &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending,
               "overflow", "physical-capacity-refuses-next-point-and-preserves-separate-loss-terminal");
        if (!settle())
            return setupFailure("overflow-real-sample-and-final-work-settle");
        Pixel accepted{}, refused{};
        const auto receipt = owner->_canvasBrushReceipt.Read();
        expect(receipt.outcome == HostOutcome::Failed &&
                   receipt.error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
                   owner->_session.history.UndoCount() == 1 && ReadPixel(owner->_session, 3, 3, accepted) &&
                   accepted == kRed && ReadPixel(owner->_session, 7, 7, refused) && refused == kTransparent,
               "overflow", "exact-quota-error-commits-accepted-prefix-and-excludes-refused-point");
        owner->_strokeFinishTestGate.reset();
    }

    // The selection fixture and literal pixels prove that the async opening/sample/final path carries the captured
    // selection coverage all the way to the committed document.
    {
        Guid active{};
        if (!installScene(active, true))
            return setupFailure("selection-scene");
        if (!down({0, 4}))
            return setupFailure("selection-down");
        move({7, 4});
        up({7, 4});
        if (!settle())
            return setupFailure("selection-stroke-settle");
        Pixel inside{}, outsideLeft{}, outsideRight{};
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                   owner->_session.history.UndoCount() == 1 && ReadPixel(owner->_session, 3, 4, inside) &&
                   inside == kRed && ReadPixel(owner->_session, 1, 4, outsideLeft) && outsideLeft == kTransparent &&
                   ReadPixel(owner->_session, 7, 4, outsideRight) && outsideRight == kTransparent,
               "selection", "selection-restricted-stroke-has-literal-inside-and-outside-pixels");
    }

    std::printf("%s application-stroke-sample-input.summary checks=%u failed=%u\n", failures ? "FAIL" : "PASS", checks,
                failures);
    return failures ? 5 : 0;
}
