#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PreparedLayerRows.h"
#include "StrokeFinishTestGate.h"

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
constexpr int kSide = 8;
constexpr DWORD kWaitMs = 10000;
using Pixel = std::array<std::uint8_t, 4>;
constexpr Pixel kClear{0, 0, 0, 0};
constexpr Pixel kRed{0, 0, 255, 255};
constexpr Pixel kBlue{255, 0, 0, 255};
constexpr Pixel kWhite{255, 255, 255, 255};
struct Mode final
{
    const char* name;
    EditorTool tool;
    BrushToolMode brush;
    Pixel source;
    Pixel expected;
    bool mask = false;
    bool stretched = false;
    bool allLayers = false;
    bool changed = true;
};
CanvasDocument MakeScene(const Mode& mode, Guid& target)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;
    const auto image = [](Pixel color)
    {
        std::array<std::uint8_t, kSide * kSide * 4> pixels{};
        for (std::size_t i = 0; i < pixels.size(); i += 4)
            std::copy(color.begin(), color.end(), pixels.begin() + i);
        return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    };
    auto layer = ImageLayer::Blank(L"Sample target", {kSide, kSide});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset = RasterAsset{image(mode.source), {}, layer.name};
    target = layer.id;
    if (mode.mask)
    {
        Raster mask;
        if (mode.stretched)
        {
            constexpr std::array<std::uint8_t, 4> values{0, 64, 128, 255};
            mask = Raster::Make(2, 2, RasterFormat::Gray8, values.data(), values.size());
        }
        else
        {
            std::array<std::uint8_t, kSide * kSide> values{};
            values.fill(128);
            mask = Raster::Make(kSide, kSide, RasterFormat::Gray8, values.data(), values.size());
        }
        LayerMask targetMask;
        targetMask.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"Literal mask"};
        layer.mask = std::move(targetMask);
    }
    fixture.layers.push_back(std::move(layer));
    if (mode.allLayers)
    {
        auto overlay = ImageLayer::Blank(L"Literal blue clone source", {kSide, kSide});
        overlay.transform.sampling = LayerSampling::Nearest;
        overlay.asset = RasterAsset{image(kBlue), {}, overlay.name};
        fixture.layers.push_back(std::move(overlay));
    }
    return std::move(fixture).Seal();
}
bool DisplayPixel(RecordedDisplayGraph* graph, int x, int y, Pixel& pixel) noexcept
{
    auto* composition = graph ? graph->CompositionForTestOwner() : nullptr;
    if (!composition)
        return false;
    std::vector<std::uint8_t> bytes;
    UINT width = 0, height = 0;
    if (FAILED(composition->Readback(bytes, width, height)) || width != kSide || height != kSide ||
        bytes.size() != kSide * kSide * 4)
        return false;
    std::copy_n(bytes.data() + (y * kSide + x) * 4, 4, pixel.data());
    return true;
}
bool SourcePixel(const DocumentSession& session, int x, int y, Pixel& pixel) noexcept
{
    const auto* layer = session.ActiveLayer();
    if (!layer || !layer->asset)
        return false;
    const auto& image = layer->asset->image;
    image.ReadRegion(x, y, 1, 1, pixel.data(), 4, 0);
    return image.HasStorage();
}
} // namespace

int Application::RunStandaloneStrokeSampleSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool value, const char* scenario, const char* control) noexcept
    {
        std::printf("%s application-stroke-sample.%s.%s\n", value ? "PASS" : "FAIL", scenario, control);
        std::fflush(stdout);
        ++checks;
        failures += value ? 0u : 1u;
    };
    const auto fail = [](const char* name) noexcept
    {
        std::printf("FAIL application-stroke-sample.setup.%s\n", name);
        std::fflush(stdout);
        return 5;
    };
    std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
    if (!owner)
        return fail("allocation");
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
                (void)PumpMessages(); // Consume WM_QUIT from this fixture's own hidden window.
            }
        });
    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
    const auto waitForSamples = [&](const std::shared_ptr<StrokeFinishTestGate>& gate, unsigned count) noexcept
    {
        const auto deadline = GetTickCount64() + kWaitMs;
        for (;;)
        {
            if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                return false;
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (gate->samplesAcknowledged.load(std::memory_order_acquire) >= count)
                return true;
            if (owner->_canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
                return false;
            if (owner->_jobs.HasUiWork() || owner->_ownerWork.HasReady())
                continue;
            const auto now = GetTickCount64();
            if (now >= deadline)
                return false;
            const HANDLE event = static_cast<HANDLE>(owner->_jobs.WakeHandle());
            const auto result = MsgWaitForMultipleObjectsEx(1, &event, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                            MWMO_INPUTAVAILABLE);
            if (result != WAIT_OBJECT_0 && result != WAIT_OBJECT_0 + 1)
                return false;
        }
    };
    const auto point = [&](DxUi::PointerAction action, Vec2 pixel) noexcept
    {
        DxUi::PointerEvent event{};
        event.action = action;
        event.modifiers = action == DxUi::PointerAction::Up ? 0u : MK_LBUTTON;
        const auto view = owner->_viewport.ViewFromDocument(pixel, owner->DocumentSize());
        if (action == DxUi::PointerAction::Down)
            return owner->QueueCanvasBrush(event, view);
        owner->RecordCanvasBrush(event, view);
        return true;
    };
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return fail("actual-hidden-warp-initialize");
    owner->BindCompositionDevice();
    constexpr std::array modes{
        Mode{"paint", EditorTool::Brush, BrushToolMode::Paint, kClear, kRed},
        Mode{"eraser", EditorTool::Brush, BrushToolMode::Erase, kRed, kClear},
        Mode{"mask", EditorTool::Brush, BrushToolMode::Paint, kWhite, kClear, true},
        Mode{"stretched-mask", EditorTool::Brush, BrushToolMode::Paint, kWhite, kClear, true, true},
        Mode{"blur-constant", EditorTool::Smear, BrushToolMode::Paint, kRed, kRed},
        Mode{"heal-constant", EditorTool::Heal, BrushToolMode::Paint, kRed, kRed},
        Mode{"clone-active", EditorTool::Clone, BrushToolMode::Paint, kRed, kRed},
        Mode{"clone-all", EditorTool::Clone, BrushToolMode::Paint, kRed, kBlue, false, false, true, true}};
    Guid initial{};
    auto firstScene = MakeScene(modes.front(), initial);
    owner->ReplaceDocument(std::move(firstScene), initial, {});
    owner->DocumentChanged();
    if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
        !owner->_strokeFinishSource.Start() || !settle())
        return fail("owner-sources-and-committed-display");
    owner->_asyncStrokeFinishEnabled = true;
    owner->_asyncStrokeSamplesEnabled = true;
    for (const auto& mode : modes)
    {
        Guid target{};
        auto scene = MakeScene(mode, target);
        owner->ReplaceDocument(std::move(scene), target, {});
        owner->_tool = mode.tool;
        owner->_session.brushMode = mode.brush;
        owner->_session.smearMode = SmearMode::Blur;
        owner->_session.brushSettings = BrushSettings{4, 1, 1, 0, 0, 1};
        owner->_session.maskPaintWhite = false;
        owner->_session.cloneSettings = CloneSettings{false, mode.allLayers};
        owner->_session.SetCloneSource({3, 3});
        owner->_session.SetForegroundColor(0xFFFF0000u);
        if (mode.mask)
            owner->_session.SelectLayerTarget(target, true);
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        owner->SyncChrome();
        if (!settle())
            return fail("scene-settle");
        auto gate = std::make_shared<StrokeFinishTestGate>(false);
        owner->_strokeFinishTestGate = gate;
        if (!point(DxUi::PointerAction::Down, {3, 3}) || !waitForSamples(gate, 1) || !owner->_activeHostRequest ||
            !owner->_utilityTicket)
            return fail("first-original-sample");
        const auto ordinal = owner->_activeHostRequest->ordinal;
        const auto original = owner->_utilityTicket.value();
        Pixel first{};
        // Legacy BrushStroke.swift: 12% gray at 45% over opaque red -> rounded BGRA 14,14,154,255.
        const Pixel previewExpected = mode.tool == EditorTool::Heal ? Pixel{14, 14, 154, 255}
                                      : mode.allLayers              ? kBlue
                                                                    : mode.expected;
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   owner->_session.history.UndoCount() == 0 && owner->_session.PendingBrush() &&
                   DisplayPixel(owner->_acknowledgedStrokePreview, 3, 3, first) && first == previewExpected,
               mode.name, "down-publishes-literal-preview-before-history-or-up");
        if (mode.stretched)
        {
            Pixel topRight{}, bottomLeft{}, bottomRight{};
            expect(DisplayPixel(owner->_acknowledgedStrokePreview, 7, 0, topRight) &&
                       DisplayPixel(owner->_acknowledgedStrokePreview, 0, 7, bottomLeft) &&
                       DisplayPixel(owner->_acknowledgedStrokePreview, 7, 7, bottomRight) &&
                       topRight == Pixel{64, 64, 64, 64} && bottomLeft == Pixel{128, 128, 128, 128} &&
                       bottomRight == kWhite,
                   mode.name, "untouched-quadrants-use-independent-nearest-stretched-mask-values");
        }
        point(DxUi::PointerAction::Move, {4, 3});
        if (!waitForSamples(gate, 2))
            return fail("second-original-sample");
        expect(owner->_utilityTicket == original && owner->_activeHostRequest &&
                   owner->_activeHostRequest->ordinal == ordinal && gate->previewsPublished.load() == 2 &&
                   gate->previewsReplaced.load() == 1 && owner->_session.history.UndoCount() == 0,
               mode.name, "move-reuses-original-ticket-and-replaces-acknowledged-private-preview");
        const auto* const acknowledged = owner->_acknowledgedStrokePreview;
        point(DxUi::PointerAction::Move, {4, 3});
        if (!waitForSamples(gate, 3))
            return fail("duplicate-original-sample");
        expect(owner->_acknowledgedStrokePreview == acknowledged && gate->previewsPublished.load() == 2 &&
                   owner->_utilityTicket == original && gate->samplesPrepared.load() == 3 &&
                   gate->samplesOffOwner.load() == 3,
               mode.name, "duplicate-acknowledges-off-owner-without-replacing-preview");
        point(DxUi::PointerAction::Up, {4, 3});
        if (!settle())
            return fail("original-up-final-publication");
        Pixel final{};
        const auto* const layer = owner->_session.ActiveLayer();
        bool finalPixel = false;
        if (mode.mask && layer && layer->mask)
        {
            std::uint8_t value = 255;
            layer->mask->asset.image.ReadRegion(3, 3, 1, 1, &value, 1, 255);
            finalPixel = value == 0;
        }
        else
            finalPixel = SourcePixel(owner->_session, 3, 3, final) && final == mode.expected;
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded && finalPixel &&
                   owner->_session.history.UndoCount() == (mode.changed ? 1 : 0),
               mode.name, "up-publishes-independent-final-pixel-with-exact-history-count");
        expect(!owner->_acknowledgedStrokePreview && owner->_acknowledgedCommittedDisplay &&
                   owner->_preparedLayerRows && owner->_preparedLayerRows->FindVisibleRow(target) &&
                   !owner->_utilityTicket && !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy(),
               mode.name, "terminal-publication-removes-live-preview-and-original-input-owner");
        const auto sampleAcknowledgements = gate->samplesAcknowledged.load();
        const auto prepares = gate->prepares.load();
        const bool retired = gate->retired.load(std::memory_order_acquire);
        const bool retiredOffOwner = gate->retiredOffOwner.load();
        if (sampleAcknowledgements != 4 || prepares != 1 || !retired || !retiredOffOwner)
            std::fprintf(stderr,
                         "stroke-sample retirement diagnostic: mode=%s acknowledged=%u prepares=%u retired=%d "
                         "off-owner=%d scheduler-idle=%d\n",
                         mode.name, sampleAcknowledgements, prepares, retired, retiredOffOwner, owner->_jobs.IsIdle());
        expect(sampleAcknowledgements == 4 && prepares == 1 && retired && retiredOffOwner, mode.name,
               "all-four-samples-and-final-coverage-join-before-off-owner-retirement");
        owner->_strokeFinishTestGate.reset();
    }
    std::printf("%s application-stroke-sample.summary checks=%u failed=%u scenarios=%zu\n", failures ? "FAIL" : "PASS",
                checks, failures, modes.size());
    return failures ? 5 : 0;
}
