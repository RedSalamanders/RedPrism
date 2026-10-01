#pragma once

#include "Document.h"
#include "ImageCodecs.h"

#include <cstddef>
#include <cstdint>
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

inline constexpr std::size_t kMaxProjectManifestBytes = 4u * 1024u * 1024u;
inline constexpr std::size_t kMaxProjectEncodedAssetBytes = 512u * 1024u * 1024u;

// Caller ceilings are checked before corresponding input/output allocations. Clamp to hard format limits;
// adaptive worker probes can lower them but cannot authorize larger inputs or outputs.
struct ProjectLoadLimits
{
    int imagePixels = kMaxLayerPixels;
    int maskPixels = kMaxMaskPixels;
    std::size_t layerCount = kMaxLayerCount;
    std::size_t manifestBytes = kMaxProjectManifestBytes;
    std::size_t encodedAssetBytes = kMaxProjectEncodedAssetBytes;
};
using ProjectLoadPixelLimits = ProjectLoadLimits;

struct ProjectLoadProbeResult
{
    ProjectIoStatus status = ProjectIoStatus::Invalid;
    ProjectLoadLimits limits{0, 0, 0, 0, 0};
    int formatVersion = 0;
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
    AssetInspected,
    AssetEncoded,
    ArchiveWriting,
    Staged,
    Published,
    CreateFailed,
    WriteFailed,
    FlushFailed,
    CloseFailed,
    RotateFailed,  // Retained numeric/API slot; new saves report the named failure stages below.
    PublishFailed, // Retained numeric/API slot; new saves report the named failure stages below.
    DestinationToBackupFailed,
    StagingToDestinationFailed,
    RollbackFailed,
    BackupCleanupFailed,
    StagingCleanupFailed,
    RollbackRetryScheduled, // Immediate native rename error; a bounded worker timer wait follows.
};

// Optional bounded observer on the calling worker. It must not touch UI or throw. The context remains borrowed
// until the operation returns. value is completed assets, percentage for ArchiveWriting, or the immediate
// Win32 error for a failure stage or RollbackRetryScheduled; otherwise zero. Failure reports precede cleanup
// that may change LastError.
struct ProjectIoObserver
{
    void (*notify)(void*, ProjectIoStage, unsigned int) noexcept = nullptr;
    void* context = nullptr;
};

// Optional deterministic failure seam for exploded-directory publication. The callback returns a Win32 error
// value for the named operation or zero to run the real operation. It is borrowed for one SaveProject call;
// there is no process-wide test state. Injected operation errors are final; only native rollback errors get the
// bounded recovery attempts. RecoveryTimerCreate/Arm/Wait fault results stop that recovery operation while the
// observer still receives the native rollback rename error. ZIP saves do not consult this policy.
enum class ProjectSaveFaultStage : std::uint8_t
{
    DestinationToBackupRename,
    StagingToDestinationRename,
    RollbackRename,
    BackupCleanup,
    StagingCleanup,
    RecoveryTimerCreate,
    RecoveryTimerArm,
    RecoveryTimerWait,
};
static_assert(static_cast<std::uint8_t>(ProjectSaveFaultStage::RollbackRename) == 2);
static_assert(static_cast<std::uint8_t>(ProjectSaveFaultStage::RecoveryTimerCreate) == 5);
static_assert(static_cast<std::uint8_t>(ProjectSaveFaultStage::RecoveryTimerArm) == 6);
static_assert(static_cast<std::uint8_t>(ProjectSaveFaultStage::RecoveryTimerWait) == 7);

struct ProjectSaveFaultPolicy
{
    std::uint32_t (*failure)(void*, ProjectSaveFaultStage) noexcept = nullptr;
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
                                            ProjectIoObserver observer = {}, ProjectLoadLimits limits = {});
// Worker-only manifest and fixed PNG-header inspection. No pixel decoding or retained asset-sized vector.
// Reapply returned limits with LoadProject; they bound a reopened source even if its contents have changed.
[[nodiscard]] ProjectLoadProbeResult ProbeProjectLoad(const wchar_t* path, CancelToken cancellation = {},
                                                      ProjectIoObserver observer = {});
[[nodiscard]] ProjectIoStatus SaveProject(const wchar_t* path, const CanvasDocument& document,
                                          const std::optional<Guid>& activeLayerID, bool zipArchive,
                                          CancelToken cancellation = {}, ProjectIoObserver observer = {},
                                          ProjectSaveFaultPolicy faultPolicy = {});
// The image becomes a layer above the active one (legacy EditorSession.insert), centered on `center` (a drop point,
// in document pixels) or else on the canvas; into an empty document it becomes the canvas and `center` is ignored.
[[nodiscard]] ProjectIoStatus ImportImageOnto(CanvasDocument& document, const wchar_t* path,
                                              std::optional<Guid>& activeLayerID,
                                              std::optional<Vec2> center = std::nullopt, CancelToken cancellation = {},
                                              ProjectIoObserver observer = {}, int maximumImagePixels = kMaxLayerPixels,
                                              std::uint64_t maximumEncodedBytes = kMaxImageEncodedBytes);
[[nodiscard]] ProjectIoStatus ExportDocumentPng(const Raster& flattened, double resolution, const wchar_t* path,
                                                CancelToken cancellation = {});
[[nodiscard]] ProjectIoStatus ExportDocumentJpeg(const Raster& flattened, const JpegExportOptions& options,
                                                 const wchar_t* path, CancelToken cancellation = {});
