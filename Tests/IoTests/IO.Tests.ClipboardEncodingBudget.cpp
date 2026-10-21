#include "IO.Tests.ClipboardEncodingBudget.h"

#include "../Common/ClipboardEncodingBudget.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stop_token>
#include <vector>

namespace
{
// Standard-only declared fixed object bounds; private Windows sizeof checks belong to the adapter.
constexpr std::uint64_t kPreparedImageStorageObjectBytes = 256;
constexpr std::uint64_t kCodecMemoryStreamObjectBytes = 256;
constexpr std::uint64_t kTemporaryVectorHeaderBytes = 4 * sizeof(std::vector<std::uint8_t>);
constexpr std::uint64_t kReceiptAndDigestLocalBytes = 128;
constexpr std::uint64_t kTemporaryOwnerHandleBytes = 16 * sizeof(void*);
constexpr std::uint64_t kNamedFixedObjectBytes = kPreparedImageStorageObjectBytes + kCodecMemoryStreamObjectBytes +
                                                 kTemporaryVectorHeaderBytes + kReceiptAndDigestLocalBytes +
                                                 kTemporaryOwnerHandleBytes;
static_assert(kClipboardEncodingFixedMetadataBytes == 1024);
static_assert(kClipboardEncodingFixedMetadataBytes >= kNamedFixedObjectBytes);

// Standard C++ witnesses name the observed private fields without including Windows/WIL types.
// Pointer members represent native owner wrappers and the COM vtable. These are field-size checks,
// not assertions of the private native ABI; integration checks actual native sizeof separately.
struct ReceiptKeySizeWitness final
{
    std::array<std::uint8_t, 16> session;
    std::uint64_t serial;
};
struct FingerprintFormatSizeWitness final
{
    std::uint64_t bytes;
    std::array<std::uint8_t, 32> sha256;
};
struct FingerprintBindingSizeWitness final
{
    ReceiptKeySizeWitness key;
    FingerprintFormatSizeWitness png;
    FingerprintFormatSizeWitness dib;
};
struct PreparedImageStorageSizeWitness final
{
    void* png;
    void* dib;
    std::uint32_t pngFormat;
    std::uint32_t sourceSequence;
    void* receipt;
    std::uint32_t receiptFormat;
    std::optional<FingerprintBindingSizeWitness> binding;
};
struct CodecMemoryStreamSizeWitness final
{
    void* comVtable;
    std::atomic<std::uint32_t> references;
    void* buffer;
    std::uint64_t capacity;
    std::uint64_t reserved;
    std::uint32_t pageBytes;
    std::stop_token localCancellation;
    std::stop_token shutdownCancellation;
    void* observerContext;
    void (*observerReached)(void*, std::uint8_t, std::uint64_t) noexcept;
    std::uint32_t (*observerFault)(void*, std::uint8_t) noexcept;
    std::uint64_t committed;
    std::uint64_t cursor;
    std::uint64_t size;
    bool limitExceeded;
};
static_assert(kPreparedImageStorageObjectBytes >= sizeof(PreparedImageStorageSizeWitness));
static_assert(kCodecMemoryStreamObjectBytes >= sizeof(CodecMemoryStreamSizeWitness));
static_assert(kClipboardEncodingFixedMetadataBytes >=
              sizeof(PreparedImageStorageSizeWitness) + sizeof(CodecMemoryStreamSizeWitness) +
                  kTemporaryVectorHeaderBytes + kReceiptAndDigestLocalBytes + kTemporaryOwnerHandleBytes);

struct Fixture final
{
    int width;
    int height;
    std::uint64_t pngCapacity;
    std::uint64_t dibBytes;
    std::uint64_t pngBandRequest;
    std::uint64_t pngEncodePeak;
    std::uint64_t pngCopyPeak;
    std::uint64_t dibStagePeak;
    std::uint64_t expectedPeak;
    const char* name;
};

// Literal known requests and independent live-stage sums. Relative to the earlier 128-byte metadata
// controls, PNG encode gains 131,968 bytes; PNG copy and DIB gain 66,432 bytes. Invalid controls stay zero.
constexpr std::array<Fixture, 15> kFixtures{{
    {1, 1, 4'194'312, 128, 4, 25'429'052, 8'590'392, 4'461'872, 25'429'052, "one-pixel"},
    {30'000, 1, 4'434'304, 120'124, 120'000, 26'029'032, 9'070'376, 4'941'856, 26'029'032, "side-limit"},
    {9'000, 5'000, 364'194'304, 180'000'124, 36'000, 745'465'032, 728'590'376, 724'461'856, 745'465'032,
     "45-million-pixels"},
    {10'000, 10'000, 536'870'912, 400'000'124, 40'000, 1'090'822'248, 1'073'878'056, 1'337'072'928, 1'337'072'928,
     "100-million-pixels-capped"},
    {30'000, 2'218, 536'514'304, 266'160'124, 120'000, 1'090'189'032, 1'073'230'376, 1'069'101'856, 1'090'189'032,
     "cap-break-sample-1"},
    {30'000, 2'219, 536'754'304, 266'280'124, 120'000, 1'090'669'032, 1'073'710'376, 1'069'581'856, 1'090'669'032,
     "cap-break-sample-2"},
    {30'000, 2'220, 536'870'912, 266'400'124, 120'000, 1'090'902'248, 1'073'878'056, 1'069'872'928, 1'090'902'248,
     "cap-break-sample-3"},
    {30'000, 2'221, 536'870'912, 266'520'124, 120'000, 1'090'902'248, 1'073'878'056, 1'070'112'928, 1'090'902'248,
     "cap-saturated"},
    {0, 1, 0, 0, 0, 0, 0, 0, 0, "zero-width"},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, "zero-height"},
    {-1, 1, 0, 0, 0, 0, 0, 0, 0, "negative-width"},
    {1, -1, 0, 0, 0, 0, 0, 0, 0, "negative-height"},
    {30'001, 1, 0, 0, 0, 0, 0, 0, 0, "side-over-limit"},
    {1, 30'001, 0, 0, 0, 0, 0, 0, 0, "height-over-limit"},
    {1'024, 64, 4'718'592, 262'268, 65'536, 26'543'144, 9'638'952, 5'510'432, 26'543'144, "normal-band"},
}};

int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s clipboardbudget.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}
} // namespace

int RunClipboardEncodingBudgetTests()
{
    gFailures = 0;
    for (const Fixture& fixture : kFixtures)
    {
        std::uint64_t pngEncodeDemand = 0;
        std::uint64_t pngVectorGlobalHashDemand = 0;
        std::uint64_t heldPngDibVectorGlobalHashDemand = 0;
        if (fixture.pngCapacity != 0)
        {
            // Independent named live demands use the literal requests, allocator slack, receipt block,
            // WIC allowance, hash object and metadata. They do not call any production planning helper.
            const std::uint64_t heldPngGlobal = (std::min)(fixture.pngCapacity + 65'536, std::uint64_t{536'870'912});
            const std::uint64_t dibGlobal = (std::min)(fixture.dibBytes + 65'536, std::uint64_t{536'870'912});
            pngEncodeDemand = (fixture.pngCapacity + 65'536) + (fixture.pngCapacity + 65'536) +
                              (fixture.pngBandRequest + 65'536) + 16'777'216 + 65'576 + 1024;
            pngVectorGlobalHashDemand = (fixture.pngCapacity + 65'536) + heldPngGlobal + 65'576 + 4096 + 1024;
            heldPngDibVectorGlobalHashDemand =
                heldPngGlobal + (fixture.dibBytes + 65'536) + dibGlobal + 65'576 + 4096 + 1024;
        }
        const std::uint64_t independentPeak =
            (std::max)({pngEncodeDemand, pngVectorGlobalHashDemand, heldPngDibVectorGlobalHashDemand});
        Expect(fixture.pngEncodePeak == pngEncodeDemand && fixture.pngCopyPeak == pngVectorGlobalHashDemand &&
                   fixture.dibStagePeak == heldPngDibVectorGlobalHashDemand && fixture.expectedPeak == independentPeak,
               fixture.name);

        const std::uint64_t actual = ClipboardEncodingBudget(fixture.width, fixture.height);
        Expect(actual == fixture.expectedPeak && actual >= pngEncodeDemand && actual >= pngVectorGlobalHashDemand &&
                   actual >= heldPngDibVectorGlobalHashDemand,
               fixture.name);
    }

    constexpr std::uint64_t kBeforeCap = 1'090'189'032;
    constexpr std::uint64_t kNearCap = 1'090'669'032;
    constexpr std::uint64_t kAtCap = 1'090'902'248;
    constexpr std::uint64_t kAfterCap = 1'090'902'248;
    constexpr std::uint64_t kPngEncodedCapacityCap = 536'870'912;
    static_assert(kBeforeCap < kNearCap && kNearCap < kAtCap && kAtCap == kAfterCap);
    static_assert(kFixtures[5].pngCapacity < kPngEncodedCapacityCap &&
                  kFixtures[6].pngCapacity == kPngEncodedCapacityCap &&
                  kFixtures[7].pngCapacity == kPngEncodedCapacityCap);
    Expect(true, "png-capacity-monotonic-break"); // Compile-time literal witnesses; actual planner checks follow.
    Expect(true, "literal-512-mib-png-cap");
    Expect(ClipboardEncodingBudget(30'000, 2'218) < ClipboardEncodingBudget(30'000, 2'219) &&
               ClipboardEncodingBudget(30'000, 2'219) < ClipboardEncodingBudget(30'000, 2'220),
           "planner-monotonic-before-cap");
    Expect(ClipboardEncodingBudget(30'000, 2'220) == ClipboardEncodingBudget(30'000, 2'221),
           "planner-flat-after-png-cap");
    Expect(ClipboardEncodingBudget(10'001, 10'000) == 0, "over-100-million-pixels");

    // WritePixels requests one whole 120,000-byte row when 4*width > 64 KiB. A normal 1024-wide
    // image instead requests a 16-row, 65,536-byte band. Both buffer allocations receive +65,536.
    constexpr std::uint64_t kWideRowOtherDemands = 25'843'496;
    constexpr std::uint64_t kNormalBandOtherDemands = 26'412'072;
    const std::uint64_t wideRowPeak = ClipboardEncodingBudget(30'000, 1);
    const std::uint64_t normalBandPeak = ClipboardEncodingBudget(1'024, 64);
    static_assert(120'000 > 65'536);
    Expect(wideRowPeak >= kWideRowOtherDemands && normalBandPeak >= kNormalBandOtherDemands &&
               wideRowPeak - kWideRowOtherDemands == 185'536 && normalBandPeak - kNormalBandOtherDemands == 131'072,
           "scratch-wide-one-row-versus-normal-band");
    Expect(true, "fixed-metadata-covers-named-objects"); // Both exact assertions are enforced at file scope.
    return gFailures;
}
