#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "KeyboardState.h"
#include "PixelMovePreviewWork.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <span>
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
    owner->_processingTestProbe = &probe;
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                    SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                    SUCCEEDED(owner->InitializeChrome()) && owner->EnableCommittedDisplayForTest() &&
                    owner->EnableSelectionOutlineForTest(),
                "hidden-WARP-application-WndProc-and-display-owners-enabled"))
        return 5;
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
            return 5;
        if (!expect(MatchesLiteral(owner->_acknowledgedCommittedDisplay, expected),
                    "after-up-committed-WARP-display-matches-independent-literal"))
            return 5;
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
    return failures ? 5 : 0;
}
