#pragma once

#include "Adjust.h"

#include <array>
#include <cstdint>
#include <vector>

extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_ApplyTables(std::uint8_t* bgra, int width, int height,
                                                                       int stride,
                                                                       const Adjust::ChannelTables& tables) noexcept;
extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_ApplyGradientMap(
    std::uint8_t* bgra, int width, int height, int stride, const std::array<std::uint8_t, 768>& table) noexcept;
extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_ApplyGrain(std::uint8_t* bgra, int width, int height,
                                                                      int stride, const GrainSettings& grain,
                                                                      double originX, double originY,
                                                                      double unitsPerPixel) noexcept;
extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_AddNoise(std::uint8_t* bgra, int width, int height,
                                                                    int stride, float amount, bool gaussian,
                                                                    bool monochromatic, std::uint32_t seed) noexcept;
extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_LensDistort(const std::uint8_t* source,
                                                                       std::uint8_t* destination, int width, int height,
                                                                       int stride, double k) noexcept;
extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_ApplyCube(std::uint8_t* bgra, int width, int height,
                                                                     int stride,
                                                                     const std::vector<float>& cube) noexcept;
