#include "Scope.Tests.UiCancellationBody.h"

#include "../../RedPrism/UiCancellationBody.h"

#include <array>
#include <cstdio>

namespace
{
unsigned int gFailures = 0;
unsigned int gChecks = 0;
constexpr JobTicket kOriginal{7, 41};
constexpr JobClass kKind = JobClass::Commit;
constexpr JobExecutionLane kLane = JobExecutionLane::Compute;

void Expect(bool passed, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s ui-cancellation-body.%s\n", passed ? "PASS" : "FAIL", name);
    if (!passed)
        ++gFailures;
}

void Enter(UiCancellationBody& body, std::int64_t ticks) noexcept
{
    body.Observe({JobExecutionBoundary::BodyEntered, kOriginal, kKind, kLane, std::nullopt}, ticks);
}
void Exit(UiCancellationBody& body, std::int64_t ticks, JobResult result = {}) noexcept
{
    body.Observe({JobExecutionBoundary::BodyExited, kOriginal, kKind, kLane, result}, ticks);
}
void Skip(UiCancellationBody& body, std::int64_t ticks) noexcept
{
    body.Observe({JobExecutionBoundary::BodySkipped, kOriginal, kKind, kLane, JobResult{JobStatus::Cancelled}}, ticks);
}

void OriginalResumedBody() noexcept
{
    UiCancellationBody body(kOriginal, kKind, kLane, 2);
    Enter(body, 10);
    Exit(body, 20);
    Expect(body.Current() == UiCancellationBody::State::Waiting && body.EntryTicks() == -1 && !body.RawResult(),
           "admission-stub-is-not-selected-body");
    body.Observe({JobExecutionBoundary::BodyEntered, {7, 42}, kKind, kLane, std::nullopt}, 25);
    body.Observe({JobExecutionBoundary::BodyExited, {7, 42}, kKind, kLane, JobResult{}}, 26);
    body.Observe({JobExecutionBoundary::BodyEntered, {8, 41}, kKind, kLane, std::nullopt}, 27);
    Expect(body.Current() == UiCancellationBody::State::Waiting,
           "foreign-slot-and-reused-generation-do-not-satisfy-original-ticket");
    Enter(body, 100);
    Expect(body.Current() == UiCancellationBody::State::Entered && body.EntryTicks() == 100 && body.ExitTicks() == -1 &&
               !body.RawResult(),
           "second-original-body-is-selected-before-exit");
    Exit(body, 180, {JobStatus::Cancelled});
    const auto result = body.RawResult();
    Expect(body.Current() == UiCancellationBody::State::Exited && body.EntryTicks() == 100 && body.ExitTicks() == 180 &&
               result && result.value().status == JobStatus::Cancelled && result.value().error == 0,
           "matching-real-body-exit-retains-raw-result");
    Enter(body, 190);
    Exit(body, 200);
    Expect(body.Current() == UiCancellationBody::State::Exited && body.EntryTicks() == 100 && body.ExitTicks() == 180 &&
               body.RawResult().value().status == JobStatus::Cancelled,
           "later-original-stage-does-not-replace-selected-body");
}

void StopPartitions() noexcept
{
    struct Case final
    {
        const char* name;
        std::int64_t begin;
        std::int64_t accepted;
        UiCancellationBody::StopOutcome expected;
    };
    constexpr std::array<Case, 8> cases{{
        {"stop-before-body-start", 99, 110, UiCancellationBody::StopOutcome::BeforeBody},
        {"finish-before-stop-dispatch", 181, 190, UiCancellationBody::StopOutcome::AlreadyFinished},
        {"finish-at-dispatch-start-is-overlap", 180, 190, UiCancellationBody::StopOutcome::FinishOverlap},
        {"finish-during-dispatch-is-overlap", 170, 190, UiCancellationBody::StopOutcome::FinishOverlap},
        {"finish-at-acceptance-is-overlap", 170, 180, UiCancellationBody::StopOutcome::FinishOverlap},
        {"body-return-after-accepted-stop", 150, 179, UiCancellationBody::StopOutcome::ReturnedAfterAcceptance},
        {"failed-stop-clock-refused", -1, 160, UiCancellationBody::StopOutcome::ClockFailed},
        {"reversed-stop-interval-refused", 170, 169, UiCancellationBody::StopOutcome::ClockFailed},
    }};
    UiCancellationBody body(kOriginal, kKind, kLane, 1);
    Expect(body.ClassifyStop(150, 160) == UiCancellationBody::StopOutcome::Pending,
           "missing-entry-cannot-qualify-stop");
    Enter(body, 100);
    Expect(body.ClassifyStop(150, 160) == UiCancellationBody::StopOutcome::Pending,
           "unreturned-body-cannot-qualify-stop");
    Exit(body, 180);
    for (const auto& trial : cases)
        Expect(body.ClassifyStop(trial.begin, trial.accepted) == trial.expected, trial.name);
}

void Refusals() noexcept
{
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 2);
        Skip(body, 10);
        Enter(body, 100);
        Exit(body, 180);
        Expect(body.Current() == UiCancellationBody::State::Exited && body.EntryTicks() == 100,
               "skipped-prior-stage-still-counts-original-ordinal");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        Skip(body, 100);
        Expect(body.Current() == UiCancellationBody::State::Skipped && !body.RawResult() &&
                   body.ClassifyStop(150, 160) == UiCancellationBody::StopOutcome::Skipped,
               "dispatched-unrun-selected-body-is-separate-bucket");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        Exit(body, 100);
        Enter(body, 110);
        Expect(body.Current() == UiCancellationBody::State::Malformed && body.EntryTicks() == -1,
               "exit-without-entry-stays-rejected");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        Enter(body, 100);
        Skip(body, 110);
        Expect(body.Current() == UiCancellationBody::State::Malformed, "running-body-cannot-also-be-skipped");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        Enter(body, 100);
        Enter(body, 110);
        Expect(body.Current() == UiCancellationBody::State::Malformed, "overlapping-entry-pair-refused");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        body.Observe({JobExecutionBoundary::BodyEntered, kOriginal, kKind, kLane, JobResult{}}, 100);
        Expect(body.Current() == UiCancellationBody::State::Malformed, "entry-with-terminal-result-refused");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        Enter(body, 100);
        body.Observe({JobExecutionBoundary::BodyExited, kOriginal, kKind, kLane, std::nullopt}, 110);
        Expect(body.Current() == UiCancellationBody::State::Malformed, "exit-without-raw-result-refused");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        body.Observe({JobExecutionBoundary::BodyEntered, kOriginal, JobClass::Background, kLane, std::nullopt}, 100);
        Expect(body.Current() == UiCancellationBody::State::Malformed, "original-ticket-class-mismatch-refused");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        body.Observe(
            {JobExecutionBoundary::BodyEntered, kOriginal, kKind, JobExecutionLane::GpuRecording, std::nullopt}, 100);
        Expect(body.Current() == UiCancellationBody::State::Malformed, "original-ticket-lane-mismatch-refused");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        Enter(body, -1);
        Exit(body, 110);
        Expect(body.Current() == UiCancellationBody::State::ClockFailed &&
                   body.ClassifyStop(100, 105) == UiCancellationBody::StopOutcome::ClockFailed,
               "failed-entry-clock-cannot-create-latency");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        Enter(body, 100);
        Exit(body, 99);
        Expect(body.Current() == UiCancellationBody::State::ClockFailed, "backwards-body-clock-cannot-create-latency");
    }
    {
        UiCancellationBody body(kOriginal, kKind, kLane, 1);
        Enter(body, 100);
        constexpr std::int32_t error = -2147024882; // E_OUTOFMEMORY, independently fixed HRESULT.
        Exit(body, 180, {JobStatus::Failed, error});
        const auto result = body.RawResult();
        Expect(body.Current() == UiCancellationBody::State::Exited && result &&
                   result.value().status == JobStatus::Failed && result.value().error == error,
               "raw-allocation-failure-is-not-normalized-to-cancellation");
    }
    Expect(UiCancellationBody({128, 41}, kKind, kLane, 1).Current() == UiCancellationBody::State::Malformed &&
               UiCancellationBody({7, 0}, kKind, kLane, 1).Current() == UiCancellationBody::State::Malformed &&
               UiCancellationBody(kOriginal, kKind, kLane, 0).Current() == UiCancellationBody::State::Malformed,
           "invalid-original-ticket-or-zero-ordinal-refused");
}
} // namespace

bool RunUiCancellationBodyTests() noexcept
{
    gFailures = 0;
    gChecks = 0;
    OriginalResumedBody();
    StopPartitions();
    Refusals();
    const bool passed = gFailures == 0 && gChecks == 28;
    std::printf("%s UI cancellation body reduction: %u checks, %u failures.\n", passed ? "PASS" : "FAIL", gChecks,
                gFailures);
    std::fflush(stdout);
    return passed;
}
