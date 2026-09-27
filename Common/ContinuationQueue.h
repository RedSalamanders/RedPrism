#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

enum class ContinuationKind
{
    Command,
    Undo,
    Redo,
    WandNew,
    WandAdd,
    WandSubtract,
    WandIntersect,
    Import
};

enum class ContinuationAdmission
{
    Queued,
    Coalesced,
    Full,
    InvalidOrder
};

// UI-owned event parameters, never a document snapshot. The owner takes immutable pixels/selection and the
// result token only when TakeReady returns the command, after preceding history work has applied or dropped.
// Ordinals come from one host sequence shared with recorded strokes, preserving their relative input order.
template <typename Request> class ContinuationQueue final
{
    static_assert(std::is_nothrow_move_constructible_v<Request> && std::is_nothrow_move_assignable_v<Request>);

  public:
    static constexpr std::size_t kCapacity = 8;
    struct Entry final
    {
        std::uint64_t documentSerial;
        std::uint64_t ordinal;
        ContinuationKind kind;
        Request input;
    };
    struct Taken final
    {
        std::optional<Entry> ready;
        std::size_t stale = 0; // owner reports that these entries belonged to a replaced document
    };

    // On refusal, the caller retains its request and must report a notice. A multi-file drop is one Request.
    [[nodiscard]] ContinuationAdmission Enqueue(std::uint64_t documentSerial, std::uint64_t ordinal,
                                                ContinuationKind kind, Request& input) noexcept
    {
        if (ordinal == 0 || ordinal <= _lastOrdinal)
        {
            return ContinuationAdmission::InvalidOrder;
        }
        if (_count != 0 && kind == ContinuationKind::WandNew)
        {
            const std::size_t tail = (_head + _count - 1) % kCapacity;
            const auto& previous = *_entries[tail];
            // A stroke or another history input accepted between the clicks prevents coalescing even though
            // strokes have their own replay storage. Add/Subtract/Intersect always retain their individual order.
            if (previous.kind == ContinuationKind::WandNew && previous.documentSerial == documentSerial &&
                ordinal - previous.ordinal == 1)
            {
                _entries[tail].emplace(Entry{documentSerial, ordinal, kind, std::move(input)});
                _lastOrdinal = ordinal;
                return ContinuationAdmission::Coalesced;
            }
        }
        if (_count == kCapacity)
        {
            return ContinuationAdmission::Full;
        }
        const std::size_t tail = (_head + _count) % kCapacity;
        _entries[tail].emplace(Entry{documentSerial, ordinal, kind, std::move(input)});
        ++_count;
        _lastOrdinal = ordinal;
        return ContinuationAdmission::Queued;
    }

    // Peek metadata to merge commands with earlier recorded strokes. The reference expires at the next queue
    // mutation; it must never be borrowed by a worker or used to take a snapshot early.
    [[nodiscard]] const Entry* Front() const noexcept
    {
        return _count == 0 ? nullptr : &*_entries[_head];
    }

    [[nodiscard]] Taken TakeReady(std::uint64_t currentDocumentSerial, bool historyBusy) noexcept
    {
        Taken result;
        if (historyBusy)
        {
            return result;
        }
        while (_count != 0)
        {
            auto entry = std::exchange(_entries[_head], std::nullopt);
            _head = (_head + 1) % kCapacity;
            --_count;
            if (entry->documentSerial != currentDocumentSerial)
            {
                ++result.stale;
                continue;
            }
            result.ready = std::move(entry);
            break;
        }
        return result;
    }

    // Escape/Cancel clears queued commands. It does not own or cancel a completed-pointer stroke.
    [[nodiscard]] std::size_t Clear() noexcept
    {
        const std::size_t removed = _count;
        for (auto& entry : _entries)
        {
            entry.reset();
        }
        _head = 0;
        _count = 0;
        return removed;
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return _count;
    }

  private:
    std::array<std::optional<Entry>, kCapacity> _entries;
    std::size_t _head = 0;
    std::size_t _count = 0;
    std::uint64_t _lastOrdinal = 0;
};
