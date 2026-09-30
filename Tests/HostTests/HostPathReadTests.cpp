#include "../../RedPrism/ContinuationRequests.h"

#include "HostPathReadTests.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <new>
#include <span>
#include <string_view>
#include <utility>

namespace
{
using Queue = ContinuationQueue<HostRequest>;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s host-path-read.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

void Retire(Queue::Entry&& entry, ContinuationRetirement retirement) noexcept
{
    entry.input.Retire(retirement);
}

void TestUnicodeOrderedBatch()
{
    constexpr std::array<std::wstring_view, 3> expected{
        L"D:\\Images\\été\\第三.png",
        L"E:\\Photos\\über.tif",
        L"F:\\素材\\朝.jpg",
    };
    constexpr std::array<std::size_t, expected.size()> lengths{expected[0].size(), expected[1].size(),
                                                               expected[2].size()};
    HostInputBudget budget;
    HostPathReadFailure failure = HostPathReadFailure::Read;
    std::size_t calls = 0;
    auto request = HostRequest::TryReadPaths(
        lengths, HostPathAction::ImagesAsLayers, Vec2{12.5, -8.25}, budget,
        [&calls, &expected](std::size_t index, std::span<wchar_t> destination) noexcept
        {
            ++calls;
            if (index >= expected.size() || destination.size() != expected[index].size() + 1)
            {
                return false;
            }
            std::copy(expected[index].begin(), expected[index].end(), destination.begin());
            destination.back() = L'\0';
            return true;
        },
        failure);

    const auto* paths = request ? std::get_if<HostPathsInput>(&request->Input()) : nullptr;
    Expect(request && failure == HostPathReadFailure::None && calls == expected.size() && paths &&
               paths->paths.size() == expected.size() && paths->paths[0] == expected[0] &&
               paths->paths[1] == expected[1] && paths->paths[2] == expected[2] &&
               paths->action == HostPathAction::ImagesAsLayers && paths->dropPoint == Vec2{12.5, -8.25},
           "unicode-reader-produces-exact-ordered-owned-batch");
    Expect(request && budget.Used() > 0, "successful-reader-batch-retains-budget-charge");
    request.reset();
    Expect(budget.Used() == 0, "successful-reader-batch-releases-charge-on-owner-destruction");
}

void TestMaximumCountAndPathLength()
{
    std::array<std::size_t, HostInputBudget::kMaxPaths> lengths{};
    lengths[0] = HostInputBudget::kMaxPathUnits;
    HostInputBudget budget;
    HostPathReadFailure failure = HostPathReadFailure::Read;
    std::size_t calls = 0;
    auto request = HostRequest::TryReadPaths(
        lengths, HostPathAction::OpenPaths, std::nullopt, budget,
        [&calls](std::size_t index, std::span<wchar_t> destination) noexcept
        {
            ++calls;
            if (destination.size() != (index == 0 ? HostInputBudget::kMaxPathUnits + 1 : 1))
            {
                return false;
            }
            std::fill(destination.begin(), destination.end() - 1, L'p');
            destination.back() = L'\0';
            return true;
        },
        failure);
    const auto* paths = request ? std::get_if<HostPathsInput>(&request->Input()) : nullptr;
    Expect(request && failure == HostPathReadFailure::None && calls == HostInputBudget::kMaxPaths && paths &&
               paths->paths.size() == HostInputBudget::kMaxPaths &&
               paths->paths.front().size() == HostInputBudget::kMaxPathUnits && paths->paths.front().front() == L'p' &&
               paths->paths.back().empty(),
           "reader-accepts-256-paths-and-32767-unit-boundary");
    request.reset();
    Expect(budget.Used() == 0, "maximum-reader-batch-releases-charge");
}

void TestPreflightAndAggregateQuotaRefusal()
{
    HostInputBudget budget;
    HostPathReadFailure failure = HostPathReadFailure::None;
    std::size_t calls = 0;
    std::array<std::size_t, HostInputBudget::kMaxPaths + 1> tooMany{};
    auto overCount = HostRequest::TryReadPaths(
        tooMany, HostPathAction::OpenPaths, std::nullopt, budget,
        [&calls](std::size_t, std::span<wchar_t>) noexcept
        {
            ++calls;
            return true;
        },
        failure, HostCopyFailureForTest::Input);
    Expect(!overCount && failure == HostPathReadFailure::Quota && calls == 0 && budget.Used() == 0,
           "257-path-count-refuses-before-reader-call");

    const std::array<std::size_t, 1> tooLong{HostInputBudget::kMaxPathUnits + 1};
    auto overLength = HostRequest::TryReadPaths(
        tooLong, HostPathAction::OpenPaths, std::nullopt, budget,
        [&calls](std::size_t, std::span<wchar_t>) noexcept
        {
            ++calls;
            return true;
        },
        failure, HostCopyFailureForTest::Input);
    Expect(!overLength && failure == HostPathReadFailure::Quota && calls == 0 && budget.Used() == 0,
           "32768-unit-path-refuses-before-reader-call");

    std::array<std::size_t, 33> lengths{};
    lengths.fill(HostInputBudget::kMaxPathUnits);
    auto overAggregate = HostRequest::TryReadPaths(
        lengths, HostPathAction::ImportImages, std::nullopt, budget,
        [&calls](std::size_t, std::span<wchar_t>) noexcept
        {
            ++calls;
            return true;
        },
        failure, HostCopyFailureForTest::Input);
    Expect(!overAggregate && failure == HostPathReadFailure::Quota && calls == 0 && budget.Used() == 0,
           "aggregate-over-one-mib-refuses-before-any-reader-call");

    const std::array<std::size_t, 1> smallLength{4};
    auto fullBudget = budget.TryReserve(HostInputBudget::kStorageLimit);
    failure = HostPathReadFailure::None;
    auto atQuota = HostRequest::TryReadPaths(
        smallLength, HostPathAction::OpenPaths, std::nullopt, budget,
        [&calls](std::size_t, std::span<wchar_t>) noexcept
        {
            ++calls;
            return true;
        },
        failure, HostCopyFailureForTest::Input);
    Expect(fullBudget && !atQuota && failure == HostPathReadFailure::Quota && calls == 0 &&
               budget.Used() == HostInputBudget::kStorageLimit,
           "budget-quota-refuses-before-reader-and-injected-input-fault");
}

void TestReadFailureRollsBackWholeBatch()
{
    HostInputBudget budget;
    auto preceding = budget.TryReserve(128);
    const std::array<std::size_t, 3> lengths{5, 6, 7};
    HostPathReadFailure failure = HostPathReadFailure::None;
    std::size_t calls = 0;
    auto request = HostRequest::TryReadPaths(
        lengths, HostPathAction::OpenPaths, std::nullopt, budget,
        [&calls](std::size_t index, std::span<wchar_t> destination) noexcept
        {
            ++calls;
            if (index == 1)
            {
                return false;
            }
            std::fill(destination.begin(), destination.end() - 1, L'r');
            destination.back() = L'\0';
            return true;
        },
        failure);
    Expect(preceding && !request && failure == HostPathReadFailure::Read && calls == 2 && budget.Used() == 128,
           "mid-batch-read-failure-rolls-back-all-new-reservation");
}

void TestInjectedAllocationFailures()
{
    HostInputBudget budget;
    auto preceding = budget.TryReserve(128);
    const std::array<std::size_t, 2> lengths{5, 6};
    const auto reader = [](std::size_t, std::span<wchar_t> destination) noexcept
    {
        std::fill(destination.begin(), destination.end() - 1, L'f');
        destination.back() = L'\0';
        return true;
    };
    HostPathReadFailure failure = HostPathReadFailure::None;
    std::size_t inputCalls = 0;
    bool inputThrew = false;
    try
    {
        auto request = HostRequest::TryReadPaths(
            lengths, HostPathAction::OpenPaths, std::nullopt, budget,
            [&inputCalls, &reader](std::size_t index, std::span<wchar_t> destination) noexcept
            {
                ++inputCalls;
                return reader(index, destination);
            },
            failure, HostCopyFailureForTest::Input);
        (void)request;
    }
    catch (const std::bad_alloc&)
    {
        inputThrew = true;
    }
    Expect(preceding && inputThrew && inputCalls == 1 && budget.Used() == 128,
           "input-fault-after-first-path-releases-reservation");

    std::size_t receiptCalls = 0;
    bool receiptThrew = false;
    try
    {
        auto request = HostRequest::TryReadPaths(
            lengths, HostPathAction::OpenPaths, std::nullopt, budget,
            [&receiptCalls, &reader](std::size_t index, std::span<wchar_t> destination) noexcept
            {
                ++receiptCalls;
                return reader(index, destination);
            },
            failure, HostCopyFailureForTest::Receipt);
        (void)request;
    }
    catch (const std::bad_alloc&)
    {
        receiptThrew = true;
    }
    Expect(receiptThrew && receiptCalls == lengths.size() && budget.Used() == 128,
           "receipt-fault-after-whole-batch-releases-reservation");
}

void TestMoveQueueAndObservedTerminalLifetime()
{
    Queue queue;
    HostInputBudget budget;
    const std::array<std::size_t, 2> lengths{5, 6};
    HostPathReadFailure failure = HostPathReadFailure::None;
    auto created = HostRequest::TryReadPaths(
        lengths, HostPathAction::OpenPaths, std::nullopt, budget,
        [](std::size_t index, std::span<wchar_t> destination) noexcept
        {
            std::fill(destination.begin(), destination.end() - 1, index == 0 ? L'a' : L'b');
            destination.back() = L'\0';
            return true;
        },
        failure);
    if (!created)
    {
        Expect(false, "reader-request-created-for-queue-lifetime");
        return;
    }

    HostRequest moved = std::move(created.value());
    const auto observer = moved.Receipt();
    const std::size_t charge = budget.Used();
    const auto admitted = queue.Enqueue(41, 1, ContinuationKind::Import, moved, Retire);
    auto taken = queue.TakeReady(41, false, Retire);
    const auto* paths = taken.ready ? std::get_if<HostPathsInput>(&taken.ready.value().input.Input()) : nullptr;
    const bool finished = taken.ready && taken.ready.value().input.Finish(ContinuationOutcome::Succeeded);
    Expect(admitted == ContinuationAdmission::Queued && taken.ready && paths && paths->paths.size() == 2 &&
               paths->paths[0] == L"aaaaa" && paths->paths[1] == L"bbbbbb" && finished &&
               observer.Read().outcome == ContinuationOutcome::Succeeded && budget.Used() == charge,
           "moved-request-keeps-batch-charge-and-observer-through-queue-terminal");
    taken.ready.reset();
    Expect(budget.Used() == 0 && observer.Read().outcome == ContinuationOutcome::Succeeded,
           "terminal-observer-survives-owner-release-and-charge-returns");
}
} // namespace

int RunHostPathReadTests()
{
    TestUnicodeOrderedBatch();
    TestMaximumCountAndPathLength();
    TestPreflightAndAggregateQuotaRefusal();
    TestReadFailureRollsBackWholeBatch();
    TestInjectedAllocationFailures();
    TestMoveQueueAndObservedTerminalLifetime();
    return gFailures;
}
