#pragma once

#include "Document.h"

#include <optional>
#include <span>
#include <utility>

// Exclusive worker construction. Admit the old/new record capacities, children, control blocks and reserve
// overlap before Clone/Rebuild. No mutable record borrow may survive an insertion, erase, Seal or move.
// A finished document still requires the caller's ordinary validation and checked history/session publication.
class CanvasDocumentDraft final
{
  public:
    CanvasDocumentDraft(const CanvasDocumentDraft&) = delete;
    CanvasDocumentDraft& operator=(const CanvasDocumentDraft&) = delete;
    CanvasDocumentDraft(CanvasDocumentDraft&& other) noexcept
        : _document(std::move(other._document)), _records(std::move(other._records)), _sealer(std::move(other._sealer)),
          _limit(std::exchange(other._limit, 0)), _state(std::exchange(other._state, State::Consumed))
    {
    }
    CanvasDocumentDraft& operator=(CanvasDocumentDraft&& other) noexcept
    {
        if (this != &other)
        {
            _document = std::move(other._document);
            _records = std::move(other._records);
            _sealer = std::move(other._sealer);
            _limit = std::exchange(other._limit, 0);
            _state = std::exchange(other._state, State::Consumed);
        }
        return *this;
    }
    [[nodiscard]] static std::optional<CanvasDocumentDraft> Rebuild(const CanvasDocument& state, std::size_t limit,
                                                                    CancelToken cancellation = {})
    {
        if (cancellation.IsCancelled() || limit > static_cast<std::size_t>(kMaxLayerCount))
            return std::nullopt;
        CanvasDocumentDraft result(state, limit);
        return cancellation.IsCancelled() ? std::nullopt : std::optional<CanvasDocumentDraft>(std::move(result));
    }
    [[nodiscard]] static std::optional<CanvasDocumentDraft> Clone(const CanvasDocument& source, std::size_t limit,
                                                                  CancelToken cancellation = {})
    {
        if (cancellation.IsCancelled() || source.layers.size() > limit)
            return std::nullopt;
        auto result = Rebuild(source, limit, cancellation);
        if (!result)
            return std::nullopt;
        for (const auto& layer : source.layers)
        {
            if (cancellation.IsCancelled())
                return std::nullopt;
            if (!result.value().Append(layer))
                return std::nullopt;
        }
        return cancellation.IsCancelled() ? std::nullopt : std::move(result);
    }
    [[nodiscard]] CanvasDocument& DocumentState() noexcept
    {
        return _document;
    }
    [[nodiscard]] const CanvasDocument& DocumentState() const noexcept
    {
        return _document;
    }
    [[nodiscard]] bool IsEditable() const noexcept
    {
        return _state == State::Editing;
    }
    [[nodiscard]] std::span<const ImageLayer> ReadLayers() const noexcept
    {
        if (_state == State::Editing)
            return _records;
        if (_state == State::Sealing)
            return _sealer.value().Read();
        return {};
    }
    [[nodiscard]] std::span<ImageLayer> EditLayers() noexcept
    {
        return IsEditable() ? std::span<ImageLayer>(_records) : std::span<ImageLayer>{};
    }
    [[nodiscard]] bool Append(const ImageLayer& layer)
    {
        if (!IsEditable() || _records.size() >= _limit)
            return false;
        _records.push_back(layer);
        return true;
    }
    [[nodiscard]] bool Append(ImageLayer&& layer) noexcept
    {
        if (!IsEditable() || _records.size() >= _limit)
            return false;
        _records.push_back(std::move(layer));
        return true;
    }
    [[nodiscard]] bool Insert(std::size_t index, ImageLayer&& layer) noexcept
    {
        if (!IsEditable() || index > _records.size() || _records.size() >= _limit)
            return false;
        _records.insert(_records.begin() + static_cast<std::ptrdiff_t>(index), std::move(layer));
        return true;
    }
    [[nodiscard]] bool Erase(std::size_t index) noexcept
    {
        if (!IsEditable() || index >= _records.size())
            return false;
        _records.erase(_records.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
    [[nodiscard]] std::optional<CanvasDocument> Seal(CancelToken cancellation = {})
    {
        if (_state == State::Consumed || cancellation.IsCancelled())
            return std::nullopt;
        if (_state == State::Editing)
        {
            auto owned = RecordRoot<ImageLayer>::Builder::Adopt(std::move(_records), _limit);
            if (!owned)
                return std::nullopt;
            _sealer = std::move(owned);
            _state = State::Sealing;
        }
        auto root = _sealer.value().Seal();
        if (!root)
            return std::nullopt;
        _state = State::Consumed;
        if (cancellation.IsCancelled())
            return std::nullopt;
        _document.layers = std::move(root.value());
        return std::move(_document);
    }

  private:
    enum class State
    {
        Editing,
        Sealing,
        Consumed
    };
    explicit CanvasDocumentDraft(CanvasDocument document, std::size_t limit)
        : _document(std::move(document)), _limit(limit)
    {
        _records.reserve(limit);
    }
    CanvasDocument _document;
    std::vector<ImageLayer> _records;
    std::optional<RecordRoot<ImageLayer>::Builder> _sealer;
    std::size_t _limit = 0;
    State _state = State::Editing;
};
