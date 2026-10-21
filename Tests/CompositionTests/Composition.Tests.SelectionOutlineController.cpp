#include "Composition.Tests.SelectionOutlineController.h"

#include "SelectionOutlineDisplayController.h"

#include <array>
#include <cstdio>
#include <memory>
#include <thread>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
unsigned int passed = 0, failed = 0;
void Expect(bool value, const char* name) noexcept
{
    std::printf("%s selection-outline-controller.%s\n", value ? "PASS" : "FAIL", name);
    value ? ++passed : ++failed;
}
SelectionLoops Source()
{
    std::optional<SelectionLoops> value;
    std::thread worker([&]() { value = SelectionLoops::AdoptForWork({{{8, 8}, {24, 8}, {24, 24}, {8, 24}}}, {}, {}); });
    worker.join();
    return value ? std::move(*value) : SelectionLoops{};
}
struct Rig final : SelectionOutlineDisplayClient
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuResidencyBudget budget;
    SelectionOutlineDisplayController controller;
    SelectionLoops selection;
    SelectionOutlineDisplayDescriptor descriptor;
    bool visible = true, initialized = false, closed = false;
    std::uint64_t available = 8ull * 1024 * 1024 * 1024;
    unsigned int submitted = 0, retired = 0, changes = 0, failures = 0;
    HRESULT error = S_OK;
    JobTicket submittedTicket{}, retiredTicket{};
    static bool Memory(void* opaque, MemorySnapshot& output) noexcept
    {
        const auto& rig = *static_cast<Rig*>(opaque);
        output = {rig.available, rig.available};
        return true;
    }
    static void Notice(void* opaque, JobNotice notice) noexcept
    {
        auto& rig = *static_cast<Rig*>(opaque);
        if (notice.event == JobNoticeKind::Submitted)
        {
            ++rig.submitted;
            rig.submittedTicket = notice.ticket;
        }
        if (notice.event == JobNoticeKind::Retired)
        {
            ++rig.retired;
            rig.retiredTicket = notice.ticket;
        }
    }
    bool Initialize() noexcept
    {
        constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level,
                                     1, D3D11_SDK_VERSION, device.put(), nullptr, immediate.put())) ||
            FAILED(jobs.Initialize(1, {Notice, this}, {Memory, this})) || !jobs.EnableDeferredRelease() ||
            !jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) ||
            FAILED(queue.Bind(device.get(), immediate.get())) ||
            FAILED(controller.InitializeOwner(jobs, queue, routes, budget, *this)))
            return false;
        initialized = true;
        descriptor.device = device.get();
        descriptor.immediate = immediate.get();
        descriptor.software = true;
        descriptor.key.deviceGeneration = queue.DeviceGeneration();
        descriptor.key.viewEpoch = 1;
        descriptor.key.document = {Guid::Generate(), 1, 1, 1, 32, 32};
        descriptor.key.grid = {16, 16, 1.0, 4.0, 4.0, 96.0f};
        return true;
    }
    void Install(SelectionLoops value)
    {
        selection = std::move(value);
        descriptor.source = &selection;
        descriptor.key.sourceIdentity = selection.Identity();
    }
    bool DescribeSelectionOutlineOwner(SelectionOutlineDisplayDescriptor& output) noexcept override
    {
        if (!visible)
            return false;
        output = descriptor;
        return true;
    }
    void SelectionOutlineChangedOwner() noexcept override
    {
        ++changes;
    }
    void SelectionOutlineFailedOwner(SelectionOutlineDisplayKey, HRESULT status) noexcept override
    {
        ++failures;
        error = status;
    }
    template <typename Predicate> bool Until(Predicate&& predicate)
    {
        const auto start = GetTickCount64();
        for (;;)
        {
            (void)DrainCompletions(jobs);
            if (predicate())
                return true;
            const auto elapsed = GetTickCount64() - start;
            if (elapsed >= 10000)
                return false;
            if (jobs.HasUiWork() || routes.HasReady())
                continue;
            if (WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()), static_cast<DWORD>(10000 - elapsed)) !=
                WAIT_OBJECT_0)
                return false;
        }
    }
    bool Publish()
    {
        controller.RequestOwner();
        return Until([&]() { return bool(controller.BorrowFrameOwner(0)) && jobs.IsIdle(); });
    }
    bool Close()
    {
        if (closed)
            return true;
        if (!initialized)
        {
            queue.StopNotifications();
            jobs.Close();
            queue.CloseAfterWorkers();
            closed = true;
            return true;
        }
        controller.StopOwner();
        queue.StopNotifications();
        jobs.Close({[](void* opaque) noexcept
                    {
                        auto& rig = *static_cast<Rig*>(opaque);
                        rig.controller.JoinedShutdownOwner();
                        rig.queue.CloseAfterWorkers();
                    },
                    [](void* opaque) noexcept { static_cast<Rig*>(opaque)->controller.RetireShutdownForWork(); },
                    this});
        const bool result = controller.CloseAfterWorkersOwner();
        closed = result;
        return result;
    }
    ~Rig()
    {
        if (!Close())
            std::terminate();
    }
};

void TestCache()
{
    Rig rig;
    Expect(rig.Initialize(), "cache.real-warp-original-scheduler-and-release-sink");
    if (!rig.initialized)
        return;
    rig.Install(Source());
    const auto weak = rig.selection.WeakStorageForTest();
    Expect(weak.use_count() == 1 && rig.budget.ReservedOwner() == 0 && rig.jobs.ReservedMemoryBytes() == 0,
           "cache.descriptor-borrows-before-admission");
    Expect(rig.Publish(), "cache.original-worker-raster-and-gpu-publication");
    if (!rig.controller.BorrowFrameOwner(0))
        return;
    Expect(rig.submitted == 1 && rig.retired == 1 && rig.submittedTicket == rig.retiredTicket,
           "cache.resumed-stages-have-one-original-admission-and-retirement");
    const auto cpu = rig.jobs.ReservedMemoryBytes();
    const auto gpu = rig.budget.ReservedOwner();
    Expect(cpu != 0 && gpu != 0 && rig.budget.ActiveOwner() == 1, "cache.complete-original-cpu-and-gpu-charges-stay");
    const auto changes = rig.changes;
    const auto submitted = rig.submitted;
    const auto original = rig.controller.BorrowFrameOwner(0);
    bool borrowed = true;
    for (unsigned int frame = 0; frame < 1000; ++frame)
    {
        const auto views = rig.controller.BorrowFrameOwner(frame % 8);
        borrowed = borrowed && views && views.solid == original.solid && views.width == 16 && views.height == 16;
    }
    Expect(borrowed && rig.submitted == submitted && rig.changes == changes && !rig.controller.HasPending() &&
               !rig.controller.HasReady() && rig.jobs.IsIdle(),
           "cache.thousand-frame-borrows-keep-settled-cache-idle");
    bool foreignThread = false;
    std::thread probe([&]() noexcept { foreignThread = !rig.controller.BorrowFrameOwner(0); });
    probe.join();
    Expect(foreignThread, "cache.foreign-thread-borrow-refuses");
    for (unsigned int field = 0; field < 6; ++field)
    {
        // Borrow-key validation only: no synthetic descriptor grants mutation or GPU acknowledgement.
        const auto saved = rig.descriptor.key.grid;
        switch (field)
        {
        case 0:
            ++rig.descriptor.key.grid.width;
            break;
        case 1:
            ++rig.descriptor.key.grid.height;
            break;
        case 2:
            rig.descriptor.key.grid.documentScalePixel += 0.25;
            break;
        case 3:
            rig.descriptor.key.grid.originPixelX += 0.25;
            break;
        case 4:
            rig.descriptor.key.grid.originPixelY += 0.25;
            break;
        case 5:
            rig.descriptor.key.grid.dpi += 1;
            break;
        }
        std::array<char, 80> label{};
        (void)std::snprintf(label.data(), label.size(), "cache.grid-field-%u-refuses-stale-frame", field);
        Expect(!rig.controller.BorrowFrameOwner(0) && rig.budget.ReservedOwner() == gpu &&
                   rig.jobs.ReservedMemoryBytes() == cpu,
               label.data());
        rig.descriptor.key.grid = saved;
    }
    ++rig.descriptor.key.document.documentSerial;
    Expect(!rig.controller.BorrowFrameOwner(0), "cache.foreign-document-refuses-before-pump");
    --rig.descriptor.key.document.documentSerial;
    ++rig.descriptor.key.viewEpoch;
    Expect(!rig.controller.BorrowFrameOwner(0), "cache.new-view-epoch-refuses-before-pump");
    rig.controller.RequestOwner();
    Expect(
        rig.Until(
            [&]()
            { return bool(rig.controller.BorrowFrameOwner(0)) && !rig.controller.HasPending() && rig.jobs.IsIdle(); }),
        "cache.new-epoch-replaces-through-real-old-context-and-worker-retirement");
    Expect(rig.submitted == 2 && rig.retired == 2 && rig.budget.ActiveOwner() == 1,
           "cache.replacement-keeps-at-most-two-graphs-and-one-final-cache");
    rig.visible = false;
    Expect(!rig.controller.BorrowFrameOwner(0), "hidden.borrow-refuses-immediately");
    rig.controller.RequestOwner();
    Expect(rig.Until([&]() { return !rig.controller.HasPending() && rig.jobs.IsIdle(); }) &&
               rig.budget.ReservedOwner() == 0 && rig.jobs.ReservedMemoryBytes() == 0,
           "hidden.actual-gpu-event-and-worker-release-return-all-charges");
    rig.visible = true;
    ++rig.descriptor.key.viewEpoch;
    Expect(rig.Publish(), "hidden.resume-prepares-current-source-without-owner-fallback");
    rig.controller.SetSuspendedOwner(true);
    Expect(!rig.controller.BorrowFrameOwner(0), "suspend.frame-borrow-excluded-before-pump");
    Expect(rig.Until([&]() { return !rig.controller.HasPending() && rig.jobs.IsIdle(); }) &&
               rig.budget.ReservedOwner() == 0 && rig.jobs.ReservedMemoryBytes() == 0,
           "suspend.retirement-settles-without-hidden-replacement");
    const auto suspendedCount = rig.submitted;
    rig.controller.SetSuspendedOwner(true);
    Expect(rig.submitted == suspendedCount && !rig.controller.HasReady(), "suspend.repeated-state-does-not-rearm");
    rig.controller.SetSuspendedOwner(false);
    Expect(rig.Until([&]() { return bool(rig.controller.BorrowFrameOwner(0)) && rig.jobs.IsIdle(); }),
           "suspend.resume-original-admission-publishes-current-grid");
    Expect(rig.Close() && !rig.controller.HasPending() && rig.budget.ReservedOwner() == 0 &&
               rig.jobs.ReservedMemoryBytes() == 0,
           "close.cached-graph-retires-in-real-joined-close-hooks");
}
void TestQuarantine()
{
    Rig rig;
    Expect(rig.Initialize(), "barrier.real-original-owner-setup");
    if (!rig.initialized)
        return;
    rig.Install(Source());
    Expect(rig.Publish(), "barrier.original-cache-publishes");
    if (!rig.controller.BorrowFrameOwner(0))
        return;
    const auto cpu = rig.jobs.ReservedMemoryBytes(), gpu = rig.budget.ReservedOwner();
    rig.queue.SetRetirementCompletionRefusalsForTest(E_ACCESSDENIED, 1);
    rig.visible = false;
    rig.controller.RequestOwner();
    // Two bounded owner passes: first detaches, second offers one real barrier that is refused before recording.
    (void)DrainCompletions(rig.jobs);
    (void)DrainCompletions(rig.jobs);
    (void)DrainCompletions(rig.jobs);
    Expect(!rig.controller.BorrowFrameOwner(0) && rig.controller.HasPending() && !rig.controller.HasReady() &&
               !rig.routes.HasReady() && rig.queue.InFlightCount() == 0,
           "barrier.refused-signal-excludes-pixels-and-remains-dormant");
    Expect(rig.jobs.ReservedMemoryBytes() == cpu && rig.budget.ReservedOwner() == gpu,
           "barrier.refusal-is-not-acknowledgement-or-memory-refund");
    rig.controller.RetryOwner();
    Expect(rig.Until([&]() { return !rig.controller.HasPending() && rig.jobs.IsIdle(); }) &&
               rig.jobs.ReservedMemoryBytes() == 0 && rig.budget.ReservedOwner() == 0,
           "barrier.explicit-retry-retires-through-real-original-event-and-worker-join");
    Expect(rig.Close(), "barrier.close-after-real-retirement");
}
void TestPressure()
{
    Rig rig;
    Expect(rig.Initialize(), "pressure.real-owner-setup");
    if (!rig.initialized)
        return;
    rig.Install(Source());
    const auto weak = rig.selection.WeakStorageForTest();
    rig.available = 1024 * 1024;
    rig.controller.RequestOwner();
    Expect(rig.Until([&]() { return rig.failures == 1 && rig.jobs.IsIdle(); }),
           "pressure.original-cpu-admission-refuses-without-waiting");
    Expect(!rig.controller.BorrowFrameOwner(0) && weak.use_count() == 1 && rig.budget.ReservedOwner() == 0 &&
               rig.jobs.ReservedMemoryBytes() == 0,
           "pressure.refusal-precedes-source-capture-and-refunds-unused-gpu-envelope");
    const auto submitted = rig.submitted;
    rig.controller.RequestOwner();
    (void)DrainCompletions(rig.jobs);
    Expect(rig.submitted == submitted && !rig.controller.HasReady(),
           "pressure.failed-unchanged-key-does-not-spin-or-readmit");
    rig.available = 8ull * 1024 * 1024 * 1024;
    rig.controller.RetryOwner();
    Expect(rig.Until([&]() { return bool(rig.controller.BorrowFrameOwner(0)) && rig.jobs.IsIdle(); }),
           "pressure.explicit-retry-after-memory-change-publishes");
    Expect(rig.Close(), "pressure.close-keeps-original-cleanup-order");
}
void TestQueuedClose()
{
    Rig rig;
    Expect(rig.Initialize(), "queued-close.real-owner-setup");
    if (!rig.initialized)
        return;
    rig.Install(Source());
    rig.controller.RequestOwner();
    rig.controller.Pump(); // Descriptor submitted; no completion is drained to capture the immutable source.
    Expect(rig.submitted == 1 && !rig.controller.BorrowFrameOwner(0),
           "queued-close.original-descriptor-has-no-published-frame");
    Expect(rig.Close() && rig.budget.ReservedOwner() == 0 && rig.jobs.ReservedMemoryBytes() == 0 &&
               !rig.controller.HasPending(),
           "queued-close.real-close-joins-and-retires-unpublished-descriptor");
}
} // namespace
int RunSelectionOutlineControllerTests()
{
    passed = failed = 0;
    TestCache();
    TestQuarantine();
    TestPressure();
    TestQueuedClose();
    std::printf("%s selection-outline-controller.summary.%u-passed.%u-failed\n", failed ? "FAIL" : "PASS", passed,
                failed);
    return static_cast<int>(failed);
}
