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

enum class ContinuationRetirement
{
    Superseded,
    Stale,
    Cancelled
};

// UI-owned event parameters, never a document snapshot. The owner takes immutable pixels/selection and the
// result token only when TakeReady returns the command, after preceding history work has applied or dropped.
// Ordinals come from one host sequence shared with recorded strokes, preserving their relative input order.
template <typename Request> class ContinuationQueue final
{
    static_assert(std::is_nothrow_move_constructible_v<Request> && std::is_nothrow_move_assignable_v<Request> &&
                  std::is_nothrow_destructible_v<Request>);

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
        return Enqueue(documentSerial, ordinal, kind, input, Discard);
    }

    // Retirement callbacks receive each detached entry before its owned input is released. They may retain it
    // by moving, but must not access this queue, dispatch UI, pump or wait. No callback is stored or allocated.
    // Receipt-owning callers use these overloads for coalescing, stale drops and explicit teardown; the count-only
    // forms are for requests without an externally observed result. A callback never runs on refused admission.
    template <typename Retire>
    [[nodiscard]] ContinuationAdmission Enqueue(std::uint64_t documentSerial, std::uint64_t ordinal,
                                                ContinuationKind kind, Request& input, Retire&& retire) noexcept
    {
        static_assert(std::is_nothrow_invocable_v<Retire&, Entry&&, ContinuationRetirement>);
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
                Entry superseded = std::move(_entries[tail].value());
                _entries[tail].emplace(Entry{documentSerial, ordinal, kind, std::move(input)});
                _lastOrdinal = ordinal;
                retire(std::move(superseded), ContinuationRetirement::Superseded);
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

    // Preview callers opt into adjacent coalescing explicitly. The newest request replaces only the pending
    // tail entry when its kind/document/ordinal directly follow that entry and Equivalent accepts their inputs.
    // The caller chooses which kinds are safe to coalesce; refusal never invokes either callback or moves input.
    // Neither callback may mutate/reenter this queue. Equivalent borrows its arguments only during this call.
    template <typename Retire, typename Equivalent>
    [[nodiscard]] ContinuationAdmission EnqueueAdjacent(std::uint64_t documentSerial, std::uint64_t ordinal,
                                                        ContinuationKind kind, Request& input, Retire&& retire,
                                                        Equivalent&& equivalent) noexcept
    {
        static_assert(std::is_nothrow_invocable_v<Retire&, Entry&&, ContinuationRetirement>);
        static_assert(std::is_nothrow_invocable_r_v<bool, Equivalent&, const Request&, const Request&>);
        if (ordinal == 0 || ordinal <= _lastOrdinal)
        {
            return ContinuationAdmission::InvalidOrder;
        }

        if (_count != 0)
        {
            const std::size_t tail = (_head + _count - 1) % kCapacity;
            const Entry& previous = _entries[tail].value();
            if (previous.kind == kind && previous.documentSerial == documentSerial && ordinal - previous.ordinal == 1 &&
                equivalent(previous.input, input))
            {
                Entry superseded = std::move(_entries[tail].value());
                _entries[tail].emplace(Entry{documentSerial, ordinal, kind, std::move(input)});
                _lastOrdinal = ordinal;
                retire(std::move(superseded), ContinuationRetirement::Superseded);
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

    // Bounded owner inspection only. The reference expires on the next queue mutation; workers never borrow it.
    [[nodiscard]] const Entry* PendingAt(std::size_t offset) const noexcept
    {
        return offset < _count ? &_entries[(_head + offset) % kCapacity].value() : nullptr;
    }

    // Detach only this pending input for independent nonmutating prompt/cancellation handling. A taken/active
    // entry is never in this queue. The caller owns its receipt and retirement after this move; no callback runs.
    // At most eight fixed slots move. Surviving input order and the admission ordinal watermark are unchanged,
    // so removing an admitted barrier cannot make later previews/clicks adjacent across its former ordinal.
    // All borrowed pending references expire, including references to surviving entries. Never retain one across
    // a prompt: retain the scalar document/ordinal key and reacquire only when the owner resumes.
    [[nodiscard]] std::optional<Entry> DetachPending(std::uint64_t documentSerial, std::uint64_t ordinal) noexcept
    {
        if (ordinal == 0)
            return std::nullopt;
        for (std::size_t offset = 0; offset < _count; ++offset)
        {
            const std::size_t slot = (_head + offset) % kCapacity;
            if (_entries[slot]->documentSerial != documentSerial || _entries[slot]->ordinal != ordinal)
                continue;
            auto detached = std::move(_entries[slot]);
            _entries[slot].reset();
            for (std::size_t next = offset + 1; next < _count; ++next)
            {
                const std::size_t from = (_head + next) % kCapacity;
                const std::size_t to = (_head + next - 1) % kCapacity;
                _entries[to] = std::move(_entries[from]);
                _entries[from].reset();
            }
            --_count;
            if (_count == 0)
                _head = 0;
            return detached;
        }
        return std::nullopt;
    }

    // Peek metadata to merge commands with earlier recorded strokes. The reference expires at the next queue
    // mutation; it must never be borrowed by a worker or used to take a snapshot early.
    [[nodiscard]] const Entry* Front() const noexcept
    {
        return _count == 0 ? nullptr : &*_entries[_head];
    }

    [[nodiscard]] Taken TakeReady(std::uint64_t currentDocumentSerial, bool historyBusy) noexcept
    {
        return TakeReady(currentDocumentSerial, historyBusy, Discard);
    }

    template <typename Retire>
    [[nodiscard]] Taken TakeReady(std::uint64_t currentDocumentSerial, bool historyBusy, Retire&& retire) noexcept
    {
        static_assert(std::is_nothrow_invocable_v<Retire&, Entry&&, ContinuationRetirement>);
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
                retire(std::move(entry.value()), ContinuationRetirement::Stale);
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
        return Clear(Discard);
    }

    // Explicitly retire queued requests before destroying a receipt-owning queue. Taken/active entries remain
    // the owner's responsibility. The destructor does not retain or invoke an owner callback.
    template <typename Retire> [[nodiscard]] std::size_t Clear(Retire&& retire) noexcept
    {
        static_assert(std::is_nothrow_invocable_v<Retire&, Entry&&, ContinuationRetirement>);
        const std::size_t removed = _count;
        while (_count != 0)
        {
            auto entry = std::exchange(_entries[_head], std::nullopt);
            _head = (_head + 1) % kCapacity;
            --_count;
            retire(std::move(entry.value()), ContinuationRetirement::Cancelled);
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
    static void Discard(Entry&&, ContinuationRetirement) noexcept {}

    std::array<std::optional<Entry>, kCapacity> _entries;
    std::size_t _head = 0;
    std::size_t _count = 0;
    std::uint64_t _lastOrdinal = 0;
};
