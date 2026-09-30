#include "CodecFileStream.h"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

CodecFileStream::CodecFileStream(wil::unique_hfile file, std::uint64_t maximumBytes, CancelToken cancellation) noexcept
    : _file(std::move(file)), _maximumBytes(maximumBytes), _cancellation(std::move(cancellation))
{
}

HRESULT CodecFileStream::Create(const wchar_t* path, std::uint64_t maximumBytes, CancelToken cancellation,
                                CodecFileStream** result) noexcept
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!path || maximumBytes == 0 || maximumBytes > static_cast<std::uint64_t>(INT64_MAX))
        return E_INVALIDARG;
    if (cancellation.IsCancelled())
        return E_ABORT;
    wil::unique_hfile file{
        CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file)
        return HRESULT_FROM_WIN32(GetLastError());
    auto* stream = new (std::nothrow) CodecFileStream(std::move(file), maximumBytes, std::move(cancellation));
    if (!stream)
    {
        // Complete failed construction, with no published owner and no temporary left by this factory.
        file.reset();
        (void)DeleteFileW(path);
        return E_OUTOFMEMORY;
    }
    *result = stream;
    return S_OK;
}

HRESULT CodecFileStream::QueryInterface(REFIID iid, void** result) noexcept
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (iid != IID_IUnknown && iid != IID_ISequentialStream && iid != IID_IStream)
        return E_NOINTERFACE;
    *result = static_cast<IStream*>(this);
    AddRef();
    return S_OK;
}

ULONG CodecFileStream::AddRef() noexcept
{
    return _references.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG CodecFileStream::Release() noexcept
{
    const ULONG references = _references.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (references == 0)
        delete this;
    return references;
}

HRESULT CodecFileStream::Read(void* output, ULONG count, ULONG* read) noexcept
{
    if (read)
        *read = 0;
    if (!output && count != 0)
        return E_POINTER;
    if (!_file)
        return STG_E_REVERTED;
    auto* bytes = static_cast<std::uint8_t*>(output);
    ULONG done = 0;
    while (done < count)
    {
        if (_cancellation.IsCancelled())
            return E_ABORT;
        const DWORD chunk = std::min<ULONG>(64 * 1024, count - done);
        DWORD received = 0;
        if (!ReadFile(_file.get(), bytes + done, chunk, &received, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());
        done += received;
        if (read)
            *read = done;
        if (received < chunk)
            return S_FALSE;
    }
    return S_OK;
}

HRESULT CodecFileStream::Write(const void* input, ULONG count, ULONG* written) noexcept
{
    if (written)
        *written = 0;
    if (!input && count != 0)
        return E_POINTER;
    if (!_file)
        return STG_E_REVERTED;
    if (_cancellation.IsCancelled())
        return E_ABORT;
    LARGE_INTEGER position{};
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(_file.get(), zero, &position, FILE_CURRENT))
        return HRESULT_FROM_WIN32(GetLastError());
    if (position.QuadPart < 0 || static_cast<std::uint64_t>(position.QuadPart) > _maximumBytes ||
        count > _maximumBytes - static_cast<std::uint64_t>(position.QuadPart))
        return STG_E_MEDIUMFULL;
    const auto* bytes = static_cast<const std::uint8_t*>(input);
    ULONG done = 0;
    while (done < count)
    {
        if (_cancellation.IsCancelled())
            return E_ABORT;
        const DWORD chunk = std::min<ULONG>(64 * 1024, count - done);
        DWORD transferred = 0;
        if (!WriteFile(_file.get(), bytes + done, chunk, &transferred, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());
        done += transferred;
        _dirty = _dirty || transferred != 0;
        if (written)
            *written = done;
        if (transferred != chunk)
            return STG_E_WRITEFAULT;
    }
    return S_OK;
}

HRESULT CodecFileStream::Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) noexcept
{
    if (!_file)
        return STG_E_REVERTED;
    LARGE_INTEGER base{};
    LARGE_INTEGER zero{};
    if (origin == STREAM_SEEK_CUR)
    {
        if (!SetFilePointerEx(_file.get(), zero, &base, FILE_CURRENT))
            return HRESULT_FROM_WIN32(GetLastError());
    }
    else if (origin == STREAM_SEEK_END)
    {
        if (!GetFileSizeEx(_file.get(), &base))
            return HRESULT_FROM_WIN32(GetLastError());
    }
    else if (origin != STREAM_SEEK_SET)
        return STG_E_INVALIDFUNCTION;
    if (base.QuadPart < 0 || static_cast<std::uint64_t>(base.QuadPart) > _maximumBytes ||
        move.QuadPart < -base.QuadPart || move.QuadPart > static_cast<std::int64_t>(_maximumBytes) - base.QuadPart)
        return STG_E_SEEKERROR;
    LARGE_INTEGER target{};
    target.QuadPart = base.QuadPart + move.QuadPart;
    if (!SetFilePointerEx(_file.get(), target, nullptr, FILE_BEGIN))
        return HRESULT_FROM_WIN32(GetLastError());
    if (position)
        position->QuadPart = static_cast<std::uint64_t>(target.QuadPart);
    return S_OK;
}

HRESULT CodecFileStream::SetSize(ULARGE_INTEGER size) noexcept
{
    if (!_file)
        return STG_E_REVERTED;
    if (_cancellation.IsCancelled())
        return E_ABORT;
    if (size.QuadPart > _maximumBytes)
        return STG_E_MEDIUMFULL;
    LARGE_INTEGER zero{};
    LARGE_INTEGER previous{};
    if (!SetFilePointerEx(_file.get(), zero, &previous, FILE_CURRENT))
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER target{};
    target.QuadPart = static_cast<std::int64_t>(size.QuadPart);
    if (!SetFilePointerEx(_file.get(), target, nullptr, FILE_BEGIN))
        return HRESULT_FROM_WIN32(GetLastError());
    const HRESULT resized = SetEndOfFile(_file.get()) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    _dirty = _dirty || SUCCEEDED(resized);
    if (!SetFilePointerEx(_file.get(), previous, nullptr, FILE_BEGIN))
        return FAILED(resized) ? resized : HRESULT_FROM_WIN32(GetLastError());
    return resized;
}

HRESULT CodecFileStream::CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER* read, ULARGE_INTEGER* written) noexcept
{
    if (read)
        *read = {};
    if (written)
        *written = {};
    return E_NOTIMPL;
}

HRESULT CodecFileStream::Commit(DWORD) noexcept
{
    if (!_file)
        return STG_E_REVERTED;
    if (_cancellation.IsCancelled())
        return E_ABORT;
    if (!_dirty)
        return S_OK;
    if (!FlushFileBuffers(_file.get()))
        return HRESULT_FROM_WIN32(GetLastError());
    _dirty = false;
    return S_OK;
}

HRESULT CodecFileStream::CloseChecked() noexcept
{
    if (!_file)
        return STG_E_REVERTED;
    const HRESULT flushed = Commit(STGC_DEFAULT);
    if (FAILED(flushed))
        return flushed;
    const HANDLE handle = _file.release();
    return CloseHandle(handle) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

HRESULT CodecFileStream::Revert() noexcept
{
    return STG_E_INVALIDFUNCTION;
}
HRESULT CodecFileStream::LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) noexcept
{
    return STG_E_INVALIDFUNCTION;
}
HRESULT CodecFileStream::UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) noexcept
{
    return STG_E_INVALIDFUNCTION;
}
HRESULT CodecFileStream::Stat(STATSTG* result, DWORD) noexcept
{
    if (!result)
        return E_POINTER;
    *result = {};
    if (!_file)
        return STG_E_REVERTED;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(_file.get(), &size))
        return HRESULT_FROM_WIN32(GetLastError());
    result->type = STGTY_STREAM;
    result->cbSize.QuadPart = static_cast<std::uint64_t>(size.QuadPart);
    result->grfMode = STGM_READWRITE | STGM_SHARE_EXCLUSIVE;
    return S_OK;
}
HRESULT CodecFileStream::Clone(IStream** result) noexcept
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    return E_NOTIMPL;
}
