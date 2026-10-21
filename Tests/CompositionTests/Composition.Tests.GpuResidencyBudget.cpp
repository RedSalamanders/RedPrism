#include "Composition.Tests.GpuResidencyBudget.h"

#include "../../RedPrism/GpuResidencyBudget.h"

#include <cstdio>
#include <limits>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s gpu-residency-budget.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr GpuResidencySnapshot kOneGiB{17, 1024ull * 1024 * 1024, 256ull * 1024 * 1024};

static_assert(!std::is_copy_constructible_v<GpuResidencyReservation>);
static_assert(!std::is_copy_assignable_v<GpuResidencyReservation>);
static_assert(std::is_nothrow_move_constructible_v<GpuResidencyReservation>);
static_assert(std::is_nothrow_move_assignable_v<GpuResidencyReservation>);

void TestReservationLifecycle()
{
    GpuResidencyBudget budget;
    GpuResidencyAdmission result = GpuResidencyAdmission::Invalid;
    GpuResidencyReservation empty;
    Expect(!empty && empty.Bytes() == 0 && empty.DeviceGeneration() == 0, "default-empty");
    empty.ResetOwner();
    Expect(!empty && budget.ReservedOwner() == 0 && budget.ActiveOwner() == 0, "default-reset-idempotent");

    auto first = budget.ReserveOwner(37, {5, 128ull * 1024 * 1024, 0}, result);
    Expect(first && result == GpuResidencyAdmission::Ready && first.Bytes() == 37 && first.DeviceGeneration() == 5,
           "owner-reservation-metadata");
    Expect(budget.ReservedOwner() == 37 && budget.ActiveOwner() == 1, "owner-accounting-after-reserve");

    auto moved = std::move(first);
    Expect(!first && first.Bytes() == 0 && first.DeviceGeneration() == 0 && moved && moved.Bytes() == 37 &&
               moved.DeviceGeneration() == 5,
           "move-construction-clears-source");
    Expect(budget.ReservedOwner() == 37 && budget.ActiveOwner() == 1, "move-construction-keeps-one-charge");

    auto replacement = budget.ReserveOwner(19, {6, 128ull * 1024 * 1024, 0}, result);
    Expect(replacement && budget.ReservedOwner() == 56 && budget.ActiveOwner() == 2, "second-reservation-held");
    replacement = std::move(moved);
    Expect(replacement && replacement.Bytes() == 37 && replacement.DeviceGeneration() == 5 && !moved &&
               budget.ReservedOwner() == 37 && budget.ActiveOwner() == 1,
           "move-assignment-releases-prior-charge-once");

    replacement = std::move(replacement);
    Expect(replacement && replacement.Bytes() == 37 && replacement.DeviceGeneration() == 5 &&
               budget.ReservedOwner() == 37 && budget.ActiveOwner() == 1,
           "self-move-assignment-safe");

    replacement.ResetOwner();
    replacement.ResetOwner();
    Expect(!replacement && replacement.Bytes() == 0 && replacement.DeviceGeneration() == 0 &&
               budget.ReservedOwner() == 0 && budget.ActiveOwner() == 0,
           "live-reset-idempotent-restores-accounting");

    {
        auto scoped = budget.ReserveOwner(23, {7, 128ull * 1024 * 1024, 0}, result);
        Expect(scoped && budget.ReservedOwner() == 23 && budget.ActiveOwner() == 1, "destructor-fixture-reserved");
    }
    Expect(budget.ReservedOwner() == 0 && budget.ActiveOwner() == 0, "destructor-restores-accounting");
}

void TestOwnerThreadRules()
{
    GpuResidencyBudget budget;
    GpuResidencyAdmission result = GpuResidencyAdmission::Invalid;
    auto reservation = budget.ReserveOwner(41, {9, 128ull * 1024 * 1024, 0}, result);
    Expect(reservation && result == GpuResidencyAdmission::Ready, "worker-fixture-reserved-on-owner");

    bool workerResult = true;
    std::uint64_t workerReserved = 1;
    std::size_t workerActive = 1;
    std::uint64_t workerBytes = 0;
    std::uint64_t workerGeneration = 0;
    GpuResidencyAdmission workerAdmission = GpuResidencyAdmission::Ready;
    std::thread worker(
        [&]() noexcept
        {
            GpuResidencyAdmission attempted = GpuResidencyAdmission::Ready;
            auto refused = budget.ReserveOwner(1, {9, 128ull * 1024 * 1024, 0}, attempted);
            workerResult = !refused && attempted == GpuResidencyAdmission::Invalid;
            workerReserved = budget.ReservedOwner();
            workerActive = budget.ActiveOwner();
            workerBytes = reservation.Bytes();
            workerGeneration = reservation.DeviceGeneration();
            workerAdmission = attempted;
        });
    worker.join();

    Expect(workerResult && workerAdmission == GpuResidencyAdmission::Invalid, "worker-reservation-refused-invalid");
    Expect(workerReserved == 0 && workerActive == 0, "worker-owner-getters-return-zero");
    Expect(workerBytes == 41 && workerGeneration == 9, "worker-reads-immutable-reservation-metadata");
    Expect(budget.ReservedOwner() == 41 && budget.ActiveOwner() == 1, "worker-attempt-does-not-change-owner-state");
    reservation.ResetOwner();
}

void TestAdmissionBoundaries()
{
    GpuResidencyBudget budget;
    GpuResidencyAdmission result = GpuResidencyAdmission::Ready;
    auto zeroBytes = budget.ReserveOwner(0, {1, 1024, 0}, result);
    Expect(!zeroBytes && result == GpuResidencyAdmission::Invalid, "zero-bytes-invalid");
    auto zeroGeneration = budget.ReserveOwner(1, {0, 1024, 0}, result);
    Expect(!zeroGeneration && result == GpuResidencyAdmission::Invalid, "unknown-generation-invalid");
    auto zeroBudget = budget.ReserveOwner(1, {1, 0, 0}, result);
    Expect(!zeroBudget && result == GpuResidencyAdmission::Invalid, "zero-budget-invalid");
    Expect(budget.ReservedOwner() == 0 && budget.ActiveOwner() == 0, "invalid-admission-has-no-partial-accounting");

    auto usageOverBudget = budget.ReserveOwner(1, {1, 1024, 1025}, result);
    Expect(!usageOverBudget && result == GpuResidencyAdmission::Pressure, "usage-over-budget-pressure");
    auto usageMaxBudgetSmall = budget.ReserveOwner(1, {1, 1024, (std::numeric_limits<std::uint64_t>::max)()}, result);
    Expect(!usageMaxBudgetSmall && result == GpuResidencyAdmission::Pressure, "usage-uint64-max-pressure-no-overflow");
    auto maxBudgetAndUsage = budget.ReserveOwner(
        1, {1, (std::numeric_limits<std::uint64_t>::max)(), (std::numeric_limits<std::uint64_t>::max)() - 1}, result);
    Expect(!maxBudgetAndUsage && result == GpuResidencyAdmission::Pressure,
           "max-budget-headroom-subtraction-does-not-overflow");
    Expect(budget.ReservedOwner() == 0 && budget.ActiveOwner() == 0, "pressure-refusal-has-no-accounting");

    const GpuResidencySnapshot exactHeadroom{11, 1024ull * 1024 * 1024, 256ull * 1024 * 1024};
    auto exact = budget.ReserveOwner(640ull * 1024 * 1024, exactHeadroom, result);
    Expect(exact && result == GpuResidencyAdmission::Ready && exact.Bytes() == 640ull * 1024 * 1024,
           "one-gibibyte-headroom-is-128-mibibytes-available-is-640");
    auto overExact = budget.ReserveOwner(1, exactHeadroom, result);
    Expect(!overExact && result == GpuResidencyAdmission::Pressure, "one-byte-over-exact-available-refused");
    Expect(budget.ReservedOwner() == 640ull * 1024 * 1024 && budget.ActiveOwner() == 1,
           "exact-boundary-refusal-preserves-existing-charge");
    exact.ResetOwner();

    const GpuResidencySnapshot minimumBoundary{12, 512ull * 1024 * 1024, 0};
    auto exactlyMinimum = budget.ReserveOwner(448ull * 1024 * 1024, minimumBoundary, result);
    Expect(exactlyMinimum && result == GpuResidencyAdmission::Ready, "64-mibibyte-headroom-at-eighth-boundary");
    exactlyMinimum.ResetOwner();
    const GpuResidencySnapshot belowBoundary{13, 511ull * 1024 * 1024, 0};
    auto minimumApplied = budget.ReserveOwner(447ull * 1024 * 1024, belowBoundary, result);
    Expect(minimumApplied && result == GpuResidencyAdmission::Ready,
           "64-mibibyte-minimum-headroom-below-eighth-boundary");
    auto oneOverMinimum = budget.ReserveOwner(447ull * 1024 * 1024 + 1, belowBoundary, result);
    Expect(!oneOverMinimum && result == GpuResidencyAdmission::Pressure,
           "minimum-headroom-boundary-refuses-one-byte-over");
    minimumApplied.ResetOwner();

    auto usageAndReservation = budget.ReserveOwner(384ull * 1024 * 1024, kOneGiB, result);
    Expect(usageAndReservation && budget.ReservedOwner() == 384ull * 1024 * 1024,
           "usage-and-reservation-fixture-admitted");
    auto overlapsUsage = budget.ReserveOwner(257ull * 1024 * 1024, kOneGiB, result);
    Expect(!overlapsUsage && result == GpuResidencyAdmission::Pressure,
           "current-usage-and-reservations-count-separately");
    Expect(budget.ReservedOwner() == 384ull * 1024 * 1024 && budget.ActiveOwner() == 1,
           "overlap-refusal-keeps-existing-reservation");
    usageAndReservation.ResetOwner();
}

void TestConcurrentGenerationsAndCpuRollback()
{
    GpuResidencyBudget budget;
    GpuResidencyAdmission result = GpuResidencyAdmission::Invalid;
    auto oldGeneration = budget.ReserveOwner(200, {31, 128ull * 1024 * 1024, 0}, result);
    auto currentGeneration = budget.ReserveOwner(300, {32, 128ull * 1024 * 1024, 0}, result);
    Expect(oldGeneration && currentGeneration && oldGeneration.DeviceGeneration() == 31 &&
               currentGeneration.DeviceGeneration() == 32,
           "old-and-current-generations-held-concurrently");
    Expect(budget.ReservedOwner() == 500 && budget.ActiveOwner() == 2, "retired-generation-remains-charged");
    oldGeneration.ResetOwner();
    Expect(budget.ReservedOwner() == 300 && budget.ActiveOwner() == 1, "retired-generation-charge-released-explicitly");
    currentGeneration.ResetOwner();

    auto gpuReservation = budget.ReserveOwner(73, {33, 128ull * 1024 * 1024, 0}, result);
    const bool cpuReservationAccepted = false;
    if (!cpuReservationAccepted)
        gpuReservation.ResetOwner();
    Expect(!gpuReservation && budget.ReservedOwner() == 0 && budget.ActiveOwner() == 0,
           "cpu-refusal-rolls-back-gpu-reservation");
    auto afterRollback = budget.ReserveOwner(64ull * 1024 * 1024, {34, 128ull * 1024 * 1024, 0}, result);
    Expect(afterRollback && result == GpuResidencyAdmission::Ready && budget.ReservedOwner() == 64ull * 1024 * 1024,
           "cpu-rollback-loses-no-quota");
    afterRollback.ResetOwner();
}

void TestCapacityAndTicketExhaustion()
{
    GpuResidencyBudget budget;
    GpuResidencyAdmission result = GpuResidencyAdmission::Invalid;
    GpuResidencyReservation leases[128];
    bool filled = true;
    for (std::size_t i = 0; i < 128; ++i)
    {
        leases[i] = budget.ReserveOwner(1, {41, 1024ull * 1024 * 1024, 0}, result);
        filled = filled && leases[i] && result == GpuResidencyAdmission::Ready;
    }
    Expect(filled && budget.ActiveOwner() == 128 && budget.ReservedOwner() == 128, "all-128-fixed-leases-admitted");

    auto overCapacity = budget.ReserveOwner(1, {42, 1024ull * 1024 * 1024, 0}, result);
    Expect(!overCapacity && result == GpuResidencyAdmission::Capacity, "129th-lease-capacity-refused");
    Expect(budget.ActiveOwner() == 128 && budget.ReservedOwner() == 128, "capacity-refusal-does-not-change-accounting");

    leases[17].ResetOwner();
    auto reusedSlot = budget.ReserveOwner(2, {43, 1024ull * 1024 * 1024, 0}, result);
    Expect(reusedSlot && result == GpuResidencyAdmission::Ready && budget.ActiveOwner() == 128 &&
               budget.ReservedOwner() == 129,
           "empty-slot-reused-with-other-leases-held");
    reusedSlot.ResetOwner();
    for (auto& lease : leases)
        lease.ResetOwner();
    Expect(budget.ActiveOwner() == 0 && budget.ReservedOwner() == 0, "capacity-fixture-drains-to-zero");

    auto active = budget.ReserveOwner(1, {44, 128ull * 1024 * 1024, 0}, result);
    Expect(active && !budget.ExhaustTicketsForTest(), "ticket-exhaustion-hook-refuses-while-active");
    active.ResetOwner();
    Expect(budget.ExhaustTicketsForTest(), "ticket-exhaustion-hook-accepts-when-drained");
    auto exhausted = budget.ReserveOwner(1, {45, 128ull * 1024 * 1024, 0}, result);
    Expect(!exhausted && result == GpuResidencyAdmission::Exhausted, "maximum-next-ticket-refuses-without-wrap");
    Expect(budget.ActiveOwner() == 0 && budget.ReservedOwner() == 0, "ticket-exhaustion-refusal-keeps-zero-accounting");
}
} // namespace

int RunGpuResidencyBudgetTests()
{
    gFailures = 0;
    TestReservationLifecycle();
    TestOwnerThreadRules();
    TestAdmissionBoundaries();
    TestConcurrentGenerationsAndCpuRollback();
    TestCapacityAndTicketExhaustion();
    return gFailures;
}
