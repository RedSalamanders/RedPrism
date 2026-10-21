#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "ChromeHost.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Runtime.Tests.LayerRowsTestGate.h"
#include "Runtime.Tests.SizingWorkTestGate.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <span>
#include <vector>

namespace
{
constexpr std::array<std::uint8_t, 8> kSource{0, 0, 255, 255, 0, 255, 0, 255};
constexpr std::array<std::uint8_t, 32> kImage{0, 0, 255, 255, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255,
                                              0, 0, 255, 255, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255};
constexpr std::array<std::uint8_t, 32> kCanvas{0,   0, 255, 255, 0,   255, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                                               255, 0, 0,   255, 255, 0,   0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
constexpr std::array<std::uint8_t, 32> kExtension{0,   0, 0, 0,   0,   0, 0, 0,   255, 0, 0, 255, 255, 0, 0, 255,
                                                  255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};

std::optional<CanvasDocument> SizeSource()
{
    DocumentFixture fixture;
    fixture.width = 2;
    fixture.height = 1;
    auto layer = ImageLayer::Blank(L"Size final source", {2, 1});
    layer.transform.sampling = LayerSampling::Nearest;
    auto image = Raster::Make(2, 1, RasterFormat::Bgra8888, kSource.data(), kSource.size());
    if (!image.HasStorage())
        return std::nullopt;
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

bool RasterEquals(const Raster& image, int width, int height, std::span<const std::uint8_t> expected) noexcept
{
    if (image.Width() != width || image.Height() != height || image.ByteSize() != expected.size())
        return false;
    const auto* data = image.Data();
    return data && std::equal(expected.begin(), expected.end(), data);
}

bool CpuEquals(const CanvasDocument& document, bool canvas, const Guid& original) noexcept
{
    if (document.width != 4 || document.height != 2 || document.layers.size() != (canvas ? 2u : 1u))
        return false;
    const ImageLayer* source = nullptr;
    const ImageLayer* extension = nullptr;
    for (const auto& layer : document.layers)
    {
        if (layer.id == original)
            source = &layer;
        else
            extension = &layer;
    }
    if (!source || !source->asset)
        return false;
    return canvas ? RasterEquals(source->asset->image, 2, 1, kSource) && extension && extension->asset &&
                        RasterEquals(extension->asset->image, 4, 2, kExtension)
                  : RasterEquals(source->asset->image, 4, 2, kImage);
}

bool GpuEquals(RecordedDisplayGraph* graph, int width, int height, std::span<const std::uint8_t> expected) noexcept
{
    if (!graph || !graph->CompositionForTestOwner())
        return false;
    std::vector<std::uint8_t> actual;
    UINT actualWidth = 0, actualHeight = 0;
    return SUCCEEDED(graph->CompositionForTestOwner()->Readback(actual, actualWidth, actualHeight)) &&
           actualWidth == static_cast<UINT>(width) && actualHeight == static_cast<UINT>(height) &&
           actual.size() == expected.size() && std::equal(expected.begin(), expected.end(), actual.begin());
}
} // namespace

int Application::RunStandaloneSizeFinalPublicationSelfTest(HINSTANCE instance) noexcept
{
    enum class Mode
    {
        Image,
        Canvas,
        Cancel,
        HistoryCancel,
        Invocation,
        Restore,
        AdmissionFailure,
        Close,
        LateCancel,
        FirstRowsResume,
        FirstRowsCancel,
        FirstRowsClose,
        FirstRowsFailure
    };
    constexpr std::array modes{
        Mode::Image,           Mode::Canvas,          Mode::Cancel,           Mode::HistoryCancel,
        Mode::Invocation,      Mode::Restore,         Mode::AdmissionFailure, Mode::Close,
        Mode::LateCancel,      Mode::FirstRowsResume, Mode::FirstRowsCancel,  Mode::FirstRowsClose,
        Mode::FirstRowsFailure};
    constexpr std::array names{"image",
                               "canvas-extension",
                               "cancel",
                               "history-cancel",
                               "invocation",
                               "restore",
                               "final-admission-failure",
                               "joined-close",
                               "late-cancel-after-apply",
                               "first-rows-resume",
                               "first-rows-cancel",
                               "first-rows-close",
                               "first-rows-failure"};
    unsigned checks = 0, failures = 0;
    const char* scenario = "setup";
    const auto expect = [&](bool condition, const char* label) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s application-size-final.%s.%s\n", condition ? "PASS" : "FAIL", scenario, label);
        return condition;
    };
    const auto summary = wil::scope_exit(
        [&]() noexcept { std::printf("SUMMARY application-size-final checks=%u failures=%u\n", checks, failures); });

    for (std::size_t index = 0; index < modes.size(); ++index)
    {
        scenario = names[index];
        const auto mode = modes[index];
        const bool firstRows = mode >= Mode::FirstRowsResume;
        const bool canvas = mode == Mode::Canvas;
        const bool applies = mode == Mode::Image || canvas || mode == Mode::LateCancel || mode == Mode::FirstRowsResume;
        struct Observation final
        {
            Application* owner = nullptr;
            Guid layer;
            std::uint64_t generation = 0;
            int undo = 0;
            JobTicket original;
            unsigned publications = 0;
            bool refuseMemory = false, enabled = false, canvas = false, lateCancel = false, atomic = true;
            bool lateCancelAccepted = false;
        } observation;
        ApplicationProcessingTestProbe probe;
        probe.context = &observation;
        probe.workerCount = 2;
        probe.memory = {[](void* raw, MemorySnapshot& snapshot) noexcept
                        {
                            const auto available =
                                static_cast<Observation*>(raw)->refuseMemory ? 0ULL : 8ULL * 1024 * 1024 * 1024;
                            snapshot = {available, available};
                            return true;
                        },
                        &observation};
        probe.committedDisplayPublished = [](void* raw, RecordedDisplayGraph* graph) noexcept
        {
            auto& value = *static_cast<Observation*>(raw);
            auto& app = *value.owner;
            if (!value.enabled || app._session.Generation() == value.generation || !app._finalPublicationCurrent ||
                !app._sizingTicket)
                return;
            ++value.publications;
            value.atomic = value.atomic && graph == app._acknowledgedCommittedDisplay && app._sizingTicket &&
                           app._sizingTicket.value() == value.original && app._historyWork.Busy() && app._sizingStamp &&
                           app._jobs.InAcknowledgedJobCallback(value.original) && app._finalPublicationCurrent &&
                           app._session.history.UndoCount() == value.undo + 1 && app._preparedLayerRows &&
                           app._chrome &&
                           app._chrome->PreparedLayerAccessibilityForTest().get() == app._preparedLayerRows.get() &&
                           app._layerRowsPublishedKey == std::optional(app.CurrentLayerRowsKey()) &&
                           app._layerRowsCacheTicket == std::optional(value.original) &&
                           CpuEquals(app._session.Document().value(), value.canvas, value.layer) &&
                           app._viewport.followsFit && app._viewport.zoom == 2 && app._viewport.pan == Vec2{};
            if (value.lateCancel)
            {
                value.lateCancelAccepted =
                    app._jobs.Cancel(value.original) && app._historyWork.RequestCancel().has_value();
                ++app._sizeInvocation; // A committed result remains successful despite later invocation cancellation.
            }
        };
        std::unique_ptr<Application> owner;
        try
        {
            owner = std::make_unique<Application>(instance, true, L"", 0, nullptr, nullptr);
        }
        catch (const std::bad_alloc&)
        {
            return 5; // Explicit noexcept fixture construction boundary: fail the complete test.
        }
        observation.owner = owner.get();
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                {
                    owner->_processingTestProbe = nullptr;
                    owner->Shutdown();
                    owner.reset();
                    while (PumpMessages().quit)
                    {
                    }
                }
            });
        owner->_processingTestProbe = &probe;
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                        SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                        SUCCEEDED(owner->InitializeChrome()) && owner->EnableCommittedDisplayForTest() &&
                        owner->EnablePreparedLayerRows() && owner->_strokeFinishSource.Start(),
                    "actual-two-worker-WARP-owner-enabled"))
            return 5;
        owner->_selectionPixelFinalPublicationEnabled = true;
        std::shared_ptr<LayerRowsTestGate> rowsGate;
        if (firstRows)
        {
            rowsGate = std::make_shared<LayerRowsTestGate>();
            rowsGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            rowsGate->hold = mode != Mode::FirstRowsFailure;
            rowsGate->throwPrepare = mode == Mode::FirstRowsFailure;
            if (!expect(rowsGate->release != nullptr, "actual-first-row-worker-gate-created"))
                return 5;
            owner->_layerRowsTestGate = rowsGate;
        }
        bool drawFrames = true;
        const auto pumpUntil = [&](auto done) noexcept
        {
            const auto deadline = GetTickCount64() + 15000;
            while (!done())
            {
                if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                    return false;
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                if (done())
                    return true;
                if (drawFrames && owner->_frameInvalidated)
                    owner->RenderFrame();
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
        auto fixture = SizeSource();
        if (!expect(fixture.has_value(), "independent-red-green-source-created"))
            return 5;
        observation.layer = fixture->layers[0].id;
        owner->_session.ReplaceDocument(std::move(fixture.value()), observation.layer);
        owner->_session.history.Reset();
        owner->DocumentChanged();
        if (!expect(pumpUntil(
                        [&]() noexcept
                        {
                            return owner->_committedDisplay.HasCurrentPublicationOwner() &&
                                   (firstRows
                                        ? !owner->_preparedLayerRows &&
                                              (mode == Mode::FirstRowsFailure ? owner->_layerRowsRetryAvailable
                                                                              : rowsGate->preparations.load() == 1)
                                        : owner->_jobs.IsIdle() && owner->_layerRowsPublishedKey ==
                                                                       std::optional(owner->CurrentLayerRowsKey()));
                        }) &&
                        GpuEquals(owner->_acknowledgedCommittedDisplay, 2, 1, kSource),
                    firstRows ? "source-display-ready-before-first-row-publication"
                              : "source-display-and-first-rows-settled"))
            return 5;
        auto oldRows = owner->_preparedLayerRows;
        const auto oldGraph = owner->_acknowledgedCommittedDisplay;
        const auto oldItem = firstRows ? owner->_nextLayerRowItemId - 1 : oldRows->ItemIdAt(0);
        observation.generation = owner->_session.Generation();
        observation.undo = owner->_session.history.UndoCount();
        observation.canvas = canvas;
        observation.lateCancel = mode == Mode::LateCancel;
        observation.enabled = true;
        auto gate = std::make_shared<SizingWorkTestGate>();
        gate->afterPreparation = true;
        owner->_sizingWorkTestGate = gate;
        std::variant<CanvasSizeOptions, ImageSizeOptions> options = ImageSizeOptions{4, 2, 72, LayerSampling::Nearest};
        if (canvas)
            options = CanvasSizeOptions{4, 2, 0, CanvasExtensionColor{0, 0, 1}, std::nullopt};
        if (!expect(SUCCEEDED(owner->StartSizing(options)) && owner->_sizingTicket && owner->_sizingStamp,
                    "one-original-size-job-admitted"))
            return 5;
        observation.original = owner->_sizingTicket.value();
        if (!expect(pumpUntil([&]() noexcept { return gate->visits.load() == 1; }) &&
                        owner->_session.Generation() == observation.generation &&
                        owner->_session.Document()->width == 2 &&
                        owner->_session.history.UndoCount() == observation.undo &&
                        owner->_preparedLayerRows == oldRows && owner->_acknowledgedCommittedDisplay == oldGraph &&
                        observation.publications == 0 && gate->payloadAllocatedOffOwner.load(),
                    "prepared-private-CPU-keeps-history-rows-and-GPU-unpublished"))
            return 5;
        drawFrames = false; // Final work must complete while hidden without a RenderFrame/Present pump.
        // This 104x100, 1x backing view has literal fit 2 for the 4x2 result (96 DIP margin).
        owner->_viewport.viewSize = {104, 100};
        owner->_viewport.backingScale = 1;
        owner->_viewport.zoom = 17;
        owner->_viewport.pan = {7, 9};
        owner->_viewport.followsFit = false;
        if (mode == Mode::Close)
        {
            oldRows.reset();
            owner->Shutdown();
            if (!expect(observation.publications == 0 && gate->payloadRetired.load() &&
                            gate->payloadRetiredOffOwner.load() && owner->_jobs.ReservedMemoryBytes() == 0 &&
                            owner->_jobs.ReleasingMemoryBytes() == 0,
                        "joined-close-retires-original-without-publication"))
                return 5;
            if (!expect(gate->failureRetiredByOriginalWorker.load(),
                        "cancelled-CPU-result-retires-on-original-worker-without-a-release-job"))
                return 5;
            continue;
        }
        if (mode == Mode::Cancel && !owner->_jobs.Cancel(observation.original))
            return 5;
        if (mode == Mode::HistoryCancel && !owner->_historyWork.RequestCancel())
            return 5;
        if (mode == Mode::Invocation)
            ++owner->_sizeInvocation;
        if (mode == Mode::Restore)
        {
            owner->_session.SetOpacity(0.5);
            owner->_session.Undo();   // Identical source pixels and dimensions with a different restore authority.
            owner->DocumentChanged(); // The real host also requests the independently restored source epoch.
        }
        observation.refuseMemory = mode == Mode::AdmissionFailure;
        (void)SetEvent(gate->release.get());
        if (firstRows && mode != Mode::FirstRowsFailure)
        {
            if (!expect(pumpUntil([&]() noexcept { return owner->FinalPublicationPendingOwner(); }) &&
                            owner->_sizingTicket == std::optional(observation.original) &&
                            !owner->FinalPublicationReadyOwner() && !owner->_preparedLayerRows &&
                            owner->_session.Generation() == observation.generation &&
                            owner->_session.history.UndoCount() == observation.undo && observation.publications == 0,
                        "same-original-waits-without-ready-spin-or-early-CPU-history-rows-GPU-publication"))
                return 5;
            if (mode == Mode::FirstRowsClose)
            {
                owner->Shutdown();
                if (!expect(observation.publications == 0 && gate->payloadRetired.load() &&
                                gate->payloadRetiredOffOwner.load() && rowsGate->retired.load() &&
                                owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                            "joined-close-retires-both-originals-without-row-release-or-useful-publication"))
                    return 5;
                continue;
            }
            if (mode == Mode::FirstRowsCancel)
            {
                if (!expect(owner->_jobs.Cancel(observation.original) &&
                                pumpUntil([&]() noexcept { return !owner->_sizingTicket; }) &&
                                !owner->FinalPublicationPendingOwner() && !owner->_preparedLayerRows &&
                                observation.publications == 0 &&
                                owner->_session.Generation() == observation.generation &&
                                owner->_session.history.UndoCount() == observation.undo,
                            "cancel-wakes-and-retires-size-original-before-first-row-worker-joins"))
                    return 5;
            }
            (void)SetEvent(rowsGate->release.get());
        }
        if (!expect(pumpUntil(
                        [&]() noexcept
                        {
                            return !owner->_sizingTicket && !owner->_historyWork.Busy() && owner->_jobs.IsIdle() &&
                                   owner->_jobs.ReleasingMemoryBytes() == 0 &&
                                   ((mode != Mode::Restore && mode != Mode::FirstRowsCancel) ||
                                    (owner->_committedDisplay.HasCurrentPublicationOwner() &&
                                     owner->_layerRowsPublishedKey == std::optional(owner->CurrentLayerRowsKey())));
                        }),
                    "original-terminal-and-retirement-settled"))
            return 5;
        if (mode == Mode::Cancel &&
            !expect(gate->failureRetiredByOriginalWorker.load() && gate->payloadRetiredOffOwner.load(),
                    "cancelled-CPU-result-retires-on-original-worker-without-a-release-job"))
            return 5;
        observation.enabled = false;
        observation.refuseMemory = false;
        if (applies)
        {
            if (!expect(observation.publications == 1 && observation.atomic &&
                            (mode != Mode::LateCancel || observation.lateCancelAccepted) &&
                            owner->_session.history.UndoCount() == observation.undo + 1 &&
                            CpuEquals(owner->_session.Document().value(), canvas, observation.layer) &&
                            GpuEquals(owner->_acknowledgedCommittedDisplay, 4, 2, canvas ? kCanvas : kImage) &&
                            owner->_preparedLayerRows->ItemIdAt(0) == oldItem,
                        "one-original-publishes-literal-CPU-history-actual-chrome-rows-fit-and-GPU"))
                return 5;
            owner->_session.Undo();
            if (!expect(owner->_session.Document()->width == 2 && owner->_session.Document()->height == 1 &&
                            owner->_session.history.UndoCount() == observation.undo &&
                            owner->_session.history.CanRedo(),
                        "one-undo-restores-original-dimensions-and-history"))
                return 5;
            owner->_session.Redo();
            if (!expect(CpuEquals(owner->_session.Document().value(), canvas, observation.layer) &&
                            owner->_session.history.UndoCount() == observation.undo + 1,
                        "one-redo-restores-exact-final-pixels-and-dimensions"))
                return 5;
        }
        else if (!expect(observation.publications == 0 && owner->_session.Document()->width == 2 &&
                             owner->_session.Document()->height == 1 &&
                             ((mode == Mode::Restore || mode == Mode::FirstRowsCancel)
                                  ? owner->_committedDisplay.HasCurrentPublicationOwner() &&
                                        owner->_layerRowsPublishedKey == std::optional(owner->CurrentLayerRowsKey()) &&
                                        owner->_preparedLayerRows->ItemIdAt(0) == oldItem &&
                                        owner->_session.history.UndoCount() == observation.undo &&
                                        (mode != Mode::Restore ||
                                         (owner->_session.history.CanRedo() &&
                                          owner->_session.history.RedoName() == L"Layer Opacity")) &&
                                        GpuEquals(owner->_acknowledgedCommittedDisplay, 2, 1, kSource)
                                  : owner->_preparedLayerRows == oldRows &&
                                        owner->_acknowledgedCommittedDisplay == oldGraph) &&
                             RasterEquals(owner->_session.Document()->layers[0].asset->image, 2, 1, kSource),
                         "refused-stale-or-cancelled-original-preserves-committed-CPU-rows-and-GPU"))
            return 5;
        if (mode == Mode::FirstRowsFailure)
        {
            if (!expect(owner->_layerRowsRetryAvailable && observation.publications == 0 && !owner->_sizingTicket &&
                            owner->_session.history.UndoCount() == observation.undo,
                        "current-row-failure-terminates-size-original-without-publication"))
                return 5;
            owner->_layerRowsTestGate.reset();
            owner->RetryPreparedLayerRows();
            if (!expect(pumpUntil(
                            [&]() noexcept
                            {
                                return owner->_jobs.IsIdle() && owner->_preparedLayerRows &&
                                       owner->_layerRowsPublishedKey == std::optional(owner->CurrentLayerRowsKey());
                            }) &&
                            observation.publications == 0 && !owner->_sizingTicket &&
                            owner->_session.Document()->width == 2 &&
                            GpuEquals(owner->_acknowledgedCommittedDisplay, 2, 1, kSource),
                        "explicit-first-row-retry-restores-source-without-reviving-failed-size"))
                return 5;
        }
        if (!expect(gate->payloadRetired.load() && gate->payloadRetiredOffOwner.load(),
                    "large-source-history-payload-retires-on-original-worker"))
            return 5;
        oldRows.reset();
        owner->Shutdown();
        if (!expect(owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                    "joined-close-acknowledges-all-original-CPU-GPU-and-row-retirement"))
            return 5;
    }

    // A changed-size final source must also survive the real display owner's visibility/device
    // transitions. Keep the independent 129 predecessor predicates above exactly as qualified.
    enum class Recovery
    {
        SlotResume,
        SlotCancel,
        SlotClose,
        DeviceResume,
        DeviceFailure
    };
    constexpr std::array recoveryModes{Recovery::SlotResume, Recovery::SlotCancel, Recovery::SlotClose,
                                       Recovery::DeviceResume, Recovery::DeviceFailure};
    constexpr std::array recoveryNames{"slot-resume", "slot-cancel", "slot-close", "device-resume",
                                       "device-source-failure"};
    for (std::size_t index = 0; index < recoveryModes.size(); ++index)
    {
        const auto mode = recoveryModes[index];
        const bool slot = mode <= Recovery::SlotClose;
        const bool applies = mode == Recovery::SlotResume || mode == Recovery::DeviceResume;
        scenario = recoveryNames[index];
        struct Observation final
        {
            Application* owner = nullptr;
            Guid layer;
            std::uint64_t generation = 0;
            JobTicket original;
            bool refuseMemory = false, enabled = false, atomic = true;
            unsigned publications = 0;
        } observation;
        ApplicationProcessingTestProbe probe;
        probe.context = &observation;
        probe.workerCount = 2;
        probe.memory = {[](void* raw, MemorySnapshot& snapshot) noexcept
                        {
                            const auto available = static_cast<Observation*>(raw)->refuseMemory ? 0ULL : 8ULL << 30;
                            snapshot = {available, available};
                            return true;
                        },
                        &observation};
        probe.committedDisplayPublished = [](void* raw, RecordedDisplayGraph* graph) noexcept
        {
            auto& value = *static_cast<Observation*>(raw);
            auto& app = *value.owner;
            if (!value.enabled || app._session.Generation() == value.generation || !app._sizingTicket)
                return; // A recovered old-size source is not a final Size publication.
            ++value.publications;
            value.atomic = value.atomic && app._sizingTicket.value() == value.original &&
                           app._jobs.InAcknowledgedJobCallback(value.original) && app._historyWork.Busy() &&
                           app._finalPublicationCurrent && app._session.history.UndoCount() == 1 &&
                           graph == app._acknowledgedCommittedDisplay && app._preparedLayerRows && app._chrome &&
                           app._chrome->PreparedLayerAccessibilityForTest().get() == app._preparedLayerRows.get() &&
                           app._layerRowsPublishedKey == std::optional(app.CurrentLayerRowsKey()) &&
                           CpuEquals(app._session.Document().value(), false, value.layer) && app._viewport.followsFit &&
                           app._viewport.zoom == 2 && app._viewport.pan == Vec2{};
        };
        std::unique_ptr<Application> owner;
        try
        {
            owner = std::make_unique<Application>(instance, true, L"", 0, nullptr, nullptr);
        }
        catch (const std::bad_alloc&)
        {
            return 5; // Explicit noexcept fixture construction boundary: fail the entire fixture.
        }
        auto gate = std::make_shared<SizingWorkTestGate>();
        gate->afterPreparation = true;
        observation.owner = owner.get();
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                (void)SetEvent(gate->release.get());
                owner->_processingTestProbe = nullptr;
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            });
        owner->_processingTestProbe = &probe;
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!expect(gate->release && SUCCEEDED(owner->InitializeProcessing()) &&
                        SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                        SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome()) &&
                        owner->EnableCommittedDisplayForTest() && owner->EnablePreparedLayerRows() &&
                        owner->_strokeFinishSource.Start(),
                    "actual-recovery-owner-and-two-workers-enabled"))
            return 5;
        owner->_selectionPixelFinalPublicationEnabled = true;
        const auto until = [&](auto done) noexcept
        {
            const auto deadline = GetTickCount64() + 15000;
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
        auto source = SizeSource();
        if (!expect(source.has_value(), "literal-old-size-source-created"))
            return 5;
        observation.layer = source->layers[0].id;
        owner->_session.ReplaceDocument(std::move(source.value()), observation.layer);
        owner->_session.history.Reset();
        owner->DocumentChanged();
        if (!expect(until(
                        [&]() noexcept
                        {
                            return owner->_jobs.IsIdle() && owner->_committedDisplay.HasCurrentPublicationOwner() &&
                                   owner->_layerRowsPublishedKey == std::optional(owner->CurrentLayerRowsKey());
                        }) &&
                        GpuEquals(owner->_acknowledgedCommittedDisplay, 2, 1, kSource),
                    "actual-old-size-rows-and-GPU-ready"))
            return 5;
        auto oldRows = owner->_preparedLayerRows;
        const auto oldItem = oldRows->ItemIdAt(0);
        observation.generation = owner->_session.Generation();
        observation.enabled = true;
        owner->_sizingWorkTestGate = gate;
        if (!expect(SUCCEEDED(owner->StartSizing(ImageSizeOptions{4, 2, 72, LayerSampling::Nearest})) &&
                        owner->_sizingTicket && owner->_sizingStamp,
                    "one-size-original-admitted-before-transition"))
            return 5;
        observation.original = owner->_sizingTicket.value();
        if (!expect(until([&]() noexcept { return gate->visits.load() == 1; }) &&
                        owner->_session.Generation() == observation.generation &&
                        owner->_session.history.UndoCount() == 0 && observation.publications == 0 &&
                        owner->_preparedLayerRows == oldRows && gate->payloadAllocatedOffOwner.load(),
                    "private-new-size-CPU-held-before-device-or-slot-transition"))
            return 5;
        owner->_viewport.viewSize = {104, 100};
        owner->_viewport.backingScale = 1;
        owner->_viewport.zoom = 17;
        owner->_viewport.pan = {7, 9};
        owner->_viewport.followsFit = false;
        if (slot)
        {
            owner->_committedDisplay.SetSuspendedOwner(true);
            if (!expect(until([&]() noexcept { return !owner->_committedDisplay.HasPending(); }) &&
                            !owner->_committedDisplay.GraphForFrameOwner(),
                        "real-suspension-retires-old-size-source-before-final-capture"))
                return 5;
        }
        else
        {
            observation.refuseMemory = mode == Recovery::DeviceFailure;
            const auto previousDevice = owner->_renderer.DeviceGeneration();
            const auto recovered = owner->_renderer.RecoverDeviceForTest();
            owner->BindCompositionDevice();
            if (!expect(SUCCEEDED(recovered) && owner->_renderer.DeviceGeneration() != previousDevice &&
                            until(
                                [&]() noexcept
                                {
                                    return !owner->_committedDisplay.HasPending() &&
                                           (mode == Recovery::DeviceFailure
                                                ? owner->CurrentCommittedDisplayFailureOwner() == E_OUTOFMEMORY
                                                : owner->_committedDisplay.HasCurrentPublicationOwner());
                                }) &&
                            owner->_session.Generation() == observation.generation &&
                            owner->_session.history.UndoCount() == 0 && observation.publications == 0,
                        "actual-device-generation-rebuild-keeps-private-size-unpublished"))
                return 5;
        }
        (void)SetEvent(gate->release.get());
        if (slot)
        {
            if (!expect(until(
                            [&]() noexcept
                            {
                                return gate->finalWaitingForDisplay.load() && owner->FinalPublicationPendingOwner() &&
                                       !owner->FinalPublicationReadyOwner() && !owner->_jobs.HasUiWork();
                            }) &&
                            owner->_sizingTicket == std::optional(observation.original) &&
                            owner->_session.Generation() == observation.generation &&
                            owner->_session.history.UndoCount() == 0 && owner->_preparedLayerRows == oldRows &&
                            observation.publications == 0,
                        "same-size-original-parks-without-polling-or-CPU-history-row-publication"))
                return 5;
            if (mode == Recovery::SlotClose)
            {
                oldRows.reset();
                owner->Shutdown();
                if (!expect(gate->payloadRetired.load() && gate->payloadRetiredOffOwner.load() &&
                                observation.publications == 0 && !owner->_finalPublicationCurrent &&
                                !owner->_finalPublicationShutdownGpu && owner->_jobs.ReservedMemoryBytes() == 0 &&
                                owner->_jobs.ReleasingMemoryBytes() == 0,
                            "joined-close-retires-parked-size-original-and-all-charges"))
                    return 5;
                continue;
            }
            if (mode == Recovery::SlotCancel)
            {
                if (!expect(owner->_jobs.Cancel(observation.original), "cancel-addresses-parked-size-original"))
                    return 5;
            }
            else
                owner->_committedDisplay.SetSuspendedOwner(false);
        }
        if (!expect(until(
                        [&]() noexcept
                        {
                            return !owner->_sizingTicket && !owner->_historyWork.Busy() && owner->_jobs.IsIdle() &&
                                   owner->_jobs.ReleasingMemoryBytes() == 0;
                        }),
                    "original-size-terminal-and-worker-retirement-acknowledged"))
            return 5;
        if (applies)
        {
            if (!expect(observation.publications == 1 && observation.atomic &&
                            owner->_session.history.UndoCount() == 1 &&
                            owner->_preparedLayerRows->ItemIdAt(0) == oldItem &&
                            CpuEquals(owner->_session.Document().value(), false, observation.layer) &&
                            GpuEquals(owner->_acknowledgedCommittedDisplay, 4, 2, kImage),
                        "recovered-size-original-publishes-one-history-actual-chrome-fit-and-literal-GPU"))
                return 5;
        }
        else if (!expect(observation.publications == 0 && owner->_session.Generation() == observation.generation &&
                             owner->_session.history.UndoCount() == 0 && owner->_preparedLayerRows == oldRows &&
                             owner->_session.Document()->width == 2 && owner->_session.Document()->height == 1 &&
                             RasterEquals(owner->_session.Document()->layers[0].asset->image, 2, 1, kSource),
                         "cancelled-or-failed-size-preserves-literal-old-CPU-history-and-rows"))
            return 5;
        if (mode == Recovery::DeviceFailure)
        {
            observation.refuseMemory = false;
            observation.enabled = false;
            owner->_committedDisplay.RetryOwner();
            if (!expect(
                    until([&]() noexcept
                          { return owner->_jobs.IsIdle() && owner->_committedDisplay.HasCurrentPublicationOwner(); }) &&
                        GpuEquals(owner->_acknowledgedCommittedDisplay, 2, 1, kSource) &&
                        observation.publications == 0 && !owner->_sizingTicket &&
                        owner->_session.history.UndoCount() == 0,
                    "explicit-source-retry-rebuilds-old-size-without-reviving-failed-original"))
                return 5;
        }
        if (!expect(gate->payloadRetired.load() && gate->payloadRetiredOffOwner.load(),
                    "private-old-and-new-size-payload-retires-off-owner"))
            return 5;
        oldRows.reset();
        owner->Shutdown();
        if (!expect(owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                    "joined-close-refunds-size-source-rows-and-GPU-after-recovery"))
            return 5;
    }

    {
        scenario = "cpu-history-allocation-failure";
        auto owner = std::make_unique<Application>(instance, true, L"", 0, nullptr, nullptr);
        auto gate = std::make_shared<SizingWorkTestGate>();
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                (void)SetEvent(gate->release.get());
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!expect(gate->release && SUCCEEDED(owner->InitializeProcessing()) &&
                        SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                        SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome()) &&
                        owner->EnableCommittedDisplayForTest() && owner->EnablePreparedLayerRows() &&
                        owner->_strokeFinishSource.Start(),
                    "actual-selected-size-owner-and-history-fault-gate-created"))
            return 5;
        owner->_selectionPixelFinalPublicationEnabled = true;
        const auto until = [&](auto done) noexcept
        {
            const auto deadline = GetTickCount64() + 15000;
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
        auto document = SizeSource();
        if (!expect(document.has_value(), "literal-source-created"))
            return 5;
        const auto layer = document->layers[0].id;
        owner->_session.ReplaceDocument(std::move(document.value()), layer);
        owner->_session.history.Reset();
        owner->DocumentChanged();
        if (!expect(until(
                        [&]() noexcept
                        {
                            return owner->_jobs.IsIdle() && owner->_committedDisplay.HasCurrentPublicationOwner() &&
                                   owner->_layerRowsPublishedKey == std::optional(owner->CurrentLayerRowsKey());
                        }) &&
                        GpuEquals(owner->_acknowledgedCommittedDisplay, 2, 1, kSource),
                    "old-source-actual-chrome-and-GPU-ready"))
            return 5;
        auto rows = owner->_preparedLayerRows;
        const auto graph = owner->_acknowledgedCommittedDisplay;
        const auto generation = owner->_session.Generation();
        const auto revision = owner->_session.history.CurrentRevision();
        gate->historyFailure = DocumentHistory::PrepareStage::BeforeSnapshot;
        // Hold before resize, then fail real history construction after the private CPU resize succeeds.
        gate->afterPreparation = false;
        owner->_sizingWorkTestGate = gate;
        if (!expect(SUCCEEDED(owner->StartSizing(ImageSizeOptions{4, 2, 72, LayerSampling::Nearest})) &&
                        owner->_sizingTicket && until([&]() noexcept { return gate->visits.load() == 1; }),
                    "same-original-held-before-real-resize-and-injected-history-allocation"))
            return 5;
        if (!expect(gate->payloadAllocatedOffOwner.load() && gate->historyFaultHits.load() == 0 &&
                        owner->_session.Generation() == generation && owner->_session.history.UndoCount() == 0 &&
                        owner->_preparedLayerRows == rows && owner->_acknowledgedCommittedDisplay == graph,
                    "held-admitted-source-has-no-early-CPU-history-row-or-GPU-publication"))
            return 5;
        (void)SetEvent(gate->release.get());
        if (!expect(until(
                        [&]() noexcept
                        {
                            return !owner->_sizingTicket && !owner->_historyWork.Busy() && owner->_jobs.IsIdle() &&
                                   owner->_jobs.ReleasingMemoryBytes() == 0;
                        }) &&
                        gate->historyFaultHits.load() == 1 && gate->failureRetiredByOriginalWorker.load() &&
                        gate->payloadRetired.load() && gate->payloadRetiredOffOwner.load(),
                    "actual-history-bad-alloc-retires-private-result-on-original-worker-before-terminal"))
            return 5;
        if (!expect(owner->_session.Generation() == generation &&
                        owner->_session.history.CurrentRevision() == revision &&
                        owner->_session.history.UndoCount() == 0 && !owner->_session.history.CanRedo() &&
                        owner->_session.Document()->width == 2 && owner->_session.Document()->height == 1 &&
                        owner->_preparedLayerRows == rows && owner->_acknowledgedCommittedDisplay == graph &&
                        RasterEquals(owner->_session.Document()->layers[0].asset->image, 2, 1, kSource) &&
                        GpuEquals(graph, 2, 1, kSource),
                    "failed-real-history-construction-preserves-literal-source-and-actual-display"))
            return 5;
        rows.reset();
        owner->Shutdown();
        if (!expect(owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                    "joined-close-refunds-all-source-and-failed-original-ownership"))
            return 5;
    }
    return failures ? 5 : 0;
}
