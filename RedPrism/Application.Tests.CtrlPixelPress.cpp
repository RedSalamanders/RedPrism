#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PixelMovePreviewWork.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Runtime.Tests.ColorWorkTestGate.h"
#include "Runtime.Tests.LayerRowsTestGate.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <span>
#include <thread>
#include <vector>

namespace
{
constexpr int kWidth = 5;
constexpr int kHeight = 3;
constexpr std::size_t kPixelBytes = static_cast<std::size_t>(kWidth * kHeight * 4);
using Pixels = std::array<std::uint8_t, kPixelBytes>;

Pixels SourcePixels() noexcept
{
    Pixels pixels{};
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        pixels[offset] = 255; // BGRA blue background
        pixels[offset + 3] = 255;
    }
    constexpr std::size_t red = static_cast<std::size_t>((1 * kWidth + 1) * 4);
    pixels[red] = 0;
    pixels[red + 1] = 0;
    pixels[red + 2] = 255;
    return pixels;
}

Pixels MovedPixels(bool duplicate, bool moved) noexcept
{
    auto pixels = SourcePixels();
    if (!moved)
        return pixels;
    constexpr std::size_t source = static_cast<std::size_t>((1 * kWidth + 1) * 4);
    constexpr std::size_t landing = static_cast<std::size_t>((2 * kWidth + 3) * 4);
    if (!duplicate)
        for (std::size_t channel = 0; channel < 4; ++channel)
            pixels[source + channel] = 0;
    pixels[landing] = 0;
    pixels[landing + 1] = 0;
    pixels[landing + 2] = 255;
    pixels[landing + 3] = 255;
    return pixels;
}

std::optional<CanvasDocument> PixelFixture()
{
    DocumentFixture fixture;
    fixture.width = kWidth;
    fixture.height = kHeight;
    fixture.selection = DocumentSelection{false, {{{1, 1}, {2, 1}, {2, 2}, {1, 2}}}};
    auto layer = ImageLayer::Blank(L"Ctrl pixel press", Vec2{kWidth, kHeight});
    layer.transform.sampling = LayerSampling::Nearest;
    const auto pixels = SourcePixels();
    auto raster = Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    if (!raster.HasStorage())
        return std::nullopt;
    layer.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), layer.name};
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

bool MatchesLiteral(RecordedDisplayGraph* graph, std::span<const std::uint8_t> expected) noexcept
{
    if (!graph || !graph->CompositionForTestOwner())
        return false;
    std::vector<std::uint8_t> actual;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(graph->CompositionForTestOwner()->Readback(actual, width, height)) || width != kWidth ||
        height != kHeight || actual.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
        if (actual[index] != expected[index])
        {
            std::printf("INFO ctrl-pixel-press.literal-byte=%zu expected=%u actual=%u\n", index,
                        static_cast<unsigned>(expected[index]), static_cast<unsigned>(actual[index]));
            return false;
        }
    return true;
}

bool MatchesCpuPixels(const CanvasDocument& document, std::span<const std::uint8_t> expected) noexcept
{
    if (document.layers.size() != 1 || !document.layers[0].asset)
        return false;
    const auto& raster = document.layers[0].asset->image;
    const auto* actual = raster.Data();
    if (!actual || raster.Width() != kWidth || raster.Height() != kHeight || raster.Stride() != kWidth * 4)
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
        if (actual[index] != expected[index])
            return false;
    return true;
}

void SetKeys(bool control, bool shift, bool alt) noexcept
{
    std::array<BYTE, 256> keys{};
    keys[VK_CONTROL] = control ? 0x80 : 0;
    keys[VK_SHIFT] = shift ? 0x80 : 0;
    keys[VK_MENU] = alt ? 0x80 : 0;
    KeyboardState::Write(keys);
}
} // namespace

int Application::RunStandaloneCtrlPixelPressSelfTest(HINSTANCE instance) noexcept
{
    unsigned checks = 0;
    unsigned failures = 0;
    const char* scenario = "setup";
    const auto expect = [&](bool condition, const char* label) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s application-ctrl-pixel-press.%s.%s\n", condition ? "PASS" : "FAIL", scenario, label);
        return condition;
    };
    const auto summary = wil::scope_exit(
        [&]() noexcept
        { std::printf("SUMMARY application-ctrl-pixel-press checks=%u failures=%u\n", checks, failures); });

    bool refuseMemory = false;
    ApplicationProcessingTestProbe probe;
    probe.workerCount = 2;
    probe.memory = {[](void* context, MemorySnapshot& snapshot) noexcept
                    {
                        const auto available = *static_cast<bool*>(context) ? 0ULL : 8ULL * 1024 * 1024 * 1024;
                        snapshot = {available, available};
                        return true;
                    },
                    &refuseMemory};
    auto owner = std::unique_ptr<Application>(new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr));
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (owner)
            {
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            }
        });
    if (!expect(owner != nullptr, "actual-owner-allocated"))
        return 5;
    struct PublicationObservation
    {
        Application* owner = nullptr;
        Pixels expected{};
        std::uint64_t generation = 0;
        int undo = 0;
        unsigned finals = 0;
        bool enabled = false;
        bool atomic = true;
    } publication{owner.get()};
    probe.context = &publication;
    probe.committedDisplayPublished = [](void* raw, RecordedDisplayGraph* graph) noexcept
    {
        auto& value = *static_cast<PublicationObservation*>(raw);
        auto& app = *value.owner;
        if (!value.enabled || !app._finalPublicationCurrent || app._session.Generation() == value.generation)
            return; // Recovery may publish the still-current committed source before final edit publication.
        const auto* state = app._polygonalPressTestEnabled
                                ? (app._polygonalPressState ? &app._polygonalPressState.value() : nullptr)
                                : (app._selectionPressState ? &app._selectionPressState.value() : nullptr);
        ++value.finals;
        value.atomic = value.atomic && graph && graph == app._acknowledgedCommittedDisplay && state &&
                       !state->completed && app._jobs.InAcknowledgedJobCallback(state->original) &&
                       app._session.Generation() > value.generation &&
                       app._session.history.UndoCount() == value.undo + 1 && !app._session.PendingPixelMove() &&
                       app._preparedLayerRows && app._chrome &&
                       app._chrome->PreparedLayerAccessibilityForTest().get() == app._preparedLayerRows.get() &&
                       app._layerRowsPublishedKey == std::optional(app.CurrentLayerRowsKey()) &&
                       app._layerRowsCacheTicket == std::optional(state->original) &&
                       MatchesCpuPixels(app._session.Document().value(), value.expected);
    };
    owner->_processingTestProbe = &probe;
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                    SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                    SUCCEEDED(owner->InitializeChrome()) && owner->EnableCommittedDisplayForTest() &&
                    owner->EnableSelectionOutlineForTest(),
                "hidden-WARP-application-WndProc-and-display-owners-enabled"))
        return 5;
    if (!expect(owner->EnablePreparedLayerRows(), "prepared-row-owner-enabled"))
        return 5;
    if (!expect(owner->_strokeFinishSource.Start(), "original-final-readiness-source-attached"))
        return 5;
    owner->_selectionPixelFinalPublicationEnabled = true;
    owner->_selectionPressTestEnabled = true;
    KeyboardState::ScopedSynthetic keyboard;
    // SetKeys also updates this thread's Win32 key state for DefWindowProc. The final
    // memory-refusal branch and every early return must release those synthetic modifiers.
    const auto clearThreadModifiers = wil::scope_exit([]() noexcept { SetKeys(false, false, false); });

    const auto pumpUntil = [&](auto done) noexcept
    {
        const auto deadline = GetTickCount64() + 15000;
        while (!done())
        {
            if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                return false;
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (owner->_frameInvalidated)
                owner->RenderFrame();
            if (done())
                return true;
            const auto now = GetTickCount64();
            if (now >= deadline)
                return false;
            if (owner->_jobs.HasUiWork())
                continue; // Consume actual readiness only; no unchanged-source self-rearm.
            const HANDLE wake = owner->_jobs.WakeHandle();
            if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                return false;
        }
        return true;
    };
    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, 30000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };

    enum class Case
    {
        Move,
        Duplicate,
        ControlShift,
        LaterModifiers,
        EarlyUp,
        ZeroOffset,
        CaptureLost,
        MemoryRefused
    };
    constexpr std::array cases{Case::Move,    Case::Duplicate,  Case::ControlShift, Case::LaterModifiers,
                               Case::EarlyUp, Case::ZeroOffset, Case::CaptureLost,  Case::MemoryRefused};
    constexpr std::array names{"move",     "duplicate",   "ctrl-shift",   "later-modifiers",
                               "early-up", "zero-offset", "capture-lost", "memory-refused"};
    constexpr std::array polygonalNames{
        "polygonal-move",     "polygonal-duplicate",   "polygonal-ctrl-shift",   "polygonal-later-modifiers",
        "polygonal-early-up", "polygonal-zero-offset", "polygonal-capture-lost", "polygonal-memory-refused"};
    for (std::size_t index = 0; index < cases.size() * 2; ++index)
    {
        const bool polygonal = index >= cases.size();
        const auto item = index % cases.size();
        const auto test = cases[item];
        scenario = polygonal ? polygonalNames[item] : names[item];
        auto fixture = PixelFixture();
        if (!expect(fixture.has_value(), "independent-red-blue-fixture-created"))
            return 5;
        const auto active = fixture->layers[0].id;
        owner->_session.ReplaceDocument(std::move(fixture.value()), active);
        owner->_session.history.Reset();
        owner->_tool = polygonal ? EditorTool::Lasso : EditorTool::Marquee;
        owner->_polygonalPressTestEnabled = polygonal;
        owner->_polygonalPixelPressEnabled = polygonal;
        if (polygonal)
            owner->_session.lassoKind = LassoKind::Polygonal;
        owner->_session.marqueeKind = LassoKind::Rectangle;
        owner->_session.selectionModeChoice = test == Case::ControlShift ? SelectionMode::Add : SelectionMode::New;
        owner->_session.selectionAntialiased = false;
        owner->_viewport.followsFit = false;
        owner->_viewport.zoom = 8;
        owner->DocumentChanged();
        if (!expect(settle(), "source-display-and-jobs-settled"))
            return 5;
        owner->RenderFrame();
        publication.enabled = false;
        const auto original = SourcePixels();
        if (!expect(MatchesLiteral(owner->_acknowledgedCommittedDisplay, original), "committed-source-literal"))
            return 5;

        const auto hole = owner->_chrome->HolePixels();
        const auto origin = owner->_viewport.DocumentRectOrigin(owner->DocumentSize());
        owner->_viewport.pan.x += std::round(hole.left + origin.x) - (hole.left + origin.x);
        owner->_viewport.pan.y += std::round(hole.top + origin.y) - (hole.top + origin.y);
        const auto send = [&](UINT message, Vec2 document, WPARAM modifiers) noexcept
        {
            const auto view = owner->_viewport.ViewFromDocument(document, owner->DocumentSize());
            const auto currentHole = owner->_chrome->HolePixels();
            const int x = static_cast<int>(std::lround(currentHole.left + view.x));
            const int y = static_cast<int>(std::lround(currentHole.top + view.y));
            SendMessageW(owner->_window.get(), message, modifiers, MAKELPARAM(x, y));
        };
        const auto stateSlot = [&]() noexcept -> std::optional<SelectionOutlineGestureState>&
        { return polygonal ? owner->_polygonalPressState : owner->_selectionPressState; };
        const auto openingCounter = [&]() noexcept
        { return polygonal ? owner->_polygonalPressFamily : owner->_selectionPressOpening; };
        const auto inputOpening = [&]() noexcept
        { return polygonal ? owner->_polygonalPressFamily : owner->_selectionPressInput.Opening(); };
        const auto inputBusy = [&]() noexcept
        { return polygonal ? owner->_polygonalPressInput.Busy() : owner->_selectionPressInput.Busy(); };
        const auto parentsBefore = owner->_polygonalPressParents;
        const auto generation = owner->_session.Generation();
        const auto undo = owner->_session.history.UndoCount();
        publication.generation = owner->_session.Generation();
        publication.undo = undo;
        publication.expected = MovedPixels(test == Case::Duplicate, true);
        publication.finals = 0;
        publication.atomic = true;
        publication.enabled = true;
        const auto openingBefore = openingCounter();
        refuseMemory = test == Case::MemoryRefused;
        SetKeys(true, test == Case::ControlShift, test == Case::Duplicate);
        // Alt is a captured event-time keyboard state, not a WM_MOUSEMOVE/WM_LBUTTONDOWN wParam flag.
        const WPARAM downModifiers = MK_LBUTTON | MK_CONTROL | (test == Case::ControlShift ? MK_SHIFT : 0);
        send(WM_LBUTTONDOWN, {1.5, 1.5}, downModifiers);
        const auto downReceipt = polygonal ? owner->_polygonalPressLastReceipt : owner->_selectionPressReceipt;
        std::optional<HostSelectionPressInput::Context> captured;
        if (polygonal)
        {
            const auto down = owner->_polygonalPressInput.Peek(inputOpening());
            if (down && down->down)
                captured = down->down;
        }
        else if (const auto* context = owner->_selectionPressInput.CapturedContext(inputOpening()))
            captured = *context;
        if (!expect(openingCounter() == openingBefore + 1 && inputBusy() &&
                        downReceipt.Read().outcome == HostOutcome::Pending && captured &&
                        captured->mode == (test == Case::Duplicate      ? SelectionMode::Subtract
                                           : test == Case::ControlShift ? SelectionMode::Add
                                                                        : SelectionMode::New) &&
                        captured->ordinal != 0 && !owner->_session.PendingPixelMove() &&
                        !owner->_session.IsMovingSelection(),
                    "WndProc-down-captures-fixed-mode-and-opening-before-source-lift"))
            return 5;

        if (test == Case::MemoryRefused)
        {
            if (!expect(settle(), "refused-original-admission-acknowledged"))
                return 5;
            const auto* state = stateSlot() ? &stateSlot().value() : nullptr;
            if (!expect(state && !state->opened && state->captures == 0 && state->hits == 0 &&
                            downReceipt.Read().outcome == HostOutcome::Failed &&
                            owner->_session.history.UndoCount() == undo &&
                            MatchesCpuPixels(owner->_session.Document().value(), original),
                        "memory-refusal-does-not-lift-or-fall-through-to-drawing"))
                return 5;
            refuseMemory = false;
            continue;
        }

        if (test == Case::EarlyUp)
            send(WM_LBUTTONUP, {1.5, 1.5}, 0); // Accepted before admission/source capture, without pumping.
        else
        {
            if (!expect(pumpUntil(
                            [&]() noexcept
                            {
                                return stateSlot() && stateSlot()->opened && stateSlot()->pixelMove &&
                                       stateSlot()->waiting;
                            }),
                        "original-parent-opens-and-parks-for-pointer-input"))
                return 5;
            const auto opening = inputOpening();
            auto* state = &stateSlot().value();
            if (!expect(state->opening == opening && state->original.generation != 0 && state->captures == 1 &&
                            state->hits == 1 && state->sameTicket &&
                            (polygonal ? owner->_polygonalPressParents == parentsBefore + 1
                                       : owner->_selectionPressMovePresentations == 0) &&
                            !owner->_pixelMovePreviewTicket && !owner->_pixelMoveCommitTicket &&
                            owner->_session.PendingPixelMove() && owner->_session.PendingPixelMove()->deferred &&
                            !owner->_composition.HasStroke(),
                        "one-original-ticket-opening-and-lift-no-legacy-pixel-preview-or-UI-commit"))
                return 5;
        }

        auto* state = stateSlot() ? &stateSlot().value() : nullptr;

        const bool moved = test == Case::Move || test == Case::Duplicate || test == Case::ControlShift ||
                           test == Case::LaterModifiers || test == Case::CaptureLost;
        std::shared_ptr<const PreparedPixelMovePreview> heldFrame;
        std::weak_ptr<const PreparedPixelMovePreview> frameWeak;
        std::weak_ptr<const SharedPublicationMemory> frameMemoryWeak;
        std::uint64_t frameBytes = 0;
        if (moved)
        {
            SetKeys(test != Case::LaterModifiers, test == Case::LaterModifiers, test == Case::LaterModifiers);
            const WPARAM modifiers = MK_LBUTTON | (test == Case::LaterModifiers ? MK_SHIFT : MK_CONTROL);
            send(WM_MOUSEMOVE, {3.5, 2.5}, modifiers);
            if (!expect(pumpUntil([&]() noexcept { return state->moves == 1 && state->waiting; }),
                        "move-is-consumed-by-same-parked-parent"))
                return 5;
            const auto preview = MovedPixels(test == Case::Duplicate, true);
            if (!expect(owner->_session.PendingPixelMove() &&
                            owner->_session.PendingPixelMove()->offset == Vec2{2, 1} &&
                            !owner->_pixelMovePreviewTicket && !owner->_pixelMoveCommitTicket &&
                            !owner->_composition.HasStroke(),
                        "held-offset-is-pending-without-legacy-preview-ticket-or-synchronous-pixels"))
                return 5;
            if (!expect(pumpUntil([&]() noexcept { return owner->_pendingDisplay.HasCurrentPublicationOwner(); }) &&
                            MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), preview),
                        "held-WARP-pending-display-matches-independent-source-hole-and-landing-bytes"))
                return 5;
            heldFrame =
                owner->_pendingDisplay.GraphForFrameOwner()->CompositionForTestOwner()->PixelPreviewForTestOwner();
            if (!expect(
                    heldFrame && heldFrame->Memory() && heldFrame->Memory()->IsBound() &&
                        heldFrame->Memory() != owner->_session.PendingPixelMove()->proceduralMemory &&
                        owner->_session.PendingPixelMove()->proceduralMemory->BoundTo(owner->_jobs, state->original),
                    "frame-and-original-lift-have-distinct-bound-original-admissions"))
                return 5;
            frameBytes = heldFrame->Memory()->Bytes();
            frameWeak = heldFrame;
            frameMemoryWeak = heldFrame->Memory();
            if (test == Case::Move)
            {
                send(WM_MOUSEMOVE, {1.5, 1.5}, MK_LBUTTON | MK_CONTROL);
                if (!expect(pumpUntil([&]() noexcept { return state->moves == 2 && state->waiting; }) &&
                                owner->_session.PendingPixelMove()->offset == Vec2{} &&
                                owner->_pendingDisplay.GraphForFrameOwner() == nullptr &&
                                MatchesLiteral(owner->_acknowledgedCommittedDisplay, original) &&
                                owner->_session.Generation() == generation &&
                                owner->_session.history.UndoCount() == undo,
                            "return-to-zero-excludes-old-landing-and-shows-source-without-document-or-history-change"))
                    return 5;
                send(WM_MOUSEMOVE, {3.5, 2.5}, MK_LBUTTON | MK_CONTROL);
                if (!expect(pumpUntil(
                                [&]() noexcept
                                {
                                    return state->moves == 3 && state->waiting &&
                                           owner->_pendingDisplay.HasCurrentPublicationOwner();
                                }) &&
                                MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), preview) &&
                                !owner->_pixelMovePreviewTicket && state->sameTicket,
                            "redrag-rebuilds-complete-procedural-grid-on-same-original-parent"))
                    return 5;
            }
            else if (test == Case::Duplicate)
            {
                const auto deviceGeneration = owner->_renderer.DeviceGeneration();
                const auto recovered = owner->_renderer.RecoverDeviceForTest();
                owner->BindCompositionDevice();
                if (!expect(
                        SUCCEEDED(recovered) && owner->_renderer.DeviceGeneration() != deviceGeneration &&
                            pumpUntil([&]() noexcept { return owner->_pendingDisplay.HasCurrentPublicationOwner(); }) &&
                            MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), preview) && state->sameTicket &&
                            state->captures == 1 && state->hits == 1 && !owner->_pixelMovePreviewTicket &&
                            owner->_session.Generation() == generation,
                        "real-device-rebuild-replays-immutable-lift-without-new-hit-lift-or-parent"))
                    return 5;
            }
            if (test == Case::CaptureLost)
                ReleaseCapture();
            else if (test == Case::LaterModifiers)
            {
                SetKeys(false, false, false);
                send(WM_LBUTTONUP, {3.5, 2.5}, 0);
            }
            else
            {
                SetKeys(true, test == Case::ControlShift, test == Case::Duplicate);
                send(WM_LBUTTONUP, {3.5, 2.5}, 0);
            }
        }
        else if (test == Case::ZeroOffset)
        {
            send(WM_MOUSEMOVE, {1.5, 1.5}, MK_LBUTTON | MK_CONTROL);
            send(WM_LBUTTONUP, {1.5, 1.5}, 0);
        }
        else if (test != Case::EarlyUp)
        {
            send(WM_LBUTTONUP, {1.5, 1.5}, 0);
        }
        SetKeys(false, false, false);
        if (!expect(settle(), "terminal-input-and-all-display-acknowledgements-settled"))
            return 5;

        const bool cancelled = test == Case::CaptureLost;
        const bool shouldMove = moved && !cancelled;
        const auto expected = MovedPixels(test == Case::Duplicate, shouldMove);
        const auto outcome = downReceipt.Read().outcome;
        const auto& completed = stateSlot().value();
        const auto expectedUndo = undo + (shouldMove ? 1 : 0);
        if (!expect(completed.completed && !completed.controller && completed.inputAcknowledged &&
                        completed.captures == 1 && completed.hits == 1 && completed.sameTicket &&
                        completed.closed == !cancelled && completed.aborted == cancelled &&
                        (cancelled ? outcome == HostOutcome::Cancelled : outcome == HostOutcome::Succeeded) &&
                        owner->_session.history.UndoCount() == expectedUndo &&
                        (!shouldMove || owner->_session.Generation() > generation) &&
                        MatchesCpuPixels(owner->_session.Document().value(), expected),
                    "terminal-receipt-single-history-and-literal-final-CPU-raster"))
        {
            std::printf(
                "INFO ctrl-final.terminal scenario=%s completed=%d controller=%d ack=%d captures=%u hits=%u same=%d "
                "closed=%d expectedClosed=%d aborted=%d outcome=%u stateStatus=%u error=%08X undo=%d expectedUndo=%d "
                "generation=%llu initial=%llu cpuLiteral=%d finalClosed=%d callbackCount=%u callbackAtomic=%d\n",
                scenario, completed.completed, completed.controller != nullptr, completed.inputAcknowledged,
                completed.captures, completed.hits, completed.sameTicket, completed.closed, !cancelled,
                completed.aborted, static_cast<unsigned>(outcome), static_cast<unsigned>(completed.outcome.status),
                static_cast<unsigned>(completed.outcome.error), owner->_session.history.UndoCount(), expectedUndo,
                static_cast<unsigned long long>(owner->_session.Generation()),
                static_cast<unsigned long long>(generation),
                MatchesCpuPixels(owner->_session.Document().value(), expected), completed.pixelFinalClosed,
                publication.finals, publication.atomic);
            return 5;
        }
        if (!expect(MatchesLiteral(owner->_acknowledgedCommittedDisplay, expected),
                    "after-up-committed-WARP-display-matches-independent-literal"))
            return 5;
        if (shouldMove && !expect(completed.pixelFinalClosed && publication.finals == 1 && publication.atomic &&
                                      owner->_layerRowsCacheTicket == std::optional(completed.original),
                                  "original-parent-publishes-CPU-history-rows-and-GPU-before-terminal"))
            return 5;
        publication.enabled = false;
        if (heldFrame)
        {
            if (!expect(!owner->_pendingDisplay.HasPending() && heldFrame->Memory()->Bytes() == frameBytes &&
                            frameBytes != 0,
                        "real-last-GPU-use-and-root-retirement-keep-external-frame-reader-charged"))
                return 5;
            heldFrame.reset();
            if (!expect(settle() && frameWeak.expired() && !frameMemoryWeak.expired(),
                        "expired-frame-weak-control-still-retains-original-admission"))
                return 5;
            const auto beforeLastWeak = owner->_jobs.ReservedMemoryBytes();
            frameWeak.reset();
            if (!expect(settle() && frameMemoryWeak.expired() && owner->_jobs.ReservedMemoryBytes() == beforeLastWeak,
                        "expired-memory-weak-control-still-keeps-the-whole-frame-admission"))
                return 5;
            frameMemoryWeak.reset(); // The diagnostic weak owner itself owns the accounting control block.
            if (!expect(settle() && beforeLastWeak >= frameBytes &&
                            owner->_jobs.ReservedMemoryBytes() == beforeLastWeak - frameBytes,
                        "last-frame-weak-control-and-real-release-ack-refund-exact-frame-admission"))
                return 5;
        }
        if (shouldMove && !expect(owner->_session.history.UndoCount() == undo + 1 &&
                                      owner->_session.history.CanUndo() && !owner->_session.PendingPixelMove() &&
                                      !owner->_pixelMovePreviewTicket && !owner->_pixelMoveCommitTicket,
                                  "moved-pixels-create-exactly-one-undo-and-clear-deferred-owners"))
            return 5;
        if (!shouldMove &&
            !expect(owner->_session.history.UndoCount() == undo && owner->_session.Generation() == generation &&
                        MatchesCpuPixels(owner->_session.Document().value(), original),
                    "no-op-or-cancel-keeps-source-and-adds-no-history"))
            return 5;
        if (shouldMove)
        {
            const auto undone = owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
            if (!expect(settle() && undone.Read().outcome == HostOutcome::Succeeded &&
                            owner->_session.history.UndoCount() == undo && owner->_session.history.CanRedo() &&
                            owner->_session.history.RedoName() ==
                                (test == Case::Duplicate ? L"Duplicate Pixels" : L"Move Pixels") &&
                            MatchesCpuPixels(owner->_session.Document().value(), original) &&
                            MatchesLiteral(owner->_acknowledgedCommittedDisplay, original),
                        "actual-queued-undo-restores-source-and-original-committed-frame"))
                return 5;
            const auto redone = owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandRedo});
            if (!expect(settle() && redone.Read().outcome == HostOutcome::Succeeded &&
                            owner->_session.history.UndoCount() == undo + 1 &&
                            MatchesCpuPixels(owner->_session.Document().value(), expected) &&
                            MatchesLiteral(owner->_acknowledgedCommittedDisplay, expected),
                        "actual-queued-redo-restores-exact-move-or-duplicate-frame"))
                return 5;
        }
    }

    scenario = "polygonal-queued-pixel-successor";
    {
        struct ClosureGate
        {
            wil::unique_handle reached{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            const DWORD ownerThread = GetCurrentThreadId();
            std::atomic<unsigned> visits{0};
            std::atomic<bool> failed{false};
        } gate;
        auto joinOnFailure = wil::scope_exit(
            [&]() noexcept
            {
                (void)SetEvent(gate.release.get());
                if (owner)
                    owner->Shutdown(); // The worker must join before its borrowed gate can die.
            });
        if (!expect(gate.reached && gate.release, "closure-worker-gate-created"))
            return 5;
        auto fixture = PixelFixture();
        if (!expect(fixture.has_value(), "independent-source-created"))
            return 5;
        const auto active = fixture->layers[0].id;
        owner->_session.ReplaceDocument(std::move(fixture.value()), active);
        owner->_session.history.Reset();
        owner->_tool = EditorTool::Lasso;
        owner->_session.lassoKind = LassoKind::Polygonal;
        owner->_session.selectionModeChoice = SelectionMode::New;
        owner->_session.selectionAntialiased = false;
        owner->_polygonalPressTestEnabled = owner->_polygonalPixelPressEnabled = true;
        owner->DocumentChanged();
        if (!expect(settle(), "committed-source-and-previous-families-settled"))
            return 5;
        const auto send = [&](UINT message, Vec2 document, WPARAM modifiers) noexcept
        {
            const auto view = owner->_viewport.ViewFromDocument(document, owner->DocumentSize());
            const auto hole = owner->_chrome->HolePixels();
            SendMessageW(owner->_window.get(), message, modifiers,
                         MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                    static_cast<int>(std::lround(hole.top + view.y))));
        };
        const auto generation = owner->_session.Generation();
        const auto parents = owner->_polygonalPressParents;
        SetKeys(true, false, false);
        send(WM_LBUTTONDOWN, {1.5, 1.5}, MK_LBUTTON | MK_CONTROL);
        const auto firstDown = owner->_polygonalPressLastReceipt;
        if (!expect(pumpUntil(
                        [&]() noexcept
                        {
                            return owner->_polygonalPressState && owner->_polygonalPressState->pixelMove &&
                                   owner->_polygonalPressState->opened && owner->_polygonalPressState->waiting;
                        }),
                    "first-original-lift-opens-before-queued-successor"))
            return 5;
        const auto firstTicket = owner->_polygonalPressState->original;
        owner->_polygonalPressState->workObserver = &gate;
        owner->_polygonalPressState->workCheckpoint =
            [](void* raw, SelectionOutlineGestureState::WorkStage stage) noexcept
        {
            if (stage != SelectionOutlineGestureState::WorkStage::PixelClosurePrepared)
                return;
            auto& selected = *static_cast<ClosureGate*>(raw);
            selected.visits.fetch_add(1);
            if (GetCurrentThreadId() == selected.ownerThread || !SetEvent(selected.reached.get()) ||
                WaitForSingleObject(selected.release.get(), 15000) != WAIT_OBJECT_0)
                selected.failed.store(true);
        };
        send(WM_MOUSEMOVE, {3.5, 2.5}, MK_LBUTTON | MK_CONTROL);
        send(WM_LBUTTONUP, {3.5, 2.5}, MK_CONTROL);
        const auto firstUp = owner->_polygonalPressLastReceipt;
        if (!expect(WaitForColorWorkTestEvent(owner->_jobs, gate.reached.get(), DispatchMessageThunk, owner.get()) &&
                        firstDown.Read().outcome == HostOutcome::Pending &&
                        firstUp.Read().outcome == HostOutcome::Pending && owner->_session.Generation() == generation &&
                        owner->_session.history.UndoCount() == 0 &&
                        MatchesCpuPixels(owner->_session.Document().value(), SourcePixels()),
                    "held-first-closure-has-no-early-pixel-history-or-receipt-publication"))
            return 5;
        send(WM_LBUTTONDOWN, {3.5, 2.5}, MK_LBUTTON | MK_CONTROL);
        const auto secondDown = owner->_polygonalPressLastReceipt;
        send(WM_MOUSEMOVE, {1.5, 1.5}, MK_LBUTTON | MK_CONTROL);
        send(WM_LBUTTONUP, {1.5, 1.5}, MK_CONTROL);
        const auto secondUp = owner->_polygonalPressLastReceipt;
        if (!expect(
                secondDown.Read().outcome == HostOutcome::Pending && secondUp.Read().outcome == HostOutcome::Pending &&
                    owner->_polygonalPressParents == parents + 1 && owner->_polygonalPressInput.PendingCount() >= 3 &&
                    owner->_session.Generation() == generation && !owner->_pixelMovePreviewTicket,
                "younger-physical-prefix-is-owned-before-first-source-generation-changes"))
            return 5;
        SetKeys(false, false, false);
        (void)SetEvent(gate.release.get());
        if (!expect(settle() && gate.visits.load() == 1 && !gate.failed.load() &&
                        firstDown.Read().outcome == HostOutcome::Succeeded &&
                        firstUp.Read().outcome == HostOutcome::Succeeded &&
                        secondDown.Read().outcome == HostOutcome::Succeeded &&
                        secondUp.Read().outcome == HostOutcome::Succeeded,
                    "both-originals-and-all-conditional-receipts-acknowledge-in-order"))
            return 5;
        auto expected = SourcePixels();
        std::fill_n(expected.begin() + (2 * kWidth + 3) * 4, 4, std::uint8_t{});
        if (!expect(owner->_polygonalPressParents == parents + 2 && owner->_polygonalPressState &&
                        owner->_polygonalPressState->original != firstTicket && owner->_polygonalPressState->closed &&
                        owner->_polygonalPressState->pixelMove && owner->_polygonalPressState->captures == 1 &&
                        owner->_polygonalPressState->hits == 1 && !owner->_polygonalPressState->controller &&
                        !owner->_polygonalPressInput.Busy() && owner->_session.history.UndoCount() == 2 &&
                        MatchesCpuPixels(owner->_session.Document().value(), expected) &&
                        MatchesLiteral(owner->_acknowledgedCommittedDisplay, expected),
                    "typed-pixel-lineage-selects-new-original-source-and-independent-final-CPU-WARP-bytes"))
            return 5;
        joinOnFailure.release(); // Every worker/recorder/wait borrower has actually joined above.
    }

    scenario = "close";
    owner->Shutdown();
    if (!expect(owner->_jobs.IsIdle(), "joined-shutdown-has-no-active-jobs"))
        return 5;
    owner.reset();
    while (PumpMessages().quit)
    {
    }

    // Reach the real private final document/rows after the lift, but hold its actual worker before
    // acknowledgement. Cancel preserves source; Close unblocks only through its actual shutdown event.
    for (unsigned boundary = 0; boundary < 14; ++boundary)
    {
        const bool waitSlot = boundary >= 8;
        const auto slotAction = waitSlot ? (boundary - 8) % 3 : 0;
        const bool busyResidency = boundary == 6 || boundary == 7;
        const bool sourceFailure = boundary == 4 || boundary == 5;
        const bool polygonal = waitSlot ? boundary >= 11 : (boundary >= 4 ? (boundary & 1u) != 0 : boundary >= 2);
        const bool closing = waitSlot ? slotAction == 2 : boundary < 4 && (boundary & 1u) != 0;
        constexpr std::array boundaryNames{"final-prepared-cancel",
                                           "final-prepared-close",
                                           "polygonal-final-prepared-cancel",
                                           "polygonal-final-prepared-close",
                                           "final-source-failure",
                                           "polygonal-final-source-failure",
                                           "final-residency-busy",
                                           "polygonal-final-residency-busy",
                                           "final-slot-resume",
                                           "final-slot-cancel",
                                           "final-slot-close",
                                           "polygonal-final-slot-resume",
                                           "polygonal-final-slot-cancel",
                                           "polygonal-final-slot-close"};
        scenario = boundaryNames[boundary];
        struct FinalGate
        {
            wil::unique_handle reached{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            const DWORD ownerThread = GetCurrentThreadId();
            std::atomic<unsigned> visits{0};
            std::atomic<DWORD> waited{WAIT_FAILED};
            std::atomic<bool> wrongThread{false};
        } gate;
        ApplicationProcessingTestProbe finalProbe;
        finalProbe.workerCount = 2;
        bool refuseSourceMemory = false;
        finalProbe.memory = {[](void* raw, MemorySnapshot& snapshot) noexcept
                             {
                                 const auto available = *static_cast<bool*>(raw) ? 0ULL : 8ULL << 30;
                                 snapshot = {available, available};
                                 return true;
                             },
                             &refuseSourceMemory};
        auto finalOwner =
            std::unique_ptr<Application>(new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr));
        auto joinFinal = wil::scope_exit(
            [&]() noexcept
            {
                if (finalOwner)
                {
                    (void)SetEvent(gate.release.get()); // Failed fixture predicates never strand the closure worker.
                    finalOwner->Shutdown();
                    finalOwner.reset();
                    while (PumpMessages().quit)
                    {
                    }
                }
            });
        if (!expect(finalOwner && gate.reached && gate.release, "owner-and-worker-gate-created"))
            return 5;
        auto& app = *finalOwner;
        app._processingTestProbe = &finalProbe;
        app._quietUi = true;
        app._prompts = &QuietPrompts();
        if (!expect(SUCCEEDED(app.InitializeProcessing()) && SUCCEEDED(app.RegisterWindowClass()) &&
                        SUCCEEDED(app.CreateMainWindow(false)) && SUCCEEDED(app.InitializeRenderer()) &&
                        SUCCEEDED(app.InitializeChrome()) && app.EnableCommittedDisplayForTest() &&
                        app.EnableSelectionOutlineForTest() && app.EnablePreparedLayerRows() &&
                        app._strokeFinishSource.Start(),
                    "real-hidden-WARP-display-and-row-owners-enabled"))
            return 5;
        app._selectionPixelFinalPublicationEnabled = true;
        app._selectionPressTestEnabled = true;
        app._polygonalPressTestEnabled = app._polygonalPixelPressEnabled = polygonal;
        auto fixture = PixelFixture();
        if (!expect(fixture.has_value(), "independent-source-created"))
            return 5;
        const auto active = fixture->layers[0].id;
        app._session.ReplaceDocument(std::move(fixture.value()), active);
        app._session.history.Reset();
        app._tool = polygonal ? EditorTool::Lasso : EditorTool::Marquee;
        app._session.lassoKind = LassoKind::Polygonal;
        app._session.marqueeKind = LassoKind::Rectangle;
        app._session.selectionModeChoice = SelectionMode::New;
        app._session.selectionAntialiased = false;
        app._viewport.followsFit = false;
        app._viewport.zoom = 8;
        app.DocumentChanged();
        const auto finalSettle = [&]() noexcept
        { return SettleJobs(app._jobs, 30000, DispatchMessageThunk, &app) == SettleResult::Settled; };
        if (!expect(finalSettle(), "source-display-and-rows-settled"))
            return 5;
        app.RenderFrame();
        const auto generation = app._session.Generation();
        const auto sourceRows = app._preparedLayerRows.get();
        const auto sendFinal = [&](UINT message, Vec2 document, WPARAM modifiers) noexcept
        {
            const auto view = app._viewport.ViewFromDocument(document, app.DocumentSize());
            const auto hole = app._chrome->HolePixels();
            SendMessageW(app._window.get(), message, modifiers,
                         MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                    static_cast<int>(std::lround(hole.top + view.y))));
        };
        SetKeys(true, false, false);
        sendFinal(WM_LBUTTONDOWN, {1.5, 1.5}, MK_LBUTTON | MK_CONTROL);
        // A move acknowledgement supplies actual owner readiness before installing the next-stage gate.
        sendFinal(WM_MOUSEMOVE, {3.5, 2.5}, MK_LBUTTON | MK_CONTROL);
        const auto finalUntil = [&](auto done) noexcept
        {
            const auto deadline = GetTickCount64() + 15000;
            while (!done())
            {
                if (PumpMessages(DispatchMessageThunk, &app).quit)
                    return false;
                (void)DrainCompletions(app._jobs);
                (void)RunUiActions(app._jobs);
                if (done())
                    return true;
                const auto now = GetTickCount64();
                if (now >= deadline)
                    return false;
                const HANDLE wake = app._jobs.WakeHandle();
                if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    return false;
            }
            return true;
        };
        auto& stateSlot = polygonal ? app._polygonalPressState : app._selectionPressState;
        if (!expect(
                finalUntil([&]() noexcept
                           { return stateSlot && stateSlot->opened && stateSlot->waiting && stateSlot->moves == 1; }),
                "same-original-lift-and-offset-acknowledged"))
            return 5;
        auto& state = stateSlot.value();
        const auto original = state.original;
        state.workObserver = &gate;
        state.pixelFinalPreparedForTest = [](void* raw, void* shutdownEvent) noexcept
        {
            auto& value = *static_cast<FinalGate*>(raw);
            value.visits.fetch_add(1);
            value.wrongThread.store(GetCurrentThreadId() == value.ownerThread || !shutdownEvent);
            const std::array<HANDLE, 2> handles{value.release.get(), static_cast<HANDLE>(shutdownEvent)};
            if (!SetEvent(value.reached.get()))
                value.wrongThread.store(true);
            value.waited.store(
                WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, 30000));
        };
        if (sourceFailure || waitSlot)
        {
            state.pixelFinalPreparedForTest = nullptr;
            state.workCheckpoint = [](void* raw, SelectionOutlineGestureState::WorkStage stage) noexcept
            {
                if (stage != SelectionOutlineGestureState::WorkStage::PixelClosurePrepared)
                    return;
                auto& value = *static_cast<FinalGate*>(raw);
                value.visits.fetch_add(1);
                value.wrongThread.store(GetCurrentThreadId() == value.ownerThread);
                if (!SetEvent(value.reached.get()))
                    value.wrongThread.store(true);
                value.waited.store(WaitForSingleObject(value.release.get(), 30000));
            };
        }
        sendFinal(WM_LBUTTONUP, {3.5, 2.5}, MK_CONTROL);
        SetKeys(false, false, false);
        const auto receipt = polygonal ? app._polygonalPressLastReceipt : app._selectionPressReceipt;
        if (!expect(WaitForColorWorkTestEvent(app._jobs, gate.reached.get(), DispatchMessageThunk, &app) &&
                        gate.visits.load() == 1 && !gate.wrongThread.load() &&
                        receipt.Read().outcome == HostOutcome::Pending && state.original == original &&
                        !state.completed && !state.closed && app._session.Generation() == generation &&
                        app._session.history.UndoCount() == 0 && app._preparedLayerRows.get() == sourceRows &&
                        MatchesCpuPixels(app._session.Document().value(), SourcePixels()) &&
                        MatchesLiteral(app._acknowledgedCommittedDisplay, SourcePixels()),
                    "held-private-final-keeps-original-CPU-history-rows-and-GPU-unpublished"))
            return 5;
        if (waitSlot)
        {
            // Exercise the controller's actual visibility transition and real cache retirement.
            // Holding the CPU closure first keeps the original from claiming a publication cell.
            app._committedDisplay.SetSuspendedOwner(true);
            const bool retiredSource = finalUntil([&]() noexcept { return !app._committedDisplay.HasPending(); });
            (void)SetEvent(gate.release.get());
            if (!expect(retiredSource && !app._committedDisplay.GraphForFrameOwner(),
                        "actual-suspension-retires-source-before-original-final-capture"))
                return 5;
            if (!expect(finalUntil([&]() noexcept { return state.pixelFinalWaitingForDisplay; }) &&
                            !app.FinalPublicationReadyOwner() && !state.completed && state.original == original &&
                            receipt.Read().outcome == HostOutcome::Pending && app._session.Generation() == generation &&
                            app._session.history.UndoCount() == 0 && app._preparedLayerRows.get() == sourceRows &&
                            MatchesCpuPixels(app._session.Document().value(), SourcePixels()),
                        "same-original-parks-without-capture-publication-or-ready-spin"))
                return 5;
            if (closing)
            {
                app.Shutdown();
                if (!expect(app._jobs.IsIdle() && !state.completed && !state.controller && !state.closed &&
                                !state.pixelFinalWaitingForDisplay && state.inputAcknowledged &&
                                receipt.Read().outcome == HostOutcome::Cancelled && !app._finalPublicationCurrent &&
                                !app._finalPublicationShutdownGpu && app._jobs.ReservedMemoryBytes() == 0,
                            "actual-joined-close-while-slot-waits-refunds-original-without-useful-completion"))
                    return 5;
            }
            else
            {
                if (slotAction == 1)
                {
                    if (!expect(app._jobs.Cancel(original), "cancel-addresses-same-original-while-slot-waits"))
                        return 5;
                    if (!expect(finalSettle() && state.completed && !state.controller && state.aborted &&
                                    !state.closed && !state.pixelFinalWaitingForDisplay && state.inputAcknowledged &&
                                    state.outcome.status == JobStatus::Cancelled &&
                                    receipt.Read().outcome == HostOutcome::Cancelled && !app._finalPublicationCurrent &&
                                    app._session.Generation() == generation && app._session.history.UndoCount() == 0 &&
                                    app._preparedLayerRows.get() == sourceRows &&
                                    MatchesCpuPixels(app._session.Document().value(), SourcePixels()),
                                "cancelled-slot-wait-preserves-CPU-history-and-row-source"))
                        return 5;
                }
                else
                {
                    app._committedDisplay.SetSuspendedOwner(false);
                    if (!expect(finalSettle() && state.completed && state.closed && state.original == original &&
                                    !state.pixelFinalWaitingForDisplay && state.inputAcknowledged &&
                                    receipt.Read().outcome == HostOutcome::Succeeded &&
                                    app._session.history.UndoCount() == 1 &&
                                    MatchesCpuPixels(app._session.Document().value(), MovedPixels(false, true)) &&
                                    MatchesLiteral(app._acknowledgedCommittedDisplay, MovedPixels(false, true)),
                                "real-visibility-resume-publishes-same-original-one-history-and-CPU-GPU-bytes"))
                        return 5;
                }
                app.Shutdown();
                if (!expect(app._jobs.IsIdle() && app._jobs.ReservedMemoryBytes() == 0,
                            "joined-close-after-slot-wait-acknowledges-all-retirement"))
                    return 5;
            }
        }
        else if (busyResidency)
        {
            struct BusyGate
            {
                wil::unique_handle acquired{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
                wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
                wil::unique_handle refresh{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
                std::atomic<bool> ownsLease{false};
            } busy;
            if (!busy.acquired || !busy.release || !busy.refresh)
                return 5;
            std::thread holder(
                [&]() noexcept
                {
                    const auto lease = Raster::AcquireResidencyLease();
                    busy.ownsLease.store(lease.Held());
                    (void)SetEvent(busy.acquired.get());
                    (void)WaitForSingleObject(busy.release.get(), 30000);
                });
            const auto joinBusy = wil::scope_exit(
                [&]() noexcept
                {
                    (void)SetEvent(busy.release.get());
                    holder.join();
                    if (!state.completed)
                        app.Shutdown(); // No borrowed BusyGate may die before the original worker actually joins.
                });
            if (!expect(WaitForColorWorkTestEvent(app._jobs, busy.acquired.get(), DispatchMessageThunk, &app) &&
                            busy.ownsLease.load(),
                        "independent-worker-holds-real-global-residency-lease"))
                return 5;
            state.workObserver = &busy;
            state.workCheckpoint = [](void* raw, SelectionOutlineGestureState::WorkStage stage) noexcept
            {
                if (stage == SelectionOutlineGestureState::WorkStage::ResidencyRefreshStarted)
                    (void)SetEvent(static_cast<BusyGate*>(raw)->refresh.get());
            };
            (void)SetEvent(gate.release.get());
            if (!expect(WaitForColorWorkTestEvent(app._jobs, busy.refresh.get(), DispatchMessageThunk, &app) &&
                            state.original == original && !state.completed && !state.closed &&
                            receipt.Read().outcome == HostOutcome::Pending && app._session.Generation() == generation &&
                            app._session.history.UndoCount() == 0 && app._preparedLayerRows.get() == sourceRows &&
                            MatchesCpuPixels(app._session.Document().value(), SourcePixels()),
                        "busy-permit-resumes-original-worker-refresh-without-owner-wait-or-early-publication"))
                return 5;
            (void)SetEvent(busy.release.get());
            if (!expect(finalSettle() && state.completed && state.closed && state.original == original &&
                            state.residencyRefreshes != 0 && state.residencyTries >= 2 &&
                            receipt.Read().outcome == HostOutcome::Succeeded && app._session.history.UndoCount() == 1 &&
                            MatchesCpuPixels(app._session.Document().value(), MovedPixels(false, true)) &&
                            MatchesLiteral(app._acknowledgedCommittedDisplay, MovedPixels(false, true)),
                        "released-lease-publishes-same-original-once-with-independent-CPU-GPU-bytes"))
                return 5;
            app.Shutdown();
            if (!expect(app._jobs.IsIdle() && app._jobs.ReservedMemoryBytes() == 0,
                        "joined-close-after-residency-contention-refunds-all-owners"))
                return 5;
        }
        else if (sourceFailure)
        {
            refuseSourceMemory = true; // The original was completely admitted before this snapshot changes.
            const auto oldDevice = app._renderer.DeviceGeneration();
            const auto recovered = app._renderer.RecoverDeviceForTest();
            app.BindCompositionDevice();
            const bool failedSource = finalUntil(
                [&]() noexcept
                { return app._committedDisplayFailure == E_OUTOFMEMORY && !app._committedDisplay.HasPending(); });
            // Release even on a failed predicate, so cleanup never relies on this fixture's timeout.
            (void)SetEvent(gate.release.get());
            if (!expect(SUCCEEDED(recovered) && app._renderer.DeviceGeneration() != oldDevice && failedSource &&
                            !app._committedDisplay.HasCurrentPublicationOwner() &&
                            app._session.Generation() == generation && app._session.history.UndoCount() == 0 &&
                            MatchesCpuPixels(app._session.Document().value(), SourcePixels()),
                        "real-device-rebuild-refuses-current-source-after-original-admission"))
                return 5;
            if (!expect(finalSettle() && gate.waited.load() == WAIT_OBJECT_0 && state.completed && !state.controller &&
                            !state.closed && state.aborted && state.inputAcknowledged &&
                            state.outcome.status == JobStatus::Failed && state.outcome.error == E_OUTOFMEMORY &&
                            receipt.Read().outcome == HostOutcome::Failed && !app._finalPublicationCurrent &&
                            app._session.Generation() == generation && app._session.history.UndoCount() == 0 &&
                            app._preparedLayerRows.get() == sourceRows &&
                            MatchesCpuPixels(app._session.Document().value(), SourcePixels()),
                        "failed-source-terminates-original-with-exact-error-and-no-CPU-history-or-row-publication"))
                return 5;
            // A failure belongs to its exact source/device, including while the next source is hidden.
            const auto failureKey = app._committedDisplayFailureKey;
            app._committedDisplay.SetSuspendedOwner(true);
            refuseSourceMemory = false;
            const auto failedDevice = app._renderer.DeviceGeneration();
            const auto secondRecovery = app._renderer.RecoverDeviceForTest();
            app.BindCompositionDevice();
            if (!expect(SUCCEEDED(secondRecovery) && app._renderer.DeviceGeneration() != failedDevice && failureKey &&
                            app._committedDisplayFailureKey == failureKey &&
                            app._committedDisplayFailure == E_OUTOFMEMORY &&
                            SUCCEEDED(app.CurrentCommittedDisplayFailureOwner()) &&
                            !app._committedDisplay.HasCurrentPublicationOwner() && !app._committedDisplay.HasPending(),
                        "old-source-device-error-is-ineligible-for-hidden-replacement-source"))
                return 5;
            app._committedDisplay.SetSuspendedOwner(false);
            app._committedDisplay.RetryOwner();
            app._pendingDisplay.RetryOwner();
            if (!expect(finalSettle() && app._committedDisplay.HasCurrentPublicationOwner() &&
                            MatchesLiteral(app._acknowledgedCommittedDisplay, SourcePixels()),
                        "restored-memory-and-explicit-retry-rebuild-current-source"))
                return 5;
            SetKeys(true, false, false);
            sendFinal(WM_LBUTTONDOWN, {1.5, 1.5}, MK_LBUTTON | MK_CONTROL);
            sendFinal(WM_MOUSEMOVE, {3.5, 2.5}, MK_LBUTTON | MK_CONTROL);
            sendFinal(WM_LBUTTONUP, {3.5, 2.5}, MK_CONTROL);
            SetKeys(false, false, false);
            const auto retryReceipt = polygonal ? app._polygonalPressLastReceipt : app._selectionPressReceipt;
            if (!expect(finalSettle() && retryReceipt.Read().outcome == HostOutcome::Succeeded && stateSlot &&
                            stateSlot->original != original && stateSlot->closed &&
                            app._session.history.UndoCount() == 1 &&
                            MatchesCpuPixels(app._session.Document().value(), MovedPixels(false, true)) &&
                            MatchesLiteral(app._acknowledgedCommittedDisplay, MovedPixels(false, true)),
                        "fresh-original-after-retry-publishes-one-history-and-independent-CPU-GPU-bytes"))
                return 5;
            app.Shutdown();
            if (!expect(app._jobs.IsIdle() && app._jobs.ReservedMemoryBytes() == 0,
                        "joined-close-after-source-failure-and-retry-refunds-all-owners"))
                return 5;
        }
        else if (closing)
        {
            app.Shutdown(); // No release signal: only this actual Close can wake the held worker.
            if (!expect(gate.waited.load() == WAIT_OBJECT_0 + 1 && app._jobs.IsIdle() && !state.completed &&
                            !state.controller && !state.closed && state.inputAcknowledged &&
                            receipt.Read().outcome == HostOutcome::Cancelled && !app._finalPublicationCurrent &&
                            !app._finalPublicationShutdownGpu && app._jobs.ReservedMemoryBytes() == 0,
                        "actual-joined-close-retires-nested-original-without-useful-publication-or-charge"))
                return 5;
        }
        else
        {
            if (!expect(app._jobs.Cancel(original), "cancel-addresses-original-before-worker-acknowledgement"))
                return 5;
            (void)SetEvent(gate.release.get());
            if (!expect(finalSettle() && gate.waited.load() == WAIT_OBJECT_0 && state.completed && !state.controller &&
                            !state.closed && state.aborted && state.inputAcknowledged &&
                            state.outcome.status == JobStatus::Cancelled &&
                            receipt.Read().outcome == HostOutcome::Cancelled && !app._finalPublicationCurrent &&
                            app._session.Generation() == generation && app._session.history.UndoCount() == 0 &&
                            app._preparedLayerRows.get() == sourceRows &&
                            MatchesCpuPixels(app._session.Document().value(), SourcePixels()) &&
                            MatchesLiteral(app._acknowledgedCommittedDisplay, SourcePixels()),
                        "cancelled-final-worker-refuses-fallback-and-preserves-literal-committed-state"))
                return 5;
            app.Shutdown();
            if (!expect(app._jobs.IsIdle() && app._jobs.ReservedMemoryBytes() == 0,
                        "subsequent-joined-close-refunds-all-original-and-source-ownership"))
                return 5;
        }
    }

    // A real first-row worker can coexist with the foreground gesture on the two-worker host.
    // The final edit must retain its original ticket while rows are unavailable; their first
    // semantic item ID must survive into final CPU/history/rows/GPU publication.
    for (unsigned boundary = 0; boundary < 8; ++boundary)
    {
        const bool polygonal = boundary >= 4;
        const unsigned action = boundary % 4; // Resume, Cancel, joined Close, current-row failure.
        constexpr std::array rowNames{
            "first-rows-resume",          "first-rows-cancel",           "first-rows-close",
            "first-rows-failure",         "polygonal-first-rows-resume", "polygonal-first-rows-cancel",
            "polygonal-first-rows-close", "polygonal-first-rows-failure"};
        scenario = rowNames[boundary];
        auto rowsGate = std::make_shared<LayerRowsTestGate>();
        rowsGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        rowsGate->hold = action != 3;
        rowsGate->throwPrepare = action == 3;
        ApplicationProcessingTestProbe rowProbe;
        rowProbe.workerCount = 2;
        rowProbe.memory = {[](void*, MemorySnapshot& snapshot) noexcept
                           {
                               snapshot = {8ULL << 30, 8ULL << 30};
                               return true;
                           },
                           nullptr};
        auto rowsOwner =
            std::unique_ptr<Application>(new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr));
        const auto joinRows = wil::scope_exit(
            [&]() noexcept
            {
                (void)SetEvent(rowsGate->release.get());
                if (rowsOwner)
                    rowsOwner->Shutdown();
                while (PumpMessages().quit)
                {
                }
            });
        if (!expect(rowsOwner && rowsGate->release, "actual-owner-and-first-row-gate-created"))
            return 5;
        auto& app = *rowsOwner;
        app._processingTestProbe = &rowProbe;
        app._quietUi = true;
        app._prompts = &QuietPrompts();
        if (!expect(SUCCEEDED(app.InitializeProcessing()) && SUCCEEDED(app.RegisterWindowClass()) &&
                        SUCCEEDED(app.CreateMainWindow(false)) && SUCCEEDED(app.InitializeRenderer()) &&
                        SUCCEEDED(app.InitializeChrome()) && app.EnableCommittedDisplayForTest() &&
                        app.EnableSelectionOutlineForTest() && app.EnablePreparedLayerRows() &&
                        app._strokeFinishSource.Start(),
                    "real-two-worker-WARP-owner-enabled"))
            return 5;
        app._selectionPixelFinalPublicationEnabled = true;
        app._selectionPressTestEnabled = true;
        app._polygonalPressTestEnabled = app._polygonalPixelPressEnabled = polygonal;
        app._layerRowsTestGate = rowsGate;
        auto fixture = PixelFixture();
        if (!expect(fixture.has_value(), "independent-source-created"))
            return 5;
        const auto active = fixture->layers[0].id;
        app._session.ReplaceDocument(std::move(fixture.value()), active);
        app._session.history.Reset();
        app._tool = polygonal ? EditorTool::Lasso : EditorTool::Marquee;
        app._session.lassoKind = LassoKind::Polygonal;
        app._session.marqueeKind = LassoKind::Rectangle;
        app._session.selectionAntialiased = false;
        app._session.selectionModeChoice = SelectionMode::New;
        app._viewport.followsFit = false;
        app._viewport.zoom = 8;
        const auto firstItemId = app._nextLayerRowItemId;
        app.DocumentChanged();
        const auto until = [&](auto done) noexcept
        {
            const auto deadline = GetTickCount64() + 15000;
            while (!done())
            {
                if (PumpMessages(DispatchMessageThunk, &app).quit)
                    return false;
                (void)DrainCompletions(app._jobs);
                (void)RunUiActions(app._jobs);
                if (app._frameInvalidated)
                    app.RenderFrame();
                if (done())
                    return true;
                const auto now = GetTickCount64();
                if (now >= deadline)
                    return false;
                if (app._jobs.HasUiWork())
                    continue;
                const HANDLE wake = app._jobs.WakeHandle();
                if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    return false;
            }
            return true;
        };
        if (!expect(until(
                        [&]() noexcept
                        {
                            return rowsGate->preparations.load() == 1 &&
                                   app._committedDisplay.HasCurrentPublicationOwner() &&
                                   !app._committedDisplay.HasPending() && (action != 3 || app._layerRowsRetryAvailable);
                        }) &&
                        !app._preparedLayerRows && rowsGate->publications.load() == 0 &&
                        MatchesLiteral(app._acknowledgedCommittedDisplay, SourcePixels()),
                    "actual-source-is-ready-before-first-row-publication"))
            return 5;
        const auto sourceRowsTicket = app._layerRowsTicket;
        const auto generation = app._session.Generation();
        const auto send = [&](UINT message, Vec2 document, WPARAM modifiers) noexcept
        {
            const auto view = app._viewport.ViewFromDocument(document, app.DocumentSize());
            const auto hole = app._chrome->HolePixels();
            SendMessageW(app._window.get(), message, modifiers,
                         MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                    static_cast<int>(std::lround(hole.top + view.y))));
        };
        SetKeys(true, false, false);
        send(WM_LBUTTONDOWN, {1.5, 1.5}, MK_LBUTTON | MK_CONTROL);
        send(WM_MOUSEMOVE, {3.5, 2.5}, MK_LBUTTON | MK_CONTROL);
        send(WM_LBUTTONUP, {3.5, 2.5}, MK_CONTROL);
        SetKeys(false, false, false);
        auto& stateSlot = polygonal ? app._polygonalPressState : app._selectionPressState;
        const auto receipt = polygonal ? app._polygonalPressLastReceipt : app._selectionPressReceipt;
        if (!expect(until(
                        [&]() noexcept
                        {
                            return stateSlot &&
                                   (action == 3 ? stateSlot->completed : stateSlot->pixelFinalWaitingForDisplay);
                        }),
                    "accepted-original-reaches-actual-first-row-boundary"))
            return 5;
        auto& state = stateSlot.value();
        const auto original = state.original;
        if (action == 3)
        {
            if (!expect(state.completed && !state.closed && state.aborted && state.inputAcknowledged &&
                            state.outcome.status == JobStatus::Failed && state.outcome.error == E_FAIL &&
                            receipt.Read().outcome == HostOutcome::Failed && !app._finalPublicationCurrent &&
                            !state.pixelFinalWaitingForDisplay && app._layerRowsRetryAvailable &&
                            !app._preparedLayerRows && app._session.Generation() == generation &&
                            app._session.history.UndoCount() == 0 &&
                            MatchesCpuPixels(app._session.Document().value(), SourcePixels()) &&
                            MatchesLiteral(app._acknowledgedCommittedDisplay, SourcePixels()),
                        "current-row-failure-terminates-original-without-publishing-edit"))
                return 5;
            app._layerRowsTestGate.reset();
            app.RetryPreparedLayerRows();
            if (!expect(SettleJobs(app._jobs, 30000, DispatchMessageThunk, &app) == SettleResult::Settled &&
                            app._preparedLayerRows && !app._layerRowsRetryAvailable &&
                            app._session.Generation() == generation && app._session.history.UndoCount() == 0 &&
                            receipt.Read().outcome == HostOutcome::Failed,
                        "explicit-row-retry-restores-source-without-reviving-failed-edit"))
                return 5;
        }
        else
        {
            if (!expect(sourceRowsTicket && app._layerRowsTicket == sourceRowsTicket &&
                            !app.FinalPublicationReadyOwner() && !state.completed && !state.closed &&
                            receipt.Read().outcome == HostOutcome::Pending && !app._preparedLayerRows &&
                            app._session.Generation() == generation && app._session.history.UndoCount() == 0 &&
                            MatchesCpuPixels(app._session.Document().value(), SourcePixels()) &&
                            MatchesLiteral(app._acknowledgedCommittedDisplay, SourcePixels()),
                        "same-original-waits-without-ready-spin-or-CPU-history-rows-GPU-publication"))
                return 5;
            if (action == 2)
            {
                app.Shutdown();
                if (!expect(app._jobs.IsIdle() && !state.completed && !state.controller && !state.closed &&
                                !state.pixelFinalWaitingForDisplay && state.inputAcknowledged &&
                                receipt.Read().outcome == HostOutcome::Cancelled && !app._finalPublicationCurrent &&
                                app._jobs.ReservedMemoryBytes() == 0 && rowsGate->retired.load() &&
                                rowsGate->retiredOffOwner.load(),
                            "joined-close-retires-both-originals-without-useful-publication"))
                    return 5;
            }
            else if (action == 1)
            {
                if (!expect(app._jobs.Cancel(original) && until([&]() noexcept { return state.completed; }) &&
                                !state.closed && state.aborted && state.inputAcknowledged &&
                                state.outcome.status == JobStatus::Cancelled && !state.pixelFinalWaitingForDisplay &&
                                receipt.Read().outcome == HostOutcome::Cancelled && !app._finalPublicationCurrent &&
                                app._layerRowsTicket == sourceRowsTicket && !app._preparedLayerRows &&
                                app._session.Generation() == generation && app._session.history.UndoCount() == 0 &&
                                MatchesCpuPixels(app._session.Document().value(), SourcePixels()),
                            "cancel-wakes-same-original-before-row-worker-joins-and-preserves-source"))
                    return 5;
                (void)SetEvent(rowsGate->release.get());
                if (!expect(SettleJobs(app._jobs, 30000, DispatchMessageThunk, &app) == SettleResult::Settled &&
                                app._preparedLayerRows && rowsGate->publications.load() == 1 &&
                                app._preparedLayerRows->ItemIdAt(0) == firstItemId &&
                                app._session.Generation() == generation && app._session.history.UndoCount() == 0,
                            "independent-row-producer-can-finish-after-original-cancellation"))
                    return 5;
            }
            else
            {
                (void)SetEvent(rowsGate->release.get());
                if (!expect(SettleJobs(app._jobs, 30000, DispatchMessageThunk, &app) == SettleResult::Settled &&
                                state.completed && state.closed && state.original == original &&
                                state.inputAcknowledged && !state.pixelFinalWaitingForDisplay &&
                                receipt.Read().outcome == HostOutcome::Succeeded &&
                                rowsGate->publications.load() == 1 && app._preparedLayerRows &&
                                app._preparedLayerRows->ItemIdAt(0) == firstItemId &&
                                app._layerRowsCacheTicket == std::optional(original) &&
                                app._session.history.UndoCount() == 1 &&
                                MatchesCpuPixels(app._session.Document().value(), MovedPixels(false, true)) &&
                                MatchesLiteral(app._acknowledgedCommittedDisplay, MovedPixels(false, true)),
                            "actual-first-row-resume-keeps-item-ID-and-publishes-same-original-CPU-history-rows-GPU"))
                    return 5;
            }
        }
        app.Shutdown();
        if (!expect(app._jobs.IsIdle() && app._jobs.ReservedMemoryBytes() == 0,
                    "joined-shutdown-acknowledges-all-original-and-row-retirement"))
            return 5;
    }

    return failures ? 5 : 0;
}
