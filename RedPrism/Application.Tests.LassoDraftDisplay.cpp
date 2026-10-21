#include "Application.h"
#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "ChromeHost.h"
#include "LassoOpeningWork.h"
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
namespace
{
struct OpeningProbe final
{
    JobTicket ticket{};
    bool applied = false;
    bool resumedOriginal = false;
    std::weak_ptr<SharedPublicationMemory> memory;
};
CanvasDocument DraftHostFixture()
{
    DocumentFixture fixture;
    fixture.width = 64;
    fixture.height = 64;
    fixture.layers.push_back(ImageLayer::Blank(L"Draft host", Vec2{64, 64}));
    return std::move(fixture).Seal();
}
} // namespace
class Application::LassoDisplayTestOpeningJob final : public Job
{
  public:
    LassoDisplayTestOpeningJob(Application& owner, std::shared_ptr<OpeningProbe> probe) noexcept
        : _owner(owner), _probe(std::move(probe))
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (_cleanup)
            return {};
        if (_stage == 0)
        {
            _memory = SharedPublicationMemory::CreateForWork(PreparedLassoOpening::MemoryBytes(256));
            return _memory ? JobResult{} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
        }
        if (!_input)
            return {JobStatus::Failed, E_INVALIDARG};
        auto prepared =
            DocumentSession::PrepareLassoOpening(std::move(_input.value()), 256, context.cancellation, _memory);
        if (prepared)
            _prepared.emplace(std::move(prepared.value()));
        _input.reset();
        return _prepared                            ? JobResult{}
               : context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                    : JobResult{JobStatus::Failed, E_INVALIDARG};
    }
    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_cleanup || result.status != JobStatus::Finished)
        {
            _prepared.reset();
            _input.reset();
            _memory.reset();
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (_cleanup || result.status != JobStatus::Finished)
            return false;
        if (_stage == 0)
        {
            // The whole original creator was admitted before this owner capture.
            auto input = _owner._session.PrepareLassoOpeningInput({8, 8}, SelectionMode::New, false);
            if (!input)
                return false;
            _input.emplace(std::move(input.value()));
            _stage = 1;
            _probe->resumedOriginal = ticket == _probe->ticket;
            if (!_owner._jobs.Resume(ticket))
                std::terminate();
            return true;
        }
        if (_prepared && _owner._session.IsPreparedLassoOpeningCurrent(_prepared.value()))
            return false;
        _cleanup = true;
        if (!_owner._jobs.Resume(ticket))
            std::terminate();
        return true;
    }
    void Completed(JobResult result) noexcept override
    {
        if (!_cleanup && result.status == JobStatus::Finished && _prepared && _memory &&
            _memory->BindAfterCompletedWork(_owner._jobs, _probe->ticket) == S_OK)
            _probe->applied = _owner._session.ApplyPreparedLassoOpeningIfCurrent(_prepared.value());
        _probe->memory = _memory;
    }
    void RetainJoinedRetirement(JobScheduler& jobs, JobTicket ticket) noexcept override
    {
        if (_memory && !_memory->IsBound() && FAILED(_memory->BindForRetirement(jobs, ticket)))
            std::terminate();
    }

  private:
    Application& _owner;
    std::shared_ptr<OpeningProbe> _probe;
    std::shared_ptr<SharedPublicationMemory> _memory;
    std::optional<LassoOpeningInput> _input;
    std::optional<PreparedLassoOpening> _prepared;
    unsigned int _stage = 0;
    bool _cleanup = false;
};
int Application::RunStandaloneLassoDraftDisplaySelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0, failures = 0;
    const auto expect = [&](bool okay, const char* label) noexcept
    {
        std::printf("%s application-lasso-draft-display.%s\n", okay ? "PASS" : "FAIL", label);
        ++checks;
        failures += okay ? 0u : 1u;
        return okay;
    };
    for (unsigned int mode = 0; mode < 2; ++mode)
    {
        auto probe = std::make_shared<OpeningProbe>();
        auto owner =
            std::unique_ptr<Application>{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
        if (!expect(bool(owner), "real-owner-allocated"))
            return 5;
        auto close = wil::scope_exit(
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
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                        SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                        SUCCEEDED(owner->InitializeChrome()),
                    "real-hidden-warp-window-renderer-chrome-and-scheduler"))
            return 5;
        if (!expect(owner->EnableCommittedDisplayForTest() && owner->EnableSelectionOutlineForTest(),
                    "prepared-document-and-existing-overlay-acquired"))
            return 5;
        const auto routes = owner->_ownerWork.SourceCountForTest();
        if (!expect(owner->EnableLassoDraftDisplayForTest() && owner->_ownerWork.SourceCountForTest() == routes,
                    "cold-draft-child-acquires-without-extra-owner-route"))
            return 5;
        auto fixture = DraftHostFixture();
        const auto target = fixture.layers[0].id;
        owner->_session.ReplaceDocument(std::move(fixture), target);
        owner->DocumentChanged();
        const auto settle = [&]() noexcept
        { return SettleJobs(owner->_jobs, 30000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
        if (!expect(settle() && owner->_acknowledgedCommittedDisplay, "real-original-document-display-ready"))
            return 5;
        owner->RenderFrame(); // Establish actual chrome layout before the first source/grid request.
        JobOptions options;
        options.kind = JobClass::Selection;
        options.memoryBytes = PreparedLassoOpening::MemoryBytes(256) + sizeof(LassoDisplayTestOpeningJob) + 8192;
        options.allowMemoryWaiting = false;
        options.deferredRetirement = true;
        options.reserveRetainedMemory = true;
        std::unique_ptr<Job> job = std::make_unique<LassoDisplayTestOpeningJob>(*owner, probe);
        if (!expect(owner->_jobs.Submit(job, options, probe->ticket) == S_OK && !job,
                    "complete-original-opening-admitted-before-source-capture"))
            return 5;
        if (mode == 1)
        {
            expect(!owner->_session.PendingLasso(), "queued-opening-publishes-no-draft-or-borrow");
            owner->Shutdown();
            expect(owner->_shutdownComplete && !owner->_lassoDraftDisplayEnabled &&
                       owner->_jobs.ReservedMemoryBytes() == 0 && owner->_graphGpuBudget.ReservedOwner() == 0,
                   "queued-original-opening-close-joins-before-zero-refund");
            continue;
        }
        if (!expect(settle() && probe->applied && probe->resumedOriginal && owner->_session.PendingLasso() &&
                        owner->_session.PendingLasso()->points.empty(),
                    "same-original-opening-applies-bound-worker-arena-without-vector"))
            return 5;
        expect(owner->_session.ExtendBoundedLasso({48, 8}) && owner->_session.ExtendBoundedLasso({48, 48}),
               "bounded-owner-points-preserve-independent-three-corner-input");
        owner->SelectionChanged();
        expect(!owner->_selectionOutlineDisplay.BorrowLassoDraftFrameOwner(), "unprepared-current-grid-borrow-refuses");
        if (!expect(settle() && owner->_selectionOutlineDisplay.BorrowLassoDraftFrameOwner(),
                    "actual-worker-draft-and-immutable-gpu-view-publish"))
            return 5;
        const auto draws = owner->_renderer.LassoDraftDrawCount();
        const auto composites = owner->_composition.CompositeCount();
        const auto cpu = owner->_jobs.ReservedMemoryBytes(), gpu = owner->_graphGpuBudget.ReservedOwner();
        owner->RenderFrame();
        owner->RenderFrame();
        expect(owner->_renderer.LassoDraftDrawCount() == draws + 2 && owner->_renderer.LassoDraftFailure() == S_OK &&
                   owner->_composition.CompositeCount() == composites && owner->_jobs.ReservedMemoryBytes() == cpu &&
                   owner->_graphGpuBudget.ReservedOwner() == gpu && owner->_session.PendingLasso()->points.empty(),
               "actual-renderer-borrows-after-chrome-with-zero-owner-compose-or-point-vector");
        const auto hole = owner->_lassoDraftHole;
        expect(!owner->BorrowLassoDraftFrameOwner(hole[0] + 0.25f, hole[1], hole[2], hole[3]).color,
               "post-layout-hole-mismatch-excludes-draft-before-draw");
        owner->_viewport.pan.x += 0.25;
        expect(!owner->_selectionOutlineDisplay.BorrowLassoDraftFrameOwner(),
               "unsynchronized-real-view-change-excludes-old-source-grid");
        owner->SyncLassoOverlay();
        expect(settle() && owner->_selectionOutlineDisplay.BorrowLassoDraftFrameOwner(),
               "real-view-replacement-retires-original-context-and-worker");
        const auto device = owner->_gpuWork.DeviceGeneration();
        if (!expect(SUCCEEDED(owner->_renderer.RecoverDeviceForTest()), "actual-renderer-device-rebuild"))
            return 5;
        owner->BindCompositionDevice();
        expect(owner->_gpuWork.DeviceGeneration() != device &&
                   !owner->_selectionOutlineDisplay.BorrowLassoDraftFrameOwner(),
               "actual-new-device-refuses-old-color-view");
        expect(settle() && owner->_selectionOutlineDisplay.BorrowLassoDraftFrameOwner(),
               "actual-device-replacement-recreates-through-original-source-jobs");
        owner->RenderFrame();
        expect(owner->_renderer.LassoDraftFailure() == S_OK, "actual-rebuilt-renderer-draws-current-draft");
        owner->_session.CancelLasso();
        owner->SelectionChanged();
        expect(!owner->_selectionOutlineDisplay.BorrowLassoDraftFrameOwner(),
               "actual-session-draft-cancel-excludes-borrow-immediately");
        expect(settle() && probe->memory.expired(), "original-last-source-readers-retire-after-cancel");
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_lassoDraftDisplayEnabled && !owner->_overlayLassoDraft &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_graphGpuBudget.ReservedOwner() == 0,
               "real-joined-close-retires-current-cpu-and-graphics-roots");
    }
    std::printf("SUMMARY application-lasso-draft-display checks=%u failures=%u\n", checks, failures);
    return failures ? 5 : 0;
}
