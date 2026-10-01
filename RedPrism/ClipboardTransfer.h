#pragma once

#include "Document.h"
#include "Executor.h"

#include <cstdint>
#include <memory>
#include <vector>

// Private Win32 adapter. Calls belong to admitted workers, never to the Application thread. No HWND,
// clipboard handle or locked OS pointer escapes this boundary. Each attempt opens the clipboard once.
namespace ClipboardTransfer
{
enum class Status : std::uint8_t
{
    Ok,
    Busy,
    Cancelled,
    Stale,
    Invalid,
    TooLarge,
    OutOfMemory,
    NativeFailure,
};

enum class Stage : std::uint8_t
{
    GlobalsPrepared,
    WindowCreated,
    Opened,
    Emptied,
    PngPlaced,
    DibPlaced,
    Closed,
    Inspected,
    Copied,
};

enum class Fault : std::uint8_t
{
    Window,
    Open,
    Empty,
    Png,
    Dib,
    PngGlobal,
    DibGlobal,
    PngRead,
    DibRead,
};

struct Observer final
{
    void* context = nullptr;
    void (*reached)(void*, Stage) noexcept = nullptr;
    // Nonzero injects an error before the named native call. It never fakes a successful publication.
    std::uint32_t (*fault)(void*, Fault) noexcept = nullptr;
};

struct Publication final
{
    Status status = Status::Invalid;
    std::uint32_t nativeError = 0;
    std::uint32_t sequence = 0;
    bool emptied = false;
    bool pngPlaced = false;
    bool dibPlaced = false;
};

class PreparedImage final
{
  public:
    PreparedImage() noexcept;
    ~PreparedImage();
    PreparedImage(PreparedImage&&) noexcept;
    PreparedImage& operator=(PreparedImage&&) noexcept;
    PreparedImage(const PreparedImage&) = delete;
    PreparedImage& operator=(const PreparedImage&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::uint64_t RetainedBytes() const noexcept;
    [[nodiscard]] std::uint32_t SourceSequence() const noexcept;

  private:
    struct Storage;
    std::unique_ptr<Storage> _storage;
    friend Status Prepare(const Raster&, double, std::uint32_t, PreparedImage&, CancelToken, Observer);
    friend Publication Publish(PreparedImage&, CancelToken, Observer) noexcept;
};

// Encodes both immediate formats and owns both complete GMEM_MOVEABLE blocks BEFORE any OS mutation.
// Allocation exceptions propagate to the scheduler worker boundary. Native allocation failures are explicit.
// The source sequence is captured with the source/private copy before encoding and sealed through retries.
[[nodiscard]] Status Prepare(const Raster& image, double resolution, std::uint32_t sourceSequence,
                             PreparedImage& output, CancelToken cancellation = {}, Observer observer = {});
// Busy, Stale and cancellation before Empty retain the blocks for an acknowledged retry/drop. Empty is an
// irreversible external mutation: after it, attempt both formats even if cancellation arrives, report exact
// publication truth and consume the prepared input. Never retry a result whose emptied flag is true.
[[nodiscard]] Publication Publish(PreparedImage& image, CancelToken cancellation = {}, Observer observer = {}) noexcept;

struct FormatInfo final
{
    std::uint64_t encodedBytes = 0;
    std::uint64_t pixels = 0;
    int width = 0;
    int height = 0;
    [[nodiscard]] bool operator==(const FormatInfo&) const noexcept = default;
};

class Inspection final
{
  public:
    Inspection() noexcept = default;
    Inspection(Inspection&& other) noexcept;
    Inspection& operator=(Inspection&& other) noexcept;
    Inspection(const Inspection&) = delete;
    Inspection& operator=(const Inspection&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return _valid;
    }
    [[nodiscard]] std::uint32_t Sequence() const noexcept
    {
        return _sequence;
    }
    [[nodiscard]] const FormatInfo& Png() const noexcept
    {
        return _png;
    }
    [[nodiscard]] const FormatInfo& Dib() const noexcept
    {
        return _dib;
    }
    // Owned byte copies plus one largest final pixel output. Codec/private document/history headroom belongs
    // to the caller's whole-graph reservation; this value alone is not a complete Paste admission bound.
    [[nodiscard]] std::uint64_t CopyAndPixelsBytes() const noexcept;

  private:
    bool _valid = false;
    std::uint32_t _sequence = 0;
    std::uint32_t _pngFormat = 0;
    std::uint32_t _dibFormat = 0;
    FormatInfo _png;
    FormatInfo _dib;
    std::int64_t _pixelBudget = 0;
    friend Status Inspect(std::int64_t, Inspection&, CancelToken, Observer);
    friend Status Copy(const Inspection&, class ImageCopy&, CancelToken, Observer);
};

class ImageCopy final
{
  public:
    ImageCopy() noexcept = default;
    ImageCopy(ImageCopy&&) noexcept = default;
    ImageCopy& operator=(ImageCopy&&) noexcept = default;
    ImageCopy(const ImageCopy&) = delete;
    ImageCopy& operator=(const ImageCopy&) = delete;
    [[nodiscard]] std::uint32_t Sequence() const noexcept
    {
        return _sequence;
    }
    [[nodiscard]] std::uint64_t RetainedBytes() const noexcept;

  private:
    std::uint32_t _sequence = 0;
    std::int64_t _pixelBudget = 0;
    std::vector<std::uint8_t> _png;
    std::vector<std::uint8_t> _dib;
    friend Status Copy(const Inspection&, ImageCopy&, CancelToken, Observer);
    friend Status Decode(const ImageCopy&, Raster&, CancelToken);
};

// Inspect locks borrowed global bytes only inside one open scope. PNG first, independently inspect DIBV5/DIB
// for a permitted fallback. Hard encoded/pixel/side caps are checked before large owned allocation.
[[nodiscard]] Status Inspect(std::int64_t pixelBudget, Inspection& output, CancelToken cancellation = {},
                             Observer observer = {});
// Reopen at the exact inspected sequence, size and metadata. Copy only inspected valid formats, in bounded
// chunks, with no borrowed handle/pointer retained after CloseClipboard. Failed/cancelled output is empty.
[[nodiscard]] Status Copy(const Inspection& inspected, ImageCopy& output, CancelToken cancellation = {},
                          Observer observer = {});
[[nodiscard]] Status Decode(const ImageCopy& copy, Raster& output, CancelToken cancellation = {});
} // namespace ClipboardTransfer
