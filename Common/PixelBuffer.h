#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <malloc.h>
#include <memory>
#include <utility>

// Unique, uninitialized pixel memory. The CRT aligned allocator is tracked by MSVC AddressSanitizer.
// Every byte must be written before a buffer is published as an immutable Raster.
class PixelBuffer final
{
  public:
    static constexpr std::size_t kAlignment = 64;

    PixelBuffer() noexcept = default;
    PixelBuffer(const PixelBuffer&) = delete;
    PixelBuffer& operator=(const PixelBuffer&) = delete;
    PixelBuffer(PixelBuffer&& other) noexcept : _data(std::move(other._data)), _size(std::exchange(other._size, 0)) {}
    PixelBuffer& operator=(PixelBuffer&& other) noexcept
    {
        if (this != &other)
        {
            _data = std::move(other._data);
            _size = std::exchange(other._size, 0);
        }
        return *this;
    }

    [[nodiscard]] static PixelBuffer TryAllocate(std::size_t size) noexcept
    {
        // Avoid zero-sized allocations and overflow in the allocator's alignment bookkeeping.
        if (size == 0 || size > std::numeric_limits<std::size_t>::max() - kAlignment - sizeof(void*))
        {
            return {};
        }
        PixelBuffer buffer;
        buffer._data.reset(static_cast<std::uint8_t*>(_aligned_malloc(size, kAlignment)));
        if (buffer._data)
        {
            buffer._size = size;
        }
        return buffer;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return _data != nullptr;
    }
    [[nodiscard]] std::uint8_t* Data() noexcept
    {
        return _data.get();
    }
    [[nodiscard]] const std::uint8_t* Data() const noexcept
    {
        return _data.get();
    }
    [[nodiscard]] std::size_t Size() const noexcept
    {
        return _size;
    }
    [[nodiscard]] std::uint8_t& operator[](std::size_t index) noexcept
    {
        return _data[index];
    }
    [[nodiscard]] const std::uint8_t& operator[](std::size_t index) const noexcept
    {
        return _data[index];
    }

  private:
    struct Free
    {
        void operator()(std::uint8_t* pixels) const noexcept
        {
            _aligned_free(pixels);
        }
    };
    std::unique_ptr<std::uint8_t[], Free> _data;
    std::size_t _size = 0;
};
