#include "Composition.Tests.ChargedRaster.h"

#include "DeferredRelease.h"
#include "Document.h"
#include "Jobs.h"
#include "PixelBuffer.h"
#include "SharedPublicationMemory.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>

namespace RedPrism::CompositionTests
{
namespace
{
constexpr std::uint64_t kOriginalCharge = 1024ull * 1024;
constexpr std::uint32_t kFailureTimeoutMs = 5000;
constexpr std::array<std::uint8_t, 16> kPixels{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};

unsigned gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s charged-raster.%s\n", condition ? "PASS" : "FAIL", label);
    gFailures += condition ? 0u : 1u;
}

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    auto& samples = *static_cast<std::atomic<unsigned>*>(context);
    samples.fetch_add(1, std::memory_order_relaxed);
    snapshot = {4ull * 1024 * 1024 * 1024, 4ull * 1024 * 1024 * 1024};
    return true;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Observation final
{
    const DWORD ownerThread = GetCurrentThreadId();
    std::atomic<bool> payloadRetired{false};
    std::atomic<bool> payloadRetiredOnMta{false};
    std::atomic<bool> payloadRetiredOffOwner{false};
    JobTicket original;
    Raster published;
    std::weak_ptr<const void> rasterWeak;
    std::weak_ptr<SharedPublicationMemory> memoryWeak;
    const std::uint8_t* dataBeforeAdopt = nullptr;
    HRESULT bindResult = E_PENDING;
    JobResult result;
    unsigned completed = 0;
    bool workerOnMta = false;
    bool invalidPreservedOwnership = false;
    bool nullAliasOwnsLifetime = false;
};
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable : 4324)
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct Payload final : DeferredReleaseNode
{
    explicit Payload(Observation& value) noexcept : observation(value)
    {
        destroy = [](DeferredReleaseNode* node) noexcept
        {
            auto* payload = static_cast<Payload*>(node);
            auto& observation = payload->observation;
            APTTYPE apartment = APTTYPE_CURRENT;
            APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
            const HRESULT apartmentResult = CoGetApartmentType(&apartment, &qualifier);
            observation.payloadRetiredOnMta.store(SUCCEEDED(apartmentResult) && apartment == APTTYPE_MTA,
                                                  std::memory_order_relaxed);
            observation.payloadRetiredOffOwner.store(GetCurrentThreadId() != observation.ownerThread,
                                                     std::memory_order_relaxed);
            {
                std::unique_ptr<Payload> owned(payload);
                owned.reset();
            }
            observation.payloadRetired.store(true, std::memory_order_release);
        };
    }

    // The accounting owner is last to be destroyed, after Raster, pixels, and their payload bytes.
    std::shared_ptr<SharedPublicationMemory> memory;
    PixelBuffer pixels;
    Raster raster;
    Observation& observation;
};
#pragma warning(pop)

#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class ChargedRasterJob final : public Job
{
  public:
    ChargedRasterJob(JobScheduler& scheduler, Observation& observation) noexcept
        : _scheduler(scheduler), _observation(observation)
    {
    }

    ~ChargedRasterJob() override
    {
        if (_payload && !TryDeferRelease(*_payload, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
        static_cast<void>(_payload.release());
    }

    JobResult Run(JobContext& context) override
    {
        if (context.reservedMemoryBytes != kOriginalCharge)
            return {JobStatus::Failed, E_UNEXPECTED};
        APTTYPE apartment = APTTYPE_CURRENT;
        APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
        _observation.workerOnMta = GetCurrentThreadId() != _observation.ownerThread &&
                                   SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA;

        _payload = std::make_unique<Payload>(_observation);
        _payload->memory = SharedPublicationMemory::CreateForWork();
        _payload->pixels = PixelBuffer::TryAllocate(kPixels.size());
        if (!_payload->memory || !_payload->pixels)
            return {JobStatus::Failed, E_OUTOFMEMORY};

        std::copy(kPixels.begin(), kPixels.end(), _payload->pixels.Data());
        _observation.dataBeforeAdopt = _payload->pixels.Data();
        std::shared_ptr<const void> lifetime{_payload->memory, nullptr};
        _observation.nullAliasOwnsLifetime = !lifetime && lifetime.use_count() != 0;
        const auto owners = _payload->memory.use_count();
        const auto invalid = Raster::Adopt(0, 2, RasterFormat::Bgra8888, std::move(_payload->pixels), lifetime);
        _observation.invalidPreservedOwnership = !invalid.HasStorage() && _payload->pixels &&
                                                 _payload->pixels.Data() == _observation.dataBeforeAdopt &&
                                                 _payload->memory.use_count() == owners;
        _payload->raster =
            Raster::Adopt(2, 2, RasterFormat::Bgra8888, std::move(_payload->pixels), std::move(lifetime));
        return _payload->raster.HasStorage() ? JobResult{} : JobResult{JobStatus::Failed, E_INVALIDARG};
    }

    void Completed(JobResult result) noexcept override
    {
        ++_observation.completed;
        _observation.result = result;
        if (result.status != JobStatus::Finished || !_payload)
            return;

        _observation.bindResult =
            static_cast<HRESULT>(_payload->memory->BindAfterCompletedWork(_scheduler, _observation.original));
        if (FAILED(_observation.bindResult))
            return;

        _observation.memoryWeak = _payload->memory;
        _observation.rasterWeak = _payload->raster.WeakStorageForTest();
        _observation.published = _payload->raster;
    }

    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _observation.original = ticket;
        return false;
    }

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        if (&scheduler != &_scheduler || !_payload || !_payload->memory)
            return;
        _observation.original = ticket;
        if (!_payload->memory->IsBound() && FAILED(_payload->memory->BindForRetirement(scheduler, ticket)))
            std::terminate();
    }

  private:
    JobScheduler& _scheduler;
    Observation& _observation;
    std::unique_ptr<Payload> _payload;
};
#pragma warning(pop)

} // namespace

int RunChargedRasterTests()
{
    gFailures = 0;
    std::printf("EXPECTED charged-raster records: 14\n");

    Observation observation;
    std::atomic<unsigned> memorySamples{0};
    JobScheduler jobs;
    const HRESULT initialized = static_cast<HRESULT>(jobs.Initialize(1, {}, {SampleMemory, &memorySamples}));
    const bool sinkEnabled = SUCCEEDED(initialized) && jobs.EnableDeferredRelease();
    Expect(sinkEnabled, "actual-scheduler-and-deferred-release-sink");
    if (!sinkEnabled)
        return 1;

    std::unique_ptr<Job> job = std::make_unique<ChargedRasterJob>(jobs, observation);
    JobOptions options;
    options.memoryBytes = kOriginalCharge;
    options.deferredRetirement = true;
    options.allowMemoryWaiting = false;
    options.reserveRetainedMemory = true;
    JobTicket original;
    const HRESULT submitted = static_cast<HRESULT>(jobs.Submit(job, options, original));
    const bool settled = SUCCEEDED(submitted) && SettleJobs(jobs, kFailureTimeoutMs) == SettleResult::Settled;
    Expect(settled && original.generation != 0 && memorySamples.load(std::memory_order_relaxed) != 0,
           "one-original-preclaimed-descriptor-settles");

    const bool bound = observation.completed == 1 && observation.result.status == JobStatus::Finished &&
                       SUCCEEDED(observation.bindResult) && observation.published.HasStorage() &&
                       jobs.ReservedMemoryBytes() == kOriginalCharge;
    Expect(bound, "actual-successful-completed-binds-original-charge-before-publish");
    Expect(bound && observation.workerOnMta, "original-useful-body-executes-off-owner-on-mta");
    Expect(bound && observation.invalidPreservedOwnership, "invalid-adopt-preserves-input-and-lifetime-ownership");
    Expect(bound && observation.nullAliasOwnsLifetime, "null-alias-control-block-retains-original-lifetime");

    const bool adoptedBytes = bound && observation.published.Data() == observation.dataBeforeAdopt &&
                              std::equal(kPixels.begin(), kPixels.end(), observation.published.Data());
    Expect(adoptedBytes, "charged-adopt-preserves-buffer-address-and-literal-bgra-bytes");

    PixelBuffer regularPixels = PixelBuffer::TryAllocate(kPixels.size());
    const std::uint8_t* regularAddress = regularPixels.Data();
    if (regularPixels)
        std::copy(kPixels.begin(), kPixels.end(), regularPixels.Data());
    Raster regular = Raster::Adopt(2, 2, RasterFormat::Bgra8888, std::move(regularPixels));
    const bool regularPath = regular.HasStorage() && regular.Data() == regularAddress &&
                             std::equal(kPixels.begin(), kPixels.end(), regular.Data()) &&
                             jobs.ReservedMemoryBytes() == kOriginalCharge;
    Expect(regularPath, "ordinary-adopt-retains-uncharged-path");
    Expect(regularPath && bound && observation.published.ByteSize() == regular.ByteSize() &&
               observation.published.RetirementNodeCount() == regular.RetirementNodeCount() &&
               observation.published.StorageRetirementMemoryBytes() == regular.StorageRetirementMemoryBytes() + 32 &&
               observation.published.RetirementMemoryBytes() == regular.RetirementMemoryBytes() + 32,
           "two-additional-lifetime-handles-are-charged-without-pixel-or-node-growth");
    regular = {};

    const bool payloadJoined = settled && observation.payloadRetired.load(std::memory_order_acquire) &&
                               observation.payloadRetiredOnMta.load(std::memory_order_relaxed) &&
                               observation.payloadRetiredOffOwner.load(std::memory_order_relaxed) &&
                               !observation.memoryWeak.expired() && jobs.ReservedMemoryBytes() == kOriginalCharge;
    Expect(payloadJoined, "wrapper-payload-joins-on-actual-mta-sink-with-reader-charge-retained");

    Raster reader = observation.published;
    observation.published = {};
    Expect(SettleJobs(jobs, kFailureTimeoutMs) == SettleResult::Settled && reader.HasStorage() &&
               !observation.rasterWeak.expired() && reader.Data() == observation.dataBeforeAdopt &&
               jobs.ReservedMemoryBytes() == kOriginalCharge,
           "copied-reader-keeps-original-storage-and-charge-after-publication-release");
    reader = {};
    const bool rasterRetired = SettleJobs(jobs, kFailureTimeoutMs) == SettleResult::Settled &&
                               observation.rasterWeak.expired() && !observation.memoryWeak.expired() &&
                               jobs.ReservedMemoryBytes() == kOriginalCharge && jobs.ReleasingMemoryBytes() == 0;
    Expect(rasterRetired, "owner-last-raster-release-drains-while-control-block-retains-memory");

    observation.rasterWeak.reset();
    const bool weakMemoryStillCharged =
        observation.memoryWeak.expired() && jobs.ReservedMemoryBytes() == kOriginalCharge;
    Expect(weakMemoryStillCharged, "shared-memory-weak-control-retains-original-charge");

    observation.memoryWeak.reset();
    // The final allocator publishes the original lease's release acknowledgement. The owner consumes
    // that event before the accounting is refunded; weak destruction itself never mutates the budget.
    const bool refunded = SettleJobs(jobs, kFailureTimeoutMs) == SettleResult::Settled &&
                          jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0 && jobs.IsIdle();
    Expect(refunded, "final-shared-memory-weak-release-refunds-charge-and-settles-idle");

    jobs.Close();
    return gFailures == 0 ? 0 : 1;
}
} // namespace RedPrism::CompositionTests
