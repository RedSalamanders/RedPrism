#pragma once

// Worker-local scratch with bounded allocation and cancellation; no arena object may escape.
#include "Executor.h"
#include "PixelBuffer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define REDPRISM_WAND_ARENA_ASAN 1
#endif

namespace RedPrismBoundedClipper
{
class Workspace final
{
  public:
    static constexpr std::size_t Alignment = 64;
    static constexpr std::size_t GuardBytes = 64;
    static constexpr std::uint64_t Quantum = 64;
    enum class AllocationPolicy : std::uint8_t
    {
        Monotonic,
        ReuseFreed
    };
    using Destructor = void (*)(void*) noexcept;
    struct Statistics
    {
        std::size_t capacity = 0;
        std::size_t consumed = 0;
        std::size_t allocations = 0;
        std::size_t liveObjects = 0;
        std::uint64_t workUnits = 0;
    };

    explicit Workspace(std::size_t capacity, CancelToken cancellation,
                       void (*checkpoint)(void*, std::uint64_t) noexcept = nullptr, void* context = nullptr,
                       std::size_t allocationLimit = (std::numeric_limits<std::size_t>::max)(),
                       AllocationPolicy policy = AllocationPolicy::Monotonic)
        : _storage(AllocateStorage(capacity, cancellation)), _capacity(capacity), _cancellation(cancellation),
          _checkpoint(checkpoint), _context(context), _entryExceptions(std::uncaught_exceptions()),
          _allocationLimit(allocationLimit), _reuseFreed(policy == AllocationPolicy::ReuseFreed)
    {
        // This engine is serial within one worker operation. No arena-owned object may escape the scope.
        assert(!_current);
        _current = this;
    }
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    ~Workspace() noexcept
    {
        _suppress = true;
        // Failure can occur before an upstream raw node enters its graph. Walk allocation ownership,
        // never a possibly incomplete linked graph. Construction failure calls the matching delete first.
        for (Header* header = _head; header; header = header->next)
            if (header->live && header->destroy)
            {
                header->live = false;
                header->destroy(header->object);
                Poison(header->object, header->size);
            }
        _current = nullptr;
        Unpoison(_storage.Data(), _capacity);
    }

    [[nodiscard]] Statistics GetStatistics() const noexcept
    {
        std::size_t live = 0;
        for (Header* header = _head; header; header = header->next)
            live += header->live && header->destroy ? 1 : 0;
        return {_capacity, _consumed, _allocations, live, _workUnits};
    }

    [[nodiscard]] static void* Allocate(std::size_t bytes, std::size_t alignment, Destructor destroy = nullptr)
    {
        assert(_current);
        Workspace& workspace = *_current;
        Pulse();
        if (workspace._allocations == workspace._allocationLimit)
            Refuse();
        bytes = std::max<std::size_t>(bytes, 1);
        if (alignment > Alignment || !alignment || (alignment & (alignment - 1)) != 0)
            Refuse();
        // Offset normalization creates and retires many independent temporary paths. Its opt-in
        // size classes reuse only fully destroyed allocations within this same admitted buffer.
        // Header ownership remains in _head; reusing a slot never links it twice or allocates a free list.
        std::size_t capacity = bytes;
        std::size_t sizeClass = 0;
        if (workspace._reuseFreed)
        {
            const auto width = std::bit_width(std::max(bytes, sizeof(void*)) - 1);
            if (width >= std::numeric_limits<std::size_t>::digits)
                Refuse();
            sizeClass = static_cast<std::size_t>(width);
            capacity = std::size_t{1} << sizeClass;
            if (Header* reused = workspace._free[sizeClass])
            {
                workspace._free[sizeClass] = reused->freeNext;
                reused->destroy = destroy;
                reused->live = true;
                ++workspace._allocations;
                Unpoison(reused->object, capacity);
                Poison(static_cast<std::byte*>(reused->object) + bytes, capacity - bytes + GuardBytes);
                return reused->object;
            }
        }
        constexpr auto maximum = (std::numeric_limits<std::size_t>::max)();
        const auto prefix = sizeof(Header) + sizeof(Header*);
        if (workspace._consumed > maximum - prefix - (Alignment - 1))
            Refuse();
        const auto objectOffset = (workspace._consumed + prefix + Alignment - 1) & ~(Alignment - 1);
        if (objectOffset > workspace._capacity || GuardBytes > workspace._capacity - objectOffset ||
            capacity > workspace._capacity - objectOffset - GuardBytes)
            Refuse();
        auto* header = std::construct_at(
            reinterpret_cast<Header*>(reinterpret_cast<std::byte*>(workspace._storage.Data()) + workspace._consumed));
        void* object = reinterpret_cast<std::byte*>(workspace._storage.Data()) + objectOffset;
        // All returned buffers are 64-byte aligned; the back pointer is separate from their payload.
        *(static_cast<Header**>(object) - 1) = header;
        header->next = workspace._head;
        header->object = object;
        header->size = capacity;
        header->destroy = destroy;
        header->live = true;
        workspace._head = header;
        ++workspace._allocations;
        Poison(static_cast<std::byte*>(object) + bytes, capacity - bytes + GuardBytes);
        workspace._consumed = objectOffset + capacity + GuardBytes;
        // The next header needs its own alignment after an odd-sized allocation.
        if (workspace._consumed <= workspace._capacity - std::min(workspace._capacity, alignof(Header) - 1))
            workspace._consumed = (workspace._consumed + alignof(Header) - 1) & ~(alignof(Header) - 1);
        return object;
    }

    static void Deallocate(void* object) noexcept
    {
        if (object)
        {
            auto* header = HeaderFor(object);
            assert(header->live);
            header->live = false;
            Poison(object, header->size);
            if (_current->_reuseFreed)
            {
                assert(std::has_single_bit(header->size));
                const auto sizeClass = static_cast<std::size_t>(std::bit_width(header->size) - 1);
                header->freeNext = _current->_free[sizeClass];
                _current->_free[sizeClass] = header;
            }
        }
        // Monotonic callers retain their existing non-reusing policy and exact arena layout.
        // Reusing callers still keep every slot and its guards inside the admitted ceiling.
    }
    [[nodiscard]] static bool IsLive(const void* object) noexcept
    {
        return object && HeaderFor(object)->live;
    }
    [[nodiscard]] static bool IsAborted() noexcept
    {
        assert(_current);
        return _current->_aborted || std::uncaught_exceptions() > _current->_entryExceptions;
    }
    static void Pulse()
    {
        assert(_current);
        Workspace& workspace = *_current;
        if (workspace._suppress)
            return;
        ++workspace._workUnits;
        if ((workspace._workUnits & (Quantum - 1)) == 0)
        {
            if (workspace._checkpoint)
                workspace._checkpoint(workspace._context, workspace._workUnits);
            if (workspace._cancellation.IsCancelled())
                Refuse();
        }
    }
    [[noreturn]] static void Refuse()
    {
        assert(_current);
        _current->_aborted = true;
        // The original job runner owns the only bad_alloc translation. A stopped token remains Cancelled.
        throw std::bad_alloc{};
    }
    class Suppress final
    {
      public:
        Suppress() noexcept : _previous(_current->_suppress)
        {
            _current->_suppress = true;
        }
        Suppress(const Suppress&) = delete;
        Suppress& operator=(const Suppress&) = delete;
        ~Suppress() noexcept
        {
            _current->_suppress = _previous;
        }

      private:
        bool _previous;
    };

  private:
    static void Unpoison(void* bytes, std::size_t count) noexcept
    {
#if defined(REDPRISM_WAND_ARENA_ASAN)
        __asan_unpoison_memory_region(bytes, count);
#else
        (void)bytes;
        (void)count;
#endif
    }
    struct Header
    {
        Header* next = nullptr;
        void* object = nullptr;
        std::size_t size = 0;
        // Only live objects have a destructor; only retired objects have a free-list link.
        union
        {
            Destructor destroy = nullptr;
            Header* freeNext;
        };
        bool live = false;
    };
    static void Poison(void* bytes, std::size_t count) noexcept
    {
#if defined(REDPRISM_WAND_ARENA_ASAN)
        __asan_poison_memory_region(bytes, count);
#else
        (void)bytes;
        (void)count;
#endif
    }
    static PixelBuffer AllocateStorage(std::size_t capacity, const CancelToken& cancellation)
    {
        if (_current || !capacity || cancellation.IsCancelled())
            throw std::bad_alloc{};
        auto storage = PixelBuffer::TryAllocate(capacity);
        if (!storage)
            throw std::bad_alloc{};
        return storage;
    }
    static Header* HeaderFor(const void* object) noexcept
    {
        assert(_current && object);
        assert(static_cast<const std::byte*>(object) >= reinterpret_cast<std::byte*>(_current->_storage.Data()) &&
               static_cast<const std::byte*>(object) <
                   reinterpret_cast<std::byte*>(_current->_storage.Data()) + _current->_capacity);
        return *(static_cast<Header* const*>(object) - 1);
    }
    inline static thread_local Workspace* _current = nullptr;
    PixelBuffer _storage;
    std::size_t _capacity = 0;
    std::size_t _consumed = 0;
    std::size_t _allocations = 0;
    Header* _head = nullptr;
    CancelToken _cancellation;
    void (*_checkpoint)(void*, std::uint64_t) noexcept = nullptr;
    void* _context = nullptr;
    int _entryExceptions = 0;
    bool _aborted = false;
    bool _suppress = false;
    std::uint64_t _workUnits = 0;
    std::size_t _allocationLimit;
    bool _reuseFreed = false;
    std::array<Header*, std::numeric_limits<std::size_t>::digits> _free{};
};

template <class T> class ArenaObject
{
  public:
    static void* operator new(std::size_t bytes)
    {
        return Workspace::Allocate(bytes, alignof(T), [](void* object) noexcept { static_cast<T*>(object)->~T(); });
    }
    static void operator delete(void* object) noexcept
    {
        Workspace::Deallocate(object);
    }
    static void* operator new[](std::size_t bytes)
    {
        static_assert(std::is_trivially_destructible_v<T>);
        return Workspace::Allocate(bytes, alignof(T));
    }
    static void operator delete[](void* object) noexcept
    {
        Workspace::Deallocate(object);
    }
};

template <class T> class ArenaAllocator
{
  public:
    using value_type = T;
    using is_always_equal = std::true_type;
    ArenaAllocator() noexcept = default;
    template <class U> ArenaAllocator(const ArenaAllocator<U>&) noexcept {}
    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (count > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
            Workspace::Refuse();
        return static_cast<T*>(Workspace::Allocate(count * sizeof(T), alignof(T)));
    }
    void deallocate(T* object, std::size_t) noexcept
    {
        Workspace::Deallocate(object);
    }
    template <class U> bool operator==(const ArenaAllocator<U>&) const noexcept
    {
        return true;
    }
};

template <class T> class ArenaVector : public std::vector<T, ArenaAllocator<T>>, public ArenaObject<ArenaVector<T>>
{
    using Base = std::vector<T, ArenaAllocator<T>>;

  public:
    // MSVC's default and move vector constructors allocate a Debug iterator proxy inside noexcept.
    // Use the throwing size constructor, then swap on move. Arena exhaustion must reach the job runner.
    ArenaVector() : Base(std::size_t{0}) {}
    explicit ArenaVector(std::size_t count) : Base(count) {}
    ArenaVector(std::size_t count, const T& value) : Base(count, value) {}
    template <class Iterator>
        requires(!std::is_integral_v<Iterator>)
    ArenaVector(Iterator first, Iterator last) : Base(first, last)
    {
    }
    ArenaVector(std::initializer_list<T> values) : Base(values) {}
    ArenaVector(const ArenaVector& other) : Base(static_cast<const Base&>(other)) {}
    ArenaVector(ArenaVector&& other)
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
        noexcept
#endif
        : Base(std::size_t{0})
    {
        Base::swap(other);
    }
    ArenaVector& operator=(const ArenaVector& other)
    {
        Base::operator=(static_cast<const Base&>(other));
        return *this;
    }
    ArenaVector& operator=(ArenaVector&& other) noexcept
    {
        Base::operator=(std::move(static_cast<Base&>(other)));
        return *this;
    }
    using Base::operator=;
};

template <class T> class ArenaQueue : public std::priority_queue<T, ArenaVector<T>>
{
  public:
    void Clear() noexcept
    {
        this->c.clear();
    }
};

template <class Iterator> class CheckedIterator final
{
  public:
    explicit CheckedIterator(Iterator iterator) : _iterator(iterator) {}
    decltype(auto) operator*() const
    {
        return *_iterator;
    }
    CheckedIterator& operator++()
    {
        Workspace::Pulse();
        ++_iterator;
        return *this;
    }
    bool operator!=(const CheckedIterator& other) const
    {
        Workspace::Pulse();
        return _iterator != other._iterator;
    }

  private:
    Iterator _iterator;
};
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
template <class Range> class CheckedRangeView final
{
  public:
    explicit CheckedRangeView(Range& range) noexcept : _range(range) {}
    auto begin()
    {
        return CheckedIterator(_range.begin());
    }
    auto end()
    {
        return CheckedIterator(_range.end());
    }

  private:
    Range& _range;
};
#pragma warning(pop)

template <class Range> auto CheckedRange(Range& range) noexcept
{
    return CheckedRangeView<Range>(range);
}

template <class Iterator, class Compare> void StableSort(Iterator first, Iterator last, Compare compare)
{
    using T = typename std::iterator_traits<Iterator>::value_type;
    const auto count = static_cast<std::size_t>(last - first);
    if (count < 2)
        return;
    ArenaVector<T> temporary;
    temporary.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        Workspace::Pulse();
        temporary.emplace_back();
    }
    for (std::size_t width = 1; width < count;)
    {
        for (std::size_t offset = 0; offset < count; ++offset)
        {
            Workspace::Pulse();
            temporary[offset] = std::move(first[static_cast<std::ptrdiff_t>(offset)]);
        }
        for (std::size_t start = 0; start < count;)
        {
            const auto middle = start + std::min(width, count - start);
            const auto end = middle + std::min(width, count - middle);
            auto left = start, right = middle;
            for (auto destination = start; destination < end; ++destination)
            {
                Workspace::Pulse();
                // Equal keys retain their original left-before-right order.
                const auto source =
                    right == end || (left < middle && !compare(temporary[right], temporary[left])) ? left++ : right++;
                first[static_cast<std::ptrdiff_t>(destination)] = std::move(temporary[source]);
            }
            start = end;
        }
        if (width > count / 2)
            break;
        width *= 2;
    }
}
} // namespace RedPrismBoundedClipper
