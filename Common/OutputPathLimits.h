#pragma once

#include <cstddef>

// Application-owned paths stay within this counted UTF-16 buffer, including the final terminator. Writers
// additionally need their exclusive sibling suffix and, for projects, the longest staged asset/codec name.
inline constexpr std::size_t kOwnedOutputPathUnits = 32767;
inline constexpr std::size_t kUniqueOutputSuffixUnits = 2 + 36; // .~ + canonical GUID
inline constexpr std::size_t kMaxImageOutputPathUnits = kOwnedOutputPathUnits - 1 - kUniqueOutputSuffixUnits;
inline constexpr std::size_t kMaxProjectOutputPathUnits =
    kOwnedOutputPathUnits - 1 - 2 * kUniqueOutputSuffixUnits - 8 - 36 - 9; // \images\ + GUID.mask.png
