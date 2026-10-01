#pragma once

#include "Executor.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <objidl.h>
#include <span>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

enum class CodecMemoryStage : std::uint8_t
{
    Reserve,
    Commit,
    ZeroBand,
    WriteBand,
    ReadBand
};
struct CodecMemoryObserver final
{
    void* context = nullptr; // survives the last COM reference; worker calls only
    void (*reached)(void*, CodecMemoryStage, std::uint64_t) noexcept = nullptr;
    std::uint32_t (*fault)(void*, CodecMemoryStage) noexcept = nullptr;
};

// Capped private WIC memory stream. One worker owns all cursor/data access, only COM references are concurrent.
// Reserve address space once, commit bounded pages lazily, and initialize all logical growth before exposure.
// No heap allocation or C++ exception occurs in COM stream methods. No payload survives a failed encoding.
class CodecMemoryStream final : public IStream
{
    struct ConstructionKey final
    {
    };

  public:
    static constexpr std::uint64_t kMaximumBytes = 512ull * 1024 * 1024;
    [[nodiscard]] static HRESULT Create(std::uint64_t capacity, CancelToken cancellation, CodecMemoryStream** output,
                                        CodecMemoryObserver observer = {}) noexcept;
    CodecMemoryStream(ConstructionKey, wil::unique_virtualalloc_ptr<std::uint8_t> buffer, std::uint64_t capacity,
                      std::uint64_t reserved, std::uint32_t pageBytes, CancelToken cancellation,
                      CodecMemoryObserver observer) noexcept;
    // Borrow only while this COM owner is retained, after the encoder completes. Never escape this view.
    [[nodiscard]] std::span<const std::uint8_t> WrittenBytes() const noexcept;
    [[nodiscard]] std::uint64_t CommittedBytes() const noexcept
    {
        return _committed;
    }
    [[nodiscard]] bool LimitExceeded() const noexcept
    {
        return _limitExceeded;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE Read(void*, ULONG, ULONG*) noexcept override;
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) noexcept override;
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER*) noexcept override;
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) noexcept override;
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) noexcept override;
    HRESULT STDMETHODCALLTYPE Commit(DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Revert() noexcept override;
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Stat(STATSTG*, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Clone(IStream**) noexcept override;

  private:
    friend struct std::default_delete<CodecMemoryStream>;
    ~CodecMemoryStream() = default;
    [[nodiscard]] HRESULT Ensure(std::uint64_t bytes) noexcept;
    [[nodiscard]] HRESULT Zero(std::uint64_t first, std::uint64_t end) noexcept;
    void Reached(CodecMemoryStage stage, std::uint64_t count) noexcept;
    std::atomic<ULONG> _references{1};
    wil::unique_virtualalloc_ptr<std::uint8_t> _buffer;
    const std::uint64_t _capacity, _reserved;
    const std::uint32_t _pageBytes;
    const CancelToken _cancellation;
    const CodecMemoryObserver _observer;
    std::uint64_t _committed = 0, _cursor = 0, _size = 0;
    bool _limitExceeded = false;
};
