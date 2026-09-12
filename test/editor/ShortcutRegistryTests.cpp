#include "input/ShortcutRegistry.h"

#include <SDL3/SDL_keycode.h>
#include <gtest/gtest.h>

// A binding fires on its key with exactly its chord: the modifier comparison
// is the one rule every held or pressed key in the editor shares.
namespace
{
struct Fired
{
    int Plain = 0;
    int Shifted = 0;
};
}

TEST(ShortcutRegistry, FiresOnExactChordOnly)
{
    Fired fired;
    ShortcutRegistry shortcuts;
    shortcuts.Register("a.plain", SDLK_Q, {}, [&] { ++fired.Plain; });
    shortcuts.Register("a.shifted", SDLK_Q, { .Shift = true }, [&] { ++fired.Shifted; });

    EXPECT_EQ(shortcuts.OnInput(KeyDownEvent{ .Key = SDLK_Q, .Modifiers = {} }), InputConsumed::Yes);
    EXPECT_EQ(shortcuts.OnInput(KeyDownEvent{ .Key = SDLK_Q, .Modifiers = { .Shift = true } }), InputConsumed::Yes);
    EXPECT_EQ(shortcuts.OnInput(KeyDownEvent{ .Key = SDLK_Q, .Modifiers = { .Ctrl = true } }), InputConsumed::No);
    EXPECT_EQ(shortcuts.OnInput(KeyDownEvent{ .Key = SDLK_Q, .Modifiers = { .Shift = true, .Alt = true } }), InputConsumed::No);
    EXPECT_EQ(shortcuts.OnInput(KeyDownEvent{ .Key = SDLK_W, .Modifiers = {} }), InputConsumed::No);
    EXPECT_EQ(fired.Plain, 1);
    EXPECT_EQ(fired.Shifted, 1);
}

TEST(ShortcutRegistry, IgnoresReleases)
{
    int fired = 0;
    ShortcutRegistry shortcuts;
    shortcuts.Register("a.plain", SDLK_Q, {}, [&] { ++fired; });
    EXPECT_EQ(shortcuts.OnInput(KeyUpEvent{ .Key = SDLK_Q, .Modifiers = {} }), InputConsumed::No);
    EXPECT_EQ(fired, 0);
}
