#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

struct PixelAllocation;

// Unique uninitialized CRT/ASan-tracked pixels. A separate small ownership record permits a last-reference
// transfer to a worker without allocation or a copy. Every byte must be initialized before publication.
class PixelBuffer final
{
  public:
    static constexpr std::size_t kAlignment = 64;

    PixelBuffer() noexcept;
    ~PixelBuffer();
    PixelBuffer(const PixelBuffer&) = delete;
    PixelBuffer& operator=(const PixelBuffer&) = delete;
    PixelBuffer(PixelBuffer&&) noexcept;
    PixelBuffer& operator=(PixelBuffer&&) noexcept;
    [[nodiscard]] static PixelBuffer TryAllocate(std::size_t size) noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::uint8_t* Data() noexcept;
    [[nodiscard]] const std::uint8_t* Data() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;
    [[nodiscard]] std::uint64_t AllocationBytes() const noexcept;
    // Owned bytes for a job peak: rounded pixels plus the ownership record. The global admission headroom
    // covers CRT allocator bookkeeping and alignment slop, which the CRT does not expose as an exact size.
    [[nodiscard]] static std::uint64_t RequiredBytes(std::size_t size) noexcept;
    [[nodiscard]] std::uint8_t& operator[](std::size_t index) noexcept
    {
        return Data()[index];
    }
    [[nodiscard]] const std::uint8_t& operator[](std::size_t index) const noexcept
    {
        return Data()[index];
    }

  private:
    struct Retire
    {
        void operator()(PixelAllocation* allocation) const noexcept;
    };
    std::unique_ptr<PixelAllocation, Retire> _allocation;
};

// One process pool explicitly scoped by the host. All operations are thread-safe. Trim and close can destroy
// large allocations and belong on a worker or after the window is hidden. No cache exists before initialization.
[[nodiscard]] bool InitializePixelBufferRecycle(std::uint64_t capBytes) noexcept;
void SetPixelBufferRecycleCap(std::uint64_t capBytes) noexcept;
[[nodiscard]] std::uint64_t PixelBufferRecycleBytes() noexcept;
void TrimPixelBufferRecycle() noexcept;
void ClosePixelBufferRecycle() noexcept;
