#include "PathClassificationRead.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <exception>
#include <memory>
#include <utility>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct PathClassificationRead::Impl final
{
    PathClassificationScratch terminated;
    PathClassificationReadSource source;
    HANDLE event = nullptr; // borrowed until final worker acknowledgement
    wil::unique_hfile file;
    OVERLAPPED operation{};
    std::array<std::uint8_t, 4> header{};
    DWORD bytesRead = 0;
    PathClassification classification = PathClassification::Image;
    bool pending = false, readyToIssue = false, cancellationIssued = false;

    void Observe(PathClassificationReadNotice notice) noexcept
    {
        if (source.observe)
            source.observe(source.context, notice);
    }

    PathClassificationReadStep FinishRead(bool successful, const CancelToken& cancellation) noexcept
    {
        assert(!pending);
        const bool zip = successful && bytesRead == header.size() && header[0] == 'P' && header[1] == 'K' &&
                         ((header[2] == 3 && header[3] == 4) || (header[2] == 5 && header[3] == 6) ||
                          (header[2] == 7 && header[3] == 8));
        classification = zip ? PathClassification::Project : PathClassification::Image;
        file.reset();
        readyToIssue = false;
        return cancellation.IsCancelled() ? PathClassificationReadStep::Cancelled
                                          : PathClassificationReadStep::Complete;
    }
    void RequestCancellation() noexcept
    {
        if (pending && !cancellationIssued)
        {
            cancellationIssued = true;
            (void)CancelIoEx(file.get(), &operation);
            Observe(PathClassificationReadNotice::CancellationRequested);
        }
    }
};
#pragma warning(pop)

PathClassificationRead::PathClassificationRead() noexcept = default;
PathClassificationRead::~PathClassificationRead()
{
    CancelAndJoinForShutdownWorker();
    const auto source = _impl ? _impl->source : PathClassificationReadSource{};
    _impl.reset();
    if (source.observe)
        source.observe(source.context, PathClassificationReadNotice::Destroyed);
}
void PathClassificationRead::Initialize(void* event, PathClassificationReadSource source)
{
    assert(!_impl && event);
    auto state = std::make_unique<Impl>();
    state->event = static_cast<HANDLE>(event);
    state->source = source;
    _impl = std::move(state);
}
PathClassificationReadStep PathClassificationRead::Prepare(std::wstring_view path,
                                                           const CancelToken& cancellation) noexcept
{
    if (!_impl || _impl->pending || _impl->readyToIssue || path.empty() || path.size() > kMaxPathClassificationLength ||
        std::find(path.begin(), path.end(), L'\0') != path.end())
        return PathClassificationReadStep::InvalidInput;
    auto& self = *_impl;
    if (cancellation.IsCancelled())
        return PathClassificationReadStep::Cancelled;
    std::copy(path.begin(), path.end(), self.terminated.begin());
    self.terminated[path.size()] = L'\0';
    self.classification = PathClassification::Image;
    const DWORD attributes = GetFileAttributesW(self.terminated.data());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        self.classification = PathClassification::Project;
        return cancellation.IsCancelled() ? PathClassificationReadStep::Cancelled
                                          : PathClassificationReadStep::Complete;
    }
    constexpr std::wstring_view extension = L".prism";
    const bool projectExtension =
        path.size() >= extension.size() &&
        CompareStringOrdinal(path.data() + path.size() - extension.size(), static_cast<int>(extension.size()),
                             extension.data(), static_cast<int>(extension.size()), TRUE) == CSTR_EQUAL;
    if (projectExtension || attributes == INVALID_FILE_ATTRIBUTES)
    {
        self.classification = projectExtension ? PathClassification::Project : PathClassification::Image;
        return cancellation.IsCancelled() ? PathClassificationReadStep::Cancelled
                                          : PathClassificationReadStep::Complete;
    }
    if (cancellation.IsCancelled())
        return PathClassificationReadStep::Cancelled;
    self.file.reset(self.source.openRead
                        ? static_cast<HANDLE>(self.source.openRead(self.source.context, self.terminated.data()))
                        : CreateFileW(self.terminated.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr));
    if (!self.file)
        return cancellation.IsCancelled() ? PathClassificationReadStep::Cancelled
                                          : PathClassificationReadStep::Complete;
    self.operation = {};
    self.operation.hEvent = self.event;
    self.header.fill(0);
    self.bytesRead = 0;
    self.cancellationIssued = false;
    self.readyToIssue = true;
    return PathClassificationReadStep::ReadyToIssue;
}
PathClassificationReadStep PathClassificationRead::Issue(const CancelToken& cancellation) noexcept
{
    if (!_impl || !_impl->readyToIssue || _impl->pending)
        return PathClassificationReadStep::InvalidInput;
    auto& self = *_impl;
    self.readyToIssue = false;
    PathClassificationReadStep result;
    if (cancellation.IsCancelled())
    {
        result = self.FinishRead(false, cancellation);
        self.Observe(PathClassificationReadNotice::Unissued);
    }
    else
    {
        const bool immediate = ReadFile(self.file.get(), self.header.data(), static_cast<DWORD>(self.header.size()),
                                        &self.bytesRead, &self.operation) != FALSE;
        if (!immediate && GetLastError() == ERROR_IO_PENDING)
        {
            self.pending = true;
            self.Observe(PathClassificationReadNotice::Pending);
            // Windows owns both bytes and OVERLAPPED until GetOverlappedResult acknowledges completion.
            return PathClassificationReadStep::Pending;
        }
        result = self.FinishRead(immediate, cancellation);
        self.Observe(immediate ? PathClassificationReadNotice::Immediate : PathClassificationReadNotice::IssueFailed);
    }
    // There is no outstanding operation here. Even a failed issue retires the original registered cycle.
    if (!SetEvent(self.event))
        std::terminate();
    return result;
}
PathClassificationReadStep PathClassificationRead::Acknowledge(const CancelToken& cancellation,
                                                               bool retireRead) noexcept
{
    if (!_impl)
        return PathClassificationReadStep::InvalidInput;
    auto& self = *_impl;
    if (!self.pending)
        return cancellation.IsCancelled() ? PathClassificationReadStep::Cancelled
                                          : PathClassificationReadStep::Complete;
    if (retireRead || cancellation.IsCancelled())
        self.RequestCancellation();
    bool successful = GetOverlappedResult(self.file.get(), &self.operation, &self.bytesRead, FALSE) != FALSE;
    if (!successful && GetLastError() == ERROR_IO_INCOMPLETE)
    {
        // Callback readiness is not an IO acknowledgement. Reset before recheck, so a completion racing the
        // reset is either observed here or leaves the same event signalled for the subsequent owner rearm.
        if (!ResetEvent(self.event))
            std::terminate();
        successful = GetOverlappedResult(self.file.get(), &self.operation, &self.bytesRead, FALSE) != FALSE;
        if (!successful && GetLastError() == ERROR_IO_INCOMPLETE)
        {
            self.Observe(PathClassificationReadNotice::Incomplete);
            return PathClassificationReadStep::Pending;
        }
    }
    self.pending = false;
    const auto finished = self.FinishRead(successful, cancellation);
    self.Observe(PathClassificationReadNotice::Acknowledged);
    // A cancellation-only stage may acknowledge before the registered callback was queued. Publish only
    // after the actual IO acknowledgement; the old registered cycle must still consume its one callback.
    if (!SetEvent(self.event))
        std::terminate();
    return finished;
}
void PathClassificationRead::RetireUnissuedCycleWorker() noexcept
{
    assert(_impl && !_impl->pending && _impl->readyToIssue);
    auto& self = *_impl;
    self.file.reset();
    self.readyToIssue = false;
    self.classification = PathClassification::Image;
    self.Observe(PathClassificationReadNotice::Unissued);
    if (!SetEvent(self.event))
        std::terminate();
}
PathClassification PathClassificationRead::Classification() const noexcept
{
    return _impl ? _impl->classification : PathClassification::Image;
}
bool PathClassificationRead::ReadyToIssue() const noexcept
{
    return _impl && _impl->readyToIssue;
}
bool PathClassificationRead::HasPendingRead() const noexcept
{
    return _impl && _impl->pending;
}
void PathClassificationRead::CancelAndJoinForShutdownWorker() noexcept
{
    if (!_impl || !_impl->pending)
        return;
    auto& self = *_impl;
    self.RequestCancellation();
    // Reset/recheck handles a deliberately premature test signal without freeing a live driver buffer.
    for (;;)
    {
        const bool complete = GetOverlappedResult(self.file.get(), &self.operation, &self.bytesRead, FALSE) != FALSE;
        if (complete || GetLastError() != ERROR_IO_INCOMPLETE)
            break;
        if (!ResetEvent(self.event))
            std::terminate();
        const bool rechecked = GetOverlappedResult(self.file.get(), &self.operation, &self.bytesRead, FALSE) != FALSE;
        if (rechecked || GetLastError() != ERROR_IO_INCOMPLETE)
            break;
        const bool joined = GetOverlappedResult(self.file.get(), &self.operation, &self.bytesRead, TRUE) != FALSE;
        if (joined || GetLastError() != ERROR_IO_INCOMPLETE)
            break;
    }
    self.pending = false;
    self.file.reset();
    self.Observe(PathClassificationReadNotice::Acknowledged);
}
