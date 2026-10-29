#include "Host.Tests.WarpPreviewLifetime.h"

#include "Common/DeferredRelease.h"
#include "Common/Jobs.h"
#include "Common/SharedPublicationMemory.h"
#include "Common/WarpPreviewWork.h"

#include <wil/resource.h>
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <thread>
#include <utility>

namespace
{
constexpr std::uint64_t kAdmissionBytes = 4ull * 1024 * 1024;
constexpr std::uint64_t kAvailableBytes = kMemoryAdmissionHeadroomBytes + kAdmissionBytes;
constexpr std::uint32_t kTimeoutMs = 10000;
constexpr std::size_t kDeferredCapacity = 4;
constexpr std::array<std::uint8_t, 16> kPixels{12, 24, 48, 96, 34, 56, 78, 100, 90, 80, 70, 110, 200, 160, 120, 220};
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s warp-preview-lifetime.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    condition ? ++gPassed : ++gFailed;
}

[[noreturn]] void SetupFailure(const char* name, std::int32_t result = 0) noexcept
{
    std::printf("FAIL warp-preview-lifetime.setup.%s result=%ld\n", name, static_cast<long>(result));
    std::fflush(stdout);
    std::_Exit(1);
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kAvailableBytes, kAvailableBytes};
    return true;
}

struct DeferredQueue final
{
    std::array<DeferredReleaseNode*, kDeferredCapacity> nodes{};
    std::size_t count = 0;
    bool overflow = false;
    std::size_t destroyed = 0;
    std::thread::id destroyThread{};

    static void Enqueue(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& queue = *static_cast<DeferredQueue*>(context);
        if (queue.count == queue.nodes.size())
        {
            queue.overflow = true;
            return;
        }
        queue.nodes[queue.count++] = &node;
    }
};

struct DeferredSinkScope final
{
    DeferredQueue queue;
    bool installed = false;

    DeferredSinkScope() noexcept : installed(InstallDeferredReleaseSink(DeferredQueue::Enqueue, &queue)) {}
    ~DeferredSinkScope()
    {
        if (installed)
            (void)ClearDeferredReleaseSink(&queue);
    }
    DeferredSinkScope(const DeferredSinkScope&) = delete;
    DeferredSinkScope& operator=(const DeferredSinkScope&) = delete;
};

void DestroyQueuedOnJoinedWorker(DeferredQueue& queue)
{
    std::thread worker(
        [&queue]() noexcept
        {
            queue.destroyThread = std::this_thread::get_id();
            for (std::size_t index = 0; index < queue.count; ++index)
            {
                DeferredReleaseNode* const node = queue.nodes[index];
                if (node && node->destroy)
                {
                    node->destroy(node);
                    ++queue.destroyed;
                }
            }
        });
    worker.join();
    queue.nodes.fill(nullptr);
    queue.count = 0;
}

[[nodiscard]] Raster LiteralSource()
{
    const Raster source = Raster::Make(2, 2, RasterFormat::Bgra8888, kPixels.data(), kPixels.size());
    if (!source.HasStorage() || source.IsSparse())
        SetupFailure("literal-dense-2x2-source");
    return source;
}

[[nodiscard]] BrushSettings Settings() noexcept
{
    BrushSettings settings{};
    settings.diameter = 2.0;
    settings.hardness = 0.5;
    settings.opacity = 0.75;
    return settings;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct PreviewState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    Raster source;
    std::thread::id ownerThread{};
    std::thread::id workerThread{};
    wil::unique_handle preparedEvent;
    wil::unique_handle releaseEvent;
    std::shared_ptr<SharedPublicationMemory> memory;
    std::shared_ptr<const PreparedWarpPreview> preview;
    JobResult completion{JobStatus::Failed};
    std::int32_t missingTicketResult = E_UNEXPECTED;
    std::int32_t unknownGenerationResult = E_UNEXPECTED;
    std::int32_t bindResult = E_UNEXPECTED;
    std::int32_t sealedBindResult = E_UNEXPECTED;
    std::uint64_t callbackChargeBefore = 0;
    std::uint64_t callbackChargeAfter = 0;
    bool workerFactoryUnbound = false;
    bool workerSinkEmpty = false;
    bool workerAccessorValid = false;
    bool readerMatchesLiteral = false;
    bool callbacksCompleted = false;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class WarpPreviewJob final : public Job
{
  public:
    explicit WarpPreviewJob(PreviewState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        _state.workerThread = std::this_thread::get_id();
        _state.workerSinkEmpty = DeferredReleaseDetail::sink.enqueue == nullptr;
        _state.memory = SharedPublicationMemory::CreateForWork();
        if (!_state.memory)
            return {JobStatus::Failed, E_UNEXPECTED};
        _state.workerFactoryUnbound = !_state.memory->IsBound();

        auto stroke = WarpStroke::MakeForWork(_state.source, SmearMode::Liquify, Settings(), _state.ownerThread,
                                              context.cancellation, _state.memory);
        if (!stroke || !stroke->IsValid())
            return {JobStatus::Failed, E_UNEXPECTED};
        _state.workerAccessorValid = stroke->SourceViewForWork() != nullptr;
        _state.preview = PrepareWarpPreviewForWork(*stroke, _state.ownerThread, context.cancellation, _state.memory);
        if (!_state.preview)
            return {JobStatus::Failed, E_UNEXPECTED};

        WarpPreviewReader reader(_state.preview);
        std::array<std::uint8_t, kPixels.size()> actual{};
        _state.readerMatchesLiteral = reader.Read({0, 0, 2, 2}, actual, 8) && actual == kPixels;

        if (!SetEvent(_state.preparedEvent.get()))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE waits[]{_state.releaseEvent.get(), static_cast<HANDLE>(context.shutdownEvent)};
        const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits, FALSE, kTimeoutMs);
        if (wait == WAIT_OBJECT_0)
            return {};
        if (wait == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        return {JobStatus::Failed,
                wait == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }

    void Completed(JobResult result) noexcept override
    {
        _state.completion = result;
        _state.callbackChargeBefore = _state.scheduler->ReservedMemoryBytes();
        if (result.status == JobStatus::Finished && _state.memory)
        {
            JobTicket missing{};
            _state.missingTicketResult = _state.memory->BindAfterCompletedWork(*_state.scheduler, missing);
            JobTicket unknown = _state.ticket;
            ++unknown.generation;
            _state.unknownGenerationResult = _state.memory->BindAfterCompletedWork(*_state.scheduler, unknown);
            _state.bindResult = _state.memory->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
            _state.sealedBindResult = _state.memory->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
        }
        _state.callbackChargeAfter = _state.scheduler->ReservedMemoryBytes();
        _state.callbacksCompleted = true;
    }

  private:
    PreviewState& _state;
};
#pragma warning(pop)

void StartScheduler(JobScheduler& scheduler)
{
    if (scheduler.Initialize(1, {}, {SampleMemory, nullptr}) != S_OK)
        SetupFailure("scheduler-initialize");
}

void SubmitPreview(JobScheduler& scheduler, PreviewState& state)
{
    state.scheduler = &scheduler;
    state.source = LiteralSource();
    state.ownerThread = std::this_thread::get_id();
    state.preparedEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    state.releaseEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!state.preparedEvent || !state.releaseEvent)
        SetupFailure("create-gate-events", HRESULT_FROM_WIN32(GetLastError()));

    std::unique_ptr<Job> job = std::make_unique<WarpPreviewJob>(state);
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = kAdmissionBytes;
    options.reserveRetainedMemory = true;
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job)
        SetupFailure("job-submit");
    const DWORD prepared = WaitForSingleObject(state.preparedEvent.get(), kTimeoutMs);
    if (prepared != WAIT_OBJECT_0)
        SetupFailure("worker-prepared-gate",
                     prepared == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : HRESULT_FROM_WIN32(ERROR_TIMEOUT));
}

void TestLifetimePermutation(bool releaseWeakFirst)
{
    DeferredSinkScope sink;
    Expect(sink.installed, "owner-installs-deferred-sink");
    if (!sink.installed)
        return;

    JobScheduler scheduler;
    StartScheduler(scheduler);
    PreviewState state;
    SubmitPreview(scheduler, state);

    Expect(state.workerThread != state.ownerThread && state.workerSinkEmpty && state.workerFactoryUnbound &&
               state.workerAccessorValid,
           "admitted-worker-builds-preview-from-known-owner-with-unbound-accounting");
    Expect(state.preview && state.preview->ownerThread == state.ownerThread && state.readerMatchesLiteral &&
               !state.source.IsSparse(),
           "immutable-preview-reader-returns-independent-literal-dense-2x2-bytes");
    Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes && !state.memory->IsBound() && !state.callbacksCompleted,
           "original-admission-remains-held-while-worker-is-gated-before-completion");

    if (!SetEvent(state.releaseEvent.get()))
        SetupFailure("release-worker-gate", HRESULT_FROM_WIN32(GetLastError()));
    if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure("settle-original-job");

    Expect(state.callbacksCompleted && state.completion.status == JobStatus::Finished &&
               state.missingTicketResult == E_INVALIDARG && state.unknownGenerationResult == E_INVALIDARG,
           "original-completed-callback-rejects-missing-and-unknown-accounting-tickets");
    Expect(state.bindResult == S_OK && state.sealedBindResult == E_INVALIDARG && state.memory->IsBound() &&
               state.memory->Bytes() == kAdmissionBytes && state.memory->ResidentBytes() == kAdmissionBytes &&
               state.callbackChargeBefore == kAdmissionBytes && state.callbackChargeAfter == kAdmissionBytes &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "original-successful-callback-binds-whole-admission-once-and-refuses-sealed-rebind");

    bool sealedRefused = false;
    std::thread validator(
        [&]
        {
            const auto privateMemory = SharedPublicationMemory::CreateForWork();
            auto draft = WarpStroke::MakeForWork(state.source, SmearMode::Liquify, Settings(), state.ownerThread, {},
                                                 privateMemory);
            sealedRefused = draft && !PrepareWarpPreviewForWork(*draft, state.ownerThread, {}, state.memory) &&
                            draft->IsValid() && draft->Points().empty();
        });
    validator.join();
    Expect(sealedRefused, "preview-factory-refuses-sealed-accounting-before-snapshot-with-valid-draft-preserved");

    std::weak_ptr<const PreparedWarpPreview> weakPreview = state.preview;
    state.preview.reset();
    state.memory.reset();
    Expect(!sink.queue.overflow && sink.queue.count == 1 && sink.queue.nodes[0] &&
               sink.queue.nodes[0]->bytes < kDeferredReleaseThreshold,
           "owner-last-strong-preview-release-queues-preallocated-always-on-owner-node");
    if (!ClearDeferredReleaseSink(&sink.queue))
        SetupFailure("clear-owner-deferred-sink");
    sink.installed = false;

    if (releaseWeakFirst)
    {
        weakPreview.reset();
        Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes,
               "original-charge-survives-last-weak-before-queued-preview-graph-retirement");
        DestroyQueuedOnJoinedWorker(sink.queue);
    }
    else
    {
        DestroyQueuedOnJoinedWorker(sink.queue);
        Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes && weakPreview.expired(),
               "original-charge-survives-queued-graph-before-last-weak-control-retirement");
        weakPreview.reset();
    }
    Expect(sink.queue.destroyed == 1 && sink.queue.destroyThread != state.ownerThread,
           "queued-preview-graph-destroyed-on-joined-non-owner-worker");
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0,
           "owner-drain-releases-original-charge-only-after-graph-and-last-weak-control-retire");
    scheduler.Close();
}

struct NoRetainedSlotState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    std::shared_ptr<SharedPublicationMemory> memory;
    std::int32_t bindResult = E_UNEXPECTED;
    JobResult completion{JobStatus::Failed};
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class NoRetainedSlotJob final : public Job
{
  public:
    explicit NoRetainedSlotJob(NoRetainedSlotState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        _state.memory = SharedPublicationMemory::CreateForWork();
        return _state.memory ? JobResult{} : JobResult{JobStatus::Failed, E_UNEXPECTED};
    }
    void Completed(JobResult result) noexcept override
    {
        _state.completion = result;
        if (result.status == JobStatus::Finished)
            _state.bindResult = _state.memory->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
    }

  private:
    NoRetainedSlotState& _state;
};
#pragma warning(pop)

void TestCompletedFreeSlotTransfer()
{
    JobScheduler scheduler;
    StartScheduler(scheduler);
    NoRetainedSlotState state;
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<NoRetainedSlotJob>(state);
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = kAdmissionBytes;
    options.reserveRetainedMemory = false;
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job ||
        SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure("missing-retained-slot-job");
    // RetainMemory at exact Completed can use a currently free retained slot. Only the earlier
    // suspended publication boundary requires a pre-reserved slot (Jobs::RetainMemoryImpl).
    Expect(state.completion.status == JobStatus::Finished && state.bindResult == S_OK && state.memory &&
               state.memory->IsBound() && state.memory->Bytes() == kAdmissionBytes &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "exact-completed-job-transfers-original-charge-to-free-unreserved-retained-slot");
    state.memory.reset();
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "free-slot-transfer-releases-only-after-final-accounting-owner");
    scheduler.Close();
}
} // namespace

int RunWarpPreviewLifetimeTests()
{
    TestLifetimePermutation(false);
    TestLifetimePermutation(true);
    TestCompletedFreeSlotTransfer();
    std::printf("SUMMARY warp-preview-lifetime passed=%u failed=%u controls=%u\n", gPassed, gFailed, gPassed + gFailed);
    std::fflush(stdout);
    return gFailed == 0 ? 0 : 1;
}
