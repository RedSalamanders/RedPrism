#include "Application.h"
#include "ChromeHost.h"
#include "DocumentDraft.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace
{
std::optional<CanvasDocument> TransportFixture()
{
    CanvasDocument document;
    document.width = 2;
    document.height = 2;
    auto draft = CanvasDocumentDraft::Rebuild(document, 1);
    if (!draft)
        return std::nullopt;
    constexpr std::array<std::uint8_t, 16> gray{128, 128, 128, 255, 128, 128, 128, 255,
                                                128, 128, 128, 255, 128, 128, 128, 255};
    auto image = Raster::Make(2, 2, RasterFormat::Bgra8888, gray.data(), gray.size());
    if (!image.HasStorage())
        return std::nullopt;
    auto layer = ImageLayer::Blank(L"Pending display", Vec2{2, 2});
    layer.asset = {std::move(image), {}, L"Pending display"};
    layer.transform.sampling = LayerSampling::Nearest;
    if (!draft->Append(std::move(layer)))
        return std::nullopt;
    return draft->Seal();
}

// Exposure reference: legacy ImageAdjustmentTests.exposureWorksInLinearLightWithOffsetAndGamma
// gives +1 stop at sRGB 128 as 176. Independently decoding sRGB, scaling linear light by 1/2,
// encoding sRGB and rounding gives -1 stop as 92; alpha remains 255.
bool MatchesTransportLiteral(RecordedDisplayGraph* graph, std::uint8_t expected)
{
    if (!graph || !graph->CompositionForTestOwner())
        return false;
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(graph->CompositionForTestOwner()->Readback(pixels, width, height)) || width != 2 || height != 2 ||
        pixels.size() != 16)
        return false;
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
        if (pixels[offset] != expected || pixels[offset + 1] != expected || pixels[offset + 2] != expected ||
            pixels[offset + 3] != 255)
        {
            std::printf("INFO application-pending-display.literal expected=%u actual=%u,%u,%u,%u\n", expected,
                        pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]);
            return false;
        }
    return true;
}
} // namespace

int Application::RunStandalonePendingDisplayTransportSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        std::printf("%s application-pending-display-transport.%s\n", condition ? "PASS" : "FAIL", name);
        std::fflush(stdout);
        ++checks;
        failures += condition ? 0u : 1u;
        return condition;
    };
    {
        auto owner =
            std::unique_ptr<Application>{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
        if (!expect(owner != nullptr, "transport-host-allocated"))
            return 5;
        const auto close = wil::scope_exit(
            [&]() noexcept
            {
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        owner->_asyncStrokeFinishEnabled = true;
        owner->_asyncStrokeSamplesEnabled = true;
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && owner->EnableCommittedDisplayForTest() &&
                        SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                        SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome()) &&
                        owner->EnablePreparedLayerRows() && owner->_strokeFinishSource.Start(),
                    "transport-actual-WARP-host-initializes"))
            return 5;

        auto fixture = TransportFixture();
        if (!expect(fixture.has_value(), "transport-literal-source-prepared"))
            return 5;
        const auto active = fixture->layers[0].id;
        owner->_session.ReplaceDocument(std::move(fixture.value()), active);
        owner->DocumentChanged();
        if (!expect(SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled &&
                        MatchesTransportLiteral(owner->_acknowledgedCommittedDisplay, 128) &&
                        owner->_recordedCommands.AttachedSourceCountOwnerForTest() == 0,
                    "transport-initial-committed-source-settled-before-refusals"))
            return 5;

        // Receipt/pump waits below keep unrelated quarantined owners pending. They drain the existing scheduler and
        // UI-action sources, dispatch the hidden host's messages, and block on the existing wake event; timeout is a
        // deadlock bound only. Never use SettleJobs after the first deliberate quarantine.
        const auto driveUntil = [&](auto&& ready) noexcept
        {
            const auto started = GetTickCount64();
            for (;;)
            {
                const auto messages = PumpMessages(DispatchMessageThunk, owner.get());
                if (messages.quit)
                {
                    PostQuitMessage(messages.exitCode);
                    return false;
                }
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                if (ready())
                    return true;
                const auto elapsed = GetTickCount64() - started;
                if (elapsed >= 15000 || !owner->_jobs.WakeHandle())
                    return false;
                if (owner->_jobs.HasUiWork())
                    continue;
                const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                const auto waited = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(15000 - elapsed),
                                                                QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (waited == WAIT_FAILED)
                    return false;
            }
        };

        owner->OnCommand(ChromeHost::kCommandExposure);
        if (!expect(SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled &&
                        owner->_session.HasColorEdit(FilterKind::Exposure) &&
                        owner->_recordedCommands.AttachedSourceCountOwnerForTest() == 0,
                    "transport-exposure-panel-opened"))
            return 5;

        // The real Exposure recording executes. Both completion enqueues are refused, retaining its original source
        // in the pump. No completion is synthesized; quarantine is the direct post-Execute observation.
        owner->_recordedCommands.SetCompletionRefusalsForTest(E_FAIL, E_FAIL);
        owner->OnAdjustAction(AdjustAction::SetExposure, 1.0);
        if (!expect(driveUntil(
                        [&]() noexcept
                        {
                            return owner->_recordedCommands.AttachedSourceCountOwnerForTest() == 1 &&
                                   owner->_recordedCommands.QuarantinedSourceCountOwnerForTest() == 1 &&
                                   !owner->_recordedCommands.HasReady();
                        }),
                    "transport-exposure-real-execute-and-recovery-refusal-leave-one-dormant-source"))
            return 5;

        owner->OnAdjustAction(AdjustAction::Cancel, 0.0);
        const auto cancelReceipt = owner->_pendingCancelReceipt;
        if (!expect(cancelReceipt.Read().outcome == HostOutcome::Pending && owner->ColorControlsFrozen(),
                    "transport-cancel-keeps-exact-accepted-receipt-pending-until-closure"))
            return 5;
        if (!expect(driveUntil([&]() noexcept { return cancelReceipt.Read().outcome != HostOutcome::Pending; }) &&
                        cancelReceipt.Read().outcome == HostOutcome::Succeeded && !owner->_session.HasColorEdit() &&
                        owner->_recordedCommands.AttachedSourceCountOwnerForTest() == 1 &&
                        owner->_recordedCommands.QuarantinedSourceCountOwnerForTest() == 1 &&
                        !owner->_recordedCommands.HasReady(),
                    "transport-cancel-receipt-acknowledges-while-original-exposure-source-remains"))
            return 5;

        // The closure receipt has completed. Select Brush by the real host request path before opacity begins.
        const auto brushTool = owner->QueueHostInput(HostToolInput{EditorTool::Brush});
        if (!expect(driveUntil([&]() noexcept { return brushTool.Read().outcome != HostOutcome::Pending; }) &&
                        brushTool.Read().outcome == HostOutcome::Succeeded,
                    "transport-real-brush-tool-request-acknowledges"))
            return 5;

        // First let the actual committed opacity producer complete and prove its source also executed and entered
        // dormant quarantine. Paint starts only after this receipt and both observer counts reach two.
        owner->_recordedCommands.SetCompletionRefusalsForTest(E_FAIL, E_FAIL);
        const auto opacity = owner->QueueHostInput(HostLayerOpacityInput{active, 0.5, ChromeEditPhase::Commit});
        if (!expect(driveUntil(
                        [&]() noexcept
                        {
                            return opacity.Read().outcome != HostOutcome::Pending &&
                                   owner->_recordedCommands.AttachedSourceCountOwnerForTest() == 2 &&
                                   owner->_recordedCommands.QuarantinedSourceCountOwnerForTest() == 2 &&
                                   !owner->_recordedCommands.HasReady();
                        }) &&
                        opacity.Read().outcome == HostOutcome::Succeeded && owner->_session.ActiveLayer() &&
                        owner->_session.ActiveLayer()->opacity == 0.5,
                    "transport-opacity-receipt-and-two-real-dormant-sources"))
            return 5;

        const auto opacityImage = owner->_session.ActiveLayer()->asset->image;
        const auto opacityRevision = owner->_session.history.CurrentRevision();
        const int opacityUndoCount = owner->_session.history.UndoCount();
        const auto hole = owner->_chrome->HolePixels();
        const auto view = owner->_viewport.ViewFromDocument({1, 1}, owner->DocumentSize());
        const LPARAM point = MAKELPARAM(static_cast<int>(hole.left + view.x), static_cast<int>(hole.top + view.y));
        owner->OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, point);
        owner->OnPointer(WM_LBUTTONUP, 0, point);
        if (!expect(owner->_canvasBrushInput.Busy() &&
                        owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending,
                    "transport-real-paint-down-up-admitted-after-opacity-ack"))
            return 5;

        const auto paintReceipt = owner->_canvasBrushReceipt;
        if (!expect(driveUntil([&]() noexcept { return paintReceipt.Read().outcome != HostOutcome::Pending; }),
                    "transport-paint-receipt-reaches-terminal-result"))
            return 5;
        const auto paintResult = paintReceipt.Read();
        const auto capacityRefusal = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA));
        if (paintResult.outcome != HostOutcome::Failed || paintResult.error != capacityRefusal)
            std::printf("INFO application-pending-display-transport.paint-result outcome=%u error=%08x "
                        "attached=%zu quarantined=%zu\n",
                        static_cast<unsigned int>(paintResult.outcome), static_cast<unsigned int>(paintResult.error),
                        owner->_recordedCommands.AttachedSourceCountOwnerForTest(),
                        owner->_recordedCommands.QuarantinedSourceCountOwnerForTest());
        if (!expect(paintResult.outcome == HostOutcome::Failed && paintResult.error == capacityRefusal,
                    "diagnostic-third-real-attachment-returns-two-slot-capacity-refusal"))
            return 5;
        if (!expect(!owner->_canvasBrushInput.Busy() && !owner->_session.PendingBrush() &&
                        !owner->_strokeFinishCurrent && !owner->_utilityTicket && owner->_session.ActiveLayer() &&
                        owner->_session.ActiveLayer()->asset &&
                        owner->_session.ActiveLayer()->asset->image == opacityImage &&
                        owner->_session.history.CurrentRevision() == opacityRevision &&
                        owner->_session.history.UndoCount() == opacityUndoCount &&
                        owner->_recordedCommands.AttachedSourceCountOwnerForTest() == 2 &&
                        owner->_recordedCommands.QuarantinedSourceCountOwnerForTest() == 2 &&
                        !owner->_recordedCommands.HasReady(),
                    "transport-capacity-refusal-does-not-publish-paint-or-change-history"))
            return 5;

        // Only joined shutdown retires the two original quarantined commands; no acknowledgement is forged.
        owner->Shutdown();
        if (!expect(owner->_shutdownComplete && !owner->_recordedCommands.HasPending() &&
                        owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                    "transport-joined-shutdown-retires-both-original-quarantined-sources"))
            return 5;
    }

    std::printf("SUMMARY application-pending-display-transport checks=%u failures=%u\n", checks, failures);
    return failures ? 5 : 0;
}
