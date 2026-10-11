#pragma once

#include "Executor.h"
#include "MemoryBudget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

// This boundary deliberately has no Windows types. Jobs own immutable input and result values. Worker bodies
// never access a session, a window, chrome or an immediate graphics context. The explicitly enabled recording
// lane may own one private deferred context under RedPrism's recording contract; no graphics type crosses here.
enum class JobClass : unsigned int
{
    InteractivePreview,
    Refinement,
    Stroke,
    Selection,
    Commit,
    Io,
    Background,
    Count
};

enum class JobStatus
{
    Finished,
    Cancelled,
    Failed
};

struct JobResult final
{
    JobStatus status = JobStatus::Finished;
    std::int32_t error = 0; // HRESULT when status is Failed
};

// An explicitly enabled single recorder may wait for bounded command transport capacity. It shares job
// records, admission, sequencing and terminal acknowledgement; normal CPU execution remains independent.
enum class JobExecutionLane : unsigned int
{
    Compute,
    GpuRecording,
    Count
};

struct JobOptions final
{
    JobClass kind = JobClass::Background;
    // Stroke, Selection, Commit and Io are FIFO within (kind, serialKey), through UI apply/drop. Zero is a
    // conservative global key. The host supplies collision-free document/path keys, never a truncated hash.
    std::uint64_t serialKey = 0;
    // Additional allocation peak for the complete graph. Immutable inputs already resident are not charged
    // again. The reservation survives Run and UI apply/drop; children borrow it instead of reserving again.
    std::uint64_t memoryBytes = 0;
    // Opt in only when external mapped/device ownership can survive a worker's terminal result. The owner
    // acknowledgement hook retains this exact record and reservation on success, failure and cancellation.
    bool deferredRetirement = false;
    // Cache replacement may depend on ownership that remains resident until a successful replacement.
    // Refuse insufficient admission once instead of waiting on that retained cache. Ordinary jobs wait.
    bool allowMemoryWaiting = true;
    // Reserve one of the fixed retained-memory slots at descriptor admission, before any capture/publication.
    // Other jobs cannot consume it. Successful final RetainMemory transfers the original reservation into
    // that slot. Exact joined destruction accounting may also retain it on failure/cancellation/Close;
    // otherwise untransferred terminal records return it. It charges no extra bytes.
    bool reserveRetainedMemory = false;
    // Explicit opt-in; never run a capacity-blocked command recorder on the ordinary kernel workers.
    JobExecutionLane executionLane = JobExecutionLane::Compute;
};

struct JobTicket final
{
    std::size_t slot = 128;
    std::uint64_t generation = 0;
    [[nodiscard]] bool operator==(const JobTicket&) const noexcept = default;
};

struct JobEventTicket final
{
    std::size_t slot = 32;
    std::uint64_t generation = 0;
};

// Exact original reservation transferred from a finished job into a live immutable cache. This value owns
// accounting only: the host must detach borrowed views and defer the cache's whole graph before releasing it.
// Scheduler lifetime and slot generation prevent foreign, stale and reused-slot release.
struct RetainedMemoryTicket final
{
    std::size_t slot = 64;
    std::uint64_t generation = 0;
    std::uint64_t owner = 0;
    [[nodiscard]] bool operator==(const RetainedMemoryTicket&) const noexcept = default;
};

// Move-only reservation ownership embedded in a shared immutable CPU graph. Destroy/reset only after the
// graph's last readable bytes have been destroyed. A foreign final release publishes one fixed-slot
// acknowledgement; the owner returns admission capacity on its next drain. The endpoint and its owned wake
// handle survive scheduler Close, without retaining a scheduler, HWND, document or graphics resource.
class SharedRetainedMemoryLease final
{
  public:
    SharedRetainedMemoryLease() noexcept = default;
    ~SharedRetainedMemoryLease();
    SharedRetainedMemoryLease(const SharedRetainedMemoryLease&) = delete;
    SharedRetainedMemoryLease& operator=(const SharedRetainedMemoryLease&) = delete;
    SharedRetainedMemoryLease(SharedRetainedMemoryLease&& other) noexcept;
    SharedRetainedMemoryLease& operator=(SharedRetainedMemoryLease&& other) noexcept;
    void Reset() noexcept;
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return bool(_state);
    }
    [[nodiscard]] std::uint64_t Bytes() const noexcept
    {
        return _bytes;
    }
    // Diagnostics of live shared graphs at this endpoint, including after Close. Owner admission continues
    // to charge acknowledged graphs until its drain consumes the generation-checked acknowledgement.
    [[nodiscard]] std::uint64_t ResidentBytes() const noexcept;

  private:
    struct State;
    std::shared_ptr<State> _state;
    std::size_t _slot = 64;
    std::uint64_t _generation = 0;
    std::uint64_t _bytes = 0;
    friend class JobScheduler;
};

enum class JobNoticeKind
{
    Submitted,
    Progress,
    Retired
};

struct JobNotice final
{
    JobNoticeKind event;
    JobTicket ticket;
    JobClass kind;
    unsigned int percent = 0;
};

// Optional owner-thread observer for chrome state. Called only outside the scheduler lock; it must not invoke
// scheduler operations, wait, or pump messages. Its context outlives Close. Shutdown drops notices with callbacks.
struct JobObserver final
{
    void (*notify)(void*, JobNotice) noexcept = nullptr;
    void* context = nullptr;
};

enum class JobExecutionBoundary : std::uint8_t
{
    BodyEntered,
    BodyExited,
    BodySkipped,          // Dispatched worker stage cancelled before Run; no entry/body/exit pair.
    CancellationObserved, // First true CancelToken check inside one joined useful Run, not body exit.
};

struct JobExecutionNotice final
{
    JobExecutionBoundary boundary;
    JobTicket ticket;
    JobClass kind;
    JobExecutionLane lane;
    // Absent at entry; raw Run result (or translated bad_alloc) at exit; Cancelled for a dispatched unrun stage.
    // Neither this diagnostic nor a returned body acknowledges external native/GPU work or authorizes apply.
    std::optional<JobResult> result;
};

// Opt-in diagnostics installed before admission. Callbacks run concurrently on compute/recording workers,
// outside scheduler locks, and must be bounded, nonthrowing and thread-safe. They may publish fixed atomic
// samples and signal their own borrowed event; no scheduler calls, waits, message pumping or document/UI access.
// The context outlives Close. Default captures no clock, allocates no state and creates no wake-up.
// BodyExited precedes RetireWorkerStage and owner apply/drop; it measures Run, not final retirement or an
// isolated kernel cancellation-check interval. Resumed original tickets can produce several body pairs.
// Descriptors that retire without dispatch and queued stages dropped by Close emit no body notice.
// A cancelled queued stage that is dispatched still emits BodySkipped.
struct JobExecutionObserver final
{
    void (*notify)(void*, JobExecutionNotice) noexcept = nullptr;
    void* context = nullptr;
    // Separately opt in to the first actual true cancellation check in each useful Run. Existing body-only
    // observers receive exactly their original boundaries. No clock/TLS diagnostic work on false checks.
    void (*cancellationObserved)(void*, JobExecutionNotice) noexcept = nullptr;
};

class JobContext final
{
  public:
    Executor& executor;
    CancelToken cancellation;
    void* const shutdownEvent; // borrowed manual-reset HANDLE, valid only during Run
    const std::uint64_t reservedMemoryBytes;
    const bool deferredRetirement;

    // Concurrent calls coalesce without waiting. A report never allocates. Percentages are monotonic, and
    // publication is limited to one per 100 ms. Run must join every producer before returning.
    void ReportProgress(unsigned int percent) const noexcept;

    using Report = void (*)(void*, unsigned int) noexcept;
    JobContext(Executor& execution, CancelToken token, void* event, Report report, void* context,
               std::uint64_t reserved = 0, bool externalRetirement = false) noexcept
        : executor(execution), cancellation(token), shutdownEvent(event), reservedMemoryBytes(reserved),
          deferredRetirement(externalRetirement), _report(report), _context(context)
    {
    }

  private:
    Report _report;
    void* _context;
};

class JobScheduler;

class Job
{
  public:
    virtual ~Job() = default;
    // Only bad_alloc is translated by the runner, to a failed operation. Other unexpected exceptions retain
    // the process crash policy. Kernels themselves do not catch. Compute/recorder producers join before return.
    // The explicit native-read continuation may retain a pending OS operation: its original admitted job
    // holds file/buffer/OVERLAPPED/event and complete reservation through actual native acknowledgement.
    // Readiness or a stop request never acknowledges that operation; no pending read blocks a compute lane.
    [[nodiscard]] virtual JobResult Run(JobContext& context) = 0;
    // Worker-only, after this stage's useful producers have joined and cancellation/bad_alloc has been
    // normalized, before publishing completion. Called for deferredRetirement only, even when cancellation
    // skipped Run entirely. This is bounded nonthrowing retirement of already-detached owned graphs; never
    // useful work, owner callbacks, context commands, new producers or fence waiting. Native-read retirement
    // may request exact cancellation and nonblocking acknowledgement, yielding while still incomplete.
    // Only final joined shutdown may wait for that existing operation before destroying its OS-owned buffer.
    // The original record,
    // FIFO and complete reservation remain held. Owner resumes a retirement stage only after external users
    // acknowledge. Close instead retains the existing joined owner/final-worker cleanup protocol.
    virtual void RetireWorkerStage(JobContext&, JobResult) noexcept {}
    // Exact final joined shutdown callback only, before this wrapper's owner-affine detachment. Every
    // compute/recorder producer has joined. May transfer its descriptor-reserved accounting into a
    // preallocated retirement owner; cannot capture sources, apply, publish, resume, submit or pump.
    // Normal completion uses the exact terminal Completed callback instead. Default has no accounting owner.
    virtual void RetainJoinedRetirement(JobScheduler&, JobTicket) noexcept {}
    // Owner thread after a successful worker stage, before final apply. Returning true retains this job, its
    // ticket, reservation and FIFO position until Resume. Failed/cancelled stages skip this hook. Resume may
    // be requested here; the next worker stage cannot start until this hook returns. A false return finalizes.
    [[nodiscard]] virtual bool DeferCompletion(JobResult, JobTicket) noexcept
    {
        return false;
    }
    // Owner-only, before DeferCompletion/Completed, after compute/recorder producers returned. A retained
    // native read may still await its explicit OS acknowledgement. Called only for
    // options.deferredRetirement, including failed/cancelled stages. True retains the original suspended ticket,
    // FIFO and whole reservation until an external acknowledgement calls Resume. The hook must preserve a
    // terminal failure in its own state, acknowledge unrun mapped loans, and never restart cancelled useful work.
    // False finalizes even if Resume was requested; shutdown joins producers and drops hooks/callbacks.
    [[nodiscard]] virtual bool DeferRetirement(JobResult, JobTicket) noexcept
    {
        return false;
    }
    // UI thread, outside window/control dispatch. Must not pump messages, show a modal or wait for work.
    // Called once for every drained job, including failure/cancellation. Shutdown drops callbacks entirely.
    // A posted UI action must own the values it needs: this Job is destroyed before that action runs.
    virtual void Completed(JobResult result) noexcept = 0;
    // May run concurrently with Run. Read only the supplied percentage, immutable inputs, published atomics
    // and UI-owned state; never read or mutate the worker's unfinished result.
    virtual void Progress(unsigned int) noexcept {}
};

class UiAction
{
  public:
    virtual ~UiAction() = default;
    // UI thread, at most one action per outer loop pass. Revalidate the captured session/target here before
    // opening a modal. A modal may dispatch messages but cannot recursively drain completions or actions.
    virtual void Run() noexcept = 0;
};

// One borrowed owner-thread source for asynchronous device work. Waiting work prevents idle settlement but
// does not request an owner pass. Event producers publish readiness before NotifyOwnerWork, and are joined
// before unregister/Close. Pump runs outside message dispatch and must not wait, show a modal or drain again.
class OwnerWorkPump
{
  public:
    virtual ~OwnerWorkPump() = default;
    [[nodiscard]] virtual bool HasPending() const noexcept = 0;
    [[nodiscard]] virtual bool HasReady() const noexcept = 0;
    virtual void Pump() noexcept = 0;
};

class JobScheduler final
{
  public:
    static constexpr std::size_t kCapacity = 128;
    static constexpr std::size_t kUiActionCapacity = 8;
    static constexpr std::size_t kRetainedMemoryCapacity = 64;
    JobScheduler() noexcept;
    ~JobScheduler();
    JobScheduler(const JobScheduler&) = delete;
    JobScheduler& operator=(const JobScheduler&) = delete;

    [[nodiscard]] std::int32_t Initialize(unsigned int threads = 0, JobObserver observer = {}, MemorySource memory = {},
                                          JobExecutionObserver execution = {}) noexcept;
    // Owner startup only, outside callbacks and before any admitted record. Failure leaves recording disabled.
    // No second scheduler or memory budget. GPU APIs remain private to the opted-in RedPrism recorder job.
    [[nodiscard]] std::int32_t EnableGpuRecordingLane() noexcept;
    // Explicitly bind large last-reference releases on this owner thread to this scheduler. Only one scheduler
    // per owner may bind. Close retains the sink through owner cleanup after join, then acknowledges final
    // worker retirement before teardown; a full job queue cannot refuse a release.
    [[nodiscard]] bool EnableDeferredRelease() noexcept;
    [[nodiscard]] bool EnableMemoryRecycling() noexcept;
    [[nodiscard]] std::uint64_t ReservedMemoryBytes() const noexcept;
    [[nodiscard]] std::uint64_t ReleasingMemoryBytes() const noexcept;
    // Completed callback only for the exact successful, uncancelled job. Transfers its entire original
    // reservation without a new admission/query/allocation. Fixed-capacity cache records do not count as
    // pending useful work; an otherwise settled scheduler stays idle while their bytes remain charged.
    [[nodiscard]] std::int32_t RetainMemory(JobTicket job, RetainedMemoryTicket& retained) noexcept;
    // Same exact successful-callback transfer and same 64-slot capacity; no new query/allocation/admission.
    // The lease itself is unique, while its containing immutable graph may have shared foreign readers.
    [[nodiscard]] std::int32_t RetainMemory(JobTicket job, SharedRetainedMemoryLease& retained) noexcept;
    // Explicit immutable publication boundary in the exact successful DeferCompletion callback only.
    // Requires descriptor-time retained-slot reservation and deferred retirement. Useful worker/recorder/map
    // users must have joined; only allocation-free original-record retirement may follow. Transfer the WHOLE
    // original admission before exposing any shared rows/display/root. A preallocated shared graph contains
    // this unique lease and every still-readable published/retiring CPU owner holds that graph until destruction.
    // No fresh query/allocation/admission or early refund. Failure preserves the record/output. This call
    // supplies accounting, not a publication authority, worker acknowledgement, GPU completion or history permit.
    [[nodiscard]] std::int32_t RetainPublicationMemory(JobTicket job, SharedRetainedMemoryLease& retained) noexcept;
    // Exact terminal callback or the joined Close retirement hook, including failed/cancelled jobs. Transfers
    // the entire original reservation to a preallocated owner with a descriptor-reserved slot. This is only
    // accounting for final destruction/weak-control ownership; no useful-work or publication authority and
    // no GPU acknowledgement. Caller joins/retains all external users through their real last-use barrier.
    [[nodiscard]] std::int32_t RetainRetirementMemory(JobTicket job, SharedRetainedMemoryLease& retained) noexcept;
    // Owner-only identity of the live shared endpoint. A closed or reconstructed scheduler cannot own
    // an older lease, even if its wrapper address and first job slot/generation have been reused.
    // This query performs no admission, transfer or publication authorization.
    [[nodiscard]] bool OwnsRetainedMemoryEndpoint(const SharedRetainedMemoryLease& retained) const noexcept;
    // Owner-only monotonic growth for a live unique lease, while its containing graph is exclusively idle.
    // Samples current physical/commit memory before new graph allocations. The original retained slot,
    // generation and last-reader acknowledgement stay unchanged; no replacement ticket or early refund.
    // No-op growth queries nothing. A failed sample/admission/revalidation changes no charge or lease bytes.
    [[nodiscard]] std::int32_t IncreaseRetainedReservation(SharedRetainedMemoryLease& retained,
                                                           std::uint64_t totalBytes) noexcept;
    // Owner only, once the borrowed cache is detached and its whole graph is queued for worker retirement.
    // The release sink's in-flight bytes then protect admission until destruction is acknowledged. Close's
    // owner hook performs this cleanup before final worker retirement; Close invalidates residual tickets.
    [[nodiscard]] bool ReleaseRetainedMemory(RetainedMemoryTicket retained) noexcept;

    // Event sources call this without waiting, only while the initialized scheduler is alive. The caller joins
    // external producers before Close; Close joins its own worker/wait sources. The next owner drain retries
    // admission from a fresh snapshot.
    void NotifyMemoryChanged() noexcept;
    // Owner-thread operations. Failed submission leaves ownership with the caller. Queue capacity includes
    // running and completed-but-undrained jobs. A ticket never identifies a reused slot's newer generation.
    [[nodiscard]] std::int32_t Submit(std::unique_ptr<Job>& job, JobOptions options, JobTicket& ticket) noexcept;
    [[nodiscard]] bool Cancel(JobTicket ticket) noexcept;
    // Only an exact suspended ticket can resume. Cancellation is acknowledged by a skipped worker stage and
    // one final Cancelled callback. No new record, reservation or sequence is allocated. Duplicate/stale fails.
    [[nodiscard]] bool Resume(JobTicket ticket) noexcept;
    // An acknowledged suspended stage may change only its execution lane. The original record, class/key
    // FIFO, cancellation, retained slot and complete reservation remain unchanged. Invalid/disabled lanes
    // leave the stage suspended; no new descriptor, queue or reservation is created.
    [[nodiscard]] bool Resume(JobTicket ticket, JobExecutionLane lane) noexcept;
    // Owner-only on the exact acknowledged suspended record, before a later phase creates its new owners.
    // Grow the complete peak from a fresh memory snapshot; never shrink, partially reserve, queue or wait.
    // Refusal preserves the original stage/ticket/charge. Cancellation refuses useful growth. A successful
    // later Resume borrows this new total and final retention/retirement keeps the same complete charge.
    [[nodiscard]] std::int32_t IncreaseReservation(JobTicket ticket, std::uint64_t totalBytes) noexcept;
    [[nodiscard]] bool RegisterOwnerWork(OwnerWorkPump& source) noexcept;
    [[nodiscard]] bool UnregisterOwnerWork(OwnerWorkPump& source) noexcept;
    // Thread-safe publication/wake only. External producers must be joined before Close destroys the event.
    void NotifyOwnerWork() noexcept;
    // Device/event sources share the scheduler's one wait pool (32 registrations, including memory pressure).
    // Register and Remove are owner startup/teardown operations outside callbacks; Remove joins the callback.
    // Handles/contexts remain borrowed until Remove or Close returns. Event callbacks publish only readiness.
    [[nodiscard]] std::int32_t RegisterEventWait(void* event, void (*callback)(void*) noexcept, void* context,
                                                 JobEventTicket& ticket) noexcept;
    [[nodiscard]] bool RearmEventWait(JobEventTicket ticket, std::uint32_t timeoutMs = 0) noexcept;
    [[nodiscard]] bool RemoveEventWait(JobEventTicket ticket) noexcept;
    [[nodiscard]] bool PostUiAction(std::unique_ptr<UiAction>& action) noexcept;
    // One stable, borrowed owner-thread source for host continuations. Register/unregister only outside UI
    // callbacks; the source outlives registration through Unregister or Close. No ownership is transferred.
    [[nodiscard]] bool RegisterUiActionSource(UiAction& source) noexcept;
    // Coalesces without allocation, including from a completion/action callback or with a full ordinary queue.
    // Readiness is consumed before Run; self-rearm schedules a later outer pass. A waiting host must not rearm
    // until its prerequisite completes. RequestStop refuses new requests and drops source readiness.
    [[nodiscard]] bool RequestUiActionSource(UiAction& source) noexcept;
    [[nodiscard]] bool UnregisterUiActionSource(UiAction& source) noexcept;
    void RequestStop() noexcept;
    // Owner thread, outside every UI callback. Hides/cancel coordination belongs to Application. Close joins
    // producers before destroying their inputs, results, intrusive nodes, events or COM pool lifetime.
    struct CloseHooks final
    {
        void (*owner)(void*) noexcept = nullptr;
        void (*worker)(void*) noexcept = nullptr;
        void* context = nullptr;
    };
    // Borrowed context outlives this call. Hooks cannot submit, pump, wait or reenter Close. After producer join
    // each original job may retain destruction accounting, then job wrappers die and the owner detaches
    // borrowed resources while the release sink remains installed. No successful-work authority is granted.
    // The preallocated final MTA callback runs worker before release/recycle drain and teardown. No hook is run
    // for an uninitialized/already closed scheduler, and useful-work cancellation cannot suppress retirement.
    void Close(CloseHooks hooks = {}) noexcept;
    [[nodiscard]] bool IsIdle() const noexcept;
    // Actual initialized pool size for diagnostics; zero before initialization or after Close. Owner thread.
    [[nodiscard]] unsigned int WorkerCount() const noexcept;
    [[nodiscard]] bool HasUiWork() const noexcept;
    struct UiReadiness final
    {
        std::uint32_t completions = 0;
        std::uint32_t actions = 0;
        bool source = false;
        bool resumes = false;
        bool owner = false;
        bool memory = false;
    };
    // Owner-only diagnostic snapshot. It consumes no readiness and never pumps or waits.
    [[nodiscard]] UiReadiness UiReadinessForTest() const noexcept;
    [[nodiscard]] bool InUiCallback() const noexcept;
    // Owner only. Exact successful joined job acknowledgement, during DeferRetirement/DeferCompletion.
    // Progress, final Completed, UI actions, another ticket and Close confer no source-capture authority.
    [[nodiscard]] bool InAcknowledgedJobCallback(JobTicket original) const noexcept;
    // Destruction authority only: exact actual Close hook after original recorder/compute join.
    // Refuse outside that owner hook, on another ticket/thread, during ordinary completion and after Close.
    // This grants no useful work, GPU/copy/row completion, source capture or admission refund.
    [[nodiscard]] bool InJoinedRetirementCallback(JobTicket original) const noexcept;
    [[nodiscard]] void* WakeHandle() const noexcept;

    using Apply = void (*)(void*, Job&, JobResult) noexcept;

  private:
    [[nodiscard]] std::int32_t RetainMemoryImpl(JobTicket job, RetainedMemoryTicket& retained, bool publication,
                                                bool retirement = false) noexcept;
    [[nodiscard]] std::int32_t RetainSharedMemoryImpl(JobTicket job, SharedRetainedMemoryLease& retained,
                                                      bool publication, bool retirement = false) noexcept;
    struct Impl;
    std::unique_ptr<Impl> _impl;
    friend std::size_t DrainCompletions(JobScheduler&, Apply, void*) noexcept;
    friend bool RunUiActions(JobScheduler&) noexcept;
};

struct MessagePumpResult final
{
    bool quit = false;
    int exitCode = 0;
    std::uint32_t dispatched = 0;
};

// nativeMessage is a borrowed MSG*, valid during the callback. Null dispatch uses Translate/DispatchMessageW.
using MessageDispatch = void (*)(void*, void* nativeMessage) noexcept;
[[nodiscard]] MessagePumpResult PumpMessages(MessageDispatch dispatch = nullptr, void* context = nullptr) noexcept;
// Default apply invokes Job::Completed. Progress and final nodes are drained FIFO. Reentrant calls do nothing.
[[nodiscard]] std::size_t DrainCompletions(JobScheduler& scheduler, JobScheduler::Apply apply = nullptr,
                                           void* context = nullptr) noexcept;
[[nodiscard]] bool RunUiActions(JobScheduler& scheduler) noexcept;

enum class SettleResult
{
    Settled,
    TimedOut,
    Quit,
    Failed
};

// Harness/headless use only, never a UI command. Pumps the same messages, completions and actions as the host,
// then waits on the event and message queue. timeoutMs is a failure bound, not a performance assertion.
[[nodiscard]] SettleResult SettleJobs(JobScheduler& scheduler, std::uint32_t timeoutMs,
                                      MessageDispatch dispatch = nullptr, void* context = nullptr,
                                      JobScheduler::Apply apply = nullptr, void* applyContext = nullptr) noexcept;
