#include "ClipboardBindingTransferTests.h"

#include "ClipboardFingerprint.h"
#include "ClipboardReceipt.h"
#include "ClipboardTransfer.h"
#include "SystemClipboard.h"

#include <Windows.h>
#include <ole2.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
// Fixture-only 100 ms event waits match the existing approximately three-second harness bound.
constexpr unsigned kFixtureAttempts = static_cast<unsigned>(SystemClipboard::kHarnessAttempts / 10);
static_assert(kFixtureAttempts >= 4);
using ClipboardTransfer::Fault;
using ClipboardTransfer::ImageCopy;
using ClipboardTransfer::Inspection;
using ClipboardTransfer::PreparedImage;
using ClipboardTransfer::Publication;
using ClipboardTransfer::Stage;
using ClipboardTransfer::Status;

constexpr std::array<std::uint8_t, 8> kPixels{10, 20, 30, 255, 90, 80, 70, 255};
constexpr std::array<std::uint8_t, 8> kChangedPixels{11, 20, 30, 255, 90, 80, 70, 255};

UINT ReceiptFormat() noexcept
{
    return RegisterClipboardFormatW(L"RedPrism.ClipboardReceipt.v1");
}

constexpr std::array<std::uint8_t, 132> LiteralDib(std::array<std::uint8_t, 8> pixels) noexcept
{
    std::array<std::uint8_t, 132> bytes{};
    bytes[0] = 124;
    bytes[4] = 2;
    bytes[8] = 1;
    bytes[12] = 1;
    bytes[14] = 32;
    bytes[16] = 3;
    bytes[20] = 8;
    bytes[42] = 255;
    bytes[45] = 255;
    bytes[48] = 255;
    bytes[55] = 255;
    bytes[56] = 'B';
    bytes[57] = 'G';
    bytes[58] = 'R';
    bytes[59] = 's';
    bytes[108] = 4;
    for (std::size_t i = 0; i < pixels.size(); ++i)
        bytes[124 + i] = pixels[i];
    return bytes;
}

constexpr auto kLiteralDib = LiteralDib(kPixels);

int gFailures = 0;
unsigned gAssertions = 0;

void Expect(bool condition, const char* fact) noexcept
{
    ++gAssertions;
    std::printf("%s clipboardbinding.%s\n", condition ? "PASS" : "FAIL", fact);
    gFailures += condition ? 0 : 1;
}

void ExpectPublication(bool condition, const char* fact, const Publication& publication) noexcept
{
    Expect(condition, fact);
    if (!condition)
        std::printf("clipboardbinding.publication status=%u nativeError=%lu sequence=%lu emptied=%u pngPlaced=%u "
                    "dibPlaced=%u receiptPlaced=%u\n",
                    static_cast<unsigned>(publication.status), static_cast<unsigned long>(publication.nativeError),
                    static_cast<unsigned long>(publication.sequence), publication.emptied ? 1u : 0u,
                    publication.pngPlaced ? 1u : 0u, publication.dibPlaced ? 1u : 0u,
                    publication.receiptPlaced ? 1u : 0u);
}

ClipboardReceipt::Key TestKey() noexcept
{
    ClipboardReceipt::Key key{};
    for (std::size_t i = 0; i < key.session.size(); ++i)
        key.session[i] = static_cast<std::uint8_t>(i + 1);
    key.serial = 0x1020304050607080ULL;
    return key;
}

Raster Source()
{
    return Raster::Make(2, 1, RasterFormat::Bgra8888, kPixels.data(), kPixels.size());
}

bool WaitOneShot(HANDLE timer) noexcept
{
    LARGE_INTEGER due{};
    due.QuadPart = -1'000'000; // 100 ms, absolute bound is controlled by the caller's attempt count.
    return SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != FALSE &&
           WaitForSingleObject(timer, 200) == WAIT_OBJECT_0;
}

bool OpenClipboardBounded(HWND owner) noexcept
{
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return false;
    for (unsigned attempt = 0; attempt < kFixtureAttempts; ++attempt)
    {
        if (OpenClipboard(owner))
            return true;
        if (attempt + 1 == kFixtureAttempts || !WaitOneShot(timer.get()))
            return false;
    }
    return false;
}

struct InspectDiagnostic final
{
    unsigned attempts = 0;
    Status status = Status::NativeFailure;
    DWORD nativeError = ERROR_SUCCESS;
    DWORD sequenceAfterCall = 0;
    DWORD busyError = 0, busyProcess = 0;
};

void DumpClipboardFormats(HWND owner, const char* label, DWORD inspectError, DWORD heldSequence,
                          DWORD inspectedSequence) noexcept
{
    const DWORD sequenceBefore = GetClipboardSequenceNumber();
    if (!OpenClipboard(owner))
    {
        const DWORD openError = GetLastError();
        const HWND opener = GetOpenClipboardWindow();
        DWORD openerProcess = 0;
        const DWORD openerThread = opener ? GetWindowThreadProcessId(opener, &openerProcess) : 0;
        std::printf("clipboard-diagnostic openerProcess=%lu openerThread=%lu callerProcess=%lu\n", openerProcess,
                    openerThread, GetCurrentProcessId());
        std::printf("clipboardbinding.diag %s inspectError=%lu heldSequence=%lu inspectedSequence=%lu "
                    "sequenceBefore=%lu open=0 openError=%lu formats=<unavailable>\n",
                    label, static_cast<unsigned long>(inspectError), static_cast<unsigned long>(heldSequence),
                    static_cast<unsigned long>(inspectedSequence), static_cast<unsigned long>(sequenceBefore),
                    static_cast<unsigned long>(openError));
        return;
    }

    bool closed = false;
    DWORD closeError = ERROR_SUCCESS;
    auto close = wil::scope_exit(
        [&]() noexcept
        {
            SetLastError(ERROR_SUCCESS);
            closed = CloseClipboard() != FALSE;
            closeError = closed ? ERROR_SUCCESS : GetLastError();
        });
    const DWORD heldNow = GetClipboardSequenceNumber();
    std::array<UINT, 32> formats{};
    unsigned count = 0;
    DWORD enumError = ERROR_SUCCESS;
    UINT format = 0;
    for (;;)
    {
        SetLastError(ERROR_SUCCESS);
        format = EnumClipboardFormats(format);
        enumError = GetLastError();
        if (!format || count == formats.size())
            break;
        formats[count++] = format;
    }
    const DWORD sequenceAfterEnum = GetClipboardSequenceNumber();
    close.reset();
    const DWORD sequenceAfterClose = GetClipboardSequenceNumber();
    std::printf("clipboardbinding.diag %s inspectError=%lu heldSequence=%lu inspectedSequence=%lu "
                "sequenceBefore=%lu heldNow=%lu sequenceAfterEnum=%lu sequenceAfterClose=%lu "
                "enumError=%lu enumTruncated=%u close=%u closeError=%lu formats=",
                label, static_cast<unsigned long>(inspectError), static_cast<unsigned long>(heldSequence),
                static_cast<unsigned long>(inspectedSequence), static_cast<unsigned long>(sequenceBefore),
                static_cast<unsigned long>(heldNow), static_cast<unsigned long>(sequenceAfterEnum),
                static_cast<unsigned long>(sequenceAfterClose), static_cast<unsigned long>(enumError),
                count == formats.size() ? 1u : 0u, closed ? 1u : 0u, static_cast<unsigned long>(closeError));
    for (unsigned i = 0; i < count; ++i)
        std::printf("%s%u", i ? "," : "", static_cast<unsigned>(formats[i]));
    std::printf("\n");
}

bool SetGlobal(UINT format, std::span<const std::uint8_t> bytes) noexcept
{
    wil::unique_hglobal block(GlobalAlloc(GMEM_MOVEABLE, bytes.size()));
    if (!block)
        return false;
    {
        const wil::unique_hglobal_locked locked(block.get());
        if (!locked.get())
            return false;
        std::memcpy(locked.get(), bytes.data(), bytes.size());
    }
    if (!SetClipboardData(format, block.get()))
        return false;
    (void)block.release();
    return true;
}

bool PutClipboard(HWND owner, std::span<const std::uint8_t> receipt, std::span<const std::uint8_t> png,
                  std::span<const std::uint8_t> dib) noexcept
{
    if (!OpenClipboardBounded(owner))
        return false;
    const auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
    if (!EmptyClipboard())
        return false;
    if (!receipt.empty() && !SetGlobal(ReceiptFormat(), receipt))
        return false;
    if (!png.empty() && !SetGlobal(RegisterClipboardFormatW(L"PNG"), png))
        return false;
    if (!dib.empty() && !SetGlobal(CF_DIBV5, dib))
        return false;
    return true;
}

struct Observation final
{
    Fault inject = static_cast<Fault>(255);
    Fault inject2 = static_cast<Fault>(255);
    DWORD injectedError = ERROR_GEN_FAILURE;
    Stage cancelAt = static_cast<Stage>(255);
    CancelSource* cancel = nullptr;
    DWORD lastMutationSequence = 0;
    DWORD heldSequence = 0;
    std::array<unsigned, static_cast<std::size_t>(Fault::ReceiptRead) + 1> faultCalls{};
    std::array<unsigned, static_cast<std::size_t>(Stage::Copied) + 1> reached{};
};

void Reached(void* raw, Stage stage) noexcept
{
    auto& observed = *static_cast<Observation*>(raw);
    const auto index = static_cast<std::size_t>(stage);
    if (index < observed.reached.size())
        ++observed.reached[index];
    if (stage == Stage::Opened)
        observed.heldSequence = GetClipboardSequenceNumber();
    if (stage == Stage::Emptied || stage == Stage::ReceiptPlaced || stage == Stage::PngPlaced ||
        stage == Stage::DibPlaced)
        observed.lastMutationSequence = GetClipboardSequenceNumber();
    if (stage == observed.cancelAt && observed.cancel)
        (void)observed.cancel->Cancel();
}

std::uint32_t FaultCall(void* raw, Fault fault) noexcept
{
    auto& observed = *static_cast<Observation*>(raw);
    const auto index = static_cast<std::size_t>(fault);
    if (index < observed.faultCalls.size())
        ++observed.faultCalls[index];
    return fault == observed.inject || fault == observed.inject2 ? observed.injectedError : 0;
}

ClipboardTransfer::Observer Observe(Observation& observed) noexcept
{
    return {&observed, Reached, FaultCall};
}

Status InspectRetry(std::int64_t budget, Inspection& inspected, CancelToken cancellation = {},
                    ClipboardTransfer::Observer observer = {}, InspectDiagnostic* diagnostic = nullptr)
{
    InspectDiagnostic localDiagnostic;
    if (!diagnostic)
        diagnostic = &localDiagnostic;
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return Status::OutOfMemory;
    for (unsigned attempt = 0; attempt < kFixtureAttempts; ++attempt)
    {
        SetLastError(ERROR_SUCCESS);
        const Status status = ClipboardTransfer::Inspect(budget, inspected, cancellation, observer);
        if (diagnostic)
        {
            ++diagnostic->attempts;
            diagnostic->status = status;
            diagnostic->nativeError = GetLastError();
            diagnostic->sequenceAfterCall = GetClipboardSequenceNumber();
            if (status == Status::Busy)
            {
                diagnostic->busyError = diagnostic->nativeError;
                if (const HWND opener = GetOpenClipboardWindow())
                    (void)GetWindowThreadProcessId(opener, &diagnostic->busyProcess);
            }
        }
        if (status != Status::Busy || cancellation.IsCancelled() || attempt + 1 == kFixtureAttempts ||
            !WaitOneShot(timer.get()))
        {
            if (diagnostic->busyError)
                std::printf("clipboardbinding.listener-settle attempts=%u status=%u busyError=%lu busyProcess=%lu "
                            "callerProcess=%lu sequence=%lu\n",
                            diagnostic->attempts, static_cast<unsigned>(status), diagnostic->busyError,
                            diagnostic->busyProcess, GetCurrentProcessId(), diagnostic->sequenceAfterCall);
            return status;
        }
    }
    return Status::Busy;
}

Status CopyRetry(const Inspection& inspected, ImageCopy& copy, CancelToken cancellation = {},
                 ClipboardTransfer::Observer observer = {})
{
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return Status::OutOfMemory;
    for (unsigned attempt = 0; attempt < kFixtureAttempts; ++attempt)
    {
        const Status status = ClipboardTransfer::Copy(inspected, copy, cancellation, observer);
        if (status != Status::Busy || cancellation.IsCancelled() || attempt + 1 == kFixtureAttempts ||
            !WaitOneShot(timer.get()))
            return status;
    }
    return Status::Busy;
}

Publication PublishRetry(PreparedImage& prepared, CancelToken cancellation = {},
                         ClipboardTransfer::Observer observer = {})
{
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return {Status::OutOfMemory};
    for (unsigned attempt = 0; attempt < kFixtureAttempts; ++attempt)
    {
        Publication result = ClipboardTransfer::Publish(prepared, cancellation, observer);
        // In particular, an emptied clipboard is terminal even if only some formats were placed.
        if (result.status != Status::Busy || result.emptied || cancellation.IsCancelled() ||
            attempt + 1 == kFixtureAttempts || !WaitOneShot(timer.get()))
            return result;
    }
    return {Status::Busy};
}

bool DecodeEquals(const ImageCopy& copy, std::span<const std::uint8_t> expected)
{
    Raster decoded;
    return ClipboardTransfer::Decode(copy, decoded) == Status::Ok && decoded.HasStorage() && decoded.Width() == 2 &&
           decoded.Height() == 1 && decoded.Format() == RasterFormat::Bgra8888 &&
           decoded.ByteSize() == expected.size() && std::equal(expected.begin(), expected.end(), decoded.Data());
}

bool InspectCopy(HWND owner, Inspection& inspected, ImageCopy& copy, Observation* observed = nullptr,
                 CancelToken cancellation = {}, InspectDiagnostic* diagnostic = nullptr)
{
    const auto observer = observed ? Observe(*observed) : ClipboardTransfer::Observer{};
    const Status inspectStatus = InspectRetry(2, inspected, cancellation, observer, diagnostic);
    if (inspectStatus != Status::Ok || !inspected)
    {
        std::printf("clipboardbinding.inspect status=%u valid=%u sequence=%lu currentSequence=%lu attempts=%u "
                    "nativeError=%lu sequenceAfterCall=%lu observerHeldSequence=%lu\n",
                    static_cast<unsigned>(inspectStatus), inspected ? 1u : 0u,
                    static_cast<unsigned long>(inspected.Sequence()),
                    static_cast<unsigned long>(GetClipboardSequenceNumber()), diagnostic ? diagnostic->attempts : 0u,
                    static_cast<unsigned long>(diagnostic ? diagnostic->nativeError : ERROR_SUCCESS),
                    static_cast<unsigned long>(diagnostic ? diagnostic->sequenceAfterCall : 0u),
                    static_cast<unsigned long>(observed ? observed->heldSequence : 0u));
        DumpClipboardFormats(owner, "inspect-failed", diagnostic ? diagnostic->nativeError : ERROR_SUCCESS,
                             observed ? observed->heldSequence : 0u, inspected.Sequence());
        return false;
    }
    const Status copyStatus = CopyRetry(inspected, copy, cancellation, observer);
    (void)owner;
    if (copyStatus != Status::Ok || copy.Sequence() != inspected.Sequence())
        std::printf("clipboardbinding.copy status=%u sequence=%lu expectedSequence=%lu currentSequence=%lu\n",
                    static_cast<unsigned>(copyStatus), static_cast<unsigned long>(copy.Sequence()),
                    static_cast<unsigned long>(inspected.Sequence()),
                    static_cast<unsigned long>(GetClipboardSequenceNumber()));
    return copyStatus == Status::Ok && copy.Sequence() == inspected.Sequence();
}

bool ReadPublished(HWND owner, std::vector<std::uint8_t>& receipt, std::vector<std::uint8_t>& png,
                   std::vector<std::uint8_t>& dib)
{
    if (!OpenClipboardBounded(owner))
        return false;
    const auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
    const auto copyFormat = [](UINT format, std::vector<std::uint8_t>& output)
    {
        const HANDLE handle = GetClipboardData(format);
        if (!handle)
            return false;
        const SIZE_T size = GlobalSize(handle);
        if (size == 0)
            return false;
        const wil::unique_hglobal_locked locked(handle);
        if (!locked.get())
            return false;
        const auto* bytes = static_cast<const std::uint8_t*>(locked.get());
        output.assign(bytes, bytes + size);
        return true;
    };
    return copyFormat(ReceiptFormat(), receipt) && copyFormat(RegisterClipboardFormatW(L"PNG"), png) &&
           copyFormat(CF_DIBV5, dib);
}

bool Origin(const ImageCopy& copy, const ClipboardFingerprint::Binding& binding,
            const ClipboardReceipt::Key& currentKey, bool& matches, CancelToken cancellation = {},
            const char* diagnosticLabel = nullptr)
{
    const Status status = ClipboardTransfer::HasPrivateOrigin(copy, binding, currentKey, matches, cancellation);
    if (diagnosticLabel && (status != Status::Ok || !matches))
    {
        const DWORD currentSequence = GetClipboardSequenceNumber();
        std::printf("clipboardbinding.diag %s originStatus=%u matches=%u copySequence=%lu currentSequence=%lu "
                    "privateKeyMatches=%u expectedPngBytes=%llu expectedDibBytes=%llu\n",
                    diagnosticLabel, static_cast<unsigned>(status), matches ? 1u : 0u,
                    static_cast<unsigned long>(copy.Sequence()), static_cast<unsigned long>(currentSequence),
                    binding.key == currentKey ? 1u : 0u, static_cast<unsigned long long>(binding.png.bytes),
                    static_cast<unsigned long long>(binding.dib.bytes));
        DumpClipboardFormats(nullptr, diagnosticLabel, ERROR_SUCCESS, copy.Sequence(), copy.Sequence());
    }
    return status == Status::Ok;
}

std::optional<ClipboardFingerprint::Binding> PrepareBound(const ClipboardReceipt::Key& key, PreparedImage& prepared,
                                                          Observation* observed = nullptr)
{
    const auto receipt = std::optional<ClipboardReceipt::Key>{key};
    const Status status =
        ClipboardTransfer::Prepare(Source(), 96.0, GetClipboardSequenceNumber(), prepared, {},
                                   observed ? Observe(*observed) : ClipboardTransfer::Observer{}, receipt);
    if (status != Status::Ok || !prepared)
    {
        std::printf("clipboardbinding.prepare status=%u prepared=%u sourceSequence=%lu currentSequence=%lu\n",
                    static_cast<unsigned>(status), prepared ? 1u : 0u,
                    static_cast<unsigned long>(prepared.SourceSequence()),
                    static_cast<unsigned long>(GetClipboardSequenceNumber()));
        return std::nullopt;
    }
    return prepared.ContentBinding();
}

void TestPrepareAndReceiptGlobal(HWND owner, const ClipboardReceipt::Key& key)
{
    PreparedImage keyed;
    const auto binding = PrepareBound(key, keyed);
    PreparedImage plain;
    const Status plainStatus = ClipboardTransfer::Prepare(Source(), 96.0, GetClipboardSequenceNumber(), plain);
    Expect(binding && binding->key == key && binding->png.bytes != 0 && binding->dib.bytes != 0 &&
               binding->png.sha256 != std::array<std::uint8_t, 32>{} &&
               binding->dib.sha256 != std::array<std::uint8_t, 32>{} &&
               keyed.RetainedBytes() >= plain.RetainedBytes() + ClipboardReceipt::kEncodedBytes,
           "prepare.keyed-local-binding-and-owned-receipt-memory");
    Expect(plainStatus == Status::Ok && !plain.ContentBinding(), "prepare.unkeyed-image-has-no-private-binding");

    Expect(PutClipboard(owner, {}, {}, kLiteralDib), "receiptglobal.previous-sequence-fixture-installed");
    const DWORD before = GetClipboardSequenceNumber();
    Observation failedGlobal;
    failedGlobal.inject = Fault::ReceiptGlobal;
    PreparedImage failed;
    const Status status = ClipboardTransfer::Prepare(Source(), 96.0, before, failed, {}, Observe(failedGlobal), key);
    Expect(status == Status::NativeFailure && !failed && GetClipboardSequenceNumber() == before &&
               failedGlobal.faultCalls[static_cast<std::size_t>(Fault::ReceiptGlobal)] == 1,
           "receiptglobal.nativefault-before-clipboard-mutation-preserves-sequence");
    failedGlobal.injectedError = ERROR_NOT_ENOUGH_MEMORY;
    Expect(ClipboardTransfer::Prepare(Source(), 96.0, before, failed, {}, Observe(failedGlobal), key) ==
                   Status::OutOfMemory &&
               !failed && GetClipboardSequenceNumber() == before &&
               failedGlobal.faultCalls[static_cast<std::size_t>(Fault::ReceiptGlobal)] == 2,
           "receiptglobal-allocation-refusal-clears-output-and-preserves-sequence");
}

void TestReceiptPlace(HWND owner, const ClipboardReceipt::Key& key)
{
    Expect(PutClipboard(owner, {}, {}, kLiteralDib), "receiptplace.previous-sequence-fixture-installed");
    PreparedImage prepared;
    const auto binding = PrepareBound(key, prepared);
    Observation fault;
    fault.inject = Fault::ReceiptPlace;
    const Publication publication = PublishRetry(prepared, {}, Observe(fault));
    ExpectPublication(binding && publication.status == Status::NativeFailure && publication.emptied &&
                          !publication.receiptPlaced && !publication.pngPlaced && !publication.dibPlaced && !prepared &&
                          fault.faultCalls[static_cast<std::size_t>(Fault::ReceiptPlace)] == 1 &&
                          fault.reached[static_cast<std::size_t>(Stage::Emptied)] == 1 &&
                          fault.reached[static_cast<std::size_t>(Stage::Closed)] == 1,
                      "receiptplace.nativefault-emptied-consumed-and-placed-no-image-formats", publication);
    Expect(publication.nativeError == fault.injectedError && publication.sequence == fault.lastMutationSequence,
           "receiptplace-reports-exact-native-error-and-held-empty-sequence");
    Expect(!IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")) && !IsClipboardFormatAvailable(CF_DIBV5),
           "receiptplace.failure-leaves-no-image-formats");
}

bool PublishBound(HWND owner, const ClipboardReceipt::Key& key, PreparedImage& prepared, Publication& publication,
                  std::optional<ClipboardFingerprint::Binding>& binding, Observation* observed = nullptr)
{
    if (!PutClipboard(owner, {}, {}, kLiteralDib))
    {
        std::printf(
            "clipboardbinding.fixture-install-failed nativeError=%lu owner=%p openWindow=%p clipboardOwner=%p\n",
            static_cast<unsigned long>(GetLastError()), static_cast<void*>(owner),
            static_cast<void*>(GetOpenClipboardWindow()), static_cast<void*>(GetClipboardOwner()));
        return false;
    }
    binding = PrepareBound(key, prepared);
    if (!binding)
        return false;
    publication = PublishRetry(prepared, {}, observed ? Observe(*observed) : ClipboardTransfer::Observer{});
    return true;
}

void TestPublishAndOrigin(HWND owner, const ClipboardReceipt::Key& key)
{
    PreparedImage prepared;
    Publication publication;
    std::optional<ClipboardFingerprint::Binding> binding;
    const bool published = PublishBound(owner, key, prepared, publication, binding);
    ExpectPublication(published && publication.status == Status::Ok && publication.emptied &&
                          publication.receiptPlaced && publication.pngPlaced && publication.dibPlaced && !prepared,
                      "publish.receipt-and-both-image-formats-placed", publication);

    Inspection inspected;
    ImageCopy copy;
    const bool copied = InspectCopy(owner, inspected, copy);
    bool origin = false;
    const bool originStatus = copied && binding && Origin(copy, binding.value(), key, origin);
    Expect(copied && originStatus && origin && DecodeEquals(copy, kPixels),
           "publish.destroyed-adapter-window-content-inspects-copies-origin-and-decodes-literal-bgra");

    ClipboardReceipt::Key stale = key;
    ++stale.serial;
    bool staleOrigin = true;
    const bool staleStatus = copied && binding && Origin(copy, binding.value(), stale, staleOrigin);
    Expect(staleStatus && !staleOrigin, "origin.stale-private-serial-has-no-private-origin");

    std::vector<std::uint8_t> receiptBytes, pngBytes, dibBytes;
    const bool readBack = ReadPublished(owner, receiptBytes, pngBytes, dibBytes);
    const DWORD publishedSequence = GetClipboardSequenceNumber();
    const bool restored = readBack && PutClipboard(owner, receiptBytes, pngBytes, dibBytes);
    const DWORD reboundSequence = GetClipboardSequenceNumber();
    Inspection reboundInspection;
    ImageCopy reboundCopy;
    const bool reboundCopied = restored && InspectCopy(owner, reboundInspection, reboundCopy);
    bool reboundOrigin = false;
    const bool reboundStatus = reboundCopied && binding && Origin(reboundCopy, binding.value(), key, reboundOrigin);
    Expect(readBack && restored && reboundSequence != publishedSequence && reboundCopied && reboundStatus &&
               reboundOrigin && DecodeEquals(reboundCopy, kPixels),
           "origin.identical-receipt-and-payload-match-after-sequence-changes");

    std::array<std::uint8_t, ClipboardReceipt::kEncodedBytes> encodedReceipt{};
    const bool encoded = ClipboardReceipt::Encode(key, encodedReceipt);
    // Change exactly one pixel byte of the just-verified original encoded block. Altering a separately
    // generated header (for example DPI) would not prove pixel replacement is what refuses origin.
    auto changedDib = dibBytes;
    const bool originalLayout = changedDib.size() >= 132 && changedDib[0] == 124 && changedDib[124] == 10;
    if (originalLayout)
        changedDib[124] = 11;
    Expect(encoded && originalLayout && PutClipboard(owner, encodedReceipt, {}, changedDib),
           "origin.changed-valid-dib-with-same-receipt-fixture-installed");
    Inspection changedInspection;
    ImageCopy changedCopy;
    const bool changedCopied = InspectCopy(owner, changedInspection, changedCopy);
    bool changedOrigin = true;
    const bool changedStatus = changedCopied && binding && Origin(changedCopy, binding.value(), key, changedOrigin);
    Expect(changedCopied && changedStatus && !changedOrigin && DecodeEquals(changedCopy, kChangedPixels),
           "origin.changed-valid-dib-no-png-is-foreign-but-decodes");
    Expect(originalLayout && PutClipboard(owner, receiptBytes, pngBytes, changedDib),
           "origin.original-png-and-one-byte-altered-dib-installed");
    Inspection mixedInspection;
    ImageCopy mixedCopy;
    bool mixedOrigin = true;
    Expect(InspectCopy(owner, mixedInspection, mixedCopy) && binding &&
               Origin(mixedCopy, binding.value(), key, mixedOrigin) && !mixedOrigin && DecodeEquals(mixedCopy, kPixels),
           "origin-one-altered-format-refuses-origin-even-with-valid-preferred-png");
}

void TestReceiptFallbacks(HWND owner, const ClipboardReceipt::Key& key)
{
    std::array<std::uint8_t, ClipboardReceipt::kEncodedBytes> receipt{};
    const bool encoded = ClipboardReceipt::Encode(key, receipt);
    Expect(encoded && PutClipboard(owner, {}, {}, kLiteralDib), "receipt.missing-fixture-installed");
    Inspection missingInspection;
    ImageCopy missingCopy;
    const bool missingCopied = InspectCopy(owner, missingInspection, missingCopy);
    bool missingOrigin = true;
    ClipboardFingerprint::Format literalFingerprint;
    Expect(ClipboardFingerprint::Hash(kLiteralDib, literalFingerprint) == 0,
           "receipt.literal-dib-fingerprint-prepared-independently");
    const ClipboardFingerprint::Binding expectedBinding{key, {}, literalFingerprint};
    const bool missingStatus = missingCopied && Origin(missingCopy, expectedBinding, key, missingOrigin);
    Expect(missingCopied && missingStatus && !missingOrigin && DecodeEquals(missingCopy, kPixels),
           "receipt.missing-still-allows-foreign-decode-without-origin");

    Expect(PutClipboard(owner, receipt, {}, kLiteralDib), "receipt.valid-discriminating-control-installed");
    Inspection validInspection;
    ImageCopy validCopy;
    bool validOrigin = false;
    Observation validObservation;
    InspectDiagnostic validDiagnostic;
    Expect(InspectCopy(owner, validInspection, validCopy, &validObservation, {}, &validDiagnostic) &&
               Origin(validCopy, expectedBinding, key, validOrigin, {}, "valid-receipt-origin") && validOrigin,
           "receipt.same-payload-with-valid-receipt-actually-matches-control");

    receipt[7] ^= 0x40;
    Expect(PutClipboard(owner, receipt, {}, kLiteralDib), "receipt.bad-fixture-installed");
    Inspection badInspection;
    ImageCopy badCopy;
    const bool badCopied = InspectCopy(owner, badInspection, badCopy);
    bool badOrigin = true;
    const bool badStatus = badCopied && Origin(badCopy, expectedBinding, key, badOrigin);
    Expect(badCopied && badStatus && !badOrigin && DecodeEquals(badCopy, kPixels),
           "receipt.bad-still-allows-foreign-decode-without-origin");
}

void TestPartialFormats(HWND owner, const ClipboardReceipt::Key& key)
{
    for (const Fault failedFormat : {Fault::Png, Fault::Dib})
    {
        if (!PutClipboard(owner, {}, {}, kLiteralDib))
        {
            Expect(false, failedFormat == Fault::Png ? "partialpng.fixture-installed" : "partialdib.fixture-installed");
            continue;
        }
        PreparedImage prepared;
        const auto binding = PrepareBound(key, prepared);
        Observation fault;
        fault.inject = failedFormat;
        const Publication publication = PublishRetry(prepared, {}, Observe(fault));
        const bool pngExpected = failedFormat != Fault::Png;
        const bool dibExpected = failedFormat != Fault::Dib;
        const bool reachedFacts =
            fault.faultCalls[static_cast<std::size_t>(failedFormat)] == 1 &&
            fault.reached[static_cast<std::size_t>(Stage::ReceiptPlaced)] == 1 &&
            fault.reached[static_cast<std::size_t>(Stage::PngPlaced)] == (pngExpected ? 1u : 0u) &&
            fault.reached[static_cast<std::size_t>(Stage::DibPlaced)] == (dibExpected ? 1u : 0u);
        Inspection inspected;
        ImageCopy copy;
        const bool copied = InspectCopy(owner, inspected, copy);
        bool origin = false;
        const bool originStatus = copied && binding && Origin(copy, binding.value(), key, origin);
        const std::span<const std::uint8_t> expected(kPixels);
        const bool onlyAvailableFormat =
            inspected.Png().pixels == (pngExpected ? 2u : 0u) && inspected.Dib().pixels == (dibExpected ? 2u : 0u);
        const char* name = failedFormat == Fault::Png ? "partialpng.receipt-binds-available-dib-decodes"
                                                      : "partialdib.receipt-binds-available-png-decodes";
        const bool decoded = copied && DecodeEquals(copy, expected);
        if (!reachedFacts || !copied || !onlyAvailableFormat || publication.sequence != fault.lastMutationSequence ||
            !originStatus || !origin || !decoded)
            std::printf("clipboardbinding.partial facts=%u copied=%u onlyAvailable=%u pngPixels=%llu dibPixels=%llu "
                        "publishedSequence=%lu mutationSequence=%lu originStatus=%u origin=%u decoded=%u\n",
                        reachedFacts ? 1u : 0u, copied ? 1u : 0u, onlyAvailableFormat ? 1u : 0u,
                        static_cast<unsigned long long>(inspected.Png().pixels),
                        static_cast<unsigned long long>(inspected.Dib().pixels),
                        static_cast<unsigned long>(publication.sequence),
                        static_cast<unsigned long>(fault.lastMutationSequence), originStatus ? 1u : 0u,
                        origin ? 1u : 0u, decoded ? 1u : 0u);
        ExpectPublication(binding && publication.status == Status::NativeFailure && publication.emptied &&
                              publication.receiptPlaced && publication.pngPlaced == pngExpected &&
                              publication.dibPlaced == dibExpected && reachedFacts && copied && onlyAvailableFormat &&
                              publication.sequence == fault.lastMutationSequence && originStatus && origin && decoded,
                          name, publication);
    }
}

void TestLateCancel(HWND owner, const ClipboardReceipt::Key& key)
{
    Expect(PutClipboard(owner, {}, {}, kLiteralDib), "latecancel.fixture-installed");
    PreparedImage prepared;
    const auto binding = PrepareBound(key, prepared);
    CancelSource cancel;
    Observation observed;
    observed.cancelAt = Stage::ReceiptPlaced;
    observed.cancel = &cancel;
    const Publication publication = PublishRetry(prepared, cancel.Token(), Observe(observed));
    ExpectPublication(binding && publication.status == Status::Ok && publication.emptied && publication.receiptPlaced &&
                          publication.pngPlaced && publication.dibPlaced && cancel.Token().IsCancelled() && !prepared &&
                          observed.reached[static_cast<std::size_t>(Stage::PngPlaced)] == 1 &&
                          observed.reached[static_cast<std::size_t>(Stage::DibPlaced)] == 1,
                      "latecancel.receiptplaced-finishes-both-image-formats-after-cancel", publication);
    Expect(publication.sequence == observed.lastMutationSequence,
           "latecancel-complete-publication-retains-exact-held-sequence");
}

void TestReceiptOnlyFailure(HWND owner, const ClipboardReceipt::Key& key)
{
    PreparedImage prepared;
    Publication publication;
    std::optional<ClipboardFingerprint::Binding> binding;
    Observation faults;
    faults.inject = Fault::Png;
    faults.inject2 = Fault::Dib;
    const bool attempted = PublishBound(owner, key, prepared, publication, binding, &faults);
    ExpectPublication(attempted && publication.emptied && publication.status == Status::NativeFailure &&
                          publication.receiptPlaced && !publication.pngPlaced && !publication.dibPlaced && !prepared &&
                          faults.faultCalls[static_cast<std::size_t>(Fault::Png)] == 1 &&
                          faults.faultCalls[static_cast<std::size_t>(Fault::Dib)] == 1,
                      "receipt-only-failure-has-exact-publication-truth", publication);
    Inspection inspection;
    Expect(InspectRetry(2, inspection) == Status::Invalid && !inspection && IsClipboardFormatAvailable(ReceiptFormat()),
           "receipt-alone-is-not-an-image-or-origin-authority");
}

void TestReceiptReadFault(HWND owner, const ClipboardReceipt::Key& key)
{
    PreparedImage prepared;
    Publication publication;
    std::optional<ClipboardFingerprint::Binding> binding;
    const bool published = PublishBound(owner, key, prepared, publication, binding);
    ExpectPublication(published && publication.status == Status::Ok && publication.emptied &&
                          publication.receiptPlaced && publication.pngPlaced && publication.dibPlaced,
                      "receiptread.control-publication-ready", publication);
    Observation fault;
    fault.inject = Fault::ReceiptRead;
    Inspection inspected;
    ImageCopy copy;
    const bool copied = published && InspectCopy(owner, inspected, copy, &fault);
    bool origin = true;
    const bool originStatus = copied && binding && Origin(copy, binding.value(), key, origin);
    Expect(copied && fault.faultCalls[static_cast<std::size_t>(Fault::ReceiptRead)] == 1 && originStatus && !origin &&
               DecodeEquals(copy, kPixels),
           "receiptread.nativefault-has-no-origin-but-owned-image-still-decodes");
}

} // namespace

int RunClipboardBindingTransferTests(HWND owner)
{
    gFailures = 0;
    gAssertions = 0;
    const ClipboardReceipt::Key key = TestKey();
    Expect(owner != nullptr && IsWindow(owner) != FALSE, "worker-caller-owned-test-owner-is-valid");
    if (!owner || !IsWindow(owner))
    {
        std::printf("ClipboardBindingTransfer: %u assertions, %d failures\n", gAssertions, gFailures);
        return gFailures;
    }
    TestPrepareAndReceiptGlobal(owner, key);
    TestReceiptPlace(owner, key);
    TestPublishAndOrigin(owner, key);
    TestReceiptFallbacks(owner, key);
    TestPartialFormats(owner, key);
    TestLateCancel(owner, key);
    TestReceiptReadFault(owner, key);
    TestReceiptOnlyFailure(owner, key);
    std::printf("ClipboardBindingTransfer: %u assertions, %d failures\n", gAssertions, gFailures);
    return gFailures;
}
