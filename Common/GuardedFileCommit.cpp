#include "GuardedFileCommit.h"

#include "OutputPathLimits.h"
#include "Win32FilePath.h"

#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

[[nodiscard]] HRESULT ReadFileOutputVersion(HANDLE handle, FileOutputVersion& version) noexcept
{
    FILE_ID_INFO file{};
    FILE_BASIC_INFO times{};
    FILE_STANDARD_INFO standard{};
    if (! GetFileInformationByHandleEx(handle, FileIdInfo, &file, sizeof(file)) ||
        ! GetFileInformationByHandleEx(handle, FileBasicInfo, &times, sizeof(times)) ||
        ! GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)))
        return HRESULT_FROM_WIN32(GetLastError());
    constexpr FILE_ID_128 kEmptyId{};
    if ((times.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        std::memcmp(file.FileId.Identifier, kEmptyId.Identifier, sizeof(kEmptyId.Identifier)) == 0 || standard.DeletePending || standard.EndOfFile.QuadPart < 0)
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    version = {
        .size          = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart),
        .lastWriteTime = times.LastWriteTime.QuadPart,
        .changeTime    = times.ChangeTime.QuadPart,
        .volume        = file.VolumeSerialNumber,
    };
    std::memcpy(version.fileId.data(), file.FileId.Identifier, version.fileId.size());
    return S_OK;
}

namespace
{
[[nodiscard]] HRESULT ObserveFileOutputVersion(const std::wstring& path, std::optional<FileOutputVersion>& version) noexcept
{
    version.reset();
    const wil::unique_hfile file{CreateFileW(path.c_str(),
                                             FILE_READ_ATTRIBUTES,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                             nullptr,
                                             OPEN_EXISTING,
                                             FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                             nullptr)};
    if (! file)
    {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(error);
    }
    FileOutputVersion observed{};
    const HRESULT result = ReadFileOutputVersion(file.get(), observed);
    if (SUCCEEDED(result))
        version = observed;
    return result;
}

} // namespace

[[nodiscard]] HRESULT PublishFileOutput(HANDLE temporary, const FileOutputTarget& target, CancelToken cancellation, FileOutputPublishPolicy policy)
{
    if (target.path.empty() || target.path.size() > kMaxImageOutputPathUnits)
        return E_INVALIDARG;
    if (cancellation.IsCancelled())
        return E_ABORT;
    constexpr std::size_t kHeaderBytes = offsetof(FILE_RENAME_INFO, FileName);
    const std::size_t nameBytes        = target.path.size() * sizeof(wchar_t);
    const std::size_t bytes            = kHeaderBytes + nameBytes + sizeof(wchar_t);
    std::vector<std::uint64_t> storage((bytes + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
    auto* const rename     = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->FileNameLength = static_cast<DWORD>(nameBytes);
    std::memcpy(rename->FileName, target.path.data(), nameBytes);
    const auto renameTemporary = [&](FILE_INFO_BY_HANDLE_CLASS kind) noexcept
    {
        if (cancellation.IsCancelled())
            return E_ABORT;
        return SetFileInformationByHandle(temporary, kind, rename, static_cast<DWORD>(bytes)) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    };
    const auto stillAccepted = [&target]() noexcept
    {
        std::optional<FileOutputVersion> current;
        return SUCCEEDED(ObserveFileOutputVersion(target.path, current)) && current == target.acceptedVersion;
    };
    const auto checkpoint = [&policy](FileOutputPublishStage stage) noexcept
    {
        if (policy.checkpoint)
            policy.checkpoint(policy.context, stage);
    };
    const auto resultStatus = [&stillAccepted](HRESULT result) noexcept
    {
        if (SUCCEEDED(result))
            return S_OK;
        if (result == E_ABORT)
            return E_ABORT;
        if (result == HRESULT_FROM_WIN32(ERROR_FILE_EXISTS) || result == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) ||
            result == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) || ! stillAccepted())
            return kOutputVersionConflict;
        return result;
    };

    if (! target.acceptedVersion)
    {
        checkpoint(FileOutputPublishStage::BeforeFinalCheck);
        rename->ReplaceIfExists = FALSE;
        return resultStatus(renameTemporary(FileRenameInfo));
    }

    wil::unique_hfile guard{CreateFileW(target.path.c_str(),
                                        FILE_READ_ATTRIBUTES | DELETE,
                                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                                        nullptr,
                                        OPEN_EXISTING,
                                        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                        nullptr)};
    FileOutputVersion held{};
    if (! guard || FAILED(ReadFileOutputVersion(guard.get(), held)) || held != target.acceptedVersion.value())
        return kOutputVersionConflict;

    checkpoint(FileOutputPublishStage::BeforeFinalCheck);
    if (cancellation.IsCancelled())
        return E_ABORT;
    if (! stillAccepted())
        return kOutputVersionConflict;
    rename->Flags        = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
    const HRESULT result = policy.forceClassic ? HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) : renameTemporary(FileRenameInfoEx);
    if (result != HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) && result != HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION) &&
        result != HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER) && result != HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED) && result != E_ACCESSDENIED)
        return resultStatus(result);

    // A classic replacement needs the target guard closed. Recheck the name after releasing it.
    guard.reset();
    checkpoint(FileOutputPublishStage::BeforeClassicCheck);
    if (cancellation.IsCancelled())
        return E_ABORT;
    if (! stillAccepted())
        return kOutputVersionConflict;
    rename->ReplaceIfExists = TRUE;
    return resultStatus(renameTemporary(FileRenameInfo));
}

HRESULT ObserveFileOutputTarget(const wchar_t* path, FileOutputTarget& target) noexcept
{
    FileOutputTarget observed;
    const HRESULT resolved = Win32FilePath::Resolve(path, Win32FilePath::Use::File, observed.path);
    if (FAILED(resolved))
        return resolved;
    if (observed.path.size() > kMaxImageOutputPathUnits)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    const HRESULT result = ObserveFileOutputVersion(observed.path, observed.acceptedVersion);
    if (SUCCEEDED(result))
        target = std::move(observed);
    return result;
}
