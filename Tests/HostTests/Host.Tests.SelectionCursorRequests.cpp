#include "Host.Tests.SelectionCursorRequests.h"

#include "RedPrism/SelectionCursorRequests.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <type_traits>

namespace
{
using Requests = SelectionCursorRequests;
using Key = Requests::Key;
using Request = Requests::Request;
using State = Requests::State;

constexpr Key kFirst{11, 7, 4.5, 9.25};

void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s selection-cursor-requests.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

void TestInitialAndCachedReplies(int& failures)
{
    Requests requests;
    Expect(!requests.Begin().has_value(), "initial-begin-has-no-work", failures);
    Expect(!requests.Busy(), "initial-state-is-not-busy", failures);
    Expect(!requests.Latest().has_value(), "initial-state-has-no-latest-request", failures);
    Expect(!requests.Closed(), "initial-state-is-open", failures);

    const auto firstObservation = requests.Observe(kFirst);
    Expect(firstObservation.state == State::Pending, "first-valid-query-is-pending", failures);
    const auto first = requests.Begin();
    Expect(first.has_value(), "first-valid-query-is-admitted", failures);
    Expect(first.value().serial == 1, "first-request-uses-serial-one", failures);
    Expect(first.value().key == kFirst, "first-request-retains-exact-key", failures);

    const auto repeatedObservation = requests.Observe(kFirst);
    Expect(repeatedObservation.state == State::Pending, "same-inflight-query-remains-pending", failures);
    Expect(requests.Latest().value() == first.value(), "same-query-reuses-original-serial", failures);
    Expect(requests.Busy(), "begun-request-is-busy", failures);
    Expect(!requests.Begin().has_value(), "begin-refuses-second-active-request", failures);
    Expect(requests.Acknowledge(first.value(), true), "exact-active-request-is-acknowledged", failures);
    const auto readyTrue = requests.Observe(kFirst);
    Expect(readyTrue.state == State::Ready, "true-reply-is-cached-as-ready", failures);
    Expect(readyTrue.inside, "true-reply-preserves-inside-value", failures);
    Expect(!requests.Begin().has_value(), "ready-cache-does-not-dispatch-again", failures);

    Requests falseReply;
    const auto falsePending = falseReply.Observe(kFirst);
    const auto falseRequest = falseReply.Begin();
    Expect(falsePending.state == State::Pending && falseRequest.has_value(), "false-cache-fixture-begins", failures);
    Expect(falseReply.Acknowledge(falseRequest.value(), false), "false-reply-is-acknowledged", failures);
    const auto readyFalse = falseReply.Observe(kFirst);
    Expect(readyFalse.state == State::Ready, "false-reply-is-cached-as-ready", failures);
    Expect(!readyFalse.inside, "false-reply-preserves-outside-value", failures);
    Expect(!falseReply.Begin().has_value(), "false-ready-cache-does-not-dispatch-again", failures);
}

void TestLatestCoalescingAndAcknowledgementAuthority(int& failures)
{
    Requests requests;
    (void)requests.Observe(kFirst);
    const auto original = requests.Begin();
    const Key secondKey{11, 7, 5.0, 9.25};
    const Key thirdKey{11, 7, 6.0, 9.25};
    const Key latestKey{11, 7, 7.0, 9.25};
    (void)requests.Observe(secondKey);
    (void)requests.Observe(thirdKey);
    (void)requests.Observe(latestKey);

    Expect(requests.Latest().value().key == latestKey, "many-queries-retain-only-latest-key", failures);
    Expect(requests.Latest().value().serial == 4, "coalesced-queries-consume-monotonic-serials", failures);
    Expect(requests.NeedsCancellation(), "superseded-active-request-needs-cancellation", failures);
    const Request foreign{latestKey, original.value().serial};
    Expect(!requests.Acknowledge(foreign, true), "foreign-acknowledgement-is-rejected", failures);
    Expect(requests.Active().value() == original.value(), "foreign-acknowledgement-keeps-active-ticket", failures);
    Expect(!requests.Begin().has_value(), "replacement-waits-for-original-acknowledgement", failures);

    Expect(requests.Acknowledge(original.value(), true), "superseded-original-acknowledgement-joins", failures);
    Expect(!requests.Acknowledge(original.value(), true), "duplicate-acknowledgement-is-rejected", failures);
    Expect(!requests.Busy(), "joined-original-clears-busy-state", failures);
    Expect(!requests.NeedsCancellation(), "joined-original-clears-cancellation-need", failures);
    const auto replacement = requests.Begin();
    Expect(replacement.has_value(), "one-replacement-begins-after-original-ack", failures);
    Expect(replacement.value().key == latestKey, "replacement-uses-only-latest-query", failures);
    Expect(replacement.value().serial == 4, "replacement-keeps-latest-query-serial", failures);

    const Request stale{secondKey, 2};
    Expect(!requests.Acknowledge(stale, false), "stale-acknowledgement-is-rejected", failures);
    Expect(requests.Active().value() == replacement.value(), "stale-acknowledgement-keeps-new-active-ticket", failures);
    Expect(!requests.Acknowledge(original.value(), false), "old-duplicate-cannot-consume-new-active-ticket", failures);
    Expect(requests.Active().value() == replacement.value(), "old-duplicate-leaves-replacement-active", failures);
    Expect(requests.Acknowledge(replacement.value(), false), "replacement-exact-acknowledgement-joins", failures);
    const auto ready = requests.Observe(latestKey);
    Expect(ready.state == State::Ready, "replacement-reply-is-published-for-latest-query", failures);
    Expect(!ready.inside, "replacement-reply-publishes-exact-false-result", failures);
}

void TestDocumentAndEpochSupersession(int& failures)
{
    Requests requests;
    (void)requests.Observe(kFirst);
    const auto original = requests.Begin();
    const Key newDocument{12, 7, kFirst.x, kFirst.y};
    (void)requests.Observe(newDocument);
    Expect(requests.NeedsCancellation(), "document-change-cancels-active-request", failures);
    Expect(requests.Acknowledge(original.value(), true), "document-change-original-ack-joins", failures);
    const auto documentObservation = requests.Observe(newDocument);
    Expect(documentObservation.state == State::Pending, "old-document-result-is-suppressed", failures);
    const auto documentRequest = requests.Begin();
    Expect(documentRequest.value().key.document == 12, "new-document-request-carries-new-identity", failures);

    Requests epochRequests;
    (void)epochRequests.Observe(kFirst);
    const auto epochOriginal = epochRequests.Begin();
    const Key newEpoch{11, 8, kFirst.x, kFirst.y};
    (void)epochRequests.Observe(newEpoch);
    Expect(epochRequests.NeedsCancellation(), "epoch-change-cancels-active-request", failures);
    Expect(epochRequests.Acknowledge(epochOriginal.value(), false), "epoch-change-original-ack-joins", failures);
    const auto epochObservation = epochRequests.Observe(newEpoch);
    Expect(epochObservation.state == State::Pending, "old-epoch-result-is-suppressed", failures);
    const auto epochRequest = epochRequests.Begin();
    Expect(epochRequest.value().key.epoch == 8, "new-epoch-request-carries-new-identity", failures);
}

void TestGeometryGenerationBeforeChromeEpoch(int& failures)
{
    Requests requests;
    Key oldSource = kFirst;
    oldSource.generation = 17;
    (void)requests.Observe(oldSource);
    const auto oldRequest = requests.Begin();
    Key changed = oldSource;
    changed.generation = 18;
    Expect(requests.Observe(changed).state == State::Pending,
           "geometry-generation-change-is-pending-before-chrome-epoch", failures);
    Expect(requests.NeedsCancellation(), "geometry-generation-cancels-old-active-source", failures);
    Expect(requests.Acknowledge(oldRequest.value(), true), "old-geometry-generation-acknowledges", failures);
    Expect(requests.Observe(changed).state == State::Pending, "old-geometry-generation-cannot-populate-new-cache",
           failures);
    const auto current = requests.Begin();
    Expect(current && current.value().serial == 2 && current.value().key.generation == 18,
           "new-geometry-generation-has-one-new-original-request", failures);
    Expect(requests.Acknowledge(current.value(), false) && requests.Observe(changed).state == State::Ready &&
               !requests.Observe(changed).inside,
           "new-geometry-generation-caches-its-own-false-result", failures);
}

void TestInvalidQueriesClearLatestButKeepActive(int& failures)
{
    Requests requests;
    (void)requests.Observe(kFirst);
    const auto active = requests.Begin();

    const double nan = (std::numeric_limits<double>::quiet_NaN)();
    const double infinity = (std::numeric_limits<double>::infinity)();
    const auto nanObservation = requests.Observe({11, 7, nan, 2.0});
    Expect(nanObservation.state == State::Unavailable, "nan-query-is-unavailable", failures);
    Expect(!requests.Latest().has_value(), "nan-query-clears-latest", failures);
    Expect(requests.NeedsCancellation(), "nan-query-requests-active-cancellation", failures);
    Expect(requests.Busy() && requests.Active().value() == active.value(), "nan-query-keeps-active-until-ack",
           failures);

    const auto infinityObservation = requests.Observe({11, 7, 2.0, infinity});
    Expect(infinityObservation.state == State::Unavailable, "infinite-query-is-unavailable", failures);
    Expect(!requests.Latest().has_value(), "infinite-query-keeps-latest-cleared", failures);
    Expect(requests.Busy() && requests.Active().value() == active.value(), "infinite-query-keeps-active-ticket",
           failures);

    const auto zeroDocumentObservation = requests.Observe({0, 7, 2.0, 3.0});
    Expect(zeroDocumentObservation.state == State::Unavailable, "zero-document-query-is-unavailable", failures);
    Expect(!requests.Latest().has_value(), "zero-document-query-clears-latest", failures);
    const auto zeroEpochObservation = requests.Observe({11, 0, 2.0, 3.0});
    Expect(zeroEpochObservation.state == State::Unavailable, "zero-epoch-query-is-unavailable", failures);
    Expect(!requests.Latest().has_value(), "zero-epoch-query-keeps-latest-cleared", failures);
    Expect(requests.Busy(), "invalid-queries-retain-active-ticket-until-ack", failures);
    Expect(requests.Acknowledge(active.value(), true), "invalid-query-active-ticket-acknowledges", failures);
    Expect(!requests.Busy(), "invalid-query-ack-clears-active-ticket", failures);
    Expect(!requests.Latest().has_value(), "invalid-query-ack-publishes-no-latest", failures);
    Expect(requests.Observe(kFirst).state == State::Pending, "valid-query-after-invalid-can-start-fresh", failures);
}

void TestAdmissionFailureAndRetryRules(int& failures)
{
    Requests requests;
    (void)requests.Observe(kFirst);
    const auto rejectedAdmission = requests.Begin();
    Expect(rejectedAdmission.has_value(), "admission-failure-fixture-begins-request", failures);
    Expect(requests.Acknowledge(rejectedAdmission.value(), std::nullopt), "failed-admission-acknowledges-null-result",
           failures);
    Expect(requests.Observe(kFirst).state == State::Unavailable, "failed-admission-makes-query-unavailable", failures);
    Expect(!requests.Begin().has_value(), "failed-admission-does-not-retry-spin", failures);
    Expect(requests.Observe(kFirst).state == State::Unavailable, "repeated-failed-query-remains-unavailable", failures);
    Expect(!requests.Begin().has_value(), "repeated-query-still-does-not-dispatch", failures);

    const Key different{11, 7, 4.75, 9.25};
    Expect(requests.Observe(different).state == State::Pending, "different-query-can-retry-admission", failures);
    const auto retry = requests.Begin();
    Expect(retry.has_value(), "different-query-admission-begins", failures);
    Expect(retry.value().serial == 2, "different-query-uses-next-serial", failures);
    Expect(requests.Acknowledge(retry.value(), true), "different-query-admission-result-joins", failures);
}

void TestInvalidationAndCloseJoinRules(int& failures)
{
    Requests invalidated;
    (void)invalidated.Observe(kFirst);
    const auto invalidatedActive = invalidated.Begin();
    invalidated.Invalidate();
    Expect(!invalidated.Latest().has_value(), "invalidation-clears-latest", failures);
    Expect(invalidated.NeedsCancellation(), "invalidation-requests-active-cancellation", failures);
    Expect(invalidated.Busy() && invalidated.Active().value() == invalidatedActive.value(),
           "invalidation-keeps-active-until-joined-ack", failures);
    Expect(!invalidated.Begin().has_value(), "invalidation-dispatches-no-new-work", failures);
    Expect(invalidated.Acknowledge(invalidatedActive.value(), true), "invalidated-active-ticket-can-join", failures);
    const auto afterInvalidation = invalidated.Observe(kFirst);
    Expect(afterInvalidation.state == State::Pending && !afterInvalidation.inside,
           "invalidation-suppresses-old-result-and-allows-fresh-query", failures);

    Requests closed;
    (void)closed.Observe(kFirst);
    const auto closedActive = closed.Begin();
    closed.Close();
    Expect(closed.Closed(), "close-latches-closed-state", failures);
    Expect(!closed.Latest().has_value(), "close-clears-latest", failures);
    Expect(closed.NeedsCancellation(), "close-requests-active-cancellation", failures);
    Expect(closed.Busy() && closed.Active().value() == closedActive.value(), "close-keeps-active-until-joined-ack",
           failures);
    Expect(!closed.Begin().has_value(), "closed-state-dispatches-no-new-work", failures);
    Expect(closed.Observe(kFirst).state == State::Unavailable, "closed-state-publishes-no-new-query", failures);
    Expect(closed.Acknowledge(closedActive.value(), true), "closed-active-ticket-can-join", failures);
    Expect(!closed.Busy(), "closed-joined-ticket-clears-busy-state", failures);
    const auto afterCloseAck = closed.Observe(kFirst);
    Expect(afterCloseAck.state == State::Unavailable && !afterCloseAck.inside,
           "closed-acknowledgement-publishes-no-result", failures);
    Expect(!closed.Begin().has_value(), "joined-closed-state-remains-undispatchable", failures);
}

void TestCoordinatesAndSerialExhaustion(int& failures)
{
    Requests signedZero;
    const Key negativeZero{11, 7, -0.0, 2.0};
    (void)signedZero.Observe(negativeZero);
    const auto zeroRequest = signedZero.Begin();
    const Key positiveZero{11, 7, 0.0, 2.0};
    const auto positiveZeroObservation = signedZero.Observe(positiveZero);
    Expect(positiveZeroObservation.state == State::Pending, "signed-zero-query-remains-pending", failures);
    Expect(signedZero.Latest().value() == zeroRequest.value(), "signed-zero-same-query-reuses-request", failures);
    Expect(!signedZero.Begin().has_value(), "signed-zero-repeat-does-not-dispatch-again", failures);
    Expect(signedZero.Acknowledge(zeroRequest.value(), false), "signed-zero-request-acknowledges", failures);
    Expect(signedZero.Observe(positiveZero).state == State::Ready, "signed-zero-reply-is-cached", failures);

    Requests negativeCoordinates;
    const Key negativeKey{11, 7, -4.0, -9.5};
    Expect(negativeCoordinates.Observe(negativeKey).state == State::Pending, "finite-negative-coordinates-are-valid",
           failures);
    const auto negativeRequest = negativeCoordinates.Begin();
    Expect(negativeRequest.value().key == negativeKey, "finite-negative-coordinates-are-preserved", failures);

    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    Requests exhausted{max - 1};
    const auto lastObservation = exhausted.Observe(kFirst);
    Expect(lastObservation.state == State::Pending, "last-serial-query-is-pending", failures);
    const auto lastRequest = exhausted.Begin();
    Expect(lastRequest.value().serial == max, "maximum-serial-is-accepted", failures);
    const Key unrecordable{11, 7, 5.5, 9.25};
    Expect(exhausted.Observe(unrecordable).state == State::Unavailable, "serial-exhaustion-refuses-new-query",
           failures);
    Expect(!exhausted.Latest().has_value(), "serial-exhaustion-clears-stale-latest", failures);
    Expect(exhausted.NeedsCancellation(), "serial-exhaustion-cancels-existing-active-request", failures);
    Expect(exhausted.Busy() && exhausted.Active().value() == lastRequest.value(),
           "serial-exhaustion-retains-active-until-ack", failures);
    Expect(!exhausted.Begin().has_value(), "serial-exhaustion-dispatches-no-unrecordable-query", failures);
    Expect(exhausted.Acknowledge(lastRequest.value(), true), "serial-exhaustion-active-ticket-can-join", failures);
    const auto afterExhaustionAck = exhausted.Observe(kFirst);
    Expect(afterExhaustionAck.state == State::Unavailable && !afterExhaustionAck.inside,
           "serial-exhaustion-does-not-publish-stale-cache", failures);
}

static_assert(std::is_trivially_copyable_v<Key>);
static_assert(std::is_trivially_destructible_v<Key>);
static_assert(std::is_trivially_copyable_v<Request>);
static_assert(std::is_trivially_destructible_v<Request>);
static_assert(std::is_trivially_copyable_v<Requests>);
static_assert(std::is_trivially_destructible_v<Requests>);
static_assert(sizeof(Requests) <= 256);
} // namespace

int RunSelectionCursorRequestsTests()
{
    int failures = 0;
    TestInitialAndCachedReplies(failures);
    TestLatestCoalescingAndAcknowledgementAuthority(failures);
    TestDocumentAndEpochSupersession(failures);
    TestGeometryGenerationBeforeChromeEpoch(failures);
    TestInvalidQueriesClearLatestButKeepActive(failures);
    TestAdmissionFailureAndRetryRules(failures);
    TestInvalidationAndCloseJoinRules(failures);
    TestCoordinatesAndSerialExhaustion(failures);
    std::printf("%s selection-cursor-requests.summary failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}