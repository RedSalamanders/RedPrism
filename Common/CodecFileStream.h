#pragma once

#include "Executor.h"

#include <atomic>
#include <cstdint>
#include <objidl.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Private WIC destination. One calling worker owns cursor operations; only COM reference counting is concurrent.
// Encoded bytes go straight to an exclusive temporary file, with a hard size cap and 64 KiB stop checkpoints.
class CodecFileStream final : public IStream
{
  public:
    [[nodiscard]] static HRESULT Create(const wchar_t* path, std::uint64_t maximumBytes, CancelToken cancellation,
                                        CodecFileStream** result) noexcept;
    [[nodiscard]] HRESULT CloseChecked() noexcept;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE Read(void* output, ULONG count, ULONG* read) noexcept override;
    HRESULT STDMETHODCALLTYPE Write(const void* input, ULONG count, ULONG* written) noexcept override;
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) noexcept override;
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER size) noexcept override;
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) noexcept override;
    HRESULT STDMETHODCALLTYPE Commit(DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Revert() noexcept override;
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* result, DWORD flags) noexcept override;
    HRESULT STDMETHODCALLTYPE Clone(IStream** result) noexcept override;

  private:
    CodecFileStream(wil::unique_hfile file, std::uint64_t maximumBytes, CancelToken cancellation) noexcept;
    ~CodecFileStream() = default;
    std::atomic<ULONG> _references{1};
    wil::unique_hfile _file;
    const std::uint64_t _maximumBytes;
    const CancelToken _cancellation;
    bool _dirty = false;
};
