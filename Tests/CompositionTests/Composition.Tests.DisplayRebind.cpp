#include "Composition.Tests.DisplayRebind.h"

#include "CommittedDisplayController.h"
#include "DocumentDraft.h"
#include "GpuCommandPump.h"
#include "GpuWork.h"
#include "Jobs.h"
#include "OwnerWorkMultiplexer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>
#include <new>
#include <objbase.h>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kDeadlockBoundMs = 10000;
unsigned int gFailures = 0;

void Expect(const char* name, bool condition) noexcept
{
    std::printf("%s display-rebind.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    if (!condition)
        ++gFailures;
}

constexpr std::array<std::uint8_t, 16> kFirstPixels{
    0x11, 0x22, 0x33, 0xFF, 0x44, 0x55, 0x66, 0xFF, 0x77, 0x88, 0x99, 0xFF, 0xAA, 0xBB, 0xCC, 0xFF,
};
constexpr std::array<std::uint8_t, 16> kSecondPixels{
    0xD1, 0xB2, 0x93, 0xFF, 0x74, 0x55, 0x36, 0xFF, 0x17, 0xE8, 0xC9, 0xFF, 0xAA, 0x8B, 0x6C, 0xFF,
};

class Client final : public CommittedDisplayClient
{
  public:
    explicit Client(GpuWorkQueue& queue) noexcept : queue(queue) {}

    DocumentSession session;
    GpuWorkQueue& queue;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    RecordedDisplayGraph* graph = nullptr;
    unsigned int publications = 0;
    unsigned int callbacks = 0;
    unsigned int errors = 0;
    HRESULT error = S_OK;

    bool DescribeCommittedDisplayOwner(CommittedDisplayDescriptor& output) noexcept override
    {
        output = {};
        const auto authority = session.CaptureDocumentReadAuthority();
        const auto& document = std::as_const(session).Document();
        if (!authority || !document || !device || !immediate)
            return false;
        output = {{authority.value(), queue.DeviceGeneration()}, &document.value(), device, immediate, true};
        return true;
    }

    void CommittedDisplayPublishedOwner(RecordedDisplayGraph* replacement) noexcept override
    {
        ++callbacks;
        graph = replacement;
        if (replacement)
            ++publications;
    }

    void CommittedDisplayFailedOwner(CommittedDisplayKey, HRESULT status) noexcept override
    {
        ++callbacks;
        ++errors;
        error = status;
    }

    bool InstallFixture(const std::array<std::uint8_t, 16>& pixels) noexcept
    {
        try
        {
            auto image = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
            if (!image.HasStorage())
                return false;
            CanvasDocument document;
            document.width = 2;
            document.height = 2;
            auto draft = CanvasDocumentDraft::Rebuild(document, 1);
            if (!draft)
                return false;
            auto layer = ImageLayer::Blank(L"Display rebind fixture", Vec2{2, 2});
            const auto active = layer.id;
            layer.asset = {std::move(image), {}, L"Display rebind fixture"};
            layer.transform.sampling = LayerSampling::Nearest;
            if (!draft->Append(std::move(layer)))
                return false;
            auto sealed = draft->Seal();
            if (!sealed)
                return false;
            session.ReplaceDocument(std::move(*sealed), active);
            return session.CaptureDocumentReadAuthority().has_value();
        }
        catch (const std::bad_alloc&)
        {
            return false; // Explicit test-fixture allocation boundary.
        }
    }
};

struct Rig final
{
    wil::com_ptr_nothrow<ID3D11Device> firstDevice;
    wil::com_ptr_nothrow<ID3D11DeviceContext> firstImmediate;
    wil::com_ptr_nothrow<ID3D11Device> secondDevice;
    wil::com_ptr_nothrow<ID3D11DeviceContext> secondImmediate;
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    Client client;
    CommittedDisplayController display;
    const DWORD ownerThread = GetCurrentThreadId();
    bool initialized = false;
    bool closed = false;
    std::atomic<bool> finalWorkerWasMta{false};

    Rig() noexcept : client(queue) {}

    bool Initialize() noexcept
    {
        constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        HRESULT result =
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                              D3D11_SDK_VERSION, firstDevice.put(), nullptr, firstImmediate.put());
        if (FAILED(result))
            return false;
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                   D3D11_SDK_VERSION, secondDevice.put(), nullptr, secondImmediate.put());
        if (FAILED(result))
            return false;
        client.device = firstDevice.get();
        client.immediate = firstImmediate.get();
        if (FAILED(jobs.Initialize(2)) || FAILED(jobs.EnableGpuRecordingLane()) || !jobs.EnableDeferredRelease() ||
            !jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) ||
            FAILED(queue.Bind(firstDevice.get(), firstImmediate.get())) ||
            FAILED(commands.InitializeOwner(jobs, queue, routes)) ||
            FAILED(display.InitializeOwner(jobs, queue, commands, routes, client)))
            std::terminate(); // Partial route registration cannot be torn down as a valid fixture.
        initialized = true;
        return true;
    }

    void Close() noexcept
    {
        if (!initialized || closed)
            return;
        display.StopOwner();
        queue.StopNotifications();
        jobs.Close({[](void* opaque) noexcept
                    {
                        auto& rig = *static_cast<Rig*>(opaque);
                        rig.display.JoinedShutdownOwner();
                        rig.queue.CloseAfterWorkers();
                    },
                    [](void* opaque) noexcept
                    {
                        auto& rig = *static_cast<Rig*>(opaque);
                        rig.display.RetireShutdownForWork();
                        APTTYPE apartment = APTTYPE_CURRENT;
                        APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
                        const HRESULT queried = CoGetApartmentType(&apartment, &qualifier);
                        rig.finalWorkerWasMta.store(SUCCEEDED(queried) && apartment == APTTYPE_MTA &&
                                                        GetCurrentThreadId() != rig.ownerThread,
                                                    std::memory_order_release);
                    },
                    this});
        closed = true;
        Expect("production-close-detaches-display-and-command-routes", display.CloseAfterWorkersOwner());
        Expect("production-close-returns-complete-cpu-charge",
               jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0);
        Expect("production-close-final-retirement-hook-runs-on-mta-worker",
               finalWorkerWasMta.load(std::memory_order_acquire));
    }

    ~Rig()
    {
        Close();
    }
};

template <typename Predicate> bool Settle(Rig& rig, Predicate predicate) noexcept
{
    const ULONGLONG started = GetTickCount64();
    for (;;)
    {
        (void)DrainCompletions(rig.jobs);
        if (predicate())
            return true;
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= kDeadlockBoundMs)
            return false;
        if (rig.jobs.HasUiWork() || rig.routes.HasReady())
            continue;
        const DWORD remaining = static_cast<DWORD>(kDeadlockBoundMs - elapsed);
        if (WaitForSingleObject(static_cast<HANDLE>(rig.jobs.WakeHandle()), remaining) != WAIT_OBJECT_0)
            return false;
    }
}

struct WarpBudgetPolicy final
{
    bool known = false;
    HRESULT refusal = E_OUTOFMEMORY;
};

WarpBudgetPolicy QueryWarpBudgetPolicy(ID3D11Device* device) noexcept
{
    GpuCapabilities::VideoMemorySnapshot observed;
    const HRESULT query = GpuCapabilities::QueryVideoMemorySnapshot(device, observed);
    const auto& segment = observed.nonLocal.available && observed.nonLocal.budget ? observed.nonLocal : observed.local;
    std::printf("INFO display-rebind.budget query=0x%08X available=%u budget=%llu usage=%llu\n",
                static_cast<unsigned int>(query), segment.available ? 1u : 0u,
                static_cast<unsigned long long>(segment.budget), static_cast<unsigned long long>(segment.currentUsage));
    std::fflush(stdout);
    const bool known = SUCCEEDED(query) && segment.available && segment.budget != 0;
    const HRESULT refusal = FAILED(query) ? query
                            : !segment.available || !segment.budget
                                ? (FAILED(segment.queryResult) ? segment.queryResult : E_OUTOFMEMORY)
                                : E_OUTOFMEMORY;
    return {known, refusal};
}

bool ReadbackEquals(RecordedDisplayGraph& graph, const std::array<std::uint8_t, 16>& expected) noexcept
{
    auto* composition = graph.CompositionForTestOwner();
    if (!composition)
        return false;
    try
    {
        std::vector<std::uint8_t> actual;
        UINT width = 0;
        UINT height = 0;
        return SUCCEEDED(composition->Readback(actual, width, height)) && width == 2 && height == 2 &&
               actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin());
    }
    catch (const std::bad_alloc&)
    {
        return false; // Explicit readback-test allocation boundary.
    }
}

bool ExpectExactBudgetRefusal(Rig& rig, HRESULT expected, unsigned int expectedErrors,
                              unsigned int publicationsBeforeRequest) noexcept
{
    const bool refused = Settle(rig, [&]() noexcept { return rig.client.errors == expectedErrors; }) &&
                         rig.client.errors == expectedErrors && rig.client.error == expected && !rig.client.graph &&
                         rig.client.publications == publicationsBeforeRequest;
    Expect("unknown-budget-refusal-matches-query-and-publishes-no-new-root", refused);
    const bool retired = Settle(rig,
                                [&]() noexcept
                                {
                                    return !rig.display.HasPending() && rig.jobs.ReservedMemoryBytes() == 0 &&
                                           rig.jobs.ReleasingMemoryBytes() == 0;
                                });
    Expect("budget-refusal-retires-through-original-worker-ack", retired);
    return refused && retired;
}

void RunDeviceRebindScenario()
{
    Rig rig;
    Expect("two-independent-actual-warp-devices-created", rig.Initialize());
    if (!rig.initialized)
        return;
    Expect("warp-device-identities-are-distinct", rig.firstDevice.get() != rig.secondDevice.get());
    if (rig.firstDevice.get() == rig.secondDevice.get())
    {
        rig.Close();
        return;
    }

    const bool firstFixture = rig.client.InstallFixture(kFirstPixels);
    Expect("first-literal-document-installed", firstFixture);
    if (!firstFixture)
    {
        rig.Close();
        return;
    }
    const WarpBudgetPolicy firstBudget = QueryWarpBudgetPolicy(rig.firstDevice.get());
    if (!firstBudget.known)
    {
        const unsigned int publicationsBeforeRequest = rig.client.publications;
        rig.display.RequestOwner();
        (void)ExpectExactBudgetRefusal(rig, firstBudget.refusal, 1, publicationsBeforeRequest);
        std::printf("INFO display-rebind.no-captured-graph-proof=first-device-budget-unknown\n");
        rig.Close();
        Expect("unknown-first-budget-close-does-not-count-a-published-root",
               rig.client.graph == nullptr && rig.client.publications == publicationsBeforeRequest);
        return;
    }

    rig.display.RequestOwner();
    const bool firstSettled =
        Settle(rig, [&]() noexcept { return rig.client.publications == 1 || rig.client.errors != 0; });
    Expect("known-first-device-budget-publishes-old-source",
           firstSettled && rig.client.errors == 0 && rig.client.publications == 1 && rig.client.graph &&
               rig.client.graph->IsPublishedOnOwner(rig.firstDevice.get(), rig.firstImmediate.get()));
    if (!firstSettled || rig.client.errors || !rig.client.graph)
    {
        rig.Close();
        return;
    }
    auto* oldGraph = rig.client.graph;
    const CommittedDisplayKey oldKey{rig.client.session.CaptureDocumentReadAuthority().value(),
                                     rig.queue.DeviceGeneration()};
    Expect("old-source-four-nonuniform-bgra-values-read-through-existing-graph",
           ReadbackEquals(*oldGraph, kFirstPixels));
    const std::uint64_t oldCharge = rig.jobs.ReservedMemoryBytes();
    Expect("published-old-source-retains-complete-nonzero-cpu-charge",
           oldCharge != 0 && rig.jobs.ReleasingMemoryBytes() == 0 && !rig.display.HasPending());

    // These fixture-owned COM references remain alive through original-use retirement and production close.
    const auto oldDeviceIdentity = rig.firstDevice.get();
    const auto oldContextIdentity = rig.firstImmediate.get();
    const bool secondFixture = rig.client.InstallFixture(kSecondPixels);
    Expect("second-literal-document-installed", secondFixture);
    if (!secondFixture)
    {
        rig.Close();
        return;
    }
    const WarpBudgetPolicy secondBudget = QueryWarpBudgetPolicy(rig.secondDevice.get());
    const HRESULT rebound = rig.queue.Bind(rig.secondDevice.get(), rig.secondImmediate.get());
    Expect("queue-successfully-binds-the-second-actual-device", SUCCEEDED(rebound));
    if (FAILED(rebound))
    {
        rig.Close();
        return;
    }
    rig.client.device = rig.secondDevice.get();
    rig.client.immediate = rig.secondImmediate.get();
    CommittedDisplayDescriptor current;
    const bool described = rig.client.DescribeCommittedDisplayOwner(current);
    Expect("descriptor-uses-second-device-context-and-new-queue-generation",
           described && current.device == rig.secondDevice.get() && current.immediate == rig.secondImmediate.get() &&
               current.key.deviceGeneration != oldKey.deviceGeneration &&
               current.key.document.documentSerial != oldKey.document.documentSerial);

    const unsigned int callbacksBeforeChange = rig.client.callbacks;
    rig.display.DeviceChangedOwner();
    Expect("device-change-immediately-excludes-old-published-pointer",
           rig.client.graph == nullptr && rig.client.callbacks == callbacksBeforeChange + 1);
    Expect("old-source-cpu-charge-survives-synchronous-device-change",
           rig.jobs.ReservedMemoryBytes() + rig.jobs.ReleasingMemoryBytes() >= oldCharge);

    // Suspend before the first owner drain after rebind. This gates new admission while the old graph completes
    // its actual original-context retirement barrier and worker destruction.
    rig.display.SetSuspendedOwner(true);
    Expect("rebind-retirement-gate-keeps-old-source-excluded", rig.client.graph == nullptr);
    const bool oldRetired = Settle(rig,
                                   [&]() noexcept
                                   {
                                       return !rig.display.HasPending() && rig.jobs.ReservedMemoryBytes() == 0 &&
                                              rig.jobs.ReleasingMemoryBytes() == 0;
                                   });
    Expect("old-original-use-and-worker-charge-retire-before-new-admission", oldRetired);
    if (!oldRetired)
    {
        rig.Close();
        return;
    }
    if (!secondBudget.known)
    {
        const unsigned int publicationsBeforeRequest = rig.client.publications;
        rig.display.SetSuspendedOwner(false);
        (void)ExpectExactBudgetRefusal(rig, secondBudget.refusal, 1, publicationsBeforeRequest);
        Expect("old-actual-device-and-context-references-were-retained-through-retirement",
               rig.firstDevice.get() == oldDeviceIdentity && rig.firstImmediate.get() == oldContextIdentity);
        std::printf("INFO display-rebind.no-captured-graph-proof=second-device-budget-unknown\n");
        rig.Close();
        Expect("unknown-second-budget-close-does-not-count-a-published-root",
               rig.client.graph == nullptr && rig.client.publications == publicationsBeforeRequest);
        return;
    }

    rig.display.SetSuspendedOwner(false);
    const ULONGLONG started = GetTickCount64();
    bool settled = false;
    while (!settled)
    {
        (void)DrainCompletions(rig.jobs);
        if (rig.client.publications == 2 && rig.client.errors == 0 && rig.client.graph && !rig.display.HasPending())
        {
            settled = true;
            break;
        }
        if (rig.client.errors != 0)
            break;
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= kDeadlockBoundMs)
            break; // Check the deadlock bound before considering another readiness pass.
        if (rig.jobs.HasUiWork() || rig.routes.HasReady())
            continue;
        const DWORD remaining = static_cast<DWORD>(kDeadlockBoundMs - elapsed);
        if (WaitForSingleObject(static_cast<HANDLE>(rig.jobs.WakeHandle()), remaining) != WAIT_OBJECT_0)
            break;
    }
    Expect("new-source-publication-and-original-retirement-settle-on-real-events", settled);
    if (settled && rig.client.graph)
    {
        Expect("new-source-is-published-on-the-second-actual-context",
               rig.client.graph->IsPublishedOnOwner(rig.secondDevice.get(), rig.secondImmediate.get()) &&
                   rig.client.graph->DeviceForTestOwner() == rig.secondDevice.get() &&
                   rig.client.graph->ContextForTestOwner() == rig.secondImmediate.get());
        Expect("old-graph-resources-are-not-reused-on-second-device",
               rig.client.graph->DeviceForTestOwner() != oldDeviceIdentity &&
                   rig.client.graph->ContextForTestOwner() != oldContextIdentity);
        Expect("second-source-four-nonuniform-bgra-values-read-through-existing-graph",
               ReadbackEquals(*rig.client.graph, kSecondPixels));
        Expect("original-use-retirement-and-worker-ack-leave-new-cache-charged",
               !rig.display.HasPending() && rig.jobs.ReservedMemoryBytes() != 0 &&
                   rig.jobs.ReleasingMemoryBytes() == 0 &&
                   rig.client.graph->IsPublishedOnOwner(rig.secondDevice.get(), rig.secondImmediate.get()));
    }
    Expect("old-actual-device-and-context-references-remained-live-through-rebind",
           rig.firstDevice.get() == oldDeviceIdentity && rig.firstImmediate.get() == oldContextIdentity);
    rig.Close();
}

} // namespace

int RunCommittedDisplayDeviceRebindTests()
{
    gFailures = 0;
    RunDeviceRebindScenario();
    return static_cast<int>(gFailures);
}
