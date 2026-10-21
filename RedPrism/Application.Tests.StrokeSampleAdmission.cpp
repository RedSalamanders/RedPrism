#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PreparedLayerRows.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Runtime.Tests.StrokeFinishTestGate.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace
{
constexpr int kWidth = 512;
constexpr int kHeight = 8;
constexpr DWORD kWaitMs = 10000;
using Pixel = std::array<std::uint8_t, 4>;
constexpr Pixel kRed{0, 0, 255, 255};

enum class SnapshotFault : std::uint8_t
{
    QueryFailure,
    ZeroAvailability,
};

struct SnapshotSource final
{
    DWORD ownerThread = 0;
    SnapshotFault fault = SnapshotFault::QueryFailure;
    bool armed = false;
    bool injected = false;
    bool wrongThread = false;
    unsigned calls = 0;
    unsigned ownerCalls = 0;
    unsigned osCalls = 0;
    unsigned injectedCalls = 0;
    std::uint64_t lastPhysical = 0;
    std::uint64_t lastCommit = 0;
};

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    auto& source = *static_cast<SnapshotSource*>(context);
    ++source.calls;
    if (GetCurrentThreadId() != source.ownerThread)
    {
        source.wrongThread = true;
        return false;
    }
    ++source.ownerCalls;
    if (source.armed && !source.injected)
    {
        source.injected = true;
        ++source.injectedCalls;
        if (source.fault == SnapshotFault::QueryFailure)
            return false;
        snapshot = {};
        return true;
    }

    MEMORYSTATUSEX status{sizeof(status)};
    if (!GlobalMemoryStatusEx(&status))
        return false;
    snapshot = {status.ullAvailPhys, status.ullAvailPageFile};
    source.lastPhysical = snapshot.availablePhysicalBytes;
    source.lastCommit = snapshot.availableCommitBytes;
    ++source.osCalls;
    return true;
}

CanvasDocument MakeClearScene(Guid& target)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kWidth;
    fixture.height = kHeight;
    auto layer = ImageLayer::Blank(L"Stroke admission source", {kWidth, kHeight});
    layer.transform.sampling = LayerSampling::Nearest;
    const auto image = Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888);
    layer.asset = RasterAsset{image, {}, layer.name};
    target = layer.id;
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

bool SourceHasNoPublishedPixels(const DocumentSession& session) noexcept
{
    const auto* layer = session.ActiveLayer();
    if (!layer || !layer->asset)
        return false;
    const auto& image = layer->asset->image;
    if (image.Width() != kWidth || image.Height() != kHeight)
        return false;
    std::array<std::uint8_t, kWidth * 4> row{};
    for (int y = 0; y < kHeight; ++y)
    {
        image.ReadRegion(0, y, kWidth, 1, row.data(), kWidth * 4, 0);
        if (!std::all_of(row.begin(), row.end(), [](std::uint8_t value) { return value == 0; }))
            return false;
    }
    return true;
}

bool ReadPreviewPixel(RecordedDisplayGraph* graph, int x, int y, Pixel& pixel) noexcept
{
    auto* composition = graph ? graph->CompositionForTestOwner() : nullptr;
    if (!composition)
        return false;
    std::vector<std::uint8_t> bytes;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(composition->Readback(bytes, width, height)) || width != kWidth || height != kHeight ||
        bytes.size() != static_cast<std::size_t>(kWidth) * kHeight * 4)
        return false;
    std::copy_n(bytes.data() + (static_cast<std::size_t>(y) * kWidth + x) * 4, 4, pixel.data());
    return true;
}
} // namespace

int Application::RunStandaloneStrokeSampleAdmissionSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool value, const char* scenario, const char* control) noexcept
    {
        std::printf("%s application-stroke-sample-admission.%s.%s\n", value ? "PASS" : "FAIL", scenario, control);
        std::fflush(stdout);
        ++checks;
        failures += value ? 0u : 1u;
        return value;
    };
    const auto fail = [](const char* scenario, const char* step) noexcept
    {
        std::printf("FAIL application-stroke-sample-admission.setup.%s.%s\n", scenario, step);
        std::fflush(stdout);
        return 5;
    };

    struct Case final
    {
        const char* name;
        SnapshotFault fault;
    };
    constexpr std::array cases{Case{"snapshot-failure", SnapshotFault::QueryFailure},
                               Case{"zero-physical-and-commit", SnapshotFault::ZeroAvailability}};

    for (const auto& test : cases)
    {
        SnapshotSource source;
        source.ownerThread = GetCurrentThreadId();
        source.fault = test.fault;
        const MemorySource memory{SampleMemory, &source};
        ApplicationProcessingTestProbe processingProbe;
        processingProbe.memory = memory;
        std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
        if (!owner)
            return fail(test.name, "application-allocation");
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                if (!owner->_shutdownComplete)
                {
                    owner->CancelHostContinuations();
                    if (SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
                        std::_Exit(5);
                    owner->Shutdown();
                }
                (void)PumpMessages();
            });
        const auto settle = [&]() noexcept
        { return SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
        const auto point = [&](DxUi::PointerAction action, Vec2 documentPoint) noexcept
        {
            DxUi::PointerEvent event{};
            event.action = action;
            event.modifiers = action == DxUi::PointerAction::Up ? 0u : MK_LBUTTON;
            const auto view = owner->_viewport.ViewFromDocument(documentPoint, owner->DocumentSize());
            if (action == DxUi::PointerAction::Down)
                return owner->QueueCanvasBrush(event, view);
            owner->RecordCanvasBrush(event, view);
            return true;
        };
        const auto pumpOnce = [&]() noexcept
        {
            if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                return false;
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            return true;
        };
        const auto waitForDown = [&](const std::shared_ptr<StrokeFinishTestGate>& gate) noexcept
        {
            const auto deadline = GetTickCount64() + kWaitMs;
            for (;;)
            {
                if (!pumpOnce())
                    return false;
                if (gate->samplesAcknowledged.load(std::memory_order_acquire) == 1)
                    return true;
                if (owner->_canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
                    return false;
                if (owner->_jobs.HasUiWork() || owner->_ownerWork.HasReady())
                    continue;
                const auto now = GetTickCount64();
                if (now >= deadline)
                    return false;
                const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                const auto result = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now),
                                                                QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (result != WAIT_OBJECT_0 && result != WAIT_OBJECT_0 + 1)
                    return false;
            }
        };
        const auto waitForTerminal = [&]() noexcept
        {
            const auto deadline = GetTickCount64() + kWaitMs;
            for (;;)
            {
                if (!pumpOnce())
                    return false;
                if (owner->_canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
                    return true;
                if (owner->_jobs.HasUiWork() || owner->_ownerWork.HasReady())
                    continue;
                const auto now = GetTickCount64();
                if (now >= deadline)
                    return false;
                const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                const auto result = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now),
                                                                QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (result != WAIT_OBJECT_0 && result != WAIT_OBJECT_0 + 1)
                    return false;
            }
        };

        owner->_processingTestProbe = &processingProbe;
        if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
            return fail(test.name, "actual-hidden-warp-initialize");
        owner->BindCompositionDevice();
        if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
            !owner->_strokeFinishSource.Start())
            return fail(test.name, "startup-recording-rows-and-original-source");
        Guid target{};
        auto scene = MakeClearScene(target);
        owner->ReplaceDocument(std::move(scene), target, {});
        owner->_tool = EditorTool::Brush;
        owner->_session.brushMode = BrushToolMode::Paint;
        owner->_session.brushSettings = BrushSettings{4, 1, 1, 0, 0, 1};
        owner->_session.SetForegroundColor(0xFFFF0000u);
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        if (!settle())
            return fail(test.name, "committed-display-rows-and-original-source-settle");
        owner->_asyncStrokeFinishEnabled = true;
        owner->_asyncStrokeSamplesEnabled = true;

        const auto baselineCpu = owner->_jobs.ReservedMemoryBytes();
        const auto baselineGpu = owner->_graphGpuBudget.ReservedOwner();
        const auto baselineRevision = owner->_session.history.CurrentRevision();
        auto gate = std::make_shared<StrokeFinishTestGate>(false);
        owner->_strokeFinishTestGate = gate;
        if (!point(DxUi::PointerAction::Down, {3, 3}) || !waitForDown(gate) || !owner->_utilityTicket ||
            !owner->_activeHostRequest)
            return fail(test.name, "acknowledged-down-on-original-parent");

        const auto originalTicket = owner->_utilityTicket.value();
        const auto originalOrdinal = owner->_activeHostRequest->ordinal;
        auto* const lastPreview = owner->_acknowledgedStrokePreview;
        const auto callsBeforeGrowth = source.calls;
        Pixel downPreview{};
        const bool downReference =
            lastPreview && ReadPreviewPixel(lastPreview, 3, 3, downPreview) && downPreview == kRed;
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending && lastPreview && downReference &&
                   gate->samplesAcknowledged.load(std::memory_order_acquire) == 1 &&
                   gate->previewsPublished.load() == 1 && owner->_utilityTicket == originalTicket &&
                   owner->_activeHostRequest && owner->_activeHostRequest->ordinal == originalOrdinal &&
                   owner->_acknowledgedStrokePreview == lastPreview && owner->_session.PendingBrush() &&
                   owner->_session.history.UndoCount() == 0 &&
                   owner->_session.history.CurrentRevision() == baselineRevision &&
                   SourceHasNoPublishedPixels(owner->_session) && owner->_jobs.ReservedMemoryBytes() > baselineCpu &&
                   owner->_graphGpuBudget.ReservedOwner() >= baselineGpu,
               test.name, "down-acknowledged-preview-is-private-and-source-history-stay-original");
        expect(source.calls == source.ownerCalls && source.osCalls != 0 && !source.wrongThread &&
                   source.lastPhysical != 0 && source.lastCommit != 0,
               test.name, "initial-memory-callback-uses-real-os-snapshot-on-owner");

        // Arm only after Down's exact preview acknowledgement. The next sample must cross from tile 0 to tile 1.
        source.armed = true;
        if (!point(DxUi::PointerAction::Move, {300, 3}))
            return fail(test.name, "queue-cross-tile-next-sample");
        if (!waitForTerminal())
            return fail(test.name, "terminal-growth-refusal-acknowledged");
        const auto receipt = owner->_canvasBrushReceipt.Read();
        expect(source.injected && source.injectedCalls == 1 && source.calls == callsBeforeGrowth + 1 &&
                   source.ownerCalls == source.calls && !source.wrongThread,
               test.name, "one-fresh-owner-snapshot-is-injected-on-next-sample");
        expect(gate->samplesAcknowledged.load(std::memory_order_acquire) == 1 &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Failed && receipt.error == E_OUTOFMEMORY,
               test.name, "cross-tile-growth-fails-with-exact-e-outofmemory-before-second-ack");
        expect(owner->_session.history.UndoCount() == 0 &&
                   owner->_session.history.CurrentRevision() == baselineRevision &&
                   SourceHasNoPublishedPixels(owner->_session),
               test.name, "failed-growth-publishes-no-source-pixel-or-history-prefix");

        if (!settle())
            return fail(test.name, "original-parent-and-retirements-settle");
        expect(gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load() &&
                   !owner->_utilityTicket && !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() &&
                   !owner->_session.PendingBrush() && !owner->_acknowledgedStrokePreview &&
                   !owner->_session.history.UndoCount(),
               test.name, "terminal-owner-retires-off-owner-and-live-input-preview-clear");
        expect(owner->_jobs.ReservedMemoryBytes() == baselineCpu &&
                   owner->_graphGpuBudget.ReservedOwner() == baselineGpu && owner->_jobs.ReleasingMemoryBytes() == 0,
               test.name, "cpu-gpu-reservations-and-releasing-bytes-return-to-pre-down-baseline");
        expect(source.calls == callsBeforeGrowth + 1 && source.injectedCalls == 1 && source.calls == source.ownerCalls,
               test.name, "memory-source-before-after-call-count-is-exactly-one");
        owner->_strokeFinishTestGate.reset();
        owner->Shutdown();
        (void)PumpMessages();
    }

    std::printf("%s application-stroke-sample-admission.summary checks=%u failed=%u scenarios=%zu\n",
                failures ? "FAIL" : "PASS", checks, failures, cases.size());
    return failures ? 5 : 0;
}
