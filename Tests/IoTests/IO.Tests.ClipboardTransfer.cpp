#include "IO.Tests.ClipboardTransfer.h"
#include "IO.Tests.ClipboardBindingTransfer.h"

#include "../../RedPrism/ClipboardTransfer.h"
#include "../../RedPrism/SystemClipboard.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
unsigned gExpectations = 0;
HWND gOwner = nullptr;

void Expect(bool condition, const char* name) noexcept
{
    ++gExpectations;
    std::printf("%s clipboardtransfer.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 8> kPixels{0, 0, 128, 128, 255, 0, 0, 255};
constexpr std::array<std::uint8_t, 8> kStraightDibPixels{0, 0, 255, 128, 255, 0, 0, 255};
constexpr std::array<std::uint8_t, 8> kLiteralDibPixels{10, 20, 30, 255, 90, 80, 70, 255};
// Independent BGRA decode expectation for the existing fixture's literal RGB rows and PNG filters.
constexpr std::array<std::uint8_t, 24> kLiteralPngPixels{157, 111, 73, 255, 157, 111, 73, 255, 157, 111, 73, 255,
                                                         157, 111, 73, 255, 157, 111, 73, 255, 157, 111, 73, 255};

// Existing independent codec fixture: literal 2x3 RGB PNG. It is only used as clipboard input, never as an
// encoder expectation. Its independent dimensions and a separately supplied valid DIB exercise PNG priority.
constexpr std::array<std::uint8_t, 77> kLiteralPng{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x08, 0x02, 0x00, 0x00, 0x00, 0x36, 0x88, 0x49,
    0xD6, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF4, 0xCC, 0x9F, 0xCB,
    0xC0, 0xC0, 0xC0, 0xC4, 0x00, 0x06, 0x50, 0x0A, 0x00, 0x19, 0x4F, 0x01, 0x5B, 0x54, 0x13, 0x85,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

// Literal 124-byte BITMAPV5HEADER plus two bottom-up opaque BGRA pixels. This is test input, not production output.
constexpr std::array<std::uint8_t, 132> kLiteralDibV5{
    // BITMAPV5HEADER: size, dimensions, planes, bit depth, BI_BITFIELDS, image size, resolution, palette counts.
    124, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 32, 0, 3, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
    // BGRA masks and the BGRs color-space tag.
    0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 255, 66, 71, 82, 115,
    // CIEXYZ endpoints, gamma values, LCS_GM_IMAGES intent, and empty profile fields.
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // Literal bottom-up BGRA rows, independent of the production encoder.
    10, 20, 30, 255, 90, 80, 70, 255};

UINT PngFormat() noexcept
{
    return RegisterClipboardFormatW(L"PNG");
}

// Match the availability/receipt fixtures: at most 30 attempts, 100 ms event waits between Busy results.
// Production admission/retry limits are independent. An admitted source sequence is never renewed here.
constexpr unsigned kFixtureAttempts = SystemClipboard::kHarnessAttempts / 10;
template <typename Attempt, typename Retryable, typename Wait>
auto RetryFixtureAcquisition(Attempt attempt, Retryable retryable, Wait wait)
{
    static_assert(kFixtureAttempts == 30);
    for (unsigned index = 0;; ++index)
    {
        auto result = attempt(index);
        if (!retryable(result) || index + 1 == kFixtureAttempts || !wait())
            return result;
    }
}

struct FixtureRetryWait final
{
    wil::unique_handle timer;

    bool operator()() noexcept
    {
        if (!timer)
        {
            timer.reset(CreateWaitableTimerW(nullptr, TRUE, nullptr));
            if (!timer)
            {
                std::printf("DIAG clipboardtransfer.fixture-timer-create error=%lu\n", GetLastError());
                return false;
            }
        }
        LARGE_INTEGER due{};
        due.QuadPart = -1'000'000; // 100 ms, relative one-shot timer; 200 ms is a failure bound.
        if (!SetWaitableTimer(timer.get(), &due, 0, nullptr, nullptr, FALSE))
        {
            std::printf("DIAG clipboardtransfer.fixture-timer-arm error=%lu\n", GetLastError());
            return false;
        }
        const DWORD result = WaitForSingleObject(timer.get(), 200);
        if (result != WAIT_OBJECT_0)
        {
            const DWORD error = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
            std::printf("DIAG clipboardtransfer.fixture-timer-wait result=%lu error=%lu\n", result, error);
        }
        return result == WAIT_OBJECT_0;
    }
};

void TestFixtureRetryPolicy()
{
    using Status = ClipboardTransfer::Status;
    struct Trace final
    {
        std::span<const Status> results;
        unsigned attempts = 0, waits = 0;
        bool waitSucceeds = true;
    };
    const auto run = [](Trace& trace, CancelToken cancellation = {}) noexcept
    {
        return RetryFixtureAcquisition(
            [&](unsigned index) noexcept
            {
                ++trace.attempts;
                return trace.results[(std::min)(static_cast<std::size_t>(index), trace.results.size() - 1)];
            },
            [&](Status status) noexcept { return status == Status::Busy && !cancellation.IsCancelled(); },
            [&]() noexcept
            {
                ++trace.waits;
                return trace.waitSucceeds;
            });
    };
    constexpr std::array ready{Status::Ok};
    Trace immediate{ready};
    Expect(run(immediate) == Status::Ok, "fixture-retry.immediate-success");
    Expect(immediate.attempts == 1 && immediate.waits == 0, "fixture-retry.ready-does-not-wait");

    constexpr std::array delayed{Status::Busy, Status::Busy, Status::Busy, Status::Busy, Status::Busy, Status::Ok};
    Trace later{delayed};
    Expect(run(later) == Status::Ok, "fixture-retry.success-after-five-contention-results");
    Expect(later.attempts == 6 && later.waits == 5, "fixture-retry.supplied-six-step-trace-consumed-once");

    constexpr std::array busy{Status::Busy};
    Trace exhausted{busy};
    Expect(run(exhausted) == Status::Busy, "fixture-retry.exhaustion-retains-busy");
    Expect(exhausted.attempts == 30 && exhausted.waits == 29, "fixture-retry.finite-30-attempts-no-final-wait");

    Trace waitFailure{busy};
    waitFailure.waitSucceeds = false;
    Expect(run(waitFailure) == Status::Busy, "fixture-retry.failed-wait-retains-original-result");
    Expect(waitFailure.attempts == 1 && waitFailure.waits == 1, "fixture-retry.failed-wait-does-not-reacquire");

    constexpr std::array replaced{Status::Busy, Status::Stale, Status::Ok};
    Trace stale{replaced};
    Expect(run(stale) == Status::Stale, "fixture-retry.stale-authority-is-final");
    Expect(stale.attempts == 2 && stale.waits == 1, "fixture-retry.stale-never-renewed-to-success");

    CancelSource stop;
    (void)stop.Cancel();
    Trace cancelled{busy};
    Expect(run(cancelled, stop.Token()) == Status::Busy, "fixture-retry.cancellation-retains-attempt-result");
    Expect(cancelled.attempts == 1 && cancelled.waits == 0, "fixture-retry.cancellation-prevents-retry");

    unsigned partialAttempts = 0, partialWaits = 0;
    const auto partial = RetryFixtureAcquisition(
        [&](unsigned) noexcept
        {
            ++partialAttempts;
            ClipboardTransfer::Publication publication;
            publication.status = Status::Busy;
            publication.emptied = true;
            publication.nativeError = ERROR_ACCESS_DENIED;
            return publication;
        },
        [](const ClipboardTransfer::Publication& publication) noexcept
        { return publication.status == Status::Busy && !publication.emptied; },
        [&]() noexcept
        {
            ++partialWaits;
            return true;
        });
    Expect(partial.status == Status::Busy && partial.emptied && partial.nativeError == ERROR_ACCESS_DENIED,
           "fixture-retry.partial-publication-retains-exact-result");
    Expect(partialAttempts == 1 && partialWaits == 0, "fixture-retry.partial-publication-never-replayed");
}

bool OpenClipboardRetry(HWND owner) noexcept
{
    FixtureRetryWait wait;
    return RetryFixtureAcquisition(
        [&](unsigned attempt) noexcept
        {
            if (OpenClipboard(owner))
                return true;
            const DWORD error = GetLastError();
            DWORD holderProcess = 0;
            const HWND holder = GetOpenClipboardWindow();
            if (holder)
                (void)GetWindowThreadProcessId(holder, &holderProcess);
            std::printf("DIAG clipboardtransfer.fixture-open attempt=%u error=%lu holderProcess=%lu callerProcess=%lu "
                        "ownerValid=%d\n",
                        attempt + 1, error, holderProcess, GetCurrentProcessId(), IsWindow(owner) != FALSE);
            return false;
        },
        [](bool opened) noexcept { return !opened; }, [&]() noexcept { return wait(); });
}

[[noreturn]] void FailFastJoin(const char* name) noexcept
{
    std::printf("FAIL clipboardtransfer thread join timed out: %s\n", name);
    std::fflush(stdout);
    RaiseFailFastException(nullptr, nullptr, 0);
    std::abort();
}

void JoinNativeThread(HANDLE thread, const char* name) noexcept
{
    if (WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0)
        FailFastJoin(name);
}

void Put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value)
{
    bytes[at] = static_cast<std::uint8_t>(value);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[at + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[at + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::vector<std::uint8_t> LargeDibHeader()
{
    // Structurally valid 10,001x10,000 24-bit header: exceeds the independent 100M hard pixel ceiling.
    std::vector<std::uint8_t> bytes(40, 0);
    Put32(bytes, 0, 40);
    Put32(bytes, 4, 10'001);
    Put32(bytes, 8, 10'000);
    bytes[12] = 1;
    bytes[14] = 24;
    return bytes;
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

void NativeFailureDiagnostic(void*, ClipboardTransfer::NativeCall call, std::uint32_t format,
                             std::uint32_t error) noexcept
{
    DWORD holderProcess = 0, ownerProcess = 0;
    if (const HWND holder = GetOpenClipboardWindow())
        (void)GetWindowThreadProcessId(holder, &holderProcess);
    if (const HWND owner = GetClipboardOwner())
        (void)GetWindowThreadProcessId(owner, &ownerProcess);
    std::printf("DIAG clipboardtransfer.native call=%u format=%lu error=%lu holderProcess=%lu ownerProcess=%lu "
                "callerProcess=%lu sequence=%lu\n",
                static_cast<unsigned>(call), static_cast<unsigned long>(format), static_cast<unsigned long>(error),
                holderProcess, ownerProcess, GetCurrentProcessId(), GetClipboardSequenceNumber());
}

ClipboardTransfer::Status InspectWithBusyRetry(std::int64_t pixelBudget, ClipboardTransfer::Inspection& output,
                                               CancelToken cancellation, ClipboardTransfer::Observer observer);
ClipboardTransfer::Status CopyWithBusyRetry(const ClipboardTransfer::Inspection& inspection,
                                            ClipboardTransfer::ImageCopy& output, CancelToken cancellation,
                                            ClipboardTransfer::Observer observer);

bool PutClipboard(std::span<const std::uint8_t> png, std::span<const std::uint8_t> dib) noexcept
{
    if (!OpenClipboardRetry(gOwner))
        return false;
    {
        auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
        if (!EmptyClipboard())
            return false;
        if (!png.empty() && !SetGlobal(PngFormat(), png))
            return false;
        if (!dib.empty() && !SetGlobal(CF_DIBV5, dib))
            return false;
        return true;
    }
}

struct DelayedProducer final
{
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    HANDLE ready = nullptr;
    std::atomic<HWND> window{nullptr};
    std::atomic<DWORD> threadId{0};
    std::atomic<bool> installed{false};
    std::atomic<unsigned> renderCalls{0};
    std::atomic<unsigned> pngRenders{0}, dibRenders{0};
    std::atomic<DWORD> renderError{0}, renderWait{WAIT_TIMEOUT};
};

constexpr wchar_t kDelayedProducerClass[] = L"RedPrismClipboardTransferDelayedProducer";

LRESULT CALLBACK DelayedWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    auto* producer = reinterpret_cast<DelayedProducer*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        producer = static_cast<DelayedProducer*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(producer));
    }
    if (message == WM_RENDERFORMAT && producer)
    {
        (void)producer->renderCalls.fetch_add(1, std::memory_order_relaxed);
        (void)SetEvent(producer->entered);
        const DWORD released = WaitForSingleObject(producer->release, 5000);
        producer->renderWait.store(released);
        if (released == WAIT_OBJECT_0)
        {
            bool placed = false;
            SetLastError(ERROR_SUCCESS);
            if (wParam == PngFormat())
            {
                producer->pngRenders.fetch_add(1);
                placed = SetGlobal(static_cast<UINT>(wParam), kLiteralPng);
            }
            else if (wParam == CF_DIBV5)
            {
                producer->dibRenders.fetch_add(1);
                placed = SetGlobal(CF_DIBV5, kLiteralDibV5);
            }
            if (!placed)
                producer->renderError.store(GetLastError());
        }
        return 0;
    }
    if (message == WM_CLOSE)
    {
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

DWORD WINAPI DelayedProducerMain(void* raw) noexcept
{
    auto& producer = *static_cast<DelayedProducer*>(raw);
    producer.threadId.store(GetCurrentThreadId(), std::memory_order_release);
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.lpfnWndProc = DelayedWndProc;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = kDelayedProducerClass;
    if (!RegisterClassExW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        (void)SetEvent(producer.ready);
        return 1;
    }
    wil::unique_hwnd window(CreateWindowExW(0, kDelayedProducerClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                            klass.hInstance, &producer));
    producer.window.store(window.get(), std::memory_order_release);
    if (window && OpenClipboardRetry(window.get()))
    {
        const bool emptied = EmptyClipboard() != FALSE;
        if (emptied)
            (void)SetClipboardData(PngFormat(), nullptr);
        const bool png = emptied && IsClipboardFormatAvailable(PngFormat()) != FALSE;
        if (png)
            (void)SetClipboardData(CF_DIBV5, nullptr);
        const bool dib = png && IsClipboardFormatAvailable(CF_DIBV5) != FALSE;
        const bool closed = CloseClipboard() != FALSE;
        producer.installed.store(png && dib && closed, std::memory_order_release);
    }
    (void)SetEvent(producer.ready);
    if (!window)
        return 1;
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    window.reset();
    producer.window.store(nullptr, std::memory_order_release);
    return producer.installed.load(std::memory_order_acquire) ? 0 : 1;
}

bool ReadClipboardPixels(std::span<const std::uint8_t> expected, int width, int height)
{
    // Verification uses the independent C9b reader, which holds one clipboard acquisition through decode.
    // Inspect/Copy authority and Stale remain exercised directly below. Listener-generated formats must not
    // invalidate this separate pixel-preservation observation between two verification acquisitions.
    Raster image;
    const bool read = SystemClipboard::ReadImage(gOwner, kMaxLayerPixels, image, SystemClipboard::kHarnessAttempts);
    const DWORD error = GetLastError();
    const bool matches = read && image.HasStorage() && image.Width() == width && image.Height() == height &&
                         image.Format() == RasterFormat::Bgra8888 &&
                         std::equal(expected.begin(), expected.end(), image.Data());
    if (!matches)
        std::printf("DIAG clipboardtransfer.fixture-read read=%d error=%lu size=%dx%d expected=%dx%d\n", read, error,
                    image.Width(), image.Height(), width, height);
    return matches;
}

bool SequenceWhileOpen(DWORD& sequence) noexcept
{
    if (!OpenClipboardRetry(gOwner))
        return false;
    sequence = GetClipboardSequenceNumber();
    const bool closed = CloseClipboard() != FALSE;
    const DWORD error = closed ? ERROR_SUCCESS : GetLastError();
    if (!closed)
        std::printf("DIAG clipboardtransfer.fixture-sequence-close error=%lu sequence=%lu\n", error, sequence);
    return closed;
}

bool VerifyPublishedGlobals(std::span<const std::uint8_t> expectedPixels) noexcept
{
    if (!OpenClipboardRetry(gOwner))
        return false;
    bool valid = false;
    {
        const auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
        const HANDLE png = GetClipboardData(PngFormat());
        const HANDLE dib = GetClipboardData(CF_DIBV5);
        if (png && dib)
        {
            const SIZE_T pngSize = GlobalSize(png);
            const wil::unique_hglobal_locked pngLock(png);
            const SIZE_T dibSize = GlobalSize(dib);
            const wil::unique_hglobal_locked dibLock(dib);
            if (pngSize >= 24 && dibSize >= 124 + expectedPixels.size() && pngLock.get() && dibLock.get())
            {
                const auto* pngBytes = static_cast<const std::uint8_t*>(pngLock.get());
                const auto* dibBytes = static_cast<const std::uint8_t*>(dibLock.get());
                const auto be32 = [](const std::uint8_t* p) noexcept
                {
                    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
                           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
                };
                const auto le32 = [](const std::uint8_t* p) noexcept
                {
                    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
                };
                const bool signature = pngBytes[0] == 0x89 && pngBytes[1] == 'P' && pngBytes[2] == 'N' &&
                                       pngBytes[3] == 'G' && pngBytes[12] == 'I' && pngBytes[13] == 'H' &&
                                       pngBytes[14] == 'D' && pngBytes[15] == 'R';
                valid = signature && be32(pngBytes + 16) == 2 && be32(pngBytes + 20) == 1 && le32(dibBytes) == 124 &&
                        le32(dibBytes + 4) == 2 && le32(dibBytes + 8) == 1 && dibBytes[14] == 32 && dibBytes[16] == 3 &&
                        // DIBV5 carries straight alpha; the PNG reader returns the original premultiplied bytes.
                        std::equal(kStraightDibPixels.begin(), kStraightDibPixels.end(), dibBytes + 124);
            }
        }
    }
    return valid;
}

struct Control final
{
    ClipboardTransfer::Stage cancelAt = static_cast<ClipboardTransfer::Stage>(255);
    ClipboardTransfer::Fault faultAt = static_cast<ClipboardTransfer::Fault>(255);
    ClipboardTransfer::Fault faultAt2 = static_cast<ClipboardTransfer::Fault>(255);
    DWORD faultError = ERROR_GEN_FAILURE;
    CancelSource* cancel = nullptr;
    std::array<unsigned, static_cast<std::size_t>(ClipboardTransfer::Stage::Copied) + 1> reached{};
    std::array<unsigned, static_cast<std::size_t>(ClipboardTransfer::Fault::ReceiptRead) + 1> injected{};
    DWORD dibPlacedSequence = 0;
};

void Reached(void* raw, ClipboardTransfer::Stage stage) noexcept
{
    auto& control = *static_cast<Control*>(raw);
    const auto index = static_cast<std::size_t>(stage);
    if (index < control.reached.size())
        ++control.reached[index];
    if (stage == ClipboardTransfer::Stage::DibPlaced)
        control.dibPlacedSequence = GetClipboardSequenceNumber();
    if (stage == control.cancelAt && control.cancel)
        (void)control.cancel->Cancel();
}

std::uint32_t Fault(void* raw, ClipboardTransfer::Fault fault) noexcept
{
    auto& control = *static_cast<Control*>(raw);
    if (fault != control.faultAt && fault != control.faultAt2)
        return 0;
    ++control.injected[static_cast<std::size_t>(fault)];
    return control.faultError;
}

ClipboardTransfer::Observer Observer(Control& control) noexcept
{
    return {&control, Reached, Fault};
}

ClipboardTransfer::Publication PublishWithBusyRetry(ClipboardTransfer::PreparedImage& prepared,
                                                    CancelToken cancellation = {},
                                                    ClipboardTransfer::Observer observer = {}) noexcept
{
    FixtureRetryWait wait;
    return RetryFixtureAcquisition(
        [&](unsigned) noexcept { return ClipboardTransfer::Publish(prepared, cancellation, observer); },
        [&](const ClipboardTransfer::Publication& result) noexcept
        { return result.status == ClipboardTransfer::Status::Busy && !result.emptied && !cancellation.IsCancelled(); },
        [&]() noexcept { return wait(); });
}

ClipboardTransfer::Status InspectWithBusyRetry(std::int64_t pixelBudget, ClipboardTransfer::Inspection& output,
                                               CancelToken cancellation = {}, ClipboardTransfer::Observer observer = {})
{
    observer.nativeFailure = NativeFailureDiagnostic;
    FixtureRetryWait wait;
    return RetryFixtureAcquisition([&](unsigned)
                                   { return ClipboardTransfer::Inspect(pixelBudget, output, cancellation, observer); },
                                   [&](ClipboardTransfer::Status status) noexcept
                                   { return status == ClipboardTransfer::Status::Busy && !cancellation.IsCancelled(); },
                                   [&]() noexcept { return wait(); });
}

ClipboardTransfer::Status CopyWithBusyRetry(const ClipboardTransfer::Inspection& inspection,
                                            ClipboardTransfer::ImageCopy& output, CancelToken cancellation = {},
                                            ClipboardTransfer::Observer observer = {})
{
    observer.nativeFailure = NativeFailureDiagnostic;
    FixtureRetryWait wait;
    return RetryFixtureAcquisition(
        [&](unsigned attempt)
        {
            const auto status = ClipboardTransfer::Copy(inspection, output, cancellation, observer);
            const DWORD error = GetLastError();
            if (status == ClipboardTransfer::Status::Busy)
            {
                DWORD holderProcess = 0;
                const HWND holder = GetOpenClipboardWindow();
                if (holder)
                    (void)GetWindowThreadProcessId(holder, &holderProcess);
                std::printf("DIAG clipboardtransfer.copy-busy attempt=%u error=%lu holderProcess=%lu callerProcess=%lu "
                            "inspectedSequence=%lu\n",
                            attempt + 1, error, holderProcess, GetCurrentProcessId(),
                            static_cast<unsigned long>(inspection.Sequence()));
            }
            return status;
        },
        [&](ClipboardTransfer::Status status) noexcept
        { return status == ClipboardTransfer::Status::Busy && !cancellation.IsCancelled(); },
        [&]() noexcept { return wait(); });
}

Raster Source()
{
    return Raster::Make(2, 1, RasterFormat::Bgra8888, kPixels.data(), kPixels.size());
}

ClipboardTransfer::PreparedImage PreparedAt(std::uint32_t sourceSequence)
{
    ClipboardTransfer::PreparedImage image;
    const auto status = ClipboardTransfer::Prepare(Source(), 96.0, sourceSequence, image);
    if (status != ClipboardTransfer::Status::Ok)
        std::printf("clipboardtransfer prepare status=%u sourceSequence=%lu retained=%llu\n",
                    static_cast<unsigned>(status), static_cast<unsigned long>(sourceSequence),
                    static_cast<unsigned long long>(image.RetainedBytes()));
    Expect(status == ClipboardTransfer::Status::Ok && static_cast<bool>(image) && image.RetainedBytes() > 0 &&
               image.SourceSequence() == sourceSequence,
           "prepare.both-moveable-globals-ready-and-source-sealed");
    return image;
}

ClipboardTransfer::PreparedImage Prepared()
{
    DWORD sourceSequence = 0;
    Expect(SequenceWhileOpen(sourceSequence), "prepare.source-sequence-captured-before-prepare");
    return PreparedAt(sourceSequence);
}

void TestPreparation()
{
    auto first = Prepared();
    const auto retained = first.RetainedBytes();
    ClipboardTransfer::PreparedImage moved = std::move(first);
    Expect(!first && moved && moved.RetainedBytes() == retained, "prepare.move-transfers-complete-ownership");
    ClipboardTransfer::PreparedImage output = Prepared();
    const auto pub = PublishWithBusyRetry(output);
    Expect(pub.status == ClipboardTransfer::Status::Ok && pub.emptied && pub.pngPlaced && pub.dibPlaced && !output,
           "prepare.published-input-single-use");
    Expect(VerifyPublishedGlobals(kPixels), "publish.both-fully-prepared-global-formats-readable");
    Expect(ReadClipboardPixels(kPixels, 2, 1), "publish.exact-premultiplied-pixels-after-window-destruction");
    const auto secondPublish = ClipboardTransfer::Publish(output);
    Expect(secondPublish.status == ClipboardTransfer::Status::Invalid && !secondPublish.emptied,
           "publish.consumed-input-rejects-second-use");

    ClipboardTransfer::PreparedImage cancelled;
    CancelSource stop;
    (void)stop.Cancel();
    DWORD cancelledSourceSequence = 0;
    Expect(SequenceWhileOpen(cancelledSourceSequence), "prepare.precancel-source-sequence-captured-first");
    Expect(ClipboardTransfer::Prepare(Source(), 96.0, cancelledSourceSequence, cancelled, stop.Token()) ==
                   ClipboardTransfer::Status::Cancelled &&
               !cancelled,
           "prepare.precancel-no-output");

    for (const auto fault : {ClipboardTransfer::Fault::PngGlobal, ClipboardTransfer::Fault::DibGlobal})
    {
        for (const DWORD error : {ERROR_GEN_FAILURE, ERROR_NOT_ENOUGH_MEMORY})
        {
            DWORD sourceSequence = 0;
            Expect(SequenceWhileOpen(sourceSequence), "prepare.fault-source-sequence-captured-before-prepare");
            Control control;
            control.faultAt = fault;
            control.faultError = error;
            ClipboardTransfer::PreparedImage failed;
            const auto status =
                ClipboardTransfer::Prepare(Source(), 96.0, sourceSequence, failed, {}, Observer(control));
            const auto expected = error == ERROR_NOT_ENOUGH_MEMORY ? ClipboardTransfer::Status::OutOfMemory
                                                                   : ClipboardTransfer::Status::NativeFailure;
            Expect(status == expected && !failed && control.injected[static_cast<std::size_t>(fault)] == 1,
                   fault == ClipboardTransfer::Fault::PngGlobal
                       ? (error == ERROR_NOT_ENOUGH_MEMORY ? "prepare.png-global-oom-no-output"
                                                           : "prepare.png-global-nativefailure-no-output")
                       : (error == ERROR_NOT_ENOUGH_MEMORY ? "prepare.dib-global-oom-no-output"
                                                           : "prepare.dib-global-nativefailure-no-output"));
        }
    }
    {
        CancelSource globalsStop;
        Control control;
        control.cancel = &globalsStop;
        control.cancelAt = ClipboardTransfer::Stage::GlobalsPrepared;
        ClipboardTransfer::PreparedImage failed;
        DWORD sourceSequence = 0;
        Expect(SequenceWhileOpen(sourceSequence), "prepare.full-globals-cancel-source-captured");
        Expect(ClipboardTransfer::Prepare(Source(), 96.0, sourceSequence, failed, globalsStop.Token(),
                                          Observer(control)) == ClipboardTransfer::Status::Cancelled &&
                   !failed &&
                   control.reached[static_cast<std::size_t>(ClipboardTransfer::Stage::GlobalsPrepared)] == 1 &&
                   ReadClipboardPixels(kPixels, 2, 1),
               "prepare.full-globals-cancel-discards-private-blocks-preserves-os-pixels");
    }
}

void TestPublishOutcomes()
{
    for (const auto fault :
         {ClipboardTransfer::Fault::Window, ClipboardTransfer::Fault::Open, ClipboardTransfer::Fault::Empty})
    {
        Expect(PutClipboard({}, kLiteralDibV5), "publish.preempty-fixture-installed");
        ClipboardTransfer::PreparedImage prepared = Prepared();
        Control control;
        control.faultAt = fault;
        const auto result = fault == ClipboardTransfer::Fault::Open
                                ? ClipboardTransfer::Publish(prepared, {}, Observer(control))
                                : PublishWithBusyRetry(prepared, {}, Observer(control));
        Expect((fault == ClipboardTransfer::Fault::Open ? result.status == ClipboardTransfer::Status::Busy
                                                        : result.status == ClipboardTransfer::Status::NativeFailure) &&
                   !result.emptied && !result.pngPlaced && !result.dibPlaced && prepared &&
                   control.injected[static_cast<std::size_t>(fault)] == 1,
               fault == ClipboardTransfer::Fault::Window ? "publish.inject-window-retains-input"
               : fault == ClipboardTransfer::Fault::Open ? "publish.inject-open-busy-retains-input"
                                                         : "publish.inject-empty-retains-input");
        Expect(ReadClipboardPixels(kLiteralDibPixels, 2, 1), "publish.preempty-fault-preserves-foreign-content");
    }

    for (const auto fault : {ClipboardTransfer::Fault::Png, ClipboardTransfer::Fault::Dib})
    {
        ClipboardTransfer::PreparedImage prepared = Prepared();
        Control control;
        control.faultAt = fault;
        const auto result = PublishWithBusyRetry(prepared, {}, Observer(control));
        const bool expectedPng = fault != ClipboardTransfer::Fault::Png;
        const bool expectedDib = fault != ClipboardTransfer::Fault::Dib;
        Expect(result.status == ClipboardTransfer::Status::NativeFailure && result.emptied &&
                   result.pngPlaced == expectedPng && result.dibPlaced == expectedDib && !prepared &&
                   control.injected[static_cast<std::size_t>(fault)] == 1,
               fault == ClipboardTransfer::Fault::Png ? "publish.partial-dib-only-truth-and-consumed"
                                                      : "publish.partial-png-only-truth-and-consumed");
    }

    {
        ClipboardTransfer::PreparedImage prepared = Prepared();
        Control control;
        control.faultAt = ClipboardTransfer::Fault::Png;
        const auto result = PublishWithBusyRetry(prepared, {}, Observer(control));
        Expect(result.emptied && !result.pngPlaced && result.dibPlaced && !prepared,
               "publish.png-failure-still-attempts-dib");
    }
    {
        ClipboardTransfer::PreparedImage prepared = Prepared();
        Control control;
        control.faultAt = ClipboardTransfer::Fault::Png;
        control.faultAt2 = ClipboardTransfer::Fault::Dib;
        const auto result = PublishWithBusyRetry(prepared, {}, Observer(control));
        Expect(result.status == ClipboardTransfer::Status::NativeFailure && result.emptied && !result.pngPlaced &&
                   !result.dibPlaced && !prepared,
               "publish.both-format-failures-report-empty-truth-and-consume");
    }
    {
        ClipboardTransfer::PreparedImage prepared = Prepared();
        Control control;
        control.faultAt = ClipboardTransfer::Fault::Png;
        control.faultError = ERROR_NOT_ENOUGH_MEMORY;
        const auto result = PublishWithBusyRetry(prepared, {}, Observer(control));
        Expect(result.emptied && !result.pngPlaced && result.dibPlaced && result.nativeError == ERROR_NOT_ENOUGH_MEMORY,
               "publish.first-native-error-retained");
    }

    {
        ClipboardTransfer::PreparedImage prepared = Prepared();
        CancelSource stop;
        (void)stop.Cancel();
        const auto result = ClipboardTransfer::Publish(prepared, stop.Token());
        Expect(result.status == ClipboardTransfer::Status::Cancelled && !result.emptied && prepared,
               "publish.precancel-retains-input-and-does-not-empty");
    }
    {
        Expect(PutClipboard({}, kLiteralDibV5), "publish.opened-cancel-original-installed");
        auto prepared = Prepared();
        CancelSource stop;
        Control control;
        control.cancel = &stop;
        control.cancelAt = ClipboardTransfer::Stage::Opened;
        const auto result = PublishWithBusyRetry(prepared, stop.Token(), Observer(control));
        Expect(result.status == ClipboardTransfer::Status::Cancelled && !result.emptied && prepared &&
                   control.reached[static_cast<std::size_t>(ClipboardTransfer::Stage::Opened)] == 1 &&
                   ReadClipboardPixels(kLiteralDibPixels, 2, 1),
               "publish.opened-cancel-before-empty-keeps-input-and-original-os-pixels");
    }
    for (const auto stage :
         {ClipboardTransfer::Stage::Emptied, ClipboardTransfer::Stage::PngPlaced, ClipboardTransfer::Stage::DibPlaced})
    {
        ClipboardTransfer::PreparedImage prepared = Prepared();
        CancelSource stop;
        Control control;
        control.cancelAt = stage;
        control.cancel = &stop;
        const auto result = PublishWithBusyRetry(prepared, stop.Token(), Observer(control));
        Expect(result.status == ClipboardTransfer::Status::Ok && result.emptied && result.pngPlaced &&
                   result.dibPlaced && !prepared,
               stage == ClipboardTransfer::Stage::Emptied     ? "publish.late-cancel-at-empty-finishes-truthfully"
               : stage == ClipboardTransfer::Stage::PngPlaced ? "publish.late-cancel-at-png-finishes-truthfully"
                                                              : "publish.late-cancel-at-dib-finishes-truthfully");
    }
}

void TestSequenceAndRetry()
{
    DWORD oldSequence = 0;
    Expect(SequenceWhileOpen(oldSequence), "sequence.expected-value-read-inside-open");
    ClipboardTransfer::PreparedImage prepared = PreparedAt(oldSequence);
    ClipboardTransfer::PreparedImage moved = std::move(prepared);
    Expect(!prepared && moved && moved.SourceSequence() == oldSequence,
           "sequence.move-preserves-sealed-source-authority");
    Expect(PutClipboard({}, kLiteralDibV5), "sequence.foreign-replacement-setup");
    const auto stale = PublishWithBusyRetry(moved);
    Expect(stale.status == ClipboardTransfer::Status::Stale && !stale.emptied && moved &&
               moved.SourceSequence() == oldSequence,
           "sequence.expected-before-empty-refuses-foreign-sequence");
    Expect(ReadClipboardPixels(kLiteralDibPixels, 2, 1), "sequence.stale-preserves-exact-foreign-content");
    const auto retry = PublishWithBusyRetry(moved);
    Expect(retry.status == ClipboardTransfer::Status::Stale && !retry.emptied && moved &&
               moved.SourceSequence() == oldSequence,
           "sequence.stale-retry-cannot-renew-old-authority");

    ClipboardTransfer::PreparedImage fresh = Prepared();
    const std::uint32_t sourceSequence = fresh.SourceSequence();
    Control heldControl;
    const auto held = PublishWithBusyRetry(fresh, {}, Observer(heldControl));
    Expect(held.status == ClipboardTransfer::Status::Ok && held.sequence != sourceSequence,
           "sequence.publication-returns-held-sequence");
    if (OpenClipboardRetry(gOwner))
    {
        const DWORD insideOpen = GetClipboardSequenceNumber();
        (void)CloseClipboard();
        Expect(heldControl.reached[static_cast<std::size_t>(ClipboardTransfer::Stage::DibPlaced)] == 1 &&
                   held.sequence == heldControl.dibPlacedSequence,
               "sequence.reported-value-is-exact-after-last-placement-while-held");
        std::printf("DIAG clipboardtransfer sequence held=%lu later-open=%lu\n", held.sequence, insideOpen);
    }
    else
        Expect(false, "sequence.open-for-held-value");
}

struct ClipboardHolder final
{
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    std::atomic<bool> opened{false};
};

DWORD WINAPI HoldClipboard(void* raw) noexcept
{
    auto& state = *static_cast<ClipboardHolder*>(raw);
    wil::unique_hwnd owner(
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr));
    const bool opened = owner && OpenClipboardRetry(owner.get());
    state.opened.store(opened, std::memory_order_release);
    (void)SetEvent(state.entered);
    if (opened)
    {
        (void)WaitForSingleObject(state.release, 5000);
        (void)CloseClipboard();
    }
    return opened ? 0 : 1;
}

void RunDelayedProducerCleanup(DelayedProducer& producer, HANDLE thread, HANDLE release) noexcept
{
    (void)SetEvent(release);
    const HWND window = producer.window.load(std::memory_order_acquire);
    if (window)
        (void)PostMessageW(window, WM_CLOSE, 0, 0);
    else
    {
        const DWORD threadId = producer.threadId.load(std::memory_order_acquire);
        if (threadId != 0)
            (void)PostThreadMessageW(threadId, WM_QUIT, 0, 0);
    }
    JoinNativeThread(thread, "delayed-render-producer");
}

void TestActualOpenBusy()
{
    ClipboardTransfer::PreparedImage prepared = Prepared();
    wil::unique_handle entered(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    wil::unique_handle release(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!entered || !release)
    {
        Expect(false, "busy-holder.events-created");
        return;
    }
    ClipboardHolder state{entered.get(), release.get()};
    wil::unique_handle holder(CreateThread(nullptr, 0, HoldClipboard, &state, 0, nullptr));
    if (!holder)
    {
        Expect(false, "busy-holder.thread-created");
        return;
    }
    const DWORD holdWait = WaitForSingleObject(entered.get(), 5000);
    Expect(holdWait == WAIT_OBJECT_0 && state.opened.load(std::memory_order_acquire),
           "busy-holder.other-thread-open-established");
    const auto result = ClipboardTransfer::Publish(prepared);
    Expect(result.status == ClipboardTransfer::Status::Busy && !result.emptied && !result.pngPlaced &&
               !result.dibPlaced && prepared,
           "busy-holder.publish-returns-busy-retains-input-without-empty");
    (void)SetEvent(release.get());
    JoinNativeThread(holder.get(), "clipboard-holder");
    Expect(true, "busy-holder.release-and-close-acknowledged");
}

struct ConcurrentInspection final
{
    HANDLE entered;
    HANDLE release;
    std::atomic<DWORD> windowThread{0}, releaseWait{WAIT_FAILED};
    ClipboardTransfer::Inspection inspection;
    ClipboardTransfer::Status status = ClipboardTransfer::Status::Invalid;
};

DWORD WINAPI HoldInspection(void* raw) noexcept
{
    auto& state = *static_cast<ConcurrentInspection*>(raw);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com))
        return 1;
    const auto uninitialize = wil::scope_exit([]() noexcept { CoUninitialize(); });
    const ClipboardTransfer::Observer observer{
        &state,
        [](void* context, ClipboardTransfer::Stage stage) noexcept
        {
            if (stage != ClipboardTransfer::Stage::Opened)
                return;
            auto& held = *static_cast<ConcurrentInspection*>(context);
            DWORD process = 0;
            const auto thread = GetWindowThreadProcessId(GetOpenClipboardWindow(), &process);
            held.windowThread = process == GetCurrentProcessId() && thread == GetCurrentThreadId() ? thread : 0;
            (void)SetEvent(held.entered);
            held.releaseWait = WaitForSingleObject(held.release, 5000);
        },
        nullptr, NativeFailureDiagnostic};
    state.status = InspectWithBusyRetry(2, state.inspection, {}, observer);
    return 0;
}

void TestConcurrentReadScopes()
{
    Expect(PutClipboard({}, kLiteralDibV5), "concurrent.distinct-worker-window-fixture-installed");
    ClipboardTransfer::Inspection previous;
    Expect(InspectWithBusyRetry(2, previous) == ClipboardTransfer::Status::Ok && previous,
           "concurrent.copy-authority-prepared-before-held-reader");
    wil::unique_handle entered(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    wil::unique_handle release(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!entered || !release)
    {
        Expect(false, "concurrent.events-created");
        return;
    }
    ConcurrentInspection held{entered.get(), release.get()};
    wil::unique_handle reader(CreateThread(nullptr, 0, HoldInspection, &held, 0, nullptr));
    if (!reader)
    {
        Expect(false, "concurrent.reader-created");
        return;
    }
    const DWORD reached = WaitForSingleObject(entered.get(), 5000);
    Expect(reached == WAIT_OBJECT_0 && held.windowThread.load() != 0,
           "concurrent.first-open-has-exact-worker-owned-window");
    unsigned opens = 0;
    const ClipboardTransfer::Observer observer{&opens, nullptr,
                                               [](void* raw, ClipboardTransfer::Fault fault) noexcept -> std::uint32_t
                                               {
                                                   if (fault == ClipboardTransfer::Fault::Open)
                                                       ++*static_cast<unsigned*>(raw);
                                                   return 0;
                                               },
                                               NativeFailureDiagnostic};
    ClipboardTransfer::Inspection second;
    ClipboardTransfer::ImageCopy copied;
    const auto inspected = ClipboardTransfer::Inspect(2, second, {}, observer);
    const auto copyStatus = ClipboardTransfer::Copy(previous, copied, {}, observer);
    Expect(inspected == ClipboardTransfer::Status::Busy && !second && copyStatus == ClipboardTransfer::Status::Busy &&
               copied.RetainedBytes() == 0 && opens == 2,
           "concurrent.inspect-and-copy-each-refuse-one-native-acquisition-with-empty-output");
    (void)SetEvent(release.get());
    JoinNativeThread(reader.get(), "isolated-clipboard-reader");
    Expect(held.releaseWait == WAIT_OBJECT_0 && held.status == ClipboardTransfer::Status::Ok && held.inspection &&
               held.inspection.Dib().width == 2 && held.inspection.Dib().height == 1,
           "concurrent.original-scope-keeps-valid-enumeration-through-its-own-close");
    Control failure;
    failure.faultAt = ClipboardTransfer::Fault::Window;
    Expect(ClipboardTransfer::Inspect(2, second, {}, Observer(failure)) == ClipboardTransfer::Status::NativeFailure &&
               !second && failure.reached[static_cast<std::size_t>(ClipboardTransfer::Stage::Opened)] == 0,
           "concurrent.inspect-window-refusal-does-not-open-or-publish");
    Expect(ClipboardTransfer::Copy(previous, copied, {}, Observer(failure)) ==
                   ClipboardTransfer::Status::NativeFailure &&
               copied.RetainedBytes() == 0 &&
               failure.reached[static_cast<std::size_t>(ClipboardTransfer::Stage::Opened)] == 0,
           "concurrent.copy-window-refusal-does-not-open-or-publish");
}

void TestInspectCopyDecode()
{
    Expect(PutClipboard(kLiteralPng, kLiteralDibV5), "inspect.png-and-dib-fixtures-installed");
    ClipboardTransfer::Inspection inspected;
    Expect(InspectWithBusyRetry(6, inspected) == ClipboardTransfer::Status::Ok && inspected &&
               inspected.Png().width == 2 && inspected.Png().height == 3 && inspected.Dib().width == 2 &&
               inspected.Dib().height == 1 && inspected.CopyAndPixelsBytes() > 0,
           "inspect.both-valid-priority-metadata-and-owned-bound");
    ClipboardTransfer::ImageCopy copied;
    Expect(CopyWithBusyRetry(inspected, copied) == ClipboardTransfer::Status::Ok && copied.RetainedBytes() > 0,
           "copy.valid-png-and-dib-owned-bytes");
    Raster decoded;
    Expect(ClipboardTransfer::Decode(copied, decoded) == ClipboardTransfer::Status::Ok && decoded.Width() == 2 &&
               decoded.Height() == 3 && std::equal(kLiteralPngPixels.begin(), kLiteralPngPixels.end(), decoded.Data()),
           "decode.png-priority-exact-literal-pixels");

    // Corrupt the PNG signature while retaining the independently valid DIB. Inspection must retain the DIB
    // fallback, and Decode may fall back from the copied invalid PNG to that DIB.
    auto badPng = kLiteralPng;
    badPng[0] = 0;
    Expect(PutClipboard(badPng, kLiteralDibV5), "inspect.invalid-png-valid-dib-fixtures-installed");
    ClipboardTransfer::Inspection fallback;
    ClipboardTransfer::ImageCopy fallbackCopy;
    Raster dibDecoded;
    Expect(InspectWithBusyRetry(2, fallback) == ClipboardTransfer::Status::Ok && !fallback.Png().pixels &&
               fallback.Dib().pixels == 2 &&
               CopyWithBusyRetry(fallback, fallbackCopy) == ClipboardTransfer::Status::Ok &&
               ClipboardTransfer::Decode(fallbackCopy, dibDecoded) == ClipboardTransfer::Status::Ok &&
               dibDecoded.Width() == 2 && dibDecoded.Height() == 1 &&
               std::equal(kLiteralDibPixels.begin(), kLiteralDibPixels.end(), dibDecoded.Data()),
           "inspect.invalid-png-valid-dib-fallback-literal-pixels");

    ClipboardTransfer::Inspection exact;
    ClipboardTransfer::Inspection lower;
    ClipboardTransfer::Inspection hard;
    ClipboardTransfer::Inspection invalid;
    Expect(InspectWithBusyRetry(2, exact) == ClipboardTransfer::Status::Ok &&
               InspectWithBusyRetry(1, lower) == ClipboardTransfer::Status::TooLarge && !lower &&
               InspectWithBusyRetry(0, hard) == ClipboardTransfer::Status::TooLarge && !hard &&
               InspectWithBusyRetry(-1, invalid) == ClipboardTransfer::Status::TooLarge && !invalid,
           "inspect.pixel-budget-exact-lower-zero-and-negative");
    const auto oversized = LargeDibHeader();
    Expect(PutClipboard({}, oversized) &&
               InspectWithBusyRetry(kMaxLayerPixels, hard) == ClipboardTransfer::Status::TooLarge && !hard,
           "inspect.independent-hard-pixel-limit");

    Expect(PutClipboard({}, kLiteralDibV5), "inspect.int64-max-small-dib-installed");
    ClipboardTransfer::Inspection maximumBudget;
    ClipboardTransfer::ImageCopy maximumCopy;
    Raster maximumRaster;
    Expect(InspectWithBusyRetry((std::numeric_limits<std::int64_t>::max)(), maximumBudget) ==
                   ClipboardTransfer::Status::Ok &&
               maximumBudget.Dib().pixels == 2 &&
               CopyWithBusyRetry(maximumBudget, maximumCopy) == ClipboardTransfer::Status::Ok &&
               ClipboardTransfer::Decode(maximumCopy, maximumRaster) == ClipboardTransfer::Status::Ok &&
               maximumRaster.Width() == 2 && maximumRaster.Height() == 1 &&
               std::equal(kLiteralDibPixels.begin(), kLiteralDibPixels.end(), maximumRaster.Data()),
           "inspect.copy.decode-int64-max-clamps-with-small-valid-control");

    Expect(PutClipboard(kLiteralPng, kLiteralDibV5), "copy.stale-fixture-restored");
    ClipboardTransfer::Inspection stale;
    Expect(InspectWithBusyRetry(6, stale) == ClipboardTransfer::Status::Ok, "copy.stale-inspection-created");
    Expect(PutClipboard({}, kLiteralDibV5), "copy.stale-foreign-replacement-installed");
    ClipboardTransfer::ImageCopy staleOutput;
    struct StaleObservation final
    {
        DWORD openedSequence = 0;
        unsigned opens = 0;
    } staleObservation;
    const ClipboardTransfer::Observer staleObserver{&staleObservation,
                                                    [](void* raw, ClipboardTransfer::Stage stage) noexcept
                                                    {
                                                        if (stage == ClipboardTransfer::Stage::Opened)
                                                        {
                                                            auto& observation = *static_cast<StaleObservation*>(raw);
                                                            observation.openedSequence = GetClipboardSequenceNumber();
                                                            ++observation.opens;
                                                        }
                                                    },
                                                    nullptr};
    const auto staleStatus = CopyWithBusyRetry(stale, staleOutput, {}, staleObserver);
    if (staleStatus != ClipboardTransfer::Status::Stale || staleOutput.RetainedBytes() != 0)
        std::printf("DIAG clipboardtransfer.copy-stale status=%u inspectedSequence=%lu openedSequence=%lu "
                    "opens=%u retained=%llu\n",
                    static_cast<unsigned>(staleStatus), static_cast<unsigned long>(stale.Sequence()),
                    staleObservation.openedSequence, staleObservation.opens,
                    static_cast<unsigned long long>(staleOutput.RetainedBytes()));
    Expect(staleStatus == ClipboardTransfer::Status::Stale && staleOutput.RetainedBytes() == 0,
           "copy.foreign-replacement-refused-empty-output");

    Expect(PutClipboard({}, kLiteralDibV5), "copy.immutable-fixture-installed");
    ClipboardTransfer::Inspection stable;
    ClipboardTransfer::Inspection movedStable;
    ClipboardTransfer::ImageCopy stableCopy;
    const bool inspectedStable = InspectWithBusyRetry(2, stable) == ClipboardTransfer::Status::Ok;
    movedStable = std::move(stable);
    ClipboardTransfer::ImageCopy movedFromOutput;
    Expect(inspectedStable && !stable &&
               CopyWithBusyRetry(stable, movedFromOutput) == ClipboardTransfer::Status::Invalid &&
               movedFromOutput.RetainedBytes() == 0 &&
               CopyWithBusyRetry(movedStable, stableCopy) == ClipboardTransfer::Status::Ok,
           "copy.stable-fixture-copied");
    Expect(PutClipboard({}, kLiteralPng), "copy.foreign-replacement-after-copy");
    Raster retained;
    Expect(ClipboardTransfer::Decode(stableCopy, retained) == ClipboardTransfer::Status::Ok && retained.Width() == 2 &&
               retained.Height() == 1 &&
               std::equal(kLiteralDibPixels.begin(), kLiteralDibPixels.end(), retained.Data()),
           "copy.owned-bytes-decode-after-foreign-replacement");
}

void TestInspectionCancellation()
{
    Expect(PutClipboard({}, kLiteralDibV5), "cancel.inspect-fixture-installed");
    CancelSource pre;
    (void)pre.Cancel();
    ClipboardTransfer::Inspection output;
    Expect(InspectWithBusyRetry(2, output, pre.Token()) == ClipboardTransfer::Status::Cancelled && !output,
           "inspect.precancel-no-output");

    ClipboardTransfer::Inspection inspected;
    Expect(InspectWithBusyRetry(2, inspected) == ClipboardTransfer::Status::Ok, "cancel.copy-inspection-ready");
    CancelSource stop;
    Control control;
    control.cancelAt = ClipboardTransfer::Stage::Copied;
    control.cancel = &stop;
    ClipboardTransfer::ImageCopy cancelled;
    Expect(CopyWithBusyRetry(inspected, cancelled, stop.Token(), Observer(control)) ==
                   ClipboardTransfer::Status::Cancelled &&
               cancelled.RetainedBytes() == 0 &&
               control.reached[static_cast<std::size_t>(ClipboardTransfer::Stage::Copied)] == 1,
           "copy.stage-copied-cancel-discards-output-after-complete-copy");
}

void TestReadFaultFallbacks()
{
    Expect(PutClipboard(kLiteralPng, kLiteralDibV5), "readfault.two-valid-formats-installed");
    ClipboardTransfer::Inspection inspected;
    Expect(InspectWithBusyRetry(6, inspected) == ClipboardTransfer::Status::Ok && inspected,
           "readfault.two-format-inspection-ready");

    Control pngReadFailure;
    pngReadFailure.faultAt = ClipboardTransfer::Fault::PngRead;
    ClipboardTransfer::ImageCopy dibFallback;
    Raster dibRaster;
    const auto fallbackStatus = CopyWithBusyRetry(inspected, dibFallback, {}, Observer(pngReadFailure));
    Expect(fallbackStatus == ClipboardTransfer::Status::Ok && dibFallback.RetainedBytes() > 0 &&
               ClipboardTransfer::Decode(dibFallback, dibRaster) == ClipboardTransfer::Status::Ok &&
               dibRaster.Width() == 2 && dibRaster.Height() == 1 &&
               std::equal(kLiteralDibPixels.begin(), kLiteralDibPixels.end(), dibRaster.Data()),
           "readfault.png-nativefailure-falls-back-to-inspected-valid-dib");

    Control dibReadFailure;
    dibReadFailure.faultAt = ClipboardTransfer::Fault::DibRead;
    ClipboardTransfer::ImageCopy pngOnly;
    Raster pngRaster;
    const auto pngOnlyStatus = CopyWithBusyRetry(inspected, pngOnly, {}, Observer(dibReadFailure));
    Expect(pngOnlyStatus == ClipboardTransfer::Status::Ok && pngOnly.RetainedBytes() > 0 &&
               ClipboardTransfer::Decode(pngOnly, pngRaster) == ClipboardTransfer::Status::Ok &&
               pngRaster.Width() == 2 && pngRaster.Height() == 3,
           "readfault.dib-nativefailure-keeps-valid-copied-png");

    Control bothReadFailures;
    bothReadFailures.faultAt = ClipboardTransfer::Fault::PngRead;
    bothReadFailures.faultAt2 = ClipboardTransfer::Fault::DibRead;
    ClipboardTransfer::ImageCopy empty;
    Expect(CopyWithBusyRetry(inspected, empty, {}, Observer(bothReadFailures)) ==
                   ClipboardTransfer::Status::NativeFailure &&
               empty.RetainedBytes() == 0,
           "readfault.both-nativefailures-return-empty-nativefailure");

    Expect(PutClipboard(kLiteralPng, {}), "readfault.png-only-fixture-installed");
    ClipboardTransfer::Inspection pngOnlyInspection;
    ClipboardTransfer::ImageCopy pngOnlyCopy;
    Control absentDibFailure;
    absentDibFailure.faultAt = ClipboardTransfer::Fault::DibRead;
    const auto pngOnlyInspectStatus = InspectWithBusyRetry(6, pngOnlyInspection);
    const auto pngOnlyCopyStatus =
        pngOnlyInspectStatus == ClipboardTransfer::Status::Ok
            ? CopyWithBusyRetry(pngOnlyInspection, pngOnlyCopy, {}, Observer(absentDibFailure))
            : pngOnlyInspectStatus;
    Raster pngOnlyRaster;
    Expect(pngOnlyInspectStatus == ClipboardTransfer::Status::Ok && !pngOnlyInspection.Dib().pixels &&
               pngOnlyCopyStatus == ClipboardTransfer::Status::Ok && pngOnlyCopy.RetainedBytes() > 0 &&
               ClipboardTransfer::Decode(pngOnlyCopy, pngOnlyRaster) == ClipboardTransfer::Status::Ok &&
               pngOnlyRaster.Width() == 2 && pngOnlyRaster.Height() == 3,
           "readfault.missing-dib-does-not-discard-valid-png");
}

struct InspectRequest final
{
    ClipboardTransfer::Status status = ClipboardTransfer::Status::Invalid;
    ClipboardTransfer::Inspection inspection;
    HANDLE complete = nullptr;
};

DWORD WINAPI InspectWorker(void* raw) noexcept
{
    auto& request = *static_cast<InspectRequest*>(raw);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(initialized))
    {
        try
        {
            request.status = InspectWithBusyRetry(6, request.inspection);
        }
        catch (const std::bad_alloc&)
        {
            request.status = ClipboardTransfer::Status::OutOfMemory;
        }
        CoUninitialize();
    }
    (void)SetEvent(request.complete);
    return 0;
}

void TestDelayedRenderer()
{
    // Leave immediate formats under a destroyed private owner first. A foreign EmptyClipboard sends
    // WM_DESTROYCLIPBOARD to the prior live owner; this coordinator does not pump that window while waiting
    // for the producer's ready event. Publication closes/destroys its private window before returning.
    auto immediate = Prepared();
    const auto immediateResult = PublishWithBusyRetry(immediate);
    Expect(immediateResult.status == ClipboardTransfer::Status::Ok && immediateResult.emptied &&
               immediateResult.pngPlaced && immediateResult.dibPlaced,
           "delayed.previous-live-test-owner-retired-before-foreign-empty");
    if (immediateResult.status != ClipboardTransfer::Status::Ok)
        return;
    wil::unique_handle entered(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    wil::unique_handle release(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    wil::unique_handle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!entered || !release || !ready)
    {
        Expect(false, "delayed.events-created");
        return;
    }
    DelayedProducer producer{entered.get(), release.get(), ready.get()};
    wil::unique_handle ownerThread(CreateThread(nullptr, 0, DelayedProducerMain, &producer, 0, nullptr));
    if (!ownerThread)
    {
        Expect(false, "delayed.owner-thread-created");
        return;
    }
    const DWORD readyWait = WaitForSingleObject(ready.get(), 5000);
    Expect(readyWait == WAIT_OBJECT_0 && producer.installed.load(std::memory_order_acquire),
           "delayed.formats-advertised-without-rendering");
    if (readyWait != WAIT_OBJECT_0 || !producer.installed.load(std::memory_order_acquire))
    {
        RunDelayedProducerCleanup(producer, ownerThread.get(), release.get());
        return;
    }

    wil::unique_handle apiDone(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!apiDone)
    {
        RunDelayedProducerCleanup(producer, ownerThread.get(), release.get());
        Expect(false, "delayed.api-completion-event-created");
        return;
    }
    InspectRequest request;
    request.complete = apiDone.get();
    wil::unique_handle apiWorker(CreateThread(nullptr, 0, InspectWorker, &request, 0, nullptr));
    if (!apiWorker)
    {
        RunDelayedProducerCleanup(producer, ownerThread.get(), release.get());
        Expect(false, "delayed.api-worker-created");
        return;
    }

    const DWORD enteredWait = WaitForSingleObject(entered.get(), 5000);
    Expect(enteredWait == WAIT_OBJECT_0, "delayed.first-getclipboarddata-reaches-renderer-gate");
    const bool blockedAtGate = enteredWait == WAIT_OBJECT_0 && WaitForSingleObject(apiDone.get(), 0) == WAIT_TIMEOUT;
    Expect(blockedAtGate, "delayed.api-worker-pending-while-renderer-is-held");
    // This coordinator is the clipboard test worker. It remains responsive while the separate MTA API worker is
    // blocked in GetClipboardData and now releases the external producer explicitly.
    const bool released = SetEvent(release.get()) != FALSE;
    Expect(released, "delayed.external-release-signalled");
    const DWORD apiWait = WaitForSingleObject(apiDone.get(), 5000);
    Expect(apiWait == WAIT_OBJECT_0, "delayed.api-worker-returns-after-external-release");
    if (apiWait != WAIT_OBJECT_0)
        FailFastJoin("delayed-api-worker");
    JoinNativeThread(apiWorker.get(), "delayed-api-worker");
    const HWND producerWindow = producer.window.load(std::memory_order_acquire);
    if (producerWindow)
        (void)PostMessageW(producerWindow, WM_CLOSE, 0, 0);
    JoinNativeThread(ownerThread.get(), "delayed-render-producer");
    Expect(true, "delayed.producer-thread-closed");
    if (request.status != ClipboardTransfer::Status::Ok || !request.inspection || request.inspection.Sequence() == 0 ||
        request.inspection.Png().width != 2 || request.inspection.Dib().width != 2 || producer.renderCalls.load() < 2)
        std::printf("DIAG clipboardtransfer.delayed.inspect status=%u valid=%d sequence=%lu current=%lu "
                    "png=%dx%d dib=%dx%d renders=%u png-renders=%u dib-renders=%u wait=%lu render-error=%lu\n",
                    static_cast<unsigned int>(request.status), static_cast<bool>(request.inspection),
                    request.inspection.Sequence(), GetClipboardSequenceNumber(), request.inspection.Png().width,
                    request.inspection.Png().height, request.inspection.Dib().width, request.inspection.Dib().height,
                    producer.renderCalls.load(), producer.pngRenders.load(), producer.dibRenders.load(),
                    producer.renderWait.load(), producer.renderError.load());
    Expect(request.status == ClipboardTransfer::Status::Ok && request.inspection &&
               request.inspection.Sequence() != 0 && request.inspection.Png().width == 2 &&
               request.inspection.Dib().width == 2 && producer.renderCalls.load(std::memory_order_relaxed) >= 2,
           "delayed.inspect-captures-sequence-after-both-rendered-formats");
    ClipboardTransfer::ImageCopy copied;
    const auto copyStatus = request.status == ClipboardTransfer::Status::Ok
                                ? CopyWithBusyRetry(request.inspection, copied)
                                : ClipboardTransfer::Status::Invalid;
    Expect(copyStatus == ClipboardTransfer::Status::Ok && copied.RetainedBytes() > 0,
           "delayed.copy-uses-post-render-inspection-sequence");
    if (copyStatus != ClipboardTransfer::Status::Ok || copied.RetainedBytes() == 0)
        std::printf("DIAG clipboardtransfer.delayed.copy status=%u inspected-sequence=%lu current=%lu bytes=%llu\n",
                    static_cast<unsigned int>(copyStatus), request.inspection.Sequence(), GetClipboardSequenceNumber(),
                    static_cast<unsigned long long>(copied.RetainedBytes()));
    Raster delayedPixels;
    Expect(copyStatus == ClipboardTransfer::Status::Ok &&
               ClipboardTransfer::Decode(copied, delayedPixels) == ClipboardTransfer::Status::Ok &&
               delayedPixels.Width() == 2 && delayedPixels.Height() == 3 &&
               std::equal(kLiteralPngPixels.begin(), kLiteralPngPixels.end(), delayedPixels.Data()),
           "delayed.copy-decodes-independent-png-pixels");
}

int RunWorker()
{
    gFailures = 0;
    gExpectations = 0;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com))
    {
        std::printf("FAIL clipboardtransfer.worker-mta-initialize hr=0x%08lx\n", static_cast<unsigned long>(com));
        return 1;
    }
    auto uninitialize = wil::scope_exit([&]() noexcept { CoUninitialize(); });
    gOwner =
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!gOwner)
    {
        std::printf("FAIL clipboardtransfer.owner-window error=%lu\n", GetLastError());
        return 1;
    }
    wil::unique_hwnd owner(gOwner);
    SystemClipboard::SavedContents saved;
    if (!SystemClipboard::SaveContents(gOwner, saved))
    {
        std::printf("FAIL clipboardtransfer.save-user-clipboard error=%lu\n", GetLastError());
        return 1;
    }
    bool restoreAttempted = false;
    auto restoreUser = wil::scope_exit(
        [&]() noexcept
        {
            if (restoreAttempted)
                return;
            restoreAttempted = true;
            if (!SystemClipboard::RestoreContents(gOwner, saved))
                Expect(false, "clipboard.restore-user-contents");
        });

    TestFixtureRetryPolicy();
    TestPreparation();
    TestPublishOutcomes();
    TestSequenceAndRetry();
    TestActualOpenBusy();
    TestConcurrentReadScopes();
    TestInspectCopyDecode();
    TestInspectionCancellation();
    TestReadFaultFallbacks();
    TestDelayedRenderer();
    gFailures += RunClipboardBindingTransferTests(gOwner);
    restoreAttempted = true;
    Expect(SystemClipboard::RestoreContents(gOwner, saved), "clipboard.restore-user-contents");
    std::printf("clipboardtransfer summary: %u checks, %d failures\n", gExpectations, gFailures);
    return gFailures == 0 ? 0 : 1;
}

DWORD WINAPI RunWorkerEntry(void* raw) noexcept
{
    try
    {
        *static_cast<int*>(raw) = RunWorker();
    }
    catch (const std::bad_alloc&)
    {
        std::printf("FAIL clipboardtransfer worker allocation failure\n");
    }
    return 0;
}
} // namespace

int RunClipboardTransferTests()
{
    int result = 1;
    wil::unique_handle worker(CreateThread(nullptr, 0, RunWorkerEntry, &result, 0, nullptr));
    if (!worker)
    {
        std::printf("FAIL clipboardtransfer.test-worker-created error=%lu\n", GetLastError());
        return 1;
    }
    // This bound covers the whole suite, including each bounded contention/delayed-render control.
    // Individual foreign producer/API joins retain their five-second failure bounds.
    if (WaitForSingleObject(worker.get(), 60'000) != WAIT_OBJECT_0)
        FailFastJoin("clipboard-transfer-test-worker");
    return result;
}
