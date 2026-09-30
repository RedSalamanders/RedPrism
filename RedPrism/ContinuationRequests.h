#pragma once

#include "CanvasSize.h"
#include "ChromeEvents.h"
#include "ContinuationQueue.h"
#include "ContinuationReceipt.h"
#include "EditorTool.h"

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

enum class SizeCardKind : std::uint8_t;

// Complete event-time draft, shared by the card and its owned Apply input. No pointer back to a live control.
struct HostSizeDraft final
{
    SizeCardKind kind{};
    CanvasSizeDraft canvas;
    int anchor = 4;
    int extension = 0;
    std::uint32_t customArgb = 0xFFFFFFFFu;
    std::uint32_t customBefore = 0xFFFFFFFFu;
    ImageSizeDraft image;
    double newWidth = 1920;
    double newHeight = 1080;
};

enum class HostImageOpenChoice : std::uint8_t
{
    AddLayer,
    NewProject,
    Cancel
};

enum class HostWriteFormat : std::uint8_t
{
    Project,
    Png,
    Jpeg
};

enum class HostPathAction : std::uint8_t
{
    OpenPaths,
    ImportImages,
    ImagesAsLayers,
    ImagesAsProject
};

struct HostCommandInput final
{
    int command;
};
struct HostToolInput final
{
    EditorTool tool;
};
struct HostLayerSelectInput final
{
    Guid id;
};
struct HostLayerTargetInput final
{
    Guid id;
    bool mask;
    bool toggleMask;
};
struct HostLayerInvokeInput final
{
    Guid id;
};
struct HostLayerReorderInput final
{
    Guid source;
    Guid target;
    std::optional<Guid> parent;
    DxUi::TreeDropPlace place;
};
struct HostLayerRenameInput final
{
    Guid id;
    std::optional<std::wstring> name;
};
struct HostLayerOpacityInput final
{
    Guid id;
    double opacity;
    ChromeEditPhase phase;
};
struct HostWriteInput final
{
    HostWriteFormat format;
    bool saveAs = false;
    std::optional<std::wstring> path; // an already selected path, when supplied; otherwise the runner selects it
};
struct HostPathsInput final
{
    std::vector<std::wstring> paths; // whole ordered batch occupies one queue entry
    HostPathAction action = HostPathAction::OpenPaths;
    std::optional<Vec2> dropPoint;
};
struct HostSizeApplyInput final
{
    HostSizeDraft draft;
    std::uint64_t invocation; // card lifetime, distinct from the session document serial
};
struct HostPanelOkInput final
{
    std::uint64_t opening;
};
struct HostNewInput final
{
};
struct HostCloseProjectInput final
{
};
struct HostCloseWindowInput final
{
};
struct HostEndSessionInput final
{
    std::uint64_t generation;
    std::int64_t reasons;
};

using HostRequestInput =
    std::variant<HostCommandInput, HostToolInput, HostLayerSelectInput, HostLayerTargetInput, HostLayerInvokeInput,
                 HostLayerReorderInput, HostLayerRenameInput, HostLayerOpacityInput, HostWriteInput, HostPathsInput,
                 HostSizeApplyInput, HostPanelOkInput, HostNewInput, HostCloseProjectInput, HostCloseWindowInput,
                 HostEndSessionInput>;

static_assert(std::is_nothrow_move_constructible_v<HostRequestInput> &&
              std::is_nothrow_move_assignable_v<HostRequestInput>);

// UI-owned input only: no pixel/session snapshot, worker state, HWND, host pointer or captured callable.
// Queue metadata supplies document serial and the shared input ordinal. The runner snapshots only when selected.
class HostRequest final
{
  public:
    [[nodiscard]] static HostRequest Create(HostRequestInput input)
    {
        return HostRequest(std::move(input), ContinuationCompletion::Create());
    }
    HostRequest(const HostRequest&) = delete;
    HostRequest& operator=(const HostRequest&) = delete;
    HostRequest(HostRequest&&) noexcept = default;
    HostRequest& operator=(HostRequest&& other) noexcept
    {
        if (this != &other)
        {
            // Retire old publication before its input can release owned paths/name storage.
            (void)_completion.Publish(ContinuationOutcome::Cancelled);
            input = std::move(other.input);
            _completion = std::move(other._completion);
        }
        return *this;
    }

    [[nodiscard]] ContinuationReceipt Receipt() const noexcept
    {
        return _completion.Receipt();
    }
    [[nodiscard]] bool Finish(ContinuationOutcome outcome, std::int32_t error = 0) noexcept
    {
        return _completion.Publish(outcome, error);
    }
    void Retire(ContinuationRetirement retirement) noexcept
    {
        (void)Finish(retirement == ContinuationRetirement::Stale ? ContinuationOutcome::Stale
                                                                 : ContinuationOutcome::Cancelled);
    }

    HostRequestInput input;

  private:
    HostRequest(HostRequestInput ownedInput, ContinuationCompletion completion) noexcept
        : input(std::move(ownedInput)), _completion(std::move(completion))
    {
    }
    ContinuationCompletion _completion;
};
