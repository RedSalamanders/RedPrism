#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "BrushStroke.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "Runtime.Tests.StrokeFinishTestGate.h"
#include "Selection.h"

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
constexpr int kWidth = 320;
constexpr int kHeight = 72;
constexpr DWORD kWaitMs = 10000;
struct ParityCase final
{
    const char* name;
    BrushKind kind;
    bool mask = false;
    bool selection = false;
    bool translated = false;
    bool allLayers = false;
    bool hard = false;
};
Raster Pattern(int width, int height, bool mask, bool opaque = false)
{
    const int channels = mask ? 1 : 4;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * channels);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            auto* p = bytes.data() + (static_cast<std::size_t>(y) * width + x) * channels;
            if (mask)
                p[0] = static_cast<std::uint8_t>((13 * x + 17 * y) & 255);
            else
            {
                const auto a = opaque ? 255 : 96 + ((11 * x + 7 * y) % 160);
                p[0] = static_cast<std::uint8_t>((3 * x + 5 * y) % (a + 1));
                p[1] = static_cast<std::uint8_t>((7 * x + 2 * y) % (a + 1));
                p[2] = static_cast<std::uint8_t>((2 * x + 9 * y) % (a + 1));
                p[3] = static_cast<std::uint8_t>(a);
            }
        }
    return Raster::Make(width, height, mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888, bytes.data(), bytes.size());
}
CanvasDocument Scene(const ParityCase& test, Guid& target, Raster& composite)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kWidth;
    fixture.height = kHeight;
    const int width = test.translated ? 64 : kWidth;
    const int height = test.translated ? 32 : kHeight;
    auto layer = ImageLayer::Blank(L"Patterned scalar-reference target",
                                   {static_cast<double>(width), static_cast<double>(height)});
    layer.transform.sampling = LayerSampling::Nearest;
    if (test.translated)
        layer.transform.origin = {120, 16};
    layer.asset = RasterAsset{Pattern(width, height, false), {}, layer.name};
    target = layer.id;
    if (test.mask || test.translated)
    {
        const auto mask = Pattern(width, height, true);
        layer.mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"Patterned semantic mask"}, true,
                               std::nullopt, false};
    }
    fixture.layers.push_back(std::move(layer));
    if (test.selection)
        fixture.selection = DocumentSelection{true, SelectionGeometry::Rect(100.5, 12.5, 185, 49)};
    if (test.allLayers)
    {
        // A fully opaque top layer makes this exact analytic composite independent of the GPU composition.
        composite = Pattern(kWidth, kHeight, false, true);
        auto overlay = ImageLayer::Blank(L"Opaque patterned Clone composite", {kWidth, kHeight});
        overlay.asset = RasterAsset{composite, {}, overlay.name};
        overlay.transform.sampling = LayerSampling::Nearest;
        fixture.layers.push_back(std::move(overlay));
    }
    return std::move(fixture).Seal();
}
bool CloseRaster(const Raster& actual, const Raster& reference)
{
    if (!actual.HasStorage() || !reference.HasStorage() || actual.Width() != reference.Width() ||
        actual.Height() != reference.Height() || actual.Format() != reference.Format())
        return false;
    const int rowBytes = actual.Width() * actual.Channels();
    std::vector<std::uint8_t> a(static_cast<std::size_t>(rowBytes)), b(static_cast<std::size_t>(rowBytes));
    for (int y = 0; y < actual.Height(); ++y)
    {
        actual.ReadRegion(0, y, actual.Width(), 1, a.data(), rowBytes, 0);
        reference.ReadRegion(0, y, reference.Width(), 1, b.data(), rowBytes, 0);
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) > 2)
                return false; // Existing Rendering_Brush CPU/compute tolerance; never derived from actual output.
    }
    return true;
}
bool CloseDraft(const BrushStroke* actual, const BrushStroke* reference)
{
    if (!actual || !reference || actual->PaintTransform() != reference->PaintTransform() ||
        actual->Tiles().size() != reference->Tiles().size())
        return false;
    for (std::size_t i = 0; i < actual->Tiles().size(); ++i)
    {
        const auto& a = actual->Tiles()[i];
        const auto& b = reference->Tiles()[i];
        if (a.key != b.key || a.rect != b.rect || a.coverage.size() != b.coverage.size() ||
            !CloseRaster(a.image, b.image))
            return false;
        for (std::size_t p = 0; p < a.coverage.size(); ++p)
            if (std::abs(static_cast<int>(a.coverage[p]) - static_cast<int>(b.coverage[p])) > 2)
                return false;
    }
    return true;
}
bool CloseLayer(const ImageLayer* actual, const ImageLayer* reference)
{
    return actual && reference && actual->asset && reference->asset && actual->transform == reference->transform &&
           CloseRaster(actual->asset->image, reference->asset->image) &&
           actual->mask.has_value() == reference->mask.has_value() &&
           (!actual->mask || (actual->mask->placement == reference->mask->placement &&
                              CloseRaster(actual->mask->asset.image, reference->mask->asset.image) &&
                              CloseRaster(actual->mask->asset.thumbnail, reference->mask->asset.thumbnail)));
}
} // namespace

int Application::RunStandaloneStrokeSampleParitySelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    constexpr std::array cases{ParityCase{"soft-alpha", BrushKind::Paint},
                               ParityCase{"erase-alpha", BrushKind::Erase},
                               ParityCase{"soft-selection", BrushKind::Paint, false, true},
                               ParityCase{"mask-pattern", BrushKind::Paint, true},
                               ParityCase{"blur-pattern", BrushKind::Blur},
                               ParityCase{"mask-blur-pattern", BrushKind::Blur, true},
                               ParityCase{"clone-active-pattern", BrushKind::Clone},
                               ParityCase{"clone-all-pattern", BrushKind::Clone, false, false, false, true},
                               ParityCase{"translated-grow-carried-mask", BrushKind::Paint, false, false, true},
                               ParityCase{"heal-pattern", BrushKind::Heal},
                               ParityCase{"hard-alpha-paint", BrushKind::Paint, false, false, false, false, true}};
    constexpr std::array points{Vec2{110, 24}, Vec2{245, 36}, Vec2{180, 54}, Vec2{270, 28}, Vec2{270, 28}};
    unsigned checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* scenario, const char* control, std::size_t point = 0) noexcept
    {
        std::printf("%s application-stroke-sample-parity.%s.%s.%zu\n", value ? "PASS" : "FAIL", scenario, control,
                    point);
        std::fflush(stdout);
        ++checks;
        failures += value ? 0u : 1u;
    };
    const auto fail = [](const char* scenario, const char* control) noexcept
    {
        std::printf("FAIL application-stroke-sample-parity.setup.%s.%s\n", scenario, control);
        std::fflush(stdout);
        return 5;
    };
    for (const auto& test : cases)
    {
        std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
        if (!owner)
            return fail(test.name, "allocation");
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                owner->CancelHostContinuations();
                if (SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
                    std::_Exit(5);
                owner->Shutdown();
                (void)PumpMessages();
            });
        const auto settle = [&]() noexcept
        { return SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
        if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
            return fail(test.name, "hidden-warp-owner");
        owner->BindCompositionDevice();
        if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
            !owner->_strokeFinishSource.Start())
            return fail(test.name, "recording-and-sources");
        Guid target{};
        Raster composite;
        auto scene = Scene(test, target, composite);
        std::unique_ptr<DocumentSession> referenceOwner{new (std::nothrow) DocumentSession()};
        if (!referenceOwner)
            return fail(test.name, "allocation");
        DocumentSession& reference = *referenceOwner;
        reference.ReplaceDocument(scene, target);
        reference.SetBrushEngine(&CpuBrushCoverage::Shared());
        owner->ReplaceDocument(std::move(scene), target, {});
        const BrushSettings settings{
            test.kind == BrushKind::Heal ? 6.0 : 18.0, test.hard || test.kind == BrushKind::Heal ? 1.0 : 0.28, 1, 0, 0,
            test.kind == BrushKind::Heal ? 1.0 : 0.37};
        reference.brushSettings = settings;
        owner->_session.brushSettings = settings;
        reference.SetForegroundColor(0xFFFF0000u);
        owner->_session.SetForegroundColor(0xFFFF0000u);
        owner->_tool = test.kind == BrushKind::Erase   ? EditorTool::Eraser
                       : test.kind == BrushKind::Clone ? EditorTool::Clone
                       : test.kind == BrushKind::Blur  ? EditorTool::Smear
                       : test.kind == BrushKind::Heal  ? EditorTool::Heal
                                                       : EditorTool::Brush;
        owner->_session.smearMode = SmearMode::Blur;
        if (test.mask)
        {
            owner->_session.SelectLayerTarget(target, true);
            reference.SelectLayerTarget(target, true);
        }
        if (test.kind == BrushKind::Clone)
        {
            owner->_session.cloneSettings.sampleAllLayers = test.allLayers;
            reference.cloneSettings.sampleAllLayers = test.allLayers;
            owner->_session.SetCloneSource({130, 24});
            reference.SetCloneSource({130, 24});
        }
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        if (!settle())
            return fail(test.name, "initial-display");
        owner->_asyncStrokeFinishEnabled = true;
        owner->_asyncStrokeSamplesEnabled = true;
        auto gate = std::make_shared<StrokeFinishTestGate>(false);
        owner->_strokeFinishTestGate = gate;
        const auto waitSample = [&](unsigned count) noexcept
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
                const auto waited = MsgWaitForMultipleObjectsEx(1, &event, static_cast<DWORD>(deadline - now),
                                                                QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (waited != WAIT_OBJECT_0 && waited != WAIT_OBJECT_0 + 1)
                    return false;
            }
        };
        for (std::size_t p = 0; p < points.size(); ++p)
        {
            if (p == 0)
            {
                if (!reference.BeginBrush(points[p], test.kind, test.allLayers ? &composite : nullptr))
                    return fail(test.name, "scalar-reference-opening");
            }
            else
                reference.ContinueBrush(points[p]);
            DxUi::PointerEvent event{};
            event.action = p == 0 ? DxUi::PointerAction::Down : DxUi::PointerAction::Move;
            event.modifiers = MK_LBUTTON;
            const auto view = owner->_viewport.ViewFromDocument(points[p], owner->DocumentSize());
            if (p == 0)
            {
                if (!owner->QueueCanvasBrush(event, view))
                    return fail(test.name, "actual-opening");
            }
            else
                owner->RecordCanvasBrush(event, view);
            if (!waitSample(static_cast<unsigned>(p + 1)))
                return fail(test.name, "actual-sample-acknowledgement");
            expect(CloseDraft(owner->_session.PendingBrush(), reference.PendingBrush()) &&
                       owner->_session.history.UndoCount() == 0 && owner->_acknowledgedStrokePreview &&
                       gate->samplesOffOwner.load() == p + 1,
                   test.name, "complete-live-tiles-and-tail-match-scalar-reference", p);
        }
        DxUi::PointerEvent up{};
        up.action = DxUi::PointerAction::Up;
        owner->RecordCanvasBrush(up, owner->_viewport.ViewFromDocument(points.back(), owner->DocumentSize()));
        reference.ContinueBrush(points.back());
        reference.FinishBrush();
        if (!settle())
            return fail(test.name, "actual-final-publication");
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                   CloseLayer(owner->_session.ActiveLayer(), reference.ActiveLayer()) &&
                   owner->_session.history.UndoCount() == reference.history.UndoCount() &&
                   reference.history.UndoCount() == 1,
               test.name, "complete-final-pixels-placement-mask-thumbnail-and-history-match-reference");
        expect(gate->samplesAcknowledged.load() == points.size() + 1 && gate->prepares.load() == 1 &&
                   gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load() &&
                   !owner->_canvasBrushInput.Busy() && !owner->_acknowledgedStrokePreview && !owner->_utilityTicket,
               test.name, "original-final-tail-and-off-owner-retirement-acknowledged");
    }
    std::printf("%s application-stroke-sample-parity.summary checks=%u failed=%u scenarios=%zu\n",
                failures ? "FAIL" : "PASS", checks, failures, cases.size());
    return failures ? 5 : 0;
}
