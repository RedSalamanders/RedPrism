#include <cstdint>
#include <immintrin.h>

// Linked but never executed: these controls must also run on machines without AVX2/AVX-512.
#if defined(REDPRISM_ISA_PROBE_SCALAR)
extern "C" __declspec(noinline) void Kernels_Scalar_Probe_Copy(const std::uint8_t* source,
                                                               std::uint8_t* destination) noexcept
{
    for (int index = 0; index < 32; ++index)
    {
        destination[index] = source[index];
    }
}
#elif defined(REDPRISM_ISA_PROBE_EVEX)
extern "C" __declspec(noinline) void Kernels_Avx512_Probe_Copy(const std::uint8_t* source,
                                                               std::uint8_t* destination) noexcept
{
    _mm512_storeu_si512(destination, _mm512_loadu_si512(source));
}
#else
extern "C" __declspec(noinline) void Kernels_Avx2_Probe_Copy(const std::uint8_t* source,
                                                             std::uint8_t* destination) noexcept
{
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(destination),
                        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source)));
}
#endif

int main(int argc, char**)
{
    std::uint8_t source[64]{};
    std::uint8_t destination[64]{};
    source[0] = static_cast<std::uint8_t>(argc);
#if defined(REDPRISM_ISA_PROBE_SCALAR)
    Kernels_Scalar_Probe_Copy(source, destination);
#elif defined(REDPRISM_ISA_PROBE_EVEX)
    Kernels_Avx512_Probe_Copy(source, destination);
#else
    Kernels_Avx2_Probe_Copy(source, destination);
#endif
    return destination[0];
}
