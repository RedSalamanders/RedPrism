#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Accepted roots have exactly the form X:\RedPrism.Perf, with an ASCII drive letter.
[[nodiscard]] bool IsValidPerfRootPath(std::wstring_view path) noexcept;

// Ownership marker: exact filename ".redprism-perf-owner" and exact ASCII bytes
// "RedPrism performance root owner v1\n" (without a trailing NUL).
// Resolves or creates the owned performance sandbox. Allocation failure is reported as E_OUTOFMEMORY.
[[nodiscard]] std::int32_t ResolvePerfRoot(std::wstring& root) noexcept;
