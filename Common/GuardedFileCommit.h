#pragma once

#include "Executor.h"
#include "FileOutputTarget.h"

#include <windows.h>

enum class FileOutputPublishStage : std::uint8_t
{
    BeforeFinalCheck,
    BeforeClassicCheck,
};

struct FileOutputPublishPolicy final
{
    void* context                                              = nullptr;
    void (*checkpoint)(void*, FileOutputPublishStage) noexcept = nullptr;
    bool forceClassic                                          = false;
};

inline const HRESULT kOutputVersionConflict = HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);

[[nodiscard]] HRESULT ReadFileOutputVersion(HANDLE file, FileOutputVersion& version) noexcept;
[[nodiscard]] HRESULT ObserveFileOutputTarget(const wchar_t* path, FileOutputTarget& target) noexcept;

// The caller owns a flushed, writable same-directory temporary opened with DELETE access.
[[nodiscard]] HRESULT PublishFileOutput(HANDLE temporary, const FileOutputTarget& target, CancelToken cancellation = {}, FileOutputPublishPolicy policy = {});
