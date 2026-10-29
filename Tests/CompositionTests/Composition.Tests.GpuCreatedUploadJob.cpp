#include "Composition.Tests.GpuCreatedUploadJob.h"

#include "GpuRasterUploadJob.h"
#include "OwnerWorkMultiplexer.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <span>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
constexpr std::uint64_t kCpuReservation = 1024 * 1024;
constexpr std::uint64_t kGpuAllowance = 80ull * 1024 * 1024;

void Expect(bool value, const char* name, const char* detail) noexcept
{
    std::printf("%s gpu-created-upload-job.%s.%s\n", value ? "PASS" : "FAIL", name, detail);
    gFailures += value ? 0 : 1;
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {16ull * 1024 * 1024 * 1024, 16ull * 1024 * 1024 * 1024};
    return true;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
struct GatedPump final : OwnerWorkPump
{
    GatedPump(OwnerWorkMultiplexer& source, std::atomic_bool& gate) noexcept : routes(source), paused(gate) {}
    bool HasPending() const noexcept override
    {
        return routes.HasPending();
    }
    bool HasReady() const noexcept override
    {
        return !paused.load(std::memory_order_acquire) && routes.HasReady();
    }
    void Pump() noexcept override
    {
        if (!paused.load(std::memory_order_acquire))
            routes.Pump();
    }
    OwnerWorkMultiplexer& routes;
    std::atomic_bool& paused;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct Fixture final
{
    GpuResidencyBudget budget; // outlives queue and every creation/retirement/adopted reservation
    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    std::atomic_bool pauseOwner{false}; // deterministic test pump gate; never production state
    GatedPump owner{routes, pauseOwner};
    GpuUploadQueue queue;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    bool initialized = false;

    bool Initialize() noexcept
    {
        constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level,
                                     1, D3D11_SDK_VERSION, device.put(), nullptr, context.put())))
            return false;
        if (scheduler.Initialize(1, {}, {StableMemory, nullptr}) != S_OK)
            return false;
        initialized = true;
        if (!scheduler.EnableDeferredRelease() || !scheduler.RegisterOwnerWork(owner) ||
            FAILED(queue.Initialize(scheduler, &routes)) || FAILED(queue.Bind(device.get(), context.get(), true)))
            return false;
        auto pool = Reserve(kGpuAllowance);
        return queue.AdmitPersistentResidency(pool);
    }
    GpuResidencyReservation Reserve(std::uint64_t bytes, std::uint64_t generation = 0) noexcept
    {
        GpuResidencyAdmission status{};
        return budget.ReserveOwner(
            bytes, {generation ? generation : queue.DeviceGeneration(), 8ull * 1024 * 1024 * 1024, 0}, status);
    }
    void Close() noexcept
    {
        if (!initialized)
            return;
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        initialized = false;
    }
    ~Fixture()
    {
        Close();
    }
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct Completion final
{
    GpuRasterCreatedResidency residency;
    JobResult result;
    JobTicket ticket;
    std::uint64_t chunks = 0;
    std::uint64_t cpuAtCallback = 0;
    std::uint64_t gpuAtCallback = 0;
    unsigned int calls = 0;
    bool duplicateRefused = false;
    bool oldBorrowCleared = false;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class Probe final : public GpuRasterUploadJob
{
  public:
    Probe(Fixture& fixture, std::unique_ptr<GpuRasterUploadInput> input, GpuResidencyReservation reservation,
          Completion& completion, ID3D11Device* device) noexcept
        : GpuRasterUploadJob(fixture.scheduler, fixture.queue, std::move(input), {device, std::move(reservation)}),
          _fixture(fixture), _completion(completion)
    {
    }
    [[nodiscard]] bool RefusesEarlyAdoption() noexcept
    {
        return !TakeCreatedResidencyOwner();
    }
    void HoldRow(RasterUploadRowObserver observer, void* context) noexcept
    {
        SetRowObserverForTest(observer, context);
    }
    void HoldPublication(PublicationObserver observer, void* context) noexcept
    {
        SetPublicationObserverForTest(observer, context);
    }

  private:
    void RasterUploadCompleted(JobResult result) noexcept override
    {
        _completion.result = result;
        _completion.ticket = UploadTicket();
        _completion.chunks = UploadedChunks();
        ++_completion.calls;
        _completion.cpuAtCallback = _fixture.scheduler.ReservedMemoryBytes();
        _completion.gpuAtCallback = _fixture.budget.ReservedOwner();
        _completion.residency = TakeCreatedResidencyOwner();
        _completion.duplicateRefused = !TakeCreatedResidencyOwner();
        _completion.oldBorrowCleared = !Destination() && !DestinationShaderView();
    }
    Fixture& _fixture;
    Completion& _completion;
};
#pragma warning(pop)

std::unique_ptr<GpuRasterUploadInput> Input(const Raster& source, int scale, bool mips)
{
    auto layout = RasterUploadLayout::Make(source, 0, 0, source.Width(), source.Height(), scale, 4096,
                                           static_cast<std::uint64_t>(1 + (source.Width() - 1) / scale) *
                                               static_cast<unsigned int>(source.Channels()),
                                           4096, mips);
    return GpuRasterUploadInput::Make(std::move(layout));
}

bool Submit(Fixture& fixture, std::unique_ptr<Job>& job, JobTicket& ticket) noexcept
{
    return fixture.scheduler.Submit(job, {JobClass::Commit, 0x435245415445ull, kCpuReservation, true}, ticket) == S_OK;
}

bool BaseEquals(Fixture& fixture, ID3D11Texture2D* source, std::span<const std::uint8_t> expected,
                unsigned int channels) noexcept
{
    if (!source)
        return false;
    D3D11_TEXTURE2D_DESC description{};
    source->GetDesc(&description);
    const auto row = static_cast<std::size_t>(description.Width) * channels;
    if (expected.size() != row * description.Height)
        return false;
    description.MipLevels = 1;
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.MiscFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    if (FAILED(fixture.device->CreateTexture2D(&description, nullptr, staging.put())))
        return false;
    fixture.context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, source, 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(fixture.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const auto unmap = wil::scope_exit([&]() noexcept { fixture.context->Unmap(staging.get(), 0); });
    if (!mapped.pData || mapped.RowPitch < row)
        return false;
    for (unsigned int y = 0; y < description.Height; ++y)
        if (std::memcmp(static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                        expected.data() + static_cast<std::size_t>(y) * row, row) != 0)
            return false;
    return true;
}

void Success(const char* name, const Raster& source, int scale, std::span<const std::uint8_t> expected,
             std::uint64_t logicalBytes, std::uint64_t chunks)
{
    Fixture fixture;
    Completion completed;
    const auto close = wil::scope_exit(
        [&]() noexcept
        {
            completed.residency.ResetOwner();
            fixture.Close();
        });
    const bool ready = fixture.Initialize();
    Expect(ready, name, "real-warp-fixture");
    if (!ready)
        return;
    const auto gpu = kGpuAllowance + logicalBytes;
    auto input = Input(source, scale, true);
    auto reservation = fixture.Reserve(gpu);
    Expect(input && reservation, name, "whole-gpu-reservation-and-cpu-input");
    if (!input || !reservation)
        return;
    auto probe =
        std::make_unique<Probe>(fixture, std::move(input), std::move(reservation), completed, fixture.device.get());
    Expect(probe->RefusesEarlyAdoption(), name, "no-adoption-before-final-callback");
    std::unique_ptr<Job> job = std::move(probe);
    JobTicket ticket;
    Expect(Submit(fixture, job, ticket), name, "one-original-ticket-submitted");
    const bool settled = SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, name, "event-driven-job-settled");
    if (!settled)
        return;
    Expect(completed.calls == 1 && completed.result.status == JobStatus::Finished && completed.ticket == ticket &&
               completed.chunks == chunks,
           name, "one-callback-same-ticket-all-base-chunks");
    Expect(completed.cpuAtCallback == kCpuReservation && completed.gpuAtCallback == kGpuAllowance + gpu &&
               fixture.scheduler.ReservedMemoryBytes() == 0,
           name, "cpu-and-gpu-charges-survive-final-callback");
    Expect(completed.residency && completed.residency.Source() == source &&
               completed.residency.ReservedBytes() == gpu &&
               completed.residency.DeviceGeneration() == fixture.queue.DeviceGeneration(),
           name, "adoption-retains-raster-and-gpu-authority");
    Expect(completed.duplicateRefused && completed.oldBorrowCleared, name,
           "duplicate-adoption-refused-original-graphics-cleared");
    Expect(fixture.budget.ReservedOwner() == kGpuAllowance + gpu && fixture.budget.ActiveOwner() == 2 &&
               fixture.scheduler.IsIdle(),
           name, "adopted-gpu-reservation-outlives-producing-job");
    Expect(
        BaseEquals(fixture, completed.residency.TextureOwner(), expected, static_cast<unsigned int>(source.Channels())),
        name, "literal-base-pixels-after-worker-creation-and-upload");
    auto moved = std::move(completed.residency);
    Expect(moved && !completed.residency && fixture.budget.ReservedOwner() == kGpuAllowance + gpu, name,
           "moving-adopted-owner-keeps-one-charge");
    moved.ResetOwner();
    Expect(fixture.budget.ReservedOwner() == kGpuAllowance && fixture.budget.ActiveOwner() == 1 &&
               fixture.queue.StagingBytes() != 0 && fixture.queue.HasAdmittedPersistentResidency(),
           name, "destination-retires-reusable-staging-remains-charged");
}

void Refusals(bool rebind, bool insufficientReservation)
{
    const char* name = rebind ? "rebind" : insufficientReservation ? "insufficient-gpu" : "foreign-generation";
    Fixture fixture;
    Completion completed;
    const auto close = wil::scope_exit([&]() noexcept { fixture.Close(); });
    const bool ready = fixture.Initialize();
    Expect(ready, name, "real-warp-fixture");
    if (!ready)
        return;
    const Raster source = Raster::Make(2, 2, RasterFormat::Bgra8888);
    const auto generation = fixture.queue.DeviceGeneration() + (!rebind && !insufficientReservation ? 1 : 0);
    auto reservation = fixture.Reserve(insufficientReservation ? 20 : kGpuAllowance + 20, generation);
    std::unique_ptr<Job> job = std::make_unique<Probe>(fixture, Input(source, 1, true), std::move(reservation),
                                                       completed, fixture.device.get());
    if (rebind)
        Expect(SUCCEEDED(fixture.queue.Bind(fixture.device.get(), fixture.context.get(), true)), name,
               "device-epoch-changes-before-worker-completion");
    JobTicket ticket;
    const bool submitted = Submit(fixture, job, ticket);
    Expect(submitted && SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled, name, "refused-graph-settles");
    Expect(completed.calls == 1 && completed.result.status == JobStatus::Failed &&
               static_cast<HRESULT>(completed.result.error) == (insufficientReservation ? E_OUTOFMEMORY : E_ABORT) &&
               !completed.residency,
           name, "original-error-no-resource-publication");
    Expect(fixture.queue.MapAttemptsForTest() == 0 && fixture.budget.ReservedOwner() == kGpuAllowance &&
               fixture.scheduler.ReservedMemoryBytes() == 0,
           name, "no-map-no-leaked-reservation");
}

void Quarantine()
{
    constexpr auto gpu = kGpuAllowance + 20;
    Fixture fixture;
    Completion completed;
    const auto close = wil::scope_exit([&]() noexcept { fixture.Close(); });
    const bool ready = fixture.Initialize();
    Expect(ready, "quarantine", "real-warp-fixture");
    if (!ready)
        return;
    auto source = Raster::Make(2, 2, RasterFormat::Bgra8888);
    std::unique_ptr<Job> job =
        std::make_unique<Probe>(fixture, Input(source, 1, true), fixture.Reserve(gpu), completed, fixture.device.get());
    fixture.queue.SetFailureForTest(GpuUploadFailurePoint::CopySignal, E_ACCESSDENIED);
    JobTicket ticket;
    Expect(Submit(fixture, job, ticket) && SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled, "quarantine",
           "failed-copy-signal-graph-settles");
    Expect(completed.calls == 1 && completed.result.status == JobStatus::Failed &&
               completed.result.error == E_ACCESSDENIED && !completed.residency,
           "quarantine", "failed-graph-never-adopts-partial-mips");
    Expect(fixture.queue.FailureHitsForTest() == 1 && fixture.queue.StagingBytes() != 0 &&
               fixture.queue.RetiredResidencyBytes() == gpu && fixture.budget.ReservedOwner() == kGpuAllowance + gpu,
           "quarantine", "uncertain-copy-keeps-entire-gpu-charge-after-job-retirement");
    auto unrelated = fixture.Reserve(41);
    Expect(!fixture.queue.RetainRetiredResidency({ticket.slot, ticket.generation + 1}, unrelated) &&
               unrelated.Bytes() == 41,
           "quarantine", "foreign-ticket-cannot-transfer-a-retirement-charge");
    unrelated.ResetOwner();
    fixture.Close();
    Expect(fixture.budget.ReservedOwner() == 0 && fixture.budget.ActiveOwner() == 0, "quarantine",
           "joined-owner-storage-release-ends-retirement-charge");
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct HeldProducer final
{
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool shutdownObserved = false;
    std::atomic_bool* pause = nullptr;
    static void Row(void* context, std::size_t) noexcept
    {
        auto& self = *static_cast<HeldProducer*>(context);
        self.pause->store(true, std::memory_order_release); // before row-lease publication can make the queue ready
    }
    static void Published(void* context, JobContext& job) noexcept
    {
        auto& self = *static_cast<HeldProducer*>(context);
        (void)SetEvent(self.entered.get());
        const HANDLE events[]{self.release.get(), static_cast<HANDLE>(job.shutdownEvent)};
        self.shutdownObserved = WaitForMultipleObjects(2, events, FALSE, 10000) == WAIT_OBJECT_0 + 1;
    }
};
#pragma warning(pop)

bool AwaitPublished(Fixture& fixture, HANDLE entered) noexcept
{
    const auto begin = GetTickCount64();
    for (;;)
    {
        (void)DrainCompletions(fixture.scheduler);
        if (WaitForSingleObject(entered, 0) == WAIT_OBJECT_0)
            return true;
        const auto elapsed = GetTickCount64() - begin;
        if (elapsed >= 10000)
            return false;
        if (fixture.scheduler.HasUiWork())
            continue;
        const HANDLE events[]{entered, static_cast<HANDLE>(fixture.scheduler.WakeHandle())};
        const auto wait = WaitForMultipleObjects(2, events, FALSE, static_cast<DWORD>(10000 - elapsed));
        if (wait != WAIT_OBJECT_0 && wait != WAIT_OBJECT_0 + 1)
            return false;
    }
}

void Shutdown()
{
    constexpr auto gpu = kGpuAllowance + 20;
    Fixture fixture;
    HeldProducer producer;
    Completion completed;
    const auto close = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(producer.release.get());
            fixture.Close();
        });
    const bool ready = fixture.Initialize();
    Expect(ready && producer.entered && producer.release, "shutdown", "real-fixture-and-held-producer-gates");
    if (!ready || !producer.entered || !producer.release)
        return;
    auto source = Raster::Make(2, 2, RasterFormat::Bgra8888);
    auto probe =
        std::make_unique<Probe>(fixture, Input(source, 1, true), fixture.Reserve(gpu), completed, fixture.device.get());
    producer.pause = &fixture.pauseOwner;
    probe->HoldRow(HeldProducer::Row, &producer);
    probe->HoldPublication(HeldProducer::Published, &producer);
    std::unique_ptr<Job> job = std::move(probe);
    JobTicket ticket;
    const bool submitted = Submit(fixture, job, ticket);
    const bool held = submitted && AwaitPublished(fixture, producer.entered.get());
    Expect(held, "shutdown", "created-destination-and-mapped-producer-held");
    if (!held)
        return;
    Expect(fixture.budget.ReservedOwner() == kGpuAllowance + gpu &&
               fixture.scheduler.ReservedMemoryBytes() == kCpuReservation && fixture.queue.LentCount() == 1,
           "shutdown", "whole-reservations-live-through-held-creator-upload");
    fixture.queue.StopNotifications();
    fixture.scheduler.Close();
    Expect(producer.shutdownObserved && completed.calls == 0 && fixture.queue.RetiredResidencyBytes() == gpu &&
               fixture.budget.ReservedOwner() == kGpuAllowance + gpu,
           "shutdown", "joined-producer-leaves-gpu-charge-with-unacknowledged-queue");
    fixture.queue.CloseAfterWorkers();
    fixture.initialized = false;
    Expect(fixture.budget.ReservedOwner() == 0 && fixture.budget.ActiveOwner() == 0, "shutdown",
           "final-owner-unmap-and-release-drain-both-graphs");
}
} // namespace

int RunGpuCreatedUploadJobTests()
{
    gFailures = 0;
    constexpr std::array<std::uint8_t, 24> bgra{1,  2,  3,  255, 4,  5,  6,  255, 7,  8,  9,  255,
                                                10, 11, 12, 255, 13, 14, 15, 255, 16, 17, 18, 255};
    const Raster image = Raster::Make(3, 2, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    Success("bgra", image, 1, bgra, 28, 2);
    constexpr std::array<std::uint8_t, 35> gray{0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
                                                18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34};
    constexpr std::array<std::uint8_t, 12> reduced{4, 6, 8, 10, 18, 20, 22, 24, 29, 31, 33, 34};
    const Raster mask = Raster::Make(7, 5, RasterFormat::Gray8, gray.data(), gray.size());
    Success("gray-proxy", mask, 2, reduced, 15, 3);
    Refusals(false, true);
    Refusals(false, false);
    Refusals(true, false);
    Quarantine();
    Shutdown();
    return gFailures;
}
