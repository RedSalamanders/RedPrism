#include "Common/MemoryAdmission.h"
#include "Common/MemoryBudget.h"

#include "Host.Tests.MemoryAdmission.h"

#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
void Expect(MemoryAdmission actualAdmission, std::uint64_t actualAvailable, MemoryAdmission expectedAdmission,
            std::uint64_t expectedAvailable, const char* name, int& failures)
{
    const bool passed = actualAdmission == expectedAdmission && actualAvailable == expectedAvailable;
    std::printf("%s memory-admission.%s\n", passed ? "PASS" : "FAIL", name);
    failures += passed ? 0 : 1;
}

void TestBoundaries(int& failures)
{
    constexpr std::uint64_t headroom = 1'073'741'824ULL;
    const auto zero = EvaluateMemoryAdmission(0, 0, 0, 0, 0);
    Expect(zero.admission, zero.availableAfterReservations, MemoryAdmission::Ready, 0, "zero-request-always-ready",
           failures);

    const auto atHeadroom = EvaluateMemoryAdmission(1, headroom, headroom, 0, 0);
    Expect(atHeadroom.admission, atHeadroom.availableAfterReservations, MemoryAdmission::Refused, 0, "headroom-equal",
           failures);
    const auto belowHeadroom = EvaluateMemoryAdmission(1, headroom - 1, headroom - 1, 0, 0);
    Expect(belowHeadroom.admission, belowHeadroom.availableAfterReservations, MemoryAdmission::Refused, 0,
           "headroom-minus-one", failures);
    const auto aboveHeadroom = EvaluateMemoryAdmission(1, headroom + 1, headroom + 1, 0, 0);
    Expect(aboveHeadroom.admission, aboveHeadroom.availableAfterReservations, MemoryAdmission::Ready, 1,
           "headroom-plus-one", failures);

    const auto physicalBottleneck = EvaluateMemoryAdmission(10, headroom + 9, headroom + 100, 0, 0);
    Expect(physicalBottleneck.admission, physicalBottleneck.availableAfterReservations, MemoryAdmission::Refused, 9,
           "physical-is-bottleneck", failures);
    const auto commitBottleneck = EvaluateMemoryAdmission(10, headroom + 100, headroom + 9, 0, 0);
    Expect(commitBottleneck.admission, commitBottleneck.availableAfterReservations, MemoryAdmission::Refused, 9,
           "commit-is-bottleneck", failures);
    const auto reservationBoundary = EvaluateMemoryAdmission(7, headroom + 20, headroom + 20, 13, 0);
    Expect(reservationBoundary.admission, reservationBoundary.availableAfterReservations, MemoryAdmission::Ready, 7,
           "reservation-subtracted-at-boundary", failures);
    const auto overReservation = EvaluateMemoryAdmission(8, headroom + 20, headroom + 20, 13, 0);
    Expect(overReservation.admission, overReservation.availableAfterReservations, MemoryAdmission::Waiting, 7,
           "reservation-one-byte-over", failures);
}

void TestWaitingAndOverflow(int& failures)
{
    constexpr std::uint64_t headroom = 1'073'741'824ULL;
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto saturatedReservations = EvaluateMemoryAdmission(maximum, maximum, maximum, maximum, 0);
    Expect(saturatedReservations.admission, saturatedReservations.availableAfterReservations, MemoryAdmission::Waiting,
           0, "max-reservation-saturates-to-zero", failures);
    const auto largeSnapshot = EvaluateMemoryAdmission(maximum, maximum, maximum, 0, 0);
    Expect(largeSnapshot.admission, largeSnapshot.availableAfterReservations, MemoryAdmission::Refused,
           maximum - 1'073'741'824ULL, "max-snapshot-no-wrap", failures);
    const auto guaranteedReservationRelease = EvaluateMemoryAdmission(1, headroom, headroom, 1, 0);
    Expect(guaranteedReservationRelease.admission, guaranteedReservationRelease.availableAfterReservations,
           MemoryAdmission::Waiting, 0, "outstanding-reservation-waits", failures);
    const auto releaseOnlyEvidence = EvaluateMemoryAdmission(1, headroom, headroom, 0, 1);
    Expect(releaseOnlyEvidence.admission, releaseOnlyEvidence.availableAfterReservations, MemoryAdmission::Waiting, 0,
           "deferred-release-does-not-add-capacity", failures);
    const auto noReleaseEvidence = EvaluateMemoryAdmission(1, headroom, headroom, 0, 0);
    Expect(noReleaseEvidence.admission, noReleaseEvidence.availableAfterReservations, MemoryAdmission::Refused, 0,
           "no-release-evidence-refuses", failures);
    const auto hugeReleasing = EvaluateMemoryAdmission(maximum, headroom, headroom, 0, maximum);
    Expect(hugeReleasing.admission, hugeReleasing.availableAfterReservations, MemoryAdmission::Waiting, 0,
           "max-deferred-release-still-not-usable", failures);
}

void TestWholeReservations(int& failures)
{
    constexpr std::uint64_t headroom = 1'073'741'824ULL;
    MemoryBudget budget;
    const MemorySnapshot snapshot{headroom + 32, headroom + 32};
    const auto first = budget.Reserve(20, snapshot);
    Expect(first.admission, budget.Reserved(), MemoryAdmission::Ready, 20, "first-whole-peak-reserved", failures);
    const auto second = budget.Reserve(16, snapshot);
    Expect(second.admission, budget.Reserved(), MemoryAdmission::Waiting, 20, "waiting-takes-no-partial-peak",
           failures);
    if (!budget.Release(20))
    {
        ++failures;
        std::printf("FAIL memory-admission.release-first\n");
    }
    const auto next = budget.Reserve(16, snapshot);
    Expect(next.admission, budget.Reserved(), MemoryAdmission::Ready, 16, "release-permits-next-peak", failures);
    if (budget.Release(17))
    {
        ++failures;
        std::printf("FAIL memory-admission.over-release-refused\n");
    }
    Expect(MemoryAdmission::Ready, budget.Reserved(), MemoryAdmission::Ready, 16, "over-release-does-not-underflow",
           failures);
    if (!budget.Release(16))
    {
        ++failures;
        std::printf("FAIL memory-admission.release-next\n");
    }
    const auto refused = budget.Reserve(33, snapshot);
    Expect(refused.admission, budget.Reserved(), MemoryAdmission::Refused, 0, "refused-takes-no-reservation", failures);
    const auto waitingForFree = budget.Reserve(33, snapshot, 16);
    Expect(waitingForFree.admission, budget.Reserved(), MemoryAdmission::Waiting, 0,
           "deferred-release-wait-is-unreserved", failures);
}
} // namespace

int RunMemoryAdmissionTests()
{
    int failures = 0;
    TestBoundaries(failures);
    TestWaitingAndOverflow(failures);
    TestWholeReservations(failures);
    return failures;
}
