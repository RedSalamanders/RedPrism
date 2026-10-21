#pragma once

#include "CancellationObservation.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <type_traits>

// Standard C++ only. Kernels borrow an executor; they never own threads or Windows handles.
class CancelToken final
{
  public:
    CancelToken() noexcept = default;
    explicit CancelToken(std::stop_token local, std::stop_token shutdown = {}) noexcept
        : _local(local), _shutdown(shutdown)
    {
    }
    [[nodiscard]] bool IsCancelled() const noexcept
    {
        const bool cancelled = _local.stop_requested() || _shutdown.stop_requested();
        if (cancelled)
            CancellationObservation::ReportCancelled();
        return cancelled;
    }
    // Worker-side IO registers both stop states after issuing overlapped work. The registration and IO owners
    // must outlive acknowledgement; a requested stop alone never permits destroying pending buffers/handles.
    [[nodiscard]] std::stop_token LocalToken() const noexcept
    {
        return _local;
    }
    [[nodiscard]] std::stop_token ShutdownToken() const noexcept
    {
        return _shutdown;
    }
    [[nodiscard]] CancelToken WithShutdown(std::stop_token shutdown) const noexcept
    {
        return shutdown.stop_possible() ? CancelToken{_local, shutdown} : *this;
    }

  private:
    std::stop_token _local;
    std::stop_token _shutdown;
};

class CancelSource final
{
  public:
    [[nodiscard]] CancelToken Token() const noexcept
    {
        return CancelToken{_source.get_token()};
    }
    bool Cancel() noexcept
    {
        return _source.request_stop();
    }

  private:
    std::stop_source _source;
};

struct Tile final
{
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct TileGrid final
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t tileWidth = 256;
    std::uint32_t tileHeight = 256;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return tileWidth != 0 && tileHeight != 0;
    }
    [[nodiscard]] std::uint64_t Columns() const noexcept
    {
        return tileWidth == 0 ? 0 : (static_cast<std::uint64_t>(width) + tileWidth - 1) / tileWidth;
    }
    [[nodiscard]] std::uint64_t Count() const noexcept
    {
        return tileHeight == 0 ? 0 : Columns() * ((static_cast<std::uint64_t>(height) + tileHeight - 1) / tileHeight);
    }
    [[nodiscard]] Tile At(std::uint64_t index) const noexcept
    {
        const std::uint64_t columns = Columns();
        if (columns == 0 || index >= Count())
        {
            return {};
        }
        const auto x = static_cast<std::uint32_t>((index % columns) * tileWidth);
        const auto y = static_cast<std::uint32_t>((index / columns) * tileHeight);
        return {x, y, (std::min)(tileWidth, width - x), (std::min)(tileHeight, height - y)};
    }
};

class Executor
{
  public:
    using Participant = void (*)(void*) noexcept;
    virtual ~Executor() = default;
    [[nodiscard]] virtual unsigned int Concurrency() const noexcept = 0;
    [[nodiscard]] virtual std::stop_token ShutdownToken() const noexcept = 0;

    // Synchronous join for worker callers. The caller participates; a pool must cancel helpers that have not
    // started before joining, and must retain context until every running participant returns. Never for the UI.
    virtual void RunParticipants(void* context, Participant run, unsigned int participants) noexcept = 0;
};

class InlineExecutor final : public Executor
{
  public:
    [[nodiscard]] unsigned int Concurrency() const noexcept override
    {
        return 1;
    }
    [[nodiscard]] std::stop_token ShutdownToken() const noexcept override
    {
        return {};
    }
    void RunParticipants(void* context, Participant run, unsigned int) noexcept override
    {
        run(context);
    }
};

// Each tile owns its output. The body returns false for an incomplete tile, checks cancellation per row (or
// finer for an expensive row), and neither allocates nor blocks. Outputs/scratch are allocated before this call.
// Exact kernels retain their scalar operation order and read halos from immutable input.
template <typename Fn>
[[nodiscard]] bool ParallelFor(Executor& executor, TileGrid grid, CancelToken token, Fn&& body) noexcept
{
    static_assert(std::is_nothrow_invocable_r_v<bool, Fn&, Tile, CancelToken>);
    if (!grid.IsValid())
    {
        return false;
    }
    token = token.WithShutdown(executor.ShutdownToken());
    const std::uint64_t count = grid.Count();
    if (count == 0 || token.IsCancelled())
    {
        return !token.IsCancelled();
    }
    struct State final
    {
        TileGrid grid;
        CancelToken token;
        std::remove_reference_t<Fn>& body;
        const std::uint64_t count;
        std::atomic<std::uint64_t> next{0};
        std::atomic<std::uint64_t> completed{0};
        std::atomic<bool> aborted{false};
    } state{grid, token, body, count};
    const auto run = [](void* context) noexcept
    {
        auto& work = *static_cast<State*>(context);
        std::uint64_t index = work.next.load(std::memory_order_relaxed);
        while (!work.token.IsCancelled() && !work.aborted.load(std::memory_order_relaxed))
        {
            // A bounded claim avoids counter wrap even for a maximum-sized abstract grid.
            if (index >= work.count)
            {
                return;
            }
            if (!work.next.compare_exchange_weak(index, index + 1, std::memory_order_relaxed))
            {
                continue;
            }
            if (!work.body(work.grid.At(index), work.token))
            {
                work.aborted.store(true, std::memory_order_relaxed);
                return;
            }
            work.completed.fetch_add(1, std::memory_order_relaxed);
            index = work.next.load(std::memory_order_relaxed);
        }
    };
    executor.RunParticipants(
        &state, run, static_cast<unsigned int>((std::min)(count, static_cast<std::uint64_t>(executor.Concurrency()))));
    return state.completed.load(std::memory_order_relaxed) == count && !token.IsCancelled();
}
