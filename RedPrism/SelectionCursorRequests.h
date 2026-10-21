#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

// Owner-only fixed storage for one useful cursor probe and the latest replaceable request.
// This holds no geometry, history, UI or job owner. The caller separately admits/captures immutable
// geometry and acknowledges the original ticket before starting the next descriptor.
class SelectionCursorRequests final
{
  public:
    struct Key final
    {
        std::uint64_t document = 0;
        std::uint64_t epoch = 0;
        double x = 0;
        double y = 0;
        // Geometry can change before a coalesced chrome/cursor epoch is published.
        std::uint64_t generation = 0;
        [[nodiscard]] bool operator==(const Key&) const noexcept = default;
    };
    struct Request final
    {
        Key key;
        std::uint64_t serial = 0;
        [[nodiscard]] bool operator==(const Request&) const noexcept = default;
    };
    enum class State
    {
        Unavailable,
        Pending,
        Ready
    };
    struct Observation final
    {
        State state = State::Unavailable;
        bool inside = false;
    };
    // A nonzero seed supports deterministic exhaustion controls. Production starts at zero.
    explicit SelectionCursorRequests(std::uint64_t lastSerial = 0) noexcept : _lastSerial(lastSerial) {}

    [[nodiscard]] Observation Observe(Key key) noexcept
    {
        if (_closed)
            return {};
        if (!key.document || !key.epoch || !std::isfinite(key.x) || !std::isfinite(key.y))
        {
            Invalidate();
            return {};
        }
        if (!_latest || _latest.value().key != key)
        {
            if (_lastSerial == (std::numeric_limits<std::uint64_t>::max)())
            {
                Invalidate();
                return {}; // No wrap or stale result after an unrecordable new query.
            }
            _latest = Request{key, ++_lastSerial};
            _ready.reset();
            _failed.reset();
        }
        if (_ready && _ready.value().request == _latest.value())
            return {State::Ready, _ready.value().inside};
        return {_failed == _latest ? State::Unavailable : State::Pending, false};
    }

    // Begin precedes actual submission. Submission failure must acknowledge this exact request with
    // nullopt; repeated mouse messages then cannot spin retrying the same unavailable admission.
    [[nodiscard]] std::optional<Request> Begin() noexcept
    {
        if (_closed || _active || !_latest || _failed == _latest ||
            (_ready && _ready.value().request == _latest.value()))
            return std::nullopt;
        _active = _latest;
        return _active;
    }
    [[nodiscard]] bool Acknowledge(Request request, std::optional<bool> inside) noexcept
    {
        if (_active != std::optional{request})
            return false; // An old/foreign acknowledgement cannot consume a newer original ticket.
        _active.reset();
        if (!_closed && _latest == std::optional{request})
        {
            if (inside.has_value())
                _ready = Reply{request, inside.value()};
            else
                _failed = request;
        }
        return true;
    }
    [[nodiscard]] bool NeedsCancellation() const noexcept
    {
        return _active && (_closed || _active != _latest);
    }
    [[nodiscard]] bool Busy() const noexcept
    {
        return _active.has_value();
    }
    [[nodiscard]] bool Closed() const noexcept
    {
        return _closed;
    }
    [[nodiscard]] std::optional<Request> Active() const noexcept
    {
        return _active;
    }
    [[nodiscard]] std::optional<Request> Latest() const noexcept
    {
        return _latest;
    }
    void Invalidate() noexcept
    {
        _latest.reset();
        _ready.reset();
        _failed.reset();
        // Keep the active scalar identity until the worker's exact terminal or joined Close acknowledges it.
    }
    void Close() noexcept
    {
        _closed = true;
        Invalidate();
    }

  private:
    struct Reply final
    {
        Request request;
        bool inside;
    };
    std::optional<Request> _latest;
    std::optional<Request> _active;
    std::optional<Request> _failed;
    std::optional<Reply> _ready;
    std::uint64_t _lastSerial = 0;
    bool _closed = false;
};
static_assert(sizeof(SelectionCursorRequests) <= 256);
