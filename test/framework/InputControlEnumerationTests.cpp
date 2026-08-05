#include <gtest/gtest.h>

#include <input/InputControl.h>
#include <input/SdlInputCapture.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

// A picker offering a name the parser rejects is worse than no picker: the
// author binds something, sees it accepted, and the control never fires. These
// assert the properties that prevent it rather than a fixed list, because
// platform scancode names are not portable.

namespace
{
bool Contains(std::span<const NamedInputControl> controls, std::string_view name)
{
    return std::any_of(controls.begin(), controls.end(),
                       [name](const NamedInputControl& entry) { return entry.Name == name; });
}
}

TEST(InputControlEnumeration, EveryOfferedNameParsesBackToItsControl)
{
    const std::span<const NamedInputControl> controls = EnumerateInputControls();
    ASSERT_FALSE(controls.empty());

    for (const NamedInputControl& entry : controls)
    {
        const std::optional<InputControl> parsed = ParseInputControl(entry.Name);
        ASSERT_TRUE(parsed.has_value()) << entry.Name << " does not parse";
        EXPECT_EQ(*parsed, entry.Control) << entry.Name << " parses to a different control";
    }
}

TEST(InputControlEnumeration, NamesAreUnique)
{
    // Distinct scancodes can share a platform name; offering both would give
    // the author two identical rows, one of which binds a key they did not pick.
    std::unordered_set<std::string> seen;
    for (const NamedInputControl& entry : EnumerateInputControls())
        EXPECT_TRUE(seen.insert(entry.Name).second) << "duplicate name " << entry.Name;
}

TEST(InputControlEnumeration, OffersTheControlsEveryDeviceHas)
{
    const std::span<const NamedInputControl> controls = EnumerateInputControls();

    EXPECT_TRUE(Contains(controls, "key.w"));
    EXPECT_TRUE(Contains(controls, "key.space"));
    EXPECT_TRUE(Contains(controls, "mouse.left"));
    EXPECT_TRUE(Contains(controls, "mouse.delta"));
    EXPECT_TRUE(Contains(controls, "gamepad.south"));
    EXPECT_TRUE(Contains(controls, "gamepad.left_stick"));
    EXPECT_TRUE(Contains(controls, "gamepad.right_trigger"));
}

TEST(InputControlEnumeration, GroupsByDeviceInOrder)
{
    // A picker sections the list by device by walking it once.
    const std::span<const NamedInputControl> controls = EnumerateInputControls();
    bool sawMouse = false;
    bool sawGamepad = false;
    for (const NamedInputControl& entry : controls)
    {
        switch (entry.Control.Source)
        {
        case InputControlSource::Key:
            EXPECT_FALSE(sawMouse || sawGamepad) << "keys must come first";
            break;
        case InputControlSource::MouseButton:
        case InputControlSource::MouseMotion:
        case InputControlSource::MouseWheel:
            EXPECT_FALSE(sawGamepad) << "mouse must precede gamepad";
            sawMouse = true;
            break;
        default:
            sawGamepad = true;
            break;
        }
    }
    EXPECT_TRUE(sawMouse);
    EXPECT_TRUE(sawGamepad);
}

// ---------------------------------------------------------------------------
// Press-to-bind event mapping
// ---------------------------------------------------------------------------

TEST(InputControlFromEvent, NamesTheKeyThatWasPressed)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_J;
    event.key.repeat = false;

    const std::optional<InputControl> control = InputControlFromSdlEvent(event, 0.5f);
    ASSERT_TRUE(control.has_value());
    EXPECT_EQ(FormatInputControl(*control), "key.j");
}

TEST(InputControlFromEvent, IgnoresKeyRepeat)
{
    // Holding a key while the field is listening should bind once, not rebind
    // on every repeat the platform sends.
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_J;
    event.key.repeat = true;

    EXPECT_FALSE(InputControlFromSdlEvent(event, 0.5f).has_value());
}

TEST(InputControlFromEvent, NamesMouseButtonsAndTheWheel)
{
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.button = SDL_BUTTON_RIGHT;
    EXPECT_EQ(FormatInputControl(*InputControlFromSdlEvent(button, 0.5f)), "mouse.right");

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1.0f;
    EXPECT_EQ(FormatInputControl(*InputControlFromSdlEvent(wheel, 0.5f)), "mouse.wheel");
}

TEST(InputControlFromEvent, PointerMotionNamesNothing)
{
    // The pointer moves while the player reaches for the control they meant, so
    // capturing it would claim every listen before they got there.
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel = 40.0f;

    EXPECT_FALSE(InputControlFromSdlEvent(event, 0.5f).has_value());
}

TEST(InputControlFromEvent, NamesAGamepadButton)
{
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;

    EXPECT_EQ(FormatInputControl(*InputControlFromSdlEvent(event, 0.5f)), "gamepad.south");
}

TEST(InputControlFromEvent, AStickNamesItselfOnlyPastTheThreshold)
{
    SDL_Event drift{};
    drift.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    drift.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTY;
    drift.gaxis.value = static_cast<Sint16>(0.1f * 32767.0f);
    EXPECT_FALSE(InputControlFromSdlEvent(drift, 0.5f).has_value());

    // Either axis of a stick names the stick: pushing left means "the left
    // stick", and half a stick is not something a binding can name.
    SDL_Event pushed = drift;
    pushed.gaxis.value = static_cast<Sint16>(-0.9f * 32767.0f);
    EXPECT_EQ(FormatInputControl(*InputControlFromSdlEvent(pushed, 0.5f)), "gamepad.left_stick");

    SDL_Event sideways = drift;
    sideways.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    sideways.gaxis.value = static_cast<Sint16>(0.9f * 32767.0f);
    EXPECT_EQ(FormatInputControl(*InputControlFromSdlEvent(sideways, 0.5f)), "gamepad.left_stick");
}

TEST(InputControlFromEvent, NamesATriggerPastTheThreshold)
{
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    event.gaxis.value = static_cast<Sint16>(0.8f * 32767.0f);

    EXPECT_EQ(FormatInputControl(*InputControlFromSdlEvent(event, 0.5f)), "gamepad.right_trigger");
}

TEST(InputControlFromEvent, UnrelatedEventsNameNothing)
{
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    EXPECT_FALSE(InputControlFromSdlEvent(quit, 0.5f).has_value());

    SDL_Event keyUp{};
    keyUp.type = SDL_EVENT_KEY_UP;
    keyUp.key.scancode = SDL_SCANCODE_J;
    EXPECT_FALSE(InputControlFromSdlEvent(keyUp, 0.5f).has_value());
}
