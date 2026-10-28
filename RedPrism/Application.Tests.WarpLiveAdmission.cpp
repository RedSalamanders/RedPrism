#include "Application.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PreparedLayerRows.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Runtime.Tests.StrokeFinishTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "WarpStroke.h"

#include <algorithm>
#include <array>
#include <cstddef>
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
constexpr int kWidth = 512;
constexpr int kHeight = 8;
constexpr DWORD kWaitMs = 10000;
constexpr BrushSettings kSettings{6, .8, 0, 0, 0, 1};
constexpr Vec2 kDown{3.25, 3.25};
constexpr Vec2 kFirstMove{4.25, 3.25};
constexpr Vec2 kGrowthMove{300.25, 3.25};
constexpr unsigned int kControlsPerScenario = 11;

enum class SnapshotFault : std::uint8_t
{
    QueryFailure,
    ZeroPhysicalAndCommit
};
struct Case final
{
    const char* name;
    SmearMode mode;
    SnapshotFault fault;
};
struct SnapshotSource final
{
    DWORD ownerThread = 0;
    SnapshotFault fault = SnapshotFault::QueryFailure;
    bool armed = false;
    bool injected = false;
    bool wrongThread = false;
    unsigned int calls = 0;
    unsigned int ownerCalls = 0;
    unsigned int osCalls = 0;
    unsigned int injectedCalls = 0;
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

CanvasDocument MakeScene(Guid& target)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kWidth;
    fixture.height = kHeight;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kWidth) * kHeight * 4);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
        {
            const auto p = static_cast<std::size_t>((y * kWidth + x) * 4);
            bytes[p] = static_cast<std::uint8_t>(x * 11 + y * 3);
            bytes[p + 1] = static_cast<std::uint8_t>(y * 29 + x * 7);
            bytes[p + 2] = static_cast<std::uint8_t>(x * 5 + y * 41);
            bytes[p + 3] = 255;
        }
    auto layer = ImageLayer::Blank(L"Warp admission source", {kWidth, kHeight});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset =
        RasterAsset{Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888, bytes.data(), bytes.size()), {}, layer.name};
    target = layer.id;
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

bool SourceMatchesLiteralPattern(const DocumentSession& session) noexcept
{
    const auto* layer = session.ActiveLayer();
    if (!layer || !layer->asset)
        return false;
    const auto& image = layer->asset->image;
    if (image.Width() != kWidth || image.Height() != kHeight || image.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, kWidth * 4> row{};
    for (int y = 0; y < kHeight; ++y)
    {
        image.ReadRegion(0, y, kWidth, 1, row.data(), kWidth * 4, 0);
        for (int x = 0; x < kWidth; ++x)
        {
            const auto p = static_cast<std::size_t>(x * 4);
            if (row[p] != static_cast<std::uint8_t>(x * 11 + y * 3) ||
                row[p + 1] != static_cast<std::uint8_t>(y * 29 + x * 7) ||
                row[p + 2] != static_cast<std::uint8_t>(x * 5 + y * 41) || row[p + 3] != 255)
                return false;
        }
    }
    return true;
}

std::optional<Raster> WarpPreviewReference(const Raster& source, SmearMode mode) noexcept
{
    WarpStroke warp(source, mode, kSettings);
    if (!warp.IsValid())
        return {};
    warp.Append(kDown);
    warp.Append(kFirstMove);
    if (!warp.IsValid())
        return {};
    auto image = warp.Image();
    return image.HasStorage() ? std::optional<Raster>{std::move(image)} : std::nullopt;
}

bool DisplayMatches(RecordedDisplayGraph* graph, const Raster& reference) noexcept
{
    if (!reference.HasStorage() || reference.Format() != RasterFormat::Bgra8888 || reference.Width() != kWidth ||
        reference.Height() != kHeight)
        return false;
    auto* composition = graph ? graph->CompositionForTestOwner() : nullptr;
    std::vector<std::uint8_t> bytes;
    unsigned int width = 0;
    unsigned int height = 0;
    if (!composition || FAILED(composition->Readback(bytes, width, height)) || width != kWidth || height != kHeight ||
        bytes.size() != static_cast<std::size_t>(kWidth * kHeight * 4))
        return false;
    std::array<std::uint8_t, kWidth * 4> row{};
    for (int y = 0; y < kHeight; ++y)
    {
        reference.ReadRegion(0, y, kWidth, 1, row.data(), kWidth * 4, 0);
        const auto offset = static_cast<std::size_t>(y) * row.size();
        if (!std::equal(row.begin(), row.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset)))
            return false;
    }
    return true;
}
} // namespace

int Application::RunWarpLiveAdmissionSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned int checks = 0;
    unsigned int failures = 0;
    unsigned int scenarios = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* control) noexcept
    {
        std::printf("%s application-warp-live-admission.%s.%s\n", condition ? "PASS" : "FAIL", scenario, control);
        std::fflush(stdout);
        ++checks;
        failures += condition ? 0u : 1u;
    };
    const auto fail = [](const char* scenario, const char* step) noexcept
    {
        std::printf("FAIL application-warp-live-admission.setup.%s.%s\n", scenario, step);
        std::fflush(stdout);
        return 5;
    };
    constexpr std::array cases{
        Case{"liquify-query-failure", SmearMode::Liquify, SnapshotFault::QueryFailure},
        Case{"liquify-zero-physical-and-commit", SmearMode::Liquify, SnapshotFault::ZeroPhysicalAndCommit},
        Case{"smudge-query-failure", SmearMode::Smudge, SnapshotFault::QueryFailure},
        Case{"smudge-zero-physical-and-commit", SmearMode::Smudge, SnapshotFault::ZeroPhysicalAndCommit},
    };

    for (const auto& test : cases)
    {
        ++scenarios;
        SnapshotSource source;
        source.ownerThread = GetCurrentThreadId();
        source.fault = test.fault;
        const MemorySource memory{SampleMemory, &source};
        ApplicationProcessingTestProbe processingProbe;
        processingProbe.memory = memory;
        std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
        if (!owner)
            return fail(test.name, "application-allocation");
        owner->_processingTestProbe = &processingProbe;
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
                    (void)PumpMessages();
                }
            });
        const auto settle = [&]() noexcept
        { return SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
        const auto pumpOnce = [&]() noexcept
        {
            if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                return false;
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            return true;
        };
        const auto waitFor = [&](auto predicate) noexcept
        {
            const auto deadline = GetTickCount64() + kWaitMs;
            for (;;)
            {
                if (!pumpOnce())
                    return false;
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
                const auto result = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now),
                                                                QS_ALLINPUT, MWMO_INPUTAVAILABLE);
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
            if (action == DxUi::PointerAction::Down)
                return owner->QueueCanvasBrush(event, view);
            owner->RecordCanvasBrush(event, view);
            return true;
        };

        if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
            return fail(test.name, "actual-hidden-owner-initialize");
        owner->BindCompositionDevice();
        Guid target{};
        auto scene = MakeScene(target);
        owner->ReplaceDocument(std::move(scene), target, {});
        owner->_tool = EditorTool::Smear;
        owner->_session.smearMode = test.mode;
        owner->_session.brushSettings = kSettings;
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
            !owner->_strokeFinishSource.Start() || !owner->_brushEngine.Ready() || !settle())
            return fail(test.name, "display-rows-original-parent-and-compute-ready");

        owner->_asyncWarpLiveEnabled = true;
        const auto* originalLayer = owner->_session.ActiveLayer();
        if (!originalLayer || !originalLayer->asset)
            return fail(test.name, "literal-source-layer");
        const Raster originalSource = originalLayer->asset->image;
        const auto baselineCpu = owner->_jobs.ReservedMemoryBytes();
        const auto baselineGpu = owner->_graphGpuBudget.ReservedOwner();
        const auto baselineRevision = owner->_session.history.CurrentRevision();
        const auto baselineUndo = owner->_session.history.UndoCount();
        auto gate = std::make_shared<StrokeFinishTestGate>(false);
        owner->_strokeFinishTestGate = gate;

        if (!point(DxUi::PointerAction::Down, kDown) ||
            !waitFor(
                [&]() noexcept
                {
                    const auto* warp = owner->_session.PendingWarp();
                    return warp && warp->Points().empty() && owner->_utilityTicket && owner->_activeHostRequest;
                }))
            return fail(test.name, "actual-down-prime-no-dab");
        const JobTicket originalTicket = owner->_utilityTicket.value();
        const auto originalOrdinal = owner->_activeHostRequest.value().ordinal;
        auto* const originalCommittedRoot = owner->_acknowledgedCommittedDisplay;
        expect(originalTicket.generation != 0 && originalOrdinal != 0 && originalCommittedRoot &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   owner->_session.history.CurrentRevision() == baselineRevision &&
                   owner->_session.history.UndoCount() == baselineUndo &&
                   SourceMatchesLiteralPattern(owner->_session) &&
                   DisplayMatches(originalCommittedRoot, originalSource),
               test.name, "down-primes-live-warp-without-dab-or-document-history-change");

        if (!point(DxUi::PointerAction::Move, kFirstMove) ||
            !waitFor(
                [&]() noexcept
                {
                    return gate->samplesAcknowledged.load(std::memory_order_acquire) == 1 &&
                           gate->previewsPublished.load(std::memory_order_acquire) == 1 &&
                           owner->_acknowledgedStrokePreview;
                }))
            return fail(test.name, "first-real-move-exact-live-acknowledgement");
        auto* const firstPreview = owner->_acknowledgedStrokePreview;
        const auto previewReference = WarpPreviewReference(originalSource, test.mode);
        if (!previewReference)
            return fail(test.name, "independent-first-preview-reference");
        expect(firstPreview && firstPreview != originalCommittedRoot && gate->samplesAcknowledged.load() == 1 &&
                   gate->previewsPublished.load() == 1 && gate->previewsReplaced.load() == 0 &&
                   DisplayMatches(firstPreview, previewReference.value()),
               test.name, "first-move-has-one-acknowledged-original-live-preview");

        const auto callsBeforeGrowth = source.calls;
        const auto ownerCallsBeforeGrowth = source.ownerCalls;
        const auto osCallsBeforeGrowth = source.osCalls;
        expect(callsBeforeGrowth != 0 && callsBeforeGrowth == ownerCallsBeforeGrowth &&
                   ownerCallsBeforeGrowth == osCallsBeforeGrowth && source.injectedCalls == 0 && !source.wrongThread &&
                   source.lastPhysical != 0 && source.lastCommit != 0,
               test.name, "all-pre-growth-memory-samples-are-real-os-queries-on-owner");
        expect(owner->_utilityTicket == originalTicket && owner->_activeHostRequest &&
                   owner->_activeHostRequest.value().ordinal == originalOrdinal &&
                   owner->_session.history.CurrentRevision() == baselineRevision &&
                   owner->_session.history.UndoCount() == baselineUndo &&
                   SourceMatchesLiteralPattern(owner->_session) && owner->_jobs.ReservedMemoryBytes() > baselineCpu &&
                   owner->_graphGpuBudget.ReservedOwner() >= baselineGpu,
               test.name, "first-ack-retains-original-ticket-ordinal-source-history-and-parent-charge");

        source.armed = true;
        if (!point(DxUi::PointerAction::Move, kGrowthMove))
            return fail(test.name, "record-cross-tile-growth-move");
        expect(owner->_utilityTicket == originalTicket && owner->_activeHostRequest &&
                   owner->_activeHostRequest.value().ordinal == originalOrdinal &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   gate->samplesAcknowledged.load() == 1 && owner->_acknowledgedStrokePreview == firstPreview,
               test.name, "recorded-growth-event-preserves-first-ack-until-owner-admission");
        if (!waitFor([&]() noexcept { return owner->_canvasBrushReceipt.Read().outcome != HostOutcome::Pending; }))
            return fail(test.name, "growth-admission-terminal-receipt");
        const auto receipt = owner->_canvasBrushReceipt.Read();
        expect(source.injected && source.injectedCalls == 1 && source.calls == callsBeforeGrowth + 1 &&
                   source.ownerCalls == ownerCallsBeforeGrowth + 1 && source.osCalls == osCallsBeforeGrowth &&
                   !source.wrongThread,
               test.name, "exactly-one-fresh-owner-query-fails-growth-admission");
        expect(receipt.outcome == HostOutcome::Failed && receipt.error == E_OUTOFMEMORY &&
                   gate->samplesAcknowledged.load(std::memory_order_acquire) == 1 &&
                   gate->previewsPublished.load(std::memory_order_acquire) == 1 &&
                   gate->previewsReplaced.load(std::memory_order_acquire) == 0,
               test.name, "growth-returns-eoutofmemory-before-second-sample-or-preview-publication");

        if (!settle())
            return fail(test.name, "original-parent-and-retirement-workers-settle");
        expect(owner->_session.history.CurrentRevision() == baselineRevision &&
                   owner->_session.history.UndoCount() == baselineUndo && SourceMatchesLiteralPattern(owner->_session),
               test.name, "every-source-pixel-and-history-revision-remain-original-after-refusal");
        expect(gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load() &&
                   !owner->_utilityTicket && !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() &&
                   !owner->_session.HasPendingWarp() && !owner->_acknowledgedStrokePreview,
               test.name, "original-draft-cache-payload-retire-on-worker-and-live-input-preview-clear");
        expect(owner->_jobs.ReservedMemoryBytes() == baselineCpu &&
                   owner->_graphGpuBudget.ReservedOwner() == baselineGpu && owner->_jobs.ReleasingMemoryBytes() == 0,
               test.name, "cpu-gpu-and-releasing-charges-return-to-pre-down-baseline");
        expect(source.calls == callsBeforeGrowth + 1 && source.ownerCalls == source.calls &&
                   source.osCalls == osCallsBeforeGrowth && source.injectedCalls == 1 && !source.wrongThread,
               test.name, "memory-callback-before-after-counts-have-no-hidden-query");

        owner->_strokeFinishTestGate.reset();
        owner->Shutdown();
        (void)PumpMessages();
    }

    constexpr unsigned int kExpectedScenarios = 4;
    constexpr unsigned int kExpectedChecks = kExpectedScenarios * kControlsPerScenario;
    if (scenarios != kExpectedScenarios || checks != kExpectedChecks)
    {
        std::printf("FAIL application-warp-live-admission.fixed-registry expected=%u/%u actual=%u/%u\n",
                    kExpectedChecks, kExpectedScenarios, checks, scenarios);
        ++failures;
    }
    std::printf("SUMMARY application-warp-live-admission checks=%u failures=%u scenarios=%u expected=%u/%u\n", checks,
                failures, scenarios, kExpectedChecks, kExpectedScenarios);
    return failures == 0 ? 0 : 5;
}
