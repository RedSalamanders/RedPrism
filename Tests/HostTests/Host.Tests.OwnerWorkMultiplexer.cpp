#include "Host.Tests.OwnerWorkMultiplexer.h"

#include "OwnerWorkMultiplexer.h"

#include <array>
#include <cstddef>
#include <cstdio>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++g_failures;
}

class MockSource final : public OwnerWorkPump
{
  public:
    [[nodiscard]] bool HasPending() const noexcept override
    {
        ++pendingQueries;
        return pending;
    }

    [[nodiscard]] bool HasReady() const noexcept override
    {
        ++readyQueries;
        return ready;
    }

    void Pump() noexcept override
    {
        ++pumpCalls;
        if (callOrder && callCount && *callCount < callOrder->size())
            (*callOrder)[(*callCount)++] = index;
        if (onPump)
            onPump(*this);
    }

    bool pending = false;
    bool ready = false;
    int index = -1;
    int pumpCalls = 0;
    mutable int pendingQueries = 0;
    mutable int readyQueries = 0;
    std::array<int, 32>* callOrder = nullptr;
    std::size_t* callCount = nullptr;
    void (*onPump)(MockSource&) noexcept = nullptr;
    OwnerWorkMultiplexer* context = nullptr;
    OwnerWorkPump* extra = nullptr;
    bool recursiveWasBlocked = false;
    bool attachWasBlocked = false;
    bool detachWasBlocked = false;
};

void ReenterAndMutate(MockSource& source) noexcept
{
    auto* mux = static_cast<OwnerWorkMultiplexer*>(source.context);
    if (!mux)
        return;
    mux->Pump();
    source.recursiveWasBlocked = source.pumpCalls == 1;
    source.attachWasBlocked = source.extra && !mux->Attach(*source.extra);
    source.detachWasBlocked = !mux->Detach(source);
}
} // namespace

int RunOwnerWorkMultiplexerTests()
{
    g_failures = 0;
    OwnerWorkMultiplexer mux;
    Expect(!mux.HasPending() && !mux.HasReady(), "owner-work.empty-inactive");
    mux.Pump();

    std::array<MockSource, 9> sources{};
    for (std::size_t i = 0; i < 8; ++i)
    {
        sources[i].index = static_cast<int>(i);
        Expect(mux.Attach(sources[i]), "owner-work.attach-within-capacity");
    }
    Expect(!mux.Attach(sources[0]), "owner-work.duplicate-rejected");
    Expect(!mux.Attach(mux), "owner-work.self-attach-rejected");
    Expect(!mux.Attach(sources[8]), "owner-work.capacity-rejected");
    MockSource foreign;
    Expect(!mux.Detach(foreign), "owner-work.foreign-detach-rejected");

    // Waiting work keeps the aggregate pending while producing no owner-ready pass or leaf pump.
    sources[0].pending = true;
    int readyBefore = sources[0].readyQueries;
    int pumpsBefore = sources[0].pumpCalls;
    Expect(mux.HasPending(), "owner-work.pending-visible");
    Expect(!mux.HasReady(), "owner-work.pending-is-not-ready");
    Expect(sources[0].readyQueries == readyBefore + 1, "owner-work.pending-readiness-queried");
    readyBefore = sources[0].readyQueries;
    mux.Pump();
    Expect(sources[0].pumpCalls == pumpsBefore, "owner-work.pending-leaf-not-pumped");
    Expect(sources[0].readyQueries == readyBefore + 1, "owner-work.pump-probes-readiness-only");
    Expect(!mux.Detach(sources[0]), "owner-work.pending-detach-refused");
    sources[0].pending = false;

    // Every leaf stays ready. Literal expected slot order proves one leaf per pass and round-robin fairness.
    std::array<int, 32> order{};
    std::size_t callCount = 0;
    for (std::size_t i = 0; i < 3; ++i)
    {
        sources[i].ready = true;
        sources[i].callOrder = &order;
        sources[i].callCount = &callCount;
    }
    for (int pass = 0; pass < 9; ++pass)
        mux.Pump();
    constexpr std::array<int, 9> expectedOrder{0, 1, 2, 0, 1, 2, 0, 1, 2};
    bool orderMatches = true;
    for (std::size_t i = 0; i < expectedOrder.size(); ++i)
        orderMatches = orderMatches && order[i] == expectedOrder[i];
    Expect(orderMatches, "owner-work.one-ready-leaf-round-robin-order");
    Expect(sources[0].pumpCalls == 3 && sources[1].pumpCalls == 3 && sources[2].pumpCalls == 3,
           "owner-work.one-leaf-per-pass");
    Expect(!mux.Detach(sources[1]), "owner-work.ready-detach-refused");

    // Quiesce, detach an idle joined source, then reuse its slot. The detached source must never be revisited.
    for (std::size_t i = 0; i < 3; ++i)
        sources[i].ready = false;
    int oldQueries = sources[1].readyQueries;
    Expect(mux.Detach(sources[1]), "owner-work.joined-idle-detach");
    oldQueries = sources[1].readyQueries;
    MockSource replacement;
    replacement.index = 7;
    replacement.ready = true;
    replacement.callOrder = &order;
    replacement.callCount = &callCount;
    Expect(mux.Attach(replacement), "owner-work.reused-slot-attach");
    mux.Pump();
    Expect(sources[1].readyQueries == oldQueries, "owner-work.detached-source-not-visited");
    Expect(order[9] == 7 && replacement.pumpCalls == 1, "owner-work.reused-slot-routed");

    // Exercise recursion and attach/detach attempted from inside a leaf callback.
    replacement.ready = false;
    Expect(mux.Detach(sources[3]), "owner-work.idle-capacity-freed-before-callback-leaf");
    MockSource callbackLeaf;
    MockSource callbackExtra;
    callbackLeaf.index = 6;
    callbackLeaf.ready = true;
    callbackLeaf.context = &mux;
    callbackLeaf.extra = &callbackExtra;
    callbackLeaf.onPump = &ReenterAndMutate;
    Expect(mux.Attach(callbackLeaf), "owner-work.callback-leaf-attached");
    mux.Pump();
    Expect(callbackLeaf.pumpCalls == 1 && callbackLeaf.recursiveWasBlocked, "owner-work.recursive-pump-blocked");
    Expect(callbackLeaf.attachWasBlocked, "owner-work.attach-during-leaf-blocked");
    Expect(callbackLeaf.detachWasBlocked, "owner-work.detach-during-leaf-blocked");

    return g_failures == 0 ? 0 : 1;
}
