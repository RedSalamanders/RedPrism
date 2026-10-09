// Actual hidden-owner qualification for accepted-prefix, bounded FIFO overflow and late input.
#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PreparedLayerRows.h"
#include "StrokeFinishTestGate.h"
#include "WarpOpeningWork.h"

#include "../Tests/Support/DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr int kSide = 16;
constexpr DWORD kWaitMs = 10000;
constexpr std::size_t kDocumentedInputCapacity = 512; // Application.h's CanvasBrushInput alias.
constexpr BrushSettings kSettings{6, .8, 0, 0, 0, 1};
constexpr Vec2 kDown{7.25, 7.25};
constexpr Vec2 kNearA{8.25, 7.25};
constexpr Vec2 kNearB{9.25, 7.25};
constexpr Vec2 kRefused{10.25, 8.25};
constexpr Vec2 kLateUp{6.25, 8.25};
constexpr Vec2 kLateMove{8.25, 8.25};

// Explicit 16x16 pattern: each nibble selects one fixed BGRA palette entry.
constexpr std::array<std::string_view, kSide> kPatternRows{
    "0123456789ABCDEF", "123456789ABCDEF0", "23456789ABCDEF01", "3456789ABCDEF012",
    "456789ABCDEF0123", "56789ABCDEF01234", "6789ABCDEF012345", "789ABCDEF0123456",
    "89ABCDEF01234567", "9ABCDEF012345678", "ABCDEF0123456789", "BCDEF0123456789A",
    "CDEF0123456789AB", "DEF0123456789ABC", "EF0123456789ABCD", "F0123456789ABCDE"};
constexpr std::array<std::array<std::uint8_t, 4>, 16> kPalette{{
    {{0x10, 0x18, 0x28, 0xFF}},
    {{0xD0, 0x30, 0x20, 0xFF}},
    {{0x20, 0xA0, 0x40, 0xFF}},
    {{0xE0, 0xC0, 0x30, 0xFF}},
    {{0x38, 0x50, 0xD8, 0xFF}},
    {{0xD0, 0x40, 0xB0, 0xFF}},
    {{0x30, 0xB8, 0xC8, 0xFF}},
    {{0xE8, 0x78, 0x28, 0xFF}},
    {{0x70, 0x30, 0x18, 0xFF}},
    {{0x20, 0x78, 0x30, 0xFF}},
    {{0x98, 0x80, 0x20, 0xFF}},
    {{0x20, 0x30, 0x90, 0xFF}},
    {{0x88, 0x28, 0x70, 0xFF}},
    {{0x20, 0x70, 0x78, 0xFF}},
    {{0xA0, 0x50, 0x18, 0xFF}},
    {{0xE8, 0xE0, 0xD0, 0xFF}},
}};

std::array<std::uint8_t, kSide * kSide * 4> PatternBytes() noexcept
{
    std::array<std::uint8_t, kSide * kSide * 4> bytes{};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
        {
            const auto character = kPatternRows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
            const auto index = character <= '9' ? static_cast<std::size_t>(character - '0')
                                                : static_cast<std::size_t>(character - 'A' + 10);
            const auto pixel = static_cast<std::size_t>((y * kSide + x) * 4);
            for (std::size_t channel = 0; channel < 4; ++channel)
                bytes[pixel + channel] = kPalette[index][channel];
        }
    return bytes;
}

CanvasDocument Scene(Guid& active)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = fixture.height = kSide;
    const auto bytes = PatternBytes();
    auto layer = ImageLayer::Blank(L"Warp live input prefix fixture", {kSide, kSide});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset =
        RasterAsset{Raster::Make(kSide, kSide, RasterFormat::Bgra8888, bytes.data(), bytes.size()), {}, layer.name};
    active = layer.id;
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

// Independent original contiguous WarpStroke::Append then scalar BrushStroke::Append/Flush reference.
// It bypasses the live preview/replay/host pipeline and consumes only the point list recorded by the fixture.
std::optional<BrushStroke::Commit> Reference(const ImageLayer& original, SmearMode mode,
                                             std::span<const Vec2> acceptedMoves)
{
    if (!original.asset)
        return {};
    WarpStroke warp(original.asset->image, mode, kSettings);
    warp.Append(kDown); // Down primes carried/source state; it is not a point dab.
    for (const auto point : acceptedMoves)
        warp.Append(point); // Lost and overflow terminals contribute no extra point.
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

bool SamePixels(const Raster& actual, const Raster& expected, int tolerance) noexcept
{
    if (!actual.HasStorage() || !expected.HasStorage() || actual.Width() != kSide || actual.Height() != kSide ||
        expected.Width() != kSide || expected.Height() != kSide)
        return false;
    std::array<std::uint8_t, kSide * 4> a{};
    std::array<std::uint8_t, kSide * 4> b{};
    for (int y = 0; y < kSide; ++y)
    {
        actual.ReadRegion(0, y, kSide, 1, a.data(), kSide * 4, 0);
        expected.ReadRegion(0, y, kSide, 1, b.data(), kSide * 4, 0);
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::abs(static_cast<int>(a[i]) - b[i]) > tolerance)
                return false;
    }
    return true;
}

// Harness-only readback from the acknowledged actual GPU root.
bool SameDisplay(RecordedDisplayGraph* graph, const Raster& expected, int tolerance) noexcept
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
        expected.ReadRegion(0, y, kSide, 1, row.data(), kSide * 4, 0);
        for (std::size_t i = 0; i < row.size(); ++i)
            if (std::abs(static_cast<int>(bytes[static_cast<std::size_t>(y) * row.size() + i]) - row[i]) > tolerance)
                return false;
    }
    return true;
}
} // namespace

int Application::RunWarpLiveInputSelfTest(HINSTANCE instance) noexcept
{
    static_assert(CanvasBrushInput::kCapacity == kDocumentedInputCapacity);
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool condition, const char* mode, const char* scenario, const char* control) noexcept
    {
        std::printf("%s application-warp-live-input.%s.%s.%s\n", condition ? "PASS" : "FAIL", mode, scenario, control);
        std::fflush(stdout);
        ++checks;
        failures += condition ? 0u : 1u;
    };
    const auto fail = [](const char* control) noexcept
    {
        std::printf("FAIL application-warp-live-input.setup.%s\n", control);
        std::fflush(stdout);
        return 5;
    };

    std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
    if (!owner)
        return fail("owner-allocation");
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
                (void)PumpMessages();
            }
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
            const auto result = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                            MWMO_INPUTAVAILABLE);
            if (result != WAIT_OBJECT_0 && result != WAIT_OBJECT_0 + 1)
                return false;
        }
    };
    const auto record = [&](DxUi::PointerAction action, Vec2 documentPoint) noexcept
    {
        DxUi::PointerEvent event{};
        event.action = action;
        event.modifiers = action == DxUi::PointerAction::Up ? 0u : MK_LBUTTON;
        const auto view = owner->_viewport.ViewFromDocument(documentPoint, owner->DocumentSize());
        const auto actualPoint = owner->_viewport.DocumentFromView(view, owner->DocumentSize());
        owner->RecordCanvasBrush(event, view);
        return actualPoint;
    };
    const auto beginAtOpeningBoundary = [&](SmearMode mode) noexcept
    {
        if (owner->_session.smearMode != mode)
            return false;
        DxUi::PointerEvent down{};
        down.action = DxUi::PointerAction::Down;
        down.modifiers = MK_LBUTTON;
        if (!owner->QueueCanvasBrush(down, owner->_viewport.ViewFromDocument(kDown, owner->DocumentSize())))
            return false;
        return pumpUntil(
            [&]() noexcept
            { return owner->_strokeFinishTestGate->sampleBoundaryVisits.load(std::memory_order_acquire) == 1; });
    };
    const auto originalParentMatches = [&](const std::optional<JobTicket>& ticket, std::uint64_t ordinal) noexcept
    {
        return ticket && ticket->generation != 0 && owner->_utilityTicket == ticket && owner->_activeHostRequest &&
               owner->_activeHostRequest->ordinal == ordinal;
    };
    const auto configureScene = [&](Guid& active, ImageLayer& original) noexcept
    {
        auto document = Scene(active);
        if (document.layers.empty())
            return false;
        original = document.layers.front();
        owner->ReplaceDocument(std::move(document), active, {});
        owner->_tool = EditorTool::Smear;
        owner->_session.smearMode = SmearMode::Liquify;
        owner->_session.brushSettings = kSettings;
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        owner->SyncChrome();
        return settle();
    };

    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return fail("hidden-owner-initialize");
    owner->BindCompositionDevice();
    if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
        !owner->_strokeFinishSource.Start() || !owner->_brushEngine.Ready())
        return fail("actual-host-sources");
    owner->_asyncWarpLiveEnabled = true;

    const auto runScenario = [&](SmearMode mode, bool overflow) noexcept -> const char*
    {
        const char* modeName = mode == SmearMode::Liquify ? "liquify" : "smudge";
        const char* scenarioName = overflow ? "fifo-overflow" : "lost-prefix";
        Guid active{};
        ImageLayer original;
        if (!configureScene(active, original))
            return "scene-settle";

        auto gate = std::make_shared<StrokeFinishTestGate>(true);
        gate->holdSample = StrokeFinishTestGate::SampleBoundary::OpeningPrepared;
        gate->holdSampleNumber = 0;
        owner->_strokeFinishTestGate = gate;
        owner->_session.smearMode = mode;
        owner->SyncChrome();
        if (!beginAtOpeningBoundary(mode))
            return "actual-opening-prepared-boundary";
        const auto ordinal = owner->_activeHostRequest ? owner->_activeHostRequest->ordinal : 0;
        const auto ticket = owner->_utilityTicket;
        const auto* contextPointer = owner->_canvasBrushInput.CapturedContext(owner->_canvasBrushInput.Opening());
        if (!ordinal || !ticket || !contextPointer || !original.asset)
            return "captured-parent-context";
        const auto contextBeforeInput = *contextPointer;
        const auto patternBytes = PatternBytes();
        const auto expectedSource =
            Raster::Make(kSide, kSide, RasterFormat::Bgra8888, patternBytes.data(), patternBytes.size());
        expect(expectedSource.HasStorage() && SamePixels(original.asset->image, expectedSource, 0) &&
                   owner->_acknowledgedCommittedDisplay &&
                   SameDisplay(owner->_acknowledgedCommittedDisplay, expectedSource, 0),
               modeName, scenarioName, "literal-source-and-initial-gpu-root-exact0");

        if (!overflow)
            expect(contextBeforeInput.tool == EditorTool::Smear && contextBeforeInput.smear == mode &&
                       owner->_canvasBrushInput.PendingCount() == 1 && originalParentMatches(ticket, ordinal),
                   modeName, scenarioName, "down-captures-mode-and-original-parent-before-move");

        if (overflow)
        {
            std::array<Vec2, kDocumentedInputCapacity - 1> acceptedMoves{};
            const auto* beforeOverflow = owner->_canvasBrushInput.CapturedContext(owner->_canvasBrushInput.Opening());
            for (std::size_t i = 0; i < acceptedMoves.size(); ++i)
            {
                const Vec2 intended = i == 0 ? kNearA : kNearB;
                acceptedMoves[i] = record(DxUi::PointerAction::Move, intended);
            }
            const auto acceptedCountBeforeOverflow = owner->_canvasBrushInput.PendingCount();
            const auto capturedBeforeOverflow =
                owner->_canvasBrushInput.CapturedContext(owner->_canvasBrushInput.Opening());
            const auto capturedSnapshot = capturedBeforeOverflow ? *capturedBeforeOverflow : CanvasBrushContext{};
            expect(beforeOverflow && capturedBeforeOverflow &&
                       acceptedCountBeforeOverflow == kDocumentedInputCapacity &&
                       capturedSnapshot.tool == EditorTool::Smear && capturedSnapshot.smear == mode &&
                       capturedSnapshot.documentSerial == contextBeforeInput.documentSerial &&
                       capturedSnapshot.target == contextBeforeInput.target && originalParentMatches(ticket, ordinal),
                   modeName, scenarioName, "512-accepted-events-and-context-captured-before-overflow");

            (void)record(DxUi::PointerAction::Move, kRefused); // The actual next Record overflows and seals Lost.
            const auto sealedCount = owner->_canvasBrushInput.PendingCount();
            (void)record(DxUi::PointerAction::Up, kLateUp);
            (void)record(DxUi::PointerAction::Move, kLateMove);
            expect(owner->_canvasBrushInput.Overflowed() && !owner->_canvasBrushInput.Sampling() &&
                       sealedCount == kDocumentedInputCapacity + 1 &&
                       owner->_canvasBrushInput.PendingCount() == sealedCount && originalParentMatches(ticket, ordinal),
                   modeName, scenarioName, "real-overflow-seals-lost-late-up-move-closed-parent-stable");

            if (!SetEvent(gate->release.get()) ||
                !pumpUntil([&]() noexcept { return owner->_session.PendingWarp() != nullptr; }) ||
                !ResetEvent(gate->reached.get()) || !ResetEvent(gate->release.get()))
                return "release-opening-and-arm-final-gate";
            if (!pumpUntil([&]() noexcept { return gate->prepares.load(std::memory_order_acquire) == 1; }))
                return "overflow-prefix-final-preparation";
            expect(originalParentMatches(ticket, ordinal) &&
                       owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                       gate->samplesAcknowledged.load(std::memory_order_acquire) == acceptedCountBeforeOverflow - 1 &&
                       gate->previewsPublished.load(std::memory_order_acquire) == 2,
                   modeName, scenarioName, "511-samples-two-previews-original-parent-retained-through-final-prepare");

            if (!SetEvent(gate->release.get()) || !settle())
                return "overflow-prefix-final-settle";
            const auto reference = Reference(original, mode, acceptedMoves);
            const auto* layer = owner->_session.ActiveLayer();
            expect(reference && layer && layer->asset && SamePixels(layer->asset->image, reference->image, 2) &&
                       layer->transform == reference->transform && owner->_session.history.UndoCount() == 1 &&
                       owner->_acknowledgedCommittedDisplay &&
                       SameDisplay(owner->_acknowledgedCommittedDisplay, reference->image, 2),
                   modeName, scenarioName, "committed-accepted-prefix-matches-contiguous-reference-and-gpu-2of255");
            (void)record(DxUi::PointerAction::Up, kLateUp);
            (void)record(DxUi::PointerAction::Move, kLateMove);
            layer = owner->_session.ActiveLayer();
            // Rendering_Brush's original overflow contract preserves the committed accepted prefix while
            // reporting the refused sample, even after late physical events arrive following retirement.
            expect(reference && owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Failed &&
                       owner->_canvasBrushReceipt.Read().error ==
                           static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
                       !owner->_activeHostRequest && !owner->_utilityTicket && !owner->_canvasBrushInput.Busy() &&
                       gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load() && layer &&
                       layer->asset && SamePixels(layer->asset->image, reference->image, 2) &&
                       owner->_session.history.UndoCount() == 1 && owner->_acknowledgedCommittedDisplay &&
                       SameDisplay(owner->_acknowledgedCommittedDisplay, reference->image, 2),
                   modeName, scenarioName, "receipt-retires-parent-and-post-receipt-input-is-inert");
        }
        else
        {
            const auto movePoint = record(DxUi::PointerAction::Move, kNearB);
            const std::array<Vec2, 1> acceptedMoves{movePoint};
            const auto countBeforeLost = owner->_canvasBrushInput.PendingCount();
            owner->LoseCanvasBrush(); // Exercise the real lost-capture route and terminal slot.
            const auto sealedCount = owner->_canvasBrushInput.PendingCount();
            (void)record(DxUi::PointerAction::Up, kLateUp);
            (void)record(DxUi::PointerAction::Move, kLateMove);
            expect(countBeforeLost == 2 && sealedCount == 3 && !owner->_canvasBrushInput.Sampling() &&
                       !owner->_canvasBrushInput.Overflowed() &&
                       owner->_canvasBrushInput.PendingCount() == sealedCount && originalParentMatches(ticket, ordinal),
                   modeName, scenarioName, "real-lost-seals-down-move-no-up-point-or-late-extension");

            if (!SetEvent(gate->release.get()) ||
                !pumpUntil([&]() noexcept { return owner->_session.PendingWarp() != nullptr; }) ||
                !ResetEvent(gate->reached.get()) || !ResetEvent(gate->release.get()))
                return "release-opening-and-arm-final-gate";
            if (!pumpUntil([&]() noexcept { return gate->prepares.load(std::memory_order_acquire) == 1; }))
                return "lost-prefix-final-preparation";
            expect(originalParentMatches(ticket, ordinal) &&
                       owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending,
                   modeName, scenarioName, "original-ticket-and-ordinal-retained-through-final-prepare");

            if (!SetEvent(gate->release.get()) || !settle())
                return "lost-prefix-final-settle";
            const auto reference = Reference(original, mode, acceptedMoves);
            const auto* layer = owner->_session.ActiveLayer();
            expect(reference && layer && layer->asset && SamePixels(layer->asset->image, reference->image, 2) &&
                       layer->transform == reference->transform && owner->_session.history.UndoCount() == 1 &&
                       owner->_acknowledgedCommittedDisplay &&
                       SameDisplay(owner->_acknowledgedCommittedDisplay, reference->image, 2),
                   modeName, scenarioName, "committed-prefix-matches-contiguous-reference-and-gpu-2of255");
            (void)record(DxUi::PointerAction::Up, kLateUp);
            (void)record(DxUi::PointerAction::Move, kLateMove);
            layer = owner->_session.ActiveLayer();
            expect(reference && owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                       owner->_canvasBrushReceipt.Read().error == S_OK && !owner->_activeHostRequest &&
                       !owner->_utilityTicket && !owner->_canvasBrushInput.Busy() &&
                       gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load() && layer &&
                       layer->asset && SamePixels(layer->asset->image, reference->image, 2) &&
                       owner->_session.history.UndoCount() == 1 && owner->_acknowledgedCommittedDisplay &&
                       SameDisplay(owner->_acknowledgedCommittedDisplay, reference->image, 2),
                   modeName, scenarioName, "receipt-retires-parent-and-post-receipt-input-is-inert");
        }
        owner->_strokeFinishTestGate.reset();
        return nullptr;
    };

    constexpr std::array modes{SmearMode::Liquify, SmearMode::Smudge};
    for (const auto mode : modes)
    {
        if (const auto* setupFailure = runScenario(mode, false))
            return fail(setupFailure);
        if (const auto* setupFailure = runScenario(mode, true))
            return fail(setupFailure);
    }
    constexpr unsigned int kExpectedChecks = 24; // 2 modes * 2 terminal cases * 6 explicit controls.
    if (checks != kExpectedChecks)
    {
        std::printf("FAIL application-warp-live-input.fixed-check-count expected=%u actual=%u\n", kExpectedChecks,
                    checks);
        ++failures;
    }
    std::printf("SUMMARY application-warp-live-input checks=%u failures=%u expected=%u\n", checks, failures,
                kExpectedChecks);
    return failures == 0 ? 0 : 5;
}
