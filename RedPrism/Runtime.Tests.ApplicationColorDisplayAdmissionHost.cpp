#include "Application.h"

#include "Prompts.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Runtime.Tests.ColorWorkTestGate.h"

#include <cstdint>
#include <cstdio>

#include <iterator>
#include <memory>

namespace
{
constexpr std::uint64_t kMiB = 1024ull * 1024;
constexpr std::uint64_t kBlockerBytes = 64 * kMiB;

bool SampleAdmissionMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot.availablePhysicalBytes = kMemoryAdmissionHeadroomBytes + kBlockerBytes;
    snapshot.availableCommitBytes = kMemoryAdmissionHeadroomBytes + kBlockerBytes;
    return true;
}

bool Expect(bool condition, const char* name) noexcept
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    return condition;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct AdmissionBlocker final
{
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
};
#pragma warning(pop)

class AdmissionBlockerJob final : public Job
{
  public:
    explicit AdmissionBlockerJob(std::shared_ptr<AdmissionBlocker> gate) noexcept : _gate(std::move(gate)) {}

    JobResult Run(JobContext& context) override
    {
        (void)SetEvent(_gate->entered.get());
        const HANDLE events[]{_gate->release.get(), context.shutdownEvent};
        const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(std::size(events)), events, FALSE, 10000);
        if (wait == WAIT_OBJECT_0 + 1 || context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        return wait == WAIT_OBJECT_0 ? JobResult{} : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }

    void Completed(JobResult) noexcept override {}

  private:
    std::shared_ptr<AdmissionBlocker> _gate;
};

} // namespace

int Application::RunStandaloneColorDisplayAdmissionSelfTest(HINSTANCE instance) noexcept
{
    const auto runCase = [instance](bool cancelWaiting) noexcept -> bool
    {
        ApplicationProcessingTestProbe processing;
        processing.memory = {SampleAdmissionMemory, nullptr};
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        Raster source;
        std::shared_ptr<AdmissionBlocker> blocker;
        std::shared_ptr<ColorWorkTestGate> colorGate;
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                if (!owner)
                    return;
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
        {
            SelfTestFailure(L"Color display admission fixture could not initialize its hidden WARP host.\n");
            return false;
        }
        owner->BindCompositionDevice();

        owner->_session.CreateDocument(4, 1, false);
        const Guid layer = InsertPaintedLayer(owner->_session, L"Admission source", 4, 1, {}, 0, 0xFF707070u);
        if (!owner->_session.ActiveLayer() || !owner->_session.ActiveLayer()->asset || !owner->_session.Document())
        {
            SelfTestFailure(L"Color display admission fixture could not create its source image.\n");
            return false;
        }
        owner->_session.history.MarkSaved();
        source = owner->_session.ActiveLayer()->asset->image;
        const void* const sourceRoot = owner->_session.Document()->layers.Identity();
        const Guid historyRevision = owner->_session.history.CurrentRevision();
        const int undoCount = owner->_session.history.UndoCount();

        blocker = std::make_shared<AdmissionBlocker>();
        std::unique_ptr<Job> blockingJob = std::make_unique<AdmissionBlockerJob>(blocker);
        JobTicket blockerTicket;
        if (!blocker->entered || !blocker->release ||
            owner->_jobs.Submit(blockingJob, {JobClass::Background, 0, kBlockerBytes}, blockerTicket) < 0 ||
            !WaitForColorWorkTestEvent(owner->_jobs, blocker->entered.get(), DispatchMessageThunk, owner.get()) ||
            owner->_jobs.ReservedMemoryBytes() != kBlockerBytes)
        {
            (void)SetEvent(blocker->release.get());
            SelfTestFailure(L"Color display admission blocker did not enter with its full reservation.\n");
            return false;
        }

        colorGate = std::make_shared<ColorWorkTestGate>();
        owner->_colorWorkTestGate = colorGate;
        owner->_session.BeginColorEdit(FilterKind::Exposure, true);
        if (!owner->_session.PendingColorEdit())
        {
            (void)SetEvent(blocker->release.get());
            return false;
        }
        auto settings = owner->_session.PendingColorEdit()->settings;
        settings.exposure.exposure = 1.0;
        owner->_session.UpdateColorEdit(settings, true);
        const std::uint64_t sequence = owner->_session.ColorEditSequence();
        owner->DocumentChanged();

        const bool waitingState = owner->_colorPreviewTicket.has_value() &&
                                  owner->_jobs.ReservedMemoryBytes() == kBlockerBytes &&
                                  colorGate->visits.load() == 0 && !colorGate->captureReservationHeld.load() &&
                                  WaitForSingleObject(colorGate->entered.get(), 0) == WAIT_TIMEOUT;
        const JobTicket previewTicket = owner->_colorPreviewTicket.value_or(JobTicket{});
        bool reached = false;
        bool published = false;
        if (waitingState)
        {
            if (cancelWaiting)
            {
                // The preview keeps its submitted gate. The dismissal is a distinct job with its own capture;
                // do not let its legitimate admission overwrite the preview's no-capture observation.
                owner->_colorWorkTestGate.reset();
                const auto cancel = owner->QueuePanelCancel(owner->_session.PendingColorEdit()->openingSequence);
                const bool exactCancel = RunUiActions(owner->_jobs) && owner->_colorCommitTicket &&
                                         owner->_colorPreviewTicket == previewTicket &&
                                         owner->_jobs.ReservedMemoryBytes() == kBlockerBytes;
                (void)SetEvent(blocker->release.get());
                reached = SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) == SettleResult::Settled;
                const auto* display = owner->_session.DisplayDocument();
                published = exactCancel && reached && cancel.Read().outcome == HostOutcome::Succeeded &&
                            !owner->_session.HasColorEdit() && !owner->_colorPreviewTicket &&
                            !owner->_colorHistogramTicket && owner->_jobs.ReservedMemoryBytes() == 0 &&
                            colorGate->visits.load() == 0 && !colorGate->captureReservationHeld.load() &&
                            owner->_session.ActiveLayer() && owner->_session.ActiveLayer()->id == layer &&
                            owner->_session.ActiveLayer()->asset->image == source &&
                            owner->_session.Document()->layers.Identity() == sourceRoot &&
                            owner->_session.history.CurrentRevision() == historyRevision &&
                            owner->_session.history.UndoCount() == undoCount && display &&
                            display->layers.Identity() == sourceRoot;
            }
            else
            {
                (void)SetEvent(blocker->release.get());
                reached = WaitForColorWorkTestEvent(owner->_jobs, colorGate->entered.get(), DispatchMessageThunk,
                                                    owner.get());
                const bool sameTicketCaptured = reached && owner->_colorPreviewTicket == previewTicket &&
                                                colorGate->captureReservationHeld.load() &&
                                                colorGate->visits.load() == 1;
                (void)SetEvent(colorGate->release.get());
                reached = reached &&
                          SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) == SettleResult::Settled;
                const auto* display = owner->_session.DisplayDocument();
                published = sameTicketCaptured && reached && !owner->_colorPreviewTicket &&
                            owner->_jobs.ReservedMemoryBytes() == 0 && colorGate->visits.load() == 1 &&
                            colorGate->payloadRetired.load() && colorGate->payloadRetiredOffOwner.load() &&
                            owner->_session.PendingColorEdit() &&
                            owner->_session.PendingColorEdit()->previewSequence == sequence &&
                            owner->_session.ActiveLayer() && owner->_session.ActiveLayer()->id == layer &&
                            owner->_session.ActiveLayer()->asset->image == source &&
                            owner->_session.Document()->layers.Identity() == sourceRoot && display &&
                            display->layers.Identity() != sourceRoot &&
                            owner->_session.history.CurrentRevision() == historyRevision &&
                            owner->_session.history.UndoCount() == undoCount;
            }
        }

        const bool pass = Expect(waitingState && reached && published,
                                 cancelWaiting ? "color-display-host.admission.waiting-cancel-no-capture"
                                               : "color-display-host.admission.wait-resumes-same-record");
        owner->_processingTestProbe = nullptr;
        owner->Shutdown();
        owner.reset();
        while (PumpMessages().quit)
        {
        }
        return pass;
    };
    return runCase(false) && runCase(true) ? 0 : 5;
}
