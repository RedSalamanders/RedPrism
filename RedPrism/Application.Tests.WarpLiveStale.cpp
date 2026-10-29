#include "Application.h"
#include "KeyboardState.h"
#include "Runtime.Tests.StrokeFinishTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <DxUi/Embedded.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace
{
constexpr int kSide = 16;
constexpr Vec2 kDown{7.25, 7.25};
constexpr Vec2 kMove{8.25, 7.25};
constexpr Vec2 kUp{9.25, 7.25};
constexpr BrushSettings kSettings{6, .8, 0, 0, 0, 1};
constexpr DWORD kWaitMs = 10000;
using Boundary = StrokeFinishTestGate::SampleBoundary;

enum class Invalidation : std::uint8_t
{
    Tool,
    ActiveLayer
};
struct Scenario final
{
    const char* name;
    Boundary boundary;
    SmearMode mode;
    Invalidation invalidation;
    unsigned preparedSamples;
};

std::uint8_t TargetByte(int x, int y, std::size_t channel) noexcept
{
    switch (channel)
    {
    case 0:
        return static_cast<std::uint8_t>(x * 11 + y * 3);
    case 1:
        return static_cast<std::uint8_t>(y * 13 + x * 2);
    default:
        return static_cast<std::uint8_t>(x * 5 + y * 7);
    }
}
std::uint8_t OtherByte(int x, int y, std::size_t channel) noexcept
{
    switch (channel)
    {
    case 0:
        return static_cast<std::uint8_t>(x * 3 + y * 17 + 91);
    case 1:
        return static_cast<std::uint8_t>(y * 5 + x * 19 + 47);
    default:
        return static_cast<std::uint8_t>(x * 23 + y * 7 + 13);
    }
}
ImageLayer MakeLayer(const wchar_t* name, bool target)
{
    auto layer = ImageLayer::Blank(name, {kSide, kSide});
    layer.transform.sampling = LayerSampling::Nearest;
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
        {
            const auto p = static_cast<std::size_t>((y * kSide + x) * 4);
            pixels[p] = target ? TargetByte(x, y, 0) : OtherByte(x, y, 0);
            pixels[p + 1] = target ? TargetByte(x, y, 1) : OtherByte(x, y, 1);
            pixels[p + 2] = target ? TargetByte(x, y, 2) : OtherByte(x, y, 2);
            pixels[p + 3] = 255;
        }
    layer.asset =
        RasterAsset{Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, layer.name};
    return layer;
}
CanvasDocument MakeScene(Guid& target, Guid& second)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = fixture.height = kSide;
    auto first = MakeLayer(L"Warp stale target", true);
    auto other = MakeLayer(L"Warp stale second", false);
    target = first.id;
    second = other.id;
    fixture.layers.push_back(std::move(first));
    fixture.layers.push_back(std::move(other));
    return std::move(fixture).Seal();
}
bool LayerMatches(const DocumentSession& session, const Guid& id, const Raster& original, bool target) noexcept
{
    const auto& document = session.Document();
    const auto* layer = document ? document->layers.FindKey(id) : nullptr;
    if (!layer || !layer->asset || layer->asset->image != original || layer->asset->image.Width() != kSide ||
        layer->asset->image.Height() != kSide || layer->asset->image.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, kSide * 4> row{};
    for (int y = 0; y < kSide; ++y)
    {
        layer->asset->image.ReadRegion(0, y, kSide, 1, row.data(), kSide * 4, 0);
        for (int x = 0; x < kSide; ++x)
        {
            const auto p = static_cast<std::size_t>(x * 4);
            if (row[p] != (target ? TargetByte(x, y, 0) : OtherByte(x, y, 0)) ||
                row[p + 1] != (target ? TargetByte(x, y, 1) : OtherByte(x, y, 1)) ||
                row[p + 2] != (target ? TargetByte(x, y, 2) : OtherByte(x, y, 2)) || row[p + 3] != 255)
                return false;
        }
    }
    return true;
}
bool BothOriginalLayers(const DocumentSession& session, const Guid& targetId, const Guid& secondId,
                        const Raster& target, const Raster& second) noexcept
{
    return LayerMatches(session, targetId, target, true) && LayerMatches(session, secondId, second, false);
}
} // namespace

int Application::RunWarpLiveStaleSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool value, const char* scenario, const char* control) noexcept
    {
        std::printf("%s application-warp-live-stale.%s.%s\n", value ? "PASS" : "FAIL", scenario, control);
        std::fflush(stdout);
        ++checks;
        failures += value ? 0u : 1u;
    };
    const auto fail = [](const char* scenario, const char* control) noexcept
    {
        std::printf("FAIL application-warp-live-stale.setup.%s.%s\n", scenario, control);
        std::fflush(stdout);
        return 5;
    };
    constexpr std::array scenarios{
        Scenario{"liquify-opening-tool", Boundary::OpeningPrepared, SmearMode::Liquify, Invalidation::Tool, 0},
        Scenario{"liquify-opening-layer", Boundary::OpeningPrepared, SmearMode::Liquify, Invalidation::ActiveLayer, 0},
        Scenario{"liquify-composed-tool", Boundary::Composed, SmearMode::Liquify, Invalidation::Tool, 1},
        Scenario{"liquify-composed-layer", Boundary::Composed, SmearMode::Liquify, Invalidation::ActiveLayer, 1},
        Scenario{"liquify-final-tool", Boundary::FinalComposed, SmearMode::Liquify, Invalidation::Tool, 2},
        Scenario{"liquify-final-layer", Boundary::FinalComposed, SmearMode::Liquify, Invalidation::ActiveLayer, 2},
        Scenario{"smudge-opening-tool", Boundary::OpeningPrepared, SmearMode::Smudge, Invalidation::Tool, 0},
        Scenario{"smudge-opening-layer", Boundary::OpeningPrepared, SmearMode::Smudge, Invalidation::ActiveLayer, 0},
        Scenario{"smudge-composed-tool", Boundary::Composed, SmearMode::Smudge, Invalidation::Tool, 1},
        Scenario{"smudge-composed-layer", Boundary::Composed, SmearMode::Smudge, Invalidation::ActiveLayer, 1},
        Scenario{"smudge-final-tool", Boundary::FinalComposed, SmearMode::Smudge, Invalidation::Tool, 2},
        Scenario{"smudge-final-layer", Boundary::FinalComposed, SmearMode::Smudge, Invalidation::ActiveLayer, 2}};

    for (const auto& scenario : scenarios)
    {
        std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
        if (!owner)
            return fail(scenario.name, "application-allocation");
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        auto gate = std::make_shared<StrokeFinishTestGate>(false);
        gate->holdSample = scenario.boundary;
        gate->holdSampleNumber = scenario.preparedSamples;
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                (void)SetEvent(gate->release.get());
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
        const auto waitForBoundary = [&]() noexcept
        {
            const auto deadline = GetTickCount64() + kWaitMs;
            for (;;)
            {
                if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                    return false;
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                // Require the exact visit and event; an earlier FinalPrepared signal is never sufficient.
                if (gate->sampleBoundaryVisits.load(std::memory_order_acquire) == 1 &&
                    WaitForSingleObject(gate->reached.get(), 0) == WAIT_OBJECT_0)
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

        if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
            return fail(scenario.name, "initialize-hidden-application");
        owner->BindCompositionDevice();
        if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
            !owner->_strokeFinishSource.Start() || !owner->_brushEngine.Ready())
            return fail(scenario.name, "initialize-real-display-rows-source");

        Guid targetId{};
        Guid secondId{};
        auto document = MakeScene(targetId, secondId);
        const auto* targetLayer = document.layers.FindKey(targetId);
        const auto* secondLayer = document.layers.FindKey(secondId);
        if (!targetLayer || !targetLayer->asset || !secondLayer || !secondLayer->asset)
            return fail(scenario.name, "construct-two-literal-image-layers");
        const Raster originalTarget = targetLayer->asset->image;
        const Raster originalSecond = secondLayer->asset->image;
        owner->ReplaceDocument(std::move(document), targetId, {});
        owner->_tool = EditorTool::Smear;
        owner->_session.smearMode = scenario.mode;
        owner->_session.brushSettings = kSettings;
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        if (!settle())
            return fail(scenario.name, "settle-original-two-layer-display");

        const auto revision = owner->_session.history.CurrentRevision();
        const auto cpuBaseline = owner->_jobs.ReservedMemoryBytes();
        const auto gpuBaseline = owner->_graphGpuBudget.ReservedOwner();
        owner->_asyncWarpLiveEnabled = true;
        owner->_strokeFinishTestGate = gate;
        DxUi::PointerEvent down{};
        down.action = DxUi::PointerAction::Down;
        down.modifiers = MK_LBUTTON;
        if (!owner->QueueCanvasBrush(down, owner->_viewport.ViewFromDocument(kDown, owner->DocumentSize())))
            return fail(scenario.name, "queue-real-down");
        if (scenario.boundary != Boundary::OpeningPrepared)
        {
            DxUi::PointerEvent move{};
            move.action = DxUi::PointerAction::Move;
            move.modifiers = MK_LBUTTON;
            owner->RecordCanvasBrush(move, owner->_viewport.ViewFromDocument(kMove, owner->DocumentSize()));
            if (scenario.boundary == Boundary::FinalComposed)
            {
                DxUi::PointerEvent up{};
                up.action = DxUi::PointerAction::Up;
                owner->RecordCanvasBrush(up, owner->_viewport.ViewFromDocument(kUp, owner->DocumentSize()));
            }
        }
        if (!waitForBoundary() || gate->sampleBoundaryVisits.load() != 1 || gate->sampleBoundaryOffOwner.load() != 1 ||
            gate->samplesPrepared.load() != scenario.preparedSamples || !owner->_utilityTicket ||
            !owner->_activeHostRequest || owner->_canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
            return fail(scenario.name, "reach-exact-off-owner-worker-boundary");

        const auto ticket = owner->_utilityTicket;
        const auto ordinal = owner->_activeHostRequest->ordinal;
        const auto cpuCharge = owner->_jobs.ReservedMemoryBytes();
        const auto gpuCharge = owner->_graphGpuBudget.ReservedOwner();
        const bool originalsBefore =
            BothOriginalLayers(owner->_session, targetId, secondId, originalTarget, originalSecond) &&
            owner->_session.history.CurrentRevision() == revision && owner->_session.history.UndoCount() == 0 &&
            !gate->retired.load();
        expect(gate->sampleBoundaryVisits.load() == 1 && WaitForSingleObject(gate->reached.get(), 0) == WAIT_OBJECT_0 &&
                   gate->sampleBoundaryOffOwner.load() == 1 &&
                   gate->samplesPrepared.load() == scenario.preparedSamples && originalsBefore,
               scenario.name, "01-actual-original-worker-held-at-exact-boundary");

        if (scenario.invalidation == Invalidation::Tool)
            owner->_tool = EditorTool::Brush;
        else
            owner->_session.SelectLayer(secondId);
        const bool invalidated = scenario.invalidation == Invalidation::Tool
                                     ? owner->_tool == EditorTool::Brush && owner->_session.ActiveLayerID() == targetId
                                     : owner->_tool == EditorTool::Smear && owner->_session.ActiveLayerID() == secondId;
        expect(invalidated && owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   !gate->retired.load() &&
                   BothOriginalLayers(owner->_session, targetId, secondId, originalTarget, originalSecond) &&
                   owner->_session.history.CurrentRevision() == revision && owner->_session.history.UndoCount() == 0,
               scenario.name, "02-owner-authority-invalidated-without-cancelling-or-mutating-source");
        expect(owner->_utilityTicket == ticket && owner->_activeHostRequest &&
                   owner->_activeHostRequest->ordinal == ordinal && cpuCharge > cpuBaseline &&
                   gpuCharge >= gpuBaseline &&
                   (scenario.boundary == Boundary::OpeningPrepared || gpuCharge > gpuBaseline) &&
                   owner->_jobs.ReservedMemoryBytes() == cpuCharge &&
                   owner->_graphGpuBudget.ReservedOwner() == gpuCharge && !gate->retired.load(),
               scenario.name, "03-original-ticket-ordinal-and-full-charge-live-until-producer-ack");

        if (!SetEvent(gate->release.get()) || !settle())
            return fail(scenario.name, "release-exact-producer-and-settle-original-parent");
        const auto receipt = owner->_canvasBrushReceipt.Read();
        expect(receipt.outcome == HostOutcome::Stale && receipt.error == static_cast<std::int32_t>(E_ABORT) &&
                   BothOriginalLayers(owner->_session, targetId, secondId, originalTarget, originalSecond) &&
                   owner->_session.history.CurrentRevision() == revision && owner->_session.history.UndoCount() == 0,
               scenario.name, "04-exact-stale-e_abort-preserves-both-image-identities-pixels-and-history");
        expect(gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load() &&
                   !owner->_session.HasPendingWarp() && !owner->_acknowledgedStrokePreview &&
                   !owner->_strokeFinishCurrent && !owner->_canvasBrushInput.Busy() &&
                   !owner->_strokeFinishSource.HasPending(),
               scenario.name, "05-joined-worker-retires-original-draft-payload-input-and-preview");
        expect(!owner->_utilityTicket && !owner->_activeHostRequest &&
                   owner->_jobs.ReservedMemoryBytes() == cpuBaseline &&
                   owner->_graphGpuBudget.ReservedOwner() == gpuBaseline && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   (scenario.invalidation == Invalidation::Tool
                        ? owner->_tool == EditorTool::Brush && owner->_session.ActiveLayerID() == targetId
                        : owner->_tool == EditorTool::Smear && owner->_session.ActiveLayerID() == secondId) &&
                   BothOriginalLayers(owner->_session, targetId, secondId, originalTarget, originalSecond) &&
                   owner->_session.history.CurrentRevision() == revision && owner->_session.history.UndoCount() == 0,
               scenario.name, "06-invalidation-remains-and-all-original-charges-return-without-stale-publication");
    }

    if (checks != 72 || scenarios.size() != 12)
    {
        std::printf("FAIL application-warp-live-stale.fixed-registry expected=72/12 actual=%u/%zu\n", checks,
                    scenarios.size());
        ++failures;
    }
    std::printf("SUMMARY application-warp-live-stale checks=%u failures=%u scenarios=%zu expected=72/12\n", checks,
                failures, scenarios.size());
    return failures ? 5 : 0;
}