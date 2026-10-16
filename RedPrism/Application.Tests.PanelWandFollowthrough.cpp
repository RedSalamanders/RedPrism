#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "Prompts.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr std::array<std::uint8_t, 8> kSource{0, 0, 128, 255, 64, 0, 0, 255};
constexpr std::array<std::uint8_t, 8> kApplied{0, 0, 0, 255, 64, 0, 0, 255};

DocumentSelection FollowthroughRect(double x)
{
    return {false, {SelectionLoop{{x, 0}, {x + 1, 0}, {x + 1, 1}, {x, 1}}}};
}

bool FollowthroughHasRect(const std::optional<DocumentSelection>& selection, double x)
{
    if (!selection || selection->antialiased || selection->loops.size() != 1)
        return false;
    const auto& points = selection->loops.Get().front();
    const std::array expected{Vec2{x, 0}, Vec2{x + 1, 0}, Vec2{x + 1, 1}, Vec2{x, 1}};
    if (points.size() != expected.size())
        return false;
    for (std::size_t start = 0; start != expected.size(); ++start)
    {
        bool same = true;
        for (std::size_t index = 0; index != expected.size(); ++index)
            same = same && points[(start + index) % expected.size()] == expected[index];
        if (same)
            return true;
    }
    return false;
}

bool FollowthroughPixels(const Raster& raster, const std::array<std::uint8_t, 8>& expected)
{
    std::array<std::uint8_t, 8> pixels{};
    if (raster.Width() != 2 || raster.Height() != 1 || raster.Format() != RasterFormat::Bgra8888)
        return false;
    raster.ReadRegion(0, 0, 2, 1, pixels.data(), 8, 0);
    return pixels == expected;
}
} // namespace

int Application::RunStandalonePanelWandFollowthroughSelfTest(HINSTANCE instance) noexcept
{
    ApplicationProcessingTestProbe processing;
    processing.workerCount = 2; // One real histogram callback may wait while the real Wand worker completes.
    auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (owner->_colorWorkTestGate)
                (void)SetEvent(owner->_colorWorkTestGate->release.get());
            owner->_processingTestProbe = nullptr;
            owner->Shutdown();
            owner.reset();
            while (PumpMessages().quit)
            {
            }
        });
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    owner->_processingTestProbe = &processing;
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return 5;
    owner->BindCompositionDevice();
    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
    // Bound the test on failure; useful work sleeps on the scheduler/message event between readiness checks.
    const auto waitReceipt = [&](const ContinuationReceipt& receipt) noexcept
    {
        const auto started = GetTickCount64();
        while (receipt.Read().outcome == HostOutcome::Pending)
        {
            const auto messages = PumpMessages(DispatchMessageThunk, owner.get());
            if (messages.quit)
            {
                PostQuitMessage(messages.exitCode);
                return false;
            }
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (receipt.Read().outcome != HostOutcome::Pending)
                break;
            const auto elapsed = GetTickCount64() - started;
            if (elapsed >= 15000 || !owner->_jobs.WakeHandle())
                return false;
            if (owner->_jobs.HasUiWork())
                continue;
            const HANDLE wake = owner->_jobs.WakeHandle();
            const auto result = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(15000 - elapsed), QS_ALLINPUT,
                                                            MWMO_INPUTAVAILABLE);
            if (result > WAIT_OBJECT_0 + 1)
                return false;
        }
        return receipt.Read().outcome == HostOutcome::Succeeded;
    };
    int assertions = 0;
    const auto expect = [&](bool condition, bool composite, const char* mode, const char* fact) noexcept
    {
        ++assertions;
        std::printf("%s panel-wand-followthrough-host.%s.%s.%s\n", condition ? "PASS" : "FAIL",
                    composite ? "composite" : "layer", mode, fact);
        return condition;
    };
    if (!expect(owner->_jobs.WorkerCount() == 2 && settle(), false, "fixture", "two-real-workers-initialized"))
        return 5;
    for (const bool composite : {false, true})
        for (int action = 0; action != 3; ++action)
        {
            const char* const mode = action == 0 ? "ok" : action == 1 ? "delayed-histogram-ok" : "cancel";
            owner->CancelColorEditForTest();
            if (!settle())
                return 5;
            owner->_colorWorkTestGate.reset();
            auto fixture = std::make_unique<DocumentFixture>();
            fixture->id = Guid::Generate();
            fixture->width = 2;
            fixture->height = 1;
            fixture->selection = FollowthroughRect(0);
            auto layer = ImageLayer::Blank(L"Panel Wand follow-through host", {2, 1});
            const auto active = layer.id;
            const auto source = Raster::Make(2, 1, RasterFormat::Bgra8888, kSource.data(), kSource.size());
            layer.asset = RasterAsset{source, {}, layer.name};
            layer.transform.sampling = LayerSampling::Nearest;
            fixture->layers.push_back(std::move(layer));
            owner->ReplaceDocument(std::move(*fixture).Seal(), active, {});
            owner->_session.history.Reset();
            owner->_session.history.MarkSaved();
            owner->_tool = EditorTool::Wand;
            owner->_viewport.zoom = 1;
            owner->_viewport.followsFit = false;
            owner->_session.selectionModeChoice = SelectionMode::New;
            owner->_session.selectionAntialiased = false;
            owner->_session.wandSettings = {};
            owner->_session.wandSettings.tolerance = 0;
            owner->_session.wandSettings.sampleSize = WandSampleSize::Point;
            owner->_session.wandSettings.contiguous = true;
            owner->_session.wandSettings.sampleAllLayers = composite;
            if (!settle())
                return 5;
            if (action == 1)
            {
                owner->_colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
                owner->_colorWorkTestGate->kind = ColorWorkTestKind::Histogram;
                owner->_colorWorkTestGate->afterPreparation = true;
                if (!owner->_colorWorkTestGate->entered || !owner->_colorWorkTestGate->release ||
                    !owner->_colorWorkTestGate->computed)
                    return 5;
            }
            if (!owner->BeginColorEdit(ChromeHost::kCommandLevels))
                return 5;
            if (action == 1)
            {
                if (!WaitForColorWorkTestEvent(owner->_jobs, owner->_colorWorkTestGate->entered.get(),
                                               DispatchMessageThunk, owner.get()))
                    return 5;
            }
            else if (!settle())
                return 5;
            const auto before = std::make_unique<ColorEdit>(*owner->_session.PendingColorEdit());
            const auto oldHistogramTicket = owner->_colorHistogramTicket;
            const auto view = owner->_viewport.ViewFromDocument({1.5, 0.5}, owner->DocumentSize());
            DxUi::PointerEvent pointer;
            pointer.action = DxUi::PointerAction::Down;
            owner->PressCanvas(pointer, WM_LBUTTONDOWN, view, POINT{42, 42});
            const auto* pending =
                owner->_hostRequests.Size() ? owner->_hostRequests.PendingAt(owner->_hostRequests.Size() - 1) : nullptr;
            const auto receipt = pending ? pending->input.Receipt() : ContinuationReceipt::Failure(E_UNEXPECTED);
            if (!expect(pending && receipt.Read().outcome == HostOutcome::Pending && owner->ColorControlsFrozen(),
                        composite, mode, "physical-wand-admitted-with-panel-open"))
                return 5;
            if (!expect(waitReceipt(receipt) && FollowthroughHasRect(owner->_session.Selection(), 1) &&
                            owner->_session.PendingColorEdit() &&
                            owner->_session.PendingColorEdit()->openingSequence == before->openingSequence &&
                            owner->_session.PendingColorEdit()->selection == before->selection &&
                            owner->_session.PendingColorEdit()->original == source &&
                            owner->_session.PendingColorEdit()->sourceToken->selection ==
                                before->sourceToken->selection &&
                            owner->_session.history.UndoCount() == 1 &&
                            owner->_session.history.UndoName() == L"Magic Wand" &&
                            FollowthroughPixels(owner->_session.ActiveLayer()->asset->image, kSource),
                        composite, mode, "wand-acknowledges-preserves-opening-coverage-and-source"))
                return 5;
            if (action == 1)
            {
                const auto gate = owner->_colorWorkTestGate;
                if (!expect(oldHistogramTicket && owner->_colorHistogramTicket == oldHistogramTicket &&
                                gate->visits.load() == 1 && gate->captureReservationHeld.load() &&
                                gate->reservationBytes != 0 &&
                                owner->_jobs.ReservedMemoryBytes() >= gate->reservationBytes &&
                                !owner->_session.PendingColorEdit()->histogramReady,
                            composite, mode, "old-computed-histogram-retains-original-ticket-and-charge"))
                    return 5;
                (void)SetEvent(gate->release.get());
                if (!expect(settle() && owner->_session.PendingColorEdit() &&
                                owner->_session.PendingColorEdit()->histogramReady && gate->visits.load() == 2 &&
                                !owner->_colorHistogramTicket && gate->payloadRetired.load() &&
                                gate->payloadRetiredOffOwner.load() &&
                                owner->_session.PendingColorEdit()->histogram[1][128] == 1,
                            composite, mode, "old-result-refused-one-fresh-histogram-acknowledges"))
                    return 5;
                owner->_colorWorkTestGate.reset();
            }
            if (!settle())
                return 5;
            if (action == 2)
            {
                owner->OnAdjustAction(AdjustAction::Cancel, 0);
                if (!expect(settle() && !owner->_session.HasColorEdit() &&
                                FollowthroughHasRect(owner->_session.Selection(), 1) &&
                                owner->_session.history.UndoCount() == 1 &&
                                FollowthroughPixels(owner->_session.ActiveLayer()->asset->image, kSource),
                            composite, mode, "cancel-preserves-wand-pixels-and-history"))
                    return 5;
                if (!expect(owner->BeginColorEdit(ChromeHost::kCommandLevels) && settle() &&
                                owner->_session.PendingColorEdit() &&
                                FollowthroughHasRect(owner->_session.PendingColorEdit()->selection, 1),
                            composite, mode, "next-panel-captures-current-selection"))
                    return 5;
                continue;
            }
            owner->OnAdjustAction(AdjustAction::SetOutputWhite, 0);
            if (!settle())
                return 5;
            const auto* display = owner->_session.DisplayDocument();
            std::vector<std::uint8_t> shown;
            UINT width = 0, height = 0;
            bool previewMatches = display && owner->_session.ColorLookupPreviewReady() &&
                                  SUCCEEDED(owner->_composition.Compose(*display)) &&
                                  SUCCEEDED(owner->_composition.Readback(shown, width, height)) && width == 2 &&
                                  height == 1 && shown.size() == kApplied.size();
            for (std::size_t i = 0; previewMatches && i != kApplied.size(); ++i)
                previewMatches = std::abs(static_cast<int>(shown[i]) - kApplied[i]) <= (i % 4 == 3 ? 0 : 2);
            if (!expect(previewMatches && FollowthroughHasRect(owner->_session.Selection(), 1) &&
                            FollowthroughPixels(owner->_session.ActiveLayer()->asset->image, kSource),
                        composite, mode, "real-gpu-preview-uses-opening-coverage"))
                return 5;
            owner->OnAdjustAction(AdjustAction::Apply, 0);
            if (!expect(settle() && !owner->_session.HasColorEdit() && !owner->_historyWork.Busy() &&
                            FollowthroughHasRect(owner->_session.Selection(), 1) &&
                            owner->_session.history.UndoCount() == 2 &&
                            owner->_session.history.UndoName() == L"Levels" &&
                            FollowthroughPixels(owner->_session.ActiveLayer()->asset->image, kApplied),
                        composite, mode, "real-ok-publishes-opening-coverage-after-wand"))
                return 5;
        }
    std::printf("panel-wand-followthrough-host.assertions=%d failures=0\n", assertions);
    return 0;
}
