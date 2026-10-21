#include "IO.Tests.ClipboardFingerprint.h"

#include "ClipboardFingerprint.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace
{
int gFailures = 0;
unsigned int gExpectations = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gExpectations;
    std::printf("%s clipboardfingerprint.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 32> kEmptySha256{0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4,
                                                    0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b,
                                                    0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
constexpr std::array<std::uint8_t, 32> kAbcSha256{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                                  0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                                  0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
// Independent fixture: byte[i] = (i*131 + floor(i/256)*17 + 29) mod 256, for i in [0,65537).
// PowerShell generated the bytes with that formula and SHA256.HashData; expected digest:
// e48cfdd36089b5a7afbae6fabca70d5d49a72293c030c346afd512c69f6983d2.
constexpr std::array<std::uint8_t, 32> kCoordinatePatternSha256{
    0xe4, 0x8c, 0xfd, 0xd3, 0x60, 0x89, 0xb5, 0xa7, 0xaf, 0xba, 0xe6, 0xfa, 0xbc, 0xa7, 0x0d, 0x5d,
    0x49, 0xa7, 0x22, 0x93, 0xc0, 0x30, 0xc3, 0x46, 0xaf, 0xd5, 0x12, 0xc6, 0x9f, 0x69, 0x83, 0xd2};

bool IsZero(const ClipboardFingerprint::Format& format) noexcept
{
    if (format.bytes != 0)
        return false;
    for (const std::uint8_t byte : format.sha256)
    {
        if (byte != 0)
            return false;
    }
    return true;
}

struct Observation final
{
    std::stop_source* stop = nullptr;
    ClipboardFingerprint::Stage cancelAt = ClipboardFingerprint::Stage::Create;
    ClipboardFingerprint::Stage faultAt = ClipboardFingerprint::Stage::Create;
    std::int32_t faultStatus = 0;
    std::array<unsigned int, 3> reached{};
    std::array<std::size_t, 3> offsets{};
    std::array<unsigned int, 3> faultCalls{};
    std::array<std::size_t, 2> bands{};
};

constexpr std::size_t StageIndex(ClipboardFingerprint::Stage stage) noexcept
{
    return static_cast<std::size_t>(stage);
}

void OnReached(void* context, ClipboardFingerprint::Stage stage, std::size_t offset) noexcept
{
    auto& observation = *static_cast<Observation*>(context);
    const std::size_t index = StageIndex(stage);
    ++observation.reached[index];
    observation.offsets[index] = offset;
    if (stage == ClipboardFingerprint::Stage::Band && observation.reached[index] <= observation.bands.size())
        observation.bands[observation.reached[index] - 1] = offset;
    if (observation.stop && observation.cancelAt == stage)
        (void)observation.stop->request_stop();
}

std::int32_t OnFault(void* context, ClipboardFingerprint::Stage stage) noexcept
{
    auto& observation = *static_cast<Observation*>(context);
    ++observation.faultCalls[StageIndex(stage)];
    return observation.faultAt == stage ? observation.faultStatus : 0;
}

ClipboardFingerprint::Format SeededOutput() noexcept
{
    ClipboardFingerprint::Format output;
    output.bytes = 73;
    output.sha256.fill(0xa5);
    return output;
}

ClipboardFingerprint::Observer Observe(Observation& observation) noexcept
{
    return {&observation, OnReached, OnFault};
}

bool HashIs(ClipboardFingerprint::Format actual, std::uint64_t bytes,
            const std::array<std::uint8_t, 32>& digest) noexcept
{
    return actual.bytes == bytes && actual.sha256 == digest;
}

void HashTests()
{
    using ClipboardFingerprint::Hash;
    using ClipboardFingerprint::Stage;
    ClipboardFingerprint::Format output = SeededOutput();
    const std::span<const std::uint8_t> empty;
    Expect(Hash(empty, output) == S_OK, "hash.empty.status");
    Expect(HashIs(output, 0, kEmptySha256), "hash.empty.sha256-literal");

    constexpr std::array<std::uint8_t, 3> abc{'a', 'b', 'c'};
    Expect(Hash(abc, output) == S_OK, "hash.abc.status");
    Expect(HashIs(output, abc.size(), kAbcSha256), "hash.abc.sha256-literal");

    std::vector<std::uint8_t> pattern(65'537);
    for (std::size_t index = 0; index < pattern.size(); ++index)
    {
        const std::size_t value = index * 131 + (index / 256) * 17 + 29;
        pattern[index] = static_cast<std::uint8_t>(value % 256);
    }
    Expect(Hash(pattern, output) == S_OK, "hash.coordinate-pattern.status");
    Expect(HashIs(output, pattern.size(), kCoordinatePatternSha256), "hash.coordinate-pattern.independent-sha256");
    Observation bands;
    Expect(Hash(pattern, output, {}, Observe(bands)) == S_OK &&
               bands.bands == std::array<std::size_t, 2>{65'536, 65'537} && bands.reached[StageIndex(Stage::Band)] == 2,
           "hash.coordinate-pattern-exact-two-bounded-bands");

    std::stop_source preCancelled;
    (void)preCancelled.request_stop();
    output = SeededOutput();
    Observation preObservation;
    Expect(Hash(abc, output, CancelToken{preCancelled.get_token()}, Observe(preObservation)) == E_ABORT,
           "cancel.pre.status");
    Expect(IsZero(output), "cancel.pre.clears-seeded-output");
    Expect(preObservation.reached == std::array<unsigned int, 3>{} &&
               preObservation.faultCalls == std::array<unsigned int, 3>{},
           "cancel.pre.no-observer-call");

    constexpr std::array<std::uint8_t, 1> tiny{0x5a};
    constexpr std::array<const char*, 3> cancelStatusNames{"cancel.create.status", "cancel.first-band.status",
                                                           "cancel.finish.status"};
    constexpr std::array<const char*, 3> cancelOutputNames{"cancel.create.clears-seeded-output",
                                                           "cancel.first-band.clears-seeded-output",
                                                           "cancel.finish.clears-seeded-output"};
    constexpr std::array<const char*, 3> cancelObserverNames{
        "cancel.create.observer-reached", "cancel.first-band.observer-reached", "cancel.finish.observer-reached"};
    for (const Stage stage : {Stage::Create, Stage::Band, Stage::Finish})
    {
        std::stop_source stop;
        Observation observation;
        observation.stop = &stop;
        observation.cancelAt = stage;
        output = SeededOutput();
        const auto source =
            stage == Stage::Band ? std::span<const std::uint8_t>{pattern} : std::span<const std::uint8_t>{tiny};
        const std::int32_t status = Hash(source, output, CancelToken{stop.get_token()}, Observe(observation));
        const std::size_t index = StageIndex(stage);
        Expect(status == E_ABORT, cancelStatusNames[index]);
        Expect(IsZero(output), cancelOutputNames[index]);
        Expect(observation.reached[index] == 1, cancelObserverNames[index]);
        if (stage == Stage::Band)
            Expect(observation.offsets[index] == 65'536 && observation.reached[StageIndex(Stage::Finish)] == 0,
                   "cancel.first-band-stops-before-next-byte-and-finish");
    }

    constexpr std::int32_t kInjectedNtStatus = -1'073'741'823; // 0xC0000001, STATUS_UNSUCCESSFUL.
    constexpr std::array<const char*, 3> faultStatusNames{"fault.create.exact-hresult-from-nt",
                                                          "fault.first-band.exact-hresult-from-nt",
                                                          "fault.finish.exact-hresult-from-nt"};
    constexpr std::array<const char*, 3> faultOutputNames{"fault.create.clears-seeded-output",
                                                          "fault.first-band.clears-seeded-output",
                                                          "fault.finish.clears-seeded-output"};
    constexpr std::array<const char*, 3> faultObserverNames{
        "fault.create.injection-observed", "fault.first-band.injection-observed", "fault.finish.injection-observed"};
    for (const Stage stage : {Stage::Create, Stage::Band, Stage::Finish})
    {
        Observation observation;
        observation.faultAt = stage;
        observation.faultStatus = kInjectedNtStatus;
        output = SeededOutput();
        const std::int32_t status = Hash(tiny, output, {}, Observe(observation));
        const std::int32_t expected = static_cast<std::int32_t>(HRESULT_FROM_NT(kInjectedNtStatus));
        const std::size_t index = StageIndex(stage);
        Expect(status == expected, faultStatusNames[index]);
        Expect(IsZero(output), faultOutputNames[index]);
        Expect(observation.faultCalls[index] == 1, faultObserverNames[index]);
    }
}

ClipboardReceipt::Key ValidKey() noexcept
{
    ClipboardReceipt::Key key{};
    for (std::size_t index = 0; index < key.session.size(); ++index)
        key.session[index] = static_cast<std::uint8_t>(index + 1);
    key.serial = 0x0807060504030201ULL;
    return key;
}

void MatchTests()
{
    using ClipboardFingerprint::Binding;
    using ClipboardFingerprint::Format;
    const ClipboardReceipt::Key key = ValidKey();
    const Format png{3, kAbcSha256};
    std::array<std::uint8_t, 32> dibDigest{};
    dibDigest.fill(0x5c);
    const Format dib{4, dibDigest};
    const std::optional<ClipboardReceipt::Key> receipt{key};
    const Binding expected{key, png, dib};

    Expect(ClipboardFingerprint::Matches(expected, key, receipt, png, dib),
           "matches.full-current-private-session-serial-receipt");

    ClipboardReceipt::Key wrongPrivateSession = key;
    wrongPrivateSession.session[0] ^= 0x40;
    Expect(!ClipboardFingerprint::Matches(expected, wrongPrivateSession, receipt, png, dib),
           "matches.rejects-current-private-session-mismatch");
    ClipboardReceipt::Key wrongPrivateSerial = key;
    ++wrongPrivateSerial.serial;
    Expect(!ClipboardFingerprint::Matches(expected, wrongPrivateSerial, receipt, png, dib),
           "matches.rejects-current-private-serial-mismatch");
    ClipboardReceipt::Key wrongReceiptSession = key;
    wrongReceiptSession.session[15] ^= 0x80;
    Expect(!ClipboardFingerprint::Matches(expected, key, wrongReceiptSession, png, dib),
           "matches.rejects-receipt-session-mismatch");
    ClipboardReceipt::Key wrongReceiptSerial = key;
    --wrongReceiptSerial.serial;
    Expect(!ClipboardFingerprint::Matches(expected, key, wrongReceiptSerial, png, dib),
           "matches.rejects-receipt-serial-mismatch");
    Expect(!ClipboardFingerprint::Matches(expected, key, std::nullopt, png, dib), "matches.rejects-missing-receipt");

    Expect(ClipboardFingerprint::Matches(expected, key, receipt, png, dib), "matches.accepts-both-formats");
    Expect(ClipboardFingerprint::Matches(expected, key, receipt, png, {}), "matches.accepts-png-only");
    Expect(ClipboardFingerprint::Matches(expected, key, receipt, {}, dib), "matches.accepts-dib-only");
    Expect(!ClipboardFingerprint::Matches(expected, key, receipt, {}, {}), "matches.rejects-absent-payload");

    Format alteredPng = png;
    alteredPng.sha256[0] ^= 0x01;
    Expect(!ClipboardFingerprint::Matches(expected, key, receipt, alteredPng, {}),
           "matches.rejects-altered-same-length-digest");
    Format badLength = png;
    ++badLength.bytes;
    Expect(!ClipboardFingerprint::Matches(expected, key, receipt, badLength, {}), "matches.rejects-bad-png-length");
    Format badDibLength = dib;
    ++badDibLength.bytes;
    Expect(!ClipboardFingerprint::Matches(expected, key, receipt, {}, badDibLength), "matches.rejects-bad-dib-length");
    Expect(!ClipboardFingerprint::Matches(expected, key, receipt, alteredPng, dib),
           "matches.rejects-png-altered-while-dib-valid");
    Format alteredDib = dib;
    alteredDib.sha256[31] ^= 0x01;
    Expect(!ClipboardFingerprint::Matches(expected, key, receipt, png, alteredDib),
           "matches.rejects-dib-altered-while-png-valid");

    const ClipboardReceipt::Key zeroKey{};
    const Binding invalidExpected{zeroKey, png, dib};
    Expect(!ClipboardFingerprint::Matches(invalidExpected, zeroKey, zeroKey, png, dib),
           "matches.rejects-all-zero-expected-key");
}
} // namespace

int RunClipboardFingerprintTests()
{
    gFailures = 0;
    gExpectations = 0;
    HashTests();
    MatchTests();
    std::printf("ClipboardFingerprint: %u assertions, %d failures\n", gExpectations, gFailures);
    return gFailures;
}
