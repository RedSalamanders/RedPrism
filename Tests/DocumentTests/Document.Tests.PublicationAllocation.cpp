#include "Document.Tests.PublicationAllocation.h"

#include "AccountedPublicationObject.h"
#include "SharedPublicationMemory.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace
{
int gFailures = 0;
int gChecks = 0;
void Expect(bool condition, const char* label) noexcept
{
    ++gChecks;
    gFailures += condition ? 0 : 1;
    std::printf("%s publication-allocation.%s\n", condition ? "PASS" : "FAIL", label);
}

struct Bytes final
{
    explicit Bytes(std::size_t count) : pixels(count, std::uint8_t{37}) {}
    std::vector<std::uint8_t> pixels;
};

struct RefusedConstruction final
{
    explicit RefusedConstruction(bool refuse)
    {
        if (refuse)
            throw std::bad_alloc{};
    }
};

struct RefusedControl final
{
    explicit RefusedControl(std::shared_ptr<const SharedPublicationMemory> memory) : original(std::move(memory))
    {
        filler = (std::numeric_limits<std::uint64_t>::max)() - original->LocalAllocationBytes();
        if (!original->TryAccountLocalAllocation(filler))
            throw std::bad_alloc{};
    }
    ~RefusedControl() noexcept
    {
        original->ReleaseLocalAllocation(filler);
    }
    std::shared_ptr<const SharedPublicationMemory> original;
    std::uint64_t filler = 0;
};
} // namespace

int RunPublicationAllocationTests()
{
    gFailures = 0;
    gChecks = 0;
    {
        auto original = SharedPublicationMemory::CreateForWork();
        auto foreign = SharedPublicationMemory::CreateForWork();
        auto value = MakeAccountedPublicationObject<Bytes>(original, 16384, 257, std::size_t{257});
        const auto strong = original->LocalAllocationBytes();
        Expect(value->pixels.size() == 257 && value->pixels.front() == 37 && value->pixels.back() == 37,
               "literal-published-data-remains-readable");
        Expect(strong >= 257 && strong <= AccountedPublicationObjectAllowance<Bytes>() + 257 &&
                   foreign->LocalAllocationBytes() == 0,
               "new-object-and-control-charge-only-original-owner");
        std::weak_ptr<Bytes> weak = value;
        value.reset();
        const auto control = original->LocalAllocationBytes();
        Expect(weak.expired() && control != 0 && control < strong - 257,
               "expired-data-refunds-data-but-keeps-weak-control-charge");
        weak.reset();
        Expect(original->LocalAllocationBytes() == 0 && foreign->LocalAllocationBytes() == 0,
               "last-weak-control-refunds-exact-original-local-charge");
    }
    {
        auto original = SharedPublicationMemory::CreateForWork();
        auto older = MakeAccountedPublicationObject<Bytes>(original, 16384, 19, std::size_t{19});
        const auto oldBytes = original->LocalAllocationBytes();
        bool refused = false;
        try
        {
            (void)MakeAccountedPublicationObject<RefusedConstruction>(original, 16384, 0,
                                                                      original->LocalAllocationBytes() != 0);
        }
        catch (const std::bad_alloc&)
        {
            refused = true; // Independent worker-construction failure fixture; no live publication changes.
        }
        Expect(refused && original->LocalAllocationBytes() == oldBytes && older->pixels.size() == 19,
               "throwing-value-construction-refunds-private-allowance-and-preserves-old-object");
        older.reset();
        Expect(original->LocalAllocationBytes() == 0, "surviving-predecessor-destruction-returns-zero");
    }
    {
        auto original = SharedPublicationMemory::CreateForWork();
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        Expect(!original->TryAccountLocalAllocation(0) && original->LocalAllocationBytes() == 0,
               "zero-accounting-refuses-without-change");
        const auto full = original->TryAccountLocalAllocation(maximum);
        Expect(full && !original->TryAccountLocalAllocation(1) && original->LocalAllocationBytes() == maximum,
               "checked-counter-overflow-refuses-and-preserves-previous-value");
        if (full)
            original->ReleaseLocalAllocation(maximum);
        Expect(original->LocalAllocationBytes() == 0, "synthetic-counter-boundary-release-is-exact");
    }
    {
        auto original = SharedPublicationMemory::CreateForWork();
        auto older = MakeAccountedPublicationObject<Bytes>(original, 16384, 23, std::size_t{23});
        const auto previous = original->LocalAllocationBytes();
        bool refused = false;
        try
        {
            (void)MakeAccountedPublicationObject<RefusedControl>(original, 16384, 0, original);
        }
        catch (const std::bad_alloc&)
        {
            refused = true; // Fill only the diagnostic counter to force shared-control admission to refuse.
        }
        Expect(refused && original->LocalAllocationBytes() == previous && older->pixels.size() == 23,
               "shared-control-refusal-destroys-private-node-and-preserves-predecessor-charge");
        older.reset();
        Expect(original->LocalAllocationBytes() == 0, "control-failure-cleanup-returns-exact-zero");
    }
    {
        auto original = SharedPublicationMemory::CreateForWork();
        bool refused = false;
        try
        {
            (void)MakeAccountedPublicationObject<Bytes>(original, 16384, (std::numeric_limits<std::uint64_t>::max)(),
                                                        std::size_t{0});
        }
        catch (const std::bad_alloc&)
        {
            refused = true; // Checked local-allowance arithmetic, before any selected object allocation.
        }
        Expect(refused && original->LocalAllocationBytes() == 0,
               "factory-allowance-overflow-refuses-before-allocation");
    }
    if (gChecks != 12)
        ++gFailures;
    std::printf("%s publication-allocation.summary checks=%d failures=%d\n", gFailures == 0 ? "PASS" : "FAIL", gChecks,
                gFailures);
    return gFailures;
}
