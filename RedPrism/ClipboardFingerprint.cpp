#include "ClipboardFingerprint.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace ClipboardFingerprint
{
namespace
{
NTSTATUS Fault(Observer observer, Stage stage) noexcept
{
    return observer.fault ? observer.fault(observer.context, stage) : 0;
}
void Reached(Observer observer, Stage stage, std::size_t offset) noexcept
{
    if (observer.reached)
        observer.reached(observer.context, stage, offset);
}
} // namespace

std::int32_t Hash(std::span<const std::uint8_t> bytes, Format& output, CancelToken cancellation,
                  Observer observer) noexcept
{
    output = {};
    if (cancellation.IsCancelled())
        return E_ABORT;
    if (!bytes.empty() && !bytes.data())
        return E_INVALIDARG;
    // Stack owner precedes the hash: CNG retains this object buffer through BCryptDestroyHash.
    alignas(64) std::array<std::uint8_t, 4096> object{};
    ULONG objectLength = 0, returned = 0;
    auto propertyStatus =
        BCryptGetProperty(BCRYPT_SHA256_ALG_HANDLE, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
                          sizeof(objectLength), &returned, 0);
    if (propertyStatus < 0)
        return HRESULT_FROM_NT(propertyStatus);
    if (returned != sizeof(objectLength) || !objectLength || objectLength > object.size())
        return E_UNEXPECTED;
    wil::unique_bcrypt_hash hash;
    auto status = Fault(observer, Stage::Create);
    if (status >= 0)
        status = BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, hash.put(), object.data(), objectLength, nullptr, 0, 0);
    if (status < 0)
        return HRESULT_FROM_NT(status);
    Reached(observer, Stage::Create, 0);
    for (std::size_t offset = 0; offset < bytes.size();)
    {
        if (cancellation.IsCancelled())
            return E_ABORT;
        const auto count = (std::min)(std::size_t{64 * 1024}, bytes.size() - offset);
        status = Fault(observer, Stage::Band);
        if (status >= 0)
            status =
                BCryptHashData(hash.get(), const_cast<PUCHAR>(bytes.data() + offset), static_cast<ULONG>(count), 0);
        if (status < 0)
            return HRESULT_FROM_NT(status);
        offset += count;
        Reached(observer, Stage::Band, offset);
    }
    if (cancellation.IsCancelled())
        return E_ABORT;
    Format completed;
    status = Fault(observer, Stage::Finish);
    if (status >= 0)
        status = BCryptFinishHash(hash.get(), completed.sha256.data(), static_cast<ULONG>(completed.sha256.size()), 0);
    if (status < 0)
        return HRESULT_FROM_NT(status);
    Reached(observer, Stage::Finish, bytes.size());
    if (cancellation.IsCancelled())
        return E_ABORT;
    completed.bytes = bytes.size();
    output = completed;
    return S_OK;
}

bool Matches(const Binding& expected, const ClipboardReceipt::Key& currentPrivate,
             const std::optional<ClipboardReceipt::Key>& receipt, const Format& png, const Format& dib) noexcept
{
    std::array<std::uint8_t, ClipboardReceipt::kEncodedBytes> packet{};
    return receipt && ClipboardReceipt::Encode(expected.key, packet) && expected.key == currentPrivate &&
           expected.key == receipt.value() && (png.bytes || dib.bytes) && (!png.bytes || png == expected.png) &&
           (!dib.bytes || dib == expected.dib);
}
} // namespace ClipboardFingerprint
