#pragma once

#include <cstddef>
#include <cstdint>

// Intrusive ownership transfer. The Windows implementation overlays its 16-byte SLIST_ENTRY on link; keeping
// that reserved storage here leaves document and kernel headers independent of Windows typedefs. The payload
// owns this node until destroy is called exactly once by the consumer. Publishing never allocates.
struct alignas(16) DeferredReleaseNode
{
    void* link[2]{};
    std::uint64_t bytes = 0;
    void (*destroy)(DeferredReleaseNode*) noexcept = nullptr;
};
static_assert(offsetof(DeferredReleaseNode, link) == 0);

inline constexpr std::uint64_t kDeferredReleaseThreshold = 16ull * 1024 * 1024;

namespace DeferredReleaseDetail
{
struct Sink final
{
    void (*enqueue)(void*, DeferredReleaseNode&) noexcept = nullptr;
    void* context = nullptr;
};
// Only the owner/UI thread is eligible to defer. Other threads have an empty slot and destroy inline. A TLS
// pair removes the load-function/load-context teardown race of a process-global callback without adding a lock,
// hazard pointer, heap allocation or reference count on every raster release.
inline thread_local Sink sink;
} // namespace DeferredReleaseDetail

// Install/clear on the same owning thread. Exactly one sink per thread; its context outlives Clear followed by
// joining/draining the consumer. Shutdown clears after owner cleanup and before final worker retirement/pool
// teardown, so no retired node outlives its consumer and subsequent owner-thread frees run inline.
[[nodiscard]] inline bool InstallDeferredReleaseSink(void (*enqueue)(void*, DeferredReleaseNode&) noexcept,
                                                     void* context) noexcept
{
    auto& sink = DeferredReleaseDetail::sink;
    if (!enqueue || !context || sink.enqueue)
    {
        return false;
    }
    sink = {enqueue, context};
    return true;
}

[[nodiscard]] inline bool ClearDeferredReleaseSink(void* context) noexcept
{
    auto& sink = DeferredReleaseDetail::sink;
    if (!sink.enqueue || sink.context != context)
    {
        return false;
    }
    sink = {};
    return true;
}

enum class DeferredReleasePolicy : std::uint8_t
{
    SizeThreshold,
    AlwaysOnOwner,
};

// Complete metadata graphs can have expensive destruction below the pixel-size threshold. Their producers
// choose AlwaysOnOwner while still declaring their actual conservative ownership bound in node.bytes.
[[nodiscard]] inline bool TryDeferRelease(DeferredReleaseNode& node,
                                          DeferredReleasePolicy policy = DeferredReleasePolicy::SizeThreshold) noexcept
{
    const auto sink = DeferredReleaseDetail::sink;
    if (!sink.enqueue || !node.destroy ||
        (policy == DeferredReleasePolicy::SizeThreshold && node.bytes < kDeferredReleaseThreshold))
    {
        return false;
    }
    sink.enqueue(sink.context, node);
    return true;
}
