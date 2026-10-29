#include "Application.Tests.RowPublication.h"

#include "Jobs.h"
#include "LayerRowsWork.h"
#include "PreparedLayerRows.h"
#include "SharedPublicationMemory.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kTimeoutMs = 10000;
constexpr std::uint64_t kWholeAdmissionBytes = 2ull * 1024 * 1024;
constexpr std::uint64_t kAvailableBytes = kMemoryAdmissionHeadroomBytes + 64ull * 1024 * 1024;
constexpr std::uint64_t kDocumentSerial = 0x5055424c49534831ull;
constexpr Guid kDocumentId{{0x51, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 1}};
constexpr Guid kLayerId{{0x51, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 2}};
constexpr const wchar_t* kLayerLabel = L"Publication target";
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s prepared-layer-publication.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    condition ? ++gPassed : ++gFailed;
}

[[noreturn]] void SetupFailure(const char* name, DWORD error = 0) noexcept
{
    std::printf("FAIL prepared-layer-publication.setup.%s error=%lu\n", name, error);
    std::fflush(stdout);
    std::_Exit(1);
}

wil::unique_handle MakeEvent(const char* name)
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
        SetupFailure(name, GetLastError());
    return event;
}

void Wait(HANDLE event, const char* name) noexcept
{
    const DWORD result = event ? WaitForSingleObject(event, kTimeoutMs) : WAIT_FAILED;
    if (result != WAIT_OBJECT_0)
        SetupFailure(name, result == WAIT_FAILED ? GetLastError() : result);
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Sampler final
{
    std::atomic<unsigned int> calls{0};
};
#pragma warning(pop)

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    static_cast<Sampler*>(context)->calls.fetch_add(1, std::memory_order_relaxed);
    snapshot = {kAvailableBytes, kAvailableBytes};
    return true;
}

void Start(JobScheduler& scheduler, Sampler& sampler)
{
    if (scheduler.Initialize(1, {}, {&SampleMemory, &sampler}) != S_OK)
        SetupFailure("scheduler-initialize");
    if (!scheduler.EnableDeferredRelease())
        SetupFailure("deferred-release-enable");
}

CanvasDocument MakeDocument()
{
    DocumentFixture fixture;
    fixture.id = kDocumentId;
    fixture.width = 1;
    fixture.height = 1;
    ImageLayer layer = ImageLayer::Blank(kLayerLabel, {1, 1});
    layer.id = kLayerId;
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Probe final
{
    std::mutex mutex;
    std::shared_ptr<PreparedLayerRows> stagingRows;
    std::shared_ptr<const PreparedLayerRows> publishedRows;
    std::weak_ptr<PreparedLayerRows> weakRows;
    std::weak_ptr<SharedPublicationMemory> weakPublication;
    HANDLE publicationReady = nullptr;
    HANDLE allowPreparation = nullptr;
    HANDLE rowsReady = nullptr;
    HANDLE allowCompletion = nullptr;
    JobTicket ticket{};
    std::atomic<DWORD> runThread{0};
    std::atomic<unsigned int> samplerCallsBeforeBind{0};
    std::atomic<unsigned int> samplerCallsAfterBind{0};
    unsigned int runCount = 0;
    unsigned int retirementCount = 0;
    unsigned int deferCount = 0;
    unsigned int completionCount = 0;
    std::int32_t outsideBind = E_UNEXPECTED;
    std::int32_t unboundRowsBind = E_UNEXPECTED;
    std::int32_t wrongTicketBind = E_UNEXPECTED;
    std::int32_t validPublicationBind = E_UNEXPECTED;
    std::int32_t validRowsBind = E_UNEXPECTED;
    std::int32_t duplicateRowsBind = E_UNEXPECTED;
    std::int32_t outsideRebind = E_UNEXPECTED;
    bool unboundSeal = true;
    bool validSeal = false;
    bool wrongTicketPreservedUnbound = false;
    bool duplicateRowsPreserved = false;
    bool outsideBindSamplerPreserved = false;
    bool unboundRowsChargePreserved = false;
    bool duplicateSeal = true;
    bool resumeAccepted = false;
    bool sameTicket = true;
    bool runOnWorker = false;
    bool wrapperAliasDroppedOnWorker = false;
    bool publishedAliasKeptWrapper = false;
    bool callbackChargeExact = false;
    bool exactCallbackIdentity = false;
    bool completedChargeExact = false;
    std::uint64_t chargeBeforeBind = 0;
    std::uint64_t chargeAfterBind = 0;
    std::uint64_t chargeAfterRefusals = 0;
    std::uint64_t finalCallbackCharge = 0;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class PublicationRowsJob final : public Job
{
  public:
    PublicationRowsJob(Probe& probe, Sampler& sampler, CanvasDocument source, LayerRowsAdmission admission)
        : _probe(probe), _sampler(sampler), _source(std::move(source)), _admission(admission)
    {
    }

    JobResult Run(JobContext&) override
    {
        ++_probe.runCount;
        _probe.runThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        _probe.runOnWorker = GetCurrentThreadId() != _ownerThread;
        if (_probe.runCount == 1)
        {
            _publication = SharedPublicationMemory::CreateForWork();
            {
                const std::lock_guard lock{_probe.mutex};
                _probe.weakPublication = _publication;
            }
            if (!SetEvent(_probe.publicationReady))
                SetupFailure("publication-ready-signal", GetLastError());
            Wait(_probe.allowPreparation, "allow-preparation-wait");

            auto rows = LayerRowsWork::Prepare(_source, {}, L"[hidden {0}]", _admission);
            if (!rows)
                SetupFailure("layer-rows-prepare");
            _source = {};
            _rows = PreparedLayerRows::Create(std::move(rows), kWholeAdmissionBytes, kDocumentSerial,
                                              PreparedLayerRows::FirstItemIdentity, nullptr);
            if (!_rows)
                SetupFailure("prepared-layer-rows-create");
            {
                const std::lock_guard lock{_probe.mutex};
                _probe.stagingRows = _rows;
            }
            if (!SetEvent(_probe.rowsReady))
                SetupFailure("rows-ready-signal", GetLastError());
            Wait(_probe.allowCompletion, "allow-completion-wait");
        }
        else
        {
            // The actual second worker stage retires the original mutable wrapper alias. Publication owns it now.
            _rows.reset();
            _probe.wrapperAliasDroppedOnWorker = true;
            _probe.publishedAliasKeptWrapper = static_cast<bool>(_probe.publishedRows);
        }
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        ++_probe.retirementCount;
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        ++_probe.deferCount;
        _probe.sameTicket = _probe.sameTicket && ticket == _probe.ticket;
        if (result.status != JobStatus::Finished || !_publication || !_rows)
            return false;
        if (_probe.deferCount != 1)
            return false;

        _probe.exactCallbackIdentity = _scheduler->InAcknowledgedJobCallback(ticket);
        _probe.chargeBeforeBind = _scheduler->ReservedMemoryBytes();
        _probe.samplerCallsBeforeBind.store(_sampler.calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
        JobTicket wrong = ticket;
        ++wrong.generation;
        _probe.wrongTicketBind = _publication->BindForPublication(*_scheduler, wrong);
        _probe.wrongTicketPreservedUnbound = !_publication->IsBound() && _publication->Bytes() == 0;
        _probe.chargeAfterRefusals = _scheduler->ReservedMemoryBytes();
        _probe.validPublicationBind = _publication->BindForPublication(*_scheduler, ticket);
        if (_probe.validPublicationBind != S_OK)
            SetupFailure("exact-callback-publication-bind", static_cast<DWORD>(_probe.validPublicationBind));

        const std::shared_ptr<const SharedPublicationMemory> immutablePublication = _publication;
        _probe.validRowsBind = _rows->BindPublicationMemory(immutablePublication);
        _probe.validSeal = _rows->SealForPublication();
        _probe.duplicateRowsBind = _rows->BindPublicationMemory(immutablePublication);
        _probe.duplicateSeal = _rows->SealForPublication();
        _probe.duplicateRowsPreserved = _rows->HasMemoryLease() && _rows->MemoryBytes() == kWholeAdmissionBytes;
        _probe.publishedRows = _rows;
        _probe.weakRows = _rows;
        _probe.chargeAfterBind = _scheduler->ReservedMemoryBytes();
        _probe.callbackChargeExact =
            _probe.chargeBeforeBind == kWholeAdmissionBytes && _probe.chargeAfterBind == kWholeAdmissionBytes;
        _probe.samplerCallsAfterBind.store(_sampler.calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
        _probe.resumeAccepted = _scheduler->Resume(ticket);
        return true;
    }

    void Completed(JobResult result) noexcept override
    {
        ++_probe.completionCount;
        _probe.sameTicket = _probe.sameTicket && result.status == JobStatus::Finished;
        _probe.finalCallbackCharge = _scheduler->ReservedMemoryBytes();
        _probe.completedChargeExact = _probe.finalCallbackCharge == kWholeAdmissionBytes;
    }

    void SetOwnerThread(DWORD thread) noexcept
    {
        _ownerThread = thread;
    }
    void SetScheduler(JobScheduler& scheduler) noexcept
    {
        _scheduler = &scheduler;
    }

  private:
    Probe& _probe;
    Sampler& _sampler;
    CanvasDocument _source;
    LayerRowsAdmission _admission;
    std::shared_ptr<SharedPublicationMemory> _publication;
    std::shared_ptr<PreparedLayerRows> _rows;
    JobScheduler* _scheduler = nullptr;
    DWORD _ownerThread = 0;
};
#pragma warning(pop)

void PumpToFirstPublication(JobScheduler& scheduler, Probe& probe)
{
    const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
    while (probe.deferCount == 0)
    {
        (void)DrainCompletions(scheduler);
        if (probe.deferCount != 0)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            SetupFailure("first-publication-deadline", ERROR_TIMEOUT);
        if (scheduler.HasUiWork())
            continue;
        const DWORD wait =
            WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), static_cast<DWORD>(deadline - now));
        if (wait != WAIT_OBJECT_0)
            SetupFailure("first-publication-wake", wait == WAIT_FAILED ? GetLastError() : wait);
    }
}

void SettleOrFail(JobScheduler& scheduler, const char* name)
{
    if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure(name, ERROR_TIMEOUT);
}

void Submit(JobScheduler& scheduler, std::unique_ptr<Job>& job, Probe& probe)
{
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = kWholeAdmissionBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    const std::int32_t result = scheduler.Submit(job, options, probe.ticket);
    if (result != S_OK || job)
        SetupFailure("job-submit", static_cast<DWORD>(result));
}

void VerifyPreparedRows(PreparedLayerRows& rows)
{
    const auto data = rows.Rows();
    const auto labels = rows.DisplayText();
    const auto row = rows.FindVisibleRow(kLayerId);
    const auto item = rows.FindVisibleItem(PreparedLayerRows::FirstItemIdentity);
    Expect(data.size() == 1 && labels.size() == 1 && data[0].id == kLayerId,
           "actual-worker-rows-preserve-single-guid-row");
    Expect(labels.size() == 1 && labels[0] == kLayerLabel, "actual-worker-rows-preserve-literal-display-label");
    Expect(row && *row == 0 && rows.ItemIdAt(0) == PreparedLayerRows::FirstItemIdentity && item && *item == 0,
           "actual-worker-rows-preserve-guid-and-item-identity-lookups");
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct LifetimeFixture final
{
    wil::unique_handle publicationReady = MakeEvent("publication-ready-event");
    wil::unique_handle allowPreparation = MakeEvent("allow-preparation-event");
    wil::unique_handle rowsReady = MakeEvent("rows-ready-event");
    wil::unique_handle allowCompletion = MakeEvent("allow-completion-event");
    Probe probe;
    Sampler sampler;
    JobScheduler scheduler;

    LifetimeFixture()
    {
        probe.publicationReady = publicationReady.get();
        probe.allowPreparation = allowPreparation.get();
        probe.rowsReady = rowsReady.get();
        probe.allowCompletion = allowCompletion.get();
        Start(scheduler, sampler);
    }
};
#pragma warning(pop)

void BuildAndPublish(LifetimeFixture& fixture)
{
    std::weak_ptr<SharedPublicationMemory> weakPublication;
    CanvasDocument source = MakeDocument();
    const LayerRowsAdmission admission = LayerRowsAdmission::Estimate(source, 0, L"[hidden {0}]");
    const std::uint64_t rowsBound =
        admission.Bytes() + PreparedLayerRows::OwnershipAllowance(1) + SharedPublicationMemory::OwnershipAllowance();
    Expect(admission.IsValid() && rowsBound <= kWholeAdmissionBytes,
           "fixed-2mib-admission-covers-row-and-both-ownership-allowances");
    if (!admission.IsValid() || rowsBound > kWholeAdmissionBytes)
        SetupFailure("fixture-admission-exceeds-literal-bound");

    auto derivedJob =
        std::make_unique<PublicationRowsJob>(fixture.probe, fixture.sampler, std::move(source), admission);
    auto* concrete = derivedJob.get();
    concrete->SetOwnerThread(GetCurrentThreadId());
    concrete->SetScheduler(fixture.scheduler);
    std::unique_ptr<Job> job = std::move(derivedJob);
    Submit(fixture.scheduler, job, fixture.probe);

    Wait(fixture.publicationReady.get(), "publication-ready");
    {
        const std::lock_guard lock{fixture.probe.mutex};
        weakPublication = fixture.probe.weakPublication;
    }
    auto publication = weakPublication.lock();
    if (!publication)
        SetupFailure("worker-publication-owner-missing");
    const auto outsideBefore = fixture.scheduler.ReservedMemoryBytes();
    const auto samplerBeforeOutsideBind = fixture.sampler.calls.load(std::memory_order_relaxed);
    fixture.probe.outsideBind = publication->BindForPublication(fixture.scheduler, fixture.probe.ticket);
    const auto outsideAfter = fixture.scheduler.ReservedMemoryBytes();
    fixture.probe.outsideBindSamplerPreserved =
        samplerBeforeOutsideBind == fixture.sampler.calls.load(std::memory_order_relaxed);
    Expect(fixture.probe.outsideBind == E_INVALIDARG && !publication->IsBound() && publication->Bytes() == 0 &&
               outsideBefore == kWholeAdmissionBytes && outsideAfter == outsideBefore &&
               fixture.probe.outsideBindSamplerPreserved,
           "unbound-outside-callback-bind-refuses-without-charge-or-lease-mutation");
    publication.reset();
    if (!SetEvent(fixture.allowPreparation.get()))
        SetupFailure("allow-preparation-signal", GetLastError());

    Wait(fixture.rowsReady.get(), "rows-ready");
    std::shared_ptr<PreparedLayerRows> stagingRows;
    {
        const std::lock_guard lock{fixture.probe.mutex};
        stagingRows = fixture.probe.stagingRows;
    }
    if (!stagingRows)
        SetupFailure("worker-prepared-rows-missing");
    VerifyPreparedRows(*stagingRows);
    std::shared_ptr<const SharedPublicationMemory> unboundPublication;
    const auto unboundChargeBefore = fixture.scheduler.ReservedMemoryBytes();
    fixture.probe.unboundRowsBind = stagingRows->BindPublicationMemory(unboundPublication);
    fixture.probe.unboundSeal = stagingRows->SealForPublication();
    fixture.probe.unboundRowsChargePreserved = fixture.scheduler.ReservedMemoryBytes() == unboundChargeBefore;
    Expect(fixture.probe.unboundRowsBind == E_INVALIDARG && !fixture.probe.unboundSeal &&
               !stagingRows->HasMemoryLease() && stagingRows->MemoryBytes() == 0 &&
               unboundChargeBefore == kWholeAdmissionBytes && fixture.probe.unboundRowsChargePreserved,
           "unbound-row-bind-and-seal-refuse-without-state-mutation");
    stagingRows.reset();
    {
        const std::lock_guard lock{fixture.probe.mutex};
        fixture.probe.stagingRows.reset();
    }
    if (!SetEvent(fixture.allowCompletion.get()))
        SetupFailure("allow-completion-signal", GetLastError());

    PumpToFirstPublication(fixture.scheduler, fixture.probe);
    SettleOrFail(fixture.scheduler, "two-stage-settle");

    if (!fixture.probe.publishedRows || fixture.probe.publishedRows->MemoryBytes() != kWholeAdmissionBytes)
        SetupFailure("published-rows-missing-or-wrong-charge");
    // A post-publication duplicate wrapper bind is refused by the sealed wrapper; the shared owner is queried
    // separately through its weak handle so no hidden strong owner changes the retirement test.
    {
        const std::lock_guard lock{fixture.probe.mutex};
        weakPublication = fixture.probe.weakPublication;
    }
    if (auto boundPublication = weakPublication.lock())
    {
        fixture.probe.outsideRebind = boundPublication->BindForPublication(fixture.scheduler, fixture.probe.ticket);
        Expect(fixture.probe.outsideRebind == E_INVALIDARG && boundPublication->IsBound() &&
                   boundPublication->Bytes() == kWholeAdmissionBytes &&
                   fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes,
               "occupied-outside-rebind-refuses-without-lease-or-charge-mutation");
    }
    else
        SetupFailure("bound-publication-owner-expired");

    Expect(fixture.probe.runOnWorker && fixture.probe.runCount == 2 && fixture.probe.retirementCount == 2 &&
               fixture.probe.deferCount == 2 && fixture.probe.completionCount == 1 && fixture.probe.sameTicket &&
               fixture.probe.resumeAccepted,
           "real-worker-resume-retirement-and-original-final-callback-counts");
    Expect(fixture.probe.wrongTicketBind == E_INVALIDARG && fixture.probe.wrongTicketPreservedUnbound &&
               fixture.probe.validPublicationBind == S_OK && fixture.probe.validRowsBind == S_OK &&
               fixture.probe.validSeal && fixture.probe.duplicateRowsBind == E_INVALIDARG &&
               fixture.probe.duplicateRowsPreserved && !fixture.probe.duplicateSeal &&
               fixture.probe.chargeAfterRefusals == kWholeAdmissionBytes,
           "exact-ticket-bind-succeeds-once-and-refusals-preserve-whole-charge");
    Expect(fixture.probe.samplerCallsBeforeBind.load(std::memory_order_relaxed) ==
                   fixture.probe.samplerCallsAfterBind.load(std::memory_order_relaxed) &&
               fixture.probe.callbackChargeExact && fixture.probe.completedChargeExact &&
               fixture.probe.exactCallbackIdentity && fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes,
           "publication-bind-is-query-free-and-final-callback-retains-exact-2mib");
    Expect(fixture.probe.wrapperAliasDroppedOnWorker && fixture.probe.publishedAliasKeptWrapper &&
               fixture.probe.weakRows.expired() == false,
           "second-real-worker-stage-drops-job-alias-while-published-owner-keeps-rows");
}

void ReleaseFinalOwnerOnWorker()
{
    LifetimeFixture fixture;
    BuildAndPublish(fixture);

    std::shared_ptr<const PreparedLayerRows> finalOwner = std::move(fixture.probe.publishedRows);
    std::weak_ptr<PreparedLayerRows> weakRows = std::move(fixture.probe.weakRows);
    if (!finalOwner || weakRows.expired())
        SetupFailure("worker-retirement-fixture-owner-missing");
    std::atomic<bool> expiredAfterStrongRelease{false};
    std::thread dropStrong(
        [owner = std::move(finalOwner), &expiredAfterStrongRelease]() mutable
        {
            owner.reset();
            expiredAfterStrongRelease.store(true, std::memory_order_release);
        });
    dropStrong.join();
    Expect(expiredAfterStrongRelease.load(std::memory_order_acquire) && weakRows.expired(),
           "worker-final-strong-release-destroys-rows-object-before-owner-drain");

    (void)DrainCompletions(fixture.scheduler);
    Expect(fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes,
           "worker-final-strong-release-keeps-charge-through-owner-drain-while-weak-lives");

    std::thread dropWeak([weak = std::move(weakRows)]() mutable { weak.reset(); });
    dropWeak.join();
    Expect(fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes,
           "foreign-final-weak-release-remains-charged-until-owner-acknowledgement");
    (void)DrainCompletions(fixture.scheduler);
    Expect(fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes &&
               fixture.scheduler.ReleasingMemoryBytes() == 0,
           "rows-weak-acknowledgement-still-retains-shared-accounting-weak-owner");
    std::thread dropAccountingWeak([weak = std::move(fixture.probe.weakPublication)]() mutable { weak.reset(); });
    dropAccountingWeak.join();
    Expect(fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes,
           "final-accounting-weak-worker-release-awaits-owner-acknowledgement");
    (void)DrainCompletions(fixture.scheduler);
    Expect(fixture.scheduler.ReservedMemoryBytes() == 0 && fixture.scheduler.ReleasingMemoryBytes() == 0,
           "worker-weak-control-block-release-acknowledgement-returns-exact-charge");
    fixture.scheduler.Close();
}

void ReleaseFinalOwnerOnOwnerAndAcknowledgeRows()
{
    LifetimeFixture fixture;
    BuildAndPublish(fixture);

    std::weak_ptr<PreparedLayerRows> weakRows = std::move(fixture.probe.weakRows);
    fixture.probe.publishedRows.reset(); // Actual owner-thread last strong release uses the scheduler sink.
    Expect(weakRows.expired() && fixture.scheduler.ReleasingMemoryBytes() == kWholeAdmissionBytes &&
               fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes,
           "owner-final-strong-release-defers-real-row-destruction-before-weak-release");
    SettleOrFail(fixture.scheduler, "deferred-row-destruction-settle");
    Expect(fixture.scheduler.ReleasingMemoryBytes() == 0 &&
               fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes && weakRows.expired(),
           "owner-drain-acknowledges-row-destruction-while-weak-control-block-keeps-charge");

    std::thread dropWeak([weak = std::move(weakRows)]() mutable { weak.reset(); });
    dropWeak.join();
    Expect(fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes,
           "owner-deferred-case-foreign-final-weak-release-waits-for-owner-ack");
    (void)DrainCompletions(fixture.scheduler);
    Expect(fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes &&
               fixture.scheduler.ReleasingMemoryBytes() == 0,
           "owner-deferred-rows-weak-ack-still-retains-shared-accounting-weak-owner");
    std::thread dropAccountingWeak([weak = std::move(fixture.probe.weakPublication)]() mutable { weak.reset(); });
    dropAccountingWeak.join();
    Expect(fixture.scheduler.ReservedMemoryBytes() == kWholeAdmissionBytes,
           "owner-deferred-final-accounting-weak-worker-release-awaits-owner-ack");
    (void)DrainCompletions(fixture.scheduler);
    Expect(fixture.scheduler.ReservedMemoryBytes() == 0 && fixture.scheduler.ReleasingMemoryBytes() == 0,
           "owner-deferred-case-final-weak-acknowledgement-returns-exact-charge");
    fixture.scheduler.Close();
}
} // namespace

int RunPreparedRowPublicationTests()
{
    ReleaseFinalOwnerOnWorker();
    ReleaseFinalOwnerOnOwnerAndAcknowledgeRows();
    std::printf("SUMMARY prepared-layer-publication passed=%u failed=%u assertions=%u\n", gPassed, gFailed,
                gPassed + gFailed);
    std::fflush(stdout);
    return gFailed == 0 ? 0 : 1;
}
