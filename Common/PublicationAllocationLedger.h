#pragma once

#include <cstdint>
#include <memory>

class SharedPublicationMemory;

// Standard-only bridge: allocation templates need no scheduler or host types. The implementation
// retains and charges the exact original publication owner; zero/overflow/sealed admission refuses.
[[nodiscard]] bool TryAccountPublicationAllocation(const std::shared_ptr<const SharedPublicationMemory>& memory,
                                                   std::uint64_t bytes) noexcept;
void ReleasePublicationAllocation(const std::shared_ptr<const SharedPublicationMemory>& memory,
                                  std::uint64_t bytes) noexcept;
