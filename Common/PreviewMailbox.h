#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

// UI-owned policy. Workers receive a moved immutable request and this opaque stamp, never the mailbox.
// The mailbox is a stable Application-lifetime member; its owner joins all jobs before destroying it.
struct PreviewStamp final
{
    const void* mailbox = nullptr; // identity only; no worker may dereference it
    std::uint64_t epoch = 0;
    std::uint64_t sequence = 0;
    [[nodiscard]] bool operator==(const PreviewStamp&) const noexcept = default;
};

struct InvalidatedPreviews final
{
    std::optional<PreviewStamp> interactive;
    std::optional<PreviewStamp> refinement;
};

struct PreviewOffer final
{
    bool accepted = false;
    std::optional<PreviewStamp> supersededRefinement;
};

// One interactive request remains in flight while a single pending slot holds the newest settings. An older
// in-flight result may publish before the pending request starts: legacy Levels/Filters intentionally do this
// to avoid starving visible updates during continuous dragging. Only refinements are superseded by a tick.
template <typename Request> class PreviewMailbox final
{
    static_assert(std::is_nothrow_move_constructible_v<Request> && std::is_nothrow_move_assignable_v<Request>);

  public:
    struct Launch final
    {
        PreviewStamp stamp;
        Request input;
    };

    PreviewMailbox() noexcept = default;
    PreviewMailbox(const PreviewMailbox&) = delete;
    PreviewMailbox& operator=(const PreviewMailbox&) = delete;
    PreviewMailbox(PreviewMailbox&&) = delete;
    PreviewMailbox& operator=(PreviewMailbox&&) = delete;

    // Close the preceding edit explicitly, cancelling the jobs its returned stamps identify, before Begin.
    [[nodiscard]] bool Begin() noexcept
    {
        if (_open || _epoch == (std::numeric_limits<std::uint64_t>::max)())
        {
            return false;
        }
        ++_epoch;
        _latest = 0;
        _open = true;
        return true;
    }

    // Rejected offers retain the caller's request. Accepted offers move it into the one pending slot. The host
    // cancels the returned refinement stamp through its job ticket; it does not cancel the interactive job.
    [[nodiscard]] PreviewOffer Offer(Request& input) noexcept
    {
        if (!_open || _latest == (std::numeric_limits<std::uint64_t>::max)())
        {
            return {};
        }
        const auto superseded = std::exchange(_refinement, std::nullopt);
        _pending.emplace(Launch{PreviewStamp{this, _epoch, ++_latest}, std::move(input)});
        return {true, superseded};
    }

    [[nodiscard]] std::optional<Launch> TakeNext() noexcept
    {
        if (!_open || _active || !_pending)
        {
            return std::nullopt;
        }
        _active = _pending->stamp;
        return std::exchange(_pending, std::nullopt);
    }

    [[nodiscard]] bool CanPublish(PreviewStamp stamp) const noexcept
    {
        return _open && _active && *_active == stamp;
    }

    // The caller validates the document/target token as well, publishes only a successful result, then finishes
    // this request. A failed submission also finishes with produced=false so a pending request can proceed.
    [[nodiscard]] bool Finish(PreviewStamp stamp, bool produced) noexcept
    {
        if (!CanPublish(stamp))
        {
            return false;
        }
        _active.reset();
        if (produced)
        {
            _shown = stamp;
        }
        return true;
    }

    // At most one refinement attempt per settings sequence. A failed attempt does not create an idle retry
    // loop. A new request (including a view-region change) permits the next attempt.
    [[nodiscard]] std::optional<PreviewStamp> TakeRefinement() noexcept
    {
        if (!_open || _active || _pending || !_shown || _shown->sequence != _latest || _refinement ||
            (_attemptedRefinement && *_attemptedRefinement == *_shown))
        {
            return std::nullopt;
        }
        _refinement = _shown;
        _attemptedRefinement = _shown;
        return _refinement;
    }

    [[nodiscard]] bool CanPublishRefinement(PreviewStamp stamp) const noexcept
    {
        return _open && _refinement && *_refinement == stamp && stamp.sequence == _latest;
    }

    [[nodiscard]] bool FinishRefinement(PreviewStamp stamp) noexcept
    {
        if (!CanPublishRefinement(stamp))
        {
            return false;
        }
        _refinement.reset();
        return true;
    }

    // Preview off, identity settings, close, document replacement and Cancel invalidate both kinds immediately.
    // The owner requests cancellation for the returned stamps. Late completions may only drop their values.
    [[nodiscard]] InvalidatedPreviews Close() noexcept
    {
        const InvalidatedPreviews cancelled{_active, _refinement};
        _open = false;
        _active.reset();
        _refinement.reset();
        _pending.reset();
        _shown.reset();
        _attemptedRefinement.reset();
        return cancelled;
    }

    [[nodiscard]] bool HasPending() const noexcept
    {
        return _pending.has_value();
    }

  private:
    bool _open = false;
    std::uint64_t _epoch = 0;
    std::uint64_t _latest = 0;
    std::optional<Launch> _pending;
    std::optional<PreviewStamp> _active;
    std::optional<PreviewStamp> _shown;
    std::optional<PreviewStamp> _refinement;
    std::optional<PreviewStamp> _attemptedRefinement;
};
