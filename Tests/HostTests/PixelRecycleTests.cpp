#include "PixelRecycleTests.h"

#include "../../Common/Document.h"
#include "../../Common/PixelBuffer.h"

#include <array>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#endif

namespace
{
constexpr std::size_t kMiB = 1024u * 1024u;
constexpr std::size_t kClassBytes = 16u * kMiB;
constexpr std::size_t kLargeBytes = kClassBytes + 1u;
constexpr std::size_t kLargerClassBytes = 2u * kClassBytes + 1u;
constexpr std::uint64_t kRecordBytes = 64;

int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s pixel-recycle.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++g_failures;
    }
}

std::uint8_t SampleValue(std::size_t index, std::uint8_t salt) noexcept
{
    return static_cast<std::uint8_t>((index * 37u + salt * 53u + 19u) & 0xffu);
}

constexpr std::array<std::size_t, 7> SampleOffsets(std::size_t size) noexcept
{
    return {0, 1, size / 7u, size / 3u, size / 2u, (size * 5u) / 7u, size - 1u};
}

std::uint64_t FillAndChecksum(PixelBuffer& buffer, std::uint8_t salt) noexcept
{
    const auto offsets = SampleOffsets(buffer.Size());
    std::uint64_t checksum = 1469598103934665603ULL;
    for (std::size_t i = 0; i < offsets.size(); ++i)
    {
        buffer[offsets[i]] = SampleValue(i, salt);
        checksum = (checksum ^ SampleValue(i, salt)) * 1099511628211ULL;
    }
    return checksum;
}

std::uint64_t ReadChecksum(const PixelBuffer& buffer) noexcept
{
    const auto offsets = SampleOffsets(buffer.Size());
    std::uint64_t checksum = 1469598103934665603ULL;
    for (std::size_t i = 0; i < offsets.size(); ++i)
    {
        checksum = (checksum ^ buffer[offsets[i]]) * 1099511628211ULL;
    }
    return checksum;
}

std::uint64_t ExpectedChecksum(std::uint8_t salt) noexcept
{
    std::uint64_t checksum = 1469598103934665603ULL;
    for (std::size_t i = 0; i < SampleOffsets(kLargeBytes).size(); ++i)
    {
        checksum = (checksum ^ SampleValue(i, salt)) * 1099511628211ULL;
    }
    return checksum;
}

void TestAccountingAndInvalidSizes() noexcept
{
    Expect(PixelBuffer::RequiredBytes(0) == 0, "required-zero");
    Expect(PixelBuffer::RequiredBytes(1) == 1u + kRecordBytes &&
               PixelBuffer::RequiredBytes(4097) == 4097u + kRecordBytes,
           "required-small-sizes-exact-includes-record");
    Expect(PixelBuffer::RequiredBytes(kClassBytes) == kClassBytes + kRecordBytes &&
               PixelBuffer::RequiredBytes(kLargeBytes) == 2u * kClassBytes + kRecordBytes &&
               PixelBuffer::RequiredBytes(kLargerClassBytes) == 3u * kClassBytes + kRecordBytes,
           "required-large-sizes-rounds-to-16mib-includes-record");
    Expect(PixelBuffer::RequiredBytes((std::numeric_limits<std::size_t>::max)()) == 0, "required-overflow-is-zero");

    const PixelBuffer zero = PixelBuffer::TryAllocate(0);
    const PixelBuffer overflow = PixelBuffer::TryAllocate((std::numeric_limits<std::size_t>::max)());
    Expect(!zero && zero.Size() == 0 && !overflow && overflow.Size() == 0, "invalid-size-allocations-empty");

    PixelBuffer small = PixelBuffer::TryAllocate(4097);
    Expect(small && small.Size() == 4097 && small.AllocationBytes() == 4097u + kRecordBytes,
           "small-allocation-exact-size-and-accounting");
    small = PixelBuffer{};
    Expect(PixelBufferRecycleBytes() == 0, "small-allocation-never-cached");
}

void TestClassReuseAndMoveAdopt() noexcept
{
    SetPixelBufferRecycleCap(PixelBuffer::RequiredBytes(kLargeBytes) + PixelBuffer::RequiredBytes(kLargerClassBytes));
    PixelBuffer first = PixelBuffer::TryAllocate(kLargeBytes);
    Expect(first && first.Size() == kLargeBytes && first.AllocationBytes() == PixelBuffer::RequiredBytes(kLargeBytes),
           "large-allocation-size-and-accounting");
    if (!first)
    {
        return;
    }

    const auto firstChecksum = FillAndChecksum(first, 3);
    const auto* const firstAddress = first.Data();
    PixelBuffer moved{std::move(first)};
    Expect(!first && moved.Data() == firstAddress && moved.Size() == kLargeBytes &&
               ReadChecksum(moved) == firstChecksum && firstChecksum == ExpectedChecksum(3),
           "move-preserves-pointer-size-and-sampled-pixels");

    PixelBuffer otherClass = PixelBuffer::TryAllocate(kLargerClassBytes);
    Expect(otherClass && otherClass.Size() == kLargerClassBytes, "other-class-allocation-valid");
    if (otherClass)
    {
        otherClass = PixelBuffer{};
    }
    moved = PixelBuffer{};
    Expect(PixelBufferRecycleBytes() ==
               PixelBuffer::RequiredBytes(kLargerClassBytes) + PixelBuffer::RequiredBytes(kLargeBytes),
           "different-size-classes-account-independently");

    PixelBuffer sameClass = PixelBuffer::TryAllocate(kLargeBytes);
    Expect(sameClass && sameClass.Data() == firstAddress && sameClass.Size() == kLargeBytes,
           "same-size-class-reuses-exact-pointer");
    sameClass = PixelBuffer{};

    PixelBuffer classChange = PixelBuffer::TryAllocate(kLargerClassBytes);
    Expect(classChange && classChange.Size() == kLargerClassBytes && classChange.Data() != firstAddress,
           "different-size-class-does-not-reuse-pointer");
    classChange = PixelBuffer{};

    PixelBuffer adoptBuffer = PixelBuffer::TryAllocate(64);
    Expect(adoptBuffer && adoptBuffer.Size() == 64, "adopt-source-allocation-valid");
    if (adoptBuffer)
    {
        for (std::size_t i = 0; i < adoptBuffer.Size(); ++i)
        {
            adoptBuffer[i] = static_cast<std::uint8_t>(i ^ 0xA5u);
        }
        const auto* const adoptedAddress = adoptBuffer.Data();
        PixelBuffer movedForAdopt{std::move(adoptBuffer)};
        Raster adopted = Raster::Adopt(8, 8, RasterFormat::Gray8, std::move(movedForAdopt));
        const bool pixelsMatch = adopted.HasStorage() && adopted.Width() == 8 && adopted.Height() == 8 &&
                                 adopted.ByteSize() == 64 && adopted.Data() == adoptedAddress &&
                                 adopted.Data()[17] == static_cast<std::uint8_t>(17u ^ 0xA5u);
        Expect(!adoptBuffer && !movedForAdopt && pixelsMatch,
               "adopt-transfers-requested-size-and-pointer-with-sampled-content");
    }
    TrimPixelBufferRecycle();
    Expect(PixelBufferRecycleBytes() == 0, "class-reuse-cleanup-trim");
}

void TestAsanRetirementAnnotations() noexcept
{
#if defined(__SANITIZE_ADDRESS__)
    SetPixelBufferRecycleCap(PixelBuffer::RequiredBytes(kLargeBytes));
    PixelBuffer retired = PixelBuffer::TryAllocate(kLargeBytes);
    if (!retired)
    {
        Expect(false, "asan-annotation-allocation-valid");
        return;
    }
    auto* const address = retired.Data();
    retired = PixelBuffer{};
    const bool cachedPoison = __asan_address_is_poisoned(address) != 0 &&
                              __asan_address_is_poisoned(address + kLargeBytes - 1u) != 0 &&
                              __asan_address_is_poisoned(address + kLargeBytes) != 0 &&
                              __asan_address_is_poisoned(address + 2u * kClassBytes - 1u) != 0;
    Expect(cachedPoison && PixelBufferRecycleBytes() == PixelBuffer::RequiredBytes(kLargeBytes),
           "asan-retired-payload-and-class-slack-poisoned-while-cached");

    constexpr std::size_t reusedSize = kLargeBytes + 256;
    PixelBuffer reused = PixelBuffer::TryAllocate(reusedSize);
    Expect(reused && reused.Data() == address && reused.Size() == reusedSize &&
               __asan_address_is_poisoned(reused.Data()) == 0 &&
               __asan_address_is_poisoned(reused.Data() + reusedSize - 1) == 0 &&
               __asan_address_is_poisoned(reused.Data() + reusedSize) != 0,
           "asan-reuse-unpoisons-requested-bytes-only");
    reused = PixelBuffer{};
    TrimPixelBufferRecycle();
#else
    Expect(true, "asan-annotation-checks-only-under-asan");
#endif
}

void TestTrimCapZeroAndCloseWithLiveBuffer() noexcept
{
    SetPixelBufferRecycleCap(PixelBuffer::RequiredBytes(kLargeBytes));
    PixelBuffer cached = PixelBuffer::TryAllocate(kLargeBytes);
    Expect(static_cast<bool>(cached), "trim-source-allocation-valid");
    cached = PixelBuffer{};
    Expect(PixelBufferRecycleBytes() == PixelBuffer::RequiredBytes(kLargeBytes), "trim-source-cached");
    TrimPixelBufferRecycle();
    Expect(PixelBufferRecycleBytes() == 0, "trim-releases-cached-bytes");

    SetPixelBufferRecycleCap(PixelBuffer::RequiredBytes(kLargeBytes) - 1u);
    PixelBuffer underCap = PixelBuffer::TryAllocate(kLargeBytes);
    Expect(static_cast<bool>(underCap), "under-cap-source-allocation-valid");
    underCap = PixelBuffer{};
    Expect(PixelBufferRecycleBytes() == 0, "cap-admission-includes-64-byte-record");

    SetPixelBufferRecycleCap(0);
    PixelBuffer uncached = PixelBuffer::TryAllocate(kLargeBytes);
    Expect(static_cast<bool>(uncached), "zero-cap-source-allocation-valid");
    uncached = PixelBuffer{};
    Expect(PixelBufferRecycleBytes() == 0, "zero-cap-prevents-return-to-pool");

    SetPixelBufferRecycleCap(PixelBuffer::RequiredBytes(kLargeBytes));
    PixelBuffer live = PixelBuffer::TryAllocate(kLargeBytes);
    Expect(static_cast<bool>(live), "close-live-source-allocation-valid");
    if (!live)
    {
        return;
    }
    const auto expected = FillAndChecksum(live, 9);
    ClosePixelBufferRecycle();
    Expect(live && live.Size() == kLargeBytes && ReadChecksum(live) == expected && expected == ExpectedChecksum(9) &&
               PixelBufferRecycleBytes() == 0,
           "close-with-live-buffer-preserves-owner-and-data");
    live = PixelBuffer{};
    Expect(PixelBufferRecycleBytes() == 0, "free-after-close-does-not-cache");

    Expect(InitializePixelBufferRecycle(0), "pool-reinitializes-after-close");
}

void TestSlotLimit() noexcept
{
    constexpr std::size_t kSlotCount = 33;
    const std::uint64_t oneAllocation = PixelBuffer::RequiredBytes(kClassBytes);
    SetPixelBufferRecycleCap(oneAllocation * kSlotCount);
    std::array<PixelBuffer, kSlotCount> buffers;
    bool allAllocated = true;
    for (PixelBuffer& buffer : buffers)
    {
        buffer = PixelBuffer::TryAllocate(kClassBytes);
        allAllocated = allAllocated && static_cast<bool>(buffer);
    }
    Expect(allAllocated, "slot-limit-fixtures-allocated");
    if (!allAllocated)
    {
        for (PixelBuffer& buffer : buffers)
        {
            buffer = PixelBuffer{};
        }
        TrimPixelBufferRecycle();
        return;
    }
    for (PixelBuffer& buffer : buffers)
    {
        buffer = PixelBuffer{};
    }
    Expect(PixelBufferRecycleBytes() == oneAllocation * 32u, "pool-retains-no-more-than-32-slots");
    TrimPixelBufferRecycle();
    Expect(PixelBufferRecycleBytes() == 0, "slot-limit-cleanup-trim");
}

void TestConcurrentBorrowers() noexcept
{
    SetPixelBufferRecycleCap(PixelBuffer::RequiredBytes(kLargeBytes) * 2u);
    std::barrier<> rendezvous{2};
    struct BorrowerResult final
    {
        const std::uint8_t* address = nullptr;
        std::uint64_t checksum = 0;
        bool allocated = false;
    };
    std::array<BorrowerResult, 2> results{};

    auto borrow = [&](std::size_t index) noexcept
    {
        PixelBuffer buffer = PixelBuffer::TryAllocate(kLargeBytes);
        results[index].allocated = static_cast<bool>(buffer);
        results[index].address = buffer.Data();
        rendezvous.arrive_and_wait();
        if (buffer)
        {
            const bool unique = results[0].address != nullptr && results[1].address != nullptr &&
                                results[0].address != results[1].address;
            if (unique)
            {
                const auto offsets = SampleOffsets(buffer.Size());
                for (std::size_t sample = 0; sample < offsets.size(); ++sample)
                {
                    buffer[offsets[sample]] = SampleValue(sample, static_cast<std::uint8_t>(index + 1u));
                }
                rendezvous.arrive_and_wait();
                std::uint64_t checksum = 1469598103934665603ULL;
                for (const std::size_t offset : offsets)
                {
                    checksum = (checksum ^ buffer[offset]) * 1099511628211ULL;
                }
                results[index].checksum = checksum;
            }
            else
            {
                rendezvous.arrive_and_wait();
            }
        }
        else
        {
            rendezvous.arrive_and_wait();
        }
    };

    std::thread first{borrow, 0};
    std::thread second{borrow, 1};
    first.join();
    second.join();
    const bool unique = results[0].allocated && results[1].allocated && results[0].address != results[1].address;
    Expect(unique, "concurrent-live-borrowers-never-alias");
    Expect(unique && results[0].checksum == ExpectedChecksum(1) && results[1].checksum == ExpectedChecksum(2),
           "concurrent-borrowers-retain-independent-sampled-bytes");
    TrimPixelBufferRecycle();
}
} // namespace

int RunPixelRecycleTests()
{
    g_failures = 0;
    Expect(InitializePixelBufferRecycle(0), "initialize-zero-cap-pool");
    if (g_failures != 0)
    {
        return g_failures;
    }

    TestAccountingAndInvalidSizes();
    TestClassReuseAndMoveAdopt();
    TestAsanRetirementAnnotations();
    TestTrimCapZeroAndCloseWithLiveBuffer();
    TestSlotLimit();
    TestConcurrentBorrowers();
    ClosePixelBufferRecycle();
    Expect(PixelBufferRecycleBytes() == 0, "final-close-has-no-cached-bytes");
    return g_failures;
}
