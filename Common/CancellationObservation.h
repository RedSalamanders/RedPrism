#pragma once

// Borrowed diagnostics for one joined useful Run. Bindings are never stored in a retained CancelToken.
// They may be copied only into a synchronously joined participant scope. No callback may wait, mutate a
// document, schedule work or acknowledge retirement. The owner context survives every participant's return.
namespace CancellationObservation
{
struct Binding final
{
    void (*notify)(void*) noexcept = nullptr;
    void* context = nullptr;
};

inline thread_local constinit Binding gCurrent{};

[[nodiscard]] inline Binding Current() noexcept
{
    return gCurrent;
}

class Scope final
{
  public:
    explicit Scope(Binding binding) noexcept : _previous(gCurrent)
    {
        gCurrent = binding;
    }
    ~Scope()
    {
        gCurrent = _previous;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    const Binding _previous;
};

// Called only after a true stop observation. Uncancelled pixel checks do not read TLS or diagnostics.
inline void ReportCancelled() noexcept
{
    const auto binding = Current();
    if (binding.notify)
        binding.notify(binding.context);
}
} // namespace CancellationObservation
