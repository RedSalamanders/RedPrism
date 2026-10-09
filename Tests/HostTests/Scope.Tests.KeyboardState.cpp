#include "Scope.Tests.KeyboardState.h"

#include "../../RedPrism/KeyboardState.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <type_traits>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

static_assert(!std::is_copy_constructible_v<KeyboardState::ScopedSynthetic>);
static_assert(!std::is_move_constructible_v<KeyboardState::ScopedSynthetic>);

int RunKeyboardStateScopeTests()
{
    unsigned int checks = 0, failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s keyboard-synthetic.%s\n", condition ? "PASS" : "FAIL", name);
    };
    const auto initial = KeyboardState::Detail::g_synthetic;
    const auto restore = wil::scope_exit([initial]() noexcept { KeyboardState::Detail::g_synthetic = initial; });
    const auto allUp = []() noexcept
    {
        std::array<BYTE, 256> keys{};
        return KeyboardState::Read(keys) &&
               std::all_of(keys.begin(), keys.end(), [](BYTE value) { return value == 0; });
    };
    const auto install = [](int key, BYTE value) noexcept
    {
        // Independent literal input; no physical keyboard writes or native window messages.
        KeyboardState::Detail::g_synthetic.keys = {};
        KeyboardState::Detail::g_synthetic.keys[static_cast<std::size_t>(key)] = value;
    };

    KeyboardState::Detail::g_synthetic = {false, {}};
    KeyboardState::Detail::g_synthetic.keys[VK_CAPITAL] = 1;
    const auto inactive = KeyboardState::Detail::g_synthetic;
    {
        const KeyboardState::ScopedSynthetic outer;
        expect(KeyboardState::Detail::g_synthetic.active && allUp(), "outer-begins-all-up-with-synthetic-input");
        install(VK_CONTROL, 0x80);
        KeyboardState::Detail::g_synthetic.keys[VK_CAPITAL] = 1;
        const auto controlAndToggle = KeyboardState::Detail::g_synthetic.keys;
        expect(KeyboardState::Held(VK_CONTROL) && !KeyboardState::Held(VK_SHIFT),
               "outer-reads-independent-held-control");
        {
            const KeyboardState::ScopedSynthetic inner;
            expect(KeyboardState::Detail::g_synthetic.active && allUp(), "nested-driver-begins-all-up");
            install(VK_MENU, 0x80);
            expect(KeyboardState::Held(VK_MENU) && !KeyboardState::Held(VK_CONTROL), "inner-keys-do-not-alias-outer");
        }
        expect(KeyboardState::Detail::g_synthetic.active, "nested-exit-keeps-outer-synthetic-mode");
        expect(KeyboardState::Detail::g_synthetic.keys == controlAndToggle && KeyboardState::Held(VK_CONTROL),
               "nested-exit-restores-held-and-toggled-outer-bytes");
        const auto earlyReturn = [&]() noexcept
        {
            const KeyboardState::ScopedSynthetic inner;
            install(VK_SHIFT, 0x80);
            return KeyboardState::Held(VK_SHIFT) && !KeyboardState::Held(VK_CONTROL);
        };
        expect(earlyReturn(), "early-return-driver-reads-own-shift");
        expect(KeyboardState::Detail::g_synthetic.active && KeyboardState::Detail::g_synthetic.keys == controlAndToggle,
               "early-return-restores-exact-outer-state");
        {
            const KeyboardState::ScopedSynthetic middle;
            install(VK_MENU, 0x80);
            const auto middleKeys = KeyboardState::Detail::g_synthetic.keys;
            {
                const KeyboardState::ScopedSynthetic inner;
                install(VK_SHIFT, 0x80);
                expect(KeyboardState::Held(VK_SHIFT) && !KeyboardState::Held(VK_MENU),
                       "third-level-holds-only-its-own-key");
            }
            expect(KeyboardState::Detail::g_synthetic.active && KeyboardState::Detail::g_synthetic.keys == middleKeys,
                   "third-level-exit-restores-middle-state");
        }
        expect(KeyboardState::Detail::g_synthetic.active && KeyboardState::Detail::g_synthetic.keys == controlAndToggle,
               "middle-exit-restores-original-outer-state");
    }
    expect(!KeyboardState::Detail::g_synthetic.active, "outer-exit-restores-physical-mode");
    expect(KeyboardState::Detail::g_synthetic.keys == inactive.keys, "inactive-snapshot-restores-every-byte");

    KeyboardState::Detail::g_synthetic = {true, {}};
    install(VK_SHIFT, 0x80);
    const auto activeKeys = KeyboardState::Detail::g_synthetic.keys;
    {
        const KeyboardState::ScopedSynthetic inner;
        expect(allUp(), "already-synthetic-entry-is-a-fresh-driver");
    }
    expect(KeyboardState::Detail::g_synthetic.active && KeyboardState::Detail::g_synthetic.keys == activeKeys &&
               KeyboardState::Held(VK_SHIFT),
           "already-synthetic-exit-restores-held-shift");
    std::printf("keyboard-synthetic summary: %u checks, %u failures\n", checks, failures);
    return static_cast<int>(failures);
}
