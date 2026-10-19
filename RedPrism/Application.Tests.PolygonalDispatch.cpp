#include "Application.h"
#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "ImageCodecs.h"
#include "KeyboardState.h"
#include "Prompts.h"
#include "Resources.h"
#include "resource.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
struct PolygonalSchedulerCapacityGate final
{
    HANDLE lastStarted = nullptr;
    HANDLE release = nullptr;
    std::atomic<unsigned> started{0};
    std::atomic<unsigned> completed{0};
    std::atomic<unsigned> failed{0};
};

class PolygonalSchedulerCapacityJob final : public Job
{
  public:
    PolygonalSchedulerCapacityJob(PolygonalSchedulerCapacityGate& gate, bool block) noexcept
        : _gate(gate), _block(block)
    {
    }

    JobResult Run(JobContext&) override
    {
        _gate.started.fetch_add(1, std::memory_order_relaxed);
        if (_block)
        {
            (void)SetEvent(_gate.lastStarted);
            if (WaitForSingleObject(_gate.release, 10000) != WAIT_OBJECT_0)
                return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        }
        return {};
    }

    void Completed(JobResult result) noexcept override
    {
        _gate.completed.fetch_add(1, std::memory_order_relaxed);
        if (result.status != JobStatus::Finished)
            _gate.failed.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    PolygonalSchedulerCapacityGate& _gate;
    bool _block;
};

class PolygonalHeaderPrompts final : public Prompts
{
  public:
    explicit PolygonalHeaderPrompts(const Prompts& delegate) noexcept : _delegate(delegate) {}

    bool ShowNotice(ChromeHost* chrome, std::wstring text) const noexcept override
    {
        return _delegate.ShowNotice(chrome, std::move(text));
    }

    void PostNotice(HWND window, UINT message, UINT stringId, int formatValue = 0) const noexcept override
    {
        ++postNoticeCount;
        lastStringId = stringId;
        _delegate.PostNotice(window, message, stringId, formatValue);
    }

    void ShowError(HWND window, UINT stringId) const noexcept override
    {
        _delegate.ShowError(window, stringId);
    }

    void ShowFormattedError(HWND window, const std::wstring& text) const noexcept override
    {
        _delegate.ShowFormattedError(window, text);
    }

    void ShowInfo(HWND window, const std::wstring& text, const std::wstring& caption) const noexcept override
    {
        _delegate.ShowInfo(window, text, caption);
    }

    [[nodiscard]] Answer AskThreeWay(HWND window, const std::wstring& title, const std::wstring& text,
                                     const std::wstring& first, const std::wstring& second, bool warning,
                                     Answer forced) const noexcept override
    {
        return _delegate.AskThreeWay(window, title, text, first, second, warning, forced);
    }

    mutable unsigned postNoticeCount = 0;
    mutable UINT lastStringId = 0;

  private:
    const Prompts& _delegate;
};

[[nodiscard]] bool IsNamedEnabledButton(IRawElementProviderFragment* fragment, std::wstring_view name) noexcept
{
    if (!fragment)
        return false;
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    wil::unique_variant label, type, enabled;
    if (FAILED(fragment->QueryInterface(IID_PPV_ARGS(simple.put()))) ||
        FAILED(simple->GetPropertyValue(UIA_NamePropertyId, &label)) || label.vt != VT_BSTR || !label.bstrVal ||
        FAILED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &type)) || type.vt != VT_I4 ||
        type.lVal != UIA_ButtonControlTypeId || FAILED(simple->GetPropertyValue(UIA_IsEnabledPropertyId, &enabled)) ||
        enabled.vt != VT_BOOL || enabled.boolVal != VARIANT_TRUE)
        return false;
    return std::wstring_view(label.bstrVal, SysStringLen(label.bstrVal)) == name;
}

[[nodiscard]] wil::com_ptr_nothrow<IRawElementProviderFragment> FindNamedEnabledButton(
    IRawElementProviderFragment* parent, std::wstring_view name, unsigned& remaining, unsigned depth = 0) noexcept
{
    wil::com_ptr_nothrow<IRawElementProviderFragment> found;
    if (!parent || remaining == 0)
        return found;
    --remaining;
    if (depth > 32)
        return found;
    if (IsNamedEnabledButton(parent, name))
    {
        (void)parent->QueryInterface(IID_PPV_ARGS(found.put()));
        return found;
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> child;
    if (FAILED(parent->Navigate(NavigateDirection_FirstChild, child.put())))
        return found;
    while (child && remaining != 0)
    {
        found = FindNamedEnabledButton(child.get(), name, remaining, depth + 1);
        if (found)
            return found;
        wil::com_ptr_nothrow<IRawElementProviderFragment> next;
        if (FAILED(child->Navigate(NavigateDirection_NextSibling, next.put())))
            break;
        child = std::move(next);
    }
    return found;
}

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
    constexpr std::array cases{"outside-replaced-parked",
                               "outside-cleared-parked",
                               "outside-foreign-history",
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
                               "scheduler-job-slot-exhaustion",
                               "outside-prefix-overflow",
                               "inside-up-prefix-overflow",
                               "inside-missing-up-successor",
                               "png-export-original-outside",
                               "jpeg-export-original-outside",
                               "png-picker-cancel-original-outside-seam",
                               "jpeg-picker-cancel-original-outside-seam",
                               "joined-close",
                               "deselect-live",
                               "undo-live",
                               "redo-live",
                               "held-foreign-history-close",
                               "held-foreign-crop-close",
                               "held-foreign-mask-close",
                               "deselect-keyboard-live",
                               "undo-keyboard-live",
                               "redo-keyboard-live",
                               "deselect-header-live",
                               "expand-header-live",
                               "contract-header-live",
                               "lasso-kind-header-live",
                               "deselect-header-widget-live",
                               "ctrl-corner-new",
                               "ctrl-corner-new-shift",
                               "ctrl-corner-new-alt",
                               "ctrl-corner-new-shift-alt",
                               "ctrl-corner-new-pending-shift-alt",
                               "ctrl-corner-add",
                               "ctrl-corner-add-shift",
                               "ctrl-corner-add-alt",
                               "ctrl-corner-add-shift-alt",
                               "ctrl-corner-add-pending-shift-alt",
                               "ctrl-corner-subtract",
                               "ctrl-corner-subtract-shift",
                               "ctrl-corner-subtract-alt",
                               "ctrl-corner-subtract-shift-alt",
                               "ctrl-corner-subtract-pending-shift-alt"};
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
        const bool originalExport = name == "png-export-original-outside" || name == "jpeg-export-original-outside" ||
                                    name == "png-picker-cancel-original-outside-seam" ||
                                    name == "jpeg-picker-cancel-original-outside-seam";
        if (originalExport)
        {
            // Same literal white output as the established default-quality JPEG white-matte golden.
            // Keep cold fixture bytes off the native test function's bounded stack.
            const std::vector<std::uint8_t> pixels(64u * 64u * 4u, 255);
            layer.asset =
                RasterAsset{Raster::Make(64, 64, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, layer.name};
        }
        if (name == "held-foreign-mask-close")
        {
            const auto mask = Raster::MakeSparse(64, 64, RasterFormat::Gray8, {}, 0, 0, {}, 255);
            layer.mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"Held original white mask"}};
        }
        const auto active = layer.id;
        fixture.layers.push_back(std::move(layer));
        fixture.selection =
            DocumentSelection{false, SelectionLoops{std::vector<SelectionLoop>{{{1, 1}, {5, 1}, {5, 5}, {1, 5}}}}};
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
        owner->_session.history.Reset();
        const bool heldMask = name == "held-foreign-mask-close";
        const bool heldCrop = name == "held-foreign-crop-close";
        const bool heldTarget = heldCrop || heldMask;
        const bool heldHistory = name == "held-foreign-history-close" || heldTarget;
        const bool headerKind = name == "lasso-kind-header-live";
        const bool headerWidget = name == "deselect-header-widget-live";
        const bool headerDeselect = name == "deselect-header-live" || headerWidget;
        const bool headerOffset = name == "expand-header-live" || name == "contract-header-live";
        const bool commandDeselect = name == "deselect-live" || name == "deselect-keyboard-live" || headerDeselect;
        const bool commandUndo = name == "undo-live" || name == "undo-keyboard-live";
        const bool commandRedo = name == "redo-live" || name == "redo-keyboard-live";
        const bool keyboardCommand =
            name == "deselect-keyboard-live" || name == "undo-keyboard-live" || name == "redo-keyboard-live";
        std::optional<DocumentSelection> commandBaseSelection, commandSeedSelection;
        Guid commandBaseRevision{}, commandSeedRevision{};
        const auto commandBaseGeneration = owner->_session.Generation();
        if (commandDeselect || commandUndo || commandRedo)
        {
            commandBaseSelection = owner->_session.Selection();
            commandBaseRevision = owner->_session.history.CurrentRevision();
            commandSeedSelection = DocumentSelection{
                false, SelectionLoops{std::vector<SelectionLoop>{{{20, 20}, {24, 20}, {24, 24}, {20, 24}}}}};
        }
        if (commandUndo || commandRedo)
        {
            owner->_session.SetSelection(commandSeedSelection, L"Polygonal command seed");
            commandSeedRevision = owner->_session.history.CurrentRevision();
            if (!expect(owner->_session.Selection() == commandSeedSelection &&
                            owner->_session.history.UndoCount() == 1 &&
                            owner->_session.history.UndoName() == L"Polygonal command seed" &&
                            owner->_session.Generation() == commandBaseGeneration + 1,
                        "genuine-seed-edit-records-literal-before-after-history-and-generation"))
                return 5;
            owner->_session.Undo();
            if (!expect(owner->_session.Selection() == commandBaseSelection &&
                            owner->_session.history.UndoCount() == 0 && owner->_session.history.CanRedo() &&
                            owner->_session.history.RedoName() == L"Polygonal command seed" &&
                            owner->_session.Generation() == commandBaseGeneration + 2,
                        "seed-undo-restores-literal-original-selection-and-redo-entry"))
                return 5;
            owner->_session.Redo();
            if (!expect(
                    owner->_session.Selection() == commandSeedSelection && owner->_session.history.UndoCount() == 1 &&
                        owner->_session.history.UndoName() == L"Polygonal command seed" &&
                        !owner->_session.history.CanRedo() && owner->_session.Generation() == commandBaseGeneration + 3,
                    "seed-redo-restores-literal-edited-selection-and-undo-entry"))
                return 5;
            if (commandRedo)
            {
                owner->_session.Undo();
                if (!expect(owner->_session.Selection() == commandBaseSelection &&
                                owner->_session.history.UndoCount() == 0 && owner->_session.history.CanRedo() &&
                                owner->_session.history.RedoName() == L"Polygonal command seed" &&
                                owner->_session.Generation() == commandBaseGeneration + 4,
                            "redo-command-fixture-returns-to-literal-before-state-with-real-redo-entry"))
                    return 5;
            }
        }
        owner->_tool = EditorTool::Lasso;
        owner->_session.lassoKind = LassoKind::Polygonal;
        const bool ctrlCorner = name.starts_with("ctrl-corner-");
        const auto mode = name == "add-enter" || name.starts_with("ctrl-corner-add") ? SelectionMode::Add
                          : name == "subtract-enter" || name.starts_with("ctrl-corner-subtract")
                              ? SelectionMode::Subtract
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
        if (ctrlCorner)
        {
            // EditorCanvas.swift::lassoMouseDown chooses an existing Polygonal draft before modifiers.
            // Cover both already-open and fully queued drafts; fixed mode comes from the first Down.
            const bool pending = name.find("pending") != std::string_view::npos;
            const bool shift = name.find("shift") != std::string_view::npos;
            const bool alt = name.find("alt") != std::string_view::npos;
            const double left = mode == SelectionMode::Subtract ? 3 : 7;
            click({left, 1});
            if (!expect(receiptCount == 1 && receipts[0].Read().outcome == HostOutcome::Pending &&
                            owner->_polygonalPressInput.Busy() && !owner->_session.PendingLasso() &&
                            owner->_session.Generation() == generation && owner->_session.history.UndoCount() == 0,
                        "initial-outside-down-admits-only-scalars-before-worker-choice"))
                return 5;
            const bool parked =
                pending || until(
                               [&]() noexcept
                               {
                                   return owner->_polygonalPressState && owner->_polygonalPressState->waiting &&
                                          owner->_polygonalPressInput.PendingCount() == 0 &&
                                          !owner->_activeHostRequest && owner->_hostRequests.Size() == 0;
                               });
            if (!expect(parked &&
                            (pending ? !owner->_session.PendingLasso()
                                     : owner->_session.PendingLasso() &&
                                           owner->_session.PendingLasso()->PointCount() == 1 &&
                                           receipts[0].Read().outcome == HostOutcome::Succeeded &&
                                           owner->_polygonalPressState->drawing && owner->_polygonalPressParents == 1),
                        "captured-first-mode-draft-is-either-queued-or-parked-before-modified-corners"))
                return 5;
            const auto original = owner->_polygonalPressState ? owner->_polygonalPressState->original : JobTicket{};
            auto keys = clearKeys;
            keys[VK_CONTROL] = 0x80;
            if (shift)
                keys[VK_SHIFT] = 0x80;
            if (alt)
                keys[VK_MENU] = 0x80;
            KeyboardState::Write(keys);
            const WPARAM modifiers = MK_CONTROL | (shift ? MK_SHIFT : 0);
            for (const Vec2 corner : std::array<Vec2, 3>{{{left + 4, 1}, {left + 4, 5}, {left, 5}}})
            {
                owner->_lastPressTick = 0;
                send(WM_LBUTTONDOWN, corner, MK_LBUTTON | modifiers);
                receipts[receiptCount++] = owner->_polygonalPressLastReceipt;
                send(WM_LBUTTONUP, corner, modifiers);
            }
            KeyboardState::Write(clearKeys);
            bool modifiedPending = receiptCount == 4;
            for (unsigned index = 1; index < receiptCount; ++index)
                modifiedPending = modifiedPending && receipts[index].Read().outcome == HostOutcome::Pending;
            if (!expect(
                    modifiedPending && !owner->_polygonalPressInput.Closed() &&
                        owner->_polygonalPressInput.PendingCount() >= 6 && owner->_session.Generation() == generation &&
                        owner->_session.history.CurrentRevision() == revision && !owner->_session.PendingPixelMove(),
                    "every-modified-corner-has-distinct-pending-descriptor-with-no-pixel-writer-or-inline-edit"))
                return 5;
            if (!expect(
                    until(
                        [&]() noexcept
                        {
                            return owner->_polygonalPressState && owner->_polygonalPressState->waiting &&
                                   owner->_polygonalPressInput.PendingCount() == 0 && !owner->_activeHostRequest &&
                                   owner->_hostRequests.Size() == 0;
                        }) &&
                        owner->_session.PendingLasso() && owner->_session.PendingLasso()->PointCount() == 4 &&
                        owner->_session.PendingLasso()->mode == mode && owner->_polygonalPressState->drawing &&
                        owner->_polygonalPressParents == 1 && owner->_polygonalPressState->captures == 1 &&
                        (pending || owner->_polygonalPressState->original == original) &&
                        !owner->_session.PendingPixelMove() && !owner->_session.IsMovingSelection() &&
                        !owner->_historyWork.Busy() && DispatcherPixels(owner->_session, SelectionMode::New, true),
                    "modified-corners-extend-one-original-draft-with-captured-mode-and-unchanged-committed-selection"))
                return 5;
            bool successful = true;
            for (unsigned index = 0; index < receiptCount; ++index)
                successful = successful && receipts[index].Read().outcome == HostOutcome::Succeeded;
            if (!expect(successful && owner->_session.Generation() == generation &&
                            owner->_session.history.CurrentRevision() == revision &&
                            owner->_session.history.UndoCount() == 0,
                        "all-four-corner-receipts-succeed-before-any-selection-history-commit"))
                return 5;
            const auto ticket = owner->_polygonalPressState->original;
            key(VK_RETURN);
            if (!expect(receiptCount == 5 && receipts[4].Read().outcome == HostOutcome::Pending &&
                            owner->_polygonalPressState->original == ticket && owner->_session.PendingLasso(),
                        "actual-enter-keeps-original-ticket-until-terminal-acknowledgement"))
                return 5;
            if (!expect(settle(), "modified-corners-original-terminal-and-retirement-settle"))
                return 5;
            successful = true;
            for (unsigned index = 0; index < receiptCount; ++index)
                successful = successful && receipts[index].Read().outcome == HostOutcome::Succeeded;
            if (!expect(
                    successful && DispatcherPixels(owner->_session, mode) &&
                        owner->_session.Generation() == generation + 1 && owner->_session.history.UndoCount() == 1 &&
                        owner->_session.history.UndoName() == L"Polygonal Lasso" && !owner->_session.PendingLasso() &&
                        !owner->_session.PendingPixelMove() && owner->_polygonalPressParents == 1 &&
                        !owner->_polygonalPressInput.Busy() && !owner->_historyWork.Busy(),
                    "all4096-literal-centers-use-first-mode-with-one-polygonal-history-entry"))
                return 5;
            owner->_session.Undo();
            if (!expect(DispatcherPixels(owner->_session, SelectionMode::New, true),
                        "modified-corner-undo-restores-original-selection"))
                return 5;
            owner->_session.Redo();
            if (!expect(DispatcherPixels(owner->_session, mode), "modified-corner-redo-restores-literal-result"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "modified-corner-joined-shutdown-refunds-original-strong-and-weak-owners"))
                return 5;
            ++reached;
            continue;
        }
        if (name == "scheduler-job-slot-exhaustion")
        {
            wil::unique_handle lastStarted{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            if (!expect(lastStarted && release, "scheduler-capacity-events-created"))
                return 5;
            PolygonalSchedulerCapacityGate gate{lastStarted.get(), release.get()};
            // Join the borrowed gate on every early exit before its stack lifetime ends.
            const auto joinOnExit = wil::scope_exit(
                [&]() noexcept
                {
                    (void)SetEvent(release.get());
                    owner->Shutdown();
                });
            std::array<JobTicket, JobScheduler::kCapacity> acceptedTickets{};
            bool admittedAll = true;
            for (std::size_t index = 0; index < JobScheduler::kCapacity; ++index)
            {
                std::unique_ptr<Job> job =
                    std::make_unique<PolygonalSchedulerCapacityJob>(gate, index + 1 == JobScheduler::kCapacity);
                if (owner->_jobs.Submit(job, {}, acceptedTickets[index]) < 0 || job)
                {
                    admittedAll = false;
                    break;
                }
            }
            const bool blockerEntered = admittedAll && WaitForSingleObject(lastStarted.get(), 10000) == WAIT_OBJECT_0;
            if (!expect(blockerEntered && gate.started.load(std::memory_order_relaxed) >= 1,
                        "128-accepted-jobs-fill-owned-slots-with-last-worker-event-gated"))
                return 5;
            click({7, 1});
            const bool acceptedPrefixPending = receiptCount == 1 && receipts[0].Read().outcome == HostOutcome::Pending;
            // Run the already registered owner continuation while the accepted completions remain undrained.
            // The ordinary fixture pump drains completions before running UI actions and would free capacity.
            owner->_continuationAction.Run();
            const auto refused = owner->_polygonalPressLastReceipt.Read();
            if (!expect(acceptedPrefixPending && refused.outcome == HostOutcome::Failed &&
                            refused.error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)),
                        "accepted-prefix-source-slot-exhaustion-returns-exact-not-enough-quota"))
                return 5;
            if (!expect(owner->_session.Generation() == generation &&
                            owner->_session.history.CurrentRevision() == revision &&
                            owner->_session.history.UndoCount() == 0 && !owner->_historyWork.Busy() &&
                            !owner->_session.PendingLasso() && owner->_polygonalPressState &&
                            !owner->_polygonalPressState->controller && owner->_polygonalPressState->runs == 0 &&
                            owner->_polygonalPressState->captures == 0 && !owner->_polygonalPressState->opened &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true) &&
                            owner->_polygonalPressParents == 0,
                        "refusal-preserves-document-history-and-admits-no-polygonal-source-or-writer"))
                return 5;
            (void)SetEvent(release.get());
            if (!expect(settle() && owner->_jobs.IsIdle() &&
                            gate.completed.load(std::memory_order_relaxed) == JobScheduler::kCapacity &&
                            gate.failed.load(std::memory_order_relaxed) == 0,
                        "accepted-predecessor-jobs-survive-refusal-and-event-gated-settlement-refunds-slots"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "scheduler-exhaustion-joined-close-refunds-all-owned-work"))
                return 5;
            ++reached;
            continue;
        }
        const bool inside = name == "inside-queued";
        const bool escape = name == "escape-before-controller";
        const bool zero = name == "backspace-before-controller";
        const bool joined = name == "joined-close";
        const bool live = name == "save-live" || name == "tool-live" || name == "leave-zoom-live" ||
                          name == "space-pan-live" || name == "right-ignore-live" || originalExport ||
                          commandDeselect || commandUndo || commandRedo || heldHistory || headerOffset || headerKind;
        const double left = mode == SelectionMode::Subtract ? 3 : 7;
        if (name == "outside-replaced-parked" || name == "outside-cleared-parked")
        {
            click({7, 1});
            click({11, 1});
            click({11, 5});
            click({7, 5});
            if (!expect(receiptCount == 4 && receipts[0].Read().outcome == HostOutcome::Pending &&
                            !owner->_polygonalPressState,
                        "physical-corner-prefix-is-recorded-before-original-controller"))
                return 5;
            if (!expect(until(
                            [&]() noexcept
                            {
                                return owner->_polygonalPressState && owner->_polygonalPressState->waiting &&
                                       owner->_polygonalPressInput.PendingCount() == 0 && !owner->_activeHostRequest &&
                                       owner->_hostRequests.Size() == 0;
                            }),
                        "all-accepted-corners-and-up-park-with-no-remaining-input-or-host-descriptor"))
                return 5;
            const auto originalTicket = owner->_polygonalPressState->original;
            const auto originalSerial = owner->_session.DocumentSerial();
            bool acknowledged = true;
            for (unsigned i = 0; i < receiptCount; ++i)
                acknowledged = acknowledged && receipts[i].Read().outcome == HostOutcome::Succeeded;
            if (!expect(acknowledged && owner->_polygonalPressState->controller && owner->_session.PendingLasso() &&
                            owner->_session.PendingLasso()->PointCount() == 4 && owner->_polygonalPressParents == 1 &&
                            !owner->_historyWork.Busy(),
                        "one-live-original-and-four-corner-receipts-are-stable-before-foreign-document"))
                return 5;
            const bool cleared = name == "outside-cleared-parked";
            std::optional<Guid> foreignActive;
            if (cleared)
                owner->_session.ClearDocument();
            else
            {
                DocumentFixture foreign;
                foreign.width = foreign.height = 64;
                auto foreignLayer = ImageLayer::Blank(L"Foreign parked replacement", {64, 64});
                foreignActive = foreignLayer.id;
                foreign.layers.push_back(std::move(foreignLayer));
                foreign.selection = DocumentSelection{
                    false, SelectionLoops{std::vector<SelectionLoop>{{{20, 20}, {24, 20}, {24, 24}, {20, 24}}}}};
                owner->_session.ReplaceDocument(std::move(foreign).Seal(), foreignActive);
            }
            owner->_session.history.Reset();
            const auto foreignGeneration = owner->_session.Generation();
            const auto foreignRevision = owner->_session.history.CurrentRevision();
            owner->DocumentChanged();
            if (!expect(owner->_session.DocumentSerial() != originalSerial &&
                            owner->_polygonalPressInput.PendingCount() == 0,
                        "foreign-document-change-adds-no-raw-input-to-the-parked-family"))
                return 5;
            if (!expect(until(
                            [&]() noexcept
                            {
                                return owner->_polygonalPressState && !owner->_polygonalPressState->controller &&
                                       !owner->_polygonalPressInput.Busy();
                            }),
                        "document-notification-alone-joins-original-and-family-without-another-pointer-event"))
                return 5;
            if (!expect(settle(), "cancelled-original-and-new-display-retirement-settle-on-existing-events"))
                return 5;
            acknowledged = true;
            for (unsigned i = 0; i < receiptCount; ++i)
                acknowledged = acknowledged && receipts[i].Read().outcome == HostOutcome::Succeeded;
            if (!expect(acknowledged && owner->_polygonalPressState->original == originalTicket &&
                            owner->_polygonalPressState->completed &&
                            owner->_polygonalPressState->outcome.status == JobStatus::Cancelled &&
                            owner->_polygonalPressParents == 1 && !owner->_session.PendingLasso() &&
                            !owner->_session.IsMovingSelection() && !owner->_historyWork.Busy() &&
                            owner->_session.Generation() == foreignGeneration &&
                            owner->_session.history.CurrentRevision() == foreignRevision &&
                            owner->_session.history.UndoCount() == 0 &&
                            owner->_session.history.CaptureState().Depth() == 0,
                        "exact-old-ticket-cancels-without-rewriting-new-history-or-acknowledged-receipts"))
                return 5;
            if (!expect(cleared ? !owner->_session.Document() && !owner->_session.ActiveLayerID()
                                : owner->_session.ActiveLayerID() == foreignActive &&
                                      DispatcherPixels(owner->_session, SelectionMode::New, true, 20, 20, 24, 24),
                        "absent-or-foreign-document-and-all4096-selection-centers-remain-exact"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "document-replacement-and-joined-close-refund-every-original-owner"))
                return 5;
            ++reached;
            continue;
        }
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
                                refused.error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_BUSY)) &&
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
                            owner->_session.Generation() == generation &&
                            owner->_session.history.UndoCount() == (commandUndo ? 1 : 0),
                        "interactive-draft-releases-host-barrier-and-keeps-original-admission"))
                return 5;
        }
        if (heldHistory)
        {
            struct Gate final
            {
                wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
                wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
                std::atomic<unsigned> visits{0};
                std::atomic<DWORD> waited{WAIT_FAILED};
            } gate;
            // The worker borrows only event/scalar state. Join before destroying that stack state on every exit.
            const auto joinGate = wil::scope_exit(
                [&]() noexcept
                {
                    (void)SetEvent(gate.release.get());
                    owner->Shutdown();
                });
            const bool parked = until(
                [&]() noexcept
                {
                    return owner->_polygonalPressState && owner->_polygonalPressState->waiting &&
                           owner->_polygonalPressInput.PendingCount() == 0 && !owner->_activeHostRequest &&
                           owner->_hostRequests.Size() == 0;
                });
            bool corners = receiptCount == 4;
            for (unsigned i = 0; i < receiptCount; ++i)
                corners = corners && receipts[i].Read().outcome == HostOutcome::Succeeded;
            if (!expect(gate.entered && gate.release && parked && corners && owner->_polygonalPressState->controller &&
                            owner->_session.PendingLasso() && owner->_session.PendingLasso()->PointCount() == 4 &&
                            owner->_polygonalPressParents == 1 && !owner->_historyWork.Busy(),
                        "four-corners-and-up-park-before-worker-preparation-gate"))
                return 5;
            const auto ticket = owner->_polygonalPressState->original;
            owner->_polygonalPressState->workObserver = &gate;
            owner->_polygonalPressState->workCheckpoint =
                [](void* raw, SelectionOutlineGestureState::WorkStage stage) noexcept
            {
                if (stage != SelectionOutlineGestureState::WorkStage::DrawingPrepared)
                    return;
                auto& held = *static_cast<Gate*>(raw);
                held.visits.fetch_add(1, std::memory_order_release);
                (void)SetEvent(held.entered.get());
                held.waited.store(WaitForSingleObject(held.release.get(), 10000), std::memory_order_release);
            };
            key(VK_RETURN);
            const auto terminal = receipts[receiptCount - 1];
            if (!expect(
                    WaitForColorWorkTestEvent(owner->_jobs, gate.entered.get(), DispatchMessageThunk, owner.get()) &&
                        gate.visits.load(std::memory_order_acquire) == 1 &&
                        terminal.Read().outcome == HostOutcome::Pending && owner->_historyWork.Busy() &&
                        owner->_polygonalPressState->original == ticket && owner->_polygonalPressState->captures == 1 &&
                        owner->_session.Generation() == generation &&
                        owner->_session.history.CurrentRevision() == revision,
                    "actual-original-worker-holds-prepared-close-before-any-selection-publication"))
                return 5;
            const auto previousHistory = owner->_session.history.CaptureAuthority();
            const auto previousEditor = owner->_session.CaptureHistoryRestoreAuthority();
            const auto previousCrop = owner->_session.CropFrame();
            const std::optional<CropRect> foreignCrop{CropRect{2, 3, 59, 57}};
            const Raster originalMask = heldMask ? owner->_session.ActiveLayer()->mask->asset.image : Raster{};
            if (heldMask)
                owner->_session.SelectLayerTarget(active, true);
            else if (heldCrop)
                owner->_session.SetCropFrame(foreignCrop);
            else
                owner->_session.history.Begin(L"Foreign Begin while Polygonal prepares", owner->_session.Document(),
                                              active);
            const auto foreign = owner->_session.history.CaptureAuthority();
            const bool changedAuthority =
                heldTarget
                    ? !owner->_session.CanApplyHistoryRestore(previousEditor) &&
                          owner->_session.history.IsCurrent(previousHistory) &&
                          (heldMask ? owner->_session.IsMaskSelected() && owner->_session.ActiveLayerID() == active &&
                                          owner->_session.ActiveLayer()->mask->asset.image == originalMask
                                    : owner->_session.CropFrame() == foreignCrop)
                    : !owner->_session.history.IsCurrent(previousHistory);
            if (!expect(changedAuthority && owner->_session.history.IsCurrent(foreign) &&
                            owner->_session.history.CaptureState().Depth() == (heldTarget ? 0 : 1) &&
                            owner->_session.Generation() == generation &&
                            owner->_session.history.CurrentRevision() == revision &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true),
                        heldMask ? "foreign-mask-target-changes-editor-authority-with-exact-pixels-history-and-document"
                        : heldCrop
                            ? "foreign-crop-changes-editor-authority-with-exact-history-and-document-preserved"
                            : "foreign-pending-history-changes-authority-without-document-generation-or-revision"))
                return 5;
            (void)SetEvent(gate.release.get());
            const bool completed = until([&]() noexcept { return terminal.Read().outcome != HostOutcome::Pending; });
            const auto result = terminal.Read();
            if (!completed || result.outcome != HostOutcome::Failed || result.error != E_ABORT)
                std::fprintf(stderr,
                             "DIAG held-polygonal-history complete=%d outcome=%d error=0x%08X status=%d "
                             "stateError=0x%08X depth=%u pending=%d family=%d\n",
                             completed, static_cast<int>(result.outcome), static_cast<unsigned>(result.error),
                             static_cast<int>(owner->_polygonalPressState->outcome.status),
                             static_cast<unsigned>(owner->_polygonalPressState->outcome.error),
                             static_cast<unsigned>(owner->_session.history.CaptureState().Depth()),
                             static_cast<bool>(owner->_session.PendingLasso()), owner->_polygonalPressInput.Busy());
            if (!expect(completed && result.outcome == HostOutcome::Failed && result.error == E_ABORT &&
                            gate.waited.load(std::memory_order_acquire) == WAIT_OBJECT_0,
                        heldMask   ? "late-foreign-mask-refuses-original-prepared-result-with-exact-abort"
                        : heldCrop ? "late-foreign-crop-refuses-original-prepared-result-with-exact-abort"
                                   : "late-foreign-history-refuses-original-prepared-result-with-exact-abort"))
                return 5;
            corners = true;
            for (unsigned i = 0; i < 4; ++i)
                corners = corners && receipts[i].Read().outcome == HostOutcome::Succeeded;
            if (!expect(settle() && corners && owner->_polygonalPressState->original == ticket &&
                            owner->_polygonalPressState->completed && !owner->_polygonalPressState->controller &&
                            owner->_polygonalPressParents == 1 && !owner->_session.PendingLasso() &&
                            !owner->_polygonalPressInput.Busy() && !owner->_activeHostRequest &&
                            owner->_hostRequests.Size() == 0 && !owner->_historyWork.Busy(),
                        "exact-old-ticket-and-successful-corners-join-without-old-draft-or-orphan-family"))
                return 5;
            if (!expect(owner->_session.history.IsCurrent(foreign) &&
                            owner->_session.history.CaptureState().Depth() == (heldTarget ? 0 : 1) &&
                            (!heldCrop || owner->_session.CropFrame() == foreignCrop) &&
                            (!heldMask || owner->_session.IsMaskSelected() &&
                                              owner->_session.ActiveLayerID() == active &&
                                              owner->_session.ActiveLayer()->mask->asset.image == originalMask) &&
                            owner->_session.Generation() == generation &&
                            owner->_session.history.CurrentRevision() == revision &&
                            owner->_session.history.UndoCount() == 0 &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true),
                        heldMask   ? "refusal-preserves-foreign-mask-target-and-literal-selection-history-pixels"
                        : heldCrop ? "refusal-preserves-exact-foreign-crop-history-and-all4096-prior-selection-centers"
                                   : "refusal-preserves-foreign-transaction-and-all4096-original-selection-centers"))
                return 5;
            if (heldMask)
                owner->_session.SelectLayerTarget(active, false);
            else if (heldCrop)
                owner->_session.CancelCrop();
            else
                owner->_session.history.End(owner->_session.Document(), active);
            if (!expect(owner->_session.history.CaptureState().Depth() == 0 &&
                            owner->_session.history.UndoCount() == 0 &&
                            owner->_session.history.CurrentRevision() == revision &&
                            (!heldCrop || owner->_session.CropFrame() == previousCrop) &&
                            (!heldMask || !owner->_session.IsMaskSelected()),
                        heldMask   ? "foreign-owner-alone-restores-image-target-without-editor-history-entry"
                        : heldCrop ? "foreign-owner-alone-clears-its-crop-without-editor-history-entry"
                                   : "foreign-owner-still-closes-its-exact-noop-transaction-with-no-editor-entry"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "held-worker-and-foreign-history-joined-close-refund-all-original-owners"))
                return 5;
            ++reached;
            continue;
        }
        if (commandDeselect || commandUndo || commandRedo)
        {
            const bool parked = until(
                [&]() noexcept
                {
                    return owner->_polygonalPressState && owner->_polygonalPressState->waiting &&
                           owner->_polygonalPressInput.PendingCount() == 0 && !owner->_activeHostRequest &&
                           owner->_hostRequests.Size() == 0;
                });
            const auto originalTicket = owner->_polygonalPressState->original;
            const auto originalRuns = owner->_polygonalPressState->runs;
            const auto originalCaptures = owner->_polygonalPressState->captures;
            const auto originalParents = owner->_polygonalPressParents;
            std::array<ContinuationReceipt::Result, 4> cornerResults{};
            bool cornersSucceeded = receiptCount == cornerResults.size();
            for (std::size_t index = 0; index < cornerResults.size() && cornersSucceeded; ++index)
            {
                cornerResults[index] = receipts[index].Read();
                cornersSucceeded = cornerResults[index].outcome == HostOutcome::Succeeded;
            }
            if (!expect(parked && cornersSucceeded && owner->_polygonalPressState->controller &&
                            owner->_polygonalPressState->waiting && owner->_polygonalPressState->drawing &&
                            owner->_polygonalPressParents == 1 && !owner->_historyWork.Busy(),
                        "parked-outside-draft-has-four-already-successful-corner-receipts"))
                return 5;

            const int command = commandDeselect ? ChromeHost::kCommandDeselect
                                : commandUndo   ? ChromeHost::kCommandUndo
                                                : ChromeHost::kCommandRedo;
            const auto commandSelectionBefore = owner->_session.Selection();
            const auto commandRevisionBefore = owner->_session.history.CurrentRevision();
            const auto commandGenerationBefore = owner->_session.Generation();
            const int commandUndoCountBefore = owner->_session.history.UndoCount();
            const auto commandUndoNameBefore = owner->_session.history.UndoName();
            const auto commandRedoNameBefore = owner->_session.history.RedoName();
            ContinuationReceipt receipt;
            if (headerDeselect)
            {
                if (headerWidget)
                {
                    owner->RenderFrame(); // Normal arranged frame supplies lazy UIA, not a new chrome accessor.
                    auto* rawRoot = owner->_chrome->AccessibilityRoot();
                    wil::com_ptr_nothrow<IRawElementProviderFragment> root;
                    if (!expect(rawRoot && SUCCEEDED(rawRoot->QueryInterface(IID_PPV_ARGS(root.put()))) && root,
                                "real-header-widget-has-normal-window-accessibility-root"))
                        return 5;
                    const auto caption = LoadStringResource(nullptr, IDS_SELECTION_DESELECT);
                    unsigned remaining = 512;
                    auto button = FindNamedEnabledButton(root.get(), caption, remaining);
                    UiaRect bounds{};
                    POINT client{};
                    const bool rectangle = button && SUCCEEDED(button->get_BoundingRectangle(&bounds)) &&
                                           bounds.width > 0 && bounds.height > 0;
                    if (rectangle)
                        client = {static_cast<LONG>(std::lround(bounds.left + bounds.width * 0.5)),
                                  static_cast<LONG>(std::lround(bounds.top + bounds.height * 0.5))};
                    const POINT screen = client;
                    const bool mapped = rectangle && ScreenToClient(owner->_window.get(), &client);
                    if (!expect(!caption.empty() && remaining > 0 && mapped &&
                                    !owner->_chrome->IsCanvasPoint(static_cast<float>(client.x),
                                                                   static_cast<float>(client.y)),
                                "bounded-uia-tree-finds-enabled-localized-header-button-outside-canvas"))
                        return 5;
                    wil::com_ptr_nothrow<IRawElementProviderFragment> hit;
                    if (!expect(SUCCEEDED(rawRoot->ElementProviderFromPoint(screen.x, screen.y, hit.put())) &&
                                    IsNamedEnabledButton(hit.get(), caption),
                                "uia-point-hit-confirms-real-enabled-deselect-widget"))
                        return 5;
                    (void)SendMessageW(owner->_window.get(), WM_LBUTTONDOWN, MK_LBUTTON,
                                       MAKELPARAM(client.x, client.y));
                    (void)SendMessageW(owner->_window.get(), WM_LBUTTONUP, 0, MAKELPARAM(client.x, client.y));
                    // All owned providers die before command pumping or owner shutdown below.
                }
                else
                    owner->OnSelectionAction(SelectionAction::Deselect, 0.0);
                const auto* queued = owner->_hostRequests.PendingAt(0);
                const auto* input = queued ? std::get_if<HostCommandInput>(&queued->input.Input()) : nullptr;
                const bool queuedCommand = owner->_hostRequests.Size() == 1 && !owner->_activeHostRequest && queued &&
                                           input && input->command == ChromeHost::kCommandDeselect &&
                                           queued->documentSerial == owner->_session.DocumentSerial() &&
                                           queued->ordinal > owner->_polygonalPressState->lastConsumedOrdinal;
                if (!expect(queuedCommand, "header-deselect-callback-admits-one-matching-later-host-command-receipt"))
                    return 5;
                receipt = queued->input.Receipt(); // Retain this exact admission; do not enqueue a second command.
            }
            else if (keyboardCommand)
            {
                const UINT virtualKey = commandDeselect ? 'D' : 'Z';
                const UINT modifiers = commandRedo ? MK_CONTROL | MK_SHIFT : MK_CONTROL;
                KeyPress diagnosticPress;
                diagnosticPress.virtualKey = static_cast<std::uint16_t>(virtualKey);
                diagnosticPress.ctrl = true;
                diagnosticPress.shift = commandRedo;
                const auto diagnosticState = owner->CurrentKeyState();
                const auto diagnosticMatch = owner->_bindings.Match(diagnosticPress, diagnosticState);
                if (!expect(owner->HandleAccelerator(virtualKey, modifiers),
                            "window-keydown-keyup-routes-configured-default-command"))
                    return 5;
                const auto* queued = owner->_hostRequests.PendingAt(0);
                const auto* input = queued ? std::get_if<HostCommandInput>(&queued->input.Input()) : nullptr;
                const bool queuedCommand = owner->_hostRequests.Size() == 1 && !owner->_activeHostRequest && queued &&
                                           input && input->command == command &&
                                           queued->documentSerial == owner->_session.DocumentSerial() &&
                                           queued->ordinal > owner->_polygonalPressState->lastConsumedOrdinal &&
                                           queued->input.Receipt().Read().outcome == HostOutcome::Pending;
                if (!queuedCommand)
                    std::fprintf(stderr,
                                 "DIAG polygonal-keyboard name=%s match=%d matchedCommand=%d expected=%d focus=%d "
                                 "size=%zu active=%d queued=%d input=%d command=%d serial=%llu currentSerial=%llu "
                                 "ordinal=%llu consumed=%llu receipt=%d executing=%d registered=%d completed=%d "
                                 "stopping=%d generationBefore=%llu generation=%llu pendingLasso=%d historyBusy=%d\n",
                                 name.data(), static_cast<int>(diagnosticMatch.result), diagnosticMatch.command,
                                 command, static_cast<int>(diagnosticState.focus), owner->_hostRequests.Size(),
                                 static_cast<bool>(owner->_activeHostRequest), queued != nullptr, input != nullptr,
                                 input ? input->command : -1,
                                 static_cast<unsigned long long>(queued ? queued->documentSerial : 0),
                                 static_cast<unsigned long long>(owner->_session.DocumentSerial()),
                                 static_cast<unsigned long long>(queued ? queued->ordinal : 0),
                                 static_cast<unsigned long long>(owner->_polygonalPressState->lastConsumedOrdinal),
                                 queued ? static_cast<int>(queued->input.Receipt().Read().outcome) : -1,
                                 owner->_hostExecuting, owner->_hostSourceRegistered,
                                 owner->_polygonalPressState->completed, owner->_polygonalPressStopping,
                                 static_cast<unsigned long long>(commandGenerationBefore),
                                 static_cast<unsigned long long>(owner->_session.Generation()),
                                 owner->_session.PendingLasso().has_value(), owner->_historyWork.Busy());
                if (!expect(queuedCommand, "routed-keyboard-command-has-matching-queued-receipt-and-later-ordinal"))
                    return 5;
                receipt = queued->input.Receipt(); // Own the observer before any queue mutation; never dispatch twice.
            }
            else
                receipt = owner->QueueHostInput(HostCommandInput{command});
            if (!expect(receipt.Read().outcome == HostOutcome::Pending,
                        "real-host-command-dispatch-admits-ordered-request"))
                return 5;
            owner->_continuationAction.Run();
            if (!expect(owner->_polygonalPressStopping && owner->_polygonalPressStamp &&
                            owner->_hostCommitStamp == owner->_polygonalPressStamp &&
                            owner->_polygonalPressState->controller && !owner->_polygonalPressState->completed &&
                            owner->_polygonalPressState->original == originalTicket && owner->_historyWork.Busy() &&
                            receipt.Read().outcome == HostOutcome::Pending &&
                            owner->_session.Selection() == commandSelectionBefore &&
                            owner->_session.history.CurrentRevision() == commandRevisionBefore &&
                            owner->_session.history.UndoCount() == commandUndoCountBefore &&
                            owner->_session.history.UndoName() == commandUndoNameBefore &&
                            owner->_session.history.RedoName() == commandRedoNameBefore &&
                            owner->_session.Generation() == commandGenerationBefore,
                        "dispatcher-stops-same-original-ticket-and-holds-command-behind-cancel-ack"))
                return 5;
            if (!expect(until([&]() noexcept { return receipt.Read().outcome != HostOutcome::Pending; }) &&
                            receipt.Read().outcome == HostOutcome::Succeeded &&
                            owner->_polygonalPressState->completed && owner->_polygonalPressState->sameTicket &&
                            owner->_polygonalPressState->original == originalTicket &&
                            owner->_polygonalPressState->outcome.status == JobStatus::Cancelled &&
                            !owner->_polygonalPressState->controller && !owner->_session.PendingLasso() &&
                            !owner->_polygonalPressInput.Busy() && !owner->_historyWork.Busy() &&
                            !owner->_activeHostRequest && owner->_hostRequests.Size() == 0,
                        "ordered-command-resolves-only-after-original-writer-cancellation-joins"))
                return 5;
            bool cornersUntouched = true;
            for (std::size_t index = 0; index < cornerResults.size(); ++index)
            {
                const auto after = receipts[index].Read();
                cornersUntouched = cornersUntouched && after.outcome == cornerResults[index].outcome &&
                                   after.error == cornerResults[index].error;
            }
            if (!expect(cornersUntouched && owner->_polygonalPressState->original == originalTicket &&
                            owner->_polygonalPressState->runs == originalRuns &&
                            owner->_polygonalPressState->captures == originalCaptures &&
                            owner->_polygonalPressParents == originalParents,
                        "successful-corner-receipts-stay-fixed-with-no-new-writer-or-capture"))
                return 5;

            const bool expectedSelection = commandDeselect ? !owner->_session.Selection()
                                           : commandRedo   ? owner->_session.Selection() == commandSeedSelection
                                                           : owner->_session.Selection() == commandBaseSelection;
            const bool expectedHistory =
                commandDeselect
                    ? owner->_session.history.UndoCount() == 1 && owner->_session.history.UndoName() == L"Deselect" &&
                          !owner->_session.history.CanRedo()
                : commandUndo ? owner->_session.history.UndoCount() == 0 && owner->_session.history.CanRedo() &&
                                    owner->_session.history.RedoName() == L"Polygonal command seed"
                              : owner->_session.history.UndoCount() == 1 &&
                                    owner->_session.history.UndoName() == L"Polygonal command seed" &&
                                    !owner->_session.history.CanRedo();
            const bool expectedRevision =
                commandDeselect ? owner->_session.history.CurrentRevision() != commandRevisionBefore
                : commandUndo   ? owner->_session.history.CurrentRevision() == commandBaseRevision
                                : owner->_session.history.CurrentRevision() == commandSeedRevision;
            if (!expectedSelection || !expectedHistory || !expectedRevision ||
                owner->_session.Generation() != commandGenerationBefore + 1)
                std::fprintf(
                    stderr,
                    "DIAG polygonal-command name=%s selection=%d history=%d revision=%d generation=%llu before=%llu "
                    "undo=%d beforeUndo=%d redo=%d receipt=%d ticket=%d parents=%llu captures=%u runs=%u\n",
                    name.data(), expectedSelection, expectedHistory, expectedRevision,
                    static_cast<unsigned long long>(owner->_session.Generation()),
                    static_cast<unsigned long long>(commandGenerationBefore), owner->_session.history.UndoCount(),
                    commandUndoCountBefore, owner->_session.history.CanRedo(), static_cast<int>(receipt.Read().outcome),
                    owner->_polygonalPressState->original == originalTicket,
                    static_cast<unsigned long long>(owner->_polygonalPressParents),
                    owner->_polygonalPressState->captures, owner->_polygonalPressState->runs);
            if (!expect(expectedSelection && expectedHistory && expectedRevision &&
                            owner->_session.Generation() == commandGenerationBefore + 1 &&
                            (commandDeselect ? commandUndoCountBefore == 0 && commandUndoNameBefore.empty() &&
                                                   commandRedoNameBefore.empty()
                             : commandUndo
                                 ? commandUndoCountBefore == 1 && commandUndoNameBefore == L"Polygonal command seed" &&
                                       commandRedoNameBefore.empty()
                                 : commandUndoCountBefore == 0 && commandUndoNameBefore.empty() &&
                                       commandRedoNameBefore == L"Polygonal command seed"),
                        "literal-command-selection-history-revision-and-generation-match"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes() && !owner->_polygonalPressInput.Busy(),
                        "ordered-command-joined-close-refunds-original-and-command-work"))
                return 5;
            ++reached;
            continue;
        }
        if (headerKind)
        {
            const bool parked = until(
                [&]() noexcept
                {
                    return owner->_polygonalPressState && owner->_polygonalPressState->waiting &&
                           owner->_polygonalPressInput.PendingCount() == 0 && !owner->_activeHostRequest &&
                           owner->_hostRequests.Size() == 0;
                });
            std::array<ContinuationReceipt::Result, 4> corners{};
            bool succeeded = receiptCount == corners.size();
            for (std::size_t index = 0; index < corners.size() && succeeded; ++index)
            {
                corners[index] = receipts[index].Read();
                succeeded = corners[index].outcome == HostOutcome::Succeeded;
            }
            if (!expect(parked && succeeded && owner->_session.lassoKind == LassoKind::Polygonal &&
                            owner->_session.PendingLasso() && owner->_session.PendingLasso()->PointCount() == 4 &&
                            owner->_polygonalPressState->controller && owner->_polygonalPressState->drawing &&
                            owner->_polygonalPressParents == 1 && !owner->_historyWork.Busy(),
                        "header-kind-starts-with-four-acknowledged-corners-on-parked-original"))
                return 5;
            const auto original = owner->_polygonalPressState->original;
            const auto* controller = owner->_polygonalPressState->controller;
            const auto captures = owner->_polygonalPressState->captures;
            const auto runs = owner->_polygonalPressState->runs;
            const auto reserved = owner->_jobs.ReservedMemoryBytes();
            const auto selection = owner->_session.Selection();
            owner->OnSelectionAction(SelectionAction::SetLassoKind, 0.0);
            if (!expect(
                    owner->_session.lassoKind == LassoKind::Freehand && owner->_polygonalPressInput.Closed() &&
                        owner->_polygonalPressInput.PendingCount() == 1 &&
                        owner->_polygonalPressState->controller == controller &&
                        owner->_polygonalPressState->original == original && !owner->_polygonalPressState->completed &&
                        owner->_session.PendingLasso() && owner->_session.PendingLasso()->PointCount() == 4 &&
                        owner->_session.Selection() == selection && owner->_session.Generation() == generation &&
                        owner->_session.history.CurrentRevision() == revision &&
                        owner->_session.history.UndoCount() == 0 && !owner->_historyWork.Busy() &&
                        owner->_polygonalPressState->captures == captures &&
                        owner->_polygonalPressState->runs == runs && owner->_jobs.ReservedMemoryBytes() == reserved &&
                        !owner->_activeHostRequest && !owner->_hostRequests.Size(),
                    "kind-callback-seals-existing-loss-slot-with-no-owner-draft-discard-or-second-job"))
                return 5;
            if (!expect(
                    until([&]() noexcept
                          { return owner->_polygonalPressState->completed && !owner->_polygonalPressInput.Busy(); }) &&
                        owner->_polygonalPressState->sameTicket && owner->_polygonalPressState->original == original &&
                        owner->_polygonalPressState->outcome.status == JobStatus::Cancelled &&
                        !owner->_polygonalPressState->controller && !owner->_session.PendingLasso() &&
                        owner->_session.Selection() == selection && owner->_session.Generation() == generation &&
                        owner->_session.history.CurrentRevision() == revision &&
                        owner->_session.history.UndoCount() == 0 && !owner->_session.history.CanRedo() &&
                        !owner->_historyWork.Busy() && owner->_polygonalPressParents == 1 &&
                        owner->_polygonalPressState->captures == captures &&
                        owner->_session.lassoKind == LassoKind::Freehand &&
                        DispatcherPixels(owner->_session, SelectionMode::New, true),
                    "same-original-loss-ack-clears-draft-preserving-all4096-selection-centers-and-new-header"))
                return 5;
            bool fixed = true;
            for (std::size_t index = 0; index < corners.size(); ++index)
            {
                const auto after = receipts[index].Read();
                fixed = fixed && after.outcome == corners[index].outcome && after.error == corners[index].error;
            }
            if (!expect(fixed && !owner->_activeHostRequest && !owner->_hostRequests.Size(),
                        "kind-cancellation-keeps-successful-corner-receipts-fixed-with-no-new-descriptor"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes() && !owner->_polygonalPressInput.Busy(),
                        "kind-cancellation-joined-close-refunds-all-original-strong-and-weak-owners"))
                return 5;
            ++reached;
            continue;
        }
        if (headerOffset)
        {
            const bool parked = until(
                [&]() noexcept
                {
                    return owner->_polygonalPressState && owner->_polygonalPressState->waiting &&
                           owner->_polygonalPressInput.PendingCount() == 0 && !owner->_activeHostRequest &&
                           owner->_hostRequests.Size() == 0;
                });
            std::array<ContinuationReceipt::Result, 4> cornerResults{};
            bool cornersSucceeded = receiptCount == cornerResults.size();
            for (std::size_t index = 0; index < cornerResults.size() && cornersSucceeded; ++index)
            {
                cornerResults[index] = receipts[index].Read();
                cornersSucceeded = cornerResults[index].outcome == HostOutcome::Succeeded;
            }
            if (!expect(parked && owner->_polygonalPressState && cornersSucceeded &&
                            owner->_polygonalPressState->waiting && owner->_polygonalPressState->drawing &&
                            owner->_polygonalPressState->controller != nullptr && owner->_polygonalPressParents == 1 &&
                            !owner->_historyWork.Busy() && !owner->_activeHostRequest && !owner->_hostRequests.Size() &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true),
                        "header-offset-starts-from-parked-four-corner-original-with-literal4096-selection-centers"))
                return 5;
            const auto originalSelection = owner->_session.Selection();
            const auto originalRevision = owner->_session.history.CurrentRevision();
            const auto originalGeneration = owner->_session.Generation();
            const auto originalTicket = owner->_polygonalPressState->original;
            const auto* const originalController = owner->_polygonalPressState->controller;
            const auto originalRuns = owner->_polygonalPressState->runs;
            const auto originalCaptures = owner->_polygonalPressState->captures;
            const auto originalParents = owner->_polygonalPressParents;
            const auto originalReservedBytes = owner->_jobs.ReservedMemoryBytes();

            PolygonalHeaderPrompts headerPrompts(QuietPrompts());
            owner->_prompts = &headerPrompts;
            const auto restorePrompts = wil::scope_exit(
                [&]() noexcept
                {
                    owner->Shutdown();
                    owner->_prompts = &QuietPrompts();
                });
            const auto action = name == "expand-header-live" ? SelectionAction::Expand : SelectionAction::Contract;
            owner->OnSelectionAction(action, 0.0);
            if (!expect(headerPrompts.postNoticeCount == 1 && headerPrompts.lastStringId == IDS_STATUS_BUSY,
                        "header-offset-posts-exactly-one-localized-busy-notice"))
                return 5;
            bool cornersUnchanged = true;
            for (std::size_t index = 0; index < cornerResults.size(); ++index)
            {
                const auto after = receipts[index].Read();
                cornersUnchanged = cornersUnchanged && after.outcome == cornerResults[index].outcome &&
                                   after.error == cornerResults[index].error;
            }
            if (!expect(owner->_session.Selection() == originalSelection &&
                            owner->_session.history.CurrentRevision() == originalRevision &&
                            owner->_session.history.UndoCount() == 0 && !owner->_session.history.CanRedo() &&
                            owner->_session.Generation() == originalGeneration && !owner->_historyWork.Busy() &&
                            owner->_polygonalPressInput.Busy() &&
                            owner->_polygonalPressState->controller == originalController &&
                            owner->_polygonalPressState->original == originalTicket &&
                            owner->_polygonalPressState->runs == originalRuns &&
                            owner->_polygonalPressState->captures == originalCaptures &&
                            owner->_polygonalPressParents == originalParents &&
                            owner->_jobs.ReservedMemoryBytes() == originalReservedBytes && cornersUnchanged &&
                            owner->_session.PendingLasso() && owner->_session.PendingLasso()->PointCount() == 4 &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true),
                        "busy-header-offset-preserves-selection-geometry-ticket-admission-and-corner-receipts"))
                return 5;
            if (!expect(!owner->_activeHostRequest && owner->_hostRequests.Size() == 0 && !owner->_historyWork.Busy(),
                        "busy-header-offset-admits-no-host-descriptor-or-history-producer"))
                return 5;

            key(VK_RETURN);
            if (!expect(settle() && owner->_polygonalPressState && owner->_polygonalPressState->completed &&
                            owner->_polygonalPressState->sameTicket &&
                            owner->_polygonalPressState->original == originalTicket &&
                            owner->_polygonalPressParents == originalParents &&
                            owner->_session.Generation() == originalGeneration + 1 &&
                            owner->_session.history.UndoCount() == 1 &&
                            owner->_session.history.CurrentRevision() != originalRevision &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true, 7, 1, 11, 5),
                        "enter-commits-exact-outside-rectangle-through-original-ticket-with-one-history-entry"))
                return 5;
            owner->_session.Undo();
            if (!expect(owner->_session.Selection() == originalSelection && owner->_session.history.UndoCount() == 0 &&
                            owner->_session.history.CanRedo() &&
                            owner->_session.Generation() == originalGeneration + 2 &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true),
                        "undo-restores-literal-prior-selection-after-header-refusal"))
                return 5;
            owner->_session.Redo();
            if (!expect(DispatcherPixels(owner->_session, SelectionMode::New, true, 7, 1, 11, 5) &&
                            owner->_session.history.UndoCount() == 1 && !owner->_session.history.CanRedo() &&
                            owner->_session.Generation() == originalGeneration + 3,
                        "redo-restores-literal-outside-selection-after-header-refusal"))
                return 5;
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes() && !owner->_polygonalPressInput.Busy(),
                        "header-offset-enter-undo-redo-join-refunds-original-ticket-reservation"))
                return 5;
            ++reached;
            continue;
        }
        if (originalExport)
        {
            // A corner receipt can acknowledge before its later raw Up resumes and parks the parent.
            // Select the export/picker only after that exact accepted input is consumed, without weakening
            // the post-operation parked-writer assertion (quiet picker cancellation completes inline).
            if (!expect(until(
                            [&]() noexcept
                            {
                                return owner->_polygonalPressState && owner->_polygonalPressState->waiting &&
                                       owner->_polygonalPressInput.PendingCount() == 0 && !owner->_activeHostRequest &&
                                       owner->_hostRequests.Size() == 0;
                            }),
                        "last-corner-and-real-up-are-parked-before-original-export-or-picker-seam"))
                return 5;
            const bool jpeg =
                name == "jpeg-export-original-outside" || name == "jpeg-picker-cancel-original-outside-seam";
            const bool pickerCancel =
                name == "png-picker-cancel-original-outside-seam" || name == "jpeg-picker-cancel-original-outside-seam";
            const auto format = jpeg ? HostWriteFormat::Jpeg : HostWriteFormat::Png;
            const auto path = output / (std::wstring(name.begin(), name.end()) + (jpeg ? L".jpg" : L".png"));
            const std::wstring priorProjectPath = L"before-original-export.prism";
            owner->_documentPath = priorProjectPath;
            owner->_session.history.MarkSaved(Guid::Generate());
            const bool priorDirtyMarker = owner->_session.history.IsModified();
            const auto ticket = owner->_polygonalPressState->original;
            std::optional<std::wstring> selectedPath;
            if (!pickerCancel)
                selectedPath = path.wstring();
            const auto write = owner->QueueHostInput(HostWriteInput{format, false, std::move(selectedPath)});
            const auto wanted = pickerCancel ? HostOutcome::Cancelled : HostOutcome::Succeeded;
            const auto wantedError = pickerCancel ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK;
            if (!expect(until([&]() noexcept { return write.Read().outcome != HostOutcome::Pending; }) &&
                            write.Read().outcome == wanted &&
                            write.Read().error == static_cast<std::int32_t>(wantedError),
                        "original-outside-native-export-or-picker-test-seam-resolves-exact-receipt"))
                return 5;
            if (!owner->_polygonalPressState->controller || !owner->_polygonalPressState->waiting ||
                !owner->_session.PendingLasso() || owner->_session.Generation() != generation ||
                owner->_historyWork.Busy() || !owner->_session.history.IsModified() ||
                owner->_documentPath != priorProjectPath)
                std::printf("DIAG original-export controller=%d waiting=%d pending=%zu lasso=%d points=%zu "
                            "parents=%llu history=%d generation=%llu expected=%llu dirty=%d path=%d\n",
                            owner->_polygonalPressState->controller != nullptr, owner->_polygonalPressState->waiting,
                            owner->_polygonalPressInput.PendingCount(), owner->_session.PendingLasso().has_value(),
                            owner->_session.PendingLasso() ? owner->_session.PendingLasso()->PointCount() : 0,
                            static_cast<unsigned long long>(owner->_polygonalPressParents), owner->_historyWork.Busy(),
                            static_cast<unsigned long long>(owner->_session.Generation()),
                            static_cast<unsigned long long>(generation), owner->_session.history.IsModified(),
                            owner->_documentPath == priorProjectPath);
            if (!expect(owner->_polygonalPressState->controller && owner->_polygonalPressState->waiting &&
                            owner->_polygonalPressState->original == ticket && owner->_session.PendingLasso() &&
                            owner->_session.PendingLasso()->PointCount() == 4 && owner->_polygonalPressParents == 1 &&
                            !owner->_historyWork.Busy() &&
                            DispatcherPixels(owner->_session, SelectionMode::New, true) &&
                            owner->_session.Generation() == generation &&
                            owner->_session.history.CurrentRevision() == revision && priorDirtyMarker &&
                            owner->_session.history.IsModified() && owner->_documentPath == priorProjectPath,
                        "export-or-picker-cancel-preserves-original-ticket-selection-revision-path-and-dirty-marker"))
                return 5;
            if (pickerCancel)
            {
                if (!expect(!std::filesystem::exists(path), "picker-test-seam-cancellation-publishes-no-file"))
                    return 5;
            }
            else
            {
                DecodedImage decoded;
                const auto decode = ImportImageFile(path.c_str(), kMaxLayerPixels, decoded);
                if (!expect(std::filesystem::is_regular_file(path) && decode == ImageCodecStatus::Ok &&
                                decoded.image.Format() == RasterFormat::Bgra8888 && decoded.image.Width() == 64 &&
                                decoded.image.Height() == 64 && decoded.image.HasStorage(),
                            "published-original-export-independently-decodes-as64x64-bgra"))
                    return 5;
                bool matches = true;
                const auto* bytes = decoded.image.Data();
                for (std::size_t pixel = 0; pixel < 64u * 64u && matches; ++pixel)
                {
                    const auto* bgra = bytes + pixel * 4;
                    if (jpeg)
                    {
                        constexpr int kJpegChannelTolerance =
                            6; // Existing default-quality jpeg.flatten-white-matte reference.
                        matches = bgra[3] == 255 &&
                                  std::abs(static_cast<int>(bgra[0]) - 255) <= kJpegChannelTolerance &&
                                  std::abs(static_cast<int>(bgra[1]) - 255) <= kJpegChannelTolerance &&
                                  std::abs(static_cast<int>(bgra[2]) - 255) <= kJpegChannelTolerance;
                    }
                    else
                        matches = bgra[0] == 255 && bgra[1] == 255 && bgra[2] == 255 && bgra[3] == 255;
                }
                if (!expect(matches, jpeg ? "jpeg-decoded-literal-bgra-within-spec-channel-tolerance"
                                          : "png-decoded-literal-bgra-exactly-matches-source"))
                    return 5;
            }
            key(VK_RETURN);
            if (!expect(settle() && owner->_polygonalPressState && owner->_polygonalPressState->completed &&
                            owner->_polygonalPressState->sameTicket && owner->_polygonalPressParents == 1 &&
                            owner->_session.Generation() == generation + 1 &&
                            owner->_session.history.UndoCount() == 1 && owner->_session.history.IsModified() &&
                            owner->_session.history.CurrentRevision() != revision &&
                            owner->_documentPath == priorProjectPath,
                        "one-real-enter-commits-through-original-ticket-with-one-dirty-history-entry"))
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
