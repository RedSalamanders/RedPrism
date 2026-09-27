#include "PixelBuffer.h"

#include "DeferredRelease.h"
#include "PipelineTrace.h"

#include <array>
#include <atomic>
#include <limits>
#include <malloc.h>
#include <mutex>
#include <new>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#endif

namespace
{
constexpr std::size_t kSizeClass = 16 * 1024 * 1024;

void Poison(void* data, std::size_t size) noexcept
{
#if defined(__SANITIZE_ADDRESS__)
    __asan_poison_memory_region(data, size);
#else
    (void)data;
    (void)size;
#endif
}

void Unpoison(void* data, std::size_t size) noexcept
{
#if defined(__SANITIZE_ADDRESS__)
    __asan_unpoison_memory_region(data, size);
#else
    (void)data;
    (void)size;
#endif
}

std::size_t Capacity(std::size_t size) noexcept
{
    constexpr std::size_t maximum = (std::numeric_limits<std::size_t>::max)() - kSizeClass;
    if (size == 0 || size > maximum)
    {
        return 0;
    }
    return size < kSizeClass ? size : ((size + kSizeClass - 1) / kSizeClass) * kSizeClass;
}

struct FreeAligned
{
    void operator()(std::uint8_t* pixels) const noexcept
    {
        _aligned_free(pixels);
    }
};

template <typename T> struct FreeRecord
{
    void operator()(T* record) const noexcept
    {
        std::destroy_at(record);
        _aligned_free(record);
    }
};
} // namespace

struct PixelAllocation : DeferredReleaseNode
{
    std::unique_ptr<std::uint8_t[], FreeAligned> pixels;
    std::size_t size = 0;
    std::size_t capacity = 0;
    std::uint64_t padding = 0;
};
static_assert(sizeof(PixelAllocation) == 64);
static_assert(sizeof(PixelBuffer) == sizeof(void*));

namespace
{
using OwnedRecord = std::unique_ptr<PixelAllocation, FreeRecord<PixelAllocation>>;

void DestroyBuffer(OwnedRecord record) noexcept
{
    TracePipelineFree(record->capacity);
    Unpoison(record->pixels.get(), record->capacity);
}

class RecyclePool final
{
  public:
    explicit RecyclePool(std::uint64_t cap) noexcept : _cap(cap) {}
    ~RecyclePool()
    {
        Trim();
    }

    OwnedRecord Take(std::size_t capacity) noexcept
    {
        const std::lock_guard guard{_mutex};
        for (auto& slot : _slots)
        {
            if (slot && slot->capacity == capacity)
            {
                _bytes.fetch_sub(slot->bytes, std::memory_order_relaxed);
                return std::move(slot);
            }
        }
        return {};
    }

    bool Keep(OwnedRecord& record) noexcept
    {
        const std::lock_guard guard{_mutex};
        const auto cap = _cap.load(std::memory_order_relaxed);
        const auto bytes = _bytes.load(std::memory_order_relaxed);
        if (record->capacity < kSizeClass || bytes > cap || record->bytes > cap - bytes)
        {
            return false;
        }
        for (auto& slot : _slots)
        {
            if (!slot)
            {
                _bytes.fetch_add(record->bytes, std::memory_order_relaxed);
                slot = std::move(record);
                return true;
            }
        }
        return false;
    }

    void Trim() noexcept
    {
        std::array<OwnedRecord, 32> removed;
        {
            const std::lock_guard guard{_mutex};
            removed.swap(_slots);
            _bytes.store(0, std::memory_order_relaxed);
        }
        for (auto& record : removed)
        {
            if (record)
            {
                DestroyBuffer(std::move(record));
            }
        }
    }

    std::atomic<std::uint64_t> _cap{0};
    std::atomic<std::uint64_t> _bytes{0};

  private:
    std::mutex _mutex;
    std::array<OwnedRecord, 32> _slots;
};

std::atomic<std::shared_ptr<RecyclePool>> g_recycle;

void RecycleOrDestroy(DeferredReleaseNode* node) noexcept
{
    OwnedRecord record{static_cast<PixelAllocation*>(node)};
    if (record->capacity >= kSizeClass)
    {
        if (auto pool = g_recycle.load(std::memory_order_acquire); pool && pool->Keep(record))
        {
            return;
        }
    }
    DestroyBuffer(std::move(record));
}
} // namespace

PixelBuffer::PixelBuffer() noexcept = default;
PixelBuffer::~PixelBuffer() = default;
PixelBuffer::PixelBuffer(PixelBuffer&&) noexcept = default;
PixelBuffer& PixelBuffer::operator=(PixelBuffer&&) noexcept = default;

void PixelBuffer::Retire::operator()(PixelAllocation* allocation) const noexcept
{
    Poison(allocation->pixels.get(), allocation->capacity);
    if (!TryDeferRelease(*allocation))
    {
        RecycleOrDestroy(allocation);
    }
}

PixelBuffer PixelBuffer::TryAllocate(std::size_t size) noexcept
{
    PixelBuffer buffer;
    const std::size_t capacity = Capacity(size);
    if (capacity == 0)
    {
        return buffer;
    }
    OwnedRecord record;
    if (capacity >= kSizeClass)
    {
        if (auto pool = g_recycle.load(std::memory_order_acquire))
        {
            record = pool->Take(capacity);
        }
    }
    if (!record)
    {
        auto* storage =
            static_cast<PixelAllocation*>(_aligned_malloc(sizeof(PixelAllocation), alignof(PixelAllocation)));
        if (!storage)
        {
            return buffer;
        }
        record.reset(std::construct_at(storage));
        record->pixels.reset(static_cast<std::uint8_t*>(_aligned_malloc(capacity, kAlignment)));
        if (!record->pixels)
        {
            return buffer;
        }
        record->capacity = capacity;
        record->bytes = capacity + sizeof(PixelAllocation);
        record->destroy = RecycleOrDestroy;
        Poison(record->pixels.get(), capacity);
    }
    record->size = size;
    Unpoison(record->pixels.get(), size);
    buffer._allocation.reset(record.release());
    return buffer;
}

PixelBuffer::operator bool() const noexcept
{
    return _allocation != nullptr;
}
std::uint8_t* PixelBuffer::Data() noexcept
{
    return _allocation ? _allocation->pixels.get() : nullptr;
}
const std::uint8_t* PixelBuffer::Data() const noexcept
{
    return _allocation ? _allocation->pixels.get() : nullptr;
}
std::size_t PixelBuffer::Size() const noexcept
{
    return _allocation ? _allocation->size : 0;
}
std::uint64_t PixelBuffer::AllocationBytes() const noexcept
{
    return _allocation ? _allocation->bytes : 0;
}
std::uint64_t PixelBuffer::RequiredBytes(std::size_t size) noexcept
{
    const auto capacity = Capacity(size);
    return capacity == 0 ? 0 : capacity + sizeof(PixelAllocation);
}

bool InitializePixelBufferRecycle(std::uint64_t capBytes) noexcept
{
    try
    {
        auto pool = std::make_shared<RecyclePool>(capBytes);
        std::shared_ptr<RecyclePool> expected;
        return g_recycle.compare_exchange_strong(expected, std::move(pool), std::memory_order_acq_rel);
    }
    catch (const std::bad_alloc&)
    {
        // Host initialization boundary: the caller fails initialization if pool construction fails.
        return false;
    }
}

void SetPixelBufferRecycleCap(std::uint64_t capBytes) noexcept
{
    if (auto pool = g_recycle.load(std::memory_order_acquire))
    {
        pool->_cap.store(capBytes, std::memory_order_release);
    }
}

std::uint64_t PixelBufferRecycleBytes() noexcept
{
    if (auto pool = g_recycle.load(std::memory_order_acquire))
    {
        return pool->_bytes.load(std::memory_order_acquire);
    }
    return 0;
}

void TrimPixelBufferRecycle() noexcept
{
    if (auto pool = g_recycle.load(std::memory_order_acquire))
    {
        pool->Trim();
    }
}

void ClosePixelBufferRecycle() noexcept
{
    (void)g_recycle.exchange({}, std::memory_order_acq_rel);
}
