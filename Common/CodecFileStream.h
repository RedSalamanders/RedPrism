#pragma once

#include "Executor.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <objidl.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Private WIC file stream. One worker owns the cursor; only COM reference counting is concurrent.
// Reads retain an opened, size-checked source with write/delete sharing denied. Writes own an exclusive temporary.
class CodecFileStream final : public IStream
{
    struct ConstructionKey final
    {
    };

  public:
    [[nodiscard]] static HRESULT Create(const wchar_t* path, std::uint64_t maximumBytes, CancelToken cancellation,
                                        CodecFileStream** result) noexcept;
    [[nodiscard]] static HRESULT CreateRead(const wchar_t* path, std::uint64_t maximumBytes, CancelToken cancellation,
                                            CodecFileStream** result) noexcept;
    CodecFileStream(ConstructionKey, wil::unique_hfile file, std::uint64_t maximumBytes, CancelToken cancellation,
                    bool readOnly) noexcept;
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
    friend struct std::default_delete<CodecFileStream>;
    [[nodiscard]] static HRESULT Open(const wchar_t* path, std::uint64_t maximumBytes, CancelToken cancellation,
                                      bool readOnly, CodecFileStream** result) noexcept;
    ~CodecFileStream() = default;
    std::atomic<ULONG> _references{1};
    wil::unique_hfile _file;
    const std::uint64_t _maximumBytes;
    const CancelToken _cancellation;
    const bool _readOnly;
    bool _dirty = false;
};
