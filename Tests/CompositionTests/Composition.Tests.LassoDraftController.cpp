#include "Composition.Tests.LassoDraftController.h"
#include "CanvasRenderer.h"
#include "SelectionOutlineDisplayController.h"
#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)
namespace
{
unsigned int checks = 0, failures = 0;
void Expect(bool value, const char* name) noexcept
{
    std::printf("%s lasso-draft-controller.%s\n", value ? "PASS" : "FAIL", name);
    ++checks;
    failures += value ? 0u : 1u;
}
struct Rig;
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class ArenaJob final : public Job
{
  public:
    explicit ArenaJob(Rig& rig) noexcept : _rig(rig) {}
    JobResult Run(JobContext&) override;
    void Completed(JobResult) noexcept override;
    void RetainJoinedRetirement(JobScheduler&, JobTicket) noexcept override;

  private:
    Rig& _rig;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct Rig final : SelectionOutlineDisplayClient, LassoDraftDisplayClient
{
    static constexpr std::uint64_t sourceBytes = 4ull * 1024 * 1024;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuResidencyBudget budget;
    SelectionOutlineDisplayController overlay;
    std::shared_ptr<SharedPublicationMemory> memory;
    std::optional<BoundedLassoPoints> arena;
    std::optional<BoundedLassoGesture> gesture;
    BoundedLassoGesture::Display source;
    LassoDraftDisplayDescriptor descriptor;
    JobTicket sourceTicket{};
    bool initialized = false, overlayInitialized = false, closed = false, visible = true, sourceBound = false;
    HRESULT sourceResult = E_PENDING;
    unsigned int notices = 0, errors = 0;
    std::uint64_t available = 8ull * 1024 * 1024 * 1024;
    static bool Memory(void* opaque, MemorySnapshot& value) noexcept
    {
        const auto& rig = *static_cast<Rig*>(opaque);
        value = {rig.available, rig.available};
        return true;
    }
    bool Initialize() noexcept
    {
        constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level,
                                     1, D3D11_SDK_VERSION, device.put(), nullptr, immediate.put())) ||
            FAILED(jobs.Initialize(1, {}, {Memory, this})) || !jobs.EnableDeferredRelease() ||
            !jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) ||
            FAILED(queue.Bind(device.get(), immediate.get())))
            return false;
        if (FAILED(overlay.InitializeOwner(jobs, queue, routes, budget, *this)))
            return false;
        overlayInitialized = true;
        if (FAILED(overlay.EnableLassoDraftOwner(*this)))
            return false;
        descriptor.device = device.get();
        descriptor.immediate = immediate.get();
        descriptor.software = true;
        descriptor.key.deviceGeneration = queue.DeviceGeneration();
        descriptor.key.viewEpoch = 1;
        descriptor.key.document = {Guid::Generate(), 1, 1, 1, 48, 40};
        descriptor.key.grid = {48, 40, 1.0, 5.0, 4.5, 96.0f};
        initialized = true;
        return true;
    }
    void Capture() noexcept
    {
        source = gesture->CaptureOwner();
        descriptor.source = &source;
        descriptor.key.draft = source.key;
        descriptor.key.arenaIdentity = source.points.ArenaIdentity();
        descriptor.key.pointCount = source.points.Count();
        descriptor.key.kind = source.kind;
        descriptor.key.mode = source.mode;
        descriptor.key.antialiased = source.antialiased;
        descriptor.key.cursor = source.cursor;
        descriptor.key.anchor = source.anchor;
    }
    bool Open()
    {
        JobOptions options;
        options.kind = JobClass::Selection;
        options.memoryBytes = sourceBytes;
        options.allowMemoryWaiting = false;
        options.deferredRetirement = true;
        options.reserveRetainedMemory = true;
        std::unique_ptr<Job> job = std::make_unique<ArenaJob>(*this);
        if (jobs.Submit(job, options, sourceTicket) != S_OK || job ||
            SettleJobs(jobs, 10000) != SettleResult::Settled || sourceResult != S_OK || !sourceBound || !arena)
            return false;
        gesture.emplace(std::move(*arena));
        arena.reset();
        if (!gesture->BeginOwner(19, LassoKind::Freehand, SelectionMode::New, true, {10, 12.5}) ||
            gesture->ExtendOwner(gesture->CurrentOwner(), {34, 12.5}) != BoundedLassoGesture::Outcome::Changed)
            return false;
        Capture();
        return true;
    }
    bool DescribeSelectionOutlineOwner(SelectionOutlineDisplayDescriptor&) noexcept override
    {
        return false;
    }
    void SelectionOutlineChangedOwner() noexcept override {}
    void SelectionOutlineFailedOwner(SelectionOutlineDisplayKey, HRESULT) noexcept override
    {
        ++errors;
    }
    bool DescribeLassoDraftOwner(LassoDraftDisplayDescriptor& out) noexcept override
    {
        if (!visible)
            return false;
        out = descriptor;
        return true;
    }
    void LassoDraftChangedOwner() noexcept override
    {
        ++notices;
    }
    void LassoDraftFailedOwner(LassoDraftDisplayKey, HRESULT) noexcept override
    {
        ++errors;
    }
    template <class P> bool Until(P&& predicate)
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
        overlay.RequestLassoDraftOwner();
        return Until([&]() { return bool(overlay.BorrowLassoDraftFrameOwner()) && jobs.IsIdle(); });
    }
    bool Close() noexcept
    {
        if (closed)
            return true;
        if (overlayInitialized)
            overlay.StopOwner();
        queue.StopNotifications();
        jobs.Close({[](void* opaque) noexcept
                    {
                        auto& rig = *static_cast<Rig*>(opaque);
                        if (rig.overlayInitialized)
                            rig.overlay.JoinedShutdownOwner();
                        // Useful creators have joined; final source release may now enter the original release sink.
                        rig.source = {};
                        rig.descriptor.source = nullptr;
                        rig.gesture.reset();
                        rig.arena.reset();
                        rig.memory.reset();
                        rig.queue.CloseAfterWorkers();
                    },
                    [](void* opaque) noexcept
                    {
                        auto& rig = *static_cast<Rig*>(opaque);
                        if (rig.overlayInitialized)
                            rig.overlay.RetireShutdownForWork();
                    },
                    this});
        const bool okay = !overlayInitialized || overlay.CloseAfterWorkersOwner();
        if (!okay)
            return false;
        closed = true;
        return true;
    }
    ~Rig()
    {
        if (!Close())
            std::terminate();
    }
};
#pragma warning(pop)

JobResult ArenaJob::Run(JobContext& context)
{
    if (context.reservedMemoryBytes != Rig::sourceBytes || !context.deferredRetirement)
        return {JobStatus::Failed, E_INVALIDARG};
    _rig.memory = SharedPublicationMemory::CreateForWork(Rig::sourceBytes);
    _rig.arena = BoundedLassoPoints::CreateForWork(_rig.memory, 1, context.cancellation);
    return _rig.arena ? JobResult{} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
}
void ArenaJob::Completed(JobResult result) noexcept
{
    _rig.sourceResult = result.status == JobStatus::Finished ? S_OK : result.error;
    if (_rig.memory)
    {
        const auto bound = result.status == JobStatus::Finished
                               ? _rig.memory->BindAfterCompletedWork(_rig.jobs, _rig.sourceTicket)
                               : _rig.memory->BindForRetirement(_rig.jobs, _rig.sourceTicket);
        _rig.sourceBound = bound == S_OK && _rig.memory->BoundTo(_rig.jobs, _rig.sourceTicket);
    }
}
void ArenaJob::RetainJoinedRetirement(JobScheduler& jobs, JobTicket ticket) noexcept
{
    if (_rig.memory && !_rig.memory->IsBound())
        _rig.sourceResult = _rig.memory->BindForRetirement(jobs, ticket);
}
bool LiteralPixels(Rig& rig, ID3D11ShaderResourceView* view) noexcept
{
    wil::com_ptr_nothrow<ID3D11Resource> resource;
    view->GetResource(resource.put());
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    if (FAILED(resource.query_to(texture.put())))
        return false;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.Width != 48 || desc.Height != 40 || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
        desc.Usage != D3D11_USAGE_IMMUTABLE)
        return false;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    if (FAILED(rig.device->CreateTexture2D(&desc, nullptr, staging.put())))
        return false;
    rig.immediate->CopyResource(staging.get(), texture.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(rig.immediate->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    auto unmap = wil::scope_exit([&]() noexcept { rig.immediate->Unmap(staging.get(), 0); });
    if (!mapped.pData || mapped.RowPitch < 48 * 4)
        return false;
    const auto pixel = [&](unsigned int x, unsigned int y) noexcept
    {
        const auto* data =
            static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch + x * 4;
        return std::array<std::uint8_t, 4>{data[0], data[1], data[2], data[3]};
    };
    return pixel(20, 4) == std::array<std::uint8_t, 4>{255, 255, 255, 255} &&
           pixel(20, 3) == std::array<std::uint8_t, 4>{0, 0, 0, 102} &&
           pixel(20, 20) == std::array<std::uint8_t, 4>{0, 0, 0, 0};
}
void DropRigSourceReferences(Rig& rig) noexcept
{
    rig.source = {};
    rig.descriptor.source = nullptr;
    rig.gesture.reset();
    rig.arena.reset();
    rig.memory.reset();
}
void TestRetirementRefusalRetry()
{
    auto rig = std::make_unique<Rig>();
    Expect(rig->Initialize(), "retirement-refusal.real-warp-owner-setup");
    if (!rig->initialized)
        return;
    const bool opened = rig->Open();
    Expect(opened, "retirement-refusal.original-arena-ticket-ready");
    if (!opened || !rig->gesture)
        return;
    const bool published = rig->Publish();
    Expect(published, "retirement-refusal.actual-draft-published-before-injection");
    if (!published)
        return;
    const auto original = rig->overlay.BorrowLassoDraftFrameOwner();
    if (!original)
    {
        Expect(false, "retirement-refusal.published-draft-is-readable");
        return;
    }
    const auto cpu = rig->jobs.ReservedMemoryBytes();
    const auto gpu = rig->budget.ReservedOwner();
    std::weak_ptr<SharedPublicationMemory> weakSource = rig->memory;
    const auto failuresBefore = rig->errors;
    const auto changesBefore = rig->notices;
    rig->queue.SetRetirementCompletionRefusalsForTest(E_ACCESSDENIED, 1);
    rig->visible = false;
    rig->overlay.RequestLassoDraftOwner();
    // One owner pass detaches the stale cache; the next offers and consumes the real injected refusal.
    (void)DrainCompletions(rig->jobs);
    (void)DrainCompletions(rig->jobs);
    Expect(!rig->overlay.BorrowLassoDraftFrameOwner() && rig->overlay.HasPending() && !rig->overlay.HasReady() &&
               !rig->routes.HasReady() && rig->queue.InFlightCount() == 0,
           "retirement-refusal.unpublished-old-draft-stays-dormant-without-event");
    Expect(rig->jobs.ReservedMemoryBytes() == cpu && cpu > Rig::sourceBytes && rig->budget.ReservedOwner() == gpu &&
               gpu != 0 && !weakSource.expired() && rig->source.points.Count() == rig->descriptor.key.pointCount,
           "retirement-refusal.source-readable-and-both-reservations-held");
    Expect(rig->errors == failuresBefore && rig->notices == changesBefore + 1,
           "retirement-refusal.invalidation-notice-is-not-a-draft-failure-callback");
    DropRigSourceReferences(*rig);
    Expect(!rig->gesture && !rig->arena && !rig->memory && rig->source.points.Empty() &&
               rig->descriptor.source == nullptr && !weakSource.expired() && rig->jobs.ReservedMemoryBytes() == cpu &&
               rig->budget.ReservedOwner() == gpu,
           "retirement-refusal.cache-is-sole-strong-source-holder-after-owner-drop");
    Expect(rig->device->GetDeviceRemovedReason() == S_OK &&
               rig->queue.DeviceGeneration() == rig->descriptor.key.deviceGeneration,
           "retirement-refusal.original-warp-device-remains-healthy");
    (void)DrainCompletions(rig->jobs);
    Expect(!rig->overlay.HasReady() && !rig->routes.HasReady() && rig->overlay.HasPending() &&
               rig->queue.InFlightCount() == 0 && rig->errors == failuresBefore,
           "retirement-refusal.extra-owner-drain-does-not-retry-or-report-failure");
    rig->overlay.RetryOwner();
    Expect(rig->overlay.HasReady() && rig->routes.HasReady(),
           "retirement-refusal.explicit-retry-rearms-existing-captured-source");
    // RetryOwner wakes both the parent selection controller and its nested draft. Their fair alternation
    // gives the already-selected parent turn one pass before the draft records its real retirement event.
    (void)DrainCompletions(rig->jobs);
    std::printf("DIAG lasso-retirement.parent-retry-turn in-flight=%zu pending=%d ready=%d cpu=%llu gpu=%llu\n",
                rig->queue.InFlightCount(), static_cast<int>(rig->overlay.HasPending()),
                static_cast<int>(rig->overlay.HasReady()),
                static_cast<unsigned long long>(rig->jobs.ReservedMemoryBytes()),
                static_cast<unsigned long long>(rig->budget.ReservedOwner()));
    (void)DrainCompletions(rig->jobs);
    Expect(rig->queue.InFlightCount() == 1 && rig->overlay.HasPending() && rig->jobs.ReservedMemoryBytes() == cpu &&
               rig->budget.ReservedOwner() == gpu,
           "retirement-refusal.retry-records-real-original-stream-event-before-release");
    const bool retired = rig->Until(
        [&]()
        {
            return !rig->overlay.HasPending() && rig->jobs.IsIdle() && rig->queue.InFlightCount() == 0 &&
                   rig->jobs.ReservedMemoryBytes() == Rig::sourceBytes && rig->budget.ReservedOwner() == 0 &&
                   weakSource.expired();
        });
    Expect(retired && weakSource.expired() && rig->jobs.ReservedMemoryBytes() == Rig::sourceBytes &&
               rig->budget.ReservedOwner() == 0 && rig->device->GetDeviceRemovedReason() == S_OK,
           "retirement-refusal.real-event-releases-arena-but-weak-control-keeps-source-ticket-charge");
    Expect(rig->errors == failuresBefore && rig->notices == changesBefore + 1,
           "retirement-refusal.event-ack-is-not-a-failure-or-extra-invalidation-callback");
    // SharedPublicationMemory's allocate_shared control block retains its original lease until the last weak owner
    // resets.
    weakSource.reset();
    (void)DrainCompletions(rig->jobs);
    Expect(rig->jobs.ReservedMemoryBytes() == 0 && !rig->overlay.HasPending() && rig->budget.ReservedOwner() == 0,
           "retirement-refusal.weak-control-reset-acknowledges-original-ticket-and-refunds-zero");
    Expect(rig->Close() && rig->jobs.ReservedMemoryBytes() == 0,
           "retirement-refusal.retry-case-closes-after-source-ticket-refund");
}
void TestRetirementRefusalJoinedClose()
{
    auto rig = std::make_unique<Rig>();
    Expect(rig->Initialize(), "retirement-close.real-warp-owner-setup");
    if (!rig->initialized)
        return;
    const bool opened = rig->Open();
    Expect(opened, "retirement-close.original-arena-ticket-ready");
    if (!opened || !rig->gesture)
        return;
    const bool published = rig->Publish();
    Expect(published, "retirement-close.actual-draft-published-before-injection");
    if (!published)
        return;
    if (!rig->overlay.BorrowLassoDraftFrameOwner())
    {
        Expect(false, "retirement-close.published-draft-is-readable");
        return;
    }
    const auto cpu = rig->jobs.ReservedMemoryBytes();
    const auto gpu = rig->budget.ReservedOwner();
    std::weak_ptr<SharedPublicationMemory> weakSource = rig->memory;
    const auto failuresBefore = rig->errors;
    const auto changesBefore = rig->notices;
    rig->queue.SetRetirementCompletionRefusalsForTest(E_ACCESSDENIED, 1);
    rig->visible = false;
    rig->overlay.RequestLassoDraftOwner();
    // One owner pass detaches the stale cache; the next offers and consumes the real injected refusal.
    (void)DrainCompletions(rig->jobs);
    (void)DrainCompletions(rig->jobs);
    Expect(!rig->overlay.BorrowLassoDraftFrameOwner() && rig->overlay.HasPending() && !rig->overlay.HasReady() &&
               !rig->routes.HasReady() && rig->queue.InFlightCount() == 0 && rig->jobs.ReservedMemoryBytes() == cpu &&
               rig->budget.ReservedOwner() == gpu && !weakSource.expired(),
           "retirement-close.refused-state-is-unborrowable-and-charged");
    DropRigSourceReferences(*rig);
    Expect(!rig->gesture && !rig->arena && !rig->memory && rig->source.points.Empty() &&
               rig->descriptor.source == nullptr && !weakSource.expired() && rig->jobs.ReservedMemoryBytes() == cpu &&
               rig->budget.ReservedOwner() == gpu,
           "retirement-close.cache-is-sole-strong-source-holder-before-close");
    Expect(rig->device->GetDeviceRemovedReason() == S_OK &&
               rig->queue.DeviceGeneration() == rig->descriptor.key.deviceGeneration,
           "retirement-close.original-warp-device-remains-healthy");
    Expect(rig->notices == changesBefore + 1, "retirement-close.cache-detach-notice-is-distinct-from-failure-callback");
    Expect(rig->errors == failuresBefore, "retirement-close.barrier-refusal-does-not-report-draft-failure");
    Expect(rig->Close() && !rig->overlay.HasPending() && rig->budget.ReservedOwner() == 0 &&
               rig->jobs.ReservedMemoryBytes() == 0 && weakSource.expired(),
           "retirement-close.joined-shutdown-retires-refused-cache-and-closes-admission");
    Expect(rig->errors == failuresBefore, "retirement-close.joined-cleanup-does-not-report-draft-failure");
}
} // namespace
int RunLassoDraftControllerTests()
{
    checks = failures = 0;
    auto rig = std::make_unique<Rig>();
    Expect(rig->Initialize(), "real-warp-owner-and-original-scheduler");
    if (!rig->initialized)
        return 1;
    const auto routeCount = rig->routes.SourceCountForTest();
    Expect(routeCount == 2 && rig->routes.ContainsSourceForTest(rig->overlay), "draft-shares-existing-overlay-route");
    Expect(rig->Open(), "worker-created-arena-binds-original-ticket-before-owner-capture");
    if (!rig->gesture)
        return 1;
    Expect(!rig->overlay.BorrowLassoDraftFrameOwner(), "unpublished-source-borrow-refuses");
    Expect(rig->Publish(), "original-worker-raster-and-immutable-bgra-publish");
    const auto original = rig->overlay.BorrowLassoDraftFrameOwner();
    if (!original)
        return 1;
    Expect(LiteralPixels(*rig, original.color), "real-immutable-bgra-texture-matches-literal-line-pixels");
    const auto cpu = rig->jobs.ReservedMemoryBytes(), gpu = rig->budget.ReservedOwner();
    Expect(cpu > Rig::sourceBytes && gpu != 0 && rig->budget.ActiveOwner() == 1,
           "cache-retains-distinct-original-arena-and-display-charges");
    const auto notices = rig->notices;
    bool borrowed = true;
    for (unsigned int i = 0; i < 1000; ++i)
    {
        const auto view = rig->overlay.BorrowLassoDraftFrameOwner();
        borrowed = borrowed && view && view.color == original.color;
    }
    Expect(borrowed && rig->notices == notices && rig->jobs.IsIdle() && !rig->routes.HasReady(),
           "thousand-settled-borrows-allocate-no-job-or-wake");
    constexpr std::array<const char*, 6> names{
        "grid-width-refuses-stale-borrow",    "grid-height-refuses-stale-borrow",   "grid-scale-refuses-stale-borrow",
        "grid-origin-x-refuses-stale-borrow", "grid-origin-y-refuses-stale-borrow", "grid-dpi-refuses-stale-borrow"};
    for (unsigned int field = 0; field < names.size(); ++field)
    {
        const auto saved = rig->descriptor.key.grid;
        switch (field)
        {
        case 0:
            ++rig->descriptor.key.grid.width;
            break;
        case 1:
            ++rig->descriptor.key.grid.height;
            break;
        case 2:
            rig->descriptor.key.grid.documentScalePixel += 0.25;
            break;
        case 3:
            rig->descriptor.key.grid.originPixelX += 0.25;
            break;
        case 4:
            rig->descriptor.key.grid.originPixelY += 0.25;
            break;
        case 5:
            rig->descriptor.key.grid.dpi += 1;
            break;
        }
        Expect(!rig->overlay.BorrowLassoDraftFrameOwner() && rig->budget.ReservedOwner() == gpu &&
                   rig->jobs.ReservedMemoryBytes() == cpu,
               names[field]);
        rig->descriptor.key.grid = saved;
    }
    ++rig->descriptor.key.document.documentSerial;
    Expect(!rig->overlay.BorrowLassoDraftFrameOwner(), "foreign-document-refuses-before-pump");
    --rig->descriptor.key.document.documentSerial;
    auto savedSource = rig->source;
    const auto savedKey = rig->descriptor.key;
    (void)rig->gesture->CancelOwner(rig->gesture->CurrentOwner());
    (void)rig->gesture->BeginOwner(20, LassoKind::Freehand, SelectionMode::New, true, {34, 12.5});
    (void)rig->gesture->ExtendOwner(rig->gesture->CurrentOwner(), {10, 12.5});
    rig->Capture();
    rig->source.key = savedSource.key;
    rig->descriptor.key = savedKey;
    Expect(!rig->overlay.BorrowLassoDraftFrameOwner(), "forged-equal-scalar-key-refuses-different-inline-tail");
    rig->source = savedSource;
    rig->descriptor.key = savedKey;
    savedSource = {}; // The spoof fixture must release its own strong reader before joined-close proof.
    ++rig->descriptor.key.viewEpoch;
    Expect(!rig->overlay.BorrowLassoDraftFrameOwner(), "new-view-epoch-refuses-before-pump");
    Expect(rig->Publish() && rig->budget.ActiveOwner() == 1 && rig->routes.SourceCountForTest() == routeCount,
           "replacement-retires-original-context-and-worker-under-same-route");
    rig->visible = false;
    Expect(!rig->overlay.BorrowLassoDraftFrameOwner(), "hidden-borrow-refuses-immediately");
    rig->overlay.RequestLassoDraftOwner();
    Expect(rig->Until([&]() { return !rig->overlay.HasPending() && rig->jobs.IsIdle(); }) &&
               rig->budget.ReservedOwner() == 0 && rig->jobs.ReservedMemoryBytes() == Rig::sourceBytes,
           "real-gpu-event-and-worker-retirement-refund-only-display-charge");
    rig->visible = true;
    ++rig->descriptor.key.viewEpoch;
    Expect(rig->Publish(), "resume-prepares-current-source-without-owner-fallback");
    rig->overlay.SetSuspendedOwner(true);
    Expect(!rig->overlay.BorrowLassoDraftFrameOwner(), "shared-suspend-excludes-draft-borrow");
    Expect(rig->Until([&]() { return !rig->overlay.HasPending() && rig->jobs.IsIdle(); }) &&
               rig->budget.ReservedOwner() == 0,
           "shared-suspend-settles-original-retirement-without-polling");
    rig->overlay.SetSuspendedOwner(false);
    Expect(rig->Publish(), "shared-resume-admits-one-current-display");
    const std::weak_ptr<SharedPublicationMemory> weak = rig->memory;
    Expect(rig->Close() && !rig->overlay.HasPending() && rig->budget.ReservedOwner() == 0 &&
               rig->jobs.ReservedMemoryBytes() == 0 && weak.expired(),
           "real-joined-close-retires-source-and-draft-before-zero-refund");
    TestRetirementRefusalRetry();
    TestRetirementRefusalJoinedClose();
    std::printf("SUMMARY lasso-draft-controller checks=%u failures=%u\n", checks, failures);
    return static_cast<int>(failures);
}
