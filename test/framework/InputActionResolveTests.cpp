#include <gtest/gtest.h>
#include <input/InputActionRegistry.h>
#include <input/InputActionResolve.h>
#include <input/InputControl.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <vector>

// The resolve rules, exercised without a world, a schedule, or a device.
// Everything here is a scenario: some device state, a compiled profile, an
// active-context set.

namespace
{
constexpr std::uint16_t kKeyW = SDL_SCANCODE_W;
constexpr std::uint16_t kKeyA = SDL_SCANCODE_A;
constexpr std::uint16_t kKeyS = SDL_SCANCODE_S;
constexpr std::uint16_t kKeyD = SDL_SCANCODE_D;
constexpr std::uint16_t kKeySpace = SDL_SCANCODE_SPACE;
constexpr std::uint16_t kKeyEscape = SDL_SCANCODE_ESCAPE;

InputControl Key(std::uint16_t scancode)
{
    return InputControl{ InputControlSource::Key, scancode };
}

InputBinding DirectBinding(std::uint32_t action, InputControl control)
{
    InputBinding binding;
    binding.ActionIndex = action;
    binding.Kind = InputBindingKind::Direct;
    binding.Controls[kBindingNegativeX] = control;
    return binding;
}

InputBinding CardinalBinding(std::uint32_t action)
{
    InputBinding binding;
    binding.ActionIndex = action;
    binding.Kind = InputBindingKind::Cardinal;
    binding.Controls[kBindingNegativeX] = Key(kKeyA);
    binding.Controls[kBindingPositiveX] = Key(kKeyD);
    binding.Controls[kBindingNegativeY] = Key(kKeyS);
    binding.Controls[kBindingPositiveY] = Key(kKeyW);
    return binding;
}

// A profile with one context holding the given bindings.
BoundInputProfile SingleContext(std::vector<InputActionType> types,
                                std::vector<InputBinding> bindings)
{
    BoundInputProfile profile;
    profile.ActionTypes = std::move(types);
    profile.Bindings = std::move(bindings);
    profile.Contexts.push_back(InputContextDefinition{
        "gameplay", 0, 0, static_cast<std::uint32_t>(profile.Bindings.size()) });
    return profile;
}

// Drives one resolve pass end to end, the way the frame loop does: fold the
// frame into the latches, then resolve the clock under test.
struct Harness
{
    BoundInputProfile Profile;
    InputDeviceSnapshot Devices;
    InputClockState Presentation;
    InputClockState Simulation;
    InputFrame Frame;
    std::vector<std::uint8_t> Active;
    std::vector<InputActionValue> Values;

    explicit Harness(BoundInputProfile profile)
        : Profile(std::move(profile))
    {
        Active.assign(Profile.Contexts.size(), 1);
        Values.assign(Profile.ActionCount(), InputActionValue{});
    }

    void PressKey(std::uint16_t scancode)
    {
        Frame.SetKeyHeld(scancode, true);
        Frame.KeysPressed.push_back(scancode);
    }

    void ReleaseKey(std::uint16_t scancode)
    {
        Frame.SetKeyHeld(scancode, false);
        Frame.KeysReleased.push_back(scancode);
    }

    void BeginFrame()
    {
        Frame.MouseDeltaX = 0.0f;
        Frame.MouseDeltaY = 0.0f;
        Frame.MouseWheelY = 0.0f;
    }

    // One rendered frame's fold into both clocks.
    void Accumulate() { AccumulateInputFrame(Frame, Devices, Presentation, Simulation); }

    const InputActionValue& ResolveTick(std::uint32_t action = 0)
    {
        ResolveInputActions(Profile, Active, Devices, Simulation, Values);
        return Values[action];
    }

    const InputActionValue& ResolveFrameClock(std::uint32_t action = 0)
    {
        ResolveInputActions(Profile, Active, Devices, Presentation, Values);
        return Values[action];
    }
};
}

// ---------------------------------------------------------------------------
// Digital press, hold, release
// ---------------------------------------------------------------------------

TEST(InputResolve, DigitalPressHoldRelease)
{
    Harness harness(SingleContext({ InputActionType::Digital },
                                  { DirectBinding(0, Key(kKeySpace)) }));

    harness.PressKey(kKeySpace);
    harness.Accumulate();
    {
        const InputActionValue value = harness.ResolveTick();
        EXPECT_TRUE(value.WasPressed());
        EXPECT_TRUE(value.IsHeld());
        EXPECT_FALSE(value.WasReleased());
        EXPECT_FLOAT_EQ(value.X, 1.0f);
    }

    // A second tick with no new device transition is a hold, not a press.
    harness.BeginFrame();
    harness.Accumulate();
    {
        const InputActionValue value = harness.ResolveTick();
        EXPECT_FALSE(value.WasPressed());
        EXPECT_TRUE(value.IsHeld());
    }

    harness.BeginFrame();
    harness.ReleaseKey(kKeySpace);
    harness.Accumulate();
    {
        const InputActionValue value = harness.ResolveTick();
        EXPECT_TRUE(value.WasReleased());
        EXPECT_FALSE(value.IsHeld());
        EXPECT_FLOAT_EQ(value.X, 0.0f);
    }
}

TEST(InputResolve, TapInsideOneFrameKeepsBothEdges)
{
    Harness harness(SingleContext({ InputActionType::Digital },
                                  { DirectBinding(0, Key(kKeySpace)) }));

    // Pressed and released before any tick ran: held state never shows it, so
    // only the latched edges carry the impulse.
    harness.PressKey(kKeySpace);
    harness.ReleaseKey(kKeySpace);
    harness.Accumulate();

    const InputActionValue value = harness.ResolveTick();
    EXPECT_TRUE(value.WasPressed());
    EXPECT_TRUE(value.WasReleased());
    EXPECT_FALSE(value.IsHeld());
}

// ---------------------------------------------------------------------------
// Fixed-step behaviour
// ---------------------------------------------------------------------------

TEST(InputResolve, EdgeSurvivesFramesThatRunNoTick)
{
    Harness harness(SingleContext({ InputActionType::Digital },
                                  { DirectBinding(0, Key(kKeySpace)) }));

    harness.PressKey(kKeySpace);
    harness.Accumulate();          // frame 1: no tick runs

    harness.BeginFrame();
    harness.Accumulate();          // frame 2: still no tick

    harness.BeginFrame();
    harness.Accumulate();          // frame 3: a tick finally runs

    const InputActionValue value = harness.ResolveTick();
    EXPECT_TRUE(value.WasPressed());
    EXPECT_TRUE(value.IsHeld());
}

TEST(InputResolve, CatchUpTicksDoNotRepeatThePressEdge)
{
    Harness harness(SingleContext({ InputActionType::Digital },
                                  { DirectBinding(0, Key(kKeySpace)) }));

    harness.PressKey(kKeySpace);
    harness.Accumulate();

    int pressedTicks = 0;
    for (int tick = 0; tick < 4; ++tick)
    {
        if (harness.ResolveTick().WasPressed())
            ++pressedTicks;
    }

    // The first tick of the burst empties the latch; the rest see held state.
    EXPECT_EQ(pressedTicks, 1);
    EXPECT_TRUE(harness.Values[0].IsHeld());
}

TEST(InputResolve, ClocksConsumeEdgesIndependently)
{
    Harness harness(SingleContext({ InputActionType::Digital },
                                  { DirectBinding(0, Key(kKeySpace)) }));

    harness.PressKey(kKeySpace);
    harness.Accumulate();

    // The presentation clock resolving first must not eat the impulse the
    // simulation clock has not seen yet.
    EXPECT_TRUE(harness.ResolveFrameClock().WasPressed());
    EXPECT_TRUE(harness.ResolveTick().WasPressed());
}

TEST(InputResolve, MotionAccumulatesUntilATickConsumesIt)
{
    Harness harness(SingleContext({ InputActionType::Axis2D },
                                  { DirectBinding(0, InputControl{ InputControlSource::MouseMotion, 0 }) }));

    harness.Frame.MouseDeltaX = 3.0f;
    harness.Accumulate();
    harness.BeginFrame();
    harness.Frame.MouseDeltaX = 4.0f;
    harness.Accumulate();

    // Two frames of motion, one tick: the tick owes the whole displacement,
    // not just the last frame's.
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 7.0f);
    // And the burst's later ticks must not apply it again.
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 0.0f);
}

TEST(InputResolve, PresentationClockSeesEachFramesMotionOnce)
{
    Harness harness(SingleContext({ InputActionType::Axis2D },
                                  { DirectBinding(0, InputControl{ InputControlSource::MouseMotion, 0 }) }));

    harness.Frame.MouseDeltaX = 3.0f;
    harness.Accumulate();
    EXPECT_FLOAT_EQ(harness.ResolveFrameClock().X, 3.0f);

    harness.BeginFrame();
    harness.Frame.MouseDeltaX = 4.0f;
    harness.Accumulate();
    EXPECT_FLOAT_EQ(harness.ResolveFrameClock().X, 4.0f);
}

// ---------------------------------------------------------------------------
// Composites and transforms
// ---------------------------------------------------------------------------

TEST(InputResolve, CardinalCompositeBuildsAPlane)
{
    Harness harness(SingleContext({ InputActionType::Axis2D }, { CardinalBinding(0) }));

    harness.PressKey(kKeyW);
    harness.Accumulate();
    EXPECT_FLOAT_EQ(harness.ResolveTick().Y, 1.0f);
    EXPECT_FLOAT_EQ(harness.Values[0].X, 0.0f);

    harness.BeginFrame();
    harness.PressKey(kKeyD);
    harness.Accumulate();
    const InputActionValue diagonal = harness.ResolveTick();

    // Two directions must not out-run one.
    EXPECT_NEAR(std::sqrt(diagonal.X * diagonal.X + diagonal.Y * diagonal.Y), 1.0f, 1e-5f);
}

TEST(InputResolve, OpposingCardinalKeysCancel)
{
    Harness harness(SingleContext({ InputActionType::Axis2D }, { CardinalBinding(0) }));

    harness.PressKey(kKeyW);
    harness.PressKey(kKeyS);
    harness.Accumulate();

    const InputActionValue value = harness.ResolveTick();
    EXPECT_FLOAT_EQ(value.X, 0.0f);
    EXPECT_FLOAT_EQ(value.Y, 0.0f);
}

TEST(InputResolve, ScaleAndInversionApply)
{
    InputBinding binding = DirectBinding(0, InputControl{ InputControlSource::MouseMotion, 0 });
    binding.Scale = 0.5f;
    binding.InvertY = true;
    Harness harness(SingleContext({ InputActionType::Axis2D }, { binding }));

    harness.Frame.MouseDeltaX = 10.0f;
    harness.Frame.MouseDeltaY = 8.0f;
    harness.Accumulate();

    const InputActionValue value = harness.ResolveTick();
    EXPECT_FLOAT_EQ(value.X, 5.0f);
    EXPECT_FLOAT_EQ(value.Y, -4.0f);
}

TEST(InputResolve, DeadZoneRescalesRatherThanStepping)
{
    InputBinding binding = DirectBinding(0, InputControl{ InputControlSource::MouseMotion, 0 });
    binding.DeadZone = 0.25f;
    Harness harness(SingleContext({ InputActionType::Axis2D }, { binding }));

    harness.Frame.MouseDeltaX = 0.2f;
    harness.Accumulate();
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 0.0f);

    harness.BeginFrame();
    harness.Frame.MouseDeltaX = 0.5f;
    harness.Accumulate();
    // Just past the threshold resolves to just above rest, not to 0.5.
    EXPECT_NEAR(harness.ResolveTick().X, (0.5f - 0.25f) / 0.75f, 1e-5f);
}

TEST(InputResolve, MultipleBindingsFeedOneAction)
{
    Harness harness(SingleContext({ InputActionType::Digital },
                                  { DirectBinding(0, Key(kKeySpace)),
                                    DirectBinding(0, InputControl{ InputControlSource::MouseButton, SDL_BUTTON_LEFT }) }));

    harness.Frame.SetMouseButtonHeld(SDL_BUTTON_LEFT, true);
    harness.Frame.MouseButtonsPressed.push_back(SDL_BUTTON_LEFT);
    harness.Accumulate();

    const InputActionValue value = harness.ResolveTick();
    EXPECT_TRUE(value.WasPressed());
    EXPECT_TRUE(value.IsHeld());
    // Two bindings on one digital action stay a boolean, not a sum.
    EXPECT_FLOAT_EQ(value.X, 1.0f);
}

// ---------------------------------------------------------------------------
// Contexts
// ---------------------------------------------------------------------------

namespace
{
// Gameplay binds Escape and WASD; a higher-priority menu context binds Escape
// only, so it shadows that one control and nothing else.
BoundInputProfile MenuOverGameplay()
{
    BoundInputProfile profile;
    profile.ActionTypes = { InputActionType::Axis2D, InputActionType::Digital, InputActionType::Digital };
    profile.Bindings = {
        DirectBinding(1, Key(kKeyEscape)),   // menu.back
        CardinalBinding(0),                  // gameplay move
        DirectBinding(2, Key(kKeyEscape)),   // gameplay pause
    };
    profile.Contexts.push_back(InputContextDefinition{ "menu", 200, 0, 1 });
    profile.Contexts.push_back(InputContextDefinition{ "gameplay", 100, 1, 2 });
    return profile;
}
}

TEST(InputResolve, InactiveContextContributesNothing)
{
    Harness harness(MenuOverGameplay());
    harness.Active = { 0, 1 };   // gameplay only

    harness.PressKey(kKeyEscape);
    harness.Accumulate();
    harness.ResolveTick();

    EXPECT_FALSE(harness.Values[1].WasPressed());
    EXPECT_TRUE(harness.Values[2].WasPressed());
}

TEST(InputResolve, HigherPriorityContextClaimsTheControl)
{
    Harness harness(MenuOverGameplay());
    harness.Active = { 1, 1 };   // both active

    harness.PressKey(kKeyEscape);
    harness.PressKey(kKeyW);
    harness.Accumulate();
    harness.ResolveTick();

    // Menu takes Escape; gameplay's pause never sees it.
    EXPECT_TRUE(harness.Values[1].WasPressed());
    EXPECT_FALSE(harness.Values[2].WasPressed());
    // Claiming is per control, so movement is untouched.
    EXPECT_FLOAT_EQ(harness.Values[0].Y, 1.0f);
}

TEST(InputResolve, ActivatingAContextOverAHeldKeyDoesNotSynthesizeAPress)
{
    Harness harness(MenuOverGameplay());
    harness.Active = { 0, 1 };

    harness.PressKey(kKeyEscape);
    harness.Accumulate();
    harness.ResolveTick();
    ASSERT_TRUE(harness.Values[2].WasPressed());

    // The key is still down when the menu context arrives. It must read as
    // held, not as a fresh press, or opening a menu would fire whatever the
    // player happened to be holding.
    harness.BeginFrame();
    harness.Active = { 1, 1 };
    harness.Accumulate();
    harness.ResolveTick();

    EXPECT_FALSE(harness.Values[1].WasPressed());
    EXPECT_TRUE(harness.Values[1].IsHeld());
}

TEST(InputResolve, DeactivatingAContextReleasesWhatItHeld)
{
    Harness harness(MenuOverGameplay());
    harness.Active = { 0, 1 };

    harness.PressKey(kKeyW);
    harness.Accumulate();
    harness.ResolveTick();
    ASSERT_TRUE(harness.Values[0].IsHeld());

    // Gameplay goes away while the key is still physically down.
    harness.BeginFrame();
    harness.Active = { 0, 0 };
    harness.Accumulate();
    harness.ResolveTick();

    EXPECT_FALSE(harness.Values[0].IsHeld());
    EXPECT_TRUE(harness.Values[0].WasReleased());
    EXPECT_FLOAT_EQ(harness.Values[0].Y, 0.0f);
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

TEST(InputActionRegistryTests, MintsDenseRegistrationOrderIds)
{
    InputActionRegistry registry;
    ASSERT_TRUE(registry.Register({ "move", InputActionType::Axis2D, InputActionScope::Simulation }));
    ASSERT_TRUE(registry.Register({ "jump", InputActionType::Digital, InputActionScope::Simulation }));

    const InputActionId move = registry.Find("move");
    const InputActionId jump = registry.Find("jump");
    ASSERT_TRUE(move.IsValid());
    ASSERT_TRUE(jump.IsValid());
    EXPECT_EQ(InputActionRegistry::IndexOf(move), 0u);
    EXPECT_EQ(InputActionRegistry::IndexOf(jump), 1u);
    EXPECT_EQ(registry.Get(jump)->Type, InputActionType::Digital);
    EXPECT_FALSE(registry.Find("sprint").IsValid());
}

TEST(InputActionRegistryTests, RejectsDuplicateNames)
{
    InputActionRegistry registry;
    ASSERT_TRUE(registry.Register({ "jump", InputActionType::Digital, InputActionScope::Simulation }));

    std::string error;
    EXPECT_FALSE(registry.Register({ "jump", InputActionType::Digital, InputActionScope::Simulation }, &error));
    EXPECT_NE(error.find("duplicate"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Control names
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Gamepad
// ---------------------------------------------------------------------------

namespace
{
InputControl Stick(GamepadStick stick)
{
    return InputControl{ InputControlSource::GamepadStick, static_cast<std::uint16_t>(stick) };
}

InputControl PadButton(GamepadButton button)
{
    return InputControl{ InputControlSource::GamepadButton, static_cast<std::uint16_t>(button) };
}
}

TEST(InputResolveGamepad, StickDrivesAPlane)
{
    InputBinding binding = DirectBinding(0, Stick(GamepadStick::Left));
    binding.InvertY = true;   // stick up reads negative; forward is positive Y
    Harness harness(SingleContext({ InputActionType::Axis2D }, { binding }));

    harness.Frame.SetGamepadAxis(GamepadAxis::LeftY, -1.0f);
    harness.Accumulate();

    EXPECT_FLOAT_EQ(harness.ResolveTick().Y, 1.0f);
}

TEST(InputResolveGamepad, StickPositionHoldsAcrossACatchUpBurst)
{
    Harness harness(SingleContext({ InputActionType::Axis2D },
                                  { DirectBinding(0, Stick(GamepadStick::Left)) }));

    harness.Frame.SetGamepadAxis(GamepadAxis::LeftX, 0.5f);
    harness.Accumulate();

    // A stick is a position, not a displacement: unlike mouse motion it reads
    // the same on every tick of the burst rather than being consumed by the
    // first one.
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 0.5f);
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 0.5f);
}

TEST(InputResolveGamepad, StickDiagonalDoesNotOutrunACardinal)
{
    Harness harness(SingleContext({ InputActionType::Axis2D },
                                  { DirectBinding(0, Stick(GamepadStick::Left)) }));

    harness.Frame.SetGamepadAxis(GamepadAxis::LeftX, 1.0f);
    harness.Frame.SetGamepadAxis(GamepadAxis::LeftY, 1.0f);
    harness.Accumulate();

    const InputActionValue value = harness.ResolveTick();
    EXPECT_NEAR(std::sqrt(value.X * value.X + value.Y * value.Y), 1.0f, 1e-5f);
}

TEST(InputResolveGamepad, DeadZoneSuppressesStickDrift)
{
    InputBinding binding = DirectBinding(0, Stick(GamepadStick::Left));
    binding.DeadZone = 0.2f;
    Harness harness(SingleContext({ InputActionType::Axis2D }, { binding }));

    harness.Frame.SetGamepadAxis(GamepadAxis::LeftX, 0.1f);
    harness.Accumulate();
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 0.0f);

    harness.Frame.SetGamepadAxis(GamepadAxis::LeftX, 0.6f);
    harness.Accumulate();
    EXPECT_NEAR(harness.ResolveTick().X, (0.6f - 0.2f) / 0.8f, 1e-5f);
}

TEST(InputResolveGamepad, ButtonsCarryEdgesLikeAnyOtherControl)
{
    Harness harness(SingleContext({ InputActionType::Digital },
                                  { DirectBinding(0, PadButton(GamepadButton::South)) }));

    const auto south = static_cast<std::uint32_t>(GamepadButton::South);
    harness.Frame.SetGamepadButtonHeld(south, true);
    harness.Frame.GamepadButtonsPressed.push_back(south);
    harness.Accumulate();

    const InputActionValue pressed = harness.ResolveTick();
    EXPECT_TRUE(pressed.WasPressed());
    EXPECT_TRUE(pressed.IsHeld());

    harness.BeginFrame();
    harness.Frame.SetGamepadButtonHeld(south, false);
    harness.Frame.GamepadButtonsReleased.push_back(south);
    harness.Accumulate();
    EXPECT_TRUE(harness.ResolveTick().WasReleased());
}

TEST(InputResolveGamepad, KeyboardAndPadFeedOneActionTogether)
{
    Harness harness(SingleContext({ InputActionType::Axis2D },
                                  { CardinalBinding(0), DirectBinding(0, Stick(GamepadStick::Left)) }));

    // Either device alone drives the action; the binding a player is not using
    // contributes nothing rather than fighting the one they are.
    harness.Frame.SetGamepadAxis(GamepadAxis::LeftX, 0.5f);
    harness.Accumulate();
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 0.5f);

    harness.BeginFrame();
    harness.Frame.SetGamepadAxis(GamepadAxis::LeftX, 0.0f);
    harness.PressKey(kKeyD);
    harness.Accumulate();
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 1.0f);
}

TEST(InputResolveGamepad, ReleaseAllHeldClearsPadStateToo)
{
    InputFrame frame;
    frame.SetGamepadButtonHeld(static_cast<std::uint32_t>(GamepadButton::South), true);
    frame.SetGamepadAxis(GamepadAxis::LeftX, 0.8f);

    frame.ReleaseAllHeld();

    EXPECT_FALSE(frame.IsGamepadButtonDown(static_cast<std::uint32_t>(GamepadButton::South)));
    EXPECT_FLOAT_EQ(frame.GetGamepadAxis(GamepadAxis::LeftX), 0.0f);
    EXPECT_EQ(frame.GamepadButtonsReleased.size(), 1u);
}

TEST(InputControlNames, ParsesGamepadControls)
{
    EXPECT_EQ(ParseInputControl("gamepad.south"), PadButton(GamepadButton::South));
    EXPECT_EQ(ParseInputControl("gamepad.left_stick"), Stick(GamepadStick::Left));
    EXPECT_EQ(ParseInputControl("gamepad.right_trigger"),
              (InputControl{ InputControlSource::GamepadTrigger,
                             static_cast<std::uint16_t>(GamepadTrigger::Right) }));
    EXPECT_FALSE(ParseInputControl("gamepad.turbo").has_value());
    EXPECT_EQ(FormatInputControl(Stick(GamepadStick::Right)), "gamepad.right_stick");
}

TEST(InputControlNames, ParsesKeysMouseAndUnderscoredNames)
{
    EXPECT_EQ(ParseInputControl("key.w"), Key(kKeyW));
    EXPECT_EQ(ParseInputControl("key.left_shift"), Key(SDL_SCANCODE_LSHIFT));
    EXPECT_EQ(ParseInputControl("mouse.left"),
              (InputControl{ InputControlSource::MouseButton, SDL_BUTTON_LEFT }));
    EXPECT_EQ(ParseInputControl("mouse.delta"),
              (InputControl{ InputControlSource::MouseMotion, 0 }));
}

TEST(InputControlNames, RejectsUnknownNames)
{
    EXPECT_FALSE(ParseInputControl("key.nope").has_value());
    EXPECT_FALSE(ParseInputControl("joystick.x").has_value());
    EXPECT_FALSE(ParseInputControl("w").has_value());
    EXPECT_FALSE(ParseInputControl("key.").has_value());
}

TEST(InputControlNames, RoundTripsThroughFormatting)
{
    EXPECT_EQ(FormatInputControl(Key(kKeyW)), "key.w");
    EXPECT_EQ(FormatInputControl(Key(SDL_SCANCODE_LSHIFT)), "key.left_shift");
    EXPECT_EQ(FormatInputControl(InputControl{ InputControlSource::MouseMotion, 0 }), "mouse.delta");
}
