#include "Composition.Tests.GpuResidencyGrowth.h"

#include "GpuResidencyBudget.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>
#include <utility>

namespace RedPrism::CompositionTests
{
namespace
{
int gFailures = 0;
int gPassed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s gpu-residency-growth.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
    gPassed += condition ? 1 : 0;
}

constexpr std::uint64_t kMiB = 1024ull * 1024;
constexpr std::uint64_t kGiB = 1024ull * kMiB;
constexpr GpuResidencySnapshot kGrowthSnapshot{17, kGiB, 256ull * kMiB};

void TestExactBoundaryAndWholeLeaseRefund()
{
    GpuResidencyBudget budget;
    GpuResidencyAdmission admission = GpuResidencyAdmission::Invalid;
    auto target = budget.ReserveOwner(64ull * kMiB, kGrowthSnapshot, admission);
    auto otherGeneration = budget.ReserveOwner(512ull * kMiB, {18, kGiB, 256ull * kMiB}, admission);
    Expect(target && otherGeneration && admission == GpuResidencyAdmission::Ready,
           "boundary-fixture-reservations-admitted");
    Expect(budget.ReservedOwner() == 576ull * kMiB && budget.ActiveOwner() == 2,
           "boundary-fixture-charges-both-generations");

    const bool grew = budget.IncreaseOwner(target, 128ull * kMiB, kGrowthSnapshot, admission);
    Expect(grew && admission == GpuResidencyAdmission::Ready, "delta-growth-reaches-exact-640-mibibyte-boundary");
    Expect(target && target.Bytes() == 128ull * kMiB && target.DeviceGeneration() == 17,
           "growth-preserves-target-lease-and-generation");
    Expect(budget.ReservedOwner() == 640ull * kMiB && budget.ActiveOwner() == 2,
           "growth-charges-only-delta-with-retired-generation-held");

    const bool overBoundary = budget.IncreaseOwner(target, 128ull * kMiB + 1, kGrowthSnapshot, admission);
    Expect(!overBoundary && admission == GpuResidencyAdmission::Pressure,
           "one-byte-over-boundary-refused-for-pressure");
    Expect(target.Bytes() == 128ull * kMiB && budget.ReservedOwner() == 640ull * kMiB && budget.ActiveOwner() == 2,
           "pressure-refusal-preserves-target-and-all-accounting");

    auto movedGrown = std::move(target);
    Expect(!target && target.Bytes() == 0 && movedGrown && movedGrown.Bytes() == 128ull * kMiB,
           "moving-grown-lease-transfers-whole-grown-reservation");
    Expect(budget.ReservedOwner() == 640ull * kMiB && budget.ActiveOwner() == 2,
           "moving-grown-lease-keeps-single-charge");
    movedGrown.ResetOwner();
    movedGrown.ResetOwner();
    Expect(budget.ReservedOwner() == 512ull * kMiB && budget.ActiveOwner() == 1,
           "grown-lease-refunds-whole-amount-exactly-once");
    Expect(otherGeneration && otherGeneration.Bytes() == 512ull * kMiB && otherGeneration.DeviceGeneration() == 18 &&
               budget.ReservedOwner() == 512ull * kMiB,
           "other-generation-remains-charged-after-grown-refund");
    otherGeneration.ResetOwner();
}

void TestEqualSizeIsNoOpUnderPressure()
{
    GpuResidencyBudget budget;
    GpuResidencyAdmission admission = GpuResidencyAdmission::Invalid;
    auto lease = budget.ReserveOwner(64ull * kMiB, kGrowthSnapshot, admission);
    Expect(lease && admission == GpuResidencyAdmission::Ready, "equal-size-fixture-admitted");

    const GpuResidencySnapshot pressuredButKnown{17, kGiB, kGiB};
    const bool unchanged = budget.IncreaseOwner(lease, 64ull * kMiB, pressuredButKnown, admission);
    Expect(unchanged && admission == GpuResidencyAdmission::Ready, "equal-total-is-ready-with-no-headroom");
    Expect(lease && lease.Bytes() == 64ull * kMiB && lease.DeviceGeneration() == 17 &&
               budget.ReservedOwner() == 64ull * kMiB && budget.ActiveOwner() == 1,
           "equal-total-does-not-change-lease-or-accounting");
    lease.ResetOwner();
}

void TestInvalidInputsAndRefusalAtomicity()
{
    GpuResidencyBudget budget;
    GpuResidencyAdmission admission = GpuResidencyAdmission::Invalid;
    auto lease = budget.ReserveOwner(64ull * kMiB, kGrowthSnapshot, admission);
    Expect(lease && admission == GpuResidencyAdmission::Ready, "invalid-input-fixture-admitted");

    const auto unchanged = [&]() noexcept
    {
        return lease && lease.Bytes() == 64ull * kMiB && lease.DeviceGeneration() == 17 &&
               budget.ReservedOwner() == 64ull * kMiB && budget.ActiveOwner() == 1;
    };
    bool accepted = budget.IncreaseOwner(lease, 128ull * kMiB, {18, kGiB, 0}, admission);
    Expect(!accepted && admission == GpuResidencyAdmission::Invalid && unchanged(),
           "different-generation-refuses-without-mutation");
    accepted = budget.IncreaseOwner(lease, 128ull * kMiB, {0, kGiB, 0}, admission);
    Expect(!accepted && admission == GpuResidencyAdmission::Invalid && unchanged(),
           "zero-generation-refuses-without-mutation");
    accepted = budget.IncreaseOwner(lease, 128ull * kMiB, {17, 0, 0}, admission);
    Expect(!accepted && admission == GpuResidencyAdmission::Invalid && unchanged(),
           "zero-budget-refuses-without-mutation");
    accepted = budget.IncreaseOwner(lease, 0, kGrowthSnapshot, admission);
    Expect(!accepted && admission == GpuResidencyAdmission::Invalid && unchanged(),
           "zero-total-refuses-without-mutation");
    accepted = budget.IncreaseOwner(lease, 64ull * kMiB - 1, kGrowthSnapshot, admission);
    Expect(!accepted && admission == GpuResidencyAdmission::Invalid && unchanged(),
           "shrinking-total-refuses-without-mutation");
    accepted = budget.IncreaseOwner(lease, (std::numeric_limits<std::uint64_t>::max)(),
                                    {17, (std::numeric_limits<std::uint64_t>::max)(), 0}, admission);
    Expect(!accepted && admission == GpuResidencyAdmission::Pressure && unchanged(),
           "maximum-total-refuses-without-arithmetic-wrap");
    accepted = budget.IncreaseOwner(lease, 128ull * kMiB, {17, kGiB, kGiB + 1}, admission);
    Expect(!accepted && admission == GpuResidencyAdmission::Pressure && unchanged(),
           "usage-over-budget-refuses-without-mutation");

    GpuResidencyReservation movedFrom = std::move(lease);
    accepted = budget.IncreaseOwner(lease, 128ull * kMiB, kGrowthSnapshot, admission);
    Expect(!accepted && admission == GpuResidencyAdmission::Invalid && movedFrom.Bytes() == 64ull * kMiB &&
               budget.ReservedOwner() == 64ull * kMiB && budget.ActiveOwner() == 1,
           "moved-from-lease-refuses-without-mutation");
    movedFrom.ResetOwner();
    accepted = budget.IncreaseOwner(movedFrom, 128ull * kMiB, kGrowthSnapshot, admission);
    Expect(!accepted && admission == GpuResidencyAdmission::Invalid && budget.ReservedOwner() == 0 &&
               budget.ActiveOwner() == 0,
           "reset-lease-refuses-invalid");
}

void TestForeignBudgetAndThreadRefusal()
{
    GpuResidencyBudget ownerBudget;
    GpuResidencyBudget foreignBudget;
    GpuResidencyAdmission admission = GpuResidencyAdmission::Invalid;
    auto lease = ownerBudget.ReserveOwner(64, {23, 128ull * kMiB, 0}, admission);
    Expect(lease && admission == GpuResidencyAdmission::Ready, "foreign-owner-fixture-admitted");

    const bool foreignBudgetAccepted = foreignBudget.IncreaseOwner(lease, 128, {23, 128ull * kMiB, 0}, admission);
    Expect(!foreignBudgetAccepted && admission == GpuResidencyAdmission::Invalid && lease.Bytes() == 64 &&
               ownerBudget.ReservedOwner() == 64 && ownerBudget.ActiveOwner() == 1 &&
               foreignBudget.ReservedOwner() == 0 && foreignBudget.ActiveOwner() == 0,
           "foreign-budget-refuses-without-either-budget-mutation");

    bool workerAccepted = true;
    GpuResidencyAdmission workerAdmission = GpuResidencyAdmission::Ready;
    std::uint64_t workerReserved = 1;
    std::size_t workerActive = 1;
    std::thread worker(
        [&]() noexcept
        {
            workerAccepted = ownerBudget.IncreaseOwner(lease, 128, {23, 128ull * kMiB, 0}, workerAdmission);
            workerReserved = ownerBudget.ReservedOwner();
            workerActive = ownerBudget.ActiveOwner();
        });
    worker.join();

    Expect(!workerAccepted && workerAdmission == GpuResidencyAdmission::Invalid,
           "foreign-thread-growth-refuses-invalid");
    Expect(workerReserved == 0 && workerActive == 0 && lease.Bytes() == 64 && lease.DeviceGeneration() == 23,
           "foreign-thread-attempt-does-not-mutate-owner-or-lease");
    Expect(ownerBudget.ReservedOwner() == 64 && ownerBudget.ActiveOwner() == 1,
           "owner-accounting-survives-foreign-thread-refusal");
    lease.ResetOwner();
}

void TestGrowthAtFullSlotCapacity()
{
    GpuResidencyBudget budget;
    GpuResidencyAdmission admission = GpuResidencyAdmission::Invalid;
    GpuResidencyReservation leases[GpuResidencyBudget::kCapacity];
    bool filled = true;
    for (std::size_t i = 0; i < GpuResidencyBudget::kCapacity; ++i)
    {
        leases[i] = budget.ReserveOwner(1, {41, kGiB, 0}, admission);
        filled = filled && leases[i] && admission == GpuResidencyAdmission::Ready;
    }
    Expect(filled && budget.ActiveOwner() == GpuResidencyBudget::kCapacity &&
               budget.ReservedOwner() == GpuResidencyBudget::kCapacity,
           "all-128-slots-held-before-growth");

    const bool grew = budget.IncreaseOwner(leases[0], 2, {41, kGiB, 0}, admission);
    Expect(grew && admission == GpuResidencyAdmission::Ready && leases[0].Bytes() == 2,
           "growth-succeeds-with-no-free-slot");
    Expect(budget.ActiveOwner() == GpuResidencyBudget::kCapacity &&
               budget.ReservedOwner() == GpuResidencyBudget::kCapacity + 1,
           "full-capacity-growth-adds-delta-without-another-live-reservation");

    for (auto& held : leases)
        held.ResetOwner();
    Expect(budget.ReservedOwner() == 0 && budget.ActiveOwner() == 0, "full-capacity-growth-fixture-drains-cleanly");
}
} // namespace

int RunGpuResidencyGrowthTests()
{
    gFailures = 0;
    gPassed = 0;
    TestExactBoundaryAndWholeLeaseRefund();
    TestEqualSizeIsNoOpUnderPressure();
    TestInvalidInputsAndRefusalAtomicity();
    TestForeignBudgetAndThreadRefusal();
    TestGrowthAtFullSlotCapacity();
    std::printf("SUMMARY gpu-residency-growth passed=%d failed=%d\n", gPassed, gFailures);
    return gFailures;
}
} // namespace RedPrism::CompositionTests
