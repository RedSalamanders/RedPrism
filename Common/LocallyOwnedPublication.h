#pragma once

#include "AccountedPublicationObject.h"

// Selected producers declare only new locally owned dynamic children. Older shared roots remain owned by
// their original controls and are never recharged. Empty memory preserves the reference construction route.
template <class T, class... Args>
[[nodiscard]] std::shared_ptr<T> MakeLocallyOwnedPublicationObject(
    std::shared_ptr<const SharedPublicationMemory> memory, std::uint64_t retirementBytes,
    std::uint64_t newlyOwnedChildren, Args&&... args)
{
    if (memory)
        return MakeAccountedPublicationObject<T>(std::move(memory), retirementBytes, newlyOwnedChildren,
                                                 std::forward<Args>(args)...);
    return MakePublicationObject<T>({}, retirementBytes, std::forward<Args>(args)...);
}

template <class T> [[nodiscard]] constexpr std::uint64_t LocallyOwnedPublicationAllowance() noexcept
{
    return AccountedPublicationObjectAllowance<T>();
}
