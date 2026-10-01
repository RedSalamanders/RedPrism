#include "ClipboardAvailabilityTests.h"

#include "ClipboardTransfer.h"
#include "SystemClipboard.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <span>
#include <stop_token>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
unsigned gChecks = 0;
HWND gOwner = nullptr;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s clipboardavailability.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

UINT PngFormat() noexcept
{
    return RegisterClipboardFormatW(L"PNG");
}

bool HarnessWait(HANDLE timer) noexcept
{
    LARGE_INTEGER due{};
    due.QuadPart = -1'000'000; // bounded one-shot fixture wait; never used by the production adapter
    return SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) &&
           WaitForSingleObject(timer, 200) == WAIT_OBJECT_0;
}
bool OpenFixture() noexcept
{
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return false;
    for (unsigned int attempt = 0; attempt < 4; ++attempt)
    {
        if (OpenClipboard(gOwner))
            return true;
        if (attempt == 3 || !HarnessWait(timer.get()))
            return false;
    }
    return false;
}
ClipboardTransfer::Status InspectSettled(std::int64_t limit, ClipboardTransfer::Inspection& output,
                                         CancelToken cancellation = {}, ClipboardTransfer::Observer observer = {},
                                         ClipboardTransfer::Availability* availability = nullptr)
{
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return ClipboardTransfer::Status::NativeFailure;
    for (unsigned int attempt = 0; attempt < 4; ++attempt)
    {
        const auto status = ClipboardTransfer::Inspect(limit, output, cancellation, observer, availability);
        if (status != ClipboardTransfer::Status::Busy || attempt == 3 || !HarnessWait(timer.get()))
            return status;
    }
    return ClipboardTransfer::Status::Busy;
}

// Literal BITMAPINFOHEADER plus one opaque 32-bit BI_RGB pixel, in BGRA byte order.
constexpr std::array<std::uint8_t, 44> kDib{40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 32, 0, 0, 0, 0, 0,  4,   0,
                                            0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 9, 20, 140, 255};

// Literal BITMAPV5HEADER with explicit RGBA masks and one different opaque BGRA pixel.
constexpr std::array<std::uint8_t, 128> kDibV5{124, 0, 0, 0, 1, 0, 0, 0,   1,  0,  0,   0,  1, 0,   32, 0,
                                               3,   0, 0, 0, 4, 0, 0, 0,   0,  0,  0,   0,  0, 0,   0,  0,
                                               0,   0, 0, 0, 0, 0, 0, 0,   0,  0,  255, 0,  0, 255, 0,  0,
                                               255, 0, 0, 0, 0, 0, 0, 255, 66, 71, 82,  115}; // Remaining endpoint,
                                                                                              // gamma, intent and
                                                                                              // profile fields are
                                                                                              // zero.
// The pixel bytes occupy offsets 124..127; spell out the full header padding independently.
constexpr std::array<std::uint8_t, 128> MakeV5LiteralWithPixel() noexcept
{
    auto bytes = kDibV5;
    bytes[124] = 7;
    bytes[125] = 80;
    bytes[126] = 190;
    bytes[127] = 255;
    return bytes;
}
constexpr auto kValidDibV5 = MakeV5LiteralWithPixel();

constexpr std::array<std::uint8_t, 128> kMalformedDibV5{124, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 32, 0};

constexpr std::array<std::uint8_t, 132> kOversizedDibV5Header{
    124, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 1,   0, 32, 0,   3, 0, 0,   0, 8, 0, 0, 0, 0, 0,   0,  0,  0,  0,
    0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0,  255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 255, 66, 71, 82, 115};

constexpr std::array<std::uint8_t, 132> MakeOversizedV5Literal() noexcept
{
    auto bytes = kOversizedDibV5Header;
    bytes[124] = 1;
    bytes[125] = 2;
    bytes[126] = 3;
    bytes[127] = 255;
    bytes[128] = 4;
    bytes[129] = 5;
    bytes[130] = 6;
    bytes[131] = 255;
    return bytes;
}
constexpr auto kOversizedValidDibV5 = MakeOversizedV5Literal();

constexpr std::array<std::uint8_t, 8> kMalformedPng{0x89, 'P', 'N', 'G', 0, 0, 0, 0};

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

bool SetTextOnlyClipboard() noexcept
{
    constexpr std::array<std::uint8_t, 6> text{'x', 0, 0, 0, 0, 0};
    if (!OpenFixture())
        return false;
    auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
    return EmptyClipboard() && SetGlobal(CF_UNICODETEXT, text);
}

bool SetClipboard(std::span<const std::uint8_t> png, std::span<const std::uint8_t> v5,
                  std::span<const std::uint8_t> dib, bool dibFirst = false) noexcept
{
    if (!OpenFixture())
        return false;
    auto close = wil::scope_exit([]() noexcept { (void)CloseClipboard(); });
    if (!EmptyClipboard())
        return false;
    const UINT pngFormat = PngFormat();
    if (dibFirst && !dib.empty() && !SetGlobal(CF_DIB, dib))
        return false;
    return (png.empty() || (pngFormat != 0 && SetGlobal(pngFormat, png))) && (v5.empty() || SetGlobal(CF_DIBV5, v5)) &&
           (dibFirst || dib.empty() || SetGlobal(CF_DIB, dib));
}

struct Observation final
{
    std::uint32_t inspectedSequence = 0;
    unsigned inspectedStages = 0;
    unsigned dibReads = 0, v5Reads = 0;
    ClipboardTransfer::Fault failAt = static_cast<ClipboardTransfer::Fault>(255);
};

void Reached(void* raw, ClipboardTransfer::Stage stage) noexcept
{
    auto& observation = *static_cast<Observation*>(raw);
    if (stage == ClipboardTransfer::Stage::DibInspectRead)
        ++observation.dibReads;
    if (stage == ClipboardTransfer::Stage::DibV5InspectRead)
        ++observation.v5Reads;
    if (stage == ClipboardTransfer::Stage::Inspected)
    {
        ++observation.inspectedStages;
        observation.inspectedSequence = GetClipboardSequenceNumber();
    }
}

std::uint32_t InjectFault(void* raw, ClipboardTransfer::Fault fault) noexcept
{
    const auto& observation = *static_cast<Observation*>(raw);
    return fault == observation.failAt ? ERROR_ACCESS_DENIED : 0;
}

ClipboardTransfer::Observer MakeObserver(Observation& observation) noexcept
{
    return {&observation, Reached, InjectFault};
}

struct CloseControl final
{
    bool closedAtOpenStage = false;
};

void CloseDuringInspection(void* raw, ClipboardTransfer::Stage stage) noexcept
{
    if (stage == ClipboardTransfer::Stage::Opened)
        static_cast<CloseControl*>(raw)->closedAtOpenStage = CloseClipboard() != FALSE;
}

bool HasPixels(const ClipboardTransfer::Inspection& inspection, const std::array<std::uint8_t, 4>& expected)
{
    ClipboardTransfer::ImageCopy copy;
    wil::unique_handle timer(CreateWaitableTimerW(nullptr, TRUE, nullptr));
    if (!timer)
        return false;
    auto copied = ClipboardTransfer::Status::Busy;
    for (unsigned int attempt = 0; attempt < 4; ++attempt)
    {
        copied = ClipboardTransfer::Copy(inspection, copy);
        if (copied != ClipboardTransfer::Status::Busy || attempt == 3 || !HarnessWait(timer.get()))
            break;
    }
    if (copied != ClipboardTransfer::Status::Ok)
    {
        std::printf("clipboardavailability.pixel-copy-status=%u inspected-sequence=%u current-sequence=%lu\n",
                    static_cast<unsigned int>(copied), inspection.Sequence(), GetClipboardSequenceNumber());
        return false;
    }
    Raster decoded;
    const auto status = ClipboardTransfer::Decode(copy, decoded);
    const bool valid = status == ClipboardTransfer::Status::Ok && decoded.Width() == 1 && decoded.Height() == 1 &&
                       decoded.Format() == RasterFormat::Bgra8888 && decoded.Data() != nullptr;
    const bool matches = valid && std::equal(expected.begin(), expected.end(), decoded.Data());
    if (!matches)
        std::printf("clipboardavailability.pixel-decode-status=%u dimensions=%dx%d first-pixel=%u,%u,%u,%u\n",
                    static_cast<unsigned int>(status), decoded.Width(), decoded.Height(), valid ? decoded.Data()[0] : 0,
                    valid ? decoded.Data()[1] : 0, valid ? decoded.Data()[2] : 0, valid ? decoded.Data()[3] : 0);
    return matches;
}

void TestAvailabilityFacts()
{
    ClipboardTransfer::Inspection inspected;
    ClipboardTransfer::Availability available{9, true, true};
    const bool emptyInstalled = SetClipboard({}, {}, {});
    Observation emptyObservation;
    const auto emptyStatus = InspectSettled(1, inspected, {}, MakeObserver(emptyObservation), &available);
    Expect(emptyInstalled && emptyStatus == ClipboardTransfer::Status::Invalid && !inspected,
           "empty.status-invalid-without-inspection");
    Expect(available.observed && !available.hasImageFormats && available.sequence == emptyObservation.inspectedSequence,
           "empty.observed-no-image-format-at-held-inspected-sequence");

    const bool textInstalled = SetTextOnlyClipboard();
    available = {9, true, true};
    const auto textStatus = InspectSettled(1, inspected, {}, {}, &available);
    Expect(textInstalled && textStatus == ClipboardTransfer::Status::Invalid && !inspected && available.observed &&
               !available.hasImageFormats,
           "text-only-is-observed-without-image-format");

    const bool malformedV5Installed = SetClipboard({}, kMalformedDibV5, {});
    available = {};
    const auto malformedStatus = InspectSettled(1, inspected, {}, {}, &available);
    Expect(malformedV5Installed && malformedStatus == ClipboardTransfer::Status::Invalid && !inspected,
           "malformed-offered-image-is-invalid");
    Expect(available.observed && available.hasImageFormats, "malformed-offered-image-still-counts-as-image-available");

    Expect(SetClipboard(kMalformedPng, {}, {}), "malformed-png-only.fixture-installed");
    const auto malformedPngStatus = InspectSettled(1, inspected, {}, {}, &available);
    Expect(malformedPngStatus == ClipboardTransfer::Status::Invalid && !inspected && available.observed &&
               available.hasImageFormats,
           "malformed-png-only-is-invalid-but-image-was-offered");

    const bool sequenceFixtureInstalled = SetClipboard({}, {}, kDib);
    Observation observation;
    const auto sequenceStatus = InspectSettled(1, inspected, {}, MakeObserver(observation), &available);
    Expect(sequenceFixtureInstalled && sequenceStatus == ClipboardTransfer::Status::Ok &&
               observation.inspectedStages == 1,
           "sequence.success-reaches-inspected-stage-once");
    Expect(available.observed && available.sequence == observation.inspectedSequence &&
               inspected.Sequence() == observation.inspectedSequence,
           "sequence-availability-and-inspection-match-held-inspected-stage");
}

void TestDibFallbacks()
{
    ClipboardTransfer::Inspection inspected;
    ClipboardTransfer::Availability available{};
    Expect(SetClipboard({}, kMalformedDibV5, kDib), "invalid-v5-valid-dib.fixture-installed");
    Observation rejected;
    const auto fallback = InspectSettled(1, inspected, {}, MakeObserver(rejected), &available);
    Expect(fallback == ClipboardTransfer::Status::Invalid && !inspected && available.observed &&
               available.hasImageFormats,
           "invalid-first-v5-refuses-local-source-without-conversion");
    Expect(rejected.v5Reads == 1 && rejected.dibReads == 0, "invalid-first-v5-never-requests-alternate-dib");

    Expect(SetClipboard({}, kOversizedValidDibV5, kDib), "oversized-v5-small-dib.fixture-installed");
    available = {};
    Observation oversized;
    const auto sizeFallback = InspectSettled(1, inspected, {}, MakeObserver(oversized), &available);
    Expect(sizeFallback == ClipboardTransfer::Status::TooLarge && !inspected,
           "over-budget-first-v5-refuses-before-conversion");
    Expect(available.observed && available.hasImageFormats && oversized.v5Reads == 1 && oversized.dibReads == 0,
           "over-budget-first-v5-reports-image-without-alternate-request");

    Expect(SetClipboard({}, kOversizedValidDibV5, {}), "oversized-v5-only.fixture-installed");
    const auto tooLarge = InspectSettled(1, inspected, {}, {}, &available);
    Expect(tooLarge == ClipboardTransfer::Status::TooLarge && !inspected && available.observed &&
               available.hasImageFormats,
           "oversized-offered-dib-is-too-large-but-image-was-offered");

    Expect(SetClipboard({}, kValidDibV5, kDib), "both-valid-dibs.fixture-installed");
    const auto preferred = InspectSettled(1, inspected, {}, {}, &available);
    Expect(preferred == ClipboardTransfer::Status::Ok && inspected && inspected.Dib().pixels == 1,
           "both-valid-dibs-inspect-successfully");
    Expect(HasPixels(inspected, {7, 80, 190, 255}), "valid-v5-is-preferred-over-different-cf-dib");

    Expect(SetClipboard(kMalformedPng, {}, kDib), "malformed-png-valid-dib.fixture-installed");
    const auto pngFallback = InspectSettled(1, inspected, {}, {}, &available);
    Expect(pngFallback == ClipboardTransfer::Status::Ok && inspected && inspected.Png().pixels == 0 &&
               inspected.Dib().pixels == 1,
           "malformed-png-falls-back-to-valid-dib");
    Expect(HasPixels(inspected, {9, 20, 140, 255}), "malformed-png-fallback-decodes-dib-pixel");

    Expect(SetClipboard({}, kValidDibV5, kDib, true), "producer-first-dib.fixture-installed");
    Observation firstDib;
    const auto producerFirst = InspectSettled(1, inspected, {}, MakeObserver(firstDib), &available);
    Expect(producerFirst == ClipboardTransfer::Status::Ok && firstDib.dibReads == 1 && firstDib.v5Reads == 0,
           "producer-first-dib-never-requests-synthesized-v5");
    Expect(HasPixels(inspected, {9, 20, 140, 255}), "producer-first-dib-preserves-literal-pixels");

    auto malformedDib = kDib;
    malformedDib[12] = 0;
    Expect(SetClipboard({}, {}, malformedDib), "invalid-first-dib.fixture-installed");
    Observation badDib;
    const auto invalidDib = InspectSettled(1, inspected, {}, MakeObserver(badDib), &available);
    Expect(invalidDib == ClipboardTransfer::Status::Invalid && !inspected && available.observed &&
               available.hasImageFormats,
           "invalid-first-dib-reports-invalid-offered-image");
    Expect(badDib.dibReads == 1 && badDib.v5Reads == 0, "invalid-first-dib-never-requests-synthesized-v5");
}

void TestBudgetCancellationAndClearing()
{
    ClipboardTransfer::Inspection inspected;
    ClipboardTransfer::Availability available{7, true, true};
    const bool budgetFixtureInstalled = SetClipboard({}, {}, kDib);
    const auto zero = InspectSettled(0, inspected, {}, {}, &available);
    Expect(budgetFixtureInstalled && zero == ClipboardTransfer::Status::TooLarge && !inspected && available.observed &&
               available.hasImageFormats,
           "zero-budget-rejects-image-but-reports-availability");

    available = {7, true, true};
    const auto negative = InspectSettled(-1, inspected, {}, {}, &available);
    Expect(negative == ClipboardTransfer::Status::TooLarge && !inspected && !available.observed &&
               !available.hasImageFormats && available.sequence == 0,
           "negative-budget-clears-availability-before-open");

    available = {};
    Expect(InspectSettled(1, inspected, {}, {}, &available) == ClipboardTransfer::Status::Ok && available.observed,
           "reuse.seeded-from-successful-inspection");
    Expect(SetClipboard({}, {}, {}), "reuse.empty-fixture-installed");
    const auto reused = InspectSettled(1, inspected, {}, {}, &available);
    Expect(reused == ClipboardTransfer::Status::Invalid && !available.hasImageFormats && available.observed,
           "reuse-does-not-retain-stale-image-facts");

    CancelSource earlyCancel;
    (void)earlyCancel.Cancel();
    available = {7, true, true};
    const auto early = InspectSettled(1, inspected, earlyCancel.Token(), {}, &available);
    Expect(early == ClipboardTransfer::Status::Cancelled && !inspected && !available.observed &&
               !available.hasImageFormats && available.sequence == 0,
           "pre-cancel-clears-inspection-and-availability");

    Expect(SetClipboard({}, {}, kDib), "late-cancel.fixture-installed");
    CancelSource lateCancel;
    ClipboardTransfer::Observer observer{&lateCancel,
                                         [](void* raw, ClipboardTransfer::Stage stage) noexcept
                                         {
                                             if (stage == ClipboardTransfer::Stage::Inspected)
                                                 (void)static_cast<CancelSource*>(raw)->Cancel();
                                         },
                                         nullptr};
    available = {7, true, true};
    const auto late = InspectSettled(1, inspected, lateCancel.Token(), observer, &available);
    Expect(late == ClipboardTransfer::Status::Cancelled && !inspected && !available.observed &&
               !available.hasImageFormats && available.sequence == 0,
           "post-close-cancel-clears-both-outputs");

    Expect(SetClipboard({}, {}, kDib), "open-fault.fixture-installed");
    Observation openFailure;
    openFailure.failAt = ClipboardTransfer::Fault::Open;
    available = {7, true, true};
    const auto busy = ClipboardTransfer::Inspect(1, inspected, {}, MakeObserver(openFailure), &available);
    Expect(busy == ClipboardTransfer::Status::Busy && !inspected && !available.observed && !available.hasImageFormats &&
               available.sequence == 0,
           "injected-open-busy-clears-both-outputs");

    const bool nativeFailureFixture = SetClipboard({}, {}, kDib);
    CloseControl closeControl;
    const ClipboardTransfer::Observer closeObserver{&closeControl, CloseDuringInspection, nullptr};
    available = {7, true, true};
    const auto nativeFailure = InspectSettled(1, inspected, {}, closeObserver, &available);
    Expect(nativeFailureFixture && nativeFailure == ClipboardTransfer::Status::NativeFailure &&
               closeControl.closedAtOpenStage && !inspected && !available.observed && !available.hasImageFormats &&
               available.sequence == 0,
           "native-close-failure-clears-both-outputs");
}

void TestPostCloseReplacementDoesNotRebindFacts()
{
    struct Replacement final
    {
        std::uint32_t held = 0;
        std::uint32_t replacement = 0;
        bool replaced = false;
    } state;
    const ClipboardTransfer::Observer observer{&state,
                                               [](void* raw, ClipboardTransfer::Stage stage) noexcept
                                               {
                                                   auto& value = *static_cast<Replacement*>(raw);
                                                   if (stage == ClipboardTransfer::Stage::Opened)
                                                       value.held = GetClipboardSequenceNumber();
                                                   if (stage == ClipboardTransfer::Stage::Inspected)
                                                   {
                                                       value.replaced = SetTextOnlyClipboard();
                                                       value.replacement = GetClipboardSequenceNumber();
                                                   }
                                               },
                                               nullptr};
    Expect(SetClipboard({}, {}, kDib), "post-close.fixture-installed");
    ClipboardTransfer::Inspection inspected;
    ClipboardTransfer::Availability available;
    const auto status = InspectSettled(1, inspected, {}, observer, &available);
    Expect(state.replaced && state.replacement != state.held, "post-close.external-replacement-changes-live-sequence");
    Expect(status == ClipboardTransfer::Status::Ok && inspected && available.observed && available.hasImageFormats &&
               available.sequence == state.held && inspected.Sequence() == state.held,
           "post-close.metadata-retains-held-image-authority-after-text-replacement");
    ClipboardTransfer::ImageCopy copy;
    Expect(ClipboardTransfer::Copy(inspected, copy) == ClipboardTransfer::Status::Stale && copy.RetainedBytes() == 0 &&
               copy.Sequence() == 0,
           "post-close.copy-refuses-replaced-source-without-owned-bytes");
}

int RunWorker()
{
    gFailures = 0;
    gChecks = 0;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com))
    {
        std::printf("FAIL clipboardavailability.worker-mta-initialize hr=0x%08lx\n", static_cast<unsigned long>(com));
        return 1;
    }
    auto uninitialize = wil::scope_exit([]() noexcept { CoUninitialize(); });
    wil::unique_hwnd owner(
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr));
    if (!owner)
    {
        std::printf("FAIL clipboardavailability.owner-window error=%lu\n", GetLastError());
        return 1;
    }
    gOwner = owner.get();
    SystemClipboard::SavedContents saved;
    if (!SystemClipboard::SaveContents(gOwner, saved))
    {
        std::printf("FAIL clipboardavailability.save-user-clipboard\n");
        return 1;
    }
    bool restoreAttempted = false;
    auto restore = wil::scope_exit(
        [&]() noexcept
        {
            if (!restoreAttempted)
                (void)SystemClipboard::RestoreContents(gOwner, saved);
        });

    TestAvailabilityFacts();
    TestDibFallbacks();
    TestBudgetCancellationAndClearing();
    TestPostCloseReplacementDoesNotRebindFacts();

    restoreAttempted = true;
    Expect(SystemClipboard::RestoreContents(gOwner, saved), "clipboard.restore-user-contents");
    gOwner = nullptr;
    std::printf("clipboardavailability summary: %u checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

DWORD WINAPI WorkerEntry(void* raw) noexcept
{
    try
    {
        *static_cast<int*>(raw) = RunWorker();
    }
    catch (const std::bad_alloc&)
    {
        std::printf("FAIL clipboardavailability.worker-out-of-memory\n");
        *static_cast<int*>(raw) = 1;
    }
    return 0;
}
} // namespace

int RunClipboardAvailabilityTests()
{
    int result = 1;
    wil::unique_handle worker(CreateThread(nullptr, 0, WorkerEntry, &result, 0, nullptr));
    if (!worker)
    {
        std::printf("FAIL clipboardavailability.worker-created error=%lu\n", GetLastError());
        return 1;
    }
    if (WaitForSingleObject(worker.get(), 60'000) != WAIT_OBJECT_0)
    {
        std::printf("FAIL clipboardavailability.worker-join-timeout\n");
        RaiseFailFastException(nullptr, nullptr, 0);
        return 1;
    }
    return result;
}
