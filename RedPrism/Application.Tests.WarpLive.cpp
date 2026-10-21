// Actual hidden-owner original Warp live input qualification; ordinary activation remains separately gated.
#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PreparedLayerRows.h"
#include "Runtime.Tests.StrokeFinishTestGate.h"
#include "WarpOpeningWork.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr int kSide = 16;
constexpr DWORD kWait = 10000;
constexpr BrushSettings kSettings{6, .8, 0, 0, 0, 1};
constexpr Vec2 kDown{7.25, 7.25};
constexpr Vec2 kMove{8.25, 7.25};
constexpr Vec2 kUp{9.25, 7.25};

CanvasDocument Scene(Guid& active)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = fixture.height = kSide;
    std::array<std::uint8_t, kSide * kSide * 4> bytes{};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
        {
            const auto p = static_cast<std::size_t>((y * kSide + x) * 4);
            bytes[p] = static_cast<std::uint8_t>(x * 11 + y * 3);
            bytes[p + 1] = static_cast<std::uint8_t>(y * 13 + x * 2);
            bytes[p + 2] = static_cast<std::uint8_t>(x * 5 + y * 7);
            bytes[p + 3] = 255;
        }
    auto layer = ImageLayer::Blank(L"Warp live host fixture", {kSide, kSide});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset =
        RasterAsset{Raster::Make(kSide, kSide, RasterFormat::Bgra8888, bytes.data(), bytes.size()), {}, layer.name};
    active = layer.id;
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

// Independent scalar reference: original contiguous WarpStroke::Append followed by BrushStroke::Append/Flush.
// It deliberately bypasses the live preview reader, live replay, GPU command path and host publication route.
std::optional<BrushStroke::Commit> Reference(const ImageLayer& original, SmearMode mode, bool zeroPointLost,
                                             bool duplicateMove = false)
{
    if (!original.asset)
        return {};
    WarpStroke warp(original.asset->image, mode, kSettings);
    warp.Append(kDown); // Warp Down primes the source/carried sample; it is not a dab.
    if (!zeroPointLost)
    {
        warp.Append(kMove);
        if (duplicateMove)
            warp.Append(kMove);
        warp.Append(kUp);
    }
    auto settings = kSettings;
    settings.diameter = 10;
    settings.hardness = settings.opacity = 1;
    BrushStroke stroke(original, false, settings, kSide, kSide, nullptr);
    stroke.SetSample(warp.Image(), 0, 0, true);
    for (const auto point : warp.Points())
        if (!stroke.Append(point))
            return {};
    return stroke.Flush() ? stroke.PaintSnapshot() : std::nullopt;
}

std::optional<Raster> WarpPreviewReference(const ImageLayer& original, SmearMode mode, bool includeMove)
{
    if (!original.asset)
        return {};
    WarpStroke warp(original.asset->image, mode, kSettings);
    warp.Append(kDown);
    if (includeMove)
        warp.Append(kMove);
    auto image = warp.Image();
    return image.HasStorage() ? std::optional<Raster>{std::move(image)} : std::nullopt;
}

bool SamePixels(const Raster& actual, const Raster& reference, int tolerance) noexcept
{
    if (!actual.HasStorage() || !reference.HasStorage() || actual.Width() != kSide || actual.Height() != kSide ||
        reference.Width() != kSide || reference.Height() != kSide)
        return false;
    std::array<std::uint8_t, kSide * 4> a{}, b{};
    for (int y = 0; y < kSide; ++y)
    {
        actual.ReadRegion(0, y, kSide, 1, a.data(), kSide * 4, 0);
        reference.ReadRegion(0, y, kSide, 1, b.data(), kSide * 4, 0);
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::abs(static_cast<int>(a[i]) - b[i]) > tolerance)
                return false;
    }
    return true;
}

// Harness-only readback of an acknowledged GPU root. The same 2/255 tolerance is used by WarpFinish.
bool SameDisplay(RecordedDisplayGraph* graph, const Raster& reference, int tolerance = 2) noexcept
{
    auto* composition = graph ? graph->CompositionForTestOwner() : nullptr;
    std::vector<std::uint8_t> bytes;
    unsigned int width = 0;
    unsigned int height = 0;
    if (!composition || FAILED(composition->Readback(bytes, width, height)) || width != kSide || height != kSide ||
        bytes.size() != static_cast<std::size_t>(kSide * kSide * 4))
        return false;
    std::array<std::uint8_t, kSide * 4> row{};
    for (int y = 0; y < kSide; ++y)
    {
        reference.ReadRegion(0, y, kSide, 1, row.data(), kSide * 4, 0);
        for (std::size_t i = 0; i < row.size(); ++i)
            if (std::abs(static_cast<int>(bytes[static_cast<std::size_t>(y) * row.size() + i]) - row[i]) > tolerance)
                return false;
    }
    return true;
}
} // namespace

// Main declares and registers this hidden Application self-test when the live Warp host route is integrated.
int Application::RunWarpLiveHostSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* control) noexcept
    {
        std::printf("%s application-warp-live.%s.%s\n", condition ? "PASS" : "FAIL", scenario, control);
        std::fflush(stdout);
        ++checks;
        failures += condition ? 0u : 1u;
    };
    const auto fail = [](const char* name) noexcept
    {
        std::printf("FAIL application-warp-live.setup.%s\n", name);
        std::fflush(stdout);
        return 5;
    };
    std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
    if (!owner)
        return fail("owner");
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (!owner->_shutdownComplete)
            {
                owner->CancelHostContinuations();
                if (SettleJobs(owner->_jobs, kWait, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
                    std::_Exit(5);
                owner->Shutdown();
                (void)PumpMessages(); // Consume WM_QUIT from this independent hidden owner.
            }
        });
    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, kWait, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
    const auto pumpUntil = [&](auto predicate) noexcept
    {
        const auto deadline = GetTickCount64() + kWait;
        for (;;)
        {
            if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                return false;
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (predicate())
                return true;
            if (owner->_canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
                return false;
            if (owner->_jobs.HasUiWork() || owner->_ownerWork.HasReady())
                continue;
            const auto now = GetTickCount64();
            if (now >= deadline)
                return false;
            const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
            const auto result = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                            MWMO_INPUTAVAILABLE);
            if (result != WAIT_OBJECT_0 && result != WAIT_OBJECT_0 + 1)
                return false;
        }
    };
    const auto point = [&](DxUi::PointerAction action, Vec2 documentPoint) noexcept
    {
        DxUi::PointerEvent event{};
        event.action = action;
        event.modifiers = action == DxUi::PointerAction::Up ? 0u : MK_LBUTTON;
        const auto view = owner->_viewport.ViewFromDocument(documentPoint, owner->DocumentSize());
        owner->RecordCanvasBrush(event, view);
        return true;
    };
    const auto beginWarp = [&](SmearMode mode) noexcept
    {
        if (owner->_session.smearMode != mode)
            return false;
        DxUi::PointerEvent down{};
        down.action = DxUi::PointerAction::Down;
        down.modifiers = MK_LBUTTON;
        if (!owner->QueueCanvasBrush(down, owner->_viewport.ViewFromDocument(kDown, owner->DocumentSize())))
            return false;
        const auto* captured = owner->_canvasBrushInput.CapturedContext(owner->_canvasBrushInput.Opening());
        return captured && captured->smear == mode && owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
    };
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return fail("hidden-window-initialize");
    owner->BindCompositionDevice();
    Guid initial{};
    owner->ReplaceDocument(Scene(initial), initial, {});
    owner->DocumentChanged();
    if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
        !owner->_strokeFinishSource.Start() || !owner->_brushEngine.Ready() || !settle())
        return fail("owner-sources-and-initial-display");

    // Experimental opt-in. This switch and captured CanvasBrushContext::smear are integration dependencies.
    owner->_asyncWarpLiveEnabled = true;
    for (const auto mode : {SmearMode::Liquify, SmearMode::Smudge})
    {
        const char* name = mode == SmearMode::Liquify ? "liquify" : "smudge";
        Guid active{};
        auto document = Scene(active);
        const auto original = document.layers.front();
        owner->ReplaceDocument(std::move(document), active, {});
        owner->_tool = EditorTool::Smear;
        owner->_session.brushSettings = kSettings;
        owner->_session.smearMode = mode;
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        owner->SyncChrome();
        if (!settle())
            return fail("mode-scene");

        const auto revision = owner->_session.history.CurrentRevision();
        auto gate = std::make_shared<StrokeFinishTestGate>(true);
        gate->holdSample = StrokeFinishTestGate::SampleBoundary::OpeningPrepared;
        gate->holdSampleNumber = 0; // OpeningPrepared occurs before the first live sample is prepared.
        owner->_strokeFinishTestGate = gate;
        if (!beginWarp(mode) ||
            !pumpUntil([&]() noexcept { return gate->sampleBoundaryVisits.load(std::memory_order_acquire) == 1; }))
            return fail("original-parent-live-down");
        const auto ordinal = owner->_activeHostRequest ? owner->_activeHostRequest->ordinal : 0;
        const auto ticket = owner->_utilityTicket.value_or(JobTicket{});
        auto* const livePreviewAtDown =
            owner->_acknowledgedCommittedDisplay; // No-dab Down retains the original GPU root.
        if (!ordinal || !ticket.generation || !livePreviewAtDown)
            return fail("live-down-state");
        expect(owner->_activeHostRequest && owner->_activeHostRequest->ordinal == ordinal &&
                   owner->_utilityTicket == ticket &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   owner->_session.history.CurrentRevision() == revision && owner->_session.history.UndoCount() == 0 &&
                   SameDisplay(livePreviewAtDown, original.asset->image, 0),
               name, "down-primes-without-dab-and-keeps-original-ticket-and-ordinal");
        if (!SetEvent(gate->release.get()) || !ResetEvent(gate->reached.get()) ||
            !pumpUntil([&]() noexcept { return owner->_session.PendingWarp() != nullptr; }))
            return fail("opening-apply");
        if (!ResetEvent(gate->release.get()))
            return fail("arm-final-preparation-hold");

        // Change the live setting after Down. The accepted gesture and all later worker input must retain its captured
        // mode.
        owner->_session.smearMode = mode == SmearMode::Liquify ? SmearMode::Smudge : SmearMode::Liquify;
        const auto* captured = owner->_canvasBrushInput.CapturedContext(owner->_canvasBrushInput.Opening());
        expect(captured && captured->smear == mode && owner->_utilityTicket == ticket && owner->_activeHostRequest &&
                   owner->_activeHostRequest->ordinal == ordinal,
               name, "later-smear-setting-edit-does-not-change-down-captured-mode-or-parent");
        point(DxUi::PointerAction::Move, kMove);
        if (!pumpUntil(
                [&]() noexcept
                {
                    return gate->samplesAcknowledged.load(std::memory_order_acquire) >= 1 &&
                           gate->previewsPublished.load(std::memory_order_acquire) >= 1;
                }))
            return fail("live-move-acknowledgement");
        auto* const movedPreview = owner->_acknowledgedStrokePreview;
        const auto moveReference = WarpPreviewReference(original, mode, true);
        expect(movedPreview && owner->_utilityTicket == ticket && owner->_activeHostRequest &&
                   owner->_activeHostRequest->ordinal == ordinal && gate->samplesOffOwner.load() >= 1 &&
                   gate->previewsPublished.load() == 1 && moveReference &&
                   SameDisplay(movedPreview, moveReference.value(), 0),
               name, "move-uses-one-original-parent-and-publishes-off-owner-preview");

        point(DxUi::PointerAction::Move, kMove); // Exact duplicate: consumed by the same input stream and parent.
        if (!pumpUntil([&]() noexcept { return gate->samplesAcknowledged.load(std::memory_order_acquire) >= 2; }))
            return fail("duplicate-acknowledgement");
        expect(owner->_acknowledgedStrokePreview == movedPreview && owner->_utilityTicket == ticket &&
                   owner->_activeHostRequest && owner->_activeHostRequest->ordinal == ordinal &&
                   gate->previewsPublished.load() == 1,
               name, "duplicate-is-consumed-without-new-dab-or-preview-replacement");

        point(DxUi::PointerAction::Up, kUp);
        if (!pumpUntil([&]() noexcept { return gate->prepares.load(std::memory_order_acquire) == 1; }))
            return fail("held-final-preparation");
        expect(owner->_utilityTicket == ticket && owner->_activeHostRequest &&
                   owner->_activeHostRequest->ordinal == ordinal &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending,
               name, "final-preparation-hold-retains-original-parent-and-pending-host-receipt");
        if (!SetEvent(gate->release.get()))
            return fail("release-final-preparation");
        if (!settle())
            return fail("actual-up-finalization");
        const auto reference = Reference(original, mode, false, true);
        const auto* layer = owner->_session.ActiveLayer();
        expect(reference && layer && layer->asset && SamePixels(layer->asset->image, reference->image, 2) &&
                   layer->transform == reference->transform && owner->_session.history.UndoCount() == 1 &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                   owner->_acknowledgedCommittedDisplay &&
                   SameDisplay(owner->_acknowledgedCommittedDisplay, reference->image),
               name, "up-matches-independent-scalar-reference-and-actual-final-gpu-root-with-one-history-entry");
        expect(owner->_preparedLayerRows && owner->_preparedLayerRows->FindVisibleRow(active) &&
                   !owner->_utilityTicket && !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() &&
                   gate->retired.load() && gate->retiredOffOwner.load(),
               name, "rows-host-input-and-complete-parent-retire-after-off-owner-worker");
        owner->_strokeFinishTestGate.reset();
        owner->OnCommand(ChromeHost::kCommandUndo);
        const bool undone = settle();
        layer = owner->_session.ActiveLayer();
        expect(undone && layer && layer->asset && SamePixels(layer->asset->image, original.asset->image, 0) &&
                   owner->_session.history.CurrentRevision() == revision && owner->_session.history.UndoCount() == 0,
               name, "one-undo-restores-original-pattern-and-revision-exactly");
    }

    // A Down followed immediately by capture loss is a zero-point live Warp: the prime is not a dab, so no
    // brush replay point or history entry may be manufactured. This goes through the physical stream and parent.
    {
        constexpr SmearMode mode = SmearMode::Smudge;
        constexpr const char* name = "smudge-zero-point-lost";
        Guid active{};
        auto document = Scene(active);
        const auto original = document.layers.front();
        owner->ReplaceDocument(std::move(document), active, {});
        owner->_tool = EditorTool::Smear;
        owner->_session.smearMode = mode;
        owner->_session.brushSettings = kSettings;
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->DocumentChanged();
        if (!settle())
            return fail("zero-point-scene");
        auto lostGate = std::make_shared<StrokeFinishTestGate>(false);
        owner->_strokeFinishTestGate = lostGate;
        if (!beginWarp(mode) ||
            !pumpUntil([&]() noexcept { return owner->_session.PendingWarp() && owner->_utilityTicket.has_value(); }))
            return fail("zero-point-down");
        const auto ordinal = owner->_activeHostRequest->ordinal;
        const auto ticket = owner->_utilityTicket.value();
        owner->LoseCanvasBrush(); // Records one Lost terminal in PointerInputStream; no direct worker callback.
        if (!settle())
            return fail("zero-point-lost-finalization");
        const auto* layer = owner->_session.ActiveLayer();
        expect(layer && layer->asset && SamePixels(layer->asset->image, original.asset->image, 0) &&
                   owner->_session.history.UndoCount() == 0 &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded,
               name, "lost-after-prime-is-no-op-without-history");
        expect(!owner->_activeHostRequest && !owner->_utilityTicket && !owner->_canvasBrushInput.Busy() &&
                   lostGate->retired.load() && lostGate->retiredOffOwner.load() && ordinal != 0 &&
                   ticket.generation != 0,
               name, "zero-point-lost-retires-original-parent-and-input-on-worker-retirement");
        owner->_strokeFinishTestGate.reset();
    }
    if (checks != 18)
    {
        std::printf("FAIL application-warp-live.fixed-check-count expected=18 actual=%u\n", checks);
        ++failures;
    }
    std::printf("SUMMARY application-warp-live checks=%u failures=%u expected=18\n", checks, failures);
    return failures == 0 ? 0 : 5;
}
