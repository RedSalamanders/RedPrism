#include "Composition.Tests.SelectionOutlineResidency.h"

#include "GpuWork.h"
#include "PreparedSelectionOutlineGpu.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <objbase.h>
#include <thread>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
using OutlineRaster = PreparedSelectionOutlineRaster;
using Grid = OutlineRaster::GridKey;
unsigned int passed = 0;
unsigned int failed = 0;
void Expect(bool value, const char* control) noexcept
{
    std::printf("%s selection-outline-residency.%s\n", value ? "PASS" : "FAIL", control);
    value ? ++passed : ++failed;
}
void Indexed(bool value, const char* scenario, unsigned int index, const char* control) noexcept
{
    std::array<char, 160> label{};
    const auto length = std::snprintf(label.data(), label.size(), "%s.%02u.%s", scenario, index, control);
    Expect(length > 0 && static_cast<std::size_t>(length) < label.size() && value, label.data());
}
bool Memory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {8ull * 1024 * 1024 * 1024, 8ull * 1024 * 1024 * 1024};
    return true;
}
struct Rig final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    JobScheduler jobs;
    GpuWorkQueue queue;
    std::uint64_t generation = 0;
    bool Initialize() noexcept
    {
        constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level,
                                     1, D3D11_SDK_VERSION, device.put(), nullptr, immediate.put())) ||
            FAILED(jobs.Initialize(1, {}, {Memory, nullptr})) || FAILED(queue.Initialize(jobs)) ||
            FAILED(queue.Bind(device.get(), immediate.get())))
            return false;
        generation = queue.DeviceGeneration();
        return generation != 0;
    }
    ~Rig()
    {
        queue.StopNotifications();
        jobs.Close();
        queue.CloseAfterWorkers();
    }
};
constexpr Grid grid{16, 16, 1.0, 4.0, 4.0, 96.0f};
constexpr std::uint64_t gpuBytes = 2ull * 9 * 16 * 16 + 64ull * 1024 * 1024;
GpuResidencyReservation Reserve(GpuResidencyBudget& budget, std::uint64_t generation, std::uint64_t bytes = gpuBytes)
{
    GpuResidencyAdmission status{};
    auto result = budget.ReserveOwner(bytes, {generation, 1024ull * 1024 * 1024, 0}, status);
    Expect(status == GpuResidencyAdmission::Ready && bool(result), "fixture.original-gpu-reservation");
    return result;
}
SelectionLoops Source(std::shared_ptr<const void> lifetime = {})
{
    std::optional<SelectionLoops> source;
    std::thread worker(
        [&]()
        { source = SelectionLoops::AdoptForWork({{{8, 8}, {24, 8}, {24, 24}, {8, 24}}}, {}, std::move(lifetime)); });
    worker.join();
    Expect(source.has_value() && source->PointCount() == 4, "fixture.worker-adopted-original-source");
    return source ? std::move(*source) : SelectionLoops{};
}
OutlineRaster RasterFor(const SelectionLoops& source)
{
    OutlineRaster raster;
    HRESULT result = E_PENDING;
    bool bounded = false;
    std::thread worker(
        [&]()
        {
            const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const auto leave = wil::scope_exit(
                [&]() noexcept
                {
                    if (SUCCEEDED(apartment))
                        CoUninitialize();
                });
            if (FAILED(apartment))
            {
                result = apartment;
                return;
            }
            OutlineRaster::MemoryPlan plan;
            result = OutlineRaster::PlanMemory(grid, source.size(), source.PointCount(), plan);
            if (FAILED(result))
                return;
            auto memory = SharedPublicationMemory::CreateForWork(plan.outputBytes);
            result = OutlineRaster::Prepare(source, grid, memory, {}, {}, raster);
            bounded = memory && memory->LocalAllocationBytes() <= plan.outputBytes;
        });
    worker.join();
    Expect(result == S_OK && bounded && raster.HasStorage(), "fixture.com-worker-prepared-locally-bounded-masks");
    return raster;
}
HRESULT Create(PreparedSelectionOutlineGpu& gpu, OutlineRaster raster, CancelToken cancellation = {},
               PreparedSelectionOutlineGpu::Observer observer = {})
{
    HRESULT result = E_PENDING;
    std::thread worker([&]() noexcept { result = gpu.CreateOnWorker(std::move(raster), cancellation, observer); });
    worker.join();
    return result;
}
bool Retire(PreparedSelectionOutlineGpu& gpu, bool detached = false)
{
    bool retired = false;
    std::thread worker([&]() noexcept
                       { retired = detached ? gpu.RetireDetachedOnWorker() : gpu.RetireUnpublishedOnWorker(); });
    worker.join();
    return retired;
}
void Cleanup(PreparedSelectionOutlineGpu& gpu, GpuResidencyBudget& budget, bool detached = false)
{
    Expect(Retire(gpu, detached) && budget.ReservedOwner() == gpuBytes,
           "retirement.joined-worker-keeps-original-gpu-charge");
    Expect(gpu.CompleteRetirementOwner() && budget.ReservedOwner() == 0, "retirement.owner-refunds-after-worker-join");
}
struct Completion final : GpuCompletionReceiver
{
    unsigned int calls = 0;
    JobTicket ticket{};
    std::uint64_t generation = 0;
    HRESULT result = E_PENDING;
    void GpuCompletionReady(JobTicket next, std::uint64_t epoch, HRESULT status) noexcept override
    {
        ++calls;
        ticket = next;
        generation = epoch;
        result = status;
    }
};
bool FinishGpu(Rig& rig, Completion& completion)
{
    const JobTicket ticket{71, 0x991};
    const auto admitted = rig.queue.EnqueueCompletion(ticket, completion);
    if (admitted != S_OK)
        return false;
    const auto start = GetTickCount64();
    while (completion.calls == 0)
    {
        (void)DrainCompletions(rig.jobs);
        rig.queue.Pump();
        if (completion.calls)
            break;
        const auto elapsed = GetTickCount64() - start;
        if (elapsed >= 10000)
            return false;
        const HANDLE wake = static_cast<HANDLE>(rig.jobs.WakeHandle());
        if (!wake || WaitForSingleObject(wake, static_cast<DWORD>(10000 - elapsed)) != WAIT_OBJECT_0)
            return false;
    }
    return completion.calls == 1 && completion.ticket == ticket && completion.generation == rig.generation &&
           completion.result == S_OK;
}
void TestPlan()
{
    std::uint64_t value = 77;
    Expect(PreparedSelectionOutlineGpu::GpuMemoryBytes(1, 1, value) == S_OK && value == 18 + 64ull * 1024 * 1024,
           "plan.one-pixel-byte-reference");
    Expect(PreparedSelectionOutlineGpu::GpuMemoryBytes(16384, 4096, value) == S_OK &&
               value == 18ull * 16384 * 4096 + 64ull * 1024 * 1024,
           "plan.maximum-pixel-byte-reference");
    constexpr std::array<std::pair<unsigned int, unsigned int>, 6> invalid{
        {{0, 1},
         {1, 0},
         {16385, 1},
         {1, 16385},
         {16384, 4097},
         {(std::numeric_limits<unsigned int>::max)(), (std::numeric_limits<unsigned int>::max)()}}};
    for (unsigned int i = 0; i < invalid.size(); ++i)
    {
        value = 77;
        Indexed(PreparedSelectionOutlineGpu::GpuMemoryBytes(invalid[i].first, invalid[i].second, value) ==
                        E_INVALIDARG &&
                    value == 77,
                "invalid-plan", i, "refusal-preserves-output");
    }
}
void TestRefusals(Rig& rig, const OutlineRaster& raster)
{
    GpuResidencyBudget budget;
    PreparedSelectionOutlineGpu gpu;
    auto original = Reserve(budget, rig.generation);
    Expect(gpu.CaptureOwner(nullptr, rig.immediate.get(), rig.generation, original) == E_INVALIDARG && original,
           "capture.null-device-keeps-original");
    Expect(gpu.CaptureOwner(rig.device.get(), nullptr, rig.generation, original) == E_INVALIDARG && original,
           "capture.null-context-keeps-original");
    Expect(gpu.CaptureOwner(rig.device.get(), rig.immediate.get(), 0, original) == E_INVALIDARG && original,
           "capture.zero-generation-keeps-original");
    Expect(gpu.CaptureOwner(rig.device.get(), rig.immediate.get(), rig.generation + 1, original) == E_INVALIDARG &&
               original,
           "capture.foreign-generation-keeps-original");
    wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
    const HRESULT created = rig.device->CreateDeferredContext(0, deferred.put());
    Expect(created == S_OK &&
               gpu.CaptureOwner(rig.device.get(), deferred.get(), rig.generation, original) == E_INVALIDARG && original,
           "capture.deferred-context-keeps-original");
    Expect(gpu.CaptureOwner(rig.device.get(), rig.immediate.get(), rig.generation, original) == S_OK && !original,
           "capture.moves-validated-original-reservation");
    auto other = Reserve(budget, rig.generation);
    Expect(gpu.CaptureOwner(rig.device.get(), rig.immediate.get(), rig.generation, other) == E_UNEXPECTED && other,
           "capture.duplicate-keeps-second-reservation");
    other.ResetOwner();
    Expect(gpu.CreateOnWorker(raster) == E_ACCESSDENIED && gpu.ResourceCallsOwner() == 0,
           "capture.owner-creation-refuses-without-calls");
    Expect(!gpu.CompleteRetirementOwner(), "capture.early-refund-refused");
    Cleanup(gpu, budget);
    auto restricted = Reserve(budget, rig.generation);
    Expect(gpu.CaptureOwner(rig.device.get(), rig.immediate.get(), rig.generation, restricted) == S_OK &&
               gpu.RestrictConcurrencyForTest(),
           "unsupported.capture-and-restrict");
    Expect(Create(gpu, raster) == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) && gpu.ResourceCallsOwner() == 0 &&
               gpu.ReservedBytesOwner() == gpuBytes,
           "unsupported.worker-refuses-with-no-owner-fallback");
    Cleanup(gpu, budget);
    auto belowBound = Reserve(budget, rig.generation, gpuBytes - 1);
    Expect(gpu.CaptureOwner(rig.device.get(), rig.immediate.get(), rig.generation, belowBound) == S_OK,
           "short-budget.capture-original-smaller-envelope");
    Expect(Create(gpu, raster) == E_OUTOFMEMORY && gpu.ResourceCallsOwner() == 0 &&
               budget.ReservedOwner() == gpuBytes - 1,
           "short-budget.refuses-before-first-resource-call");
    Expect(Retire(gpu) && budget.ReservedOwner() == gpuBytes - 1, "short-budget.worker-retirement-keeps-charge");
    Expect(gpu.CompleteRetirementOwner() && budget.ReservedOwner() == 0, "short-budget.joined-refund");
}
void TestPrefixes(Rig& rig, const OutlineRaster& raster)
{
    for (unsigned int prefix = 0; prefix < 18; ++prefix)
    {
        GpuResidencyBudget budget;
        PreparedSelectionOutlineGpu gpu;
        auto original = Reserve(budget, rig.generation);
        const bool captured = gpu.CaptureOwner(rig.device.get(), rig.immediate.get(), rig.generation, original) == S_OK;
        Indexed(captured && gpu.SetResourceFailureForTest(prefix, E_ACCESSDENIED), "fault", prefix, "installed");
        Indexed(Create(gpu, raster) == E_ACCESSDENIED && gpu.ResourceCallsOwner() == prefix, "fault", prefix,
                "original-error-and-exact-created-prefix");
        Indexed(gpu.PublishOwner(rig.device.get(), rig.immediate.get(), rig.generation, raster.SourceIdentity(),
                                 grid) == E_ACCESSDENIED &&
                    !gpu.BorrowOwner(rig.device.get(), rig.immediate.get(), rig.generation, raster.SourceIdentity(),
                                     grid, 0),
                "fault", prefix, "no-partial-publication");
        Indexed(budget.ReservedOwner() == gpuBytes && !gpu.CompleteRetirementOwner(), "fault", prefix,
                "original-charge-before-retirement");
        Cleanup(gpu, budget);
    }
    for (unsigned int stop = 0; stop <= 18; ++stop)
    {
        GpuResidencyBudget budget;
        PreparedSelectionOutlineGpu gpu;
        auto original = Reserve(budget, rig.generation);
        Expect(gpu.CaptureOwner(rig.device.get(), rig.immediate.get(), rig.generation, original) == S_OK,
               "cancel.capture-original-charge");
        CancelSource cancellation;
        struct Trigger final
        {
            CancelSource* source;
            unsigned int call;
        } trigger{&cancellation, stop};
        if (!stop)
            (void)cancellation.Cancel();
        const PreparedSelectionOutlineGpu::Observer observer{&trigger, [](void* opaque, unsigned int call) noexcept
                                                             {
                                                                 auto& current = *static_cast<Trigger*>(opaque);
                                                                 if (call == current.call)
                                                                     (void)current.source->Cancel();
                                                             }};
        Indexed(Create(gpu, raster, cancellation.Token(), observer) == HRESULT_FROM_WIN32(ERROR_CANCELLED) &&
                    gpu.ResourceCallsOwner() == stop,
                "cancel", stop, "exact-prefix-and-native-cancel-status");
        Indexed(
            !gpu.BorrowOwner(rig.device.get(), rig.immediate.get(), rig.generation, raster.SourceIdentity(), grid, 0) &&
                budget.ReservedOwner() == gpuBytes,
            "cancel", stop, "no-views-and-no-early-refund");
        Cleanup(gpu, budget);
    }
}
void TestTransfer(Rig& rig)
{
    auto originalOwner = std::make_shared<const unsigned int>(73);
    std::weak_ptr<const unsigned int> weakOwner = originalOwner;
    auto source = Source(originalOwner);
    auto weakSource = source.WeakStorageForTest();
    auto raster = RasterFor(source);
    auto weakRaster = raster.WeakStorageForTest();
    const auto identity = source.Identity();
    source = {};
    originalOwner.reset();
    std::array<std::array<std::uint8_t, 256>, 9> expected{};
    for (std::size_t plane = 0; plane < expected.size(); ++plane)
    {
        const auto bytes = raster.Plane(plane);
        std::copy(bytes.begin(), bytes.end(), expected[plane].begin());
    }
    GpuResidencyBudget budget;
    PreparedSelectionOutlineGpu gpu;
    auto original = Reserve(budget, rig.generation);
    Expect(gpu.CaptureOwner(rig.device.get(), rig.immediate.get(), rig.generation, original) == S_OK,
           "transfer.capture-original-reservation");
    Expect(Create(gpu, std::move(raster)) == S_OK && gpu.ResourceCallsOwner() == 18,
           "transfer.worker-initializes-nine-r8-textures-and-views");
    Expect(weakRaster.expired() && !weakSource.expired() && !weakOwner.expired(),
           "transfer.mask-data-dies-on-creator-original-source-owner-stays");
    weakRaster.reset();
    Expect(!gpu.BorrowOwner(rig.device.get(), rig.immediate.get(), rig.generation, identity, grid, 0),
           "transfer.unpublished-views-unavailable");
    for (unsigned int field = 0; field < 6; ++field)
    {
        auto changed = grid;
        switch (field)
        {
        case 0:
            ++changed.width;
            break;
        case 1:
            ++changed.height;
            break;
        case 2:
            changed.documentScalePixel += 0.25;
            break;
        case 3:
            changed.originPixelX += 0.25;
            break;
        case 4:
            changed.originPixelY += 0.25;
            break;
        case 5:
            changed.dpi += 1;
            break;
        }
        Indexed(gpu.PublishOwner(rig.device.get(), rig.immediate.get(), rig.generation, identity, changed) == E_ABORT,
                "stale-publish", field, "grid-refusal-keeps-unpublished");
    }
    Expect(gpu.PublishOwner(rig.device.get(), rig.immediate.get(), rig.generation + 1, identity, grid) == E_ABORT,
           "stale-publish.device-generation");
    auto foreign = Source();
    Expect(gpu.PublishOwner(rig.device.get(), rig.immediate.get(), rig.generation, foreign.Identity(), grid) == E_ABORT,
           "stale-publish.source-identity");
    Expect(gpu.PublishOwner(rig.device.get(), rig.immediate.get(), rig.generation, identity, grid) == S_OK,
           "transfer.exact-current-publication");
    bool offOwner = false;
    std::thread probe(
        [&]() noexcept
        {
            offOwner = gpu.PublishOwner(rig.device.get(), rig.immediate.get(), rig.generation, identity, grid) ==
                           E_ACCESSDENIED &&
                       !gpu.BorrowOwner(rig.device.get(), rig.immediate.get(), rig.generation, identity, grid, 0);
        });
    probe.join();
    Expect(offOwner, "transfer.foreign-thread-cannot-publish-or-borrow");
    for (unsigned int field = 0; field < 6; ++field)
    {
        auto changed = grid;
        switch (field)
        {
        case 0:
            ++changed.width;
            break;
        case 1:
            ++changed.height;
            break;
        case 2:
            changed.documentScalePixel += 0.25;
            break;
        case 3:
            changed.originPixelX += 0.25;
            break;
        case 4:
            changed.originPixelY += 0.25;
            break;
        case 5:
            changed.dpi += 1;
            break;
        }
        Indexed(!gpu.BorrowOwner(rig.device.get(), rig.immediate.get(), rig.generation, identity, changed, 0),
                "stale-borrow", field, "grid-refusal");
    }
    Expect(!gpu.BorrowOwner(nullptr, rig.immediate.get(), rig.generation, identity, grid, 0), "stale-borrow.device");
    Expect(!gpu.BorrowOwner(rig.device.get(), nullptr, rig.generation, identity, grid, 0), "stale-borrow.context");
    Expect(!gpu.BorrowOwner(rig.device.get(), rig.immediate.get(), rig.generation + 1, identity, grid, 0),
           "stale-borrow.generation");
    Expect(!gpu.BorrowOwner(rig.device.get(), rig.immediate.get(), rig.generation, foreign.Identity(), grid, 0),
           "stale-borrow.source");
    Expect(!gpu.BorrowOwner(rig.device.get(), rig.immediate.get(), rig.generation, identity, grid, 8),
           "stale-borrow.phase");
    std::array<wil::com_ptr_nothrow<ID3D11Texture2D>, 9> staging;
    D3D11_TEXTURE2D_DESC description{};
    description.Width = 16;
    description.Height = 16;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    bool copied = true;
    for (unsigned int plane = 0; plane < 9; ++plane)
    {
        const auto views = gpu.BorrowOwner(rig.device.get(), rig.immediate.get(), rig.generation, identity, grid,
                                           plane == 0 ? 0u : plane - 1);
        const bool complete = bool(views) && views.width == 16 && views.height == 16;
        Indexed(complete, "transfer", plane, "exact-live-plane-dimensions");
        const auto created = rig.device->CreateTexture2D(&description, nullptr, staging[plane].put());
        Indexed(created == S_OK, "transfer", plane, "real-r8-staging-created");
        if (!complete || FAILED(created))
        {
            copied = false;
            continue;
        }
        wil::com_ptr_nothrow<ID3D11Resource> resource;
        (plane == 0 ? views.solid : views.dashes)->GetResource(resource.put());
        rig.immediate->CopyResource(staging[plane].get(), resource.get());
    }
    Completion completion;
    const bool acknowledged = FinishGpu(rig, completion);
    Expect(acknowledged, "transfer.original-gpu-completion-before-map-and-retirement");
    if (!acknowledged)
        std::terminate(); // Never free resources after an unacknowledged test draw/copy.
    for (unsigned int plane = 0; plane < 9; ++plane)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto result = staging[plane] ? rig.immediate->Map(staging[plane].get(), 0, D3D11_MAP_READ,
                                                                D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)
                                           : E_FAIL;
        bool equal = copied && result == S_OK && mapped.pData && mapped.RowPitch >= 16;
        if (result == S_OK)
        {
            if (equal)
                for (unsigned int row = 0; row < 16; ++row)
                    equal = equal && std::memcmp(static_cast<const std::uint8_t*>(mapped.pData) + row * mapped.RowPitch,
                                                 expected[plane].data() + row * 16, 16) == 0;
            rig.immediate->Unmap(staging[plane].get(), 0);
        }
        Indexed(equal, "transfer", plane, "byte-exact-to-prepared-cpu-plane");
    }
    Expect(gpu.DetachOwner() &&
               !gpu.BorrowOwner(rig.device.get(), rig.immediate.get(), rig.generation, identity, grid, 0),
           "retirement.detach-excludes-future-frame-borrows");
    Expect(!gpu.CompleteRetirementOwner(), "retirement.refund-refused-before-worker-join");
    Expect(Retire(gpu, true) && weakSource.expired() && !weakOwner.expired() && budget.ReservedOwner() == gpuBytes,
           "retirement.source-data-dies-on-worker-weak-control-keeps-old-owner");
    weakSource.reset();
    Expect(weakOwner.expired(), "retirement.original-source-owner-dies-after-last-weak-control");
    Expect(gpu.CompleteRetirementOwner() && budget.ReservedOwner() == 0,
           "retirement.refund-after-actual-gpu-and-worker-acknowledgements");
}
} // namespace

int RunSelectionOutlineResidencyTests()
{
    passed = failed = 0;
    TestPlan();
    Rig rig;
    Expect(rig.Initialize(), "fixture.real-warp-device-queue-and-scheduler");
    if (!rig.generation)
        return 1;
    D3D11_FEATURE_DATA_THREADING threading{};
    const auto query = rig.device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    Expect(query == S_OK && threading.DriverConcurrentCreates, "fixture.actual-concurrent-creation-support-required");
    if (query != S_OK || !threading.DriverConcurrentCreates)
        return 1; // Positive route is unqualified on this host.
    auto source = Source();
    const auto raster = RasterFor(source);
    if (!raster.HasStorage())
        return 1;
    TestRefusals(rig, raster);
    TestPrefixes(rig, raster);
    TestTransfer(rig);
    std::printf("%s selection-outline-residency.summary.%u-passed.%u-failed\n", failed ? "FAIL" : "PASS", passed,
                failed);
    return static_cast<int>(failed);
}
