#include "GpuMipChainJobTests.h"

#include "GpuRasterUploadJob.h"
#include "OwnerWorkMultiplexer.h"

#include <algorithm>
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
    std::printf("%s gpu-mip-chain-job.%s.%s\n", value ? "PASS" : "FAIL", name, detail);
    gFailures += value ? 0 : 1;
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {16ull * 1024 * 1024 * 1024, 16ull * 1024 * 1024 * 1024};
    return true;
}

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
        if (paused.load(std::memory_order_acquire))
            return;
        if (pauseBeforeMip && queue && queue->HasPending() && queue->InFlightCount() == 0 &&
            queue->MapAttemptsForTest() == expectedMaps && queue->RecordedMipSlices() == 0)
        {
            paused.store(true, std::memory_order_release);
            return;
        }
        const auto before = queue ? queue->RecordedMipSlices() : 0;
        routes.Pump();
        const auto after = queue ? queue->RecordedMipSlices() : 0;
        bounded = bounded && after >= before && after - before <= 1;
        if (pauseAfterMip && after != 0)
            paused.store(true, std::memory_order_release);
    }
    OwnerWorkMultiplexer& routes;
    std::atomic_bool& paused;
    GpuUploadQueue* queue = nullptr;
    unsigned int expectedMaps = 0;
    bool pauseBeforeMip = false;
    bool pauseAfterMip = false;
    bool bounded = true;
};

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
        owner.queue = &queue;
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

class Probe final : public GpuRasterUploadJob
{
  public:
    Probe(Fixture& fixture, std::unique_ptr<GpuRasterUploadInput> input, GpuResidencyReservation reservation,
          Completion& completion, ID3D11Device* device) noexcept
        : GpuRasterUploadJob(fixture.scheduler, fixture.queue, std::move(input),
                             {device, std::move(reservation), true}),
          _fixture(fixture), _completion(completion)
    {
    }
    [[nodiscard]] bool RefusesEarlyAdoption() noexcept
    {
        return !TakeCreatedResidencyOwner();
    }
    bool FailMipView(unsigned int slice) noexcept
    {
        return SetMipViewFailureForTest(slice, E_ACCESSDENIED);
    }
    bool FailCreation(GpuRasterCreationFailure point) noexcept
    {
        return SetCreationFailureForTest(point, E_ACCESSDENIED);
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

std::unique_ptr<GpuRasterUploadInput> Input(const Raster& source, int scale, bool mips)
{
    auto layout = RasterUploadLayout::Make(
        source, 0, 0, source.Width(), source.Height(), scale, 4096,
        static_cast<std::uint64_t>(1 + (source.Width() - 1) / scale) * source.Channels(), 4096, mips);
    return GpuRasterUploadInput::Make(std::move(layout));
}

bool Submit(Fixture& fixture, std::unique_ptr<Job>& job, JobTicket& ticket) noexcept
{
    return fixture.scheduler.Submit(job, {JobClass::Commit, 0x435245415445ull, kCpuReservation, true}, ticket) == S_OK;
}

bool MipEquals(Fixture& fixture, ID3D11Texture2D* source, unsigned int mip, std::span<const std::uint8_t> expected,
               unsigned int channels) noexcept
{
    if (!source)
        return false;
    D3D11_TEXTURE2D_DESC description{};
    source->GetDesc(&description);
    if (mip >= description.MipLevels)
        return false;
    description.Width = (std::max)(1U, description.Width >> mip);
    description.Height = (std::max)(1U, description.Height >> mip);
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
    fixture.context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, source, mip, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(fixture.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const auto unmap = wil::scope_exit([&]() noexcept { fixture.context->Unmap(staging.get(), 0); });
    if (!mapped.pData || mapped.RowPitch < row)
        return false;
    for (unsigned int y = 0; y < description.Height; ++y)
        if (std::memcmp(static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                        expected.data() + static_cast<std::size_t>(y) * row, row) != 0)
        {
            const auto* actual =
                static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
            std::printf("INFO gpu-mip-chain-job.readback mip=%u row=%u first=%u expected=%u\n", mip, y,
                        static_cast<unsigned int>(actual[0]),
                        static_cast<unsigned int>(expected[static_cast<std::size_t>(y) * row]));
            return false;
        }
    return true;
}

void Success(const char* name, const Raster& source, std::uint64_t logicalBytes, unsigned int expectedSlices,
             std::span<const std::uint8_t> base, std::span<const std::uint8_t> last,
             std::span<const std::uint8_t> firstMip = {})
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
    // A mip draw must establish its pipeline even after unrelated owner rendering. These states would
    // suppress every pixel if the queue inherited blending, scissoring or the old viewport.
    D3D11_BLEND_DESC blendDescription{};
    wil::com_ptr_nothrow<ID3D11BlendState> noWrites;
    D3D11_RASTERIZER_DESC rasterizerDescription{};
    rasterizerDescription.FillMode = D3D11_FILL_SOLID;
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    rasterizerDescription.ScissorEnable = TRUE;
    wil::com_ptr_nothrow<ID3D11RasterizerState> emptyScissor;
    const bool contaminated =
        SUCCEEDED(fixture.device->CreateBlendState(&blendDescription, noWrites.put())) &&
        SUCCEEDED(fixture.device->CreateRasterizerState(&rasterizerDescription, emptyScissor.put()));
    Expect(contaminated, name, "unrelated-owner-state-installed");
    if (!contaminated)
        return;
    const D3D11_RECT emptyRect{};
    const D3D11_VIEWPORT oldViewport{37, 41, 1, 1, 0, 1};
    fixture.context->OMSetBlendState(noWrites.get(), nullptr, 0);
    fixture.context->RSSetState(emptyScissor.get());
    fixture.context->RSSetScissorRects(1, &emptyRect);
    fixture.context->RSSetViewports(1, &oldViewport);
    auto input = Input(source, 1, true);
    auto reservation = fixture.Reserve(kGpuAllowance + logicalBytes);
    Expect(input && reservation, name, "complete-graph-admitted");
    if (!input || !reservation)
        return;
    auto probe =
        std::make_unique<Probe>(fixture, std::move(input), std::move(reservation), completed, fixture.device.get());
    Expect(probe->RefusesEarlyAdoption(), name, "incomplete-chain-not-adoptable");
    std::unique_ptr<Job> job = std::move(probe);
    JobTicket ticket;
    const bool submitted = Submit(fixture, job, ticket);
    const bool settled = submitted && SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, name, "all-slices-event-driven-settlement");
    if (!settled)
        return;
    Expect(completed.calls == 1 && completed.ticket == ticket && completed.result.status == JobStatus::Finished &&
               completed.residency && completed.residency.MipsComplete(),
           name, "one-complete-original-ticket-publication");
    Expect(fixture.queue.RecordedMipSlices() == expectedSlices && fixture.owner.bounded, name,
           "one-adjacent-level-record-per-pump");
    Expect(completed.cpuAtCallback == kCpuReservation && completed.gpuAtCallback == 2 * kGpuAllowance + logicalBytes &&
               fixture.scheduler.ReservedMemoryBytes() == 0,
           name, "whole-graph-charged-through-last-mip-acknowledgement");
    Expect(completed.chunks == static_cast<unsigned int>(source.Height()) &&
               fixture.queue.MapAttemptsForTest() == static_cast<unsigned int>(source.Height()),
           name, "mip-stage-never-maps-or-replays-base-rows");
    Expect(MipEquals(fixture, completed.residency.TextureOwner(), 0, base, source.Channels()), name,
           "literal-base-preserved");
    Expect(MipEquals(fixture, completed.residency.TextureOwner(), expectedSlices, last, source.Channels()), name,
           "literal-final-mip-after-completion");
    if (!firstMip.empty())
        Expect(MipEquals(fixture, completed.residency.TextureOwner(), 1, firstMip, source.Channels()), name,
               "literal-intermediate-mip-after-completion");
    Expect(completed.duplicateRefused && completed.oldBorrowCleared, name,
           "exactly-once-adoption-clears-producer-borrows");
    auto moved = std::move(completed.residency);
    Expect(moved.MipsComplete() && !completed.residency.MipsComplete(), name, "complete-mip-authority-follows-move");
    moved.ResetOwner();
    Expect(!moved.MipsComplete() && fixture.budget.ReservedOwner() == kGpuAllowance, name,
           "mip-owner-release-leaves-only-reusable-pool-charge");
}

void PipelineFailure(const char* name, GpuRasterCreationFailure point)
{
    Fixture fixture;
    Completion completed;
    const auto close = wil::scope_exit([&]() noexcept { fixture.Close(); });
    const bool ready = fixture.Initialize();
    Expect(ready, name, "real-warp-fixture");
    if (!ready)
        return;
    constexpr std::uint64_t logical = 64 + 16 + 4 + 1;
    const auto source = Raster::Make(8, 8, RasterFormat::Gray8);
    auto probe = std::make_unique<Probe>(fixture, Input(source, 1, true), fixture.Reserve(kGpuAllowance + logical),
                                         completed, fixture.device.get());
    Expect(probe->FailCreation(point), name, "exact-worker-pipeline-fault-selected");
    std::unique_ptr<Job> job = std::move(probe);
    JobTicket ticket;
    const bool submitted = Submit(fixture, job, ticket);
    const bool settled = submitted && SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled && completed.calls == 1 && completed.ticket == ticket &&
               completed.result.status == JobStatus::Failed &&
               static_cast<HRESULT>(completed.result.error) == E_ACCESSDENIED && !completed.residency,
           name, "original-error-no-partial-publication");
    Expect(fixture.queue.RecordedMipSlices() == 0 && fixture.queue.MapAttemptsForTest() == 0, name,
           "partial-pipeline-never-reaches-owner-recording");
    Expect(fixture.scheduler.ReservedMemoryBytes() == 0 && fixture.budget.ReservedOwner() == kGpuAllowance, name,
           "joined-partial-owner-release-drains-destination-charge");
    fixture.Close();
    Expect(fixture.budget.ReservedOwner() == 0, name, "joined-queue-release-drains-persistent-charge");
}

void Fault(bool viewFailure, bool signalFailure)
{
    const char* name = viewFailure     ? "view-prefix-failure"
                       : signalFailure ? "post-mip-signal-failure"
                                       : "pre-record-refusal";
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
    const auto source = Raster::Make(8, 8, RasterFormat::Gray8);
    constexpr std::uint64_t logical = 64 + 16 + 4 + 1;
    const auto gpu = kGpuAllowance + logical;
    auto probe =
        std::make_unique<Probe>(fixture, Input(source, 1, true), fixture.Reserve(gpu), completed, fixture.device.get());
    if (viewFailure)
        Expect(probe->FailMipView(1), name, "worker-slice-view-fault-after-one-prefix");
    else
        fixture.queue.SetFailureForTest(
            signalFailure ? GpuUploadFailurePoint::MipSignal : GpuUploadFailurePoint::MipRecording, E_ACCESSDENIED);
    std::unique_ptr<Job> job = std::move(probe);
    JobTicket ticket;
    const bool submitted = Submit(fixture, job, ticket);
    const bool settled = submitted && SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
    Expect(settled, name, "failed-chain-settles-original-ticket");
    Expect(completed.calls == 1 && completed.ticket == ticket && completed.result.status == JobStatus::Failed &&
               completed.result.error == E_ACCESSDENIED && !completed.residency,
           name, "original-error-no-partial-adoption");
    Expect(fixture.queue.RecordedMipSlices() == (signalFailure ? 1U : 0U), name,
           "exact-resource-recording-fault-boundary");
    Expect(fixture.queue.MapAttemptsForTest() == (viewFailure ? 0U : 8U), name,
           "creation-failure-precedes-upload-record-failure-follows-base");
    Expect(fixture.queue.RetiredResidencyBytes() == (signalFailure ? gpu : 0U) &&
               fixture.budget.ReservedOwner() == kGpuAllowance + (signalFailure ? gpu : 0U),
           name, "uncertain-mip-context-keeps-whole-charge");
    fixture.Close();
    Expect(fixture.budget.ReservedOwner() == 0, name, "joined-owner-release-drains-mip-charge");
}

bool AwaitPaused(Fixture& fixture) noexcept
{
    const auto begin = GetTickCount64();
    for (;;)
    {
        (void)DrainCompletions(fixture.scheduler);
        if (fixture.pauseOwner.load(std::memory_order_acquire))
            return true;
        const auto elapsed = GetTickCount64() - begin;
        if (elapsed >= 10000)
            return false;
        if (fixture.scheduler.HasUiWork())
            continue;
        const HANDLE event = static_cast<HANDLE>(fixture.scheduler.WakeHandle());
        if (WaitForSingleObject(event, static_cast<DWORD>(10000 - elapsed)) != WAIT_OBJECT_0)
            return false;
    }
}

void CancelOrShutdown(bool shutdown, bool beforeRecord)
{
    const char* name = shutdown       ? "shutdown-after-mip-record"
                       : beforeRecord ? "cancel-before-mip-record"
                                      : "cancel-after-mip-record";
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
    constexpr std::uint64_t logical = 64 + 16 + 4 + 1;
    const auto gpu = kGpuAllowance + logical;
    const auto source = Raster::Make(8, 8, RasterFormat::Gray8);
    fixture.owner.expectedMaps = 8;
    fixture.owner.pauseBeforeMip = beforeRecord;
    fixture.owner.pauseAfterMip = !beforeRecord;
    std::unique_ptr<Job> job =
        std::make_unique<Probe>(fixture, Input(source, 1, true), fixture.Reserve(gpu), completed, fixture.device.get());
    JobTicket ticket;
    const bool submitted = Submit(fixture, job, ticket);
    const bool held = submitted && AwaitPaused(fixture);
    Expect(held, name, "original-chain-held-at-mip-boundary");
    if (!held)
        return;
    Expect(completed.calls == 0 && fixture.scheduler.ReservedMemoryBytes() == kCpuReservation &&
               fixture.budget.ReservedOwner() == kGpuAllowance + gpu,
           name, "no-publication-and-whole-graph-charged");
    Expect(fixture.queue.RecordedMipSlices() == (beforeRecord ? 0U : 1U) && fixture.owner.bounded, name,
           "exact-slice-prefix-held");
    if (shutdown)
    {
        fixture.queue.StopNotifications();
        fixture.scheduler.Close();
        Expect(completed.calls == 0 && fixture.queue.RetiredResidencyBytes() == gpu &&
                   fixture.budget.ReservedOwner() == kGpuAllowance + gpu,
               name, "shutdown-suppresses-callback-retains-mip-view-context-charge");
        fixture.queue.CloseAfterWorkers();
        fixture.initialized = false;
        Expect(fixture.budget.ReservedOwner() == 0, name, "joined-owner-mip-view-release-ends-charge");
    }
    else
    {
        Expect(fixture.scheduler.Cancel(ticket), name, "cancel-original-suspended-ticket");
        fixture.owner.pauseBeforeMip = false;
        fixture.owner.pauseAfterMip = false;
        fixture.pauseOwner.store(false, std::memory_order_release);
        const bool settled = SettleJobs(fixture.scheduler, 10000) == SettleResult::Settled;
        Expect(settled && completed.calls == 1 && completed.ticket == ticket &&
                   completed.result.status == JobStatus::Cancelled && !completed.residency,
               name, "terminal-cancel-acknowledges-without-partial-mips");
        Expect(fixture.queue.RecordedMipSlices() == (beforeRecord ? 0U : 1U) &&
                   fixture.scheduler.ReservedMemoryBytes() == 0 && fixture.budget.ReservedOwner() == kGpuAllowance,
               name, "cancel-never-records-another-level-and-drains-safe-owners");
    }
}
} // namespace

int RunGpuMipChainJobTests()
{
    gFailures = 0;
    constexpr std::array<std::uint8_t, 16> bgra{0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 192, 192, 192, 255};
    constexpr std::array<std::uint8_t, 4> bgraMip{96, 96, 96, 255};
    Success("bgra-two-level", Raster::Make(2, 2, RasterFormat::Bgra8888, bgra.data(), bgra.size()), 20, 1, bgra,
            bgraMip);
    constexpr std::array<std::uint8_t, 64> gray = []() constexpr
    {
        std::array<std::uint8_t, 64> p{};
        p.fill(128);
        return p;
    }();
    constexpr std::array<std::uint8_t, 1> grayMip{128};
    Success("gray-four-level", Raster::Make(8, 8, RasterFormat::Gray8, gray.data(), gray.size()), 85, 3, gray, grayMip);
    constexpr std::array<std::uint8_t, 1> singleton{213};
    Success("one-level-no-record", Raster::Make(1, 1, RasterFormat::Gray8, singleton.data(), singleton.size()), 1, 0,
            singleton, singleton);
    // Independent normalized-center linear-filter literals. 3x5 -> 1x2 samples x=1 and y=.75/3.25;
    // 5x3 -> 2x1 samples x=.75/3.25 and y=1. These also test axes that reach one pixel at different levels.
    constexpr std::array<std::uint8_t, 15> tall{0, 8, 16, 32, 40, 48, 64, 72, 80, 96, 104, 112, 128, 136, 144};
    constexpr std::array<std::uint8_t, 2> tallFirst{32, 112};
    constexpr std::array<std::uint8_t, 1> tallLast{72};
    Success("odd-tall", Raster::Make(3, 5, RasterFormat::Gray8, tall.data(), tall.size()), 18, 2, tall, tallLast,
            tallFirst);
    constexpr std::array<std::uint8_t, 15> wide{0, 8, 16, 24, 32, 32, 40, 48, 56, 64, 64, 72, 80, 88, 96};
    constexpr std::array<std::uint8_t, 2> wideFirst{38, 58};
    constexpr std::array<std::uint8_t, 1> wideLast{48};
    Success("odd-wide", Raster::Make(5, 3, RasterFormat::Gray8, wide.data(), wide.size()), 18, 2, wide, wideLast,
            wideFirst);
    constexpr std::array<std::uint8_t, 16> varying{0, 16, 32, 48, 32, 48, 64, 80, 64, 80, 96, 112, 96, 112, 128, 144};
    constexpr std::array<std::uint8_t, 4> varyingFirst{24, 56, 88, 120};
    constexpr std::array<std::uint8_t, 1> varyingLast{72};
    Success("gray-three-varying-levels", Raster::Make(4, 4, RasterFormat::Gray8, varying.data(), varying.size()), 21, 2,
            varying, varyingLast, varyingFirst);
    constexpr std::array<std::uint8_t, 16> premultiplied{0, 0, 0, 0, 32, 16, 8, 64, 64, 32, 16, 128, 96, 48, 24, 192};
    constexpr std::array<std::uint8_t, 4> premultipliedMip{48, 24, 12, 96};
    Success("premultiplied-alpha",
            Raster::Make(2, 2, RasterFormat::Bgra8888, premultiplied.data(), premultiplied.size()), 20, 1,
            premultiplied, premultipliedMip);
    Fault(true, false);
    Fault(false, false);
    Fault(false, true);
    PipelineFailure("vertex-creation-failure", GpuRasterCreationFailure::MipVertexShader);
    PipelineFailure("pixel-creation-failure", GpuRasterCreationFailure::MipPixelShader);
    PipelineFailure("sampler-creation-failure", GpuRasterCreationFailure::MipSampler);
    PipelineFailure("rasterizer-creation-failure", GpuRasterCreationFailure::MipRasterizer);
    PipelineFailure("depth-creation-failure", GpuRasterCreationFailure::MipDepthStencil);
    CancelOrShutdown(false, true);
    CancelOrShutdown(false, false);
    CancelOrShutdown(true, false);
    return gFailures;
}
