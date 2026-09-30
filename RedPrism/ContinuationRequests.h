#pragma once

#include "CanvasSize.h"
#include "ChromeEvents.h"
#include "ContinuationQueue.h"
#include "ContinuationReceipt.h"
#include "EditorTool.h"

#include <cstddef>
#include <cstdint>
#include <new>
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

// Owner-thread accounting, independent of pixel/job reservations. A ticket follows the request through queue,
// active work and moves. Declare the budget before its request owners so it outlives their tickets.
class HostInputBudget final
{
  public:
    static constexpr std::size_t kStorageLimit = 1024 * 1024;
    static constexpr std::size_t kMaxPaths = 256;
    static constexpr std::size_t kMaxPathUnits = 32767;

    class Reservation final
    {
      public:
        Reservation() noexcept = default;
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        Reservation(Reservation&& other) noexcept
            : _owner(std::exchange(other._owner, nullptr)), _bytes(std::exchange(other._bytes, 0))
        {
        }
        Reservation& operator=(Reservation&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                _owner = std::exchange(other._owner, nullptr);
                _bytes = std::exchange(other._bytes, 0);
            }
            return *this;
        }
        ~Reservation() noexcept
        {
            Reset();
        }

      private:
        friend class HostInputBudget;
        Reservation(HostInputBudget& owner, std::size_t bytes) noexcept : _owner(&owner), _bytes(bytes) {}
        void Reset() noexcept
        {
            if (_owner)
            {
                _owner->_used -= _bytes;
                _owner = nullptr;
                _bytes = 0;
            }
        }
        HostInputBudget* _owner = nullptr;
        std::size_t _bytes = 0;
    };

    HostInputBudget() noexcept = default;
    HostInputBudget(const HostInputBudget&) = delete;
    HostInputBudget& operator=(const HostInputBudget&) = delete;

    [[nodiscard]] std::optional<Reservation> TryReserve(std::size_t bytes) noexcept
    {
        if (bytes > kStorageLimit - _used)
        {
            return std::nullopt;
        }
        _used += bytes;
        return Reservation(*this, bytes);
    }
    [[nodiscard]] std::size_t Used() const noexcept
    {
        return _used;
    }

  private:
    std::size_t _used = 0;
};

// Charge retained capacities, not just live lengths: an almost-empty moved string/vector can retain a large
// allocation. Fixed state/receipt and 64 bytes per container conservatively cover ordinary allocation metadata;
// global allocator headroom still applies. At most 256 paths are inspected, with no allocation or pixel access.
[[nodiscard]] inline std::optional<std::size_t> HostInputStorageBytes(const HostRequestInput& input) noexcept
{
    static_assert(sizeof(HostRequestInput) + 256 <= HostInputBudget::kStorageLimit);
    if (input.valueless_by_exception())
    {
        return std::nullopt;
    }
    std::size_t bytes = sizeof(HostRequestInput) + 256;
    const auto add = [&bytes](std::size_t count, std::size_t unit) noexcept
    {
        if (count > (HostInputBudget::kStorageLimit - bytes) / unit)
        {
            return false;
        }
        bytes += count * unit;
        return true;
    };
    const auto string = [&add](const std::wstring& value) noexcept
    {
        return value.capacity() < HostInputBudget::kStorageLimit / sizeof(wchar_t) &&
               add(value.capacity() + 1, sizeof(wchar_t)) && add(64, 1);
    };
    const bool valid = std::visit(
        [&](const auto& value) noexcept
        {
            using Input = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Input, HostPathsInput>)
            {
                if (value.paths.size() > HostInputBudget::kMaxPaths ||
                    !add(value.paths.capacity(), sizeof(std::wstring)) || !add(64, 1))
                {
                    return false;
                }
                for (const auto& path : value.paths)
                {
                    if (path.size() > HostInputBudget::kMaxPathUnits || !string(path))
                    {
                        return false;
                    }
                }
                return true;
            }
            else if constexpr (std::is_same_v<Input, HostWriteInput>)
            {
                return !value.path ||
                       (value.path->size() <= HostInputBudget::kMaxPathUnits && string(value.path.value()));
            }
            else if constexpr (std::is_same_v<Input, HostLayerRenameInput>)
            {
                return !value.name || string(value.name.value());
            }
            else
            {
                return true;
            }
        },
        input);
    return valid ? std::optional<std::size_t>(bytes) : std::nullopt;
}

// UI-owned input only: no pixel/session snapshot, worker state, HWND, host pointer or captured callable.
// Queue metadata supplies document serial and the shared input ordinal. The runner snapshots only when selected.
class HostRequest final
{
  public:
    [[nodiscard]] static HostRequest Create(HostRequestInput input)
    {
        return HostRequest(std::move(input), ContinuationCompletion::Create());
    }
    // Host admission takes an already-owned, preflighted event payload. On refusal/allocation failure the caller
    // retains its input; no result remains Pending. Producers must bound borrowed values before copying them.
    [[nodiscard]] static std::optional<HostRequest> TryCreate(HostRequestInput& input, HostInputBudget& budget,
                                                              bool failReceiptAllocationForTest = false)
    {
        const auto bytes = HostInputStorageBytes(input);
        if (!bytes)
        {
            return std::nullopt;
        }
        auto reservation = budget.TryReserve(bytes.value());
        if (!reservation)
        {
            return std::nullopt;
        }
        if (failReceiptAllocationForTest)
        {
            throw std::bad_alloc{}; // receipt-allocation control after reservation, before input ownership moves
        }
        auto completion = ContinuationCompletion::Create(); // throwing here returns the ticket without moving input
        return HostRequest(std::move(input), std::move(completion), std::move(reservation.value()));
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
            _input = std::move(other._input);
            _completion = std::move(other._completion);
            _reservation = std::move(other._reservation); // old input is gone before returning its charge
        }
        return *this;
    }

    [[nodiscard]] ContinuationReceipt Receipt() const noexcept
    {
        return _completion.Receipt();
    }
    [[nodiscard]] const HostRequestInput& Input() const noexcept
    {
        return _input;
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

  private:
    // Destruction publishes first, then releases the owned input, and only then returns its charge.
    HostInputBudget::Reservation _reservation;

    HostRequestInput _input;
    HostRequest(HostRequestInput ownedInput, ContinuationCompletion completion,
                HostInputBudget::Reservation reservation = {}) noexcept
        : _reservation(std::move(reservation)), _input(std::move(ownedInput)), _completion(std::move(completion))
    {
    }
    ContinuationCompletion _completion;
};
