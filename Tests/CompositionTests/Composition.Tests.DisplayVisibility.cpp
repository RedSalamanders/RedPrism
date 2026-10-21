#include "Composition.Tests.DisplayVisibility.h"

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
#include <memory>
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
    std::printf("%s display-visibility.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    if (!condition)
        ++gFailures;
}

constexpr std::array<std::uint8_t, 16> kFirstPixels{
    0x11, 0x22, 0x33, 0xFF, 0x44, 0x55, 0x66, 0xFF, 0x77, 0x88, 0x99, 0xFF, 0xAA, 0xBB, 0xCC, 0xFF,
};
constexpr std::array<std::uint8_t, 16> kLatestPixels{
    0xC1, 0xB2, 0xA3, 0xFF, 0x94, 0x85, 0x76, 0xFF, 0x67, 0x58, 0x49, 0xFF, 0x3A, 0x2B, 0x1C, 0xFF,
};

class Client final : public CommittedDisplayClient
{
  public:
    Client(GpuWorkQueue& queue, ID3D11Device* device, ID3D11DeviceContext* immediate) noexcept
        : queue(queue), device(device), immediate(immediate)
    {
    }

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

    bool InstallFixture(const std::array<std::uint8_t, 16>& pixels)
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
        auto layer = ImageLayer::Blank(L"Display visibility fixture", Vec2{2, 2});
        const auto active = layer.id;
        layer.asset = {std::move(image), {}, L"Display visibility fixture"};
        layer.transform.sampling = LayerSampling::Nearest;
        if (!draft->Append(std::move(layer)))
            return false;
        auto sealed = draft->Seal();
        if (!sealed)
            return false;
        session.ReplaceDocument(std::move(*sealed), active);
        return session.CaptureDocumentReadAuthority().has_value();
    }
};

struct Rig final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
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

    Rig() noexcept : client(queue, nullptr, nullptr) {}

    bool Initialize(unsigned int workers) noexcept
    {
        constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                           &level, 1, D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
        if (FAILED(result))
            return false;
        client.device = device.get();
        client.immediate = immediate.get();
        if (FAILED(jobs.Initialize(workers)) || FAILED(jobs.EnableGpuRecordingLane()) ||
            !jobs.EnableDeferredRelease() || !jobs.RegisterOwnerWork(routes) ||
            FAILED(queue.Initialize(jobs, &routes)) || FAILED(queue.Bind(device.get(), immediate.get())) ||
            FAILED(commands.InitializeOwner(jobs, queue, routes)) ||
            FAILED(display.InitializeOwner(jobs, queue, commands, routes, client)))
            std::terminate(); // A partially attached route cannot be destroyed as a successful test fixture.
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
        Expect("close-detaches-display-and-command-routes", display.CloseAfterWorkersOwner());
        Expect("close-final-charge-is-zero-on-owner",
               jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0);
    }

    ~Rig()
    {
        Close();
    }
};

class WorkerGate final : public Job
{
  public:
    WorkerGate(HANDLE entered, HANDLE release) noexcept : _entered(entered), _release(release) {}

    JobResult Run(JobContext& context) override
    {
        if (!SetEvent(_entered))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE waits[]{_release, static_cast<HANDLE>(context.shutdownEvent)};
        const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0)
            return {};
        if (result == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
    }

    void Completed(JobResult) noexcept override {}

  private:
    HANDLE _entered;
    HANDLE _release;
};

bool WaitOwnerEvent(HANDLE event) noexcept
{
    return event && WaitForSingleObject(event, kDeadlockBoundMs) == WAIT_OBJECT_0;
}

template <typename Predicate> bool Settle(Rig& rig, Predicate predicate) noexcept
{
    const ULONGLONG started = GetTickCount64();
    while (!predicate())
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
    return true;
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
    std::printf("INFO display-visibility.warp-budget query=0x%08X available=%u budget=%llu usage=%llu\n",
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
    std::vector<std::uint8_t> actual;
    UINT width = 0;
    UINT height = 0;
    return SUCCEEDED(composition->Readback(actual, width, height)) && width == 2 && height == 2 &&
           actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin());
}

bool PublishRequired(Rig& rig, const char* publishedName)
{
    rig.display.RequestOwner();
    const bool settled = Settle(rig, [&]() noexcept { return rig.client.publications != 0 || rig.client.errors != 0; });
    Expect("initial-source-settles-on-real-events", settled);
    if (!settled)
        return false;
    if (rig.client.graph)
    {
        const bool published =
            rig.client.errors == 0 && rig.client.graph->IsPublishedOnOwner(rig.device.get(), rig.immediate.get());
        Expect(publishedName, published);
        return published;
    }
    Expect("known-warp-budget-must-publish-or-fail-the-test", false);
    return false;
}

bool ExpectBudgetRefusal(Rig& rig, HRESULT expected) noexcept
{
    rig.display.RequestOwner();
    const bool completed = Settle(rig, [&]() noexcept { return rig.client.errors != 0; });
    const bool refused = completed && rig.client.errors == 1 && rig.client.error == expected && !rig.client.graph &&
                         rig.client.publications == 0;
    Expect("unknown-warp-budget-refuses-with-exact-query-policy", refused);
    Expect("unknown-warp-budget-refund-follows-original-retirement",
           Settle(rig,
                  [&]() noexcept
                  {
                      return !rig.display.HasPending() && rig.jobs.ReservedMemoryBytes() == 0 &&
                             rig.jobs.ReleasingMemoryBytes() == 0;
                  }));
    return refused;
}

void RunPublishedVisibilityScenario()
{
    Rig rig;
    Expect("warp-rig-initializes", rig.Initialize(2));
    if (!rig.initialized)
        return;
    Expect("actual-first-literal-document-installed", rig.client.InstallFixture(kFirstPixels));
    const WarpBudgetPolicy policy = QueryWarpBudgetPolicy(rig.device.get());
    if (!policy.known)
    {
        (void)ExpectBudgetRefusal(rig, policy.refusal);
        rig.Close();
        return;
    }
    if (!PublishRequired(rig, "known-warp-budget-publishes-complete-source"))
    {
        rig.Close();
        return;
    }

    auto* first = rig.client.graph;
    Expect("first-published-source-readback-matches-four-literals", first && ReadbackEquals(*first, kFirstPixels));
    const auto firstCharge = rig.jobs.ReservedMemoryBytes();
    Expect("published-source-retains-whole-nonzero-charge", firstCharge != 0 && !rig.display.HasPending());

    rig.display.SetSuspendedOwner(true);
    Expect("suspend-clears-published-pointer-before-drain", rig.client.graph == nullptr);
    Expect("suspend-keeps-original-charge-until-completion", rig.jobs.ReservedMemoryBytes() == firstCharge);
    Expect("suspend-last-use-and-worker-retirement-settle", Settle(rig,
                                                                   [&]() noexcept
                                                                   {
                                                                       return !rig.display.HasPending() &&
                                                                              rig.jobs.ReservedMemoryBytes() == 0 &&
                                                                              rig.jobs.ReleasingMemoryBytes() == 0;
                                                                   }));
    const auto callbacksAtSuspend = rig.client.callbacks;
    for (unsigned int i = 0; i < 8; ++i)
    {
        rig.display.RequestOwner();
        rig.display.SetSuspendedOwner(true);
    }
    (void)DrainCompletions(rig.jobs);
    Expect("repeated-suspended-requests-admit-no-work-or-ready-loop",
           !rig.display.HasPending() && !rig.display.HasReady() && rig.client.callbacks == callbacksAtSuspend &&
               rig.jobs.ReservedMemoryBytes() == 0);

    Expect("suspended-document-replaced-with-latest-literal", rig.client.InstallFixture(kLatestPixels));
    rig.display.RequestOwner();
    (void)DrainCompletions(rig.jobs);
    Expect("suspended-replacement-is-not-published-or-charged",
           rig.client.graph == nullptr && rig.client.publications == 1 && !rig.display.HasPending() &&
               !rig.display.HasReady() && rig.jobs.ReservedMemoryBytes() == 0);

    rig.display.SetSuspendedOwner(false);
    Expect("resume-publishes-the-latest-document-on-real-events",
           Settle(rig, [&]() noexcept { return rig.client.publications == 2 || rig.client.errors != 0; }) &&
               rig.client.errors == 0 && rig.client.publications == 2 && rig.client.graph &&
               rig.client.graph->IsPublishedOnOwner(rig.device.get(), rig.immediate.get()));
    Expect("resumed-latest-source-readback-matches-four-literals",
           rig.client.graph && ReadbackEquals(*rig.client.graph, kLatestPixels));
    Expect("resumed-cache-retains-its-complete-charge",
           rig.jobs.ReservedMemoryBytes() != 0 && rig.jobs.ReleasingMemoryBytes() == 0);
    rig.display.SetSuspendedOwner(true);
    Expect("second-suspend-clears-latest-pointer-immediately", rig.client.graph == nullptr);
    Expect("latest-last-use-ack-precedes-worker-refund", Settle(rig,
                                                                [&]() noexcept
                                                                {
                                                                    return !rig.display.HasPending() &&
                                                                           rig.jobs.ReservedMemoryBytes() == 0 &&
                                                                           rig.jobs.ReleasingMemoryBytes() == 0;
                                                                }));
    rig.Close();
}

bool StartGate(Rig& rig, wil::unique_handle& entered, wil::unique_handle& release) noexcept
{
    entered.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!entered || !release)
        return false;
    std::unique_ptr<Job> gate{new (std::nothrow) WorkerGate(entered.get(), release.get())};
    if (!gate)
        return false;
    JobTicket ticket;
    const HRESULT submitted = rig.jobs.Submit(gate, {.kind = JobClass::Background, .memoryBytes = 4096}, ticket);
    return SUCCEEDED(submitted) && !gate && WaitOwnerEvent(entered.get());
}

void RunSuspendedOpeningScenario()
{
    Rig rig;
    Expect("opening-rig-initializes", rig.Initialize(1));
    if (!rig.initialized)
        return;
    wil::unique_handle entered;
    wil::unique_handle release;
    Expect("compute-worker-is-held-by-test-owned-event", StartGate(rig, entered, release));
    if (!entered || !release || WaitForSingleObject(entered.get(), 0) != WAIT_OBJECT_0)
    {
        (void)SetEvent(release.get());
        rig.Close();
        return;
    }
    Expect("actual-opening-document-installed", rig.client.InstallFixture(kFirstPixels));
    const auto gateCharge = rig.jobs.ReservedMemoryBytes();
    rig.display.RequestOwner();
    Expect("original-opening-reaches-admission-behind-held-worker",
           Settle(rig,
                  [&]() noexcept { return rig.display.HasPending() && rig.jobs.ReservedMemoryBytes() > gateCharge; }));
    const auto admittedCharge = rig.jobs.ReservedMemoryBytes();
    Expect("opening-is-admitted-and-blocked-before-any-apply",
           admittedCharge != 0 && rig.display.HasPending() && rig.client.callbacks == 0 &&
               rig.client.publications == 0 && rig.client.graph == nullptr);
    rig.display.SetSuspendedOwner(true);
    Expect("opening-suspension-publishes-no-pixels", rig.client.graph == nullptr && rig.client.publications == 0);
    Expect("opening-charge-remains-until-original-cancel-ack", rig.jobs.ReservedMemoryBytes() == admittedCharge);
    (void)SetEvent(release.get());
    Expect("suspended-opening-cancels-and-refunds-on-original-worker-ack",
           Settle(rig,
                  [&]() noexcept
                  {
                      return !rig.display.HasPending() && rig.jobs.ReservedMemoryBytes() == 0 &&
                             rig.jobs.ReleasingMemoryBytes() == 0;
                  }) &&
               rig.client.graph == nullptr && rig.client.publications == 0 && rig.client.errors == 0);
    rig.display.RequestOwner();
    (void)DrainCompletions(rig.jobs);
    Expect("repeated-request-after-cancel-stays-dormant-while-suspended",
           !rig.display.HasPending() && !rig.display.HasReady() && rig.jobs.ReservedMemoryBytes() == 0);
    rig.Close();
}

void RunCloseWhileSuspendedScenario()
{
    Rig rig;
    Expect("close-rig-initializes", rig.Initialize(2));
    if (!rig.initialized)
        return;
    Expect("close-original-document-installed", rig.client.InstallFixture(kFirstPixels));
    const WarpBudgetPolicy policy = QueryWarpBudgetPolicy(rig.device.get());
    const bool hasPublishedRoot = policy.known && PublishRequired(rig, "close-source-publishes-actual-warp-graph");
    if (policy.known && !hasPublishedRoot)
    {
        rig.Close();
        return;
    }
    if (!policy.known && !ExpectBudgetRefusal(rig, policy.refusal))
    {
        rig.Close();
        return;
    }

    constexpr std::size_t gateCount = 2;
    std::array<wil::unique_handle, gateCount> entered;
    std::array<wil::unique_handle, gateCount> release;
    bool gatesStarted = true;
    for (std::size_t i = 0; i < gateCount; ++i)
    {
        gatesStarted = StartGate(rig, entered[i], release[i]) && gatesStarted;
    }
    Expect("close-compute-workers-are-held-by-test-owned-events", gatesStarted);
    if (!gatesStarted)
    {
        for (auto& event : release)
            if (event)
                (void)SetEvent(event.get());
        rig.Close();
        return;
    }

    const auto chargeBeforeReplacement = rig.jobs.ReservedMemoryBytes();
    // Keep the existing complete cache resident while the blocked worker admits this same-document edit.
    // Replacing the document would retire that cache concurrently, so aggregate charge growth would not
    // distinguish admission from the old root's legitimate refund.
    const auto beforeEdit = rig.client.session.Generation();
    rig.client.session.SetOpacity(0.5);
    Expect("close-replacement-document-installed", rig.client.session.Generation() != beforeEdit);
    rig.display.RequestOwner();
    Expect("close-replacement-reaches-original-admission-behind-held-workers",
           Settle(rig, [&]() noexcept { return rig.jobs.ReservedMemoryBytes() > chargeBeforeReplacement; }));
    const auto admittedCharge = rig.jobs.ReservedMemoryBytes();
    Expect("close-has-real-pending-opening-charge",
           admittedCharge > chargeBeforeReplacement && rig.display.HasPending());
    rig.display.SetSuspendedOwner(true);
    Expect("close-suspension-excludes-pixels-immediately", rig.client.graph == nullptr);
    Expect("close-pending-charge-survives-until-joined-shutdown", rig.jobs.ReservedMemoryBytes() == admittedCharge);

    const auto callbacksBeforeStop = rig.client.callbacks;
    const auto errorsBeforeStop = rig.client.errors;
    rig.Close(); // Exact production close ordering is in Rig::Close; shutdown wakes the held worker.
    Expect("close-does-not-publish-or-fail-pending-pixels",
           rig.client.graph == nullptr && rig.client.publications == (hasPublishedRoot ? 1u : 0u) &&
               rig.client.errors == errorsBeforeStop &&
               rig.client.callbacks == callbacksBeforeStop + 1); // StopOwner clears the borrowed pointer once.
    Expect("final-retirement-hook-runs-on-an-mta-worker", rig.finalWorkerWasMta.load(std::memory_order_acquire));
    Expect("mta-retirement-completes-before-final-charge-zero",
           rig.jobs.ReservedMemoryBytes() == 0 && rig.jobs.ReleasingMemoryBytes() == 0);
}
} // namespace

int RunCommittedDisplayVisibilityTests()
{
    gFailures = 0;
    RunPublishedVisibilityScenario();
    RunSuspendedOpeningScenario();
    RunCloseWhileSuspendedScenario();
    return static_cast<int>(gFailures);
}
