#include "Host.Tests.UiCancellationBody.h"

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

void ActualCheckSequences() noexcept
{
    const auto check = [](UiCancellationBody& body, std::int64_t tick) noexcept
    { body.Observe({JobExecutionBoundary::CancellationObserved, kOriginal, kKind, kLane, std::nullopt}, tick); };
    UiCancellationBody body(kOriginal, kKind, kLane, 2);
    Enter(body, 10);
    check(body, 15);
    Exit(body, 20);
    Expect(body.CheckTicks() == -1, "admission-stub-check-is-not-useful-check");
    Enter(body, 100);
    check(body, 115);
    Expect(body.Current() == UiCancellationBody::State::Entered && body.CheckTicks() == 115 && body.ExitTicks() == -1 &&
               !body.RawResult(),
           "actual-check-does-not-invent-body-exit-or-result");
    Expect(body.ClassifyCheck(105, 110) == UiCancellationBody::CheckOutcome::ObservedAfterAcceptance,
           "actual-check-after-acceptance-is-distinct-sample");
    Expect(body.ClassifyCheck(105, 115) == UiCancellationBody::CheckOutcome::StopOverlap,
           "actual-check-at-acceptance-remains-overlap");
    Expect(body.ClassifyCheck(114, 116) == UiCancellationBody::CheckOutcome::StopOverlap,
           "actual-check-during-stop-dispatch-remains-overlap");
    Expect(body.ClassifyCheck(116, 120) == UiCancellationBody::CheckOutcome::BeforeStop,
           "actual-check-before-selected-stop-is-not-positive-latency");
    Expect(body.ClassifyStop(105, 110) == UiCancellationBody::StopOutcome::Pending,
           "actual-check-cannot-qualify-unreturned-body");
    Exit(body, 150, {JobStatus::Cancelled});
    Enter(body, 160);
    check(body, 165);
    Exit(body, 180);
    Expect(body.CheckTicks() == 115 && body.ExitTicks() == 150, "later-body-cannot-replace-selected-check-or-exit");

    UiCancellationBody missing(kOriginal, kKind, kLane, 1);
    Enter(missing, 100);
    Exit(missing, 150, {JobStatus::Cancelled});
    Expect(missing.ClassifyCheck(105, 110) == UiCancellationBody::CheckOutcome::Unobserved &&
               missing.CheckTicks() == -1,
           "cancelled-body-exit-is-not-an-observed-check");
    Expect(missing.ClassifyCheck(-1, 110) == UiCancellationBody::CheckOutcome::ClockFailed,
           "missing-check-does-not-hide-invalid-stop-clock");
    UiCancellationBody outside(kOriginal, kKind, kLane, 1);
    check(outside, 100);
    Expect(outside.Current() == UiCancellationBody::State::Malformed, "check-outside-useful-run-refuses");
    UiCancellationBody duplicate(kOriginal, kKind, kLane, 1);
    Enter(duplicate, 100);
    check(duplicate, 110);
    check(duplicate, 111);
    Expect(duplicate.ClassifyCheck(105, 109) == UiCancellationBody::CheckOutcome::Malformed,
           "duplicate-first-check-refuses");
    UiCancellationBody result(kOriginal, kKind, kLane, 1);
    Enter(result, 100);
    result.Observe({JobExecutionBoundary::CancellationObserved, kOriginal, kKind, kLane, JobResult{}}, 110);
    Expect(result.Current() == UiCancellationBody::State::Malformed, "check-cannot-carry-a-raw-result");
    UiCancellationBody clock(kOriginal, kKind, kLane, 1);
    Enter(clock, 100);
    check(clock, 99);
    Expect(clock.ClassifyCheck(105, 109) == UiCancellationBody::CheckOutcome::ClockFailed,
           "reversed-check-clock-refuses");
}
} // namespace

bool RunUiCancellationBodyTests() noexcept
{
    gFailures = 0;
    gChecks = 0;
    OriginalResumedBody();
    StopPartitions();
    Refusals();
    ActualCheckSequences();
    const bool passed = gFailures == 0 && gChecks == 42;
    std::printf("%s UI cancellation body reduction: %u checks, %u failures.\n", passed ? "PASS" : "FAIL", gChecks,
                gFailures);
    std::fflush(stdout);
    return passed;
}
