#include "ClipboardReceiptTests.h"

#include "ClipboardReceipt.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>

namespace
{

using ClipboardReceipt::Decode;
using ClipboardReceipt::Encode;
using ClipboardReceipt::kEncodedBytes;
using ClipboardReceipt::Key;

constexpr std::array<std::uint8_t, kEncodedBytes> kLiteral{0x52, 0x50, 0x43, 0x4C, 0x49, 0x50, 0x30, 0x31, 0x01, 0x00,
                                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
                                                           0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
                                                           0x0F, 0x10, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

int g_failures = 0;
int g_assertions = 0;

void Expect(bool condition, const char* caseName) noexcept
{
    ++g_assertions;
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", caseName);
    if (!condition)
    {
        ++g_failures;
    }
}

Key ExpectedKey() noexcept
{
    Key key{};
    for (std::size_t index = 0; index < key.session.size(); ++index)
    {
        key.session[index] = static_cast<std::uint8_t>(index + 1);
    }
    key.serial = 0x0807060504030201ULL;
    return key;
}

void ExpectDecodeFailure(std::span<const std::uint8_t> bytes, const char* rejectedCase,
                         const char* clearedCase) noexcept
{
    Key output = ExpectedKey();
    Expect(!Decode(bytes, output), rejectedCase);
    Expect(output == Key{}, clearedCase);
}

} // namespace

int RunClipboardReceiptTests()
{
    g_failures = 0;
    g_assertions = 0;

    const Key expected = ExpectedKey();
    std::array<std::uint8_t, kEncodedBytes> encoded{};
    Expect(Encode(expected, encoded) && encoded == kLiteral, "ClipboardReceipt.encode.literal-packet");

    Key decoded{};
    Expect(Decode(kLiteral, decoded) && decoded == expected, "ClipboardReceipt.decode.literal-packet");

    auto badPrefix = kLiteral;
    badPrefix[0] ^= 0x01;
    ExpectDecodeFailure(badPrefix, "ClipboardReceipt.decode.rejects-prefix",
                        "ClipboardReceipt.decode.prefix-clears-output");
    auto badVersion = kLiteral;
    badVersion[8] = 2;
    ExpectDecodeFailure(badVersion, "ClipboardReceipt.decode.rejects-version",
                        "ClipboardReceipt.decode.version-clears-output");
    auto badReserved = kLiteral;
    badReserved[12] = 1;
    ExpectDecodeFailure(badReserved, "ClipboardReceipt.decode.rejects-reserved",
                        "ClipboardReceipt.decode.reserved-clears-output");

    ExpectDecodeFailure(std::span<const std::uint8_t>{kLiteral.data(), kLiteral.size() - 1},
                        "ClipboardReceipt.decode.rejects-short-length",
                        "ClipboardReceipt.decode.short-length-clears-output");
    std::array<std::uint8_t, kEncodedBytes + 1> extra{};
    for (std::size_t index = 0; index < kLiteral.size(); ++index)
    {
        extra[index] = kLiteral[index];
    }
    ExpectDecodeFailure(extra, "ClipboardReceipt.decode.rejects-long-length",
                        "ClipboardReceipt.decode.long-length-clears-output");

    auto zeroSession = kLiteral;
    zeroSession[16] = 0;
    for (std::size_t index = 17; index < 32; ++index)
    {
        zeroSession[index] = 0;
    }
    ExpectDecodeFailure(zeroSession, "ClipboardReceipt.decode.rejects-zero-session",
                        "ClipboardReceipt.decode.zero-session-clears-output");
    auto zeroSerial = kLiteral;
    zeroSerial[32] = 0;
    for (std::size_t index = 33; index < 40; ++index)
    {
        zeroSerial[index] = 0;
    }
    ExpectDecodeFailure(zeroSerial, "ClipboardReceipt.decode.rejects-zero-serial",
                        "ClipboardReceipt.decode.zero-serial-clears-output");

    Key invalid = expected;
    invalid.serial = 0;
    encoded.fill(0xA5);
    Expect(!Encode(invalid, encoded), "ClipboardReceipt.encode.rejects-zero-serial");
    Expect(encoded == std::array<std::uint8_t, kEncodedBytes>{},
           "ClipboardReceipt.encode.failure-clears-serial-output");
    invalid = expected;
    invalid.session.fill(0);
    encoded.fill(0xA5);
    Expect(!Encode(invalid, encoded), "ClipboardReceipt.encode.rejects-zero-session");
    Expect(encoded == std::array<std::uint8_t, kEncodedBytes>{},
           "ClipboardReceipt.encode.failure-clears-session-output");

    Key maxSerial = expected;
    maxSerial.serial = std::numeric_limits<std::uint64_t>::max();
    const bool maxSerialEncoded = Encode(maxSerial, encoded);
    decoded = {};
    Expect(maxSerialEncoded && Decode(encoded, decoded) && decoded == maxSerial,
           "ClipboardReceipt.round-trips-maximum-serial");

    auto versionHighByte = kLiteral;
    versionHighByte[11] = 1;
    Expect(!Decode(versionHighByte, decoded) && decoded == Key{}, "ClipboardReceipt.decode.rejects-full-width-version");
    auto reservedHighByte = kLiteral;
    reservedHighByte[15] = 1;
    Expect(!Decode(reservedHighByte, decoded) && decoded == Key{},
           "ClipboardReceipt.decode.rejects-full-width-reserved");
    Key highBit;
    highBit.session[15] = 0xff;
    highBit.serial = 0x8000000000000000ULL;
    std::array<std::uint8_t, kEncodedBytes> highLiteral{0x52, 0x50, 0x43, 0x4c, 0x49, 0x50, 0x30, 0x31, 1};
    highLiteral[31] = 0xff;
    highLiteral[39] = 0x80;
    Expect(Encode(highBit, encoded) && encoded == highLiteral,
           "ClipboardReceipt.encode.literal-high-bit-and-zero-prefix");
    Expect(Decode(highLiteral, decoded) && decoded == highBit,
           "ClipboardReceipt.decode.literal-high-bit-and-zero-prefix");

    std::printf("ClipboardReceipt: %d assertions, %d failures\n", g_assertions, g_failures);
    return g_failures == 0 ? 0 : 1;
}
