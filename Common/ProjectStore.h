#pragma once

#include "Document.h"
#include "ImageCodecs.h"

#include <optional>
#include <string>
#include <vector>

enum class ProjectIoStatus
{
    Ok,
    Invalid,
    UnsupportedVersion,
    TooLarge,
    MissingImage,
    Encode,
    Cancelled,
};

// Optional per-load ceilings support constrained callers and deterministic aggregate-budget tests. Values are
// clamped to the format hard limits; callers can lower, never raise, the accepted aggregate pixel count.
struct ProjectLoadPixelLimits
{
    int imagePixels = kMaxLayerPixels;
    int maskPixels = kMaxMaskPixels;
};

struct ProjectLoadResult
{
    ProjectIoStatus status = ProjectIoStatus::Invalid;
    CanvasDocument document{};
    std::optional<Guid> activeLayerID;
    int formatVersion = 0;
};

enum class ProjectIoStage
{
    ManifestReady,
    AssetDecoded,
    AssetEncoded,
    ArchiveWriting,
    Staged,
    Published,
    CreateFailed,
    WriteFailed,
    FlushFailed,
    CloseFailed,
    RotateFailed,
    PublishFailed,
};

// Optional bounded observer on the calling worker. It must not touch UI or throw. The context remains borrowed
// until the operation returns. value is completed assets, percentage for ArchiveWriting, or the immediate
// Win32 error for a failure stage; otherwise zero. Failure reports precede cleanup that may change LastError.
struct ProjectIoObserver
{
    void (*notify)(void*, ProjectIoStage, unsigned int) noexcept = nullptr;
    void* context = nullptr;
};

inline constexpr std::uint32_t kProjectReparseAttribute = 0x400u;
inline constexpr std::uint32_t kProjectMissingAttributes = 0xFFFFFFFFu;

// True when `candidate` is inside `root` and no component after `root` is a reparse point.
// `attributes` returns Win32 file attributes, or kProjectMissingAttributes when the path is missing.
// A missing component is not a reparse point: the decoder reports MissingImage. A junction is Invalid.
template <typename AttributeFn>
[[nodiscard]] bool ExplodedMemberPathAllowed(const std::wstring& root, const std::wstring& candidate,
                                             AttributeFn&& attributes)
{
    const auto trim = [](std::wstring path)
    {
        while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
        {
            path.pop_back();
        }
        return path;
    };
    const auto samePrefix = [](const std::wstring& text, const std::wstring& prefix)
    {
        if (text.size() < prefix.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < prefix.size(); ++index)
        {
            wchar_t left = text[index];
            wchar_t right = prefix[index];
            if (left >= L'A' && left <= L'Z')
            {
                left = static_cast<wchar_t>(left - L'A' + L'a');
            }
            if (right >= L'A' && right <= L'Z')
            {
                right = static_cast<wchar_t>(right - L'A' + L'a');
            }
            if (left != right)
            {
                return false;
            }
        }
        return true;
    };
    const std::wstring base = trim(root);
    const std::wstring full = trim(candidate);
    if (base.empty() || full.size() <= base.size() || !samePrefix(full, base))
    {
        return false;
    }
    if (full[base.size()] != L'\\' && full[base.size()] != L'/')
    {
        return false;
    }
    std::size_t cursor = base.size();
    while (cursor < full.size())
    {
        const std::size_t slash = full.find_first_of(L"\\/", cursor + 1);
        const std::size_t end = slash == std::wstring::npos ? full.size() : slash;
        const std::wstring component = full.substr(cursor + 1, end - (cursor + 1));
        if (component.empty() || component == L"." || component == L"..")
        {
            return false;
        }
        const std::uint32_t flags = attributes(full.substr(0, end));
        if (flags != kProjectMissingAttributes && (flags & kProjectReparseAttribute) != 0)
        {
            return false;
        }
        if (slash == std::wstring::npos)
        {
            break;
        }
        cursor = slash;
    }
    return true;
}

[[nodiscard]] bool PathLooksLikeZipProject(const wchar_t* path) noexcept;
[[nodiscard]] ProjectLoadResult LoadProject(const wchar_t* path, CancelToken cancellation = {},
                                            ProjectIoObserver observer = {}, ProjectLoadPixelLimits pixelLimits = {});
[[nodiscard]] ProjectIoStatus SaveProject(const wchar_t* path, const CanvasDocument& document,
                                          const std::optional<Guid>& activeLayerID, bool zipArchive,
                                          CancelToken cancellation = {}, ProjectIoObserver observer = {});
// The image becomes a layer above the active one (legacy EditorSession.insert), centered on `center` (a drop point,
// in document pixels) or else on the canvas; into an empty document it becomes the canvas and `center` is ignored.
[[nodiscard]] ProjectIoStatus ImportImageOnto(CanvasDocument& document, const wchar_t* path,
                                              std::optional<Guid>& activeLayerID,
                                              std::optional<Vec2> center = std::nullopt, CancelToken cancellation = {},
                                              ProjectIoObserver observer = {});
[[nodiscard]] ProjectIoStatus ExportDocumentPng(const Raster& flattened, double resolution, const wchar_t* path,
                                                CancelToken cancellation = {});
[[nodiscard]] ProjectIoStatus ExportDocumentJpeg(const Raster& flattened, const JpegExportOptions& options,
                                                 const wchar_t* path, CancelToken cancellation = {});
