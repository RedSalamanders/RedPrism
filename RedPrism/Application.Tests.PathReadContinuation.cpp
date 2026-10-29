// Original-ticket native path-read lifetime and actual host boundary fixtures.
#include "Application.h"
#include "ChromeHost.h"
#include "DeferredRelease.h"
#include "PathClassificationRead.h"
#include "PathReadWait.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Runtime.Tests.ApplicationShutdownTestProbe.h"
#include <cstddef>

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace
{
constexpr DWORD kFailureBound = 15000;
constexpr UINT kReadNotice = WM_APP + 0x7A3;
constexpr UINT kReadyNotice = WM_APP + 0x7A4;
constexpr std::array<std::uint8_t, 4> kImageHeader{'I', 'M', 'G', 0};
constexpr std::array<std::uint8_t, 8> kPixels{10, 20, 30, 255, 40, 50, 60, 255};

class TestFile final
{
  public:
    TestFile() noexcept
    {
        std::array<wchar_t, MAX_PATH> directory{};
        const auto length = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
        if (!length || length >= directory.size() || !GetTempFileNameW(directory.data(), L"PCW", 0, _path.data()))
            return;
        _created = true;
        wil::unique_hfile file{
            CreateFileW(_path.data(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        DWORD written = 0;
        _valid =
            file &&
            WriteFile(file.get(), kImageHeader.data(), static_cast<DWORD>(kImageHeader.size()), &written, nullptr) &&
            written == kImageHeader.size();
    }
    ~TestFile()
    {
        if (_created)
            (void)DeleteFileW(_path.data());
    }
    TestFile(const TestFile&) = delete;
    TestFile& operator=(const TestFile&) = delete;
    bool Valid() const noexcept
    {
        return _valid;
    }
    const wchar_t* Path() const noexcept
    {
        return _path.data();
    }

  private:
    std::array<wchar_t, MAX_PATH> _path{};
    bool _created = false;
    bool _valid = false;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class TestPipe final
{
  public:
    bool Initialize() noexcept
    {
        static unsigned int sequence = 0; // Test setup is serial on the owner.
        std::array<wchar_t, 192> name{};
        (void)std::format_to_n(name.data(), static_cast<std::ptrdiff_t>(name.size() - 1),
                               L"\\\\.\\pipe\\RedPrism.PathRead.{}.{}", GetCurrentProcessId(), ++sequence);
        _server.reset(CreateNamedPipeW(name.data(),
                                       PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 64, 64, 0, nullptr));
        if (!_server)
            return false;
        wil::unique_event_nothrow connected;
        if (FAILED(connected.create(wil::EventOptions::ManualReset)))
            return false;
        OVERLAPPED operation{};
        operation.hEvent = connected.get();
        const bool immediate = ConnectNamedPipe(_server.get(), &operation) != FALSE;
        const auto error = immediate ? ERROR_SUCCESS : GetLastError();
        if (!immediate && error != ERROR_IO_PENDING && error != ERROR_PIPE_CONNECTED)
            return false;
        bool pending = !immediate && error == ERROR_IO_PENDING;
        const auto joinOnFailure = wil::scope_exit(
            [&]() noexcept
            {
                if (pending)
                {
                    (void)CancelIoEx(_server.get(), &operation);
                    DWORD ignored = 0;
                    (void)GetOverlappedResult(_server.get(), &operation, &ignored, TRUE);
                }
            });
        _writer.reset(
            CreateFileW(name.data(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!_writer)
            return false;
        if (pending)
        {
            if (WaitForSingleObject(connected.get(), kFailureBound) != WAIT_OBJECT_0)
                return false;
            DWORD ignored = 0;
            if (!GetOverlappedResult(_server.get(), &operation, &ignored, FALSE))
                return false;
            pending = false;
        }
        return true;
    }
    void* TakeReaderWorker() noexcept
    {
        return _server.release();
    }
    HANDLE Writer() const noexcept
    {
        return _writer.get();
    }

  private:
    wil::unique_hfile _server;
    wil::unique_hfile _writer;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Source final
{
    TestPipe* pipe = nullptr;
    const wchar_t* pipePath = nullptr;
    DWORD ownerThread = GetCurrentThreadId();
    HANDLE issueRelease = nullptr; // Controlled harness hold; outlives actual Close.
    std::atomic<bool> issueHeld{false};
    std::atomic<unsigned int> earlyReady{0};
    std::atomic<std::uint64_t> expectedEarlyCycle{0};
    std::atomic<unsigned int> opens{0}, immediate{0}, nativeAcknowledgements{0}, issueFailures{0};
    std::atomic<unsigned int> pending{0}, incomplete{0}, acknowledged{0}, cancellations{0}, destroyed{0};
    std::atomic<bool> offOwner{true}, mta{true}, posted{true};
    bool pipeRead = false; // Worker only; owner reads published atomics, never this unfinished state.

    static void* Open(void* opaque, const wchar_t* path) noexcept
    {
        auto& source = *static_cast<Source*>(opaque);
        source.opens.fetch_add(1);
        source.pipeRead = CompareStringOrdinal(path, -1, source.pipePath, -1, FALSE) == CSTR_EQUAL;
        return source.pipeRead ? source.pipe->TakeReaderWorker()
                               : CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    }
    static void Observe(void* opaque, PathClassificationReadNotice notice) noexcept
    {
        auto& source = *static_cast<Source*>(opaque);
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        if (GetCurrentThreadId() == source.ownerThread)
            source.offOwner.store(false);
        if (FAILED(CoGetApartmentType(&apartment, &qualifier)) || apartment != APTTYPE_MTA)
            source.mta.store(false);
        if (notice == PathClassificationReadNotice::Destroyed)
            source.destroyed.fetch_add(1);
        if (notice == PathClassificationReadNotice::Immediate)
            source.immediate.fetch_add(1);
        if (notice == PathClassificationReadNotice::Acknowledged)
            source.nativeAcknowledgements.fetch_add(1);
        if (notice == PathClassificationReadNotice::IssueFailed)
            source.issueFailures.fetch_add(1);
        if (!source.pipeRead)
            return;
        switch (notice)
        {
        case PathClassificationReadNotice::Pending:
            source.pending.fetch_add(1);
            break;
        case PathClassificationReadNotice::Incomplete:
            source.incomplete.fetch_add(1);
            break;
        case PathClassificationReadNotice::Acknowledged:
            source.acknowledged.fetch_add(1);
            break;
        case PathClassificationReadNotice::CancellationRequested:
            source.cancellations.fetch_add(1);
            break;
        case ::PathClassificationReadNotice::Count:
        case ::PathClassificationReadNotice::Destroyed:
        case ::PathClassificationReadNotice::Immediate:
        case ::PathClassificationReadNotice::IssueFailed:
        case ::PathClassificationReadNotice::Unissued:
        default:
            return;
        }
        if (notice == PathClassificationReadNotice::Pending && source.issueRelease)
            source.issueHeld.store(true); // Publish the controlled hold before the owner can signal readiness.
        if (!PostThreadMessageW(source.ownerThread, kReadNotice, static_cast<WPARAM>(notice), 0))
            source.posted.store(false);
        if (notice == PathClassificationReadNotice::Pending && source.issueRelease)
        {
            if (WaitForSingleObject(source.issueRelease, kFailureBound) != WAIT_OBJECT_0)
                source.posted.store(false);
            source.issueHeld.store(false);
        }
    }
    static void Ready(void* opaque, std::uint64_t readCycle) noexcept
    {
        auto& source = *static_cast<Source*>(opaque);
        if (source.issueHeld.load() && readCycle == source.expectedEarlyCycle.load())
        {
            source.earlyReady.fetch_add(1);
            if (!PostThreadMessageW(source.ownerThread, kReadyNotice, 0, 0))
                source.posted.store(false);
        }
    }
};
#pragma warning(pop)

// One independent compute job writes the literal bytes needed by the suspended original native read.
// On a one-worker scheduler it cannot run if that read blocks the compute lane waiting for the bytes.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class LanePing final : public Job
{
  public:
    LanePing(HANDLE writer, std::atomic<unsigned int>& runs, bool& completed, JobResult& terminal)
        : _writer(writer), _runs(runs), _completed(completed), _terminal(terminal)
    {
    }
    JobResult Run(JobContext&) override
    {
        _runs.fetch_add(1);
        DWORD written = 0;
        if (!WriteFile(_writer, kImageHeader.data(), static_cast<DWORD>(kImageHeader.size()), &written, nullptr))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        return written == kImageHeader.size() ? JobResult{} : JobResult{JobStatus::Failed, E_UNEXPECTED};
    }
    void Completed(JobResult result) noexcept override
    {
        _terminal = result;
        _completed = true;
    }

  private:
    HANDLE const _writer; // Fixture outlives the original scheduler Close.
    std::atomic<unsigned int>& _runs;
    bool& _completed; // Owner callback only.
    JobResult& _terminal;
};
#pragma warning(pop)

enum class Mode : std::uint8_t
{
    Normal,
    Premature,
    Cancel,
    Replace,
    LateCancel,
    Shutdown,
    PrematureCancel,
    PrematureShutdown,
    EarlyReady,
    InitiallyExhausted,
    LastValidRead
};
constexpr std::array kCases{"normal-three-read-cycles",
                            "premature-event",
                            "cancel-pending",
                            "replace-pending",
                            "cancel-after-native-ack",
                            "shutdown-pending",
                            "cancel-after-premature-event",
                            "shutdown-after-premature-event",
                            "early-ready-before-issue-worker-return"};
constexpr std::array kBoundaryCases{"initially-exhausted", "last-valid-read-rearm"};
constexpr std::array kBoundaryControls{
    "one-real-compute-worker-and-hidden-host",
    "original-root-is-admitted-before-native-issue",
    "original-ticket-ordinal-and-charge-survive-until-worker-acknowledgement",
    "failed-classification-never-restores-or-publishes-detached-paths",
    "younger-command-waits-for-original-root",
    "only-actual-incomplete-read-can-request-rearm",
    "only-pending-read-needs-the-independent-compute-job",
    "last-native-read-acknowledges-once-without-cancellation",
    "new-read-refuses-before-issue-after-exact-successful-prefix",
    "original-fails-and-younger-command-succeeds",
    "literal-inverted-pixels-and-single-history-entry",
    "entire-payload-retires-on-mta-worker",
    "hidden-host-presents-no-frames",
    "all-original-owners-retire-at-shutdown",
    "exact-arithmetic-overflow-receipt",
    "rearm-preserves-last-identity-and-never-wraps",
    "second-shutdown-is-idempotent",
};
constexpr std::array kControls{
    "one-real-compute-worker-and-hidden-host",
    "first-owner-root-is-budgeted-and-unduplicated",
    "pending-read-holds-original-ticket-ordinal-receipt-and-input-charge",
    "detached-input-retains-owner-budget",
    "younger-command-waits-for-the-original-root",
    "premature-event-never-acknowledges-native-read",
    "independent-job-runs-on-the-only-compute-lane",
    "native-result-is-acknowledged-exactly-once",
    "original-order-path-storage-is-restored-without-copy",
    "exact-terminal-and-younger-outcomes",
    "literal-history-result-or-unchanged-revision",
    "whole-read-payload-retires-on-mta-worker",
    "hidden-host-presents-no-frames",
    "final-events-tickets-input-and-memory-retire",
};
} // namespace

int Application::RunStandalonePathReadContinuationSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0, failures = 0, boundaryChecks = 0;
    constexpr std::array resourceCases{"event-creation", "wait-registration", "route-attachment"};
    for (unsigned int caseIndex = 0; caseIndex < resourceCases.size(); ++caseIndex)
    {
        struct FaultState final
        {
            PathReadWaitFault failAt;
            unsigned int visits = 0;
            bool exact = true;
        } fault{static_cast<PathReadWaitFault>(caseIndex)};
        ApplicationProcessingTestProbe processing;
        processing.workerCount = 1;
        processing.pathReadWait = {&fault, [](void* opaque, PathReadWaitFault point) noexcept -> std::int32_t
                                   {
                                       auto& fault = *static_cast<FaultState*>(opaque);
                                       fault.exact = fault.exact && static_cast<unsigned int>(point) == fault.visits;
                                       ++fault.visits;
                                       return point == fault.failAt ? E_ACCESSDENIED : S_OK;
                                   }};
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto cleanup = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        owner->_processingTestProbe = &processing;
        const auto expect = [&](bool condition, const char* control) noexcept
        {
            ++checks;
            failures += condition ? 0u : 1u;
            std::printf("%s application-path-read-startup.%s.%s\n", condition ? "PASS" : "FAIL",
                        resourceCases[caseIndex], control);
        };
        expect(owner->InitializeProcessing() == E_ACCESSDENIED, "exact-reached-resource-refusal");
        expect(!owner->_utilityReadbackInitialized && !owner->_utilityReadback.HasPending() &&
                   !owner->_utilityReadback.HasReady(),
               "failed-native-read-prefix-never-acquires-later-utility-route");
        expect(fault.exact && fault.visits == caseIndex + 1, "only-the-successful-prefix-is-acquired");
        expect(owner->_jobs.WorkerCount() == 1 && owner->_jobs.WakeHandle() && DeferredReleaseDetail::sink.enqueue &&
                   !owner->_window && !owner->_chrome,
               "original-pool-and-sink-survive-the-partial-startup");
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_jobs.WakeHandle() && owner->_jobs.WorkerCount() == 0 &&
                   !owner->_pathReadWait.EventForWorker() && !owner->_pathReadWait.HasPending() &&
                   !owner->_pathReadWait.HasReady() && !DeferredReleaseDetail::sink.enqueue,
               "shutdown-joins-and-closes-the-entire-acquired-prefix");
        const bool attached = owner->_ownerWork.Attach(owner->_pathReadWait);
        expect(attached && owner->_ownerWork.Detach(owner->_pathReadWait), "partial-native-route-is-detached");
        const bool utilityAttached = owner->_ownerWork.Attach(owner->_utilityReadback);
        expect(utilityAttached && owner->_ownerWork.Detach(owner->_utilityReadback) &&
                   !owner->_utilityReadbackInitialized,
               "failed-native-read-prefix-leaves-no-utility-registration");
        owner->Shutdown();
        expect(owner->_shutdownComplete && fault.visits == caseIndex + 1 && owner->_jobs.ReservedMemoryBytes() == 0 &&
                   owner->_jobs.ReleasingMemoryBytes() == 0,
               "second-shutdown-does-not-repeat-resources-or-faults");
    }
    for (unsigned int caseIndex = 0; caseIndex < kCases.size() + kBoundaryCases.size(); ++caseIndex)
    {
        const auto mode = static_cast<Mode>(caseIndex);
        const bool boundary = caseIndex >= kCases.size();
        const bool initiallyExhausted = mode == Mode::InitiallyExhausted;
        const auto caseName = boundary ? kBoundaryCases[caseIndex - kCases.size()] : kCases[caseIndex];
        const bool premature = mode == Mode::Premature || mode == Mode::PrematureCancel ||
                               mode == Mode::PrematureShutdown || mode == Mode::EarlyReady ||
                               mode == Mode::LastValidRead;
        const bool shuttingDown = mode == Mode::Shutdown || mode == Mode::PrematureShutdown;
        const bool cancelled = mode == Mode::Cancel || mode == Mode::PrematureCancel;
        const auto expect = [&](bool condition, unsigned int control) noexcept
        {
            ++checks;
            boundaryChecks += boundary ? 1u : 0u;
            failures += condition ? 0u : 1u;
            std::printf("%s %s.%s.%s\n", condition ? "PASS" : "FAIL",
                        boundary ? "application-path-read-boundary" : "application-path-read", caseName,
                        boundary ? kBoundaryControls[control] : kControls[control]);
        };
        TestFile first, placeholder, last;
        TestPipe pipe;
        Source source;
        wil::unique_event_nothrow issueRelease;
        if (mode == Mode::EarlyReady)
        {
            if (FAILED(issueRelease.create(wil::EventOptions::ManualReset)))
                return 5;
            source.issueRelease = issueRelease.get();
        }
        source.pipe = &pipe;
        source.pipePath = placeholder.Path();
        ApplicationProcessingTestProbe processing;
        processing.workerCount = 1; // Startup-only override; production retains its existing automatic count.
        processing.pathReadWait = {&source, nullptr, Source::Ready,
                                   boundary ? (initiallyExhausted ? UINT64_MAX : UINT64_MAX - 2) : 0};
        // These borrowed ping values outlive the shutdown guard, including setup/timeout failure exits.
        bool pingCompleted = false;
        JobResult pingResult{JobStatus::Failed, E_UNEXPECTED};
        std::atomic<unsigned int> pingRuns{0};
        struct JoinedFacts final
        {
            Application* owner = nullptr;
            Source* source = nullptr;
            Guid revision;
            std::uint64_t inputBytes = 0;
            HANDLE readEvent = nullptr;
            bool intact = false;
            bool originalReadHeld = false;
            unsigned int calls = 0;
        } joined;
        ApplicationShutdownTestProbe retirement;
        retirement.checkContext = &joined;
        retirement.checkJoined = [](void* opaque) noexcept
        {
            auto& joined = *static_cast<JoinedFacts*>(opaque);
            ++joined.calls;
            const auto& document = joined.owner->_session.Document();
            joined.intact = document && document->width == 2 && document->height == 1 && document->layers.size() == 1 &&
                            document->layers.front().asset &&
                            document->layers.front().asset->image.ByteSize() == kPixels.size() &&
                            joined.owner->_session.history.CurrentRevision() == joined.revision;
            if (joined.intact)
                for (std::size_t i = 0; i < kPixels.size(); ++i)
                    joined.intact = joined.intact && document->layers.front().asset->image.Data()[i] == kPixels[i];
            DWORD handleFlags = 0;
            joined.originalReadHeld = joined.source->pending.load() == 1 && joined.source->acknowledged.load() == 0 &&
                                      joined.source->destroyed.load() == 0 &&
                                      joined.owner->_jobs.ReservedMemoryBytes() >= joined.inputBytes + 96 * 1024 &&
                                      GetHandleInformation(joined.readEvent, &handleFlags);
        };
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        joined.owner = owner.get();
        joined.source = &source;
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                if (issueRelease)
                    (void)SetEvent(issueRelease.get());
                owner->Shutdown();
                (void)PumpMessages(); // Consume this independent hidden owner's WM_QUIT and final read notices.
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        owner->_processingTestProbe = &processing;
        owner->_shutdownTestProbe = &retirement;
        owner->_pathReadSource = {&source, Source::Open, Source::Observe};
        owner->_forcedImageOpen = ImageOpenChoice::Cancel;
        if (!first.Valid() || !placeholder.Valid() || !last.Valid() || !pipe.Initialize() ||
            FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
        {
            std::printf("FAIL application-path-read.%s.setup\n", caseName);
            return 5;
        }
        joined.readEvent = static_cast<HANDLE>(owner->_pathReadWait.EventForWorker());
        owner->BindCompositionDevice();
        owner->_session.CreateDocument(2, 1);
        auto layer = ImageLayer::Blank(L"Native read history source", {2, 1});
        layer.asset =
            RasterAsset{Raster::Make(2, 1, RasterFormat::Bgra8888, kPixels.data(), kPixels.size()), {}, layer.name};
        const auto id = layer.id;
        DocumentFixture fixture;
        fixture.id = Guid::Generate();
        fixture.width = 2;
        fixture.height = 1;
        fixture.layers.push_back(std::move(layer));
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), id);
        owner->_session.history.MarkSaved();
        owner->DocumentChanged();
        const auto revision = owner->_session.history.CurrentRevision();
        joined.revision = revision;
        const auto historyCount = owner->_session.history.UndoCount();
        const auto presents = owner->_renderer.PresentCount();
        const auto initialReadCycle = owner->_pathReadWait.ReadCycleForTest();
        expect(owner->_jobs.WorkerCount() == 1 && owner->_renderer.IsWarp() && owner->_window && owner->_chrome &&
                   !IsWindowVisible(owner->_window.get()),
               0);

        const std::array<std::wstring_view, 4> paths{L"", first.Path(), placeholder.Path(), last.Path()};
        const auto receipt = owner->QueueHostPaths(paths, HostPathAction::OpenPaths);
        (void)RunUiActions(owner->_jobs);
        if (!owner->_activeHostRequest || !owner->_pathClassificationTicket)
        {
            std::printf("FAIL application-path-read.%s.original-root-start\n", caseName);
            return 5;
        }
        const auto ordinal = owner->_activeHostRequest->ordinal;
        const auto ticket = owner->_pathClassificationTicket.value();
        const auto* originalPaths = std::get_if<HostPathsInput>(&owner->_activeHostRequest->input.Input());
        const auto* originalStorage = originalPaths ? originalPaths->paths.data() : nullptr;
        const auto inputBytes = owner->_activeHostRequest->input.ReservedInputBytes();
        joined.inputBytes = inputBytes;
        expect(originalStorage && originalPaths->paths.size() == 4 && inputBytes > 0 &&
                   owner->_hostInputBudget.Used() == inputBytes && receipt.Read().outcome == HostOutcome::Pending,
               1);
        const auto younger = owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
        const auto totalInputBytes = owner->_hostInputBudget.Used();
        bool pendingObserved = false, held = false, detached = false, youngerHeld = false;
        bool prematureObserved = false, restored = false, classified = false, stopNow = false, earlyReadyHeld = false;
        bool pingSubmitted = false;
        bool lastIdentityStable = true;
        if (initiallyExhausted)
        {
            held = owner->_pathClassificationTicket == ticket && owner->_activeHostRequest->ordinal == ordinal &&
                   owner->_jobs.ReservedMemoryBytes() >= inputBytes + 96 * 1024 &&
                   receipt.Read().outcome == HostOutcome::Pending &&
                   owner->_session.history.CurrentRevision() == revision;
            youngerHeld = younger.Read().outcome == HostOutcome::Pending;
        }
        const auto submitPing = [&]() noexcept
        {
            if (pingSubmitted)
                return;
            std::unique_ptr<Job> ping = std::make_unique<LanePing>(pipe.Writer(), pingRuns, pingCompleted, pingResult);
            JobTicket ignored;
            pingSubmitted = SUCCEEDED(owner->_jobs.Submit(ping, {JobClass::Background, 0, 1024}, ignored));
        };
        struct PumpContext final
        {
            void* state;
            void (*notice)(void*, MSG&) noexcept;
            Application* owner;
        };
        const auto onNotice = [&](MSG& message) noexcept
        {
            if (message.message == kReadyNotice)
            {
                earlyReadyHeld = mode == Mode::EarlyReady && source.issueHeld.load() && source.earlyReady.load() == 1 &&
                                 owner->_pathClassificationTicket == ticket && owner->_pathReadWait.HasPending() &&
                                 !owner->_pathReadWait.HasReady() && receipt.Read().outcome == HostOutcome::Pending;
                if (!SetEvent(issueRelease.get()))
                    stopNow = true;
                return;
            }
            const auto notice = static_cast<PathClassificationReadNotice>(message.wParam);
            if (notice == PathClassificationReadNotice::Pending)
            {
                pendingObserved = true;
                if (mode == Mode::LastValidRead)
                    lastIdentityStable = lastIdentityStable && owner->_pathReadWait.ReadCycleForTest() == UINT64_MAX;
                held = owner->_pathClassificationTicket == ticket && owner->_activeHostRequest &&
                       owner->_activeHostRequest->ordinal == ordinal &&
                       receipt.Read().outcome == HostOutcome::Pending &&
                       owner->_jobs.ReservedMemoryBytes() >= inputBytes + 96 * 1024 &&
                       owner->_session.history.CurrentRevision() == revision;
                const auto* selected = owner->_activeHostRequest
                                           ? std::get_if<HostPathsInput>(&owner->_activeHostRequest->input.Input())
                                           : nullptr;
                detached = selected && selected->paths.empty() &&
                           owner->_activeHostRequest->input.ReservedInputBytes() == inputBytes &&
                           totalInputBytes > inputBytes && owner->_hostInputBudget.Used() == totalInputBytes;
                youngerHeld = younger.Read().outcome == HostOutcome::Pending;
                if (mode == Mode::Cancel)
                    owner->CancelHostContinuations();
                else if (mode == Mode::Replace)
                    owner->_session.CreateDocument(3, 2);
                else if (mode == Mode::Shutdown)
                    stopNow = true;
                else if (premature)
                {
                    if (mode == Mode::EarlyReady)
                        source.expectedEarlyCycle.store(owner->_pathReadWait.ReadCycleForTest());
                    if (!SetEvent(static_cast<HANDLE>(owner->_pathReadWait.EventForWorker())))
                        stopNow = true;
                    return; // Do not supply bytes until the actual incomplete acknowledgement is reached.
                }
                if (mode != Mode::Cancel && mode != Mode::Shutdown)
                    submitPing();
            }
            else if (notice == PathClassificationReadNotice::Incomplete && premature)
            {
                if (mode == Mode::LastValidRead)
                    lastIdentityStable = lastIdentityStable && owner->_pathReadWait.ReadCycleForTest() == UINT64_MAX;
                prematureObserved = source.acknowledged.load() == 0 && receipt.Read().outcome == HostOutcome::Pending &&
                                    owner->_pathClassificationTicket == ticket && owner->_activeHostRequest &&
                                    owner->_activeHostRequest->ordinal == ordinal;
                if (mode == Mode::PrematureCancel)
                    owner->CancelHostContinuations();
                else if (mode == Mode::PrematureShutdown)
                    stopNow = true;
                else
                    submitPing();
            }
            else if (notice == PathClassificationReadNotice::Acknowledged && mode == Mode::LateCancel)
                owner->_pathClassificationCancelled = true;
        };
        PumpContext context{const_cast<void*>(static_cast<const void*>(&onNotice)),
                            [](void* state, MSG& message) noexcept
                            { (*static_cast<const decltype(onNotice)*>(state))(message); }, owner.get()};
        const auto dispatch = [](void* opaque, void* native) noexcept
        {
            auto& context = *static_cast<PumpContext*>(opaque);
            auto& message = *static_cast<MSG*>(native);
            if (message.message == kReadNotice || message.message == kReadyNotice)
                context.notice(context.state, message);
            else
                context.owner->DispatchQueuedMessage(message);
        };
        const auto deadline = GetTickCount64() + kFailureBound;
        bool settled = false;
        for (;;)
        {
            if (PumpMessages(dispatch, &context).quit)
                break;
            if (stopNow)
                break;
            (void)DrainCompletions(owner->_jobs);
            if (owner->_pathClassifications.status == PathClassificationStatus::Ok)
            {
                classified = owner->_pathClassifications.count == 4;
                for (std::size_t i = 0; i < 4; ++i)
                    classified =
                        classified && owner->_pathClassifications.classifications[i] == PathClassification::Image;
                const auto* selected = owner->_activeHostRequest
                                           ? std::get_if<HostPathsInput>(&owner->_activeHostRequest->input.Input())
                                           : nullptr;
                restored = selected && selected->paths.size() == 4 && selected->paths.data() == originalStorage;
                if (restored)
                    for (std::size_t i = 0; i < 4; ++i)
                        restored = restored && selected->paths[i] == paths[i];
            }
            (void)RunUiActions(owner->_jobs);
            if (owner->_jobs.IsIdle())
            {
                settled = true;
                break;
            }
            if (owner->_jobs.HasUiWork() || owner->_ownerWork.HasReady())
                continue;
            const auto now = GetTickCount64();
            if (now >= deadline)
                break;
            const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
            const auto waited = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                            MWMO_INPUTAVAILABLE);
            if (waited != WAIT_OBJECT_0 && waited != WAIT_OBJECT_0 + 1)
                break;
        }
        expect((initiallyExhausted || pendingObserved) && held, 2);
        expect(boundary ? !classified && !restored && (initiallyExhausted ? source.destroyed.load() == 1 : detached)
                        : detached,
               3);
        expect(youngerHeld, 4);
        // Cancellation may need an additional real incomplete acknowledgement before its IO event arrives.
        expect((!premature || (prematureObserved && source.incomplete.load() >= 1)) &&
                   (mode != Mode::EarlyReady || earlyReadyHeld),
               5);
        const bool pingExpected = !cancelled && !shuttingDown && !initiallyExhausted;
        expect(pingExpected
                   ? pingSubmitted && pingCompleted && pingRuns.load() == 1 && pingResult.status == JobStatus::Finished
                   : pingRuns.load() == 0,
               6);
        if (!settled && issueRelease)
            (void)SetEvent(issueRelease.get()); // Release the controlled producer before any shutdown join.
        if (shuttingDown || !settled)
            owner->Shutdown(); // Actual outstanding native operation must acknowledge on the final MTA worker.
        expect(initiallyExhausted
                   ? source.pending.load() == 0 && source.acknowledged.load() == 0 && source.cancellations.load() == 0
                   : source.pending.load() == 1 && source.acknowledged.load() == 1 &&
                         source.cancellations.load() == (cancelled || shuttingDown ? 1u : 0u),
               7);
        const bool threeNativeReads = source.opens.load() == 3 && source.issueFailures.load() == 0 &&
                                      source.immediate.load() + source.nativeAcknowledgements.load() == 3;
        expect(boundary ? !restored && !classified && source.opens.load() == (initiallyExhausted ? 1u : 3u) &&
                              source.issueFailures.load() == 0 &&
                              source.immediate.load() + source.nativeAcknowledgements.load() ==
                                  (initiallyExhausted ? 0u : 2u)
               : mode == Mode::Normal || mode == Mode::Premature || mode == Mode::EarlyReady
                   ? restored && classified && threeNativeReads
                   : !classified,
               8);
        const auto expected = boundary                ? HostOutcome::Failed
                              : mode == Mode::Replace ? HostOutcome::Stale
                                                      : HostOutcome::Cancelled;
        const auto expectedYounger = boundary || mode == Mode::Normal || mode == Mode::Premature ||
                                             mode == Mode::EarlyReady || mode == Mode::LateCancel
                                         ? HostOutcome::Succeeded
                                     : mode == Mode::Replace ? HostOutcome::Stale
                                                             : HostOutcome::Cancelled;
        expect((settled || (shuttingDown && stopNow)) && receipt.Read().outcome == expected &&
                   younger.Read().outcome == expectedYounger,
               9);
        bool pixels = shuttingDown ? joined.intact && joined.originalReadHeld && joined.calls == 1 : true;
        const bool inverted = expectedYounger == HostOutcome::Succeeded;
        if (mode == Mode::Replace)
            pixels = owner->_session.Document() && owner->_session.Document()->width == 3 &&
                     owner->_session.Document()->height == 2;
        else if (!shuttingDown)
        {
            const auto* active = owner->_session.Document() ? owner->_session.Document()->layers.FindKey(id) : nullptr;
            pixels = active && active->asset && active->asset->image.Width() == 2 &&
                     active->asset->image.Height() == 1 && active->asset->image.Format() == RasterFormat::Bgra8888;
            std::array<std::uint8_t, kPixels.size()> actualPixels{};
            if (pixels)
            {
                active->asset->image.ReadRegion(0, 0, 2, 1, actualPixels.data(), static_cast<int>(actualPixels.size()),
                                                0);
                for (std::size_t i = 0; i < kPixels.size(); ++i)
                    pixels = pixels &&
                             actualPixels[i] ==
                                 (inverted && i % 4 != 3 ? static_cast<std::uint8_t>(255 - kPixels[i]) : kPixels[i]);
            }
            pixels = pixels && owner->_session.history.UndoCount() == historyCount + (inverted ? 1 : 0) &&
                     (inverted ? owner->_session.history.UndoName() == L"Invert"
                               : owner->_session.history.CurrentRevision() == revision);
        }
        expect(pixels, 10);
        expect(source.destroyed.load() == 1 && source.offOwner.load() && source.mta.load() && source.posted.load(), 11);
        expect(owner->_renderer.PresentCount() == presents, 12);
        if (boundary)
        {
            expect(receipt.Read().error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)),
                   14);
            expect(initialReadCycle == (initiallyExhausted ? UINT64_MAX : UINT64_MAX - 2) &&
                       owner->_pathReadWait.ReadCycleForTest() == UINT64_MAX && lastIdentityStable &&
                       !owner->_pathReadWait.HasPending() && !owner->_pathReadWait.HasReady() &&
                       (initiallyExhausted
                            ? source.incomplete.load() == 0 &&
                                  WaitForSingleObject(static_cast<HANDLE>(owner->_pathReadWait.EventForWorker()), 0) ==
                                      WAIT_TIMEOUT
                            : prematureObserved && source.incomplete.load() >= 1),
                   15);
        }
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_jobs.WakeHandle() && owner->_jobs.ReservedMemoryBytes() == 0 &&
                   owner->_jobs.ReleasingMemoryBytes() == 0 && owner->_hostInputBudget.Used() == 0 &&
                   !owner->_pathClassificationTicket && !owner->_pathReadWait.HasPending() &&
                   !owner->_pathReadWait.HasReady() && !owner->_pathReadWait.EventForWorker(),
               13);
        if (boundary)
        {
            owner->Shutdown();
            expect(owner->_shutdownComplete && source.destroyed.load() == 1 &&
                       owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                       !owner->_pathReadWait.EventForWorker() && !owner->_pathReadWait.HasReady() &&
                       !owner->_pathReadWait.HasPending(),
                   16);
        }
        (void)PumpMessages();
    }
    std::printf("SUMMARY application-path-read cases=9 startupCases=3 checks=%u expected=150 failures=%u\n",
                checks - boundaryChecks, failures);
    std::printf("SUMMARY application-path-read-boundary cases=2 checks=%u expected=34 failures=%u\n", boundaryChecks,
                failures);
    return checks == 184 && boundaryChecks == 34 && !failures ? 0 : 5;
}
