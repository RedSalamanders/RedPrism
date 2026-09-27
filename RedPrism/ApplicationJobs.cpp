#include "Application.h"

#include <atomic>
#include <cstdlib>
#include <format>

namespace
{
struct ShutdownEvidence final
{
    wil::unique_handle started{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> returned{false};
    std::atomic<bool> mtaAlive{false};
    bool destroyedAfterRun = false;
    unsigned int applies = 0;
};

class ShutdownGateJob final : public Job
{
  public:
    explicit ShutdownGateJob(ShutdownEvidence& evidence) noexcept : _evidence(evidence) {}
    ~ShutdownGateJob() override
    {
        _evidence.destroyedAfterRun = _evidence.returned.load();
    }
    JobResult Run(JobContext& context) override
    {
        (void)SetEvent(_evidence.started.get());
        if (WaitForSingleObject(context.shutdownEvent, 10000) != WAIT_OBJECT_0)
        {
            SelfTestFailure(L"Worker shutdown failure-exit gate timed out.\n");
            std::_Exit(5); // failure bound only; do not destroy a still-borrowed test frame
        }
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        _evidence.mtaAlive.store(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA);
        _evidence.returned.store(true);
        return {};
    }
    void Completed(JobResult) noexcept override
    {
        ++_evidence.applies;
    }

  private:
    ShutdownEvidence& _evidence;
};
} // namespace

int Application::RunJobShutdownSelfTest() noexcept
{
    APTTYPE beforeApartment{};
    APTTYPEQUALIFIER beforeQualifier{};
    if (FAILED(CoGetApartmentType(&beforeApartment, &beforeQualifier)) ||
        (beforeApartment != APTTYPE_STA && beforeApartment != APTTYPE_MAINSTA))
    {
        SelfTestFailure(L"The failure-exit self-test requires the initialized UI STA.\n");
        return 5;
    }
    ShutdownEvidence evidence;
    if (!evidence.started)
    {
        SelfTestFailure(L"Could not create the worker shutdown gate.\n");
        return 5;
    }
    // A second owner needs no HWND/device. Exercise the same explicit Shutdown used by Main after a mode's early
    // failure return, while that owner still has a running callback and the caller's STA is alive.
    auto owner = std::make_unique<Application>(_instance, true, nullptr, 0);
    const auto shutdown = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
    if (FAILED(owner->_jobs.Initialize(1)))
    {
        SelfTestFailure(L"Could not start the failure-exit worker pool.\n");
        return 5;
    }
    const auto failWithJobInFlight = [&]() noexcept
    {
        std::unique_ptr<Job> job = std::make_unique<ShutdownGateJob>(evidence);
        JobTicket ticket;
        if (FAILED(owner->_jobs.Submit(job, {JobClass::Commit}, ticket)))
        {
            return 1;
        }
        if (WaitForSingleObject(evidence.started.get(), 10000) != WAIT_OBJECT_0)
        {
            SelfTestFailure(L"Failure-exit worker did not enter its gate.\n");
            std::_Exit(5);
        }
        return 5; // simulate a self-test/bench assertion failing before any successful close path
    };
    const int failedMode = failWithJobInFlight();
    owner->Shutdown();
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    if (failedMode != 5 || !owner->_shutdownComplete || !evidence.returned.load() || !evidence.mtaAlive.load() ||
        !evidence.destroyedAfterRun || evidence.applies != 0 || FAILED(CoGetApartmentType(&apartment, &qualifier)) ||
        apartment != beforeApartment || qualifier != beforeQualifier)
    {
        SelfTestFailure(std::format(
            L"Failure-exit shutdown: mode={}, closed={}, returned={}, workerMta={}, "
            L"destroyedAfterRun={}, applies={}, apartment={} (before {}), qualifier={} (before {}).\n",
            failedMode, owner->_shutdownComplete, evidence.returned.load(), evidence.mtaAlive.load(),
            evidence.destroyedAfterRun, evidence.applies, static_cast<int>(apartment),
            static_cast<int>(beforeApartment), static_cast<int>(qualifier), static_cast<int>(beforeQualifier)));
        return 5;
    }
    return 0;
}
