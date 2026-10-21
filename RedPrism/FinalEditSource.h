#pragma once

#include "DocumentSizeHistory.h"
#include "PixelMoveClosureWork.h"

#include <type_traits>
#include <utility>
#include <variant>

// Exclusively owned by one original's worker/owner stages. This adapter creates no job, admission,
// document mutation authority or device. The immutable final borrow ends at checked publication.
class FinalEditSource final
{
    using Storage = std::variant<PreparedPixelMoveClosure, PreparedDocumentSize>;
    Storage _source;

  public:
    enum class Kind
    {
        PixelMove,
        Size
    };
    explicit FinalEditSource(PreparedPixelMoveClosure&& source) noexcept
        : _source(std::in_place_type<PreparedPixelMoveClosure>, std::move(source))
    {
    }
    explicit FinalEditSource(PreparedDocumentSize&& source) noexcept
        : _source(std::in_place_type<PreparedDocumentSize>, std::move(source))
    {
    }
    FinalEditSource(const FinalEditSource&) = delete;
    FinalEditSource& operator=(const FinalEditSource&) = delete;
    FinalEditSource(FinalEditSource&&) noexcept = default;
    FinalEditSource& operator=(FinalEditSource&&) = delete;
    [[nodiscard]] Kind SourceKind() const noexcept
    {
        return _source.index() == 0 ? Kind::PixelMove : Kind::Size;
    }
    [[nodiscard]] const CanvasDocument* FinalDocument() const noexcept
    {
        return std::visit([](const auto& source) noexcept { return source.FinalDocument(); }, _source);
    }
    [[nodiscard]] bool IsCurrent(const DocumentSession& session) const noexcept
    {
        return std::visit(
            [&](const auto& source) noexcept
            {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(source)>, PreparedPixelMoveClosure>)
                    return session.IsPreparedDeferredPixelMoveClosureCurrent(source);
                else
                    return session.IsPreparedDocumentSizeCurrent(source);
            },
            _source);
    }
    [[nodiscard]] DocumentHistory::ResidencyValidation TryAcquireHistoryResidency(
        DocumentHistory::ResidencyPermit& permit) noexcept
    {
        return std::visit([&](auto& source) noexcept { return source.TryAcquireHistoryResidency(permit); }, _source);
    }
    [[nodiscard]] bool RefreshHistoryResidency(CancelToken cancellation)
    {
        return std::visit([&](auto& source) { return source.RefreshHistoryResidency(cancellation); }, _source);
    }
    [[nodiscard]] bool ApplyIfCurrent(DocumentSession& session) noexcept
    {
        return std::visit(
            [&](auto& source) noexcept
            {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(source)>, PreparedPixelMoveClosure>)
                    return session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(source);
                else
                    return session.ApplyPreparedDocumentSizeIfCurrent(source);
            },
            _source);
    }
};

static_assert(std::is_nothrow_move_constructible_v<FinalEditSource>);
