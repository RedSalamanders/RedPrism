#include "CodecMemoryStream.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/result.h>
#pragma warning(pop)

CodecMemoryStream::CodecMemoryStream(ConstructionKey, wil::unique_virtualalloc_ptr<std::uint8_t> buffer,
                                     std::uint64_t capacity, std::uint64_t reserved, std::uint32_t pageBytes,
                                     CancelToken cancellation, CodecMemoryObserver observer) noexcept
    : _buffer(std::move(buffer)), _capacity(capacity), _reserved(reserved), _pageBytes(pageBytes),
      _cancellation(cancellation), _observer(observer)
{
}

HRESULT CodecMemoryStream::Create(std::uint64_t capacity, CancelToken cancellation, CodecMemoryStream** output,
                                  CodecMemoryObserver observer) noexcept
{
    if (!output)
        return E_POINTER;
    *output = nullptr;
    if (!capacity || capacity > kMaximumBytes)
        return E_INVALIDARG;
    if (cancellation.IsCancelled())
        return E_ABORT;
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    if (!info.dwPageSize || info.dwPageSize > 64 * 1024)
        return E_UNEXPECTED; // retain a fixed conservative page-rounding allowance in graph admission
    const auto reserved = ((capacity + info.dwPageSize - 1) / info.dwPageSize) * info.dwPageSize;
    if (observer.fault)
        if (const auto error = observer.fault(observer.context, CodecMemoryStage::Reserve))
            return HRESULT_FROM_WIN32(error);
    wil::unique_virtualalloc_ptr<std::uint8_t> buffer(
        static_cast<std::uint8_t*>(VirtualAlloc(nullptr, static_cast<SIZE_T>(reserved), MEM_RESERVE, PAGE_READWRITE)));
    if (!buffer)
        return HRESULT_FROM_WIN32(GetLastError());
    if (observer.reached)
        observer.reached(observer.context, CodecMemoryStage::Reserve, reserved);
    if (cancellation.IsCancelled())
        return E_ABORT;
    try
    {
        auto stream = std::make_unique<CodecMemoryStream>(ConstructionKey{}, std::move(buffer), capacity, reserved,
                                                          info.dwPageSize, cancellation, observer);
        *output = stream.release(); // transfer the initial COM reference
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // explicit noexcept admission boundary fails the entire encode
    }
}

void CodecMemoryStream::Reached(CodecMemoryStage stage, std::uint64_t count) noexcept
{
    if (_observer.reached)
        _observer.reached(_observer.context, stage, count);
}

std::span<const std::uint8_t> CodecMemoryStream::WrittenBytes() const noexcept
{
    return {_buffer.get(), static_cast<std::size_t>(_size)};
}

HRESULT CodecMemoryStream::Ensure(std::uint64_t bytes) noexcept
{
    if (_cancellation.IsCancelled())
        return E_ABORT;
    if (bytes > _capacity)
    {
        _limitExceeded = true;
        return STG_E_MEDIUMFULL;
    }
    const auto target = ((bytes + _pageBytes - 1) / _pageBytes) * _pageBytes;
    if (target > _reserved)
        return E_UNEXPECTED;
    if (target <= _committed)
        return S_OK;
    while (_committed < target)
    {
        if (_cancellation.IsCancelled())
            return E_ABORT;
        if (_observer.fault)
            if (const auto error = _observer.fault(_observer.context, CodecMemoryStage::Commit))
                return HRESULT_FROM_WIN32(error);
        const auto next = (std::min)(target, _committed + 64 * 1024);
        if (!VirtualAlloc(_buffer.get() + _committed, static_cast<SIZE_T>(next - _committed), MEM_COMMIT,
                          PAGE_READWRITE))
            return HRESULT_FROM_WIN32(GetLastError());
        _committed = next;
        Reached(CodecMemoryStage::Commit, _committed);
    }
    return _cancellation.IsCancelled() ? E_ABORT : S_OK;
}

HRESULT CodecMemoryStream::Zero(std::uint64_t first, std::uint64_t end) noexcept
{
    while (first < end)
    {
        if (_cancellation.IsCancelled())
            return E_ABORT;
        const auto count = (std::min)(std::uint64_t{64 * 1024}, end - first);
        std::memset(_buffer.get() + first, 0, static_cast<std::size_t>(count));
        first += count;
        Reached(CodecMemoryStage::ZeroBand, first);
    }
    return _cancellation.IsCancelled() ? E_ABORT : S_OK;
}

HRESULT CodecMemoryStream::QueryInterface(REFIID iid, void** result) noexcept
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
ULONG CodecMemoryStream::AddRef() noexcept
{
    return _references.fetch_add(1, std::memory_order_relaxed) + 1;
}
ULONG CodecMemoryStream::Release() noexcept
{
    const auto references = _references.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (!references)
    {
        const std::unique_ptr<CodecMemoryStream> retired(this);
    }
    return references;
}

HRESULT CodecMemoryStream::Read(void* output, ULONG count, ULONG* received) noexcept
{
    if (received)
        *received = 0;
    if (!output && count)
        return E_POINTER;
    if (_cancellation.IsCancelled())
        return E_ABORT;
    const auto available = _cursor < _size ? _size - _cursor : 0;
    const auto total = static_cast<ULONG>((std::min)(static_cast<std::uint64_t>(count), available));
    auto* destination = static_cast<std::uint8_t*>(output);
    ULONG done = 0;
    if (total && !destination)
        return E_POINTER;
    while (done < total)
    {
        if (_cancellation.IsCancelled())
            return E_ABORT;
        const auto band = (std::min)(ULONG{64 * 1024}, total - done);
        std::memcpy(destination + done, _buffer.get() + _cursor, band);
        _cursor += band;
        done += band;
        if (received)
            *received = done;
        Reached(CodecMemoryStage::ReadBand, done);
    }
    return _cancellation.IsCancelled() ? E_ABORT : total == count ? S_OK : S_FALSE;
}

HRESULT CodecMemoryStream::Write(const void* input, ULONG count, ULONG* written) noexcept
{
    if (written)
        *written = 0;
    if (!input && count)
        return E_POINTER;
    if (_cancellation.IsCancelled())
        return E_ABORT;
    if (static_cast<std::uint64_t>(count) > _capacity - _cursor)
    {
        _limitExceeded = true;
        return STG_E_MEDIUMFULL;
    }
    if (!count)
        return S_OK;
    const auto previouslyCommitted = _committed;
    RETURN_IF_FAILED(Ensure(_cursor + count));
    if (_cursor > _size)
    {
        // Newly committed pages already read as zero. Clear only previously committed stale bytes.
        RETURN_IF_FAILED(Zero(_size, (std::min)(_cursor, previouslyCommitted)));
        _size = _cursor;
    }
    const auto* source = static_cast<const std::uint8_t*>(input);
    ULONG done = 0;
    while (done < count)
    {
        if (_cancellation.IsCancelled())
            return E_ABORT;
        const auto band = (std::min)(ULONG{64 * 1024}, count - done);
        std::memcpy(_buffer.get() + _cursor, source + done, band);
        _cursor += band;
        _size = (std::max)(_size, _cursor);
        done += band;
        if (written)
            *written = done;
        Reached(CodecMemoryStage::WriteBand, done);
    }
    return _cancellation.IsCancelled() ? E_ABORT : S_OK;
}

HRESULT CodecMemoryStream::Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) noexcept
{
    if (_cancellation.IsCancelled())
        return E_ABORT;
    const auto base = origin == STREAM_SEEK_CUR ? _cursor : origin == STREAM_SEEK_END ? _size : 0;
    if (origin != STREAM_SEEK_SET && origin != STREAM_SEEK_CUR && origin != STREAM_SEEK_END)
        return STG_E_INVALIDFUNCTION;
    if (move.QuadPart < -static_cast<std::int64_t>(base))
        return STG_E_SEEKERROR;
    if (move.QuadPart > static_cast<std::int64_t>(_capacity - base))
    {
        _limitExceeded = true;
        return STG_E_SEEKERROR;
    }
    _cursor = static_cast<std::uint64_t>(static_cast<std::int64_t>(base) + move.QuadPart);
    if (position)
        position->QuadPart = _cursor;
    return S_OK;
}

HRESULT CodecMemoryStream::SetSize(ULARGE_INTEGER size) noexcept
{
    if (_cancellation.IsCancelled())
        return E_ABORT;
    if (size.QuadPart > _capacity)
    {
        _limitExceeded = true;
        return STG_E_MEDIUMFULL;
    }
    if (size.QuadPart > _size)
    {
        const auto previouslyCommitted = _committed;
        RETURN_IF_FAILED(Ensure(size.QuadPart));
        RETURN_IF_FAILED(Zero(_size, (std::min)(size.QuadPart, previouslyCommitted)));
    }
    _size = size.QuadPart; // cursor is independent and remains within capacity after shrink
    return S_OK;
}
HRESULT CodecMemoryStream::CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER* read, ULARGE_INTEGER* written) noexcept
{
    if (read)
        read->QuadPart = 0;
    if (written)
        written->QuadPart = 0;
    return E_NOTIMPL;
}
HRESULT CodecMemoryStream::Commit(DWORD) noexcept
{
    return _cancellation.IsCancelled() ? E_ABORT : S_OK;
}
HRESULT CodecMemoryStream::Revert() noexcept
{
    return STG_E_INVALIDFUNCTION;
}
HRESULT CodecMemoryStream::LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) noexcept
{
    return STG_E_INVALIDFUNCTION;
}
HRESULT CodecMemoryStream::UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) noexcept
{
    return STG_E_INVALIDFUNCTION;
}
HRESULT CodecMemoryStream::Stat(STATSTG* result, DWORD) noexcept
{
    if (!result)
        return E_POINTER;
    *result = {};
    if (_cancellation.IsCancelled())
        return E_ABORT;
    result->type = STGTY_STREAM;
    result->cbSize.QuadPart = _size;
    result->grfMode = STGM_READWRITE;
    return S_OK;
}
HRESULT CodecMemoryStream::Clone(IStream** result) noexcept
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    return E_NOTIMPL;
}
