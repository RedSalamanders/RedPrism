#pragma once

#include "GuardedFileCommit.h"
#include "ProjectStore.h"

#include <optional>
#include <string>

namespace ProjectSaveFixture
{
[[nodiscard]] inline std::optional<FileOutputVersion> SeedArchive(const std::wstring& path,
                                                                  const CanvasDocument& document,
                                                                  const std::optional<Guid>& activeLayer) noexcept
{
    try
    {
        const FileOutputTarget newName{path, std::nullopt};
        if (SaveProject(path.c_str(), document, activeLayer, true, {}, {}, {}, &newName) != ProjectIoStatus::Ok)
            return {};
        const auto opened = LoadProject(path.c_str());
        return opened.status == ProjectIoStatus::Ok ? opened.archiveVersion : std::nullopt;
    }
    catch (...)
    {
        return {};
    }
}

[[nodiscard]] inline bool ArchiveUnchanged(const std::wstring& path, const std::optional<FileOutputVersion>& expected) noexcept
{
    FileOutputTarget observed;
    return SUCCEEDED(ObserveFileOutputTarget(path.c_str(), observed)) && observed.acceptedVersion == expected;
}
} // namespace ProjectSaveFixture
