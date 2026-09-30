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
#include <span>
#include <string>
#include <string_view>
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
struct HostShapeFinishInput final
{
    ShapeDraft draft;
    std::uint32_t foregroundArgb;
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
                 HostEndSessionInput, HostShapeFinishInput>;

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
        friend class HostRequest;
        friend class HostSelectedOutput;
        Reservation(HostInputBudget& owner, std::size_t bytes) noexcept : _owner(&owner), _bytes(bytes) {}
        [[nodiscard]] bool ReduceTo(std::size_t bytes) noexcept
        {
            if (!_owner || bytes > _bytes)
            {
                return false;
            }
            _owner->_used -= _bytes - bytes;
            _bytes = bytes;
            return true;
        }
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

struct HostPathsView final
{
    std::span<const std::wstring_view> paths;
    HostPathAction action;
    std::optional<Vec2> dropPoint;
};
struct HostWriteView final
{
    HostWriteFormat format;
    bool saveAs;
    std::optional<std::wstring_view> path;
};
struct HostLayerRenameView final
{
    Guid id;
    std::optional<std::wstring_view> name;
};
using HostBorrowedRequestInput = std::variant<HostPathsView, HostWriteView, HostLayerRenameView>;
static_assert(std::is_nothrow_copy_constructible_v<HostBorrowedRequestInput>);

// Shell/Win32 paths may provide only a terminated pointer. Bound the scan before constructing a length-bearing
// view; never call an unbounded string length routine as part of event admission. The caller retains the buffer.
[[nodiscard]] inline std::optional<std::wstring_view> HostBoundedPathView(const wchar_t* path) noexcept
{
    if (!path)
    {
        return std::nullopt;
    }
    for (std::size_t length = 0; length <= HostInputBudget::kMaxPathUnits; ++length)
    {
        if (path[length] == L'\0')
        {
            return std::wstring_view(path, length);
        }
    }
    return std::nullopt;
}

enum class HostCopyFailureForTest : std::uint8_t
{
    None,
    Input,
    Receipt
};

enum class HostPathReadFailure : std::uint8_t
{
    None,
    Quota,
    Read
};

[[nodiscard]] inline bool HostAddStorageBytes(std::size_t& bytes, std::size_t count, std::size_t unit) noexcept
{
    if (unit == 0 || bytes > HostInputBudget::kStorageLimit || count > (HostInputBudget::kStorageLimit - bytes) / unit)
    {
        return false;
    }
    bytes += count * unit;
    return true;
}

// MSVC v145 reserves exact vector slots and rounds a directly constructed wchar string's capacity. Thirty-two
// extra code units cover that rounding and inline storage. Reconcile with actual capacities before publication;
// never retain an allocation exceeding the preflight charge. No owned text/container is constructed here.
[[nodiscard]] inline bool HostAddCopiedTextBytes(std::size_t& bytes, std::wstring_view value) noexcept
{
    return value.size() <= HostInputBudget::kStorageLimit / sizeof(wchar_t) &&
           HostAddStorageBytes(bytes, value.size() + 32, sizeof(wchar_t)) && HostAddStorageBytes(bytes, 64, 1);
}

[[nodiscard]] inline std::optional<std::size_t> HostBorrowedInputStorageBytes(
    const HostBorrowedRequestInput& input) noexcept
{
    std::size_t bytes = sizeof(HostRequestInput) + 256;
    const bool valid = std::visit(
        [&](const auto& value) noexcept
        {
            using Input = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Input, HostPathsView>)
            {
                if (value.paths.size() > HostInputBudget::kMaxPaths ||
                    !HostAddStorageBytes(bytes, value.paths.size(), sizeof(std::wstring)) ||
                    !HostAddStorageBytes(bytes, 64, 1))
                {
                    return false;
                }
                for (const auto path : value.paths)
                {
                    if (path.size() > HostInputBudget::kMaxPathUnits || !HostAddCopiedTextBytes(bytes, path))
                    {
                        return false;
                    }
                }
                return true;
            }
            else if constexpr (std::is_same_v<Input, HostWriteView>)
            {
                return !value.path || (value.path->size() <= HostInputBudget::kMaxPathUnits &&
                                       HostAddCopiedTextBytes(bytes, value.path.value()));
            }
            else
            {
                // Name trimming/UTF-8 validity belongs to the document operation, not event retention.
                return !value.name || HostAddCopiedTextBytes(bytes, value.name.value());
            }
        },
        input);
    return valid ? std::optional<std::size_t>(bytes) : std::nullopt;
}

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

// A path selected after request admission is charged against the same host budget. Destroy its owned string
// before returning the ticket; moves release an old string before releasing its corresponding charge.
class HostSelectedOutput final
{
  public:
    HostSelectedOutput(const HostSelectedOutput&) = delete;
    HostSelectedOutput& operator=(const HostSelectedOutput&) = delete;
    HostSelectedOutput(HostSelectedOutput&&) noexcept = default;
    HostSelectedOutput& operator=(HostSelectedOutput&& other) noexcept
    {
        if (this != &other)
        {
            _path = std::move(other._path);
            _reservation = std::move(other._reservation);
        }
        return *this;
    }
    [[nodiscard]] std::wstring_view Path() const noexcept
    {
        return _path;
    }

  private:
    friend class HostRequest;
    [[nodiscard]] static std::optional<HostSelectedOutput> TryCreate(std::wstring_view value, HostInputBudget& budget,
                                                                     bool failCopyAllocationForTest)
    {
        std::size_t bytes = sizeof(HostSelectedOutput) + 64;
        if (value.size() > HostInputBudget::kMaxPathUnits || !HostAddCopiedTextBytes(bytes, value))
        {
            return std::nullopt;
        }
        auto reservation = budget.TryReserve(bytes);
        if (!reservation)
        {
            return std::nullopt;
        }
        std::wstring path(value);
        if (failCopyAllocationForTest)
        {
            throw std::bad_alloc{}; // new path exists, but the active request's old output remains untouched
        }
        std::size_t actualBytes = sizeof(HostSelectedOutput) + 64;
        if (path.capacity() >= HostInputBudget::kStorageLimit / sizeof(wchar_t) ||
            !HostAddStorageBytes(actualBytes, path.capacity() + 1, sizeof(wchar_t)) ||
            !HostAddStorageBytes(actualBytes, 64, 1) || !reservation.value().ReduceTo(actualBytes))
        {
            return std::nullopt;
        }
        return HostSelectedOutput(std::move(path), std::move(reservation.value()));
    }
    HostSelectedOutput(std::wstring path, HostInputBudget::Reservation reservation) noexcept
        : _reservation(std::move(reservation)), _path(std::move(path))
    {
    }
    HostInputBudget::Reservation _reservation;
    std::wstring _path;
};

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
    [[nodiscard]] static std::optional<HostRequest> TryCopyPaths(
        std::span<const std::wstring_view> paths, HostPathAction action, std::optional<Vec2> dropPoint,
        HostInputBudget& budget, HostCopyFailureForTest failure = HostCopyFailureForTest::None)
    {
        return TryCopy(HostPathsView{paths, action, dropPoint}, budget, failure);
    }
    // Query lengths before calling this factory. The immediate reader fills admitted storage while the source
    // remains alive; no reader, source pointer or temporary uncharged string batch survives admission.
    template <typename Reader>
    [[nodiscard]] static std::optional<HostRequest> TryReadPaths(
        std::span<const std::size_t> lengths, HostPathAction action, std::optional<Vec2> dropPoint,
        HostInputBudget& budget, Reader&& reader, HostPathReadFailure& failure,
        HostCopyFailureForTest fault = HostCopyFailureForTest::None)
    {
        static_assert(std::is_nothrow_invocable_r_v<bool, Reader&, std::size_t, std::span<wchar_t>>);
        failure = HostPathReadFailure::Quota;
        std::size_t bytes = sizeof(HostRequestInput) + 256;
        if (lengths.size() > HostInputBudget::kMaxPaths ||
            !HostAddStorageBytes(bytes, lengths.size(), sizeof(std::wstring)) || !HostAddStorageBytes(bytes, 64, 1))
        {
            return std::nullopt;
        }
        for (const auto length : lengths)
        {
            if (length > HostInputBudget::kMaxPathUnits || !HostAddStorageBytes(bytes, length + 32, sizeof(wchar_t)) ||
                !HostAddStorageBytes(bytes, 64, 1))
            {
                return std::nullopt;
            }
        }
        auto reservation = budget.TryReserve(bytes);
        if (!reservation)
        {
            return std::nullopt;
        }
        HostPathsInput paths{{}, action, dropPoint};
        paths.paths.reserve(lengths.size());
        for (std::size_t index = 0; index < lengths.size(); ++index)
        {
            paths.paths.emplace_back(lengths[index], L'\0');
            auto& path = paths.paths.back();
            if (!reader(index, std::span<wchar_t>(path.data(), path.size() + 1)))
            {
                failure = HostPathReadFailure::Read;
                return std::nullopt;
            }
            if (fault == HostCopyFailureForTest::Input)
            {
                throw std::bad_alloc{};
            }
        }
        HostRequestInput input = std::move(paths);
        const auto actualBytes = HostInputStorageBytes(input);
        if (!actualBytes || !reservation.value().ReduceTo(actualBytes.value()))
        {
            return std::nullopt;
        }
        if (fault == HostCopyFailureForTest::Receipt)
        {
            throw std::bad_alloc{};
        }
        auto completion = ContinuationCompletion::Create();
        failure = HostPathReadFailure::None;
        return HostRequest(std::move(input), std::move(completion), std::move(reservation.value()));
    }
    [[nodiscard]] static std::optional<HostRequest> TryCopyWrite(
        HostWriteFormat format, bool saveAs, std::optional<std::wstring_view> path, HostInputBudget& budget,
        HostCopyFailureForTest failure = HostCopyFailureForTest::None)
    {
        return TryCopy(HostWriteView{format, saveAs, path}, budget, failure);
    }
    [[nodiscard]] static std::optional<HostRequest> TryCopyRename(
        Guid id, std::optional<std::wstring_view> name, HostInputBudget& budget,
        HostCopyFailureForTest failure = HostCopyFailureForTest::None)
    {
        return TryCopy(HostLayerRenameView{id, name}, budget, failure);
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
            _selectedOutput = std::move(other._selectedOutput);
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
    // Selection is an active-stage operation. On refusal or allocation failure preserve the old output; the
    // replacement's reservation must coexist with it. The returned view is borrowed only while this owner lives.
    [[nodiscard]] bool TrySelectOutput(std::wstring_view path, HostInputBudget& budget,
                                       bool failCopyAllocationForTest = false)
    {
        if (_reservation._owner != &budget || Receipt().Read().outcome != ContinuationOutcome::Pending)
        {
            return false;
        }
        auto output = HostSelectedOutput::TryCreate(path, budget, failCopyAllocationForTest);
        if (!output)
        {
            return false;
        }
        _selectedOutput = std::move(output);
        return true;
    }
    [[nodiscard]] std::optional<std::wstring_view> SelectedOutputPath() const noexcept
    {
        return _selectedOutput ? std::optional<std::wstring_view>(_selectedOutput.value().Path()) : std::nullopt;
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
    [[nodiscard]] static std::optional<HostRequest> TryCopy(const HostBorrowedRequestInput& borrowed,
                                                            HostInputBudget& budget, HostCopyFailureForTest failure)
    {
        const auto bytes = HostBorrowedInputStorageBytes(borrowed);
        if (!bytes)
        {
            return std::nullopt;
        }
        auto reservation = budget.TryReserve(bytes.value());
        if (!reservation)
        {
            return std::nullopt;
        }
        HostRequestInput input = std::visit(
            [failure](const auto& value) -> HostRequestInput
            {
                using Input = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Input, HostPathsView>)
                {
                    HostPathsInput owned{{}, value.action, value.dropPoint};
                    owned.paths.reserve(value.paths.size());
                    for (const auto path : value.paths)
                    {
                        owned.paths.emplace_back(path);
                        if (failure == HostCopyFailureForTest::Input)
                        {
                            throw std::bad_alloc{}; // exercise retirement with a partially copied ordered batch
                        }
                    }
                    return owned;
                }
                else if constexpr (std::is_same_v<Input, HostWriteView>)
                {
                    return HostWriteInput{value.format, value.saveAs,
                                          value.path ? std::optional<std::wstring>(value.path.value()) : std::nullopt};
                }
                else
                {
                    return HostLayerRenameInput{value.id, value.name ? std::optional<std::wstring>(value.name.value())
                                                                     : std::nullopt};
                }
            },
            borrowed);
        if (failure == HostCopyFailureForTest::Input)
        {
            throw std::bad_alloc{};
        }
        const auto actualBytes = HostInputStorageBytes(input);
        if (!actualBytes || !reservation.value().ReduceTo(actualBytes.value()))
        {
            return std::nullopt;
        }
        if (failure == HostCopyFailureForTest::Receipt)
        {
            throw std::bad_alloc{};
        }
        auto completion = ContinuationCompletion::Create();
        return HostRequest(std::move(input), std::move(completion), std::move(reservation.value()));
    }
    // Destruction publishes first, then releases the owned input, and only then returns its charge.
    HostInputBudget::Reservation _reservation;

    std::optional<HostSelectedOutput> _selectedOutput;
    HostRequestInput _input;
    HostRequest(HostRequestInput ownedInput, ContinuationCompletion completion,
                HostInputBudget::Reservation reservation = {}) noexcept
        : _reservation(std::move(reservation)), _input(std::move(ownedInput)), _completion(std::move(completion))
    {
    }
    ContinuationCompletion _completion;
};

static_assert(sizeof(HostRequest) <= sizeof(HostRequestInput) + 256);
