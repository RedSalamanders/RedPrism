#pragma once

// Developer-only scalar CPU baseline. Prints one parseable KERNEL BENCH row per operation,
// including complete Raster-producing wrappers; outputs must match the warmup checksum.
// The no-argument entry point uses 3840x2160 and ten samples after one warmup.
// The explicit entry point accepts a positive sample count; 8192x5464 Gaussian blur
// is capped at three samples after one warmup because of its cost. No timing is a test gate.
[[nodiscard]] int RunKernelBenchmark() noexcept;
[[nodiscard]] int RunKernelBenchmark(int width, int height, int iterations) noexcept;
// Alternating copy/adopt pairs in one process, with identical output checksums.
[[nodiscard]] int RunBufferBenchmark(int width, int height, int iterations) noexcept;
