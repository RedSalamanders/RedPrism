#include "SelectionStorageTests.h"

#include "Document.h"

#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace
{
int gSelectionStorageFailures = 0;

void ExpectSelectionStorage(bool condition, const char* name)
{
    std::printf("%s selectionstorage.%s\n", condition ? "PASS" : "FAIL", name);
    gSelectionStorageFailures += condition ? 0 : 1;
}

SelectionLoops MakeRoomyLoops()
{
    std::vector<SelectionLoop> loops;
    loops.reserve(7);
    loops.emplace_back();
    loops.back().reserve(11);
    loops.back().push_back(Vec2{0, 0});
    loops.back().push_back(Vec2{6, 0});
    loops.back().push_back(Vec2{6, 8});
    loops.back().push_back(Vec2{0, 8});
    return SelectionLoops(std::move(loops));
}

void EmptyConstructionReportsNoCharge()
{
    const SelectionLoops defaultEmpty;
    const SelectionLoops vectorEmpty(std::vector<SelectionLoop>{});
    std::vector<SelectionLoop> reservedEmpty;
    reservedEmpty.reserve(5);
    const SelectionLoops reservedVectorEmpty(std::move(reservedEmpty));
    const SelectionLoops initializerEmpty{};
    ExpectSelectionStorage(defaultEmpty.StorageBytes() == 0 && vectorEmpty.StorageBytes() == 0 &&
                               reservedVectorEmpty.StorageBytes() == 0 && initializerEmpty.StorageBytes() == 0,
                           "empty-construction-zero");
}

void RetainedCapacityIsChargedWithoutChangingGeometry()
{
    const SelectionLoops tight{{Vec2{0, 0}, Vec2{6, 0}, Vec2{6, 8}, Vec2{0, 8}}};
    const SelectionLoops roomy = MakeRoomyLoops();
    ExpectSelectionStorage(tight == roomy, "spare-capacity-keeps-geometry-equal");
    ExpectSelectionStorage(tight.StorageBytes() > 0 && roomy.StorageBytes() > 0, "nonempty-storage-has-charge");
    ExpectSelectionStorage(roomy.StorageBytes() > tight.StorageBytes(), "reserved-spare-capacity-increases-charge");
}

void CopiesShareCachedStorageCharge()
{
    const SelectionLoops original = MakeRoomyLoops();
    const SelectionLoops shared = original;
    ExpectSelectionStorage(shared.Identity() == original.Identity(), "copy-retains-shared-identity");
    ExpectSelectionStorage(shared.StorageBytes() == original.StorageBytes(), "copy-retains-cached-charge");
    ExpectSelectionStorage(shared == original, "shared-copy-keeps-geometry");
}

void MovesTransferChargeAndResetDonors()
{
    SelectionLoops source = MakeRoomyLoops();
    const void* sourceIdentity = source.Identity();
    const std::uint64_t expected = source.StorageBytes();
    SelectionLoops moved(std::move(source));
    ExpectSelectionStorage(moved.Identity() == sourceIdentity && moved.StorageBytes() == expected &&
                               source.Identity() == nullptr && source.StorageBytes() == 0,
                           "move-construction-transfers-identity-and-resets-donor");

    SelectionLoops assignmentSource = MakeRoomyLoops();
    const void* assignmentIdentity = assignmentSource.Identity();
    const std::uint64_t assignmentExpected = assignmentSource.StorageBytes();
    SelectionLoops destination{{Vec2{-1, -1}, Vec2{1, 1}}};
    destination = std::move(assignmentSource);
    ExpectSelectionStorage(destination.Identity() == assignmentIdentity &&
                               destination.StorageBytes() == assignmentExpected &&
                               assignmentSource.Identity() == nullptr && assignmentSource.StorageBytes() == 0,
                           "move-assignment-transfers-identity-and-resets-donor");
}
} // namespace

int RunSelectionStorageTests()
{
    gSelectionStorageFailures = 0;
    EmptyConstructionReportsNoCharge();
    RetainedCapacityIsChargedWithoutChangingGeometry();
    CopiesShareCachedStorageCharge();
    MovesTransferChargeAndResetDonors();
    return gSelectionStorageFailures;
}
