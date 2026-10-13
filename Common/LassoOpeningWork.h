#pragma once

#include "BoundedLassoGesture.h"
#include "HistoryRestoreWork.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

// The host reserves opening arena, input stream, wrapper/control, and all retained sources before capture.
// This worker-created writer is acquired only after its original creator joins and its memory is bound.
class LassoOpeningInput final
{
  public:
    LassoOpeningInput(const LassoOpeningInput&) = delete;
    LassoOpeningInput& operator=(const LassoOpeningInput&) = delete;
    LassoOpeningInput(LassoOpeningInput&& source) noexcept
        : _authority(std::move(source._authority)), _token(std::move(source._token)), _point(source._point),
          _mode(source._mode), _kind(source._kind), _marquee(source._marquee), _opening(source._opening),
          _intent(source._intent), _valid(std::exchange(source._valid, false))
    {
    }
    LassoOpeningInput& operator=(LassoOpeningInput&&) = delete;

  private:
    friend class DocumentSession;
    friend class PreparedLassoOpening;
    LassoOpeningInput(HistoryRestoreAuthority authority, JobToken token, Vec2 point, SelectionMode mode, LassoKind kind,
                      bool marquee, std::uint64_t opening, std::uint64_t intent) noexcept
        : _authority(std::move(authority)), _token(std::move(token)), _point(point), _mode(mode), _kind(kind),
          _marquee(marquee), _opening(opening), _intent(intent)
    {
    }
    HistoryRestoreAuthority _authority;
    JobToken _token;
    Vec2 _point;
    SelectionMode _mode;
    LassoKind _kind;
    bool _marquee;
    std::uint64_t _opening;
    std::uint64_t _intent;
    bool _valid = true;
};
class PreparedLassoOpening final
{
  public:
    PreparedLassoOpening(const PreparedLassoOpening&) = delete;
    PreparedLassoOpening& operator=(const PreparedLassoOpening&) = delete;
    PreparedLassoOpening(PreparedLassoOpening&&) noexcept = default;
    PreparedLassoOpening& operator=(PreparedLassoOpening&&) = delete;
    [[nodiscard]] static std::uint64_t MemoryBytes(std::uint32_t blocks) noexcept;

  private:
    friend class DocumentSession;
    explicit PreparedLassoOpening(LassoOpeningInput input) noexcept : _input(std::move(input)) {}
    LassoOpeningInput _input;
    std::shared_ptr<BoundedLassoGesture> _writer;
    std::shared_ptr<const SharedPublicationMemory> _memory;
    std::uint64_t _retirementBytes = 0;
    bool _applied = false;
};
