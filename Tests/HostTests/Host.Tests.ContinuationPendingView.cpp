#include "Host.Tests.ContinuationPendingView.h"

#include "ContinuationQueue.h"

#include <array>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
using Queue = ContinuationQueue<int>;
static_assert(std::is_same_v<decltype(std::declval<const Queue&>().PendingAt(0)), const Queue::Entry*>);
static_assert(noexcept(std::declval<const Queue&>().PendingAt(0)));
void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s continuation-pending-view.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}
} // namespace

int RunContinuationPendingViewTests()
{
    int failures = 0;
    Queue queue;
    constexpr auto maximum = (std::numeric_limits<std::size_t>::max)();
    Expect(!queue.PendingAt(0) && !queue.PendingAt(maximum), "empty-bounds", failures);
    bool admitted = true;
    for (int value : std::array{101, 102, 103, 104, 105, 106, 107, 108})
        admitted = queue.Enqueue(17, static_cast<std::uint64_t>(value), ContinuationKind::Command, value) ==
                       ContinuationAdmission::Queued &&
                   admitted;
    Expect(admitted && queue.Size() == 8, "eight-accepted-inputs", failures);
    Expect(!queue.PendingAt(8) && !queue.PendingAt(maximum), "full-bounds", failures);
    bool initial = true;
    std::size_t offset = 0;
    for (int value : std::array{101, 102, 103, 104, 105, 106, 107, 108})
    {
        const auto* entry = queue.PendingAt(offset++);
        initial = entry && entry->input == value && entry->documentSerial == 17 && initial;
    }
    Expect(initial && queue.Size() == 8, "literal-initial-order-without-consumption", failures);
    bool consumed = true;
    for (int value : std::array{101, 102, 103})
    {
        auto next = queue.TakeReady(17, false);
        consumed = next.ready && next.ready.value().input == value && consumed;
    }
    Expect(consumed && queue.Size() == 5, "literal-prefix-consumption", failures);
    bool wrapped = true;
    for (int value : std::array{109, 110, 111})
        wrapped = queue.Enqueue(17, static_cast<std::uint64_t>(value), ContinuationKind::Command, value) ==
                      ContinuationAdmission::Queued &&
                  wrapped;
    offset = 0;
    for (int value : std::array{104, 105, 106, 107, 108, 109, 110, 111})
    {
        const auto* entry = queue.PendingAt(offset++);
        wrapped = entry && entry->input == value && wrapped;
    }
    Expect(wrapped && queue.Size() == 8, "literal-wrapped-order-without-consumption", failures);
    auto next = queue.TakeReady(17, false);
    Expect(next.ready && next.ready.value().input == 104 && queue.Size() == 7 && !queue.PendingAt(7),
           "inspection-retains-next-input-and-bounds", failures);
    Expect(queue.Clear() == 7 && !queue.PendingAt(0), "clear-removes-all-views", failures);
    return failures;
}
