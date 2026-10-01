#include "CodecMemoryStreamTests.h"

#include "CodecMemoryStream.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <stop_token>
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s codecmem.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void ExpectHr(HRESULT actual, HRESULT expected, const char* name) noexcept
{
    ++gAssertions;
    if (actual == expected)
    {
        std::printf("PASS codecmem.%s\n", name);
    }
    else
    {
        std::printf("FAIL codecmem.%s actual=0x%08lX expected=0x%08lX\n", name, static_cast<unsigned long>(actual),
                    static_cast<unsigned long>(expected));
        ++gFailures;
    }
}

[[nodiscard]] bool CursorIs(IStream* stream, std::uint64_t expected) noexcept
{
    LARGE_INTEGER move{};
    ULARGE_INTEGER position{};
    return SUCCEEDED(stream->Seek(move, STREAM_SEEK_CUR, &position)) && position.QuadPart == expected;
}

struct ObserverState final
{
    CodecMemoryStage selected = CodecMemoryStage::Reserve;
    std::uint64_t selectedCount = 0;
    CodecMemoryStage faultStage = CodecMemoryStage::Reserve;
    std::uint32_t faultCode = ERROR_SUCCESS;
    std::uint32_t faultCalls = 0;
    std::uint32_t faultOnCall = 1;
    std::uint32_t reachedCalls = 0;
    std::uint64_t lastCount = 0;
    std::stop_source* cancellation = nullptr;
};

void Reached(void* context, CodecMemoryStage stage, std::uint64_t count) noexcept
{
    auto& state = *static_cast<ObserverState*>(context);
    ++state.reachedCalls;
    state.lastCount = count;
    if (state.cancellation && stage == state.selected && count >= state.selectedCount)
        (void)state.cancellation->request_stop();
}

std::uint32_t Fault(void* context, CodecMemoryStage stage) noexcept
{
    auto& state = *static_cast<ObserverState*>(context);
    if (stage == state.faultStage)
    {
        ++state.faultCalls;
        return state.faultCalls >= state.faultOnCall ? state.faultCode : ERROR_SUCCESS;
    }
    return ERROR_SUCCESS;
}

[[nodiscard]] CodecMemoryObserver Observe(ObserverState& state) noexcept
{
    return {&state, Reached, Fault};
}

void LiteralWriteOverwriteReadAndEof()
{
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(32, {}, stream.put()), S_OK, "literal.create");
    if (!stream)
        return;

    constexpr std::array<std::uint8_t, 6> initial{'a', 'b', 'c', 'd', 'e', 'f'};
    ULONG count = 99;
    ExpectHr(stream->Write(initial.data(), static_cast<ULONG>(initial.size()), &count), S_OK, "literal.write");
    Expect(count == initial.size() && CursorIs(stream.get(), initial.size()), "literal.write-count-and-cursor");

    LARGE_INTEGER move{};
    move.QuadPart = 2;
    ULARGE_INTEGER position{};
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, &position), S_OK, "literal.seek-overwrite");
    Expect(position.QuadPart == 2, "literal.seek-position");
    constexpr std::array<std::uint8_t, 2> replacement{'X', 'Y'};
    ExpectHr(stream->Write(replacement.data(), static_cast<ULONG>(replacement.size()), &count), S_OK,
             "literal.overwrite");
    Expect(count == replacement.size() && CursorIs(stream.get(), 4), "literal.overwrite-count-and-cursor");

    move.QuadPart = 0;
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, nullptr), S_OK, "literal.rewind");
    std::array<std::uint8_t, 6> actual{};
    ExpectHr(stream->Read(actual.data(), static_cast<ULONG>(actual.size()), &count), S_OK, "literal.read");
    constexpr std::array<std::uint8_t, 6> expected{'a', 'b', 'X', 'Y', 'e', 'f'};
    Expect(count == expected.size() && actual == expected, "literal.exact-bytes");

    std::uint8_t eofByte = 0xCC;
    count = 99;
    ExpectHr(stream->Read(&eofByte, 1, &count), S_FALSE, "literal.eof-short-read");
    Expect(count == 0 && eofByte == 0xCC && CursorIs(stream.get(), expected.size()), "literal.eof-count-buffer-cursor");
}

void CapRejectsWithoutChangingPrefixCursorOrCommit()
{
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(4, {}, stream.put()), S_OK, "cap.create");
    if (!stream)
        return;
    constexpr std::array<std::uint8_t, 3> prefix{'o', 'k', '!'};
    ULONG written = 0;
    ExpectHr(stream->Write(prefix.data(), static_cast<ULONG>(prefix.size()), &written), S_OK, "cap.prefix-write");
    Expect(written == prefix.size(), "cap.prefix-count");
    constexpr std::uint8_t finalByte = '?';
    written = 0;
    ExpectHr(stream->Write(&finalByte, 1, &written), S_OK, "cap.write-exact-capacity");
    Expect(written == 1 && CursorIs(stream.get(), 4), "cap.exact-capacity-count-and-cursor");
    const auto committed = stream->CommittedBytes();
    constexpr std::uint8_t overflow = '!';
    written = 99;
    ExpectHr(stream->Write(&overflow, 1, &written), STG_E_MEDIUMFULL, "cap.overwrite-cap-rejected");
    Expect(stream->LimitExceeded(), "cap.overflow-sets-limit-flag");
    const auto bytes = stream->WrittenBytes();
    constexpr std::array<std::uint8_t, 4> exactPrefix{'o', 'k', '!', '?'};
    Expect(written == 0 && bytes.size() == exactPrefix.size() &&
               std::equal(bytes.begin(), bytes.end(), exactPrefix.begin()),
           "cap.unchanged-prefix-and-zero-count");
    Expect(CursorIs(stream.get(), exactPrefix.size()), "cap.unchanged-cursor");
    Expect(stream->CommittedBytes() == committed, "cap.unchanged-committed-bytes");
    LARGE_INTEGER rewind{};
    ExpectHr(stream->Seek(rewind, STREAM_SEEK_SET, nullptr), S_OK, "cap.valid-seek-after-overflow");
    Expect(stream->LimitExceeded(), "cap.overflow-flag-stays-set");
}

void LimitExceededIsStickyForCapacityFailures()
{
    wil::com_ptr_nothrow<CodecMemoryStream> resized;
    ExpectHr(CodecMemoryStream::Create(8, {}, resized.put()), S_OK, "limit.resize-create");
    if (!resized)
        return;
    ULARGE_INTEGER size{};
    size.QuadPart = 9;
    ExpectHr(resized->SetSize(size), STG_E_MEDIUMFULL, "limit.set-size-over-cap");
    Expect(resized->LimitExceeded(), "limit.set-size-sets-flag");
    size.QuadPart = 8;
    ExpectHr(resized->SetSize(size), S_OK, "limit.set-size-valid-after-over-cap");
    Expect(resized->LimitExceeded(), "limit.set-size-flag-stays-set");

    wil::com_ptr_nothrow<CodecMemoryStream> positiveSeek;
    ExpectHr(CodecMemoryStream::Create(8, {}, positiveSeek.put()), S_OK, "limit.positive-seek-create");
    if (!positiveSeek)
        return;
    LARGE_INTEGER move{};
    move.QuadPart = 9;
    ExpectHr(positiveSeek->Seek(move, STREAM_SEEK_SET, nullptr), STG_E_SEEKERROR, "limit.positive-seek-over-cap");
    Expect(positiveSeek->LimitExceeded(), "limit.positive-seek-sets-flag");
    move.QuadPart = 8;
    ExpectHr(positiveSeek->Seek(move, STREAM_SEEK_SET, nullptr), S_OK, "limit.positive-seek-valid-after-over-cap");
    Expect(positiveSeek->LimitExceeded(), "limit.positive-seek-flag-stays-set");

    wil::com_ptr_nothrow<CodecMemoryStream> negativeSeek;
    ExpectHr(CodecMemoryStream::Create(8, {}, negativeSeek.put()), S_OK, "limit.negative-seek-create");
    if (!negativeSeek)
        return;
    move.QuadPart = -1;
    ExpectHr(negativeSeek->Seek(move, STREAM_SEEK_SET, nullptr), STG_E_SEEKERROR, "limit.negative-seek-before-zero");
    Expect(!negativeSeek->LimitExceeded(), "limit.negative-seek-does-not-set-cap-flag");
}

void SeekValidationAndSparseGap()
{
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(16, {}, stream.put()), S_OK, "seek.create");
    if (!stream)
        return;
    constexpr std::array<std::uint8_t, 3> prefix{'A', 'B', 'C'};
    ULONG written = 0;
    ExpectHr(stream->Write(prefix.data(), static_cast<ULONG>(prefix.size()), &written), S_OK, "seek.prefix-write");
    LARGE_INTEGER move{};
    ULARGE_INTEGER position{};
    move.QuadPart = 1;
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, &position), S_OK, "seek.establish-cursor");

    move.QuadPart = (std::numeric_limits<std::int64_t>::min)();
    position.QuadPart = 0xA5A5;
    ExpectHr(stream->Seek(move, STREAM_SEEK_CUR, &position), STG_E_SEEKERROR, "seek.int64-min-rejected");
    Expect(position.QuadPart == 0xA5A5 && CursorIs(stream.get(), 1), "seek.min-preserves-output-and-cursor");
    move.QuadPart = (std::numeric_limits<std::int64_t>::max)();
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, &position), STG_E_SEEKERROR, "seek.int64-max-rejected");
    Expect(CursorIs(stream.get(), 1), "seek.max-preserves-cursor");
    ExpectHr(stream->Seek({}, 0xFFFFFFFFu, &position), STG_E_INVALIDFUNCTION, "seek.unknown-origin-rejected");
    Expect(CursorIs(stream.get(), 1), "seek.unknown-origin-preserves-cursor");

    move.QuadPart = 9;
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, &position), S_OK, "seek.beyond-logical-size");
    Expect(position.QuadPart == 9, "seek.beyond-logical-position");
    constexpr std::array<std::uint8_t, 2> suffix{'Y', 'Z'};
    ExpectHr(stream->Write(suffix.data(), static_cast<ULONG>(suffix.size()), &written), S_OK, "seek.write-after-gap");
    Expect(written == suffix.size() && CursorIs(stream.get(), 11), "seek.gap-write-count-and-cursor");
    const auto bytes = stream->WrittenBytes();
    constexpr std::array<std::uint8_t, 11> expected{'A', 'B', 'C', 0, 0, 0, 0, 0, 0, 'Y', 'Z'};
    Expect(bytes.size() == expected.size() && std::equal(bytes.begin(), bytes.end(), expected.begin()),
           "seek.zero-filled-logical-gap");
}

void ShrinkRegrowAndNullBuffers()
{
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(16, {}, stream.put()), S_OK, "resize.create");
    if (!stream)
        return;
    constexpr std::array<std::uint8_t, 6> original{'a', 'b', 'c', 'd', 'e', 'f'};
    ULONG written = 0;
    ExpectHr(stream->Write(original.data(), static_cast<ULONG>(original.size()), &written), S_OK, "resize.write");
    LARGE_INTEGER move{};
    move.QuadPart = 8;
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, nullptr), S_OK, "resize.cursor-outside-size");
    ULARGE_INTEGER size{};
    size.QuadPart = 2;
    ExpectHr(stream->SetSize(size), S_OK, "resize.shrink");
    Expect(CursorIs(stream.get(), 8), "resize.shrink-preserves-cursor");
    size.QuadPart = 6;
    ExpectHr(stream->SetSize(size), S_OK, "resize.regrow");
    Expect(CursorIs(stream.get(), 8), "resize.regrow-preserves-cursor");
    constexpr std::array<std::uint8_t, 6> expected{'a', 'b', 0, 0, 0, 0};
    const auto bytes = stream->WrittenBytes();
    Expect(bytes.size() == expected.size() && std::equal(bytes.begin(), bytes.end(), expected.begin()),
           "resize.regrown-bytes-zero-and-prefix-preserved");

    ExpectHr(stream->Write(nullptr, 0, &written), S_OK, "buffer.zero-write-null-accepted");
    Expect(written == 0 && CursorIs(stream.get(), 8), "buffer.zero-write-null-keeps-cursor");
    written = 99;
    ExpectHr(stream->Write(nullptr, 1, &written), E_POINTER, "buffer.nonzero-write-null-rejected");
    Expect(written == 0 && CursorIs(stream.get(), 8), "buffer.invalid-write-keeps-cursor");
    ULONG read = 99;
    ExpectHr(stream->Read(nullptr, 0, &read), S_OK, "buffer.zero-read-null-accepted");
    Expect(read == 0 && CursorIs(stream.get(), 8), "buffer.zero-read-null-keeps-cursor");
    ExpectHr(stream->Read(nullptr, 1, &read), E_POINTER, "buffer.nonzero-read-null-rejected");
    Expect(read == 0 && CursorIs(stream.get(), 8), "buffer.invalid-read-keeps-cursor");
}

void FactoryValidationAndAllocationFaults()
{
    ExpectHr(CodecMemoryStream::Create(1, {}, nullptr), E_POINTER, "factory.null-output");
    constexpr std::array<std::uint64_t, 3> invalidCaps{0, CodecMemoryStream::kMaximumBytes + 1,
                                                       (std::numeric_limits<std::uint64_t>::max)()};
    for (std::size_t index = 0; index < invalidCaps.size(); ++index)
    {
        CodecMemoryStream* raw = reinterpret_cast<CodecMemoryStream*>(static_cast<std::uintptr_t>(1));
        const HRESULT result = CodecMemoryStream::Create(invalidCaps[index], {}, &raw);
        ExpectHr(result, E_INVALIDARG,
                 index == 0   ? "factory.zero-cap"
                 : index == 1 ? "factory.over-512m-cap"
                              : "factory.uint64-max-cap");
        Expect(raw == nullptr, index == 0   ? "factory.zero-cap-clears-output"
                               : index == 1 ? "factory.over-cap-clears-output"
                                            : "factory.max-cap-clears-output");
    }

    ObserverState reserveFailure{};
    reserveFailure.faultStage = CodecMemoryStage::Reserve;
    reserveFailure.faultCode = ERROR_NOT_ENOUGH_MEMORY;
    wil::com_ptr_nothrow<CodecMemoryStream> noReserve;
    ExpectHr(CodecMemoryStream::Create(32, {}, noReserve.put(), Observe(reserveFailure)),
             HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY), "fault.reserve-exact-error");
    Expect(reserveFailure.faultCalls == 1 && !noReserve, "fault.reserve-reached-and-output-empty");

    ObserverState commitFailure{};
    commitFailure.faultStage = CodecMemoryStage::Commit;
    commitFailure.faultOnCall = 2;
    commitFailure.faultCode = ERROR_NOT_ENOUGH_MEMORY;
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(131073, {}, stream.put(), Observe(commitFailure)), S_OK, "fault.commit-create");
    if (!stream)
        return;
    commitFailure.faultCalls = 0;
    // The first 4 KiB page is committed before a later write requests a larger commit range.
    commitFailure.faultStage = CodecMemoryStage::Commit;
    constexpr std::uint8_t first = 0x5A;
    ULONG written = 0;
    ExpectHr(stream->Write(&first, 1, &written), S_OK, "fault.commit-prefix-write");
    const auto beforeCommitted = stream->CommittedBytes();
    const auto beforeBytes = stream->WrittenBytes();
    LARGE_INTEGER move{};
    move.QuadPart = 65536;
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, nullptr), S_OK, "fault.commit-position-next-band");
    constexpr std::uint8_t second = 0xA5;
    written = 99;
    ExpectHr(stream->Write(&second, 1, &written), HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY),
             "fault.commit-exact-error");
    Expect(commitFailure.faultCalls == 2, "fault.commit-reached-after-successful-prior-commit");
    Expect(written == 0 && stream->CommittedBytes() == beforeCommitted, "fault.commit-count-and-commit-unchanged");
    Expect(CursorIs(stream.get(), 65536), "fault.commit-cursor-unchanged");
    const auto afterBytes = stream->WrittenBytes();
    Expect(afterBytes.size() == beforeBytes.size() && afterBytes.size() == 1 && afterBytes[0] == 0x5A,
           "fault.commit-content-unchanged");
}

void CancellationAtReserveAndCommit()
{
    std::stop_source preCancelledSource;
    (void)preCancelledSource.request_stop();
    wil::com_ptr_nothrow<CodecMemoryStream> preCancelled;
    ExpectHr(CodecMemoryStream::Create(8, CancelToken{preCancelledSource.get_token()}, preCancelled.put()), E_ABORT,
             "cancel.pre-cancelled-create");
    Expect(!preCancelled, "cancel.pre-cancelled-create-clears-output");

    std::stop_source reserveStop;
    ObserverState reserve{};
    reserve.selected = CodecMemoryStage::Reserve;
    reserve.selectedCount = 1;
    reserve.cancellation = &reserveStop;
    wil::com_ptr_nothrow<CodecMemoryStream> noStream;
    ExpectHr(CodecMemoryStream::Create(8, CancelToken{reserveStop.get_token()}, noStream.put(), Observe(reserve)),
             E_ABORT, "cancel.reserve-aborts-create");
    Expect(reserve.reachedCalls == 1 && !noStream, "cancel.reserve-reached-and-clears-output");

    SYSTEM_INFO pageInfo{};
    GetSystemInfo(&pageInfo);
    std::stop_source commitStop;
    ObserverState commit{};
    commit.selected = CodecMemoryStage::Commit;
    commit.selectedCount = 1;
    commit.cancellation = &commitStop;
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(131073, CancelToken{commitStop.get_token()}, stream.put(), Observe(commit)),
             S_OK, "cancel.commit-create");
    if (!stream)
        return;
    constexpr std::uint8_t byte = 7;
    ULONG written = 99;
    ExpectHr(stream->Write(&byte, 1, &written), E_ABORT, "cancel.commit-aborts-write");
    Expect(commit.reachedCalls == 2 && commit.lastCount == pageInfo.dwPageSize, "cancel.commit-stage-reached");
    const auto bytes = stream->WrittenBytes();
    Expect(written == 0 && bytes.empty(), "cancel.commit-no-logical-content-or-count");
    Expect(stream->CommittedBytes() == pageInfo.dwPageSize, "cancel.commit-reports-reached-commit");
}

void PreCancelledOperationsLeaveLogicalStateUnchanged()
{
    std::stop_source source;
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(16, CancelToken{source.get_token()}, stream.put()), S_OK,
             "cancel.pre-op-create");
    if (!stream)
        return;
    constexpr std::array<std::uint8_t, 4> original{0x10, 0x20, 0x30, 0x40};
    ULONG count = 0;
    ExpectHr(stream->Write(original.data(), static_cast<ULONG>(original.size()), &count), S_OK,
             "cancel.pre-op-fixture-write");
    LARGE_INTEGER position{};
    position.QuadPart = 1;
    ExpectHr(stream->Seek(position, STREAM_SEEK_SET, nullptr), S_OK, "cancel.pre-op-fixture-position");
    (void)source.request_stop();

    constexpr std::uint8_t replacement = 0xEE;
    ULONG written = 99;
    ExpectHr(stream->Write(&replacement, 1, &written), E_ABORT, "cancel.pre-op-write-aborts");
    Expect(written == 0, "cancel.pre-op-write-zero-count");
    std::array<std::uint8_t, 4> output{};
    output.fill(0xCC);
    ULONG received = 99;
    ExpectHr(stream->Read(output.data(), static_cast<ULONG>(output.size()), &received), E_ABORT,
             "cancel.pre-op-read-aborts");
    Expect(received == 0 && std::all_of(output.begin(), output.end(), [](std::uint8_t byte) { return byte == 0xCC; }),
           "cancel.pre-op-read-zero-count-and-untouched-buffer");
    ULARGE_INTEGER size{};
    size.QuadPart = 1;
    ExpectHr(stream->SetSize(size), E_ABORT, "cancel.pre-op-resize-aborts");
    ExpectHr(stream->Commit(STGC_DEFAULT), E_ABORT, "cancel.pre-op-commit-aborts");
    const auto bytes = stream->WrittenBytes();
    Expect(bytes.size() == original.size() && std::equal(bytes.begin(), bytes.end(), original.begin()),
           "cancel.pre-op-preserves-content-and-logical-size");
}

void CancellationAtZeroBand()
{
    std::stop_source stop;
    ObserverState observer{};
    observer.selected = CodecMemoryStage::ZeroBand;
    // ZeroBand reports absolute end positions; the first band ends at 1 + 65536.
    observer.selectedCount = 65537;
    observer.cancellation = &stop;
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(131073, CancelToken{stop.get_token()}, stream.put(), Observe(observer)), S_OK,
             "cancel.zero-create");
    if (!stream)
        return;
    std::array<std::uint8_t, 131073> pattern{};
    pattern.fill(0x6D);
    ULONG written = 0;
    ExpectHr(stream->Write(pattern.data(), static_cast<ULONG>(pattern.size()), &written), S_OK,
             "cancel.zero-fixture-write");
    LARGE_INTEGER move{};
    move.QuadPart = 7;
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, nullptr), S_OK, "cancel.zero-fixture-cursor");
    ULARGE_INTEGER size{};
    size.QuadPart = 1;
    ExpectHr(stream->SetSize(size), S_OK, "cancel.zero-fixture-shrink");
    size.QuadPart = 131073;
    ExpectHr(stream->SetSize(size), E_ABORT, "cancel.zero-band-aborts-regrow");
    Expect(observer.lastCount == 65537 && observer.reachedCalls >= 1, "cancel.zero-band-reached");
    const auto bytes = stream->WrittenBytes();
    Expect(bytes.size() == 1 && bytes[0] == 0x6D, "cancel.zero-keeps-logical-content-and-size");
    Expect(stream->CommittedBytes() >= 131073, "cancel.zero-keeps-prior-commit");
}

void CancellationStopsWriteAndReadAfterOne64KiBBand()
{
    constexpr std::size_t byteCount = 131073;
    std::array<std::uint8_t, byteCount> input{};
    // Fixture generation must run on the baseline ISA, independently of the machine running this test.
#pragma loop(no_vector)
    for (std::size_t index = 0; index < input.size(); ++index)
        input[index] = static_cast<std::uint8_t>((index * 29u + 17u) & 0xFFu);

    std::stop_source writeStop;
    ObserverState writeObserver{};
    writeObserver.selected = CodecMemoryStage::WriteBand;
    writeObserver.selectedCount = 65536;
    writeObserver.cancellation = &writeStop;
    wil::com_ptr_nothrow<CodecMemoryStream> writeStream;
    ExpectHr(CodecMemoryStream::Create(byteCount, CancelToken{writeStop.get_token()}, writeStream.put(),
                                       Observe(writeObserver)),
             S_OK, "cancel.write-create");
    if (!writeStream)
        return;
    ULONG written = 99;
    ExpectHr(writeStream->Write(input.data(), static_cast<ULONG>(input.size()), &written), E_ABORT,
             "cancel.write-band-aborts");
    const auto partial = writeStream->WrittenBytes();
    Expect(writeObserver.lastCount == 65536 && written == 65536, "cancel.write-exact-partial-count");
    Expect(partial.size() == 65536 && std::equal(partial.begin(), partial.end(), input.begin()),
           "cancel.write-partial-prefix-and-cursor");

    LARGE_INTEGER rewind{};
    std::stop_source readStop;
    ObserverState readObserver{};
    readObserver.selected = CodecMemoryStage::ReadBand;
    readObserver.selectedCount = 65536;
    readObserver.cancellation = &readStop;
    // The observer is fixed at construction; use a second stream whose fixture is written before observing reads.
    wil::com_ptr_nothrow<CodecMemoryStream> observedRead;
    ExpectHr(CodecMemoryStream::Create(byteCount, CancelToken{readStop.get_token()}, observedRead.put(),
                                       Observe(readObserver)),
             S_OK, "cancel.read-create");
    if (!observedRead)
        return;
    ExpectHr(observedRead->Write(input.data(), static_cast<ULONG>(input.size()), &written), S_OK,
             "cancel.read-observed-fixture-write");
    ExpectHr(observedRead->Seek(rewind, STREAM_SEEK_SET, nullptr), S_OK, "cancel.read-observed-fixture-rewind");
    std::array<std::uint8_t, byteCount> output{};
    output.fill(0xCC);
    ULONG received = 99;
    ExpectHr(observedRead->Read(output.data(), static_cast<ULONG>(output.size()), &received), E_ABORT,
             "cancel.read-band-aborts");
    Expect(readObserver.lastCount == 65536 && received == 65536, "cancel.read-exact-partial-count");
    Expect(std::equal(output.begin(), output.begin() + 65536, input.begin()) && output[65536] == 0xCC,
           "cancel.read-partial-bytes-and-cursor");
}

void TinyCapBoundedZeroWriteReadAndComLifetime()
{
    constexpr std::uint64_t capacity = 131073;
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(capacity, {}, stream.put()), S_OK, "bounded.create");
    if (!stream)
        return;
    LARGE_INTEGER move{};
    move.QuadPart = static_cast<std::int64_t>(capacity - 3);
    ExpectHr(stream->Seek(move, STREAM_SEEK_SET, nullptr), S_OK, "bounded.seek-near-cap");
    constexpr std::array<std::uint8_t, 3> tail{0x11, 0x22, 0x33};
    ULONG count = 0;
    ExpectHr(stream->Write(tail.data(), static_cast<ULONG>(tail.size()), &count), S_OK, "bounded.write-tail");
    Expect(count == tail.size() && stream->CommittedBytes() >= capacity && stream->CommittedBytes() <= capacity + 65536,
           "bounded.capacity-commit-and-write-count");
    const auto written = stream->WrittenBytes();
    const bool gapIsZero = written.size() == capacity && std::all_of(written.begin(), written.end() - 3,
                                                                     [](std::uint8_t value) { return value == 0; });
    Expect(gapIsZero && std::equal(written.end() - 3, written.end(), tail.begin()), "bounded.zero-gap-and-tail");

    LARGE_INTEGER rewind{};
    ExpectHr(stream->Seek(rewind, STREAM_SEEK_SET, nullptr), S_OK, "bounded.rewind");
    std::array<std::uint8_t, capacity> actual{};
    ULONG received = 0;
    ExpectHr(stream->Read(actual.data(), static_cast<ULONG>(actual.size()), &received), S_OK, "bounded.read-all");
    Expect(received == capacity && actual[capacity - 3] == tail[0] && actual[capacity - 2] == tail[1] &&
               actual[capacity - 1] == tail[2] &&
               std::all_of(actual.begin(), actual.end() - 3, [](std::uint8_t value) { return value == 0; }),
           "bounded.read-all-exact-data");

    wil::com_ptr_nothrow<IStream> retained;
    ExpectHr(stream->QueryInterface(IID_IStream, reinterpret_cast<void**>(retained.put())), S_OK, "com.query-stream");
    if (!retained)
        return;
    wil::com_ptr_nothrow<IUnknown> identity;
    wil::com_ptr_nothrow<IUnknown> identityFromStream;
    ExpectHr(stream->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(identity.put())), S_OK, "com.query-unknown");
    if (retained)
        ExpectHr(retained->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(identityFromStream.put())), S_OK,
                 "com.stream-query-unknown");
    Expect(identity && identityFromStream && identity.get() == identityFromStream.get(), "com.iunknown-identity");

    STATSTG stat{};
    ExpectHr(stream->Stat(nullptr, STATFLAG_NONAME), E_POINTER, "com.stat-null-output");
    ExpectHr(stream->Stat(&stat, STATFLAG_NONAME), S_OK, "com.stat");
    Expect(stat.type == STGTY_STREAM && stat.cbSize.QuadPart == capacity && stat.grfMode == STGM_READWRITE,
           "com.stat-type-size-mode");
    ULARGE_INTEGER copiedRead{};
    ULARGE_INTEGER copiedWritten{};
    copiedRead.QuadPart = 0xA5A5;
    copiedWritten.QuadPart = 0xA5A5;
    ExpectHr(stream->CopyTo(nullptr, {}, &copiedRead, &copiedWritten), E_NOTIMPL, "com.copyto-unsupported");
    Expect(copiedRead.QuadPart == 0 && copiedWritten.QuadPart == 0, "com.copyto-clears-counts");
    IStream* cloneRaw = reinterpret_cast<IStream*>(static_cast<std::uintptr_t>(1));
    ExpectHr(stream->Clone(nullptr), E_POINTER, "com.clone-null-output");
    ExpectHr(stream->Clone(&cloneRaw), E_NOTIMPL, "com.clone-unsupported");
    Expect(cloneRaw == nullptr, "com.clone-clears-output");

    const auto* retainedConcrete = static_cast<CodecMemoryStream*>(retained.get());
    const auto retainedBytes = retainedConcrete->WrittenBytes();
    stream.reset();
    Expect(retainedBytes.size() == capacity && retainedBytes[capacity - 3] == tail[0] &&
               retainedBytes[capacity - 2] == tail[1] && retainedBytes[capacity - 1] == tail[2],
           "com.retained-reference-keeps-byte-view-alive");
    identity.reset();
    identityFromStream.reset();
    retained.reset();
}

struct CommitTrace final
{
    std::uint64_t last = 0;
    unsigned int commits = 0;
    unsigned int zeroBands = 0;
    bool bounded = true;
};
void TraceCommit(void* context, CodecMemoryStage stage, std::uint64_t count) noexcept
{
    auto& trace = *static_cast<CommitTrace*>(context);
    if (stage == CodecMemoryStage::Commit)
    {
        trace.bounded = trace.bounded && count > trace.last && count - trace.last <= 65536;
        trace.last = count;
        ++trace.commits;
    }
    if (stage == CodecMemoryStage::ZeroBand)
        ++trace.zeroBands;
}
void CommitGrowthIsBoundedAndNewPagesAreZero()
{
    constexpr std::uint64_t capacity = 131073;
    CommitTrace trace{};
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(capacity, {}, stream.put(), {&trace, TraceCommit, nullptr}), S_OK,
             "commit-bands.create");
    if (!stream)
        return;
    ULARGE_INTEGER size{};
    size.QuadPart = capacity;
    ExpectHr(stream->SetSize(size), S_OK, "commit-bands.grow");
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto expected = ((capacity + info.dwPageSize - 1) / info.dwPageSize) * info.dwPageSize;
    Expect(trace.commits == 3 && trace.bounded && trace.last == expected && stream->CommittedBytes() == expected,
           "commit-bands.exact-os-page-rounding-and-three-bounded-commits");
    const auto bytes = stream->WrittenBytes();
    Expect(bytes.size() == capacity &&
               std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t byte) { return byte == 0; }),
           "commit-bands.new-pages-visible-zero");
    Expect(trace.zeroBands == 0 && CursorIs(stream.get(), 0), "commit-bands.no-redundant-zero-pass-or-cursor-change");

    std::stop_source source;
    ObserverState cancellation{};
    cancellation.selected = CodecMemoryStage::Commit;
    cancellation.selectedCount = 131072;
    cancellation.cancellation = &source;
    wil::com_ptr_nothrow<CodecMemoryStream> cancelled;
    ExpectHr(
        CodecMemoryStream::Create(capacity, CancelToken{source.get_token()}, cancelled.put(), Observe(cancellation)),
        S_OK, "commit-bands.cancel-create");
    if (!cancelled)
        return;
    ExpectHr(cancelled->SetSize(size), E_ABORT, "commit-bands.stop-after-second-commit");
    Expect(cancelled->CommittedBytes() == 131072 && cancelled->WrittenBytes().empty() && cancellation.reachedCalls == 3,
           "commit-bands.stop-no-third-commit-or-logical-output");

    ObserverState failure{};
    failure.faultStage = CodecMemoryStage::Commit;
    failure.faultOnCall = 3;
    failure.faultCode = ERROR_DISK_FULL;
    wil::com_ptr_nothrow<CodecMemoryStream> faulted;
    ExpectHr(CodecMemoryStream::Create(capacity, {}, faulted.put(), Observe(failure)), S_OK,
             "commit-bands.fault-create");
    if (!faulted)
        return;
    ExpectHr(faulted->SetSize(size), HRESULT_FROM_WIN32(ERROR_DISK_FULL), "commit-bands.third-commit-exact-error");
    Expect(failure.faultCalls == 3 && faulted->CommittedBytes() == 131072 && faulted->WrittenBytes().empty() &&
               CursorIs(faulted.get(), 0),
           "commit-bands.partial-commit-retained-no-logical-output");
}
void UnsupportedOperations()
{
    wil::com_ptr_nothrow<CodecMemoryStream> stream;
    ExpectHr(CodecMemoryStream::Create(8, {}, stream.put()), S_OK, "unsupported.create");
    if (!stream)
        return;
    ExpectHr(stream->Revert(), STG_E_INVALIDFUNCTION, "unsupported.revert");
    ExpectHr(stream->LockRegion({}, {}, 0), STG_E_INVALIDFUNCTION, "unsupported.lock-region");
    ExpectHr(stream->UnlockRegion({}, {}, 0), STG_E_INVALIDFUNCTION, "unsupported.unlock-region");
    void* unsupported = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
    ExpectHr(stream->QueryInterface(IID_IStream, nullptr), E_POINTER, "unsupported.query-null-output");
    ExpectHr(stream->QueryInterface(IID_IDispatch, &unsupported), E_NOINTERFACE, "unsupported.query-interface");
    Expect(unsupported == nullptr, "unsupported.query-clears-output");
}
} // namespace

int RunCodecMemoryStreamTests()
{
    const int failuresBefore = gFailures;
    const int assertionsBefore = gAssertions;
    LiteralWriteOverwriteReadAndEof();
    CapRejectsWithoutChangingPrefixCursorOrCommit();
    LimitExceededIsStickyForCapacityFailures();
    SeekValidationAndSparseGap();
    ShrinkRegrowAndNullBuffers();
    FactoryValidationAndAllocationFaults();
    CancellationAtReserveAndCommit();
    PreCancelledOperationsLeaveLogicalStateUnchanged();
    CancellationAtZeroBand();
    CancellationStopsWriteAndReadAfterOne64KiBBand();
    TinyCapBoundedZeroWriteReadAndComLifetime();
    CommitGrowthIsBoundedAndNewPagesAreZero();
    UnsupportedOperations();
    std::printf("ASSERTIONS codecmem.total=%d failures=%d\n", gAssertions - assertionsBefore,
                gFailures - failuresBefore);
    return gFailures - failuresBefore;
}
