#pragma once

#include "Executor.h"

#include <cstddef>
#include <cstdint>
#include <memory>

// This boundary deliberately has no Windows types. Jobs own immutable input and result values. Worker bodies
// never access a session, a window, chrome or a graphics context; only UI callbacks may touch those owners.
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

struct JobOptions final
{
    JobClass kind = JobClass::Background;
    // Stroke, Selection, Commit and Io are FIFO within (kind, serialKey), through UI apply/drop. Zero is a
    // conservative global key. The host supplies collision-free document/path keys, never a truncated hash.
    std::uint64_t serialKey = 0;
};

struct JobTicket final
{
    std::size_t slot = 128;
    std::uint64_t generation = 0;
    [[nodiscard]] bool operator==(const JobTicket&) const noexcept = default;
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

class JobContext final
{
  public:
    Executor& executor;
    CancelToken cancellation;
    void* const shutdownEvent; // borrowed manual-reset HANDLE, valid only during Run

    // Concurrent calls coalesce without waiting. A report never allocates. Percentages are monotonic, and
    // publication is limited to one per 100 ms. Run must join every producer before returning.
    void ReportProgress(unsigned int percent) const noexcept;

    using Report = void (*)(void*, unsigned int) noexcept;
    JobContext(Executor& execution, CancelToken token, void* event, Report report, void* context) noexcept
        : executor(execution), cancellation(token), shutdownEvent(event), _report(report), _context(context)
    {
    }

  private:
    Report _report;
    void* _context;
};

class Job
{
  public:
    virtual ~Job() = default;
    // Only bad_alloc is translated by the runner, to a failed operation. Other unexpected exceptions retain
    // the process crash policy. Kernels themselves do not catch. Run cannot leave any producer in flight.
    [[nodiscard]] virtual JobResult Run(JobContext& context) = 0;
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

class JobScheduler final
{
  public:
    static constexpr std::size_t kCapacity = 128;
    static constexpr std::size_t kUiActionCapacity = 8;
    JobScheduler() noexcept;
    ~JobScheduler();
    JobScheduler(const JobScheduler&) = delete;
    JobScheduler& operator=(const JobScheduler&) = delete;

    [[nodiscard]] std::int32_t Initialize(unsigned int threads = 0, JobObserver observer = {}) noexcept;
    // Owner-thread operations. Failed submission leaves ownership with the caller. Queue capacity includes
    // running and completed-but-undrained jobs. A ticket never identifies a reused slot's newer generation.
    [[nodiscard]] std::int32_t Submit(std::unique_ptr<Job>& job, JobOptions options, JobTicket& ticket) noexcept;
    [[nodiscard]] bool Cancel(JobTicket ticket) noexcept;
    [[nodiscard]] bool PostUiAction(std::unique_ptr<UiAction>& action) noexcept;
    void RequestStop() noexcept;
    // Owner thread, outside every UI callback. Hides/cancel coordination belongs to Application. Close joins
    // producers before destroying their inputs, results, intrusive nodes, events or COM pool lifetime.
    void Close() noexcept;
    [[nodiscard]] bool IsIdle() const noexcept;
    [[nodiscard]] bool HasUiWork() const noexcept;
    [[nodiscard]] bool InUiCallback() const noexcept;
    [[nodiscard]] void* WakeHandle() const noexcept;

    using Apply = void (*)(void*, Job&, JobResult) noexcept;

  private:
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
