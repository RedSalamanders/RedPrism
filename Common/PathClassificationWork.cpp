#include "PathClassificationWork.h"

#include <algorithm>
#include <array>
#include <stop_token>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
static_assert(sizeof(wchar_t) == sizeof(char16_t));

[[nodiscard]] PathClassificationBatch FailedBatch(PathClassificationStatus status) noexcept
{
    PathClassificationBatch result;
    result.status = status;
    return result;
}

[[nodiscard]] bool HasPrismExtension(std::wstring_view path) noexcept
{
    constexpr std::wstring_view extension = L".prism";
    if (path.size() < extension.size())
    {
        return false;
    }
    const std::wstring_view tail = path.substr(path.size() - extension.size());
    return CompareStringOrdinal(tail.data(), static_cast<int>(tail.size()), extension.data(),
                                static_cast<int>(extension.size()), TRUE) == CSTR_EQUAL;
}

[[nodiscard]] PathClassification ClassifyTerminatedPath(const wchar_t* path, std::wstring_view pathView,
                                                        const CancelToken& cancellation) noexcept
{
    const DWORD attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return PathClassification::Project;
    }
    const bool prismExtension = HasPrismExtension(pathView);
    if (prismExtension || attributes == INVALID_FILE_ATTRIBUTES)
    {
        return prismExtension ? PathClassification::Project : PathClassification::Image;
    }
    if (cancellation.IsCancelled())
    {
        return PathClassification::Image;
    }

    wil::unique_hfile file{CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr)};
    if (!file)
    {
        return PathClassification::Image;
    }
    if (cancellation.IsCancelled())
    {
        return PathClassification::Image;
    }

    std::array<std::uint8_t, 4> header{};
    wil::unique_event_nothrow completed;
    wil::unique_event_nothrow cancelled;
    if (FAILED(completed.create(wil::EventOptions::ManualReset)) ||
        FAILED(cancelled.create(wil::EventOptions::ManualReset)))
    {
        return PathClassification::Image;
    }
    OVERLAPPED operation{};
    operation.hEvent = completed.get();
    DWORD bytesRead = 0;
    if (!ReadFile(file.get(), header.data(), static_cast<DWORD>(header.size()), &bytesRead, &operation))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            return PathClassification::Image;
        }
        // Register after issue: an already requested stop must cancel this exact outstanding read. Registering
        // before ReadFile would allow cancellation to race before the operation existed. Both callbacks are
        // retired before the file/OVERLAPPED/buffer, and acknowledgement is required even after cancellation.
        const auto signalCancellation = [&]() noexcept { cancelled.SetEvent(); };
        const std::stop_callback local(cancellation.LocalToken(), signalCancellation);
        const std::stop_callback shutdown(cancellation.ShutdownToken(), signalCancellation);
        const HANDLE events[]{completed.get(), cancelled.get()};
        const auto waited = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (waited != WAIT_OBJECT_0)
            (void)CancelIoEx(file.get(), &operation); // the worker owns driver cancellation, never the UI callback
        if (!GetOverlappedResult(file.get(), &operation, &bytesRead, TRUE))
        {
            return PathClassification::Image;
        }
    }
    if (bytesRead != header.size())
        return PathClassification::Image;

    const bool zipSignature = header[0] == 'P' && header[1] == 'K' &&
                              ((header[2] == 3 && header[3] == 4) || (header[2] == 5 && header[3] == 6) ||
                               (header[2] == 7 && header[3] == 8));
    return zipSignature ? PathClassification::Project : PathClassification::Image;
}
} // namespace

PathClassificationBatch ClassifyPathBatch(std::span<const std::wstring_view> paths,
                                          const CancelToken& cancellation) noexcept
{
    if (paths.size() > kMaxPathClassificationBatch)
    {
        return FailedBatch(PathClassificationStatus::InvalidInput);
    }
    if (cancellation.IsCancelled())
        return FailedBatch(PathClassificationStatus::Cancelled);

    for (const std::wstring_view path : paths)
    {
        if (cancellation.IsCancelled())
            return FailedBatch(PathClassificationStatus::Cancelled);
        if (path.empty() || path.size() > kMaxPathClassificationLength ||
            std::find(path.begin(), path.end(), L'\0') != path.end())
        {
            return FailedBatch(PathClassificationStatus::InvalidInput);
        }
    }

    PathClassificationBatch result;
    result.status = PathClassificationStatus::Ok;
    std::array<wchar_t, kMaxPathClassificationLength + 1> terminatedPath{};
    for (std::size_t index = 0; index < paths.size(); ++index)
    {
        if (cancellation.IsCancelled())
        {
            return FailedBatch(PathClassificationStatus::Cancelled);
        }

        const std::wstring_view path = paths[index];
        std::copy(path.begin(), path.end(), terminatedPath.begin());
        terminatedPath[path.size()] = L'\0';
        result.classifications[index] = ClassifyTerminatedPath(terminatedPath.data(), path, cancellation);
        if (cancellation.IsCancelled())
        {
            return FailedBatch(PathClassificationStatus::Cancelled);
        }
    }

    result.count = paths.size();
    return result;
}
