#include "Host.Tests.WorkerWriteCompletion.h"

#include "RedPrism/WorkerWriteCompletion.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kDeadlockGuardMs = 30000;

[[noreturn]] void ExitOnDeadlock() noexcept
{
    // A launched thread may still borrow stack state. Do not unwind that state on a deadlock path.
    std::_Exit(2);
}

DWORD WaitBounded(HANDLE handle, bool launchedThreadMayBorrow = false) noexcept
{
    const DWORD result = WaitForSingleObject(handle, kDeadlockGuardMs);
    if (result == WAIT_TIMEOUT || (launchedThreadMayBorrow && result == WAIT_FAILED))
        ExitOnDeadlock();
    return result;
}

bool Signal(HANDLE handle) noexcept
{
    return handle != nullptr && SetEvent(handle) != FALSE;
}

void SignalBorrowedThreadGate(HANDLE handle) noexcept
{
    if (!Signal(handle))
        ExitOnDeadlock();
}

wil::unique_handle CreateManualEvent() noexcept
{
    return wil::unique_handle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct ReadbackRace final
{
    WorkerWriteCompletion* completion = nullptr;
    HANDLE startGate = nullptr;
    HANDLE attempted = nullptr;
    std::atomic<unsigned int> fileWriterEntries{0};
    std::atomic<bool> workerBegan{false};
};
#pragma warning(pop)

DWORD WINAPI TryReadbackWriter(void* parameter) noexcept
{
    auto* const race = static_cast<ReadbackRace*>(parameter);
    const DWORD gateResult = WaitForSingleObject(race->startGate, INFINITE);
    if (gateResult == WAIT_OBJECT_0)
    {
        const bool began = race->completion->BeginWorker(WorkerWriteCompletion::Phase::Readback);
        race->workerBegan.store(began, std::memory_order_release);
        if (began)
        {
            race->fileWriterEntries.fetch_add(1, std::memory_order_relaxed);
            race->completion->Acknowledge();
        }
    }
    (void)SetEvent(race->attempted);
    return 0;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct WriterRace final
{
    WorkerWriteCompletion* completion = nullptr;
    HANDLE startGate = nullptr;
    HANDLE acknowledgeGate = nullptr;
    HANDLE enteredWriting = nullptr;
    HANDLE beforeAcknowledge = nullptr;
    std::atomic<unsigned int> fileWriterEntries{0};
    std::atomic<bool> workerBegan{false};
};
#pragma warning(pop)

DWORD WINAPI GatedWriter(void* parameter) noexcept
{
    auto* const race = static_cast<WriterRace*>(parameter);
    const DWORD startResult = WaitForSingleObject(race->startGate, INFINITE);
    const bool began =
        startResult == WAIT_OBJECT_0 && race->completion->BeginWorker(WorkerWriteCompletion::Phase::Readback);
    race->workerBegan.store(began, std::memory_order_release);
    if (began)
        race->fileWriterEntries.fetch_add(1, std::memory_order_relaxed);

    (void)SetEvent(race->enteredWriting);
    (void)SetEvent(race->beforeAcknowledge);
    const DWORD acknowledgeResult = WaitForSingleObject(race->acknowledgeGate, INFINITE);
    if (acknowledgeResult == WAIT_OBJECT_0)
        race->completion->Acknowledge();
    return 0;
}
} // namespace

int RunWorkerWriteCompletionChecks() noexcept
{
    int failures = 0;
    const auto expect = [&failures](bool condition, char const* name) noexcept
    {
        std::printf("%s worker-write-completion.%s\n", condition ? "PASS" : "FAIL", name);
        if (!condition)
            ++failures;
    };
    // Project/job path: queued work enters the sole writer stage and can finish only by acknowledgement.
    WorkerWriteCompletion project(WorkerWriteCompletion::Phase::Queued);
    const bool projectReady = SUCCEEDED(project.CreationError()) && project.Event() != nullptr;
    expect(projectReady, "project-event-created");
    expect(project.Current() == WorkerWriteCompletion::Phase::Queued, "project-starts-queued");

    unsigned int projectWriterEntries = 0;
    const bool projectBegan = project.BeginWorker(WorkerWriteCompletion::Phase::Queued);
    if (projectBegan)
        ++projectWriterEntries;
    expect(projectBegan, "project-queued-to-writing");
    expect(project.Current() == WorkerWriteCompletion::Phase::Writing, "project-phase-writing");

    const bool duplicateBegan = project.BeginWorker(WorkerWriteCompletion::Phase::Queued);
    if (duplicateBegan)
        ++projectWriterEntries;
    expect(!duplicateBegan && !project.BeginWorker(WorkerWriteCompletion::Phase::Writing) &&
               !project.BeginWorker(WorkerWriteCompletion::Phase::Cancelled) &&
               !project.BeginWorker(WorkerWriteCompletion::Phase::Finished) && projectWriterEntries == 1,
           "duplicate-begin-rejected-no-new-writer");

    project.Acknowledge();
    const DWORD projectCompletionWait = project.Event() != nullptr ? WaitBounded(project.Event()) : WAIT_FAILED;
    expect(project.Current() == WorkerWriteCompletion::Phase::Finished && projectCompletionWait == WAIT_OBJECT_0,
           "project-acknowledgement-finishes-event");

    // Cancellation wins while readback is still pending. The worker is held until after cancellation.
    WorkerWriteCompletion cancelled(WorkerWriteCompletion::Phase::Readback);
    auto cancelStart = CreateManualEvent();
    auto cancelAttempted = CreateManualEvent();
    const bool cancelSetup =
        SUCCEEDED(cancelled.CreationError()) && cancelled.Event() != nullptr && cancelStart && cancelAttempted;
    ReadbackRace cancelRace{&cancelled, cancelStart.get(), cancelAttempted.get()};
    wil::unique_handle cancelThread;
    if (cancelSetup)
        cancelThread.reset(CreateThread(nullptr, 0, TryReadbackWriter, &cancelRace, 0, nullptr));

    const bool cancellationWon = cancelSetup && cancelThread && cancelled.CancelReadback();
    expect(cancellationWon, "readback-cancel-wins-before-writer");
    expect(cancelled.Current() == WorkerWriteCompletion::Phase::Cancelled, "readback-cancelled-phase");

    bool cancelWorkerAttemptObserved = false;
    if (cancelThread)
    {
        SignalBorrowedThreadGate(cancelStart.get());
        const DWORD attemptWait = WaitBounded(cancelAttempted.get(), true);
        const DWORD joinWait = WaitBounded(cancelThread.get(), true);
        cancelWorkerAttemptObserved = attemptWait == WAIT_OBJECT_0 && joinWait == WAIT_OBJECT_0;
    }
    expect(cancelWorkerAttemptObserved && !cancelRace.workerBegan.load(std::memory_order_acquire),
           "cancelled-readback-worker-cannot-begin");
    expect(cancelRace.fileWriterEntries.load(std::memory_order_relaxed) == 0,
           "cancelled-readback-does-not-enter-file-writer");

    // The writer wins the same boundary. It cannot acknowledge until the owner releases the worker gate.
    WorkerWriteCompletion writing(WorkerWriteCompletion::Phase::Readback);
    auto writerStart = CreateManualEvent();
    auto acknowledgeGate = CreateManualEvent();
    auto enteredWriting = CreateManualEvent();
    auto beforeAcknowledge = CreateManualEvent();
    const bool writerSetup = SUCCEEDED(writing.CreationError()) && writing.Event() != nullptr && writerStart &&
                             acknowledgeGate && enteredWriting && beforeAcknowledge;
    WriterRace writerRace{&writing, writerStart.get(), acknowledgeGate.get(), enteredWriting.get(),
                          beforeAcknowledge.get()};
    wil::unique_handle writerThread;
    if (writerSetup)
        writerThread.reset(CreateThread(nullptr, 0, GatedWriter, &writerRace, 0, nullptr));

    bool writerEntered = false;
    bool cancellationLost = false;
    bool writerStillWritingAfterCancel = false;
    bool authorityRetainedAfterCancel = false;
    DWORD beforeAckWait = WAIT_FAILED;
    DWORD writerCompletionWait = WAIT_FAILED;
    bool writerJoined = false;
    if (writerThread)
    {
        SignalBorrowedThreadGate(writerStart.get());
        const DWORD enteredWait = WaitBounded(enteredWriting.get(), true);
        writerEntered = enteredWait == WAIT_OBJECT_0 && writerRace.workerBegan.load(std::memory_order_acquire);
        cancellationLost = writerEntered && !writing.CancelReadback();
        writerStillWritingAfterCancel = writing.Current() == WorkerWriteCompletion::Phase::Writing;
        authorityRetainedAfterCancel = cancellationLost && writing.Event() != nullptr &&
                                       writerRace.workerBegan.load(std::memory_order_acquire) &&
                                       writerStillWritingAfterCancel;

        HANDLE const beforeAckHandles[2] = {writing.Event(), beforeAcknowledge.get()};
        beforeAckWait = beforeAckHandles[0] != nullptr && beforeAckHandles[1] != nullptr
                            ? WaitForMultipleObjects(2, beforeAckHandles, FALSE, kDeadlockGuardMs)
                            : WAIT_FAILED;
        if (beforeAckWait != WAIT_OBJECT_0 && beforeAckWait != WAIT_OBJECT_0 + 1)
            ExitOnDeadlock();

        SignalBorrowedThreadGate(acknowledgeGate.get());
        writerCompletionWait = writing.Event() != nullptr ? WaitBounded(writing.Event(), true) : WAIT_FAILED;
        writerJoined = WaitBounded(writerThread.get(), true) == WAIT_OBJECT_0;
    }

    expect(writerEntered && writerRace.fileWriterEntries.load(std::memory_order_relaxed) == 1,
           "writer-enters-before-cancel");
    expect(cancellationLost && writerStillWritingAfterCancel, "writer-wins-cancel-keeps-writing");
    expect(beforeAckWait == WAIT_OBJECT_0 + 1, "writer-event-unset-before-scope-ack");
    expect(writerCompletionWait == WAIT_OBJECT_0 && writerJoined, "owner-waits-for-writer-acknowledgement");
    expect(writing.Current() == WorkerWriteCompletion::Phase::Finished && writing.Event() != nullptr,
           "writer-acknowledgement-finishes-event");
    expect(authorityRetainedAfterCancel, "cancel-cannot-reclaim-handle-job-authority");

    return failures == 0 ? 0 : 1;
}
