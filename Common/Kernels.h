#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "Executor.h"

struct GrainSettings;
namespace Adjust
{
struct ChannelTables;
}

enum class KernelIsa : std::uint8_t
{
    Auto,
    Scalar,
    Sse2,
    Avx2,
    Avx512,
    Neon
};

[[nodiscard]] constexpr std::optional<KernelIsa> ParseKernelIsa(std::wstring_view value) noexcept
{
    if (value == L"scalar")
        return KernelIsa::Scalar;
    if (value == L"sse2")
        return KernelIsa::Sse2;
    if (value == L"avx2")
        return KernelIsa::Avx2;
    if (value == L"avx512")
        return KernelIsa::Avx512;
    if (value == L"neon")
        return KernelIsa::Neon;
    return std::nullopt;
}

[[nodiscard]] constexpr const wchar_t* KernelIsaName(KernelIsa isa) noexcept
{
    switch (isa)
    {
    case KernelIsa::Auto:
        return L"auto";
    case KernelIsa::Scalar:
        return L"scalar";
    case KernelIsa::Sse2:
        return L"sse2";
    case KernelIsa::Avx2:
        return L"avx2";
    case KernelIsa::Avx512:
        return L"avx512";
    case KernelIsa::Neon:
        return L"neon";
    }
    return L"invalid";
}

// Immutable process-lifetime tables. Dispatch happens once per kernel call, never inside the pixel loop.
// Scalar means the portable reference math compiled at the platform baseline (SSE2 on x64, NEON on ARM64).
struct KernelTable final
{
    KernelIsa isa;
    void (*applyTables)(std::uint8_t*, int, int, int, const Adjust::ChannelTables&, const CancelToken&) noexcept;
    void (*applyGradientMap)(std::uint8_t*, int, int, int, const std::array<std::uint8_t, 768>&,
                             const CancelToken&) noexcept;
    void (*applyGrain)(std::uint8_t*, int, int, int, const GrainSettings&, double, double, double,
                       const CancelToken&) noexcept;
    void (*addNoise)(std::uint8_t*, int, int, int, float, bool, bool, std::uint32_t, const CancelToken&) noexcept;
    void (*lensDistort)(const std::uint8_t*, std::uint8_t*, int, int, int, double, const CancelToken&) noexcept;
    void (*applyCube)(std::uint8_t*, int, int, int, const std::vector<float>&, const CancelToken&) noexcept;
};

struct KernelCapabilities final
{
    bool sse2 = false;
    bool avx2 = false;
    bool avx512 = false;
    bool neon = false;
};

// Raw feature input makes every missing CPUID/OS-state bit testable on any host. Missing leaves stay zero.
struct X64KernelFeatures final
{
    std::uint32_t leaf1Ecx = 0;
    std::uint32_t leaf1Edx = 0;
    std::uint32_t leaf7Ebx = 0;
    std::uint64_t xcr0 = 0;
};
[[nodiscard]] KernelCapabilities DecodeX64KernelFeatures(const X64KernelFeatures& features) noexcept;
[[nodiscard]] KernelCapabilities DetectKernelCapabilities() noexcept;
[[nodiscard]] bool KernelIsaImplemented(KernelIsa isa) noexcept;
enum class KernelSelection : std::uint8_t
{
    Selected,
    UnsupportedHardware,
    NotImplemented
};
[[nodiscard]] KernelSelection ValidateKernelIsa(KernelIsa isa, KernelCapabilities capabilities) noexcept;
// Called before workers start. A refused override leaves the previous table intact; no silent fallback.
[[nodiscard]] KernelSelection InitializeKernels(KernelIsa requested = KernelIsa::Auto) noexcept;
// Tests change tiers only while no kernels/jobs are in flight, and restore Auto before returning.
[[nodiscard]] KernelSelection SetIsaForTest(KernelIsa requested) noexcept;
[[nodiscard]] const KernelTable& ActiveKernels() noexcept;
