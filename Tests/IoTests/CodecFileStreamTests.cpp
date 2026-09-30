#include "CodecFileStreamTests.h"

#include "CodecFileStream.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string>
#include <system_error>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
std::atomic<std::uint32_t> gDirectorySequence{0};
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s codefstream.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void ExpectHr(HRESULT actual, HRESULT expected, const char* name) noexcept
{
    if (actual == expected)
    {
        std::printf("PASS codefstream.%s\n", name);
    }
    else
    {
        std::printf("FAIL codefstream.%s actual=0x%08lX expected=0x%08lX\n", name, static_cast<unsigned long>(actual),
                    static_cast<unsigned long>(expected));
        ++gFailures;
    }
}

class ScratchDirectory final
{
  public:
    ScratchDirectory() noexcept
    {
        std::array<wchar_t, 32768> temp{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
        if (length == 0 || static_cast<std::size_t>(length) >= temp.size())
        {
            return;
        }

        const DWORD attributes = GetFileAttributesW(temp.data());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            return;
        }

        for (unsigned int attempt = 0; attempt < 32; ++attempt)
        {
            _path.assign(temp.data(), length);
            if (!_path.empty() && _path.back() != L'\\')
            {
                _path.push_back(L'\\');
            }
            _path.append(L"RedPrism.CodecFileStream.");
            _path.append(std::to_wstring(GetCurrentProcessId()));
            _path.push_back(L'.');
            _path.append(std::to_wstring(gDirectorySequence.fetch_add(1, std::memory_order_relaxed)));
            if (CreateDirectoryW(_path.c_str(), nullptr))
            {
                _created = true;
                return;
            }
            if (GetLastError() != ERROR_ALREADY_EXISTS)
            {
                return;
            }
        }
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    ~ScratchDirectory()
    {
        if (_created)
        {
            std::error_code error;
            std::filesystem::remove_all(_path, error);
        }
    }

    [[nodiscard]] bool IsCreated() const noexcept
    {
        return _created;
    }

    [[nodiscard]] std::wstring File(const wchar_t* name) const
    {
        std::wstring result = _path;
        result.push_back(L'\\');
        result.append(name);
        return result;
    }

  private:
    std::wstring _path;
    bool _created = false;
};

[[nodiscard]] bool ReadFileBytes(const std::wstring& path, std::span<std::uint8_t> output) noexcept
{
    wil::unique_hfile file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file || output.size() > MAXDWORD)
    {
        return false;
    }
    DWORD received = 0;
    if (!output.empty() && !ReadFile(file.get(), output.data(), static_cast<DWORD>(output.size()), &received, nullptr))
    {
        return false;
    }
    LARGE_INTEGER size{};
    return GetFileSizeEx(file.get(), &size) && size.QuadPart >= 0 &&
           static_cast<std::uint64_t>(size.QuadPart) == output.size() &&
           static_cast<std::size_t>(received) == output.size();
}

[[nodiscard]] bool WriteFileBytes(const std::wstring& path, std::span<const std::uint8_t> input) noexcept
{
    wil::unique_hfile file{
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file || input.size() > MAXDWORD)
    {
        return false;
    }
    DWORD written = 0;
    return (input.empty() ||
            (WriteFile(file.get(), input.data(), static_cast<DWORD>(input.size()), &written, nullptr) != FALSE &&
             static_cast<std::size_t>(written) == input.size())) &&
           FlushFileBuffers(file.get()) != FALSE;
}

[[nodiscard]] bool CursorIs(IStream* stream, std::uint64_t expected) noexcept
{
    LARGE_INTEGER move{};
    ULARGE_INTEGER position{};
    return SUCCEEDED(stream->Seek(move, STREAM_SEEK_CUR, &position)) && position.QuadPart == expected;
}

void ByteCapRejectsWithoutChangingFile()
{
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "cap.temp-root-created");
    if (!scratch.IsCreated())
    {
        return;
    }

    const std::wstring path = scratch.File(L"bounded.bin");
    wil::com_ptr_nothrow<CodecFileStream> stream;
    ExpectHr(CodecFileStream::Create(path.c_str(), 4, {}, stream.put()), S_OK, "cap.create");
    if (!stream)
    {
        return;
    }

    constexpr std::array<std::uint8_t, 3> initial{'a', 'b', 'c'};
    ULONG written = 99;
    ExpectHr(stream->Write(initial.data(), static_cast<ULONG>(initial.size()), &written), S_OK,
             "cap.write-within-limit");
    Expect(written == initial.size(), "cap.write-count");

    constexpr std::array<std::uint8_t, 2> overflow{'x', 'y'};
    written = 99;
    ExpectHr(stream->Write(overflow.data(), static_cast<ULONG>(overflow.size()), &written), STG_E_MEDIUMFULL,
             "cap.write-rejects-over-limit");
    Expect(written == 0 && CursorIs(stream.get(), 3), "cap.write-rejects-without-partial-output-or-cursor-move");

    ULARGE_INTEGER unchangedPosition{};
    unchangedPosition.QuadPart = 0xA5A5;
    LARGE_INTEGER beyond{};
    beyond.QuadPart = 5;
    ExpectHr(stream->Seek(beyond, STREAM_SEEK_SET, &unchangedPosition), STG_E_SEEKERROR, "cap.seek-rejects-over-limit");
    Expect(unchangedPosition.QuadPart == 0xA5A5 && CursorIs(stream.get(), 3),
           "cap.seek-rejects-without-position-change");

    ULARGE_INTEGER tooLarge{};
    tooLarge.QuadPart = 5;
    ExpectHr(stream->SetSize(tooLarge), STG_E_MEDIUMFULL, "cap.resize-rejects-over-limit");
    Expect(CursorIs(stream.get(), 3), "cap.resize-rejects-without-cursor-change");

    ExpectHr(stream->CloseChecked(), S_OK, "cap.close");
    stream.reset();
    std::array<std::uint8_t, initial.size()> actual{};
    Expect(ReadFileBytes(path, actual) && actual == initial, "cap.disk-bytes-unchanged-after-rejections");
}

void LiteralWriteSeekOverwriteReadResizeAndStat()
{
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "literal.temp-root-created");
    if (!scratch.IsCreated())
    {
        return;
    }

    const std::wstring path = scratch.File(L"literal.bin");
    wil::com_ptr_nothrow<CodecFileStream> stream;
    ExpectHr(CodecFileStream::Create(path.c_str(), 16, {}, stream.put()), S_OK, "literal.create");
    if (!stream)
    {
        return;
    }

    constexpr std::array<std::uint8_t, 6> original{'a', 'b', 'c', 'd', 'e', 'f'};
    ULONG count = 0;
    ExpectHr(stream->Write(original.data(), static_cast<ULONG>(original.size()), &count), S_OK, "literal.write");
    Expect(count == original.size(), "literal.write-count");

    LARGE_INTEGER move{};
    move.QuadPart = 2;
    ULARGE_INTEGER position{};
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, &position), S_OK, "literal.seek-to-overwrite");
    Expect(position.QuadPart == 2, "literal.seek-position");
    constexpr std::array<std::uint8_t, 2> replacement{'X', 'Y'};
    ExpectHr(stream->Write(replacement.data(), static_cast<ULONG>(replacement.size()), &count), S_OK,
             "literal.overwrite");
    Expect(count == replacement.size(), "literal.overwrite-count");

    move.QuadPart = 0;
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, &position), S_OK, "literal.seek-to-read");
    std::array<std::uint8_t, 6> actual{};
    count = 0;
    ExpectHr(stream->Read(actual.data(), static_cast<ULONG>(actual.size()), &count), S_OK, "literal.read");
    constexpr std::array<std::uint8_t, 6> overwritten{'a', 'b', 'X', 'Y', 'e', 'f'};
    Expect(count == actual.size() && actual == overwritten, "literal.read-exact-bytes");

    std::uint8_t eofByte = 0xCC;
    count = 99;
    ExpectHr(stream->Read(&eofByte, 1, &count), S_FALSE, "literal.read-eof-is-short-read");
    Expect(count == 0, "literal.eof-read-count-zero");

    move.QuadPart = 5;
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, &position), S_OK, "literal.seek-before-resize");
    ULARGE_INTEGER larger{};
    larger.QuadPart = 8;
    ExpectHr(stream->SetSize(larger), S_OK, "literal.resize-extend");
    Expect(CursorIs(stream.get(), 5), "literal.extend-preserves-cursor");

    STATSTG stat{};
    ExpectHr(stream->Stat(&stat, STATFLAG_NONAME), S_OK, "literal.stat-extended");
    Expect(stat.type == STGTY_STREAM && stat.cbSize.QuadPart == 8 &&
               stat.grfMode == (STGM_READWRITE | STGM_SHARE_EXCLUSIVE),
           "literal.stat-size-type-and-mode");
    Expect(CursorIs(stream.get(), 5), "literal.stat-preserves-cursor");

    move.QuadPart = 0;
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, &position), S_OK, "literal.seek-after-extend");
    actual.fill(0xCC);
    ExpectHr(stream->Read(actual.data(), static_cast<ULONG>(actual.size()), &count), S_OK, "literal.read-after-extend");
    // SetEndOfFile does not define newly extended bytes. Verify only the already written prefix.
    Expect(count == actual.size() && actual == overwritten, "literal.extend-preserves-written-prefix");

    move.QuadPart = 5;
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, &position), S_OK, "literal.seek-before-shrink");
    ULARGE_INTEGER smaller{};
    smaller.QuadPart = 4;
    ExpectHr(stream->SetSize(smaller), S_OK, "literal.resize-shrink");
    Expect(CursorIs(stream.get(), 5), "literal.shrink-preserves-cursor");
    ExpectHr(stream->Stat(&stat, STATFLAG_NONAME), S_OK, "literal.stat-shrunk");
    Expect(stat.type == STGTY_STREAM && stat.cbSize.QuadPart == 4, "literal.stat-shrunk-size");

    ExpectHr(stream->Commit(STGC_DEFAULT), S_OK, "literal.commit-flushes");
    ExpectHr(stream->CloseChecked(), S_OK, "literal.checked-close");
    ExpectHr(stream->CloseChecked(), STG_E_REVERTED, "literal.second-close-rejected");
    stream.reset();

    constexpr std::array<std::uint8_t, 4> finalBytes{'a', 'b', 'X', 'Y'};
    std::array<std::uint8_t, finalBytes.size()> disk{};
    Expect(ReadFileBytes(path, disk) && disk == finalBytes, "literal.final-disk-bytes");
}

void ClosedStreamRejectsOperations()
{
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "closed.temp-root-created");
    if (!scratch.IsCreated())
    {
        return;
    }

    const std::wstring path = scratch.File(L"closed.bin");
    wil::com_ptr_nothrow<CodecFileStream> stream;
    ExpectHr(CodecFileStream::Create(path.c_str(), 8, {}, stream.put()), S_OK, "closed.create");
    if (!stream)
    {
        return;
    }
    constexpr std::array<std::uint8_t, 2> bytes{'o', 'k'};
    ULONG count = 0;
    ExpectHr(stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &count), S_OK, "closed.write");
    ExpectHr(stream->Commit(STGC_DEFAULT), S_OK, "closed.commit-before-close");
    ExpectHr(stream->CloseChecked(), S_OK, "closed.close-checked");

    std::uint8_t byte = 0;
    LARGE_INTEGER move{};
    ULARGE_INTEGER size{};
    STATSTG stat{};
    ExpectHr(stream->Read(&byte, 1, &count), STG_E_REVERTED, "closed.read-rejected");
    Expect(count == 0, "closed.read-count-zero");
    ExpectHr(stream->Write(&byte, 1, &count), STG_E_REVERTED, "closed.write-rejected");
    Expect(count == 0, "closed.write-count-zero");
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, nullptr), STG_E_REVERTED, "closed.seek-rejected");
    ExpectHr(stream->SetSize(size), STG_E_REVERTED, "closed.resize-rejected");
    ExpectHr(stream->Commit(STGC_DEFAULT), STG_E_REVERTED, "closed.commit-rejected");
    ExpectHr(stream->Stat(&stat, STATFLAG_NONAME), STG_E_REVERTED, "closed.stat-rejected");
    stream.reset();
}

void CancellationStopsCreationAndMutation()
{
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "cancel.temp-root-created");
    if (!scratch.IsCreated())
    {
        return;
    }

    const std::wstring preCancelledPath = scratch.File(L"pre-cancelled.bin");
    std::stop_source alreadyStopped;
    (void)alreadyStopped.request_stop();
    wil::com_ptr_nothrow<CodecFileStream> preCancelled;
    ExpectHr(CodecFileStream::Create(preCancelledPath.c_str(), 8, CancelToken{alreadyStopped.get_token()},
                                     preCancelled.put()),
             E_ABORT, "cancel.create-pre-cancelled");
    Expect(!preCancelled && GetFileAttributesW(preCancelledPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
               GetLastError() == ERROR_FILE_NOT_FOUND,
           "cancel.pre-cancelled-create-leaves-no-file");

    const std::wstring path = scratch.File(L"cancelled.bin");
    std::stop_source source;
    wil::com_ptr_nothrow<CodecFileStream> stream;
    ExpectHr(CodecFileStream::Create(path.c_str(), 16, CancelToken{source.get_token()}, stream.put()), S_OK,
             "cancel.create");
    if (!stream)
    {
        return;
    }
    constexpr std::array<std::uint8_t, 4> before{'s', 'a', 'f', 'e'};
    ULONG count = 0;
    ExpectHr(stream->Write(before.data(), static_cast<ULONG>(before.size()), &count), S_OK, "cancel.initial-write");
    Expect(count == before.size(), "cancel.initial-write-count");
    (void)source.request_stop();

    constexpr std::array<std::uint8_t, 2> after{'n', 'o'};
    count = 99;
    ExpectHr(stream->Write(after.data(), static_cast<ULONG>(after.size()), &count), E_ABORT, "cancel.write-rejected");
    Expect(count == 0, "cancel.write-count-zero");
    ULARGE_INTEGER larger{};
    larger.QuadPart = 9;
    ExpectHr(stream->SetSize(larger), E_ABORT, "cancel.resize-rejected");
    ExpectHr(stream->Commit(STGC_DEFAULT), E_ABORT, "cancel.commit-rejected");
    ExpectHr(stream->CloseChecked(), E_ABORT, "cancel.checked-close-reports-uncommitted-cancel");

    stream.reset();
    std::array<std::uint8_t, before.size()> actual{};
    Expect(ReadFileBytes(path, actual) && actual == before, "cancel.existing-bytes-remain-unchanged");
}

void QueryInterfaceAndCreateNewCollision()
{
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "com.temp-root-created");
    if (!scratch.IsCreated())
    {
        return;
    }

    const std::wstring path = scratch.File(L"interfaces.bin");
    wil::com_ptr_nothrow<CodecFileStream> stream;
    ExpectHr(CodecFileStream::Create(path.c_str(), 16, {}, stream.put()), S_OK, "com.create");
    if (!stream)
    {
        return;
    }

    wil::com_ptr_nothrow<IUnknown> unknown;
    wil::com_ptr_nothrow<ISequentialStream> sequential;
    wil::com_ptr_nothrow<IStream> queriedStream;
    ExpectHr(stream->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(unknown.put())), S_OK, "com.query-iunknown");
    ExpectHr(stream->QueryInterface(IID_ISequentialStream, reinterpret_cast<void**>(sequential.put())), S_OK,
             "com.query-isequentialstream");
    ExpectHr(stream->QueryInterface(IID_IStream, reinterpret_cast<void**>(queriedStream.put())), S_OK,
             "com.query-istream");

    wil::com_ptr_nothrow<IUnknown> identityFromSequential;
    wil::com_ptr_nothrow<IUnknown> identityFromStream;
    if (sequential)
    {
        ExpectHr(sequential->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(identityFromSequential.put())), S_OK,
                 "com.sequential-iunknown-identity");
    }
    if (queriedStream)
    {
        ExpectHr(queriedStream->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(identityFromStream.put())), S_OK,
                 "com.stream-iunknown-identity");
    }
    Expect(unknown && identityFromSequential && identityFromStream && unknown.get() == identityFromSequential.get() &&
               unknown.get() == identityFromStream.get(),
           "com.all-interfaces-share-iunknown-identity");

    void* unsupported = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
    ExpectHr(stream->QueryInterface(IID_IDispatch, &unsupported), E_NOINTERFACE, "com.unsupported-iid");
    Expect(unsupported == nullptr, "com.unsupported-iid-clears-output");
    ExpectHr(stream->QueryInterface(IID_IStream, nullptr), E_POINTER, "com.null-query-output");

    const std::wstring collisionPath = scratch.File(L"unrelated.bin");
    constexpr std::array<std::uint8_t, 5> unrelated{'k', 'e', 'e', 'p', '!'};
    Expect(WriteFileBytes(collisionPath, unrelated), "com.create-collision-sentinel");
    wil::com_ptr_nothrow<CodecFileStream> collision;
    ExpectHr(CodecFileStream::Create(collisionPath.c_str(), 16, {}, collision.put()),
             HRESULT_FROM_WIN32(ERROR_FILE_EXISTS), "com.create-new-collision-rejected");
    Expect(!collision, "com.collision-returns-no-stream");
    std::array<std::uint8_t, unrelated.size()> preserved{};
    Expect(ReadFileBytes(collisionPath, preserved) && preserved == unrelated, "com.collision-preserves-unrelated-file");

    stream.reset();
    Expect(sequential && queriedStream && identityFromSequential && identityFromStream,
           "com.queried-references-own-stream");
    // Drop each queried owner through WIL; the implementation's references must keep the file stream alive.
    unknown.reset();
    identityFromSequential.reset();
    identityFromStream.reset();
    stream.reset();
    ExpectHr(queriedStream->Commit(STGC_DEFAULT), S_OK, "com.stream-reference-survives-original-release");
    queriedStream.reset();
    std::uint8_t byte = 0;
    ULONG count = 0;
    ExpectHr(sequential->Read(&byte, 1, &count), S_FALSE, "com.sequential-reference-survives-original-release");
    sequential.reset();
}

} // namespace

int RunCodecFileStreamTests()
{
    const int failuresBefore = gFailures;
    ByteCapRejectsWithoutChangingFile();
    LiteralWriteSeekOverwriteReadResizeAndStat();
    ClosedStreamRejectsOperations();
    CancellationStopsCreationAndMutation();
    QueryInterfaceAndCreateNewCollision();
    return gFailures - failuresBefore;
}
