#include "GpuRasterUploadJobTests.h"

#include "../../Common/Document.h"
#include "../../Common/Jobs.h"
#include "../../Common/OwnerWorkMultiplexer.h"
#include "../../Common/RasterUploadLayout.h"
#include "../../RedPrism/GpuRasterUploadJob.h"
#include "../../RedPrism/GpuUploadWork.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s gpu-raster-upload-job.%s\n", condition ? "PASS" : "FAIL", label);
    gFailures += condition ? 0 : 1;
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

struct Device final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;

    HRESULT Create() noexcept
    {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    }
};

struct Fixture final
{
    JobScheduler scheduler;
    GpuUploadQueue queue;
    bool initialized = false;

    bool Initialize() noexcept
    {
        if (scheduler.Initialize(1, {}, {StableMemory, nullptr}) != S_OK)
            return false;
        initialized = true;
        if (!scheduler.EnableDeferredRelease() || FAILED(queue.Initialize(scheduler)))
        {
            Close();
            return false;
        }
        return true;
    }

    bool Bind(Device& device) noexcept
    {
        return SUCCEEDED(queue.Bind(device.device.get(), device.context.get(), true)) &&
               queue.CompletionRoute() == GpuCapabilities::CompletionRoute::EnqueueSetEvent;
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

struct GatedOwnerPump final : OwnerWorkPump
{
    GatedOwnerPump(JobScheduler& ownerScheduler, OwnerWorkMultiplexer& ownerRoutes) noexcept
        : scheduler(ownerScheduler), routes(ownerRoutes)
    {
    }

    [[nodiscard]] bool HasPending() const noexcept override
    {
        return routes.HasPending();
    }

    [[nodiscard]] bool HasReady() const noexcept override
    {
        return !paused && routes.HasReady();
    }

    void Pump() noexcept override
    {
        if (!paused)
            routes.Pump();
    }

    void Pause() noexcept
    {
        paused = true;
        scheduler.NotifyOwnerWork();
    }

    void Resume() noexcept
    {
        paused = false;
        scheduler.NotifyOwnerWork();
    }

    JobScheduler& scheduler;
    OwnerWorkMultiplexer& routes;
    bool paused = false;
};

struct GatedFixture final
{
    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    GatedOwnerPump owner{scheduler, routes};
    GpuUploadQueue queue;
    bool initialized = false;

    bool Initialize() noexcept
    {
        if (scheduler.Initialize(1, {}, {StableMemory, nullptr}) != S_OK)
            return false;
        initialized = true;
        if (!scheduler.EnableDeferredRelease() || !scheduler.RegisterOwnerWork(owner) ||
            FAILED(queue.Initialize(scheduler, &routes)))
        {
            Close();
            return false;
        }
        return true;
    }

    bool Bind(Device& device) noexcept
    {
        return SUCCEEDED(queue.Bind(device.device.get(), device.context.get(), true)) &&
               queue.CompletionRoute() == GpuCapabilities::CompletionRoute::EnqueueSetEvent;
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

    ~GatedFixture()
    {
        Close();
    }
};

wil::com_ptr_nothrow<ID3D11Texture2D> MakeTarget(Device& device, unsigned int width, unsigned int height,
                                                 DXGI_FORMAT format, std::uint8_t guard) noexcept
{
    const std::size_t channels = format == DXGI_FORMAT_R8_UNORM ? 1U : 4U;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * channels, guard);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    const D3D11_SUBRESOURCE_DATA initial{pixels.data(), static_cast<UINT>(width * channels), 0};
    wil::com_ptr_nothrow<ID3D11Texture2D> target;
    if (FAILED(device.device->CreateTexture2D(&desc, &initial, target.put())))
        target.reset();
    return target;
}

bool TargetEquals(Device& device, ID3D11Texture2D& target, const std::vector<std::uint8_t>& expected,
                  std::size_t channels) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    target.GetDesc(&desc);
    const std::size_t rowBytes = static_cast<std::size_t>(desc.Width) * channels;
    if (expected.size() != rowBytes * desc.Height)
        return false;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    if (FAILED(device.device->CreateTexture2D(&desc, nullptr, staging.put())))
        return false;
    device.context->CopyResource(staging.get(), &target);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(device.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const auto unmap = wil::scope_exit([&]() noexcept { device.context->Unmap(staging.get(), 0); });
    if (!mapped.pData || mapped.RowPitch < rowBytes)
        return false;
    for (unsigned int y = 0; y < desc.Height; ++y)
    {
        const auto* actual =
            static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        if (std::memcmp(actual, expected.data() + static_cast<std::size_t>(y) * rowBytes, rowBytes) != 0)
            return false;
    }
    return true;
}

template <typename Predicate> bool Until(JobScheduler& scheduler, Predicate predicate) noexcept
{
    const std::uint64_t start = GetTickCount64();
    for (;;)
    {
        (void)DrainCompletions(scheduler);
        if (predicate())
            return true;
        const std::uint64_t elapsed = GetTickCount64() - start;
        if (elapsed >= 10000 || !scheduler.WakeHandle())
            return false;
        if (scheduler.HasUiWork())
            continue;
        const DWORD result =
            WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), static_cast<DWORD>(10000 - elapsed));
        if (result != WAIT_OBJECT_0)
            return false;
    }
}

bool WaitForEvent(JobScheduler& scheduler, HANDLE event) noexcept
{
    const std::uint64_t start = GetTickCount64();
    while (WaitForSingleObject(event, 0) != WAIT_OBJECT_0)
    {
        (void)DrainCompletions(scheduler);
        if (WaitForSingleObject(event, 0) == WAIT_OBJECT_0)
            return true;
        const std::uint64_t elapsed = GetTickCount64() - start;
        if (elapsed >= 10000 || !scheduler.WakeHandle())
            return false;
        if (scheduler.HasUiWork())
            continue;
        const std::array<HANDLE, 2> events{event, static_cast<HANDLE>(scheduler.WakeHandle())};
        const DWORD waited = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE,
                                                    static_cast<DWORD>(10000 - elapsed));
        if (waited != WAIT_OBJECT_0 && waited != WAIT_OBJECT_0 + 1)
            return false;
    }
    return true;
}

struct Completion final
{
    unsigned int calls = 0;
    JobResult result{};
    JobTicket ticket{};
    std::uint64_t chunks = 0;
    std::uint64_t reservedAtCallback = 0;
    JobScheduler* scheduler = nullptr;
};

class ProbeJob final : public GpuRasterUploadJob
{
  public:
    ProbeJob(JobScheduler& scheduler, GpuUploadQueue& queue, std::unique_ptr<GpuRasterUploadInput> input,
             ID3D11Texture2D* destination, Completion& completion, unsigned int x = 0, unsigned int y = 0) noexcept
        : GpuRasterUploadJob(scheduler, queue, std::move(input), destination, 0, x, y), _completion(completion)
    {
        _completion.scheduler = &scheduler;
    }

    void ObserveRows(RasterUploadRowObserver observer, void* context) noexcept
    {
        SetRowObserverForTest(observer, context);
    }

    void ObservePublication(PublicationObserver observer, void* context) noexcept
    {
        SetPublicationObserverForTest(observer, context);
    }

  private:
    void RasterUploadCompleted(JobResult result) noexcept override
    {
        ++_completion.calls;
        _completion.result = result;
        _completion.ticket = UploadTicket();
        _completion.chunks = UploadedChunks();
        _completion.reservedAtCallback = _completion.scheduler->ReservedMemoryBytes();
    }
    Completion& _completion;
};

struct Observer final
{
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<unsigned int> observedRows{0};
    unsigned int holdRow = 0;
    bool wokeForShutdown = false;

    static void Row(void* context, std::size_t row) noexcept
    {
        auto& self = *static_cast<Observer*>(context);
        self.observedRows.store(static_cast<unsigned int>(row), std::memory_order_release);
        if (row == self.holdRow)
        {
            (void)SetEvent(self.entered.get());
            const DWORD result = WaitForSingleObject(self.release.get(), 10000);
            if (result != WAIT_OBJECT_0)
                self.observedRows.store(0xffffffffU, std::memory_order_release);
        }
    }

    static void Publication(void* context, JobContext& jobContext) noexcept
    {
        auto& self = *static_cast<Observer*>(context);
        (void)SetEvent(self.entered.get());
        HANDLE events[]{self.release.get(), static_cast<HANDLE>(jobContext.shutdownEvent)};
        const DWORD result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        self.wokeForShutdown = result == WAIT_OBJECT_0 + 1;
    }
};

RasterUploadLayout Layout(const Raster& source, int x, int y, int width, int height, int scale,
                          std::uint64_t chunkBytes) noexcept
{
    return RasterUploadLayout::Make(source, x, y, width, height, scale, 4096, chunkBytes, GpuUploadQueue::kMaxRowBytes,
                                    false);
}

std::unique_ptr<GpuRasterUploadInput> Input(RasterUploadLayout layout)
{
    return GpuRasterUploadInput::Make(std::move(layout));
}

template <typename FixtureType>
bool Submit(FixtureType& fixture, std::unique_ptr<Job>& job, std::uint64_t reservation, bool deferred,
            JobTicket& ticket) noexcept
{
    return fixture.scheduler.Submit(job, {JobClass::Commit, 0x47505531ULL, reservation, deferred}, ticket) == S_OK;
}

void RunBgraCropChunks()
{
    constexpr std::array<std::uint8_t, 96> sourcePixels{
        1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
        49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72,
        73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96};
    constexpr std::uint8_t guard = 211;
    constexpr std::uint64_t reservation = 1ULL << 20;
    Device device;
    Fixture fixture;
    if (FAILED(device.Create()) || !device.device || !fixture.Initialize() || !fixture.Bind(device))
    {
        Expect(false, "bgra.fixture");
        return;
    }
    auto source = Raster::Make(6, 4, RasterFormat::Bgra8888, sourcePixels.data(), sourcePixels.size());
    auto layout = Layout(source, 1, 1, 4, 3, 1, 16); // One row per tiny serial base chunk.
    auto input = Input(std::move(layout));
    auto target = MakeTarget(device, 8, 6, DXGI_FORMAT_B8G8R8A8_UNORM, guard);
    Expect(input && input->Layout().BaseChunkCount() == 3 && target != nullptr,
           "bgra.owned-input-three-chunks-and-precreated-target");
    if (!input || !target)
        return;
    Completion completion;
    const auto closeFixture = wil::scope_exit([&]() noexcept { fixture.Close(); });
    std::unique_ptr<Job> job =
        std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue, std::move(input), target.get(), completion, 2, 1);
    JobTicket ticket{};
    const bool submitted = Submit(fixture, job, reservation, true, ticket);
    Expect(submitted, "bgra.deferred-job-submitted");
    if (!submitted)
        return;
    const bool settled = SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, "bgra.settled");
    if (!settled)
        return;
    std::vector<std::uint8_t> expected(8U * 6U * 4U, guard);
    for (std::size_t y = 0; y < 3; ++y)
        for (std::size_t x = 0; x < 4; ++x)
            for (std::size_t channel = 0; channel < 4; ++channel)
                expected[((y + 1) * 8 + (x + 2)) * 4 + channel] = sourcePixels[((y + 1) * 6 + (x + 1)) * 4 + channel];
    Expect(TargetEquals(device, *target.get(), expected, 4), "bgra.crop-offset-pixels-and-guard-literals");
    Expect(completion.calls == 1 && completion.ticket == ticket && completion.result.status == JobStatus::Finished &&
               completion.chunks == 3 && completion.reservedAtCallback == reservation &&
               fixture.scheduler.ReservedMemoryBytes() == 0,
           "bgra.same-ticket-three-chunks-one-final-release");
}

void RunGrayScale2OddCrop()
{
    constexpr std::array<std::uint8_t, 35> sourcePixels{0,  1,  2,  3,  4,  5,  6,  10, 11, 12, 13, 14,
                                                        15, 16, 20, 21, 22, 23, 24, 25, 26, 30, 31, 32,
                                                        33, 34, 35, 36, 40, 41, 42, 43, 44, 45, 46};
    constexpr std::array<std::uint8_t, 6> reduced{17, 19, 20, 32, 34, 35};
    constexpr std::uint8_t guard = 207;
    Device device;
    Fixture fixture;
    if (FAILED(device.Create()) || !device.device || !fixture.Initialize() || !fixture.Bind(device))
    {
        Expect(false, "gray.fixture");
        return;
    }
    auto source = Raster::Make(7, 5, RasterFormat::Gray8, sourcePixels.data(), sourcePixels.size());
    auto layout = Layout(source, 1, 1, 5, 3, 2, 3); // Odd crop: 3x2 output, a partial block at both edges.
    auto input = Input(std::move(layout));
    auto target = MakeTarget(device, 6, 5, DXGI_FORMAT_R8_UNORM, guard);
    Expect(input && input->Layout().Width() == 3 && input->Layout().Height() == 2 &&
               input->Layout().BaseChunkCount() == 2 && target != nullptr,
           "gray.odd-crop-rounds-up-splits-and-precreates-target");
    if (!input || !target)
        return;
    Completion completion;
    const auto closeFixture = wil::scope_exit([&]() noexcept { fixture.Close(); });
    std::unique_ptr<Job> job =
        std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue, std::move(input), target.get(), completion, 2, 1);
    JobTicket ticket{};
    const bool submitted = Submit(fixture, job, 1ULL << 20, true, ticket);
    Expect(submitted, "gray.deferred-job-submitted");
    if (!submitted)
        return;
    const bool settled = SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, "gray.settled");
    if (!settled)
        return;
    std::vector<std::uint8_t> expected(6U * 5U, guard);
    for (std::size_t y = 0; y < 2; ++y)
        for (std::size_t x = 0; x < 3; ++x)
            expected[(y + 1) * 6 + (x + 2)] = reduced[y * 3 + x];
    Expect(TargetEquals(device, *target.get(), expected, 1), "gray.serial-rounded-literals-and-guard");
    Expect(completion.calls == 1 && completion.ticket == ticket && completion.result.status == JobStatus::Finished &&
               completion.chunks == 2 && completion.reservedAtCallback == (1ULL << 20) &&
               fixture.scheduler.ReservedMemoryBytes() == 0,
           "gray.same-ticket-two-chunks-one-final-release");
}

void RunEarlyRefusals()
{
    constexpr std::array<std::uint8_t, 4> grayPixels{7, 17, 29, 41};
    Device device;
    Fixture fixture;
    if (FAILED(device.Create()) || !device.device || !fixture.Initialize() || !fixture.Bind(device))
    {
        Expect(false, "refusal.fixture");
        return;
    }
    auto source = Raster::Make(2, 2, RasterFormat::Gray8, grayPixels.data(), grayPixels.size());
    auto grayInput = Input(Layout(source, 0, 0, 2, 2, 1, 8));
    auto bgraTarget = MakeTarget(device, 2, 2, DXGI_FORMAT_B8G8R8A8_UNORM, 101);
    Expect(grayInput && bgraTarget, "refusal.fixtures-created");
    if (!grayInput || !bgraTarget)
        return;

    Completion wrongFormat;
    std::unique_ptr<Job> wrongJob = std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue, std::move(grayInput),
                                                               bgraTarget.get(), wrongFormat);
    JobTicket wrongTicket{};
    const unsigned int beforeWrong = fixture.queue.MapAttemptsForTest();
    const bool wrongSubmitted = Submit(fixture, wrongJob, 1ULL << 20, true, wrongTicket);
    const bool wrongSettled = wrongSubmitted && SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(wrongSettled, "refusal.wrong-format-settled");
    if (!wrongSettled)
    {
        fixture.Close();
        return;
    }
    Expect(wrongFormat.calls == 1 && wrongFormat.result.status == JobStatus::Failed &&
               wrongFormat.result.error == E_INVALIDARG && fixture.queue.MapAttemptsForTest() == beforeWrong,
           "refusal.gray-to-bgra-fails-before-map");

    auto bgraSource = Raster::Make(2, 2, RasterFormat::Bgra8888);
    auto plainInput = Input(Layout(bgraSource, 0, 0, 2, 2, 1, 16));
    Completion missingFlag;
    std::unique_ptr<Job> missingJob = std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue,
                                                                 std::move(plainInput), bgraTarget.get(), missingFlag);
    JobTicket missingTicket{};
    const unsigned int beforeMissing = fixture.queue.MapAttemptsForTest();
    const bool missingSubmitted = Submit(fixture, missingJob, 1ULL << 20, false, missingTicket);
    const bool missingSettled = missingSubmitted && SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(missingSettled, "refusal.missing-flag-settled");
    if (!missingSettled)
    {
        fixture.Close();
        return;
    }
    Expect(missingFlag.calls == 1 && missingFlag.result.status == JobStatus::Failed &&
               missingFlag.result.error == E_UNEXPECTED && fixture.queue.MapAttemptsForTest() == beforeMissing,
           "refusal.missing-deferred-flag-before-queue-escape");

    auto reducedInput = Input(Layout(source, 0, 0, 2, 2, 2, 1));
    const auto scratch = reducedInput ? reducedInput->WorkerAllocationBytes() : 0;
    Completion insufficient;
    std::unique_ptr<Job> insufficientJob = std::make_unique<ProbeJob>(
        fixture.scheduler, fixture.queue, std::move(reducedInput), bgraTarget.get(), insufficient);
    JobTicket insufficientTicket{};
    const unsigned int beforeInsufficient = fixture.queue.MapAttemptsForTest();
    const std::uint64_t shortReservation = scratch == 0 ? 0 : scratch - 1;
    const bool insufficientSubmitted =
        scratch != 0 && Submit(fixture, insufficientJob, shortReservation, true, insufficientTicket);
    const bool insufficientSettled =
        insufficientSubmitted && SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(scratch != 0 && insufficientSettled, "refusal.insufficient-scratch-submitted-and-settled");
    if (!insufficientSettled)
    {
        fixture.Close();
        return;
    }
    Expect(insufficient.calls == 1 && insufficient.result.status == JobStatus::Failed &&
               insufficient.result.error == E_OUTOFMEMORY && fixture.queue.MapAttemptsForTest() == beforeInsufficient &&
               fixture.scheduler.ReservedMemoryBytes() == 0,
           "refusal.scratch-reservation-fails-before-map-and-releases");
}

struct Blocker final
{
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
};

struct Marker final
{
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
};

class MarkerJob final : public Job
{
  public:
    explicit MarkerJob(Marker& marker) noexcept : _marker(marker) {}
    JobResult Run(JobContext&) override
    {
        (void)SetEvent(_marker.entered.get());
        return {};
    }
    void Completed(JobResult) noexcept override {}

  private:
    Marker& _marker;
};

class BlockerJob final : public Job
{
  public:
    explicit BlockerJob(Blocker& blocker) noexcept : _blocker(blocker) {}
    JobResult Run(JobContext&) override
    {
        (void)SetEvent(_blocker.entered.get());
        return WaitForSingleObject(_blocker.release.get(), 10000) == WAIT_OBJECT_0
                   ? JobResult{}
                   : JobResult{JobStatus::Failed, E_UNEXPECTED};
    }
    void Completed(JobResult) noexcept override {}

  private:
    Blocker& _blocker;
};

void RunProducerSkippedCancellation()
{
    constexpr std::array<std::uint8_t, 16> pixels{7, 11, 13, 255, 17, 19, 23, 255, 29, 31, 37, 255, 41, 43, 47, 255};
    constexpr std::uint8_t guard = 53;
    Device device;
    Fixture fixture;
    Blocker blocker;
    if (FAILED(device.Create()) || !device.device || !fixture.Initialize() || !fixture.Bind(device) ||
        !blocker.entered || !blocker.release)
    {
        Expect(false, "skipped.fixture");
        return;
    }
    auto source = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    auto input = Input(Layout(source, 0, 0, 2, 2, 1, 16));
    auto target = MakeTarget(device, 2, 2, DXGI_FORMAT_B8G8R8A8_UNORM, guard);
    Expect(input && target, "skipped.fixtures-created");
    if (!input || !target)
        return;
    Completion completion;
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(blocker.release.get());
            fixture.Close();
        });
    std::unique_ptr<Job> upload =
        std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue, std::move(input), target.get(), completion);
    JobTicket ticket{};
    constexpr std::uint64_t reservation = 1ULL << 20;
    const bool submitted = Submit(fixture, upload, reservation, true, ticket);
    Expect(submitted, "skipped.deferred-upload-queued");
    if (!submitted)
        return;
    const bool suspendedPending = Until(fixture.scheduler, [&]() noexcept
                                        { return fixture.queue.HasPending() && fixture.queue.LentCount() == 0; });
    Expect(suspendedPending && fixture.queue.HasPending() && fixture.queue.LentCount() == 0,
           "skipped.prepared-suspended-request-before-map");
    if (!suspendedPending)
        return;
    Expect(fixture.scheduler.ReservedMemoryBytes() == reservation && completion.calls == 0,
           "skipped.preparation-retains-original-job-reservation");

    std::unique_ptr<Job> blockerJob = std::make_unique<BlockerJob>(blocker);
    JobTicket blockerTicket{};
    const bool blockerSubmitted =
        fixture.scheduler.Submit(blockerJob, {JobClass::Background, 0, 0, false}, blockerTicket) == S_OK;
    Expect(blockerSubmitted, "skipped.blocker-submitted");
    if (!blockerSubmitted)
        return;
    const bool blockerRunning = WaitForSingleObject(blocker.entered.get(), 10000) == WAIT_OBJECT_0;
    Expect(blockerRunning, "skipped.worker-held-before-resumed-upload");
    if (!blockerRunning)
        return;

    fixture.queue.Pump(); // Deliver one mapped loan; the unrelated blocker prevents the resumed Run stage.
    (void)DrainCompletions(fixture.scheduler); // Promote the exact suspended ticket without pumping a second map.
    Expect(fixture.queue.LentCount() == 1 && fixture.queue.HasPending() &&
               fixture.scheduler.ReservedMemoryBytes() == reservation && completion.calls == 0,
           "skipped.mapped-loan-held-while-run-cannot-enter");
    Expect(fixture.scheduler.Cancel(ticket), "skipped.queued-ticket-cancelled");
    fixture.queue.Cancel(ticket);
    Expect(fixture.queue.LentCount() == 1 && fixture.scheduler.ReservedMemoryBytes() == reservation,
           "skipped.cancel-retains-loan-and-reservation-until-producer-ack");
    (void)SetEvent(blocker.release.get());
    const bool settled = SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, "skipped.settled");
    if (!settled)
        return;
    const std::vector<std::uint8_t> expected(2U * 2U * 4U, guard);
    Expect(completion.calls == 1 && completion.ticket == ticket && completion.result.status == JobStatus::Cancelled &&
               completion.reservedAtCallback == reservation && fixture.queue.MapAttemptsForTest() == 1 &&
               TargetEquals(device, *target.get(), expected, 4),
           "skipped.producer-skipped-cancel-acknowledges-mapped-loan-without-copy");
    Expect(fixture.scheduler.ReservedMemoryBytes() == 0 && fixture.queue.LentCount() == 0 &&
               fixture.queue.InFlightCount() == 0 && !fixture.queue.HasPending(),
           "skipped.reservation-released-after-final-ack");
}

void RunPendingCancellation()
{
    constexpr std::array<std::uint8_t, 16> pixels{7, 11, 13, 255, 17, 19, 23, 255, 29, 31, 37, 255, 41, 43, 47, 255};
    constexpr std::uint64_t reservation = 1ULL << 20;
    constexpr std::uint8_t guard = 67;
    Device device;
    GatedFixture fixture;
    if (FAILED(device.Create()) || !device.device || !fixture.Initialize() || !fixture.Bind(device))
    {
        Expect(false, "pending-cancel.fixture");
        return;
    }
    auto source = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    auto input = Input(Layout(source, 0, 0, 2, 2, 1, 16));
    auto target = MakeTarget(device, 2, 2, DXGI_FORMAT_B8G8R8A8_UNORM, guard);
    Expect(input && target, "pending-cancel.fixtures-created");
    if (!input || !target)
        return;
    Completion completion;
    const auto closeFixture = wil::scope_exit([&]() noexcept { fixture.Close(); });
    fixture.owner.Pause(); // The registered gate suppresses queue Pump from the first scheduler drain onward.
    std::unique_ptr<Job> job =
        std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue, std::move(input), target.get(), completion);
    JobTicket ticket{};
    const bool submitted = Submit(fixture, job, reservation, true, ticket);
    Expect(submitted, "pending-cancel.submitted");
    if (!submitted)
        return;

    const bool pendingBeforeMap = Until(fixture.scheduler, [&]() noexcept
                                        { return fixture.queue.HasPending() && fixture.queue.LentCount() == 0; });
    Expect(pendingBeforeMap && fixture.queue.HasPending() && fixture.queue.LentCount() == 0,
           "pending-cancel.suspended-before-any-map");
    if (!pendingBeforeMap)
        return;

    const unsigned int mapsBeforeCancel = fixture.queue.MapAttemptsForTest();
    const bool cancelled = fixture.scheduler.Cancel(ticket);
    Expect(cancelled, "pending-cancel.scheduler-cancel-accepted");
    if (!cancelled)
        return;
    const bool resumed = fixture.scheduler.Resume(ticket);
    Expect(resumed, "pending-cancel.external-cancel-ack-resumed");
    if (!resumed)
        return;
    Marker marker;
    Expect(marker.entered != nullptr, "pending-cancel.marker-event-created");
    if (!marker.entered)
        return;
    std::unique_ptr<Job> markerJob = std::make_unique<MarkerJob>(marker);
    JobTicket markerTicket{};
    const bool markerSubmitted =
        fixture.scheduler.Submit(markerJob, {JobClass::Commit, 0x47505532ULL, 0, false}, markerTicket) == S_OK;
    Expect(markerSubmitted, "pending-cancel.terminal-stage-marker-submitted");
    if (!markerSubmitted)
        return;
    const bool markerRan = WaitForEvent(fixture.scheduler, marker.entered.get());
    Expect(markerRan, "pending-cancel.skipped-run-precedes-marker");
    if (!markerRan)
        return;
    // The worker publishes the cancelled stage before taking the later marker. The paused gate prevents this
    // drain from mapping the pending queue slot while the upload's terminal-retirement hook runs.
    (void)DrainCompletions(fixture.scheduler);
    Expect(fixture.queue.HasPending() && fixture.queue.LentCount() == 0 &&
               fixture.queue.MapAttemptsForTest() == mapsBeforeCancel &&
               fixture.scheduler.ReservedMemoryBytes() == reservation && completion.calls == 0 &&
               !fixture.scheduler.IsIdle(),
           "pending-cancel.receiver-and-reservation-survive-terminal-drain-without-map");

    fixture.owner.Resume();
    const bool settled = SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, "pending-cancel.queue-acknowledged-and-settled");
    if (!settled)
        return;
    const std::vector<std::uint8_t> expected(2U * 2U * 4U, guard);
    Expect(completion.calls == 1 && completion.ticket == ticket && completion.result.status == JobStatus::Cancelled &&
               completion.reservedAtCallback == reservation && TargetEquals(device, *target.get(), expected, 4),
           "pending-cancel.one-final-cancelled-callback-and-unchanged-target");
    Expect(fixture.scheduler.ReservedMemoryBytes() == 0 && fixture.queue.LentCount() == 0 &&
               fixture.queue.InFlightCount() == 0 && !fixture.queue.HasPending(),
           "pending-cancel.all-charges-and-receiver-state-cleared");
}

void RunAwaitCopyCancellation()
{
    constexpr std::array<std::uint8_t, 16> pixels{7, 11, 13, 255, 17, 19, 23, 255, 29, 31, 37, 255, 41, 43, 47, 255};
    constexpr std::uint64_t reservation = 1ULL << 20;
    constexpr std::uint8_t guard = 79;
    Device device;
    GatedFixture fixture;
    Observer observer;
    if (FAILED(device.Create()) || !device.device || !fixture.Initialize() || !fixture.Bind(device))
    {
        Expect(false, "await-copy.fixture");
        return;
    }
    auto source = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    auto input = Input(Layout(source, 0, 0, 2, 2, 1, 16));
    auto target = MakeTarget(device, 2, 2, DXGI_FORMAT_B8G8R8A8_UNORM, guard);
    Expect(input && target, "await-copy.fixtures-created");
    if (!input || !target)
        return;
    Completion completion;
    const auto closeFixture = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(observer.release.get());
            fixture.Close();
        });
    fixture.owner.Pause();
    auto concrete =
        std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue, std::move(input), target.get(), completion);
    concrete->ObservePublication(Observer::Publication, &observer);
    std::unique_ptr<Job> job = std::move(concrete);
    JobTicket ticket{};
    const bool submitted = Submit(fixture, job, reservation, true, ticket);
    Expect(submitted, "await-copy.submitted");
    if (!submitted)
        return;

    const bool pendingBeforeMap = Until(fixture.scheduler, [&]() noexcept
                                        { return fixture.queue.HasPending() && fixture.queue.LentCount() == 0; });
    Expect(pendingBeforeMap, "await-copy.pending-before-map-with-gate-paused");
    if (!pendingBeforeMap)
        return;

    fixture.queue.Pump(); // One direct harness pump maps the pending request while the scheduler gate stays paused.
    Expect(fixture.queue.LentCount() == 1 && fixture.queue.InFlightCount() == 1,
           "await-copy.one-mapped-loan-before-worker-fill");
    const bool publicationHeld = WaitForEvent(fixture.scheduler, observer.entered.get());
    Expect(publicationHeld, "await-copy.worker-held-after-publication");
    if (!publicationHeld)
        return;
    const bool filledReady =
        Until(fixture.scheduler, [&]() noexcept { return fixture.queue.HasReady() && fixture.queue.LentCount() == 1; });
    Expect(filledReady, "await-copy.filled-lease-ready-with-owner-gate-paused");
    if (!filledReady)
        return;
    fixture.queue.Pump(); // Exactly one copy Pump; a fast fence remains unconsumed behind the paused owner gate.
    const unsigned int mapsAfterCopy = fixture.queue.MapAttemptsForTest();
    Expect(fixture.queue.InFlightCount() == 1 && fixture.queue.LentCount() == 0 && mapsAfterCopy == 1,
           "await-copy.single-copy-pump-left-fence-in-flight");

    Marker firstMarker;
    Expect(firstMarker.entered != nullptr, "await-copy.first-marker-event-created");
    if (!firstMarker.entered)
        return;
    std::unique_ptr<Job> firstMarkerJob = std::make_unique<MarkerJob>(firstMarker);
    JobTicket firstMarkerTicket{};
    const bool firstMarkerSubmitted =
        fixture.scheduler.Submit(firstMarkerJob, {JobClass::Commit, 0x47505532ULL, 0, false}, firstMarkerTicket) ==
        S_OK;
    Expect(firstMarkerSubmitted, "await-copy.successful-stage-marker-submitted");
    if (!firstMarkerSubmitted)
        return;
    (void)SetEvent(observer.release.get());
    const bool firstMarkerRan = WaitForEvent(fixture.scheduler, firstMarker.entered.get());
    Expect(firstMarkerRan, "await-copy.successful-stage-precedes-marker");
    if (!firstMarkerRan)
        return;
    (void)DrainCompletions(fixture.scheduler); // Applies Finished and retains the record in AwaitCopy.
    Expect(fixture.queue.InFlightCount() == 1 && fixture.scheduler.ReservedMemoryBytes() == reservation &&
               completion.calls == 0,
           "await-copy.finished-stage-retained-before-cancel");

    const bool cancelled = fixture.scheduler.Cancel(ticket);
    Expect(cancelled, "await-copy.scheduler-cancel-accepted");
    if (!cancelled)
        return;
    const bool resumed = fixture.scheduler.Resume(ticket);
    Expect(resumed, "await-copy.external-cancel-ack-resumed");
    if (!resumed)
        return;
    Marker marker;
    Expect(marker.entered != nullptr, "await-copy.cancel-marker-event-created");
    if (!marker.entered)
        return;
    std::unique_ptr<Job> markerJob = std::make_unique<MarkerJob>(marker);
    JobTicket markerTicket{};
    const bool markerSubmitted =
        fixture.scheduler.Submit(markerJob, {JobClass::Commit, 0x47505532ULL, 0, false}, markerTicket) == S_OK;
    Expect(markerSubmitted, "await-copy.cancel-terminal-marker-submitted");
    if (!markerSubmitted)
        return;
    const bool markerRan = WaitForEvent(fixture.scheduler, marker.entered.get());
    Expect(markerRan, "await-copy.cancelled-run-precedes-marker");
    if (!markerRan)
        return;
    (void)DrainCompletions(fixture.scheduler);
    Expect(fixture.owner.paused && fixture.queue.InFlightCount() == 1 && fixture.queue.LentCount() == 0 &&
               fixture.queue.MapAttemptsForTest() == mapsAfterCopy &&
               fixture.scheduler.ReservedMemoryBytes() == reservation && completion.calls == 0 &&
               !fixture.scheduler.IsIdle(),
           "await-copy.terminal-drain-retains-ticket-until-fence-ack");

    fixture.owner.Resume();
    const bool settled = SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, "await-copy.fence-acknowledged-and-settled");
    if (!settled)
        return;
    Expect(completion.calls == 1 && completion.ticket == ticket && completion.result.status == JobStatus::Cancelled &&
               completion.reservedAtCallback == reservation,
           "await-copy.single-cancelled-callback-after-queue-ack");
    Expect(fixture.scheduler.ReservedMemoryBytes() == 0 && fixture.queue.LentCount() == 0 &&
               fixture.queue.InFlightCount() == 0 && !fixture.queue.HasPending(),
           "await-copy.all-charges-and-receiver-state-cleared");
}

void RunHeldRowCancellation(unsigned int heldRow)
{
    constexpr std::array<std::uint8_t, 32> pixels{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                                  17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
    constexpr std::uint8_t guard = 199;
    constexpr std::uint64_t reservation = 1ULL << 20;
    Device device;
    Fixture fixture;
    Observer observer;
    if (FAILED(device.Create()) || !device.device || !fixture.Initialize() || !fixture.Bind(device) ||
        !observer.entered || !observer.release)
    {
        Expect(false, "row-cancel.fixture");
        return;
    }
    auto source = Raster::Make(2, 4, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    auto input = Input(Layout(source, 0, 0, 2, 4, 1, 32)); // One four-row chunk: observe actual middle/final rows.
    auto target = MakeTarget(device, 2, 4, DXGI_FORMAT_B8G8R8A8_UNORM, guard);
    Expect(input && target, "row-cancel.fixtures-created");
    if (!input || !target)
        return;
    Completion completion;
    auto concrete =
        std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue, std::move(input), target.get(), completion);
    observer.holdRow = heldRow;
    concrete->ObserveRows(Observer::Row, &observer);
    std::unique_ptr<Job> job = std::move(concrete);
    JobTicket ticket{};
    const bool submitted = Submit(fixture, job, reservation, true, ticket);
    Expect(submitted, "row-cancel.submitted");
    const auto releaseGate = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(observer.release.get());
            fixture.Close();
        });
    Expect(submitted && WaitForEvent(fixture.scheduler, observer.entered.get()), "row-cancel.observer-held");
    Expect(observer.observedRows.load(std::memory_order_acquire) == heldRow,
           "row-cancel.exact-mid-or-final-row-boundary");
    Expect(fixture.queue.LentCount() == 1 && fixture.scheduler.ReservedMemoryBytes() == reservation &&
               completion.calls == 0,
           "row-cancel.ticket-reservation-and-loan-held");
    const bool cancelAccepted = fixture.scheduler.Cancel(ticket);
    Expect(cancelAccepted, "row-cancel.owner-cancel-accepted");
    (void)SetEvent(observer.release.get());
    const bool settled = SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, "row-cancel.settled");
    if (!settled)
        return;
    const std::vector<std::uint8_t> expected(2U * 4U * 4U, guard);
    Expect(completion.calls == 1 && completion.ticket == ticket && completion.result.status == JobStatus::Cancelled &&
               completion.reservedAtCallback == reservation && TargetEquals(device, *target.get(), expected, 4),
           "row-cancel.one-cancelled-result-and-copy-suppressed");
    Expect(fixture.scheduler.ReservedMemoryBytes() == 0 && fixture.queue.LentCount() == 0 &&
               fixture.queue.InFlightCount() == 0 && !fixture.queue.HasPending(),
           "row-cancel.no-reservation-or-mapped-survivors");
}

void RunPublicationOrdering(bool cancel, bool retire)
{
    constexpr std::array<std::uint8_t, 16> pixels{7, 11, 13, 255, 17, 19, 23, 255, 29, 31, 37, 255, 41, 43, 47, 255};
    constexpr std::uint64_t reservation = 1ULL << 20;
    constexpr std::uint8_t guard = 53;
    Device device, replacement;
    Fixture fixture;
    Observer observer;
    if (retire)
        (void)replacement.Create();
    if (FAILED(device.Create()) || !device.device || (retire && !replacement.device) || !fixture.Initialize() ||
        !fixture.Bind(device) || !observer.entered || !observer.release)
    {
        Expect(false, "publish.fixture");
        return;
    }
    auto source = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    auto input = Input(Layout(source, 0, 0, 2, 2, 1, 16));
    auto target = MakeTarget(device, 2, 2, DXGI_FORMAT_B8G8R8A8_UNORM, guard);
    Expect(input && target, "publish.fixtures-created");
    if (!input || !target)
        return;
    Completion completion;
    auto concrete =
        std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue, std::move(input), target.get(), completion);
    concrete->ObservePublication(Observer::Publication, &observer);
    std::unique_ptr<Job> job = std::move(concrete);
    JobTicket ticket{};
    const bool submitted = Submit(fixture, job, reservation, true, ticket);
    Expect(submitted, "publish.submitted-with-deferred-retirement");
    const auto releaseGate = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(observer.release.get());
            fixture.Close();
        });
    const bool mapped =
        submitted && Until(fixture.scheduler, [&]() noexcept { return fixture.queue.LentCount() == 1; });
    Expect(mapped && fixture.queue.LentCount() == 1, "publish.owner-pump-delivers-writable-loan");
    const bool producerHeld = mapped && WaitForSingleObject(observer.entered.get(), 10000) == WAIT_OBJECT_0;
    Expect(producerHeld, "publish.direct-bounded-wait-after-publication-without-owner-pump");
    if (!producerHeld)
    {
        (void)SetEvent(observer.release.get());
        return;
    }

    if (cancel)
    {
        Expect(fixture.scheduler.Cancel(ticket), "publish.actual-scheduler-cancel-accepted");
        fixture.queue.Cancel(ticket);
    }
    if (retire)
    {
        fixture.queue.RetireDevice();
        Expect(fixture.queue.StagingBytes() != 0 && fixture.queue.LentCount() == 1,
               "publish.retirement-retains-held-mapping-until-ack");
    }
    const bool queueFinished = Until(fixture.scheduler, [&]() noexcept
                                     { return !fixture.queue.HasPending() && fixture.queue.InFlightCount() == 0; });
    Expect(queueFinished && !fixture.queue.HasPending() && fixture.queue.InFlightCount() == 0,
           "publish.queue-callback-and-copy-or-abort-finish-before-worker-release");
    Expect(completion.calls == 0 && fixture.scheduler.ReservedMemoryBytes() == reservation &&
               !fixture.scheduler.IsIdle(),
           "publish.ticket-job-and-reservation-live-after-queue-ack");

    (void)SetEvent(observer.release.get());
    const bool settled = SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, "publish.worker-released-and-settled");
    if (!settled)
        return;
    const JobStatus expectedStatus = cancel ? JobStatus::Cancelled : retire ? JobStatus::Failed : JobStatus::Finished;
    const std::int32_t expectedError = retire ? E_ABORT : 0;
    Expect(completion.calls == 1 && completion.ticket == ticket && completion.result.status == expectedStatus &&
               completion.result.error == expectedError && completion.reservedAtCallback == reservation &&
               fixture.scheduler.ReservedMemoryBytes() == 0,
           "publish.single-final-result-retains-local-retirement-error");
    if (!cancel && !retire)
    {
        const std::vector<std::uint8_t> expected(pixels.begin(), pixels.end());
        Expect(TargetEquals(device, *target.get(), expected, 4), "publish.queue-copy-literal-after-ack");
    }
    Expect(fixture.queue.LentCount() == 0 && fixture.queue.InFlightCount() == 0 && !fixture.queue.HasPending(),
           "publish.no-dangling-receiver-or-queue-slot");
}

struct ShutdownState final
{
    GpuUploadQueue* queue = nullptr;
    bool ownerHookCalled = false;
    std::size_t lentAtOwnerHook = 0;
    std::uint64_t stagingAtOwnerHook = 0;
};

void CloseQueueAfterJoin(void* context) noexcept
{
    auto& state = *static_cast<ShutdownState*>(context);
    state.ownerHookCalled = true;
    state.lentAtOwnerHook = state.queue->LentCount();
    state.stagingAtOwnerHook = state.queue->StagingBytes();
    state.queue->CloseAfterWorkers();
}

void RunShutdownWhilePublished()
{
    constexpr std::array<std::uint8_t, 16> pixels{7, 11, 13, 255, 17, 19, 23, 255, 29, 31, 37, 255, 41, 43, 47, 255};
    Device device;
    Fixture fixture;
    Observer observer;
    if (FAILED(device.Create()) || !device.device || !fixture.Initialize() || !fixture.Bind(device) ||
        !observer.entered)
    {
        Expect(false, "shutdown.fixture");
        return;
    }
    auto source = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    auto input = Input(Layout(source, 0, 0, 2, 2, 1, 16));
    auto target = MakeTarget(device, 2, 2, DXGI_FORMAT_B8G8R8A8_UNORM, 53);
    Expect(input && target, "shutdown.fixtures-created");
    if (!input || !target)
        return;
    Completion completion;
    auto concrete =
        std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue, std::move(input), target.get(), completion);
    concrete->ObservePublication(Observer::Publication, &observer);
    std::unique_ptr<Job> job = std::move(concrete);
    JobTicket ticket{};
    constexpr std::uint64_t reservation = 1ULL << 20;
    const bool submitted = Submit(fixture, job, reservation, true, ticket);
    Expect(submitted, "shutdown.job-submitted");
    if (!submitted)
        return;
    bool closingForShutdown = false;
    const auto releaseOnFailure = wil::scope_exit(
        [&]() noexcept
        {
            if (!closingForShutdown)
            {
                (void)SetEvent(observer.release.get());
                fixture.Close();
            }
        });
    const bool mapped = Until(fixture.scheduler, [&]() noexcept { return fixture.queue.LentCount() == 1; });
    Expect(mapped && fixture.queue.LentCount() == 1, "shutdown.owner-pump-delivers-writable-loan");
    if (!mapped)
        return;
    // Wait directly after WritableRowsReady/Resume. Pumping here could consume the Ready publication and copy it.
    const bool producerHeld = WaitForSingleObject(observer.entered.get(), 10000) == WAIT_OBJECT_0;
    Expect(producerHeld, "shutdown.direct-bounded-wait-for-held-publication-without-owner-pump");
    if (!producerHeld)
        return;
    Expect(fixture.queue.HasReady() && fixture.queue.LentCount() == 1 && fixture.queue.StagingBytes() != 0 &&
               fixture.scheduler.ReservedMemoryBytes() == reservation && completion.calls == 0,
           "shutdown.producer-publication-ack-loan-and-reservation-held");

    ShutdownState state{&fixture.queue};
    fixture.queue.StopNotifications();
    closingForShutdown = true;
    fixture.scheduler.Close({CloseQueueAfterJoin, nullptr, &state});
    fixture.initialized = false; // The owner hook closed the queue after the scheduler joined the worker.
    Expect(observer.wokeForShutdown && state.ownerHookCalled && state.lentAtOwnerHook == 1 &&
               state.stagingAtOwnerHook != 0,
           "shutdown.joined-producer-before-owner-unmap");
    Expect(completion.calls == 0 && fixture.queue.StagingBytes() == 0 && fixture.queue.LentCount() == 0 &&
               fixture.queue.InFlightCount() == 0 && !fixture.queue.HasPending() &&
               fixture.scheduler.ReservedMemoryBytes() == 0,
           "shutdown.drops-final-callback-and-releases-every-charge");
}

void RunQueueFault(GpuUploadFailurePoint point, HRESULT error)
{
    constexpr std::array<std::uint8_t, 16> pixels{7, 11, 13, 255, 17, 19, 23, 255, 29, 31, 37, 255, 41, 43, 47, 255};
    constexpr std::uint8_t guard = 53;
    Device device;
    Fixture fixture;
    if (FAILED(device.Create()) || !device.device || !fixture.Initialize() || !fixture.Bind(device))
    {
        Expect(false, "fault.fixture");
        return;
    }
    auto source = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    auto input = Input(Layout(source, 0, 0, 2, 2, 1, 16));
    auto target = MakeTarget(device, 2, 2, DXGI_FORMAT_B8G8R8A8_UNORM, guard);
    Expect(input && target, "fault.fixtures-created");
    if (!input || !target)
        return;
    fixture.queue.SetFailureForTest(point, error);
    const unsigned int mapsBefore = fixture.queue.MapAttemptsForTest();
    Completion completion;
    std::unique_ptr<Job> job =
        std::make_unique<ProbeJob>(fixture.scheduler, fixture.queue, std::move(input), target.get(), completion);
    JobTicket ticket{};
    const bool submitted = Submit(fixture, job, 1ULL << 20, true, ticket);
    Expect(submitted, "fault.job-submitted");
    const bool settled = submitted && SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, "fault.settled");
    if (!settled)
    {
        fixture.Close();
        return;
    }
    Expect(completion.calls == 1 && completion.ticket == ticket && completion.result.status == JobStatus::Failed &&
               completion.result.error == static_cast<std::int32_t>(error) &&
               completion.reservedAtCallback == (1ULL << 20) && fixture.queue.FailureHitsForTest() == 1,
           "fault.one-final-failed-result-preserves-queue-error");
    if (point == GpuUploadFailurePoint::Map)
    {
        std::vector<std::uint8_t> expected(2U * 2U * 4U, guard);
        Expect(fixture.queue.MapAttemptsForTest() == mapsBefore + 1 && TargetEquals(device, *target.get(), expected, 4),
               "fault.map-refusal-no-target-write");
    }
    Expect(fixture.scheduler.ReservedMemoryBytes() == 0 && fixture.queue.LentCount() == 0 &&
               fixture.queue.InFlightCount() == 0 && !fixture.queue.HasPending(),
           "fault.no-reservation-or-slot-survivor");
}
} // namespace

int RunGpuRasterUploadJobTests()
{
    gFailures = 0;
    RunBgraCropChunks();
    RunGrayScale2OddCrop();
    RunEarlyRefusals();
    RunProducerSkippedCancellation();
    RunPendingCancellation();
    RunAwaitCopyCancellation();
    RunHeldRowCancellation(2);
    RunHeldRowCancellation(4);
    RunPublicationOrdering(false, false);
    RunPublicationOrdering(true, false);
    RunPublicationOrdering(false, true);
    RunShutdownWhilePublished();
    RunQueueFault(GpuUploadFailurePoint::Map, E_ACCESSDENIED);
    RunQueueFault(GpuUploadFailurePoint::CopySignal, E_ACCESSDENIED);
    return gFailures;
}
