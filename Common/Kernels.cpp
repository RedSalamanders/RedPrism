#include "Kernels.h"

#include "Kernels_Scalar_Adjust.h"

#include <atomic>
#if defined(_M_X64)
#include <intrin.h>
#endif

namespace
{
constexpr KernelTable kScalar{KernelIsa::Scalar,
                              &Kernels_Scalar_Adjust_ApplyTables,
                              &Kernels_Scalar_Adjust_ApplyGradientMap,
                              &Kernels_Scalar_Adjust_ApplyGrain,
                              &Kernels_Scalar_Adjust_AddNoise,
                              &Kernels_Scalar_Adjust_LensDistort,
                              &Kernels_Scalar_Adjust_ApplyCube};
std::atomic<const KernelTable*> gActive{&kScalar};
constexpr std::uint32_t kAvxLeaf1 = (1u << 12) | (1u << 26) | (1u << 27) | (1u << 28) | (1u << 29);
constexpr std::uint32_t kAvxLeaf7 = (1u << 5) | (1u << 8);
} // namespace

KernelCapabilities DecodeX64KernelFeatures(const X64KernelFeatures& features) noexcept
{
    KernelCapabilities result;
    result.sse2 = (features.leaf1Edx & (1u << 26)) != 0;
    result.avx2 = result.sse2 && (features.leaf1Ecx & kAvxLeaf1) == kAvxLeaf1 &&
                  (features.leaf7Ebx & kAvxLeaf7) == kAvxLeaf7 && (features.xcr0 & 6) == 6;
    // D16 has not selected an AVX-512 instruction subset; no AVX-512 tier is advertised yet.
    return result;
}

KernelCapabilities DetectKernelCapabilities() noexcept
{
#if defined(_M_X64)
    int registers[4]{};
    __cpuidex(registers, 0, 0);
    const int highestLeaf = registers[0];
    X64KernelFeatures features;
    if (highestLeaf >= 1)
    {
        __cpuidex(registers, 1, 0);
        features.leaf1Ecx = static_cast<std::uint32_t>(registers[2]);
        features.leaf1Edx = static_cast<std::uint32_t>(registers[3]);
        // XGETBV itself faults without OSXSAVE. Never run it on the unsupported path.
        if ((features.leaf1Ecx & (1u << 27)) != 0)
        {
            features.xcr0 = _xgetbv(0);
        }
    }
    if (highestLeaf >= 7)
    {
        __cpuidex(registers, 7, 0);
        features.leaf7Ebx = static_cast<std::uint32_t>(registers[1]);
    }
    return DecodeX64KernelFeatures(features);
#elif defined(_M_ARM64)
    KernelCapabilities features;
    features.neon = true; // Windows ARM64 baseline; optional FP16/dot-product paths are not installed.
    return features;
#else
#error Unsupported RedPrism CPU architecture
#endif
}

bool KernelIsaImplemented(KernelIsa isa) noexcept
{
    return isa == KernelIsa::Scalar;
}

KernelSelection ValidateKernelIsa(KernelIsa isa, KernelCapabilities capabilities) noexcept
{
    if (isa == KernelIsa::Auto || isa == KernelIsa::Scalar)
    {
        return KernelSelection::Selected;
    }
    bool supported = false;
    switch (isa)
    {
    case KernelIsa::Sse2:
        supported = capabilities.sse2;
        break;
    case KernelIsa::Avx2:
        supported = capabilities.avx2;
        break;
    case KernelIsa::Avx512:
        return KernelSelection::NotImplemented;
    case KernelIsa::Neon:
        supported = capabilities.neon;
        break;
    default:
        return KernelSelection::NotImplemented;
    }
    if (!supported)
    {
        return KernelSelection::UnsupportedHardware;
    }
    return KernelIsaImplemented(isa) ? KernelSelection::Selected : KernelSelection::NotImplemented;
}

KernelSelection InitializeKernels(KernelIsa requested) noexcept
{
    const KernelSelection result = ValidateKernelIsa(requested, DetectKernelCapabilities());
    if (result == KernelSelection::Selected)
    {
        // P0.8 installs only the retained reference. P2 adds measured exact SIMD tables.
        gActive.store(&kScalar, std::memory_order_release);
    }
    return result;
}

KernelSelection SetIsaForTest(KernelIsa requested) noexcept
{
    return InitializeKernels(requested);
}

const KernelTable& ActiveKernels() noexcept
{
    return *gActive.load(std::memory_order_acquire);
}
