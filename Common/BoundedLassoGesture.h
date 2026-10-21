#pragma once

#include "BoundedLassoPoints.h"
#include "Selection.h"

#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <utility>

// Owner-only gesture metadata over an already admitted, worker-created arena. This is not admission,
// history authority or a document publication. The caller binds original accounting before BeginOwner.
class BoundedLassoGesture final
{
  public:
    enum class Outcome : std::uint8_t
    {
        Changed,
        Unchanged,
        Refused
    };
    struct Key final
    {
        std::uint64_t opening = 0;
        std::uint64_t revision = 0;
        [[nodiscard]] bool operator==(const Key&) const noexcept = default;
    };
    struct Display final
    {
        BoundedLassoPoints::Snapshot points;
        Key key;
        LassoKind kind = LassoKind::Freehand;
        SelectionMode mode = SelectionMode::New;
        bool antialiased = true;
        std::optional<Vec2> cursor;
        std::optional<Vec2> anchor;
    };
    explicit BoundedLassoGesture(BoundedLassoPoints points) noexcept : _points(std::move(points)) {}
    BoundedLassoGesture(const BoundedLassoGesture&) = delete;
    BoundedLassoGesture& operator=(const BoundedLassoGesture&) = delete;
    BoundedLassoGesture(BoundedLassoGesture&& source) noexcept
        : _points(std::move(source._points)), _key(std::exchange(source._key, {})), _kind(source._kind),
          _mode(source._mode), _antialiased(source._antialiased), _first(source._first),
          _anchor(std::exchange(source._anchor, {})), _cursor(std::exchange(source._cursor, {})),
          _state(std::exchange(source._state, State::Cancelled))
    {
    }
    BoundedLassoGesture& operator=(BoundedLassoGesture&&) = delete;

    [[nodiscard]] bool BeginOwner(std::uint64_t opening, LassoKind kind, SelectionMode mode, bool antialiased,
                                  Vec2 point) noexcept
    {
        if (_state != State::Fresh || !opening || !Finite(point) || kind > LassoKind::Ellipse ||
            mode > SelectionMode::Subtract || _points.CountOwner())
            return false;
        const bool marquee = kind == LassoKind::Rectangle || kind == LassoKind::Ellipse;
        const Vec2 first = marquee ? Vec2{std::round(point.x), std::round(point.y)} : point;
        if (!_points.PushOwner(first))
            return false;
        _key = {opening, 1};
        _first = first;
        _kind = kind;
        _mode = mode;
        _antialiased = antialiased;
        _anchor = marquee ? std::optional(first) : std::nullopt;
        _state = State::Active;
        return true;
    }
    [[nodiscard]] Key CurrentOwner() const noexcept
    {
        return _key;
    }
    [[nodiscard]] bool ActiveOwner() const noexcept
    {
        return _state == State::Active;
    }
    [[nodiscard]] std::uint32_t PointCountOwner() const noexcept
    {
        return _points.CountOwner();
    }
    [[nodiscard]] std::optional<Vec2> FirstOwner() const noexcept
    {
        return _points.CountOwner() ? std::optional(_first) : std::nullopt;
    }
    [[nodiscard]] Display CaptureOwner() const noexcept
    {
        return {_points.CaptureOwner(), _key, _kind, _mode, _antialiased, _cursor, _anchor};
    }
    [[nodiscard]] Outcome ExtendOwner(Key expected, Vec2 point) noexcept
    {
        if (!Matches(expected) || _anchor || !Finite(point))
            return Outcome::Refused;
        const auto last = _points.LastOwner();
        if (last && std::hypot(point.x - last->x, point.y - last->y) < 0.25)
            return Outcome::Unchanged;
        if (!CanAdvance() || !_points.PushOwner(point))
            return Outcome::Refused;
        ++_key.revision;
        return Outcome::Changed;
    }
    [[nodiscard]] Outcome CursorOwner(Key expected, std::optional<Vec2> point) noexcept
    {
        if (!Matches(expected) || _kind != LassoKind::Polygonal || (point && !Finite(point.value())))
            return Outcome::Refused;
        if (point == _cursor)
            return Outcome::Unchanged;
        if (!CanAdvance())
            return Outcome::Refused;
        _cursor = point;
        ++_key.revision;
        return Outcome::Changed;
    }
    [[nodiscard]] Outcome PopOwner(Key expected) noexcept
    {
        if (!Matches(expected) || !CanAdvance() || !_points.PopOwner())
            return Outcome::Refused;
        ++_key.revision;
        if (!_points.CountOwner())
        {
            _state = State::Cancelled;
            _cursor.reset();
        }
        return Outcome::Changed;
    }
    [[nodiscard]] Outcome MarqueeOwner(Key expected, Vec2 point, bool square, bool fromCenter) noexcept
    {
        if (!Matches(expected) || !_anchor || !Finite(point))
            return Outcome::Refused;
        const auto box = DragBox(_anchor.value(), point, square, fromCenter);
        if (!std::isfinite(box.minX) || !std::isfinite(box.minY) || !std::isfinite(box.maxX) ||
            !std::isfinite(box.maxY))
            return Outcome::Refused;
        const auto before = _points.CaptureOwner().Bounds();
        if (_points.CountOwner() == 4 && before && before.value() == box)
            return Outcome::Unchanged;
        if (!CanAdvance())
            return Outcome::Refused;
        const std::array<Vec2, 4> corners{Vec2{box.minX, box.minY}, Vec2{box.maxX, box.minY}, Vec2{box.maxX, box.maxY},
                                          Vec2{box.minX, box.maxY}};
        _points.ClearOwner();
        _first = corners.front();
        // A live valid arena always has its inline 64-point tail. All four finite points fit without
        // sealing a block, so failure here would be corrupted invariants, not partial capacity refusal.
        for (const auto corner : corners)
            if (!_points.PushOwner(corner))
                std::terminate();
        ++_key.revision;
        return Outcome::Changed;
    }
    [[nodiscard]] std::optional<Display> FinishOwner(Key expected,
                                                     std::optional<bool> finishAntialiased = std::nullopt) noexcept
    {
        if (!Matches(expected) || !CanAdvance())
            return std::nullopt;
        auto display = CaptureOwner();
        display.antialiased = finishAntialiased.value_or(_antialiased);
        display.cursor.reset(); // Rubber band is display-only; the original final outline excludes it.
        _terminal = display;
        _state = State::Finished;
        ++_key.revision;
        _points.ClearOwner();
        _cursor.reset();
        return display;
    }
    [[nodiscard]] bool MatchesTerminalOwner(const Display& display) const noexcept
    {
        return _state == State::Finished && _terminal && display.key == _terminal->key &&
               display.kind == _terminal->kind && display.mode == _terminal->mode &&
               display.antialiased == _terminal->antialiased && display.cursor == _terminal->cursor &&
               display.anchor == _terminal->anchor && display.points.SameCapture(_terminal->points);
    }
    [[nodiscard]] bool CancelOwner(Key expected) noexcept
    {
        if (!Matches(expected))
            return false;
        _state = State::Cancelled;
        _points.ClearOwner();
        _cursor.reset();
        return true; // Exclusion uses terminal state, so cancellation does not need a wrapping revision.
    }
    [[nodiscard]] bool SetRevisionForTest(std::uint64_t revision) noexcept
    {
        if (!ActiveOwner() || !revision || revision < _key.revision)
            return false;
        _key.revision = revision;
        return true;
    }

  private:
    enum class State : std::uint8_t
    {
        Fresh,
        Active,
        Finished,
        Cancelled
    };
    [[nodiscard]] static bool Finite(Vec2 point) noexcept
    {
        return std::isfinite(point.x) && std::isfinite(point.y);
    }
    [[nodiscard]] bool Matches(Key key) const noexcept
    {
        return _state == State::Active && key == _key;
    }
    [[nodiscard]] bool CanAdvance() const noexcept
    {
        return _key.revision != (std::numeric_limits<std::uint64_t>::max)();
    }
    BoundedLassoPoints _points;
    Key _key;
    LassoKind _kind = LassoKind::Freehand;
    SelectionMode _mode = SelectionMode::New;
    bool _antialiased = true;
    Vec2 _first{};
    std::optional<Vec2> _anchor;
    std::optional<Vec2> _cursor;
    std::optional<Display> _terminal;
    State _state = State::Fresh;
};
