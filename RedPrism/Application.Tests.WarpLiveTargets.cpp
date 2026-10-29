#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PreparedLayerRows.h"
#include "Runtime.Tests.StrokeFinishTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "WarpStroke.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr int kSide = 16;
constexpr DWORD kWaitMs = 10000;
constexpr BrushSettings kSettings{6, .8, 0, 0, 0, 1};
constexpr Vec2 kDown{7.25, 7.25}, kMove{8.25, 7.25}, kUp{9.25, 7.25};
enum class Geometry : unsigned
{
    Translated,
    Scaled,
    Rotated,
    Sparse,
    Blank,
    Selected,
    Masked
};
constexpr std::array<const char*, 7> kNames{"translated", "scaled", "rotated", "sparse", "blank", "selected", "masked"};
constexpr std::array<std::array<std::uint8_t, 4>, 4> kColors{
    {{0, 0, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255}, {0, 255, 255, 255}}};

Raster LiteralSource(Geometry geometry)
{
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    if (geometry != Geometry::Blank)
        for (int y = 4; y < kSide; ++y)
            for (int x = 4; x < kSide; ++x)
            {
                const int dx = x - 4, dy = y - 4;
                if (geometry != Geometry::Scaled && (dx >= 8 || dy >= 8))
                    continue;
                const int ix = geometry == Geometry::Scaled ? dx / 2 : geometry == Geometry::Rotated ? 7 - dx : dx;
                const int iy = geometry == Geometry::Scaled ? dy / 2 : geometry == Geometry::Rotated ? 7 - dy : dy;
                auto* pixel = pixels.data() + (y * kSide + x) * 4;
                std::copy(kColors[static_cast<std::size_t>((iy / 4) * 2 + ix / 4)].begin(),
                          kColors[static_cast<std::size_t>((iy / 4) * 2 + ix / 4)].end(), pixel);
                if (geometry == Geometry::Sparse && ix == 2 && iy == 3)
                    std::fill_n(pixel, 4, std::uint8_t{255});
            }
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}
Raster MaskDisplay(const Raster& source, Geometry geometry)
{
    if (geometry != Geometry::Masked)
        return source;
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    source.ReadRegion(0, 0, kSide, kSide, pixels.data(), kSide * 4, 0);
    for (auto& value : pixels)
        value = static_cast<std::uint8_t>((static_cast<unsigned>(value) * 128 + 127) / 255);
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}
CanvasDocument Scene(Geometry geometry, Guid& active)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = fixture.height = kSide;
    auto layer = ImageLayer::Blank(L"Placed live Warp target", {8, 8});
    layer.transform.origin = {4, 4};
    layer.transform.size = geometry == Geometry::Scaled ? Vec2{16, 16} : Vec2{8, 8};
    layer.transform.rotation = geometry == Geometry::Rotated ? 180 : 0;
    layer.transform.sampling = LayerSampling::Nearest;
    if (geometry != Geometry::Blank)
    {
        std::array<std::uint8_t, 8 * 8 * 4> pixels{};
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                std::copy(kColors[static_cast<std::size_t>((y / 4) * 2 + x / 4)].begin(),
                          kColors[static_cast<std::size_t>((y / 4) * 2 + x / 4)].end(),
                          pixels.data() + (y * 8 + x) * 4);
        auto image = Raster::Make(8, 8, RasterFormat::Bgra8888, pixels.data(), pixels.size());
        if (geometry == Geometry::Sparse)
        {
            constexpr std::array<std::uint8_t, 4> white{255, 255, 255, 255};
            std::vector<RasterTile> tiles{
                {2, 3, Raster::Make(1, 1, RasterFormat::Bgra8888, white.data(), white.size())}};
            image = Raster::MakeSparse(8, 8, RasterFormat::Bgra8888, image, 0, 0, std::move(tiles), 0);
        }
        layer.asset = RasterAsset{std::move(image), {}, layer.name};
    }
    if (geometry == Geometry::Selected)
        fixture.selection = DocumentSelection{false, {{{4, 4}, {8, 4}, {8, 12}, {4, 12}}}};
    if (geometry == Geometry::Masked)
    {
        constexpr std::uint8_t value = 128;
        const auto mask = Raster::Make(1, 1, RasterFormat::Gray8, &value, 1);
        layer.mask = LayerMask{RasterAsset{mask, mask, L"Uniform 128 mask"}, true, {}, true};
    }
    active = layer.id;
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}
std::optional<BrushStroke::Commit> Reference(const ImageLayer& original, Geometry geometry, SmearMode mode)
{
    // Literal nearest document placement followed by the original contiguous Warp and scalar brush kernels.
    // No candidate placed reader, live sample facade, recorder or final publication is used.
    WarpStroke warp(LiteralSource(geometry), mode, kSettings);
    warp.Append(kDown);
    warp.Append(kMove);
    warp.Append(kUp);
    auto settings = kSettings;
    settings.diameter = 10;
    settings.hardness = settings.opacity = 1;
    BrushStroke brush(original, false, settings, kSide, kSide, nullptr);
    if (geometry == Geometry::Selected)
    {
        std::array<std::uint8_t, kSide * kSide> coverage{};
        for (int y = 4; y < 12; ++y)
            for (int x = 4; x < 8; ++x)
                coverage[static_cast<std::size_t>(y * kSide + x)] = 255;
        brush.SetSelectionClip(Raster::Make(kSide, kSide, RasterFormat::Gray8, coverage.data(), coverage.size()));
    }
    brush.SetSample(warp.Image(), 0, 0, true);
    for (const auto point : warp.Points())
        if (!brush.Append(point))
            return {};
    return brush.Flush() ? brush.PaintSnapshot() : std::nullopt;
}
bool SamePixels(const Raster& actual, const Raster& expected, int tolerance) noexcept
{
    if (!actual.HasStorage() || !expected.HasStorage() || actual.Format() != RasterFormat::Bgra8888 ||
        expected.Format() != RasterFormat::Bgra8888 || actual.Width() != expected.Width() ||
        actual.Height() != expected.Height() || actual.Width() > 32 || actual.Height() > 32)
        return false;
    std::array<std::uint8_t, 32 * 4> a{}, b{};
    const int stride = actual.Width() * 4;
    for (int y = 0; y < actual.Height(); ++y)
    {
        actual.ReadRegion(0, y, actual.Width(), 1, a.data(), stride, 0);
        expected.ReadRegion(0, y, expected.Width(), 1, b.data(), stride, 0);
        for (int x = 0; x < stride; ++x)
            if (std::abs(static_cast<int>(a[static_cast<std::size_t>(x)]) - b[static_cast<std::size_t>(x)]) > tolerance)
                return false;
    }
    return true;
}
Raster LiteralFinalDisplay(const BrushStroke::Commit& reference, Geometry geometry)
{
    // Independent axis-aligned/half-turn nearest placement: do not call LayerTransform::Inverted,
    // RasterView, SamplePlaced, Compose or the candidate preview reader for the display expectation.
    const auto& t = reference.transform;
    if ((t.rotation != 0 && t.rotation != 180) || t.flipX || t.flipY || t.sampling != LayerSampling::Nearest)
        return {};
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
        {
            double u = (x + .5 - t.origin.x) / t.size.x;
            double v = (y + .5 - t.origin.y) / t.size.y;
            if (t.rotation == 180)
            {
                u = 1 - u;
                v = 1 - v;
            }
            if (u < 0 || u >= 1 || v < 0 || v >= 1)
                continue;
            auto* pixel = pixels.data() + (y * kSide + x) * 4;
            reference.image.ReadRegion(static_cast<int>(std::floor(u * reference.image.Width())),
                                       static_cast<int>(std::floor(v * reference.image.Height())), 1, 1, pixel, 4, 0);
        }
    return MaskDisplay(Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size()), geometry);
}
bool SameDisplay(RecordedDisplayGraph* graph, const Raster& reference, int tolerance) noexcept
{
    auto* composition = graph ? graph->CompositionForTestOwner() : nullptr;
    std::vector<std::uint8_t> pixels;
    unsigned width = 0, height = 0;
    if (!reference.HasStorage() || !composition || FAILED(composition->Readback(pixels, width, height)) ||
        width != kSide || height != kSide || pixels.size() != kSide * kSide * 4)
        return false;
    const auto actual = Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    return SamePixels(actual, reference, tolerance);
}
} // namespace

int Application::RunWarpLiveTargetsSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned checks = 0, failures = 0;
    const auto fail = [](const char* name) noexcept
    {
        std::printf("FAIL application-warp-live-targets.setup.%s\n", name);
        std::fflush(stdout);
        return 5;
    };
    const auto expect = [&](bool value, const char* geometry, const char* mode, const char* name) noexcept
    {
        std::printf("%s application-warp-live-targets.%s.%s.%s\n", value ? "PASS" : "FAIL", geometry, mode, name);
        std::fflush(stdout);
        ++checks;
        failures += value ? 0u : 1u;
    };
    std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
    if (!owner)
        return fail("owner");
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
    const auto pumpUntil = [&](auto predicate) noexcept
    {
        const auto deadline = GetTickCount64() + kWaitMs;
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
            const auto waited = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                            MWMO_INPUTAVAILABLE);
            if (waited != WAIT_OBJECT_0 && waited != WAIT_OBJECT_0 + 1)
                return false;
        }
    };
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return fail("initialize");
    owner->BindCompositionDevice();
    if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
        !owner->_strokeFinishSource.Start() || !owner->_brushEngine.Ready())
        return fail("sources");
    owner->_asyncWarpLiveEnabled = true;
    for (unsigned index = 0; index < kNames.size(); ++index)
        for (const auto mode : {SmearMode::Liquify, SmearMode::Smudge})
        {
            const auto geometry = static_cast<Geometry>(index);
            const char* name = kNames[index];
            const char* modeName = mode == SmearMode::Liquify ? "liquify" : "smudge";
            Guid active{};
            auto document = Scene(geometry, active);
            const auto original = document.layers.front();
            const auto originalSelection = document.selection;
            const auto reference = Reference(original, geometry, mode);
            if (!reference)
                return fail("independent-scalar-reference");
            const auto initial = MaskDisplay(LiteralSource(geometry), geometry);
            const auto final = LiteralFinalDisplay(*reference, geometry);
            if (!initial.HasStorage() || !final.HasStorage())
                return fail("literal-display-reference");
            owner->ReplaceDocument(std::move(document), active, {});
            owner->_tool = EditorTool::Smear;
            owner->_session.smearMode = mode;
            owner->_session.brushSettings = kSettings;
            owner->_session.history.Reset();
            owner->_session.history.MarkSaved();
            owner->_viewport.zoom = 1;
            owner->_viewport.pan = {};
            owner->_viewport.followsFit = false;
            owner->DocumentChanged();
            if (!settle())
                return fail("scene-settle");
            auto gate = std::make_shared<StrokeFinishTestGate>(false);
            owner->_strokeFinishTestGate = gate;
            const auto revision = owner->_session.history.CurrentRevision();
            expect(SameDisplay(owner->_acknowledgedCommittedDisplay, initial, 0) &&
                       (geometry != Geometry::Sparse ||
                        (original.asset->image.IsSparse() && !original.asset->image.IsMaterialized())),
                   name, modeName, "literal-initial-placement-and-semantic-mask-exact0");
            DxUi::PointerEvent down{};
            down.action = DxUi::PointerAction::Down;
            down.modifiers = MK_LBUTTON;
            if (!owner->QueueCanvasBrush(down, owner->_viewport.ViewFromDocument(kDown, owner->DocumentSize())) ||
                !pumpUntil([&]() noexcept { return owner->_session.PendingWarp() != nullptr; }))
                return fail("real-down");
            const auto ticket = owner->_utilityTicket;
            const auto ordinal = owner->_activeHostRequest ? owner->_activeHostRequest->ordinal : 0;
            expect(ticket && ordinal && owner->_session.history.CurrentRevision() == revision &&
                       owner->_session.history.UndoCount() == 0 &&
                       SameDisplay(owner->_acknowledgedCommittedDisplay, initial, 0),
                   name, modeName, "down-primes-placed-source-without-publication-or-dab");
            DxUi::PointerEvent move{};
            move.action = DxUi::PointerAction::Move;
            move.modifiers = MK_LBUTTON;
            owner->RecordCanvasBrush(move, owner->_viewport.ViewFromDocument(kMove, owner->DocumentSize()));
            if (!pumpUntil([&]() noexcept { return gate->samplesAcknowledged.load(std::memory_order_acquire) == 1; }))
                return fail("real-move");
            WarpStroke preview(LiteralSource(geometry), mode, kSettings);
            preview.Append(kDown);
            preview.Append(kMove);
            const auto expectedPreview = MaskDisplay(preview.Image(), geometry);
            expect(owner->_utilityTicket == ticket && owner->_activeHostRequest &&
                       owner->_activeHostRequest->ordinal == ordinal &&
                       SameDisplay(owner->_acknowledgedStrokePreview, expectedPreview,
                                   geometry == Geometry::Masked ? 1 : 0) &&
                       owner->_session.history.UndoCount() == 0,
                   name, modeName, "real-preview-matches-independent-placed-warp-and-mask");
            DxUi::PointerEvent up{};
            up.action = DxUi::PointerAction::Up;
            owner->RecordCanvasBrush(up, owner->_viewport.ViewFromDocument(kUp, owner->DocumentSize()));
            if (!settle())
                return fail("real-up");
            const auto* layer = owner->_session.ActiveLayer();
            expect(layer && layer->asset && SamePixels(layer->asset->image, reference->image, 2) &&
                       layer->transform == reference->transform && layer->mask == original.mask &&
                       owner->_session.Selection() == originalSelection && owner->_session.history.UndoCount() == 1 &&
                       SameDisplay(owner->_acknowledgedCommittedDisplay, final, 2),
                   name, modeName, "final-cpu-and-gpu-match-scalar-selection-and-mask-reference");
            expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                       owner->_canvasBrushReceipt.Read().error == S_OK && !owner->_utilityTicket &&
                       !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() &&
                       !owner->_session.HasPendingWarp() && owner->_preparedLayerRows &&
                       owner->_preparedLayerRows->FindVisibleRow(active) &&
                       gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load(),
                   name, modeName, "exact-receipt-rows-and-original-worker-retirement");
            owner->_strokeFinishTestGate.reset();
            owner->OnCommand(ChromeHost::kCommandUndo);
            const bool undone = settle();
            layer = owner->_session.ActiveLayer();
            expect(undone && layer && layer->asset.has_value() == original.asset.has_value() &&
                       (!original.asset || layer->asset->image == original.asset->image) &&
                       layer->transform == original.transform && layer->mask == original.mask &&
                       owner->_session.Selection() == originalSelection &&
                       owner->_session.history.CurrentRevision() == revision &&
                       owner->_session.history.UndoCount() == 0 &&
                       SameDisplay(owner->_acknowledgedCommittedDisplay, initial, 0),
                   name, modeName, "undo-restores-exact-source-placement-selection-and-mask");
        }
    if (checks != 84)
    {
        std::printf("FAIL application-warp-live-targets.fixed-count expected=84 actual=%u\n", checks);
        ++failures;
    }
    std::printf("SUMMARY application-warp-live-targets checks=%u failures=%u scenarios=14 expected=84/14\n", checks,
                failures);
    return failures ? 5 : 0;
}
