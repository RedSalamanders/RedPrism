#include "Application.h"
#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"
#include "ImageCodecs.h"
#include "KeyboardState.h"
#include "ProjectSaveTestGate.h"
#include "ProjectStore.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
bool PolygonalHostFilled(const DocumentSelection& selection, Vec2 point) noexcept
{
    int winding = 0;
    for (const auto& loop : selection.loops.Get())
    {
        if (loop.empty())
            continue;
        auto a = loop.back();
        for (const auto b : loop)
        {
            const auto cross = (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
            if (a.y <= point.y && b.y > point.y && cross > 0)
                ++winding;
            else if (a.y > point.y && b.y <= point.y && cross < 0)
                --winding;
            a = b;
        }
    }
    return winding != 0;
}
bool PolygonalHostPixels(const std::optional<DocumentSelection>& selection, SelectionMode mode) noexcept
{
    if (!selection)
        return false;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            const bool drawn = x >= 2 && x < 8 && y >= 2 && y < 7;
            const bool prior = x >= 1 && x < 5 && y >= 1 && y < 5;
            const bool wanted = mode == SelectionMode::New   ? drawn
                                : mode == SelectionMode::Add ? prior || drawn
                                                             : prior && !drawn;
            if (PolygonalHostFilled(selection.value(), {x + 0.5, y + 0.5}) != wanted)
                return false;
        }
    return true;
}
bool SamePolygonalHostSelection(const std::optional<DocumentSelection>& a,
                                const std::optional<DocumentSelection>& b) noexcept
{
    if (a.has_value() != b.has_value())
        return false;
    if (!a)
        return true;
    if (a.value().antialiased != b.value().antialiased || a.value().Bounds() != b.value().Bounds())
        return false;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (PolygonalHostFilled(a.value(), {x + 0.5, y + 0.5}) !=
                PolygonalHostFilled(b.value(), {x + 0.5, y + 0.5}))
                return false;
    return true;
}
} // namespace

int Application::RunStandalonePolygonalPressSelfTest(HINSTANCE instance) noexcept
{
    KeyboardState::ScopedSynthetic keyboard;
    std::array<BYTE, 256> nativeKeys{};
    if (!GetKeyboardState(nativeKeys.data()))
        return 5;
    auto restoreKeys = wil::scope_exit([&]() noexcept { (void)SetKeyboardState(nativeKeys.data()); });
    unsigned checks = 0;
    unsigned failures = 0;
    unsigned reached = 0;
    const char* family = "setup";
    const char* scenario = "setup";
    const auto expect = [&](bool value, const char* label) noexcept
    {
        ++checks;
        failures += value ? 0u : 1u;
        std::printf("%s polygonal-press-host.%s.%s.%s\n", value ? "PASS" : "FAIL", family, scenario, label);
        return value;
    };
    auto summary = wil::scope_exit(
        [&]() noexcept
        { std::printf("SUMMARY polygonal-press-host checks=%u failures=%u reached=%u\n", checks, failures, reached); });
    constexpr std::array cases{"enter",
                               "near-first",
                               "double-click",
                               "hover-backspace",
                               "save-open",
                               "save-cancel",
                               "save-failed",
                               "prompt-cancel",
                               "tool-cancel",
                               "finish-before-admission",
                               "older-close-younger-enter",
                               "blocked-layer",
                               "header-deselect",
                               "header-kind-cancel",
                               "png-open",
                               "jpeg-open",
                               "png-cancel",
                               "jpeg-cancel",
                               "zoom-live",
                               "pointer-leave-live",
                               "escape-cancel",
                               "backspace-zero",
                               "near-first-two",
                               "double-click-two",
                               "header-mode-live",
                               "harness-dpi-live",
                               "joined-close"};
    const auto output =
        std::filesystem::current_path() / L".build" / L"P1PolygonalFixture" / Guid::Generate().ToString();
    std::error_code pathError;
    std::filesystem::create_directories(output, pathError);
    if (!expect(!pathError, "exclusive-output-directory"))
        return 5;
    for (const auto mode : {SelectionMode::New, SelectionMode::Add, SelectionMode::Subtract})
    {
        family = mode == SelectionMode::New ? "new" : mode == SelectionMode::Add ? "add" : "subtract";
        scenario = "setup";
        ApplicationProcessingTestProbe probe;
        probe.workerCount = 2;
        probe.memory = {[](void*, MemorySnapshot& out) noexcept
                        {
                            const auto bytes = kMemoryAdmissionHeadroomBytes + 2ULL * 1024 * 1024 * 1024;
                            out = {bytes, bytes};
                            return true;
                        },
                        nullptr};
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
                        SUCCEEDED(owner->InitializeChrome()),
                    "hidden-native-warp-owner-and-chrome"))
            return 5;
        if (!expect(owner->EnableCommittedDisplayForTest() && owner->EnableSelectionOutlineForTest() &&
                        owner->EnableLassoDraftDisplayForTest(),
                    "actual-prepared-display-and-draft-owners"))
            return 5;
        owner->_polygonalDrawingTestEnabled = true;
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
                    continue; // A bounded pump may leave acknowledged ready actions; never wait over them.
                const HANDLE wake = owner->_jobs.WakeHandle();
                if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    return false;
            }
            return true;
        };
        for (const std::string_view name : cases)
        {
            scenario = name.data();
            const bool early = name == "finish-before-admission" || name == "older-close-younger-enter";
            const bool duplicate = name == "older-close-younger-enter";
            const bool joined = name == "joined-close";
            const bool zoomLive = name == "zoom-live";
            const bool pointerLeaveLive = name == "pointer-leave-live";
            const bool escapeCancel = name == "escape-cancel";
            const bool backspaceZero = name == "backspace-zero";
            const bool nearFirstTwo = name == "near-first-two";
            const bool doubleClickTwo = name == "double-click-two";
            const bool headerModeLive = name == "header-mode-live";
            const bool harnessDpiLive = name == "harness-dpi-live";
            const bool shortTwo = nearFirstTwo || doubleClickTwo;
            const bool tool = name == "tool-cancel";
            const bool kindCancel = name == "header-kind-cancel";
            const bool deselect = name == "header-deselect";
            const bool save = name == "save-open" || name == "save-cancel" || name == "save-failed";
            const bool exportOpen = name == "png-open" || name == "jpeg-open";
            const bool exportCancel = name == "png-cancel" || name == "jpeg-cancel";
            const bool cancelled = tool || kindCancel || joined || deselect || escapeCancel || nearFirstTwo;
            const bool noPublication = cancelled || backspaceZero;
            if (owner->_polygonalDrawing && owner->_polygonalDrawing->controller)
                return 5;
            owner->_polygonalDrawing.reset();
            owner->_projectSaveTestGate.reset();
            owner->_forcedDiscardAnswer.reset();
            owner->_documentPath.clear();
            DocumentFixture fixture;
            fixture.width = fixture.height = 64;
            for (const auto label : {L"Polygon target", L"Other target"})
            {
                auto layer = ImageLayer::Blank(label, {64, 64});
                layer.asset =
                    RasterAsset{Raster::MakeSparse(64, 64, RasterFormat::Bgra8888, {}, 0, 0, {}, 0), {}, layer.name};
                fixture.layers.push_back(std::move(layer));
            }
            if (mode != SelectionMode::New)
                fixture.selection = DocumentSelection{
                    false, SelectionLoops{std::vector<SelectionLoop>{{{1, 1}, {5, 1}, {5, 5}, {1, 5}}}}};
            else if (doubleClickTwo)
                fixture.selection = DocumentSelection{false, SelectionLoops{}};
            const auto active = fixture.layers[0].id;
            const auto other = fixture.layers[1].id;
            owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
            owner->_session.history.Reset();
            if (exportOpen || exportCancel)
                owner->_session.history.MarkSaved(Guid::Generate()); // Export must preserve a genuinely dirty marker.
            owner->_tool = EditorTool::Lasso;
            owner->_session.lassoKind = LassoKind::Polygonal;
            owner->_session.selectionModeChoice = mode;
            owner->_session.selectionAntialiased = false;
            owner->_viewport.followsFit = false;
            owner->_viewport.zoom = 8;
            owner->DocumentChanged();
            if (!expect(settle(), "initial-document-and-first-display-settled"))
                return 5;
            owner->RenderFrame();
            const auto hole = owner->_chrome->HolePixels();
            const auto origin = owner->_viewport.DocumentRectOrigin(owner->DocumentSize());
            owner->_viewport.pan.x += std::round(hole.left + origin.x) - (hole.left + origin.x);
            owner->_viewport.pan.y += std::round(hole.top + origin.y) - (hole.top + origin.y);
            const auto original = owner->_session.Selection();
            const auto generation = owner->_session.Generation();
            const auto revision = owner->_session.history.CurrentRevision();
            const auto send = [&](UINT message, Vec2 point, WPARAM modifiers) noexcept
            {
                const auto view = owner->_viewport.ViewFromDocument(point, owner->DocumentSize());
                const auto currentHole = owner->_chrome->HolePixels();
                SendMessageW(owner->_window.get(), message, modifiers,
                             MAKELPARAM(static_cast<int>(std::lround(currentHole.left + view.x)),
                                        static_cast<int>(std::lround(currentHole.top + view.y))));
            };
            const auto click = [&](Vec2 point, bool doubleClick = false) noexcept
            {
                owner->_lastPressTick =
                    doubleClick ? GetTickCount64() : 0; // Controlled separate clicks, never a timing sleep.
                send(WM_LBUTTONDOWN, point, MK_LBUTTON);
                send(WM_LBUTTONUP, point, 0);
            };
            const auto key = [&](UINT virtualKey) noexcept
            {
                SendMessageW(owner->_window.get(), WM_KEYDOWN, virtualKey, 0);
                SendMessageW(owner->_window.get(), WM_KEYUP, virtualKey, MAKELPARAM(1, KF_UP | KF_REPEAT));
            };
            std::array<BYTE, 256> keys{};
            KeyboardState::Write(keys);
            click({2, 2});
            const auto creator = owner->_lassoOpeningReceipt;
            const auto opening = owner->_lassoInput.Opening();
            const auto provisionalReceipt = [&]() noexcept
            {
                if (owner->_activeHostRequest)
                {
                    const auto* canvas = std::get_if<HostCanvasBrushInput>(&owner->_activeHostRequest->input.Input());
                    if (canvas && canvas->kind == HostCanvasInputKind::LassoFinish && canvas->opening == opening &&
                        canvas->lassoClosingIntent)
                        return owner->_activeHostRequest->input.Receipt();
                }
                for (std::size_t index = 0; index < owner->_hostRequests.Size(); ++index)
                {
                    const auto* queued = owner->_hostRequests.PendingAt(index);
                    const auto* canvas = queued ? std::get_if<HostCanvasBrushInput>(&queued->input.Input()) : nullptr;
                    if (canvas && canvas->kind == HostCanvasInputKind::LassoFinish && canvas->opening == opening &&
                        canvas->lassoClosingIntent)
                        return queued->input.Receipt();
                }
                return ContinuationReceipt{};
            };
            const auto* source = owner->_lassoInput.CapturedContext(opening);
            if (!expect(source && source->kind == LassoKind::Polygonal && source->mode == mode &&
                            creator.Read().outcome == HostOutcome::Pending && !owner->_polygonalDrawing &&
                            !owner->_session.PendingLasso(),
                        "native-down-captures-only-event-scalars-before-worker"))
                return 5;
            if (!expect(owner->_lassoInput.Sampling() && owner->_lassoInput.Busy() &&
                            !std::holds_alternative<DraftDrag>(owner->_drag),
                        "native-up-is-nonterminal-for-captured-polygon"))
                return 5;
            ContinuationReceipt terminal;
            if (early)
            {
                click({8, 2});
                click({8, 7});
                click({2, 7});
                owner->_session.selectionAntialiased = !duplicate;
                if (duplicate)
                    click({2, 2});
                owner->_session.selectionAntialiased = true;
                key(VK_RETURN);
                terminal = owner->_lassoFinishReceipt;
                if (!expect(!owner->_polygonalDrawing && terminal.Read().outcome == HostOutcome::Pending &&
                                !owner->_lassoInput.Sampling(),
                            "physical-terminal-precedes-original-controller-admission"))
                    return 5;
            }
            if (!expect(until(
                            [&]() noexcept
                            {
                                return owner->_polygonalDrawing && owner->_polygonalDrawing->opened &&
                                       creator.Read().outcome != HostOutcome::Pending;
                            }) &&
                            creator.Read().outcome == HostOutcome::Succeeded,
                        "acknowledged-opening-yields-only-creator-receipt"))
                return 5;
            const auto ticket = owner->_polygonalDrawing->original;
            const auto* preparedWriter = owner->_polygonalDrawing->controller;
            if (!early)
            {
                if (!expect(until([&]() noexcept { return owner->_polygonalDrawing->waiting; }) &&
                                owner->_polygonalDrawing->controller && owner->_session.PendingLasso() &&
                                !owner->_historyWork.Busy() && !owner->_activeHostRequest,
                            "live-original-job-keeps-writer-without-global-history-barrier"))
                    return 5;
                click({8, 2});
                if (!shortTwo)
                    click({8, 7});
                if (!expect(until(
                                [&]() noexcept
                                {
                                    return owner->_polygonalDrawing->waiting && owner->_session.PendingLasso() &&
                                           owner->_session.PendingLasso().value().PointCount() == (shortTwo ? 2u : 3u);
                                }),
                            "separate-physical-corners-replay-on-original-parent"))
                    return 5;
                if ((shortTwo || escapeCancel || backspaceZero) &&
                    !expect(owner->_polygonalDrawing->waiting &&
                                owner->_polygonalDrawing->controller == preparedWriter &&
                                owner->_polygonalDrawing->original == ticket && owner->_session.PendingLasso() &&
                                owner->_session.PendingLasso().value().PointCount() == (shortTwo ? 2u : 3u) &&
                                owner->_session.Selection() == original &&
                                owner->_session.history.CurrentRevision() == revision &&
                                owner->_session.history.UndoCount() == 0,
                            "short-or-cancel-prefix-retains-original-uncommitted-writer"))
                    return 5;
                if (zoomLive)
                {
                    const double zoomBefore = owner->_viewport.zoom;
                    const auto zoomCommand =
                        mode == SelectionMode::Add ? ChromeHost::kCommandZoomOut : ChromeHost::kCommandZoomIn;
                    const auto zoom = owner->QueueHostInput(HostCommandInput{zoomCommand});
                    if (!expect(until([&]() noexcept { return zoom.Read().outcome != HostOutcome::Pending; }) &&
                                    zoom.Read().outcome == HostOutcome::Succeeded && zoom.Read().error == S_OK &&
                                    (mode == SelectionMode::Add ? owner->_viewport.zoom < zoomBefore
                                                                : owner->_viewport.zoom > zoomBefore) &&
                                    owner->_polygonalDrawing->original == ticket &&
                                    owner->_polygonalDrawing->controller == preparedWriter &&
                                    owner->_session.PendingLasso() &&
                                    owner->_session.PendingLasso().value().PointCount() == 3 &&
                                    owner->_session.Selection() == original &&
                                    owner->_session.history.CurrentRevision() == revision &&
                                    owner->_session.history.UndoCount() == 0,
                                "queued-live-zoom-changes-view-preserves-original-writer-committed-state"))
                        return 5;
                }
                if (pointerLeaveLive)
                {
                    send(WM_MOUSEMOVE, {2, 7}, 0);
                    const bool trackedBeforeLeave = owner->_mouseTracked;
                    SendMessageW(owner->_window.get(), WM_MOUSELEAVE, 0, 0);
                    if (!expect(trackedBeforeLeave && !owner->_mouseTracked &&
                                    std::holds_alternative<std::monostate>(owner->_drag) &&
                                    owner->_polygonalDrawing->original == ticket &&
                                    owner->_polygonalDrawing->controller == preparedWriter &&
                                    owner->_session.PendingLasso() &&
                                    owner->_session.PendingLasso().value().PointCount() == 3 &&
                                    owner->_session.Selection() == original &&
                                    owner->_session.history.CurrentRevision() == revision &&
                                    owner->_session.history.UndoCount() == 0,
                                "native-mouse-leave-preserves-live-original-writer-and-committed-state"))
                        return 5;
                }
                if (save)
                {
                    const auto leaf = mode == SelectionMode::New   ? L"new.prism"
                                      : mode == SelectionMode::Add ? L"add.prism"
                                                                   : L"subtract.prism";
                    const auto path = output / (name == "save-failed" ? L"failed" : L"saved") / leaf;
                    std::filesystem::create_directories(path.parent_path(), pathError);
                    if (!expect(!pathError, "exclusive-write-parent-ready"))
                        return 5;
                    if (name == "save-failed")
                    {
                        owner->_projectSaveTestGate = std::make_shared<ProjectSaveTestGate>();
                        owner->_projectSaveTestGate->traceOnly = true;
                        owner->_projectSaveTestGate->faultStageForTest = ProjectSaveFaultStage::ArchiveCommit;
                        owner->_projectSaveTestGate->faultErrorForTest = ERROR_WRITE_FAULT;
                    }
                    const auto write = owner->QueueHostInput(HostWriteInput{
                        HostWriteFormat::Project, true,
                        name == "save-cancel" ? std::optional<std::wstring>{} : std::optional{path.wstring()}});
                    const auto wanted = name == "save-cancel"   ? HostOutcome::Cancelled
                                        : name == "save-failed" ? HostOutcome::Failed
                                                                : HostOutcome::Succeeded;
                    const auto error = name == "save-cancel"   ? HRESULT_FROM_WIN32(ERROR_CANCELLED)
                                       : name == "save-failed" ? E_FAIL
                                                               : S_OK;
                    if (!expect(until([&]() noexcept { return write.Read().outcome != HostOutcome::Pending; }) &&
                                    write.Read().outcome == wanted && write.Read().error == error,
                                "actual-write-or-dialog-cancel-has-exact-receipt-outcome"))
                        return 5;
                    if (!expect(owner->_polygonalDrawing->controller && owner->_polygonalDrawing->original == ticket &&
                                    owner->_session.PendingLasso() &&
                                    owner->_session.PendingLasso().value().PointCount() == 3 &&
                                    !owner->_historyWork.Busy() && owner->_session.Selection() == original &&
                                    owner->_session.history.CurrentRevision() == revision,
                                "write-outcome-preserves-live-draft-original-ticket-and-committed-state"))
                        return 5;
                    if (name == "save-open")
                    {
                        const auto loaded = LoadProject(path.c_str());
                        if (!expect(loaded.status == ProjectIoStatus::Ok &&
                                        loaded.activeLayerID == std::optional(active) && !loaded.document.selection &&
                                        loaded.document.width == 64 && loaded.document.height == 64 &&
                                        !owner->_session.history.IsModified(),
                                    "saved-file-excludes-transient-selection-and-marks-committed-revision"))
                            return 5;
                    }
                    if (name == "save-failed")
                        if (!expect(owner->_projectSaveTestGate->saveStatus == ProjectIoStatus::Encode &&
                                        owner->_projectSaveTestGate->faultVisits[static_cast<std::size_t>(
                                            ProjectSaveFaultStage::ArchiveCommit)] == 1 &&
                                        !std::filesystem::exists(path),
                                    "injected-native-archive-failure-publishes-no-file-or-selection"))
                            return 5;
                }
                if (exportOpen || exportCancel)
                {
                    const bool jpeg = name == "jpeg-open" || name == "jpeg-cancel";
                    const auto format = jpeg ? HostWriteFormat::Jpeg : HostWriteFormat::Png;
                    const auto path = output / L"exports" /
                                      std::wstring(family, family + std::char_traits<char>::length(family)) /
                                      (std::wstring(name.begin(), name.end()) + (jpeg ? L".jpg" : L".png"));
                    std::filesystem::create_directories(path.parent_path(), pathError);
                    if (!expect(!pathError, "exclusive-export-parent-ready"))
                        return 5;
                    const std::wstring priorProjectPath = L"before-export.prism";
                    owner->_documentPath = priorProjectPath;
                    std::optional<std::wstring> selectedPath;
                    if (exportOpen)
                        selectedPath = path.wstring();
                    const auto write = owner->QueueHostInput(HostWriteInput{format, false, std::move(selectedPath)});
                    const auto wanted = exportCancel ? HostOutcome::Cancelled : HostOutcome::Succeeded;
                    const auto error = exportCancel ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK;
                    if (!expect(until([&]() noexcept { return write.Read().outcome != HostOutcome::Pending; }) &&
                                    write.Read().outcome == wanted && write.Read().error == error,
                                "actual-png-or-jpeg-host-write-resolves-exact-outcome"))
                        return 5;
                    if (!expect(owner->_polygonalDrawing->controller && owner->_polygonalDrawing->waiting &&
                                    owner->_polygonalDrawing->original == ticket && owner->_session.PendingLasso() &&
                                    owner->_session.PendingLasso().value().PointCount() == 3 &&
                                    !owner->_historyWork.Busy() && owner->_session.Selection() == original &&
                                    owner->_session.history.CurrentRevision() == revision &&
                                    owner->_session.history.IsModified() && owner->_documentPath == priorProjectPath,
                                "export-preserves-live-original-draft-ticket-history-path-and-saved-marker"))
                        return 5;
                    if (exportCancel)
                    {
                        if (!expect(!std::filesystem::exists(path), "cancelled-export-publishes-no-artifact"))
                            return 5;
                    }
                    else
                    {
                        DecodedImage decoded;
                        const bool decodedOk =
                            ImportImageFile(path.c_str(), kMaxLayerPixels, decoded) == ImageCodecStatus::Ok &&
                            decoded.image.HasStorage();
                        if (!expect(decodedOk && decoded.image.Format() == RasterFormat::Bgra8888 &&
                                        decoded.image.Width() == 64 && decoded.image.Height() == 64,
                                    "export-output-decodes-to-independent64x64"))
                            return 5;
                        bool pixelsMatch = true;
                        const auto* bytes = decoded.image.Data();
                        for (std::size_t pixel = 0; pixel < 64u * 64u && pixelsMatch; ++pixel)
                        {
                            const auto* bgra = bytes + pixel * 4;
                            if (jpeg)
                            {
                                constexpr int kWhiteMatteJpegTolerance =
                                    6; // Existing jpeg.flatten-white-matte reference tolerance.
                                pixelsMatch = bgra[3] == 255 &&
                                              std::abs(static_cast<int>(bgra[0]) - 255) <= kWhiteMatteJpegTolerance &&
                                              std::abs(static_cast<int>(bgra[1]) - 255) <= kWhiteMatteJpegTolerance &&
                                              std::abs(static_cast<int>(bgra[2]) - 255) <= kWhiteMatteJpegTolerance;
                            }
                            else
                                pixelsMatch = bgra[0] == 0 && bgra[1] == 0 && bgra[2] == 0 && bgra[3] == 0;
                        }
                        if (jpeg)
                        {
                            if (!expect(pixelsMatch, "jpeg-transparent-blank-flattens-to-white-matte"))
                                return 5;
                        }
                        else if (!expect(pixelsMatch, "png-transparent-blank-retains-allzero-pixels"))
                            return 5;
                    }
                }
                if (name == "prompt-cancel")
                {
                    owner->_session.history.MarkSaved(Guid::Generate());
                    owner->_forcedDiscardAnswer = Prompts::Answer::Cancel;
                    const auto close = owner->QueueHostInput(HostCloseProjectInput{});
                    if (!expect(until([&]() noexcept { return close.Read().outcome != HostOutcome::Pending; }) &&
                                    close.Read().outcome == HostOutcome::Cancelled && owner->_window &&
                                    owner->_polygonalDrawing->controller &&
                                    owner->_polygonalDrawing->original == ticket &&
                                    owner->_session.PendingLasso().value().PointCount() == 3,
                                "cancelled-close-confirmation-preserves-window-capture-and-original-writer"))
                        return 5;
                    owner->_forcedDiscardAnswer.reset();
                }
                if (name == "blocked-layer")
                {
                    owner->OnLayerSelected(other);
                    const auto* queued = owner->_hostRequests.Front();
                    const auto refused = queued ? queued->input.Receipt() : ContinuationReceipt{};
                    if (!expect(queued &&
                                    until([&]() noexcept { return refused.Read().outcome != HostOutcome::Pending; }) &&
                                    refused.Read().outcome == HostOutcome::Failed &&
                                    refused.Read().error == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) &&
                                    owner->_session.ActiveLayerID() == std::optional(active) &&
                                    owner->_polygonalDrawing->controller,
                                "unsupported-direct-layer-callback-queues-and-refuses-before-source-mutation"))
                        return 5;
                }
                if (nearFirstTwo)
                {
                    click({2, 2});
                    const auto nearReceipt = provisionalReceipt();
                    if (!expect(nearReceipt.Read().outcome == HostOutcome::Pending &&
                                    until(
                                        [&]() noexcept
                                        {
                                            return nearReceipt.Read().outcome != HostOutcome::Pending &&
                                                   owner->_polygonalDrawing->waiting;
                                        }) &&
                                    nearReceipt.Read().outcome == HostOutcome::Succeeded &&
                                    nearReceipt.Read().error == S_OK && owner->_polygonalDrawing->waiting &&
                                    owner->_polygonalDrawing->controller == preparedWriter &&
                                    owner->_polygonalDrawing->original == ticket && owner->_session.PendingLasso() &&
                                    owner->_session.PendingLasso().value().PointCount() == 3 &&
                                    owner->_session.Selection() == original &&
                                    owner->_session.history.CurrentRevision() == revision,
                                "near-first-two-corners-appends-and-keeps-original-draft-live"))
                        return 5;
                    key(VK_ESCAPE);
                }
                else if (doubleClickTwo)
                {
                    click({8, 2},
                          true); // The second click is at the previous corner, inside native double-click bounds.
                    terminal = provisionalReceipt();
                    const auto accepted = owner->_lassoInput.Peek(opening);
                    if (!expect(terminal.Read().outcome == HostOutcome::Pending && accepted &&
                                    accepted->action == HostLassoInput::Action::Move &&
                                    accepted->sample.operation == HostLassoInput::Operation::PolygonPress &&
                                    accepted->sample.doubleClick && !accepted->sample.nearFirst,
                                "two-corner-double-click-retains-selected-short-terminal-receipt"))
                        return 5;
                }
                else if (escapeCancel)
                {
                    key(VK_ESCAPE);
                }
                else if (backspaceZero)
                {
                    key(VK_BACK);
                    key(VK_BACK);
                    key(VK_BACK);
                }
                else
                {
                    if (headerModeLive)
                    {
                        const int direction = mode == SelectionMode::Subtract ? -1 : 1;
                        const int targetMode = static_cast<int>(mode) + direction;
                        owner->OnSelectionAction(SelectionAction::SetMode, static_cast<double>(targetMode));
                        if (!expect(owner->_session.selectionModeChoice == static_cast<SelectionMode>(targetMode) &&
                                        owner->_lassoInput.CapturedContext(opening) &&
                                        owner->_lassoInput.CapturedContext(opening)->mode == mode &&
                                        owner->_polygonalDrawing->controller == preparedWriter &&
                                        owner->_polygonalDrawing->original == ticket &&
                                        owner->_session.PendingLasso() &&
                                        owner->_session.PendingLasso().value().PointCount() == 3 &&
                                        owner->_session.Selection() == original &&
                                        owner->_session.history.CurrentRevision() == revision,
                                    "header-mode-callback-preserves-captured-original-drawing-mode"))
                            return 5;
                    }
                    if (harnessDpiLive)
                    {
                        RECT originalBounds{};
                        const bool haveBounds = GetWindowRect(owner->_window.get(), &originalBounds) != FALSE;
                        const UINT nativeDpi = GetDpiForWindow(owner->_window.get());
                        const UINT previousHarnessDpi = owner->_screenshotDpi;
                        const UINT restoredDpi = previousHarnessDpi ? previousHarnessDpi : nativeDpi;
                        const UINT harnessDpi = nativeDpi == 144 ? 120 : 144;
                        RECT suggested = originalBounds;
                        if (haveBounds)
                        {
                            suggested.left -= 16;
                            suggested.right += 16;
                            suggested.top -= 12;
                            suggested.bottom += 12;
                        }
                        owner->_screenshotDpi = harnessDpi;
                        if (haveBounds)
                            SendMessageW(owner->_window.get(), WM_DPICHANGED, MAKELONG(harnessDpi, harnessDpi),
                                         reinterpret_cast<LPARAM>(&suggested));
                        owner->RenderFrame();
                        RECT afterHarnessMessage{};
                        const bool changed =
                            haveBounds && owner->ChromeDpi() == static_cast<float>(harnessDpi) &&
                            GetDpiForWindow(owner->_window.get()) == nativeDpi &&
                            GetWindowRect(owner->_window.get(), &afterHarnessMessage) &&
                            afterHarnessMessage.left == suggested.left && afterHarnessMessage.top == suggested.top &&
                            afterHarnessMessage.right == suggested.right &&
                            afterHarnessMessage.bottom == suggested.bottom &&
                            owner->_polygonalDrawing->controller == preparedWriter &&
                            owner->_polygonalDrawing->original == ticket && owner->_session.PendingLasso() &&
                            owner->_session.PendingLasso().value().PointCount() == 3 &&
                            owner->_session.Selection() == original &&
                            owner->_session.history.CurrentRevision() == revision;
                        if (changed)
                        {
                            click({2, 7});
                            click({2, 5});
                        }
                        const bool currentViewportClicks =
                            changed &&
                            until(
                                [&]() noexcept
                                {
                                    return owner->_polygonalDrawing->waiting && owner->_session.PendingLasso() &&
                                           owner->_session.PendingLasso().value().PointCount() == 5;
                                }) &&
                            owner->_polygonalDrawing->controller == preparedWriter &&
                            owner->_polygonalDrawing->original == ticket && owner->_session.Selection() == original &&
                            owner->_session.history.CurrentRevision() == revision;
                        owner->_screenshotDpi = previousHarnessDpi;
                        if (haveBounds)
                            SendMessageW(owner->_window.get(), WM_DPICHANGED, MAKELONG(nativeDpi, nativeDpi),
                                         reinterpret_cast<LPARAM>(&originalBounds));
                        owner->RenderFrame();
                        RECT restoredBounds{};
                        const bool restored =
                            owner->ChromeDpi() == static_cast<float>(restoredDpi) &&
                            GetDpiForWindow(owner->_window.get()) == nativeDpi &&
                            GetWindowRect(owner->_window.get(), &restoredBounds) &&
                            restoredBounds.left == originalBounds.left && restoredBounds.top == originalBounds.top &&
                            restoredBounds.right == originalBounds.right &&
                            restoredBounds.bottom == originalBounds.bottom &&
                            owner->_polygonalDrawing->controller == preparedWriter &&
                            owner->_polygonalDrawing->original == ticket && owner->_session.PendingLasso() &&
                            owner->_session.PendingLasso().value().PointCount() == 5;
                        if (!expect(changed,
                                    "harness-dpi-override-native-message-rebuilds-live-chrome-not-os-transition"))
                            return 5;
                        if (!expect(restored, "native-message-restores-normal-window-harness-dpi"))
                            return 5;
                        if (!expect(currentViewportClicks,
                                    "native-corners-use-current-view-mapping-after-harness-dpichanged"))
                            return 5;
                    }
                    if (!harnessDpiLive)
                        click({2, 7});
                }
                if ((zoomLive || pointerLeaveLive) &&
                    !expect(until(
                                [&]() noexcept
                                {
                                    return owner->_polygonalDrawing->waiting && owner->_session.PendingLasso() &&
                                           owner->_session.PendingLasso().value().PointCount() == 4;
                                }) &&
                                owner->_polygonalDrawing->original == ticket &&
                                owner->_polygonalDrawing->controller == preparedWriter &&
                                owner->_session.Selection() == original &&
                                owner->_session.history.CurrentRevision() == revision &&
                                owner->_session.history.UndoCount() == 0,
                            "reentering-native-corner-continues-same-prepared-writer"))
                    return 5;
                if (name == "hover-backspace")
                {
                    send(WM_MOUSEMOVE, {11, 9}, 0);
                    key(VK_BACK);
                    click({2, 7});
                }
                if (!shortTwo && !escapeCancel && !backspaceZero &&
                    !expect(until(
                                [&]() noexcept
                                {
                                    return owner->_polygonalDrawing->waiting && owner->_session.PendingLasso() &&
                                           owner->_session.PendingLasso().value().PointCount() ==
                                               (harnessDpiLive ? 5u : 4u) &&
                                           (name != "hover-backspace" || (owner->_polygonalDrawing->backspaces == 1 &&
                                                                          owner->_polygonalDrawing->cursors >= 1 &&
                                                                          owner->_polygonalDrawing->points == 4));
                                }) &&
                                owner->_session.Selection() == original &&
                                owner->_session.history.CurrentRevision() == revision,
                            "complete-corner-prefix-stays-uncommitted-before-chosen-terminal"))
                    return 5;
                owner->_session.selectionAntialiased = true;
                if (joined)
                    owner->Shutdown();
                else if (tool)
                    owner->OnTool(EditorTool::Move);
                else if (kindCancel)
                    owner->OnSelectionAction(SelectionAction::SetLassoKind, 0.0);
                else if (deselect)
                    owner->OnSelectionAction(SelectionAction::Deselect, 0.0);
                else if (nearFirstTwo || doubleClickTwo || escapeCancel || backspaceZero)
                {
                    // These physical terminals were already sent while the live prefix was inspected.
                }
                else if (name == "near-first")
                    click({2, 2});
                else if (name == "double-click")
                    click({2, 7}, true);
                else
                    key(VK_RETURN);
                if (!noPublication)
                {
                    terminal = owner->_lassoFinishReceipt;
                    if (name == "near-first" || name == "double-click" || doubleClickTwo)
                    {
                        bool capturedTerminal = false;
                        for (std::size_t index = 0; index < owner->_hostRequests.Size(); ++index)
                        {
                            const auto* queued = owner->_hostRequests.PendingAt(index);
                            const auto* canvas =
                                queued ? std::get_if<HostCanvasBrushInput>(&queued->input.Input()) : nullptr;
                            if (canvas && canvas->kind == HostCanvasInputKind::LassoFinish &&
                                canvas->opening == opening && canvas->lassoClosingIntent)
                            {
                                terminal = queued->input.Receipt();
                                capturedTerminal = true;
                                break;
                            }
                        }
                        if (!expect(capturedTerminal && terminal.Read().outcome == HostOutcome::Pending,
                                    "physical-provisional-close-retains-its-own-pending-receipt"))
                            return 5;
                    }
                }
            }
            const auto terminalSettle =
                joined ? SettleResult::Settled : SettleJobs(owner->_jobs, 30000, DispatchMessageThunk, owner.get());
            if (terminalSettle != SettleResult::Settled)
            {
                const auto readiness = owner->_jobs.UiReadinessForTest();
                const auto next = owner->_lassoInput.Peek(opening);
                const auto* pending = owner->_hostRequests.Front();
                const auto* selected =
                    owner->_activeHostRequest
                        ? std::get_if<HostCanvasBrushInput>(&owner->_activeHostRequest->input.Input())
                        : nullptr;
                const auto& current = owner->_polygonalDrawing;
                std::fprintf(
                    stderr,
                    "DIAG polygonal-terminal.%s.%s settle=%u idle=%u ui=%u ready=%u,%u,%u,%u,%u,%u "
                    "history=%u stamp=%u stage=%u active=%llu canvas=%u intent=%llu front=%llu "
                    "input=%u count=%zu next=%u,%u,%llu wait=%u reason=%u complete=%u publish=%u "
                    "controller=%u outcome=%u error=%08lx closing=%llu,%llu replay=%llu,%u "
                    "refresh=%u docgen=%llu undo=%d reserved=%llu releasing=%llu\n",
                    family, scenario, static_cast<unsigned>(terminalSettle), owner->_jobs.IsIdle() ? 1u : 0u,
                    owner->_jobs.HasUiWork() ? 1u : 0u, readiness.completions, readiness.actions,
                    readiness.source ? 1u : 0u, readiness.resumes ? 1u : 0u, readiness.owner ? 1u : 0u,
                    readiness.memory ? 1u : 0u, owner->_historyWork.Busy() ? 1u : 0u, owner->_polygonalStamp ? 1u : 0u,
                    static_cast<unsigned>(owner->_hostStage),
                    static_cast<unsigned long long>(owner->_activeHostRequest ? owner->_activeHostRequest->ordinal : 0),
                    selected ? static_cast<unsigned>(selected->kind) : 0u,
                    static_cast<unsigned long long>(selected ? selected->lassoClosingIntent : 0),
                    static_cast<unsigned long long>(pending ? pending->ordinal : 0),
                    owner->_lassoInput.Busy() ? 1u : 0u, owner->_lassoInput.PendingCount(),
                    next ? static_cast<unsigned>(next.value().action) : 0u,
                    next ? static_cast<unsigned>(next.value().sample.operation) : 0u,
                    static_cast<unsigned long long>(next ? next.value().sample.ordinal : 0),
                    current && current->waiting ? 1u : 0u, current ? static_cast<unsigned>(current->waitReason) : 0u,
                    current && current->completed ? 1u : 0u, current && current->published ? 1u : 0u,
                    current && current->controller ? 1u : 0u,
                    current ? static_cast<unsigned>(current->outcome.status) : 0u,
                    static_cast<unsigned long>(current ? current->outcome.error : 0),
                    static_cast<unsigned long long>(current ? current->closingOrdinal : 0),
                    static_cast<unsigned long long>(current ? current->closingIntent : 0),
                    static_cast<unsigned long long>(owner->_lassoReplayedClosingIntent),
                    owner->_lassoReplayedCloseChosen ? 1u : 0u, current ? current->residencyRefreshes : 0u,
                    static_cast<unsigned long long>(owner->_session.Generation()), owner->_session.history.UndoCount(),
                    static_cast<unsigned long long>(owner->_jobs.ReservedMemoryBytes()),
                    static_cast<unsigned long long>(owner->_jobs.ReleasingMemoryBytes()));
                std::fflush(stdout);
                std::fflush(stderr);
            }
            if (!expect(terminalSettle == SettleResult::Settled, "original-parent-and-all-host-acknowledgements-join"))
                return 5;
            const auto& state = owner->_polygonalDrawing;
            if (!expect(state && state->original == ticket && state->sameTicket && state->captures == 1 &&
                            state->inputAcknowledged && !state->controller && !owner->_lassoInput.Busy() &&
                            !owner->_polygonalStamp && !owner->_historyWork.Busy() && !owner->_activeHostRequest &&
                            owner->_hostRequests.Size() == 0,
                        "one-original-ticket-capture-input-and-history-barrier-retire-exactly-once"))
                return 5;
            bool geometry = false;
            if (joined)
                geometry = !owner->_session.Document() && owner->_jobs.IsIdle();
            else if (deselect)
                geometry = !owner->_session.Selection() && !owner->_session.PendingLasso() &&
                           owner->_session.history.UndoCount() == (mode == SelectionMode::New ? 0 : 1);
            else if (tool || kindCancel)
                geometry = SamePolygonalHostSelection(owner->_session.Selection(), original) &&
                           !owner->_session.PendingLasso() && owner->_session.Generation() == generation &&
                           owner->_session.history.UndoCount() == 0 &&
                           (tool ? owner->_tool == EditorTool::Move : owner->_session.lassoKind == LassoKind::Freehand);
            else if (escapeCancel || nearFirstTwo)
                geometry = state->outcome.status == JobStatus::Cancelled && !state->published &&
                           state->inputAcknowledged && owner->_lassoCancelledOpening == opening &&
                           !owner->_lassoInput.Sampling() && !owner->_session.PendingLasso() &&
                           SamePolygonalHostSelection(owner->_session.Selection(), original) &&
                           owner->_session.Generation() == generation && owner->_session.history.UndoCount() == 0;
            else if (backspaceZero)
                geometry = state->endedByBackspace && state->outcome.status == JobStatus::Finished &&
                           !state->published && state->inputAcknowledged && state->backspaces == 3 &&
                           !owner->_session.PendingLasso() &&
                           SamePolygonalHostSelection(owner->_session.Selection(), original) &&
                           owner->_session.Generation() == generation && owner->_session.history.UndoCount() == 0;
            else if (doubleClickTwo)
                geometry = state->published && terminal.Read().outcome == HostOutcome::Succeeded &&
                           (mode == SelectionMode::New
                                ? !owner->_session.Selection() && owner->_session.history.UndoCount() == 1
                                : SamePolygonalHostSelection(owner->_session.Selection(), original) &&
                                      owner->_session.history.UndoCount() == 0) &&
                           owner->_session.Generation() == generation + (mode == SelectionMode::New ? 1u : 0u);
            else
                geometry = state->published && terminal.Read().outcome == HostOutcome::Succeeded &&
                           PolygonalHostPixels(owner->_session.Selection(), mode) &&
                           owner->_session.Selection().value().antialiased == !duplicate &&
                           owner->_session.Generation() == generation + 1 && owner->_session.history.UndoCount() == 1;
            if (!expect(geometry, "independent4096-centers-terminal-aa-and-cancel-history-policy"))
                return 5;
            if (!noPublication && !(doubleClickTwo && mode != SelectionMode::New))
            {
                const auto final = owner->_session.Selection();
                owner->_session.Undo();
                const bool undone = SamePolygonalHostSelection(owner->_session.Selection(), original) &&
                                    owner->_session.history.CanRedo();
                const bool saved = exportOpen || exportCancel ? owner->_session.history.IsModified()
                                   : name == "save-open"      ? !owner->_session.history.IsModified()
                                                              : true;
                owner->_session.Redo();
                if (!expect(undone && saved && SamePolygonalHostSelection(owner->_session.Selection(), final) &&
                                owner->_session.history.UndoCount() == 1 && owner->_session.history.IsModified(),
                            "undo-redo-restores-source-and-retains-marker-written-during-live-polygon"))
                    return 5;
            }
            ++reached;
        }
        scenario = "close";
        owner->Shutdown();
        if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                        !owner->_jobs.ReleasingMemoryBytes(),
                    "joined-mode-close-refunds-all-cpu-gpu-and-weak-owners"))
            return 5;
    }
    family = scenario = "close";
    if (!expect(reached == 81 && !failures, "all45-native-mode-order-write-and-lifetime-cases-reached"))
        return 5;
    return failures ? 5 : 0;
}
