#include "ClipboardEncodingBudget.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
constexpr std::uint64_t kMaxSide = 30'000;
constexpr std::uint64_t kMaxPixels = 100'000'000;
constexpr std::uint64_t kMaxEncodedBytes = 512ull * 1024 * 1024;
constexpr std::uint64_t kPngCapacityOverhead = 4ull * 1024 * 1024;
constexpr std::uint64_t kWicWorkingAllowance = 16ull * 1024 * 1024;
constexpr std::uint64_t kBandTargetBytes = 64ull * 1024;
constexpr std::uint64_t kGlobalSlack = 64ull * 1024;
constexpr std::uint64_t kAllocationRoundingBytes = 64ull * 1024;
constexpr std::uint64_t kReceiptBytes = 40;
constexpr std::uint64_t kHashObjectBytes = 4096;
// Declared fixed object allowances: PreparedImage::Storage (three global owners, format/sequence IDs,
// optional Binding), the CodecMemoryStream allocation (COM base, buffer owner, counters, cancellation
// and observer), four possible vector headers (caller PNG, returned encoded, band, DIB), receipt packet,
// key/digest locals and temporary COM/global/lock owners. These object allowances are not native sizeof
// measurements; the primary adapter must check its private Windows types against these named limits.
constexpr std::uint64_t kPreparedImageStorageObjectBytes = 256;
constexpr std::uint64_t kCodecMemoryStreamObjectBytes = 256;
constexpr std::uint64_t kTemporaryVectorHeaderBytes = 4 * sizeof(std::vector<std::uint8_t>);
constexpr std::uint64_t kReceiptAndDigestLocalBytes = 128;
constexpr std::uint64_t kTemporaryOwnerHandleBytes = 16 * sizeof(void*);
static_assert(kClipboardEncodingFixedMetadataBytes >= kPreparedImageStorageObjectBytes + kCodecMemoryStreamObjectBytes +
                                                          kTemporaryVectorHeaderBytes + kReceiptAndDigestLocalBytes +
                                                          kTemporaryOwnerHandleBytes);

[[nodiscard]] bool Add(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept
{
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left)
    {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > (std::numeric_limits<std::uint64_t>::max)() / left)
    {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool Sum(std::uint64_t first, std::uint64_t second, std::uint64_t third, std::uint64_t fourth,
                       std::uint64_t fifth, std::uint64_t& result) noexcept
{
    std::uint64_t partial = 0;
    return Add(first, second, partial) && Add(partial, third, partial) && Add(partial, fourth, partial) &&
           Add(partial, fifth, result);
}

[[nodiscard]] bool GlobalCapacity(std::uint64_t requested, std::uint64_t& result) noexcept
{
    std::uint64_t withSlack = 0;
    if (!Add(requested, kGlobalSlack, withSlack))
    {
        return false;
    }
    // GlobalCopy rejects exposed blocks above the encoded-byte cap. Otherwise its private adapter
    // permits at most request + 64 KiB, which covers allocator rounding and initialized padding.
    result = (std::min)(withSlack, kMaxEncodedBytes);
    return true;
}

[[nodiscard]] bool Plan(int width, int height, std::uint64_t& peak) noexcept
{
    peak = 0;
    if (width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) > kMaxSide ||
        static_cast<std::uint64_t>(height) > kMaxSide)
    {
        return false;
    }

    std::uint64_t pixels = 0;
    std::uint64_t rasterBytes = 0;
    if (!Multiply(static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height), pixels) || pixels == 0 ||
        pixels > kMaxPixels || !Multiply(pixels, 4, rasterBytes))
    {
        return false;
    }

    // Matches EncodeMemoryCapacity for the BGRA clipboard input: min(512 MiB, 4 MiB + 2 * 4 * pixels).
    std::uint64_t doubledRasterBytes = 0;
    std::uint64_t uncappedPngCapacity = 0;
    if (!Multiply(rasterBytes, 2, doubledRasterBytes) ||
        !Add(kPngCapacityOverhead, doubledRasterBytes, uncappedPngCapacity))
    {
        return false;
    }
    const std::uint64_t pngCapacity = (std::min)(uncappedPngCapacity, kMaxEncodedBytes);

    // EncodeToFormat keeps its page-rounded CodecMemoryStream reserve while it allocates and copies
    // the returned vector. The stream's page size is <=64 KiB; charge that full rounding allowance.
    std::uint64_t pngSinkReserve = 0;
    if (!Add(pngCapacity, kAllocationRoundingBytes, pngSinkReserve))
    {
        return false;
    }

    // EncodeToFormat's sourceStride is width*4 and bandRows=max(1, 64 KiB/sourceStride).
    // The sole conversion vector remains live through WritePixels; no additional full-frame copy exists.
    std::uint64_t sourceStride = 0;
    if (!Multiply(static_cast<std::uint64_t>(width), 4, sourceStride))
    {
        return false;
    }
    const std::uint64_t bandRows = (std::max)(std::uint64_t{1}, kBandTargetBytes / sourceStride);
    const std::uint64_t allocatedRows = (std::min)(bandRows, static_cast<std::uint64_t>(height));
    std::uint64_t pngBandBytes = 0;
    if (!Multiply(sourceStride, allocatedRows, pngBandBytes))
    {
        return false;
    }

    std::uint64_t dibBytes = 0;
    if (!Add(124, rasterBytes, dibBytes))
    {
        return false;
    }

    // Each vector's requested capacity receives its own 64 KiB allocation/page rounding allowance.
    // In particular, the PNG band is a separate allocation even when its logical request is one row.
    std::uint64_t pngVectorAllocation = 0;
    std::uint64_t pngBandAllocation = 0;
    std::uint64_t dibVectorAllocation = 0;
    if (!Add(pngCapacity, kAllocationRoundingBytes, pngVectorAllocation) ||
        !Add(pngBandBytes, kAllocationRoundingBytes, pngBandAllocation) ||
        !Add(dibBytes, kAllocationRoundingBytes, dibVectorAllocation))
    {
        return false;
    }

    std::uint64_t receiptGlobal = 0;
    std::uint64_t pngGlobal = 0;
    std::uint64_t dibGlobal = 0;
    if (!GlobalCapacity(kReceiptBytes, receiptGlobal) || !GlobalCapacity(pngCapacity, pngGlobal) ||
        !GlobalCapacity(dibBytes, dibGlobal))
    {
        return false;
    }

    // PNG encode phase: sink reserve + returned vector + row band + declared WIC allowance +
    // receipt owner/metadata. WIC's private allocation is opaque; 16 MiB is an explicit planning
    // allowance and does not make that implementation-owned workspace hard-bounded.
    std::uint64_t pngEncodePeak = 0;
    if (!Sum(pngSinkReserve, pngVectorAllocation, pngBandAllocation, kWicWorkingAllowance, receiptGlobal,
             pngEncodePeak) ||
        !Add(pngEncodePeak, kClipboardEncodingFixedMetadataBytes, pngEncodePeak))
    {
        return false;
    }

    // After EncodePng returns its sink is gone. During GlobalCopy, the PNG vector overlaps the
    // PNG global; hashing then retains both globals plus the 4096-byte CNG object on the stack.
    std::uint64_t pngCopyPeak = 0;
    if (!Sum(pngVectorAllocation, pngGlobal, receiptGlobal, kHashObjectBytes, kClipboardEncodingFixedMetadataBytes,
             pngCopyPeak))
    {
        return false;
    }
    const std::uint64_t pngStagePeak = (std::max)(pngEncodePeak, pngCopyPeak);

    // DIB rows are read and unpremultiplied directly in their destination vector, so that row span
    // adds no scratch allocation. The completed PNG global and optional receipt remain held while
    // DIB vector/global overlap and the DIB hash runs.
    std::uint64_t dibStagePeak = 0;
    if (!Sum(pngGlobal, receiptGlobal, dibVectorAllocation, dibGlobal, kHashObjectBytes, dibStagePeak) ||
        !Add(dibStagePeak, kClipboardEncodingFixedMetadataBytes, dibStagePeak))
    {
        return false;
    }

    // PNG and DIB are sequential scopes in ClipboardTransfer::Prepare. Their whole-stage peaks do
    // not coexist, so admission charges the larger complete phase rather than their sum.
    peak = (std::max)(pngStagePeak, dibStagePeak);
    return true;
}
} // namespace

std::uint64_t ClipboardEncodingBudget(int width, int height) noexcept
{
    std::uint64_t peak = 0;
    return Plan(width, height, peak) ? peak : 0;
}
