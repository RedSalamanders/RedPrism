#include "Document.Tests.PublicationBudget.h"

#include "AccountedPublicationObject.h"
#include "SharedPublicationMemory.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>

namespace
{
int failures = 0;
int checks = 0;
void Expect(bool condition, const char* label) noexcept
{
    ++checks;
    failures += condition ? 0 : 1;
    std::printf("%s publication-budget.%s\n", condition ? "PASS" : "FAIL", label);
}
struct Value final
{
    explicit Value(int& constructions) : bytes(17, std::uint8_t{83})
    {
        ++constructions;
    }
    std::vector<std::uint8_t> bytes;
};
} // namespace

int RunPublicationBudgetTests()
{
    failures = checks = 0;
    {
        auto owner = SharedPublicationMemory::CreateForWork(32);
        Expect(owner->TryAccountLocalAllocation(32) && owner->LocalAllocationBytes() == 32,
               "literal-limit-admits-exactly-thirty-two");
        Expect(!owner->TryAccountLocalAllocation(1) && owner->LocalAllocationBytes() == 32,
               "one-byte-over-limit-refuses-preserving-charge");
        owner->ReleaseLocalAllocation(32);
        Expect(owner->LocalAllocationBytes() == 0, "exact-release-restores-empty-counter");
    }
    {
        auto owner = SharedPublicationMemory::CreateForWork(0);
        int constructions = 0;
        bool refused = false;
        try
        {
            (void)MakeAccountedPublicationObject<Value>(owner, 16384, 17, constructions);
        }
        catch (const std::bad_alloc&)
        {
            refused = true;
        }
        Expect(refused && constructions == 0 && owner->LocalAllocationBytes() == 0,
               "empty-envelope-refuses-before-value-construction");
    }
    {
        constexpr auto limit = AccountedPublicationObjectAllowance<Value>() + 17;
        auto owner = SharedPublicationMemory::CreateForWork(limit);
        int constructions = 0;
        auto value = MakeAccountedPublicationObject<Value>(owner, 16384, 17, constructions);
        const auto strong = owner->LocalAllocationBytes();
        Expect(constructions == 1 && strong > 17 && strong <= limit && value->bytes.size() == 17 &&
                   value->bytes.front() == 83 && value->bytes.back() == 83,
               "owned-value-remains-literal-within-original-envelope");
        Expect(owner->SealLocalAllocations() && !owner->SealLocalAllocations() &&
                   !owner->TryAccountLocalAllocation(1) && owner->LocalAllocationBytes() == strong,
               "seal-is-one-way-and-refuses-late-allocation");
        bool refused = false;
        try
        {
            (void)MakeAccountedPublicationObject<Value>(owner, 16384, 17, constructions);
        }
        catch (const std::bad_alloc&)
        {
            refused = true;
        }
        Expect(refused && constructions == 1 && owner->LocalAllocationBytes() == strong && value->bytes.front() == 83,
               "late-factory-refuses-before-construction-preserving-published-value");
        std::weak_ptr<Value> weak = value;
        value.reset();
        Expect(weak.expired() && owner->LocalAllocationBytes() > 0 && owner->LocalAllocationBytes() < strong,
               "sealed-owner-refunds-dead-data-and-keeps-weak-control-charge");
        weak.reset();
        Expect(owner->LocalAllocationBytes() == 0 && !owner->TryAccountLocalAllocation(1),
               "last-weak-refunds-exactly-without-reopening-admission");
    }
    if (checks != 9)
        ++failures;
    std::printf("%s publication-budget.summary checks=%d failures=%d\n", failures == 0 ? "PASS" : "FAIL", checks,
                failures);
    return failures;
}
