#include "Application.h"
#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"
#include "KeyboardState.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
bool DispatcherFilled(const DocumentSelection& selection, Vec2 point) noexcept
{
    int winding = 0;
    for (const auto& loop : selection.loops.Get())
    {
        if (loop.empty())
            continue;
        auto a = loop.back();
        for (const auto b : loop)
        {
            const double cross = (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
            if (a.y <= point.y && b.y > point.y && cross > 0)
                ++winding;
            else if (a.y > point.y && b.y <= point.y && cross < 0)
                --winding;
            a = b;
        }
    }
    return winding != 0;
}
bool DispatcherPixels(const DocumentSession& session, SelectionMode mode, bool moved = false, int left = 1, int top = 1,
                      int right = 5, int bottom = 5) noexcept
{
    if (!session.Selection())
        return false;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            const bool prior = x >= 1 && x < 5 && y >= 1 && y < 5;
            const bool shape = x >= (mode == SelectionMode::Subtract ? 3 : 7) &&
                               x < (mode == SelectionMode::Subtract ? 7 : 11) && y >= 1 && y < 5;
            const bool wanted = moved                        ? x >= left && x < right && y >= top && y < bottom
                                : mode == SelectionMode::New ? shape
                                : mode == SelectionMode::Add ? prior || shape
                                                             : prior && !shape;
            if (DispatcherFilled(*session.Selection(), {x + 0.5, y + 0.5}) != wanted)
                return false;
        }
    return true;
}
} // namespace

int Application::RunStandalonePolygonalDispatcherSelfTest(HINSTANCE instance) noexcept
{
    KeyboardState::ScopedSynthetic synthetic;
    std::array<BYTE, 256> savedKeys{};
    if (!GetKeyboardState(savedKeys.data()))
        return 5;
    auto restoreKeys = wil::scope_exit([&]() noexcept { (void)SetKeyboardState(savedKeys.data()); });
    unsigned checks = 0, failures = 0, reached = 0;
    const char* scenario = "setup";
    const auto expect = [&](bool value, const char* fact) noexcept
    {
        ++checks;
        failures += value ? 0 : 1;
        std::printf("%s polygonal-dispatch-host.%s.%s\n", value ? "PASS" : "FAIL", scenario, fact);
        return value;
    };
    auto summary = wil::scope_exit(
        [&]() noexcept
        {
            std::printf("SUMMARY polygonal-dispatch-host checks=%u failures=%u reached=%u\n", checks, failures,
                        reached);
        });
    constexpr std::array cases{"outside-foreign-history",
                               "inside-foreign-history",
                               "inside-save-before-up",
                               "inside-queued",
                               "new-enter",
                               "add-enter",
                               "subtract-enter",
                               "close-then-next",
                               "escape-before-controller",
                               "backspace-before-controller",
                               "hover-backspace",
                               "save-live",
                               "tool-live",
                               "leave-zoom-live",
                               "space-pan-live",
                               "right-ignore-first",
                               "family-counter-exhausted",
                               "record-counter-exhausted",
                               "absent-document",
                               "right-ignore-live",
                               "prefix-replaced-document",
                               "prefix-target-refusal",
                               "prefix-memory-refusal",
                               "host-descriptor-exhaustion",
                               "outside-prefix-overflow",
                               "inside-up-prefix-overflow",
                               "inside-missing-up-successor",
                               "joined-close"};
    const auto output =
        std::filesystem::current_path() / L".build" / L"P1PolygonalDispatcherFixture" / Guid::Generate().ToString();
    std::error_code directoryError;
    std::filesystem::create_directories(output, directoryError);
    if (!expect(!directoryError, "exclusive-output-directory"))
        return 5;
    for (const std::string_view name : cases)
    {
        scenario = name.data();
        std::atomic<bool> scarce{false};
        ApplicationProcessingTestProbe probe;
        probe.workerCount = 2;
        probe.memory = {[](void* raw, MemorySnapshot& value) noexcept
                        {
                            const auto bytes = static_cast<std::atomic<bool>*>(raw)->load(std::memory_order_relaxed)
                                                   ? 0
                                                   : kMemoryAdmissionHeadroomBytes + 2ull * 1024 * 1024 * 1024;
                            value = {bytes, bytes};
                            return true;
                        },
                        &scarce};
        auto owner = std::make_unique<Application>(instance, true, L"", 0, nullptr, nullptr);
        auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            });
        owner->_processingTestProbe = &probe;
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                        SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                        SUCCEEDED(owner->InitializeChrome()) && owner->EnableCommittedDisplayForTest() &&
                        owner->EnableSelectionOutlineForTest() && owner->EnableLassoDraftDisplayForTest(),
                    "actual-hidden-warp-window-and-prepared-owners"))
            return 5;
        owner->_polygonalPressTestEnabled = true;
        DocumentFixture fixture;
        fixture.width = fixture.height = 64;
        auto layer = ImageLayer::Blank(L"Physical Polygonal dispatcher", {64, 64});
        layer.asset = RasterAsset{Raster::MakeSparse(64, 64, RasterFormat::Bgra8888, {}, 0, 0, {}, 0), {}, layer.name};
        const auto active = layer.id;
        fixture.layers.push_back(std::move(layer));
        fixture.selection =
            DocumentSelection{false, SelectionLoops{std::vector<SelectionLoop>{{{1, 1}, {5, 1}, {5, 5}, {1, 5}}}}};
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
        owner->_session.history.Reset();
        owner->_tool = EditorTool::Lasso;
        owner->_session.lassoKind = LassoKind::Polygonal;
        const auto mode = name == "add-enter"        ? SelectionMode::Add
                          : name == "subtract-enter" ? SelectionMode::Subtract
                                                     : SelectionMode::New;
        owner->_session.selectionModeChoice = mode;
        owner->_session.selectionAntialiased = false;
        owner->_viewport.followsFit = false;
        owner->_viewport.zoom = 8;
        owner->DocumentChanged();
        const auto settle = [&]() noexcept
        { return SettleJobs(owner->_jobs, 30000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
        const auto until = [&](auto done) noexcept
        {
            const auto deadline = GetTickCount64() + 10000;
            while (!done())
            {
                if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                    return false;
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                if (done())
                    return true;
                const auto now = GetTickCount64();
                if (now >= deadline)
                    return false;
                if (owner->_jobs.HasUiWork())
                    continue;
                const HANDLE wake = owner->_jobs.WakeHandle();
                if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    return false;
            }
            return true;
        };
        if (!expect(settle(), "initial-source-and-first-display-settled"))
            return 5;
        owner->RenderFrame();
        const auto hole = owner->_chrome->HolePixels();
        const auto origin = owner->_viewport.DocumentRectOrigin(owner->DocumentSize());
        owner->_viewport.pan.x += std::round(hole.left + origin.x) - (hole.left + origin.x);
        owner->_viewport.pan.y += std::round(hole.top + origin.y) - (hole.top + origin.y);
        std::array<BYTE, 256> clearKeys{};
        KeyboardState::Write(clearKeys);
        const auto send = [&](UINT message, Vec2 point, WPARAM modifiers = 0) noexcept
        {
            const auto view = owner->_viewport.ViewFromDocument(point, owner->DocumentSize());
            const auto current = owner->_chrome->HolePixels();
            SendMessageW(owner->_window.get(), message, modifiers,
                         MAKELPARAM(static_cast<int>(std::lround(current.left + view.x)),
                                    static_cast<int>(std::lround(current.top + view.y))));
        };
        std::array<ContinuationReceipt, 8> receipts;
        unsigned receiptCount = 0;
        const auto click = [&](Vec2 point) noexcept
        {
            owner->_lastPressTick = 0;
            send(WM_LBUTTONDOWN, point, MK_LBUTTON);
            if (receiptCount < receipts.size())
                receipts[receiptCount++] = owner->_polygonalPressLastReceipt;
            send(WM_LBUTTONUP, point);
        };
        const auto key = [&](UINT value) noexcept
        {
            SendMessageW(owner->_window.get(), WM_KEYDOWN, value, 0);
            SendMessageW(owner->_window.get(), WM_KEYUP, value, MAKELPARAM(1, KF_UP | KF_REPEAT));
            if (receiptCount < receipts.size())
                receipts[receiptCount++] = owner->_polygonalPressLastReceipt;
        };
        const auto generation = owner->_session.Generation();
        const auto revision = owner->_session.history.CurrentRevision();
        const bool inside = name == "inside-queued";
        const bool escape = name == "escape-before-controller";
        const bool zero = name == "backspace-before-controller";
        const bool joined = name == "joined-close";
        const bool live = name == "save-live" || name == "tool-live" || name == "leave-zoom-live" ||
                          name == "space-pan-live" || name == "right-ignore-live";
        const double left = mode == SelectionMode::Subtract ? 3 : 7;
        if (name == "inside-save-before-up")
        {
            send(WM_LBUTTONDOWN, {2, 2}, MK_LBUTTON);
            const auto dragReceipt = owner->_polygonalPressLastReceipt;
            send(WM_MOUSEMOVE, {5, 4}, MK_LBUTTON);
            if (!expect(until(
                            [&]() noexcept
                            {
                                return owner->_polygonalPressState && owner->_polygonalPressState->waiting &&
                                       owner->_session.IsMovingSelection();
                            }),
                        "original-inside-drag-opens-and-parks-before-release"))
                return 5;
            const auto original = owner->_polygonalPressState->original;
            if (!expect(dragReceipt.Read().outcome == HostOutcome::Pending && owner->_historyWork.Busy() &&
                            owner->_polygonalPressParents == 1 && owner->_polygonalPressState->moves == 1 &&
                            owner->_session.history.CaptureState().Depth() == 1 &&
                            owner->_session.Generation() == generation + 1 &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true),
                        "one-original-parent-holds-history-with-only-scalar-preview-offset"))
                return 5;
            const auto file = output / L"inside-before-up.prism";
            const auto save = owner->QueueHostInput(HostWriteInput{HostWriteFormat::Project, false, file.wstring()});
            if (!expect(save.Read().outcome == HostOutcome::Pending && owner->_hostRequests.Size() == 1,
                        "save-accepted-after-move-before-release-on-same-host-order"))
                return 5;
            (void)RunUiActions(owner->_jobs);
            if (!expect(save.Read().outcome == HostOutcome::Pending &&
                            dragReceipt.Read().outcome == HostOutcome::Pending &&
                            owner->_polygonalPressState->original == original && owner->_polygonalPressState->waiting &&
                            !std::filesystem::exists(file),
                        "save-waits-original-atomic-drag-with-no-synthetic-terminal-or-write"))
                return 5;
            const auto pending = owner->_polygonalPressInput.PendingCount();
            send(WM_LBUTTONUP, {5, 4});
            if (!expect(owner->_polygonalPressInput.PendingCount() == pending + 1 &&
                            save.Read().outcome == HostOutcome::Pending && owner->_polygonalPressParents == 1,
                        "later-real-up-is-recorded-without-second-parent-while-save-waits"))
                return 5;
            if (!expect(settle(), "real-up-releases-original-barrier-then-complete-save-and-retirement"))
                return 5;
            if (!expect(dragReceipt.Read().outcome == HostOutcome::Succeeded &&
                            save.Read().outcome == HostOutcome::Succeeded && std::filesystem::is_regular_file(file) &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true, 4, 3, 8, 7) &&
                            owner->_session.Generation() == generation + 2 &&
                            owner->_session.history.UndoCount() == 1 && !owner->_session.history.IsModified() &&
                            owner->_session.history.CaptureState().Depth() == 0 &&
                            !owner->_polygonalPressInput.Busy() && !owner->_historyWork.Busy() &&
                            !owner->_session.IsMovingSelection() && owner->_polygonalPressParents == 1 &&
                            owner->_polygonalPressState->completed && owner->_polygonalPressState->sameTicket,
                        "save-publishes-only-after-exact-move-with-one-history-entry-and-original-ticket"))
                return 5;
            owner->_session.Undo();
            if (!expect(DispatcherPixels(owner->_session, SelectionMode::New, true),
                        "saved-inside-move-undo-restores-original-literal-selection"))
                return 5;
            owner->_session.Redo();
            if (!expect(DispatcherPixels(owner->_session, SelectionMode::New, true, 4, 3, 8, 7),
                        "saved-inside-move-redo-restores-complete-literal-result"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "save-drag-and-joined-shutdown-refund-original-cpu-gpu-and-weak-owners"))
                return 5;
            ++reached;
            continue;
        }
        if (name == "outside-foreign-history" || name == "inside-foreign-history")
        {
            if (name == "inside-foreign-history")
            {
                send(WM_LBUTTONDOWN, {2, 2}, MK_LBUTTON);
                receipts[receiptCount++] = owner->_polygonalPressLastReceipt;
                send(WM_MOUSEMOVE, {5, 4}, MK_LBUTTON);
                send(WM_LBUTTONUP, {5, 4});
            }
            else
            {
                click({7, 1});
                click({11, 1});
                click({11, 5});
                click({7, 5});
                key(VK_RETURN);
            }
            if (!expect(receiptCount && receipts[0].Read().outcome == HostOutcome::Pending &&
                            !owner->_polygonalPressState && owner->_polygonalPressInput.Busy(),
                        "prefix-recorded-before-foreign-transaction-and-first-controller"))
                return 5;
            owner->_session.history.Begin(L"Foreign transaction", owner->_session.Document(), active);
            const auto foreignHistory = owner->_session.history.CaptureAuthority();
            if (!expect(owner->_session.history.CaptureState().Depth() == 1 &&
                            owner->_session.Generation() == generation &&
                            owner->_session.history.CurrentRevision() == revision,
                        "foreign-opening-changes-history-authority-without-changing-document-generation"))
                return 5;
            if (!expect(settle(), "queued-dependent-receipts-and-retirement-settle"))
                return 5;
            bool stale = true;
            for (unsigned i = 0; i < receiptCount; ++i)
                stale = stale && receipts[i].Read().outcome == HostOutcome::Stale;
            if (!expect(stale && owner->_polygonalPressParents == 0 && !owner->_polygonalPressState &&
                            !owner->_polygonalPressInput.Busy() && !owner->_activeHostRequest &&
                            !owner->_hostRequests.Size() && !owner->_historyWork.Busy() &&
                            !owner->_session.PendingLasso() && !owner->_session.IsMovingSelection(),
                        "foreign-pending-history-refuses-before-original-admission-source-or-writer"))
                return 5;
            if (!expect(DispatcherPixels(owner->_session, SelectionMode::New, true) &&
                            owner->_session.Generation() == generation &&
                            owner->_session.history.CurrentRevision() == revision &&
                            owner->_session.history.CaptureState().Depth() == 1 &&
                            owner->_session.history.IsCurrent(foreignHistory) &&
                            owner->_session.history.UndoCount() == 0,
                        "refusal-preserves-exact-foreign-history-and-all4096-prior-selection-centers"))
                return 5;
            owner->_session.history.End(owner->_session.Document(), active);
            if (!expect(owner->_session.history.CaptureState().Depth() == 0 &&
                            owner->_session.history.UndoCount() == 0 &&
                            owner->_session.history.CurrentRevision() == revision,
                        "foreign-owner-closes-original-noop-without-editor-entry-or-revision"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "refused-prefix-and-joined-shutdown-refund-all-original-and-display-work"))
                return 5;
            ++reached;
            continue;
        }
        if (name == "right-ignore-first")
        {
            send(WM_RBUTTONDOWN, {2, 2}, MK_RBUTTON);
            send(WM_RBUTTONUP, {2, 2});
            if (!expect(!owner->_polygonalPressInput.Busy() && !owner->_polygonalPressState &&
                            !owner->_activeHostRequest && !owner->_hostRequests.Size() && !owner->_historyWork.Busy() &&
                            !owner->_session.PendingLasso() && !owner->_session.IsMovingSelection() &&
                            owner->_session.Generation() == generation &&
                            owner->_session.history.CurrentRevision() == revision &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true),
                        "right-selection-press-captures-no-original-and-preserves-all-literal-selection-centers"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "ignored-right-first-press-has-no-admitted-or-retained-work"))
                return 5;
            ++reached;
            continue;
        }
        if (name == "family-counter-exhausted" || name == "record-counter-exhausted")
        {
            constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
            if (name == "family-counter-exhausted")
                owner->_polygonalPressFamily = maximum;
            else
                owner->_polygonalPressRecord = maximum;
            click({2, 2});
            const auto receipt = owner->_polygonalPressLastReceipt.Read();
            if (!expect(receipt.outcome == HostOutcome::Failed &&
                            receipt.error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)),
                        "first-left-press-refuses-exhausted-counter-with-arithmetic-overflow"))
                return 5;
            if (!expect(!owner->_polygonalPressInput.Busy() && !owner->_polygonalPressState &&
                            !owner->_polygonalPressLeftDown && owner->_polygonalPressParents == 0 &&
                            !owner->_activeHostRequest && !owner->_hostRequests.Size() && !owner->_historyWork.Busy() &&
                            !owner->_session.PendingLasso() && !owner->_session.IsMovingSelection() &&
                            owner->_polygonalPressFamily == (name == "family-counter-exhausted" ? maximum : 0) &&
                            owner->_polygonalPressRecord == (name == "record-counter-exhausted" ? maximum : 0) &&
                            owner->_polygonalPressDownOrdinal == 0 && owner->_session.Generation() == generation &&
                            owner->_session.history.CurrentRevision() == revision &&
                            owner->_session.history.UndoCount() == 0 &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true),
                        "overflow-refusal-does-no-journal-controller-host-history-or-document-work"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "overflow-refusal-and-joined-close-refund-all-work"))
                return 5;
            ++reached;
            continue;
        }
        if (name == "absent-document")
        {
            owner->_session.ClearDocument();
            owner->DocumentChanged();
            if (!expect(settle(), "cleared-document-and-display-settled-before-canvas-press"))
                return 5;
            const auto emptyGeneration = owner->_session.Generation();
            const auto emptyRevision = owner->_session.history.CurrentRevision();
            const auto family = owner->_polygonalPressFamily;
            const auto record = owner->_polygonalPressRecord;
            const auto downOrdinal = owner->_polygonalPressDownOrdinal;
            const auto undoCount = owner->_session.history.UndoCount();
            const auto emptyHole = owner->_chrome->HolePixels();
            const LPARAM point = MAKELPARAM(static_cast<int>(std::lround(emptyHole.left + emptyHole.width / 2.0)),
                                            static_cast<int>(std::lround(emptyHole.top + emptyHole.height / 2.0)));
            owner->_lastPressTick = 0;
            SendMessageW(owner->_window.get(), WM_LBUTTONDOWN, MK_LBUTTON, point);
            SendMessageW(owner->_window.get(), WM_LBUTTONUP, 0, point);
            const auto receipt = owner->_polygonalPressLastReceipt.Read();
            if (!expect(receipt.outcome == HostOutcome::Failed && receipt.error == E_ABORT,
                        "canvas-press-without-document-is-handled-quietly-with-abort-receipt"))
                return 5;
            if (!expect(!owner->_polygonalPressInput.Busy() && !owner->_polygonalPressState &&
                            !owner->_polygonalPressLeftDown && owner->_polygonalPressParents == 0 &&
                            !owner->_activeHostRequest && !owner->_hostRequests.Size() && !owner->_historyWork.Busy() &&
                            !owner->_session.PendingLasso() && !owner->_session.IsMovingSelection() &&
                            owner->_polygonalPressFamily == family && owner->_polygonalPressRecord == record &&
                            owner->_polygonalPressDownOrdinal == downOrdinal &&
                            owner->_session.Generation() == emptyGeneration &&
                            owner->_session.history.CurrentRevision() == emptyRevision &&
                            owner->_session.history.UndoCount() == undoCount && !owner->_session.Document().has_value(),
                        "absent-document-refusal-does-not-admit-or-mutate-session-or-counters"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "absent-document-refusal-and-joined-close-refund-all-work"))
                return 5;
            ++reached;
            continue;
        }
        const bool replaced = name == "prefix-replaced-document";
        const bool targetRefusal = name == "prefix-target-refusal";
        const bool memoryRefusal = name == "prefix-memory-refusal";
        const bool capacity = name == "host-descriptor-exhaustion";
        const bool outsideOverflow = name == "outside-prefix-overflow";
        const bool insideOverflow = name == "inside-up-prefix-overflow";
        const bool missingUp = name == "inside-missing-up-successor";
        if (replaced || targetRefusal || memoryRefusal || capacity || outsideOverflow || insideOverflow || missingUp)
        {
            if (memoryRefusal)
                scarce.store(true, std::memory_order_relaxed);
            if (capacity)
            {
                for (unsigned i = 0; i < receipts.size(); ++i)
                    click({7.0 + i * 4.0, 1});
                click({43, 1}); // Ninth descriptor must fail while all eight accepted records remain paired.
                const auto refused = owner->_polygonalPressLastReceipt.Read();
                if (!expect(receiptCount == 8 && refused.outcome == HostOutcome::Failed &&
                                refused.error == HRESULT_FROM_WIN32(ERROR_BUSY) &&
                                owner->_polygonalPressInput.Closed() &&
                                owner->_polygonalPressInput.PendingCount() == 17,
                            "ninth-host-descriptor-refuses-and-seals-only-the-accepted-eight-record-pairs"))
                    return 5;
            }
            else if (insideOverflow || missingUp)
            {
                send(WM_LBUTTONDOWN, {2, 2}, MK_LBUTTON);
                receipts[receiptCount++] = owner->_polygonalPressLastReceipt;
                send(WM_MOUSEMOVE, {5, 4}, MK_LBUTTON);
                if (insideOverflow)
                    send(WM_LBUTTONUP, {5, 4});
                owner->_lastPressTick = 0;
                send(WM_LBUTTONDOWN, {20, 20}, MK_LBUTTON);
                receipts[receiptCount++] = owner->_polygonalPressLastReceipt;
                if (missingUp)
                {
                    send(WM_LBUTTONUP, {20, 20});
                    key(VK_ESCAPE);
                }
            }
            else
            {
                click({7, 1});
                if (!outsideOverflow)
                {
                    click({11, 1});
                    key(VK_RETURN);
                }
            }
            if (outsideOverflow || insideOverflow)
            {
                const auto accepted = owner->_polygonalPressInput.PendingCount();
                for (std::size_t i = accepted; i < HostPolygonalInputJournal::kCapacity; ++i)
                    send(WM_MOUSEMOVE, {21, 21}, insideOverflow ? MK_LBUTTON : 0);
                send(WM_MOUSEMOVE, {22, 22}, insideOverflow ? MK_LBUTTON : 0);
                send(WM_LBUTTONUP, {22, 22});
                if (!expect(owner->_polygonalPressInput.Closed() && owner->_polygonalPressInput.Overflowed() &&
                                owner->_polygonalPressInput.PendingCount() == HostPolygonalInputJournal::kCapacity + 1,
                            "all512-physical-records-plus-one-reserved-loss-are-present-before-controller"))
                    return 5;
            }
            if (!expect(receiptCount && receipts[0].Read().outcome == HostOutcome::Pending &&
                            owner->_polygonalPressInput.Busy() && !owner->_polygonalPressState &&
                            owner->_session.Generation() == generation &&
                            owner->_session.history.CurrentRevision() == revision,
                        "boundary-prefix-reserves-only-scalars-before-any-original-controller-or-capture"))
                return 5;
            std::optional<Guid> foreignActive;
            if (replaced)
            {
                DocumentFixture foreign;
                foreign.width = foreign.height = 64;
                foreign.id = Guid::Generate();
                auto foreignLayer = ImageLayer::Blank(L"Foreign replacement", {64, 64});
                foreignActive = foreignLayer.id;
                foreign.layers.push_back(std::move(foreignLayer));
                foreign.selection = DocumentSelection{
                    false, SelectionLoops{std::vector<SelectionLoop>{{{20, 20}, {24, 20}, {24, 24}, {20, 24}}}}};
                owner->_session.ReplaceDocument(std::move(foreign).Seal(), foreignActive);
                owner->DocumentChanged();
            }
            else if (targetRefusal)
            {
                owner->_session.SelectLayer(std::nullopt);
                owner->SelectionChanged();
            }
            const auto postMutationGeneration = owner->_session.Generation();
            const auto postMutationRevision = owner->_session.history.CurrentRevision();
            if (!expect(settle(), "boundary-completions-source-actions-and-retirement-settle-without-polling"))
                return 5;
            scarce.store(false, std::memory_order_relaxed);
            bool outcomes = true;
            for (unsigned i = 0; i < receiptCount; ++i)
            {
                const auto result = receipts[i].Read();
                outcomes = outcomes &&
                           result.outcome == (memoryRefusal               ? HostOutcome::Failed
                                              : replaced || targetRefusal ? HostOutcome::Stale
                                                                          : HostOutcome::Succeeded) &&
                           (!memoryRefusal || result.error == E_OUTOFMEMORY);
            }
            const bool overflowJoined = !(outsideOverflow || insideOverflow) ||
                                        (owner->_polygonalPressState && owner->_polygonalPressState->completed &&
                                         owner->_polygonalPressState->overflowObserved &&
                                         owner->_polygonalPressState->outcome.status == JobStatus::Cancelled &&
                                         owner->_polygonalPressState->maximumDrawingSamples <= 16);
            if (!expect(outcomes && overflowJoined && !owner->_polygonalPressInput.Busy() &&
                            !owner->_activeHostRequest && !owner->_hostRequests.Size() && !owner->_historyWork.Busy() &&
                            (!owner->_polygonalPressState || !owner->_polygonalPressState->controller) &&
                            !owner->_session.PendingLasso() && !owner->_session.IsMovingSelection(),
                        "every-dependent-receipt-and-family-joins-with-no-orphan-raw-prefix-or-writer"))
                return 5;
            if (memoryRefusal)
            {
                // Submit returns one original ticket even when AdmitWaiting queues an immediate memory failure.
                // That failed record must execute no worker, capture no roots and open no writer.
                if (!owner->_polygonalPressState || owner->_polygonalPressState->captures ||
                    owner->_polygonalPressState->runs || owner->_polygonalPressState->opened ||
                    owner->_polygonalPressParents != 1)
                    std::printf("DIAG memory-refusal state=%d parents=%llu captures=%u runs=%u opened=%d\n",
                                owner->_polygonalPressState.has_value(),
                                static_cast<unsigned long long>(owner->_polygonalPressParents),
                                owner->_polygonalPressState ? owner->_polygonalPressState->captures : 0,
                                owner->_polygonalPressState ? owner->_polygonalPressState->runs : 0,
                                owner->_polygonalPressState && owner->_polygonalPressState->opened);
                if (!expect(owner->_polygonalPressState && owner->_polygonalPressState->captures == 0 &&
                                owner->_polygonalPressState->runs == 0 && !owner->_polygonalPressState->opened &&
                                owner->_polygonalPressParents == 1,
                            "memory-refusal-precedes-original-worker-immutable-capture-and-writer"))
                    return 5;
            }
            const bool moved = insideOverflow || missingUp;
            const bool pixels = replaced ? DispatcherPixels(owner->_session, SelectionMode::New, true, 20, 20, 24, 24)
                                : moved  ? DispatcherPixels(owner->_session, SelectionMode::New, true, 4, 3, 8, 7)
                                         : DispatcherPixels(owner->_session, SelectionMode::New, true);
            if (!expect(pixels && owner->_session.Generation() == postMutationGeneration + (moved ? 2u : 0u) &&
                            owner->_session.history.UndoCount() == (moved ? 1 : 0) &&
                            (moved || owner->_session.history.CurrentRevision() == postMutationRevision) &&
                            (!replaced || owner->_session.ActiveLayerID() == foreignActive) &&
                            (!targetRefusal || !owner->_session.ActiveLayerID()) &&
                            (!(replaced || targetRefusal) || owner->_polygonalPressParents == 0) &&
                            (!moved || owner->_polygonalPressParents == 2),
                        "boundary-keeps-exact-foreign-or-prior-document-and-only-the-older-owned-inside-move"))
                return 5;
            if (moved)
            {
                owner->_session.Undo();
                if (!expect(DispatcherPixels(owner->_session, SelectionMode::New, true),
                            "older-success-undo-survives-younger-loss"))
                    return 5;
                owner->_session.Redo();
                if (!expect(DispatcherPixels(owner->_session, SelectionMode::New, true, 4, 3, 8, 7),
                            "older-success-redo-survives-younger-loss"))
                    return 5;
            }
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "boundary-joined-shutdown-refunds-original-cpu-gpu-and-weak-owners"))
                return 5;
            ++reached;
            continue;
        }
        if (inside)
        {
            send(WM_LBUTTONDOWN, {2, 2}, MK_LBUTTON);
            receipts[receiptCount++] = owner->_polygonalPressLastReceipt;
            send(WM_MOUSEMOVE, {5, 4}, MK_LBUTTON);
            send(WM_LBUTTONUP, {5, 4});
            send(WM_LBUTTONDOWN, {5, 4}, MK_LBUTTON);
            receipts[receiptCount++] = owner->_polygonalPressLastReceipt;
            send(WM_MOUSEMOVE, {7, 5}, MK_LBUTTON);
            send(WM_LBUTTONUP, {7, 5});
        }
        else
        {
            click({left, 1});
            if (escape)
                key(VK_ESCAPE);
            else if (zero)
                key(VK_BACK);
            else if (!joined)
            {
                click({left + 4, 1});
                click({left + 4, 5});
                if (name == "hover-backspace")
                {
                    send(WM_MOUSEMOVE, {20, 20});
                    key(VK_BACK);
                    click({left + 4, 5});
                }
                click({left, 5});
                if (!live)
                {
                    key(VK_RETURN);
                    if (name == "close-then-next")
                    {
                        send(WM_LBUTTONDOWN, {8, 2}, MK_LBUTTON);
                        receipts[receiptCount++] = owner->_polygonalPressLastReceipt;
                        send(WM_MOUSEMOVE, {9, 3}, MK_LBUTTON);
                        send(WM_LBUTTONUP, {9, 3});
                    }
                }
            }
        }
        if (!expect(receiptCount > 0 && receipts[0].Read().outcome == HostOutcome::Pending &&
                        owner->_polygonalPressInput.Busy() && owner->_polygonalPressInput.PendingCount() >= 2 &&
                        owner->_session.Generation() == generation &&
                        owner->_session.history.CurrentRevision() == revision,
                    "physical-prefix-and-conditional-receipts-admit-before-controller-or-source-capture"))
            return 5;
        if (live || joined)
        {
            if (!expect(until(
                            [&]() noexcept
                            {
                                return owner->_polygonalPressState && owner->_polygonalPressState->waiting &&
                                       receipts[receiptCount - 1].Read().outcome == HostOutcome::Succeeded;
                            }),
                        "original-outside-writer-remains-live-after-creator-and-corner-ack"))
                return 5;
            if (!expect(owner->_polygonalPressState->controller && owner->_polygonalPressState->drawing &&
                            owner->_polygonalPressParents == 1 && !owner->_historyWork.Busy() &&
                            owner->_session.Generation() == generation && owner->_session.history.UndoCount() == 0,
                        "interactive-draft-releases-host-barrier-and-keeps-original-admission"))
                return 5;
        }
        if (name == "save-live")
        {
            const auto file = output / L"live.prism";
            const auto ticket = owner->_polygonalPressState->original;
            auto receipt = owner->QueueHostInput(HostWriteInput{HostWriteFormat::Project, false, file.wstring()});
            if (!expect(until([&]() noexcept { return receipt.Read().outcome != HostOutcome::Pending; }) &&
                            receipt.Read().outcome == HostOutcome::Succeeded &&
                            std::filesystem::is_regular_file(file) && owner->_polygonalPressState->controller &&
                            owner->_polygonalPressState->original == ticket && owner->_session.PendingLasso() &&
                            owner->_session.history.CurrentRevision() == revision,
                        "actual-project-save-preserves-exact-unfinished-original-writer"))
                return 5;
            key(VK_RETURN);
        }
        else if (name == "tool-live")
        {
            auto receipt = owner->QueueHostInput(HostToolInput{EditorTool::Hand});
            if (!expect(until([&]() noexcept { return receipt.Read().outcome != HostOutcome::Pending; }) &&
                            receipt.Read().outcome == HostOutcome::Succeeded && owner->_tool == EditorTool::Hand &&
                            !owner->_session.PendingLasso() && owner->_session.history.UndoCount() == 0,
                        "ordered-tool-change-joins-original-cancel-before-applying-tool"))
                return 5;
        }
        else if (name == "leave-zoom-live")
        {
            const auto ticket = owner->_polygonalPressState->original;
            SendMessageW(owner->_window.get(), WM_MOUSELEAVE, 0, 0);
            auto receipt = owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandZoomIn});
            if (!expect(until([&]() noexcept { return receipt.Read().outcome != HostOutcome::Pending; }) &&
                            receipt.Read().outcome == HostOutcome::Succeeded &&
                            owner->_polygonalPressState->original == ticket && owner->_session.PendingLasso(),
                        "actual-leave-and-queued-zoom-preserve-original-family"))
                return 5;
            key(VK_RETURN);
        }
        else if (name == "space-pan-live")
        {
            const auto ticket = owner->_polygonalPressState->original;
            const auto pan = owner->_viewport.pan;
            const auto points = owner->_session.PendingLasso()->PointCount();
            SendMessageW(owner->_window.get(), WM_KEYDOWN, VK_SPACE, 0);
            send(WM_LBUTTONDOWN, {20, 20}, MK_LBUTTON);
            send(WM_MOUSEMOVE, {22, 21}, MK_LBUTTON);
            SendMessageW(owner->_window.get(), WM_KEYUP, VK_SPACE, MAKELPARAM(1, KF_UP | KF_REPEAT));
            send(WM_LBUTTONUP, {22, 21});
            if (!expect(!owner->_spacePan && owner->_viewport.pan != pan &&
                            std::holds_alternative<std::monostate>(owner->_drag) &&
                            owner->_polygonalPressState->original == ticket &&
                            owner->_polygonalPressState->controller && owner->_polygonalPressParents == 1 &&
                            owner->_session.PendingLasso() && owner->_session.PendingLasso()->PointCount() == points &&
                            !owner->_polygonalPressLeftDown && owner->_session.Generation() == generation,
                        "space-pan-and-release-before-up-preserve-original-writer-and-end-only-view-drag"))
                return 5;
            SendMessageW(owner->_window.get(), WM_KEYDOWN, VK_SPACE, 0);
            send(WM_LBUTTONDOWN, {20, 20}, MK_LBUTTON);
            send(WM_MOUSEMOVE, {22, 21}, MK_LBUTTON);
            const bool captured = GetCapture() == owner->_window.get();
            (void)ReleaseCapture(); // Actual WM_CAPTURECHANGED must end this view drag without a lasso terminal.
            SendMessageW(owner->_window.get(), WM_KEYUP, VK_SPACE, MAKELPARAM(1, KF_UP | KF_REPEAT));
            if (!expect(captured && GetCapture() != owner->_window.get() &&
                            std::holds_alternative<std::monostate>(owner->_drag) &&
                            owner->_polygonalPressState->original == ticket &&
                            owner->_polygonalPressState->controller && owner->_session.PendingLasso() &&
                            owner->_session.PendingLasso()->PointCount() == points &&
                            !owner->_polygonalPressInput.Closed() && owner->_session.Generation() == generation,
                        "actual-pan-capture-loss-clears-only-pan-and-keeps-original-polygonal-writer"))
                return 5;
            key(VK_RETURN);
        }
        else if (name == "right-ignore-live")
        {
            const auto ticket = owner->_polygonalPressState->original;
            const auto points = owner->_session.PendingLasso()->PointCount();
            const auto receipt = owner->_polygonalPressLastReceipt;
            send(WM_RBUTTONDOWN, {26, 26}, MK_RBUTTON);
            send(WM_RBUTTONUP, {26, 26});
            if (!expect(owner->_polygonalPressState->controller && owner->_polygonalPressState->original == ticket &&
                            owner->_polygonalPressParents == 1 && owner->_session.PendingLasso() &&
                            owner->_session.PendingLasso()->PointCount() == points &&
                            !owner->_session.IsMovingSelection() && !owner->_polygonalPressLeftDown &&
                            owner->_polygonalPressLastReceipt.Read().outcome == receipt.Read().outcome &&
                            owner->_session.Generation() == generation,
                        "right-selection-press-cannot-extend-or-finish-original-prepared-writer"))
                return 5;
            key(VK_RETURN);
        }
        if (joined)
        {
            owner->Shutdown();
            if (!expect(!owner->_polygonalPressInput.Busy() && !owner->_polygonalPressState && owner->_jobs.IsIdle() &&
                            !owner->_jobs.ReservedMemoryBytes() && !owner->_jobs.ReleasingMemoryBytes(),
                        "joined-shutdown-suppresses-useful-callback-and-retires-entire-family"))
                return 5;
        }
        else
        {
            if (!expect(settle(), "all-exact-receipts-and-original-retirement-settle"))
                return 5;
            const bool cancelled = escape || zero || name == "tool-live";
            const unsigned parents = inside || name == "close-then-next" ? 2 : 1;
            bool allReceipts = true;
            for (unsigned i = 0; i < receiptCount; ++i)
                allReceipts = allReceipts && receipts[i].Read().outcome == HostOutcome::Succeeded;
            if (!expect(allReceipts && owner->_polygonalPressParents == parents &&
                            !owner->_polygonalPressInput.Busy() && !owner->_activeHostRequest &&
                            !owner->_hostRequests.Size() && !owner->_historyWork.Busy(),
                        "all-accepted-physical-descriptors-acknowledge-and-family-retires-once"))
                return 5;
            const bool pixels = cancelled ? DispatcherPixels(owner->_session, SelectionMode::New, true)
                                : inside  ? DispatcherPixels(owner->_session, SelectionMode::New, true, 6, 4, 10, 8)
                                : name == "close-then-next"
                                    ? DispatcherPixels(owner->_session, SelectionMode::New, true, 8, 2, 12, 6)
                                    : DispatcherPixels(owner->_session, mode);
            if (!expect(pixels && owner->_session.history.UndoCount() == (cancelled ? 0 : static_cast<int>(parents)) &&
                            owner->_session.Generation() == generation + (cancelled      ? 0u
                                                                          : inside       ? 4u
                                                                          : parents == 2 ? 3u
                                                                                         : 1u),
                        "all4096-independent-centers-generation-and-history-match-selected-branch"))
                return 5;
            if (!cancelled)
            {
                for (unsigned i = 0; i < parents; ++i)
                    owner->_session.Undo();
                if (!expect(DispatcherPixels(owner->_session, SelectionMode::New, true),
                            "undo-chain-restores-exact-original-selection"))
                    return 5;
                for (unsigned i = 0; i < parents; ++i)
                    owner->_session.Redo();
                if (!expect(inside ? DispatcherPixels(owner->_session, SelectionMode::New, true, 6, 4, 10, 8)
                            : name == "close-then-next"
                                ? DispatcherPixels(owner->_session, SelectionMode::New, true, 8, 2, 12, 6)
                                : DispatcherPixels(owner->_session, mode),
                            "redo-chain-restores-complete-literal-result"))
                    return 5;
            }
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "joined-native-close-refunds-cpu-gpu-and-weak-owners"))
                return 5;
        }
        ++reached;
    }
    scenario = "complete";
    return expect(reached == cases.size() && failures == 0, "all-native-dispatch-scenarios-reached") ? 0 : 5;
}
