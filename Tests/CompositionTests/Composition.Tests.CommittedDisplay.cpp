#include "CommittedDisplayController.h"
#include "DocumentDraft.h"

#include <array>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace
{
unsigned int failures = 0;
constexpr std::uint64_t kGiB = 1'073'741'824ULL;
constexpr std::uint64_t kAdmissionHeadroom = kMemoryAdmissionHeadroomBytes;

struct MemorySourceState final
{
    MemorySnapshot snapshot{kAdmissionHeadroom, kAdmissionHeadroom};
    unsigned int samples = 0;
};

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    auto& state = *static_cast<MemorySourceState*>(context);
    ++state.samples;
    snapshot = state.snapshot;
    return true;
}

void Expect(const char* name, bool condition) noexcept
{
    std::printf("%s admitted-display.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    if (!condition)
        ++failures;
}

class Client final : public CommittedDisplayClient
{
  public:
    Client(GpuWorkQueue& queue, ID3D11Device* device, ID3D11DeviceContext* context, bool software) noexcept
        : queue(queue), device(device), context(context), software(software)
    {
    }
    DocumentSession session;
    GpuWorkQueue& queue;
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    bool software;
    RecordedDisplayGraph* graph = nullptr;
    unsigned int publications = 0;
    unsigned int errors = 0;
    HRESULT error = S_OK;
    bool DescribeCommittedDisplayOwner(CommittedDisplayDescriptor& output) noexcept override
    {
        const auto authority = session.CaptureDocumentReadAuthority();
        if (!authority)
            return false;
        output = {
            {authority.value(), queue.DeviceGeneration()}, &session.Document().value(), device, context, software};
        return true;
    }
    void CommittedDisplayPublishedOwner(RecordedDisplayGraph* replacement) noexcept override
    {
        graph = replacement;
        if (replacement)
            ++publications;
    }
    void CommittedDisplayFailedOwner(CommittedDisplayKey, HRESULT status) noexcept override
    {
        ++errors;
        error = status;
        std::printf("INFO admitted-display.refusal=0x%08X\n", static_cast<unsigned int>(status));
        std::fflush(stdout);
    }
    bool InstallFixture(std::uint8_t color)
    {
        constexpr int side = 2;
        std::array<std::uint8_t, 16> bytes{};
        for (std::size_t i = 0; i < bytes.size(); i += 4)
        {
            bytes[i] = color;
            bytes[i + 1] = color;
            bytes[i + 2] = color;
            bytes[i + 3] = 255;
        }
        auto image = Raster::Make(side, side, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        CanvasDocument document;
        document.width = side;
        document.height = side;
        auto draft = CanvasDocumentDraft::Rebuild(document, 1);
        if (!draft || !image.HasStorage())
            return false;
        auto layer = ImageLayer::Blank(L"Controller fixture", Vec2{side, side});
        const auto active = layer.id;
        layer.asset = {std::move(image), {}, L"Controller fixture"};
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

template <typename Predicate> bool Until(JobScheduler& jobs, OwnerWorkMultiplexer& routes, Predicate predicate) noexcept
{
    const auto started = GetTickCount64();
    for (;;)
    {
        (void)DrainCompletions(jobs);
        if (predicate())
            return true;
        const auto elapsed = GetTickCount64() - started;
        if (elapsed >= 10000)
            return false;
        if (jobs.HasUiWork() || routes.HasReady())
            continue;
        if (WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()), static_cast<DWORD>(10000 - elapsed)) !=
            WAIT_OBJECT_0)
            return false;
    }
}

bool Run(D3D_DRIVER_TYPE driver, bool optionalHardware = false)
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    constexpr auto level = D3D_FEATURE_LEVEL_11_0;
    const auto made = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                        D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    if (FAILED(made))
    {
        Expect("hardware-device-unavailable-skipped", optionalHardware);
        return optionalHardware;
    }
    GpuCapabilities::VideoMemorySnapshot observed;
    const auto queried = GpuCapabilities::QueryVideoMemorySnapshot(device.get(), observed);
    const auto& segment = driver == D3D_DRIVER_TYPE_WARP && observed.nonLocal.available && observed.nonLocal.budget
                              ? observed.nonLocal
                              : observed.local;
    const bool budgetKnown = SUCCEEDED(queried) && segment.available && segment.budget;
    std::printf("INFO admitted-display.driver=%s query=0x%08X budget=%llu usage=%llu\n",
                driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware", static_cast<unsigned int>(queried),
                static_cast<unsigned long long>(segment.budget), static_cast<unsigned long long>(segment.currentUsage));
    std::fflush(stdout);

    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    CommittedDisplayController display;
    Client client{queue, device.get(), context.get(), driver == D3D_DRIVER_TYPE_WARP};
    MemorySourceState memory;
    if (FAILED(jobs.Initialize(2, {}, {SampleMemory, &memory})) || FAILED(jobs.EnableGpuRecordingLane()) ||
        !jobs.EnableDeferredRelease() || !jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) ||
        FAILED(queue.Bind(device.get(), context.get())) || FAILED(commands.InitializeOwner(jobs, queue, routes)) ||
        FAILED(display.InitializeOwner(jobs, queue, commands, routes, client)))
        std::terminate(); // Probe setup failure is not a successful controller result.
    Expect("actual-fixture-session-established", client.InstallFixture(31));
    display.RequestOwner();
    Expect("request-alone-does-not-capture-or-publish", !client.graph && jobs.ReservedMemoryBytes() == 0);
    const bool refused = Until(jobs, routes, [&]() noexcept { return client.errors == 1 || client.publications != 0; });
    Expect("actual-scheduler-memory-refusal-settles-on-event",
           refused && client.errors == 1 && !client.graph && client.publications == 0 &&
               client.error == E_OUTOFMEMORY && !display.HasPending() && jobs.ReservedMemoryBytes() == 0);

    const auto samplesAfterRefusal = memory.samples;
    for (unsigned int request = 0; request < 3; ++request)
    {
        display.RequestOwner();
        (void)DrainCompletions(jobs);
    }
    Expect("same-key-refusal-does-not-rearm-or-spin",
           client.errors == 1 && client.publications == 0 && memory.samples == samplesAfterRefusal &&
               !display.HasPending() && !jobs.HasUiWork() && !routes.HasReady() && jobs.ReservedMemoryBytes() == 0);

    memory.snapshot = {kAdmissionHeadroom + 2 * kGiB, kAdmissionHeadroom + 2 * kGiB};
    display.RetryOwner();
    const unsigned int samplesBeforeDeferredCapture = memory.samples;
    const bool latestFixtureInstalled = client.InstallFixture(191);
    Expect("retry-request-does-not-capture-before-owner-drain",
           latestFixtureInstalled && !client.graph && client.publications == 0 &&
               memory.samples == samplesBeforeDeferredCapture && jobs.ReservedMemoryBytes() == 0);
    const bool first = Until(jobs, routes, [&]() noexcept { return client.publications == 1 || client.errors == 2; });
    Expect("first-original-parent-settles-on-event", first);
    if (budgetKnown)
    {
        Expect("known-actual-budget-publishes-exact-graph",
               client.errors == 1 && client.graph && client.graph->IsPublishedOnOwner(device.get(), context.get()));
        if (client.graph)
        {
            std::vector<std::uint8_t> latestPixels;
            UINT latestWidth = 0;
            UINT latestHeight = 0;
            const HRESULT latestReadback =
                client.graph->CompositionForTestOwner()->Readback(latestPixels, latestWidth, latestHeight);
            Expect("same-controller-supersession-publishes-latest-exact-source",
                   SUCCEEDED(latestReadback) && latestWidth == 2 && latestHeight == 2 &&
                       latestPixels == std::vector<std::uint8_t>{191, 191, 191, 255, 191, 191, 191, 255, 191, 191, 191,
                                                                 255, 191, 191, 191, 255});
            const auto firstCharge = jobs.ReservedMemoryBytes();
            auto* old = client.graph;
            const auto composites = old->CompositionForTestOwner()->CompositeCount();
            Expect("published-cache-keeps-complete-charge", firstCharge != 0 && !display.HasPending());
            display.RequestOwner();
            (void)DrainCompletions(jobs);
            Expect("same-key-reuses-exact-cache-and-charge",
                   client.graph == old && client.publications == 1 && jobs.ReservedMemoryBytes() == firstCharge &&
                       old->CompositionForTestOwner()->CompositeCount() == composites);
            client.session.SetOpacity(0.5);
            display.RequestOwner();
            Expect("same-document-keeps-last-frame-until-replacement", client.graph == old);
            Expect("replacement-and-last-use-retirement-settle",
                   Until(jobs, routes, [&]() noexcept
                         { return (client.publications == 2 && !display.HasPending()) || client.errors > 1; }));
            Expect("replacement-publishes-on-same-real-context",
                   client.errors == 1 && client.publications == 2 && client.graph &&
                       client.graph->IsPublishedOnOwner(device.get(), context.get()) &&
                       jobs.ReservedMemoryBytes() != 0 && jobs.ReleasingMemoryBytes() == 0);
            Expect("different-document-fixture-installed", client.InstallFixture(73));
            display.RequestOwner();
            Expect("new-document-excludes-old-pixels-before-next-route", client.graph == nullptr);
            Expect("new-document-parent-and-old-retirement-settle",
                   Until(jobs, routes, [&]() noexcept
                         { return (client.publications == 3 && !display.HasPending()) || client.errors > 1; }));
            Expect("new-document-published-without-refusal",
                   client.publications == 3 && client.errors == 1 && client.graph);
            client.session.ClearDocument();
            display.RequestOwner();
            Expect("clearing-document-excludes-pixels-immediately", client.graph == nullptr);
            Expect("last-root-destruction-refunds-full-charge", Until(jobs, routes,
                                                                      [&]() noexcept
                                                                      {
                                                                          return !display.HasPending() &&
                                                                                 jobs.ReservedMemoryBytes() == 0 &&
                                                                                 jobs.ReleasingMemoryBytes() == 0;
                                                                      }));
        }
    }
    else
    {
        Expect("unknown-actual-budget-refuses-without-fallback",
               client.errors == 2 && FAILED(client.error) && !client.graph);
        Expect("refused-source-retirement-refunds-full-charge", Until(jobs, routes,
                                                                      [&]() noexcept
                                                                      {
                                                                          return !display.HasPending() &&
                                                                                 jobs.ReservedMemoryBytes() == 0 &&
                                                                                 jobs.ReleasingMemoryBytes() == 0;
                                                                      }));
    }
    display.StopOwner();
    queue.StopNotifications();
    struct Closing
    {
        CommittedDisplayController& display;
        GpuWorkQueue& queue;
    } closing{display, queue};
    jobs.Close({[](void* opaque) noexcept
                {
                    auto& state = *static_cast<Closing*>(opaque);
                    state.display.JoinedShutdownOwner();
                    state.queue.CloseAfterWorkers();
                },
                [](void* opaque) noexcept { static_cast<Closing*>(opaque)->display.RetireShutdownForWork(); },
                &closing});
    Expect("actual-joined-shutdown-removes-all-controller-routes", display.CloseAfterWorkersOwner());
    Expect("actual-joined-shutdown-returns-all-CPU-charges",
           jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0);
    return failures == 0;
}
} // namespace

int RunCommittedDisplayControllerTests()
{
    const bool warp = Run(D3D_DRIVER_TYPE_WARP);
    const bool hardware = Run(D3D_DRIVER_TYPE_HARDWARE, true);
    return warp && hardware && failures == 0 ? 0 : 1;
}
