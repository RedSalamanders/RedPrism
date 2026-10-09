#pragma once

#include <windows.h>

#include <array>
#include <cstddef>

// The keyboard the input code reads (RF-1110). An ordinary run reads the thread's key state, the state as of the
// message being handled. The self-test switches to a keyboard of its own that its drivers write (HandleAccelerator,
// the steps that hold Alt): Windows writes each physical key transition into every thread's key state at once, the
// hidden self-test window's thread included and whatever SetKeyboardState put there, so a Shift or Ctrl the user
// presses in another application while the self-test runs would otherwise reach its synthetic input and fail a step
// at random. UI thread only.
namespace KeyboardState
{
namespace Detail
{
struct Synthetic
{
    bool active = false;
    std::array<BYTE, 256> keys{};
};

inline Synthetic g_synthetic;
} // namespace Detail

// Turns the self-test's keyboard on (every key up) or off (the thread's key state again).
inline void UseSynthetic(bool active) noexcept
{
    Detail::g_synthetic.active = active;
    Detail::g_synthetic.keys = {};
}

// A nested self-test owns its temporary keyboard, not the surrounding driver's state. Restore both the
// previous mode and every held/toggled key, including when the inner test returns early. UI thread only;
// this scope owns no Windows keyboard state and never changes an ordinary application's input policy.
class ScopedSynthetic final
{
  public:
    ScopedSynthetic() noexcept : _previous(Detail::g_synthetic)
    {
        UseSynthetic(true);
    }
    ~ScopedSynthetic()
    {
        Detail::g_synthetic = _previous;
    }
    ScopedSynthetic(const ScopedSynthetic&) = delete;
    ScopedSynthetic& operator=(const ScopedSynthetic&) = delete;
    ScopedSynthetic(ScopedSynthetic&&) = delete;
    ScopedSynthetic& operator=(ScopedSynthetic&&) = delete;

  private:
    const Detail::Synthetic _previous;
};

// GetKeyState(key) < 0, or the self-test's keyboard while it is on.
[[nodiscard]] inline bool Held(int virtualKey) noexcept
{
    if (Detail::g_synthetic.active)
    {
        return (Detail::g_synthetic.keys[static_cast<std::size_t>(virtualKey) & 0xFFu] & 0x80u) != 0;
    }
    return GetKeyState(virtualKey) < 0;
}

// GetKeyboardState, or the self-test's keyboard while it is on.
[[nodiscard]] inline bool Read(std::array<BYTE, 256>& keys) noexcept
{
    if (Detail::g_synthetic.active)
    {
        keys = Detail::g_synthetic.keys;
        return true;
    }
    return GetKeyboardState(keys.data()) != FALSE;
}

// SetKeyboardState, and the self-test's keyboard while it is on (Windows still gets the state for what it reads
// itself, such as DefWindowProc's Alt handling).
inline void Write(const std::array<BYTE, 256>& keys) noexcept
{
    if (Detail::g_synthetic.active)
    {
        Detail::g_synthetic.keys = keys;
    }
    std::array<BYTE, 256> copy = keys;
    (void)SetKeyboardState(copy.data());
}
} // namespace KeyboardState
