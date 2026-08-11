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

// A profile with one context holding the given bindings. Fire modes default to
// the compiler's default so a scenario that is not about firing says nothing
// about it; the tables stay parallel either way.
BoundInputProfile SingleContext(std::vector<InputActionType> types,
                                std::vector<InputBinding> bindings,
                                std::vector<InputActionFireMode> fireModes = {})
{
    BoundInputProfile profile;
    profile.ActionTypes = std::move(types);
    profile.ActionFireModes = std::move(fireModes);
    profile.ActionFireModes.resize(profile.ActionTypes.size(), InputActionFireMode::Pressed);
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
    std::vector<InputActionValue> SampledValues;

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
        SampledValues.assign(Profile.ActionCount(), InputActionValue{});
        ResolveInputActions(Profile, Active, Devices, Simulation, Values, 1,
                            SampledValues);
        return Values[action];
    }

    // One tick of a frame that runs several, told how many are still to come
    // including itself -- what the frame loop passes so the last takes the
    // exact remainder.
    const InputActionValue& ResolveTickOfBurst(std::uint32_t ticksLeft,
                                               std::uint32_t action = 0)
    {
        ResolveInputActions(Profile, Active, Devices, Simulation, Values, ticksLeft);
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

// A transition happened at an instant and belongs to one tick, but motion
// accumulated over a span the ticks of a catch-up frame are dividing between
// them. Handing it all to the first tick makes anything integrating it -- a
// heading, most visibly -- advance in steps that do not match the simulated
// time they cover.
TEST(InputResolve, ACatchUpBurstSplitsMotionAcrossItsTicks)
{
    Harness harness(SingleContext({ InputActionType::Axis2D },
                                  { DirectBinding(0, InputControl{ InputControlSource::MouseMotion, 0 }) }));

    harness.Frame.MouseDeltaX = 8.0f;
    harness.Frame.MouseDeltaY = 4.0f;
    harness.Accumulate();

    const InputActionValue first = harness.ResolveTickOfBurst(2);
    EXPECT_FLOAT_EQ(first.X, 4.0f);
    EXPECT_FLOAT_EQ(first.Y, 2.0f);

    const InputActionValue second = harness.ResolveTickOfBurst(1);
    EXPECT_FLOAT_EQ(second.X, 4.0f);
    EXPECT_FLOAT_EQ(second.Y, 2.0f);

    // And nothing is left over for a tick the frame did not run.
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 0.0f);
}

// Splitting must not lose or invent displacement, however the division rounds:
// the last tick of the burst takes the exact remainder.
TEST(InputResolve, SplittingMotionConservesTheTotal)
{
    Harness harness(SingleContext({ InputActionType::Axis2D },
                                  { DirectBinding(0, InputControl{ InputControlSource::MouseMotion, 0 }) }));

    harness.Frame.MouseDeltaX = 1.0f;
    harness.Accumulate();

    float total = 0.0f;
    for (std::uint32_t left = 3; left >= 1; --left)
        total += harness.ResolveTickOfBurst(left).X;

    EXPECT_FLOAT_EQ(total, 1.0f);
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 0.0f);
}

// The split is for displacement only. A press is a moment, so it still lands
// whole on the first tick of the burst and never on the ones after it.
TEST(InputResolve, SplittingMotionDoesNotSplitAnEdge)
{
    Harness harness(SingleContext({ InputActionType::Digital, InputActionType::Axis2D },
                                  { DirectBinding(0, Key(kKeySpace)),
                                    DirectBinding(1, InputControl{ InputControlSource::MouseMotion, 0 }) }));

    harness.PressKey(kKeySpace);
    harness.Frame.MouseDeltaX = 6.0f;
    harness.Accumulate();

    harness.ResolveTickOfBurst(2);
    EXPECT_TRUE(harness.Values[0].WasPressed());
    EXPECT_FLOAT_EQ(harness.Values[1].X, 3.0f);

    harness.ResolveTickOfBurst(1);
    EXPECT_FALSE(harness.Values[0].WasPressed())
        << "the press belongs to the tick that took it";
    EXPECT_TRUE(harness.Values[0].IsHeld());
    EXPECT_FLOAT_EQ(harness.Values[1].X, 3.0f);
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

namespace
{
// One digital action a player can drive from either hand.
BoundInputProfile TwoControlsOneAction()
{
    return SingleContext({ InputActionType::Digital },
                         { DirectBinding(0, Key(kKeySpace)),
                           DirectBinding(0, InputControl{ InputControlSource::MouseButton,
                                                          SDL_BUTTON_LEFT }) });
}
}

TEST(InputResolve, ASecondControlJoiningAHeldActionIsNotANewPress)
{
    Harness harness(TwoControlsOneAction());

    harness.PressKey(kKeySpace);
    harness.Accumulate();
    ASSERT_TRUE(harness.ResolveTick().WasPressed());

    // The other hand joins in on a control bound to the same action. Edges
    // belong to the action, and the action never came up.
    harness.BeginFrame();
    harness.Frame.SetMouseButtonHeld(SDL_BUTTON_LEFT, true);
    harness.Frame.MouseButtonsPressed.push_back(SDL_BUTTON_LEFT);
    harness.Accumulate();

    const InputActionValue value = harness.ResolveTick();
    EXPECT_TRUE(value.IsHeld());
    EXPECT_FALSE(value.WasPressed()) << "an edge-driven ability would fire twice";
}

TEST(InputResolve, ReleasingOneOfTwoHeldControlsDoesNotReleaseTheAction)
{
    Harness harness(TwoControlsOneAction());

    harness.PressKey(kKeySpace);
    harness.Frame.SetMouseButtonHeld(SDL_BUTTON_LEFT, true);
    harness.Frame.MouseButtonsPressed.push_back(SDL_BUTTON_LEFT);
    harness.Accumulate();
    ASSERT_TRUE(harness.ResolveTick().IsHeld());

    // One hand lets go while the other keeps holding.
    harness.BeginFrame();
    harness.ReleaseKey(kKeySpace);
    harness.Accumulate();

    const InputActionValue stillHeld = harness.ResolveTick();
    EXPECT_TRUE(stillHeld.IsHeld());
    EXPECT_FALSE(stillHeld.WasReleased()) << "a held ability would be cancelled under the player";

    // The last one lets go, and now the action really did come up.
    harness.BeginFrame();
    harness.Frame.SetMouseButtonHeld(SDL_BUTTON_LEFT, false);
    harness.Frame.MouseButtonsReleased.push_back(SDL_BUTTON_LEFT);
    harness.Accumulate();

    const InputActionValue released = harness.ResolveTick();
    EXPECT_FALSE(released.IsHeld());
    EXPECT_TRUE(released.WasReleased());
}

TEST(InputResolve, LosingEveryBindingReleasesWhatWasHeld)
{
    Harness harness(SingleContext({ InputActionType::Digital },
                                  { DirectBinding(0, Key(kKeySpace)) }));

    harness.PressKey(kKeySpace);
    harness.Accumulate();
    ASSERT_TRUE(harness.ResolveTick().IsHeld());

    // What the resolve system publishes when no profile can be bound: the
    // action falls, and it owes the release for falling.
    ReleaseInputActions(harness.Simulation, harness.Values);
    EXPECT_FALSE(harness.Values[0].IsHeld());
    EXPECT_TRUE(harness.Values[0].WasReleased());
    EXPECT_FLOAT_EQ(harness.Values[0].X, 0.0f);

    // And it only owes it once.
    ReleaseInputActions(harness.Simulation, harness.Values);
    EXPECT_FALSE(harness.Values[0].WasReleased());
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
    profile.ActionFireModes.assign(profile.ActionTypes.size(), InputActionFireMode::Pressed);
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

namespace
{
InputActionDefinition Action(std::string name, InputActionType type)
{
    return InputActionDefinition{ std::move(name), type, InputActionScope::Simulation };
}
}

TEST(InputActionRegistryTests, MintsDenseRegistrationOrderIds)
{
    InputActionRegistry registry;
    const std::vector<InputActionDefinition> set = {
        Action("move", InputActionType::Axis2D),
        Action("jump", InputActionType::Digital),
    };
    ASSERT_TRUE(registry.Rebuild(set));

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
    const std::vector<InputActionDefinition> set = {
        Action("jump", InputActionType::Digital),
        Action("jump", InputActionType::Digital),
    };

    std::string error;
    EXPECT_FALSE(registry.Rebuild(set, &error));
    EXPECT_NE(error.find("duplicate"), std::string::npos);
}

TEST(InputActionRegistryTests, ANameKeepsItsIdWhenTheSetIsReordered)
{
    InputActionRegistry registry;
    ASSERT_TRUE(registry.Rebuild(std::vector<InputActionDefinition>{
        Action("move", InputActionType::Axis2D),
        Action("jump", InputActionType::Digital) }));
    const InputActionId move = registry.Find("move");
    const InputActionId jump = registry.Find("jump");

    // Consumers resolve names once and index by id afterwards, so an author
    // reordering the document must not repoint those ids at each other.
    ASSERT_TRUE(registry.Rebuild(std::vector<InputActionDefinition>{
        Action("jump", InputActionType::Digital),
        Action("move", InputActionType::Axis2D) }));
    EXPECT_EQ(registry.Find("move"), move);
    EXPECT_EQ(registry.Find("jump"), jump);
    EXPECT_EQ(registry.Get(move)->Type, InputActionType::Axis2D);
}

TEST(InputActionRegistryTests, ADroppedNameRetiresItsSlotRatherThanLendingIt)
{
    InputActionRegistry registry;
    ASSERT_TRUE(registry.Rebuild(std::vector<InputActionDefinition>{
        Action("move", InputActionType::Axis2D),
        Action("jump", InputActionType::Digital) }));
    const InputActionId jump = registry.Find("jump");

    // jump is deleted and crouch is added in the same edit. A cached jump id
    // has to read as absent -- reading as crouch is how a control ends up
    // firing the wrong thing.
    ASSERT_TRUE(registry.Rebuild(std::vector<InputActionDefinition>{
        Action("move", InputActionType::Axis2D),
        Action("crouch", InputActionType::Digital) }));
    EXPECT_FALSE(registry.Find("jump").IsValid());
    EXPECT_EQ(registry.Get(jump), nullptr);
    EXPECT_NE(registry.Find("crouch"), jump);
    EXPECT_EQ(registry.SlotCount(), 3u);

    // And bringing it back is the same action again, not a third slot.
    ASSERT_TRUE(registry.Rebuild(std::vector<InputActionDefinition>{
        Action("move", InputActionType::Axis2D),
        Action("crouch", InputActionType::Digital),
        Action("jump", InputActionType::Digital) }));
    EXPECT_EQ(registry.Find("jump"), jump);
    EXPECT_EQ(registry.SlotCount(), 3u);
}

TEST(InputActionRegistryTests, ARejectedRebuildLeavesThePreviousVocabularyIntact)
{
    InputActionRegistry registry;
    ASSERT_TRUE(registry.Rebuild(std::vector<InputActionDefinition>{
        Action("move", InputActionType::Axis2D) }));
    const InputActionId move = registry.Find("move");

    EXPECT_FALSE(registry.Rebuild(std::vector<InputActionDefinition>{
        Action("jump", InputActionType::Digital),
        Action("jump", InputActionType::Digital) }));

    // The caller still has a working vocabulary to fall back on.
    EXPECT_EQ(registry.Find("move"), move);
    EXPECT_EQ(registry.SlotCount(), 1u);
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

// A stick reports a held position and a pointer reports an accumulated
// displacement. The mixed value keeps both, as every consumer has always read
// it; the sampled channel carries the position-sourced share alone, which is
// what lets a time-integrating consumer treat it as the rate it is. Losing
// this split is how a held stick once turned a view that stepped backward at
// tick rate.
TEST(InputResolveGamepad, SampledChannelCarriesTheStickShareAlone)
{
    InputBinding stick = DirectBinding(0, Stick(GamepadStick::Right));
    InputBinding mouse = DirectBinding(0, InputControl{ InputControlSource::MouseMotion, 0 });
    Harness harness(SingleContext({ InputActionType::Axis2D }, { stick, mouse }));

    harness.Frame.SetGamepadAxis(GamepadAxis::RightX, 0.5f);
    harness.Frame.MouseDeltaX = 3.0f;
    harness.Accumulate();

    const InputActionValue& value = harness.ResolveTick();
    EXPECT_FLOAT_EQ(value.X, 3.5f)
        << "the mixed value still carries both kinds";
    EXPECT_FLOAT_EQ(harness.SampledValues[0].X, 0.5f)
        << "the sampled share is the stick's alone";
    EXPECT_FLOAT_EQ(value.X - harness.SampledValues[0].X, 3.0f)
        << "value minus sampled is the displacement share";
}

// The sampled share is conditioned exactly like the mixed value: the same dead
// zone, scale, and inversion. A consumer subtracting one from the other would
// otherwise read a phantom displacement from a resting stick.
TEST(InputResolveGamepad, SampledShareIsConditionedLikeTheValue)
{
    InputBinding stick = DirectBinding(0, Stick(GamepadStick::Right));
    stick.DeadZone = 0.2f;
    stick.Scale = 2.0f;
    Harness harness(SingleContext({ InputActionType::Axis2D }, { stick }));

    harness.Frame.SetGamepadAxis(GamepadAxis::RightX, 0.6f);
    harness.Accumulate();

    const InputActionValue& value = harness.ResolveTick();
    EXPECT_FLOAT_EQ(harness.SampledValues[0].X, value.X)
        << "a stick-only action's sampled share is its whole value";
    EXPECT_GT(value.X, 0.0f);
    EXPECT_LT(value.X, 2.0f * 0.6f)
        << "the dead zone rescale applied before the scale";

    // At rest inside the dead zone both channels read zero.
    harness.BeginFrame();
    harness.Frame.SetGamepadAxis(GamepadAxis::RightX, 0.1f);
    harness.Accumulate();
    harness.ResolveTick();
    EXPECT_FLOAT_EQ(harness.Values[0].X, 0.0f);
    EXPECT_FLOAT_EQ(harness.SampledValues[0].X, 0.0f);
}

// Latched displacement never leaks into the sampled channel: a mouse-only
// action reads as pure displacement.
TEST(InputResolveGamepad, MouseMotionIsNeverSampled)
{
    Harness harness(SingleContext({ InputActionType::Axis2D },
                                  { DirectBinding(0, InputControl{ InputControlSource::MouseMotion, 0 }) }));

    harness.Frame.MouseDeltaX = 5.0f;
    harness.Accumulate();
    harness.ResolveTick();

    EXPECT_FLOAT_EQ(harness.Values[0].X, 5.0f);
    EXPECT_FLOAT_EQ(harness.SampledValues[0].X, 0.0f);
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

// ---------------------------------------------------------------------------
// A trigger driving a button action
// ---------------------------------------------------------------------------

namespace
{
InputControl Trigger(GamepadTrigger which)
{
    return InputControl{ InputControlSource::GamepadTrigger, static_cast<std::uint16_t>(which) };
}

Harness TriggerHarness(float threshold = 0.5f)
{
    InputBinding binding = DirectBinding(0, Trigger(GamepadTrigger::Right));
    binding.Threshold = threshold;
    return Harness(SingleContext({ InputActionType::Digital }, { binding }));
}

void PullTrigger(Harness& harness, float amount)
{
    harness.Frame.SetGamepadAxis(GamepadAxis::RightTrigger, amount);
}
}

TEST(InputResolveThreshold, ATriggerCrossingItsThresholdIsAPress)
{
    Harness harness = TriggerHarness();

    PullTrigger(harness, 0.2f);
    harness.Accumulate();
    {
        const InputActionValue value = harness.ResolveTick();
        EXPECT_FALSE(value.IsHeld()) << "resting travel is not a press";
        EXPECT_FALSE(value.WasPressed());
    }

    PullTrigger(harness, 0.8f);
    harness.Accumulate();
    {
        const InputActionValue value = harness.ResolveTick();
        EXPECT_TRUE(value.WasPressed());
        EXPECT_TRUE(value.IsHeld());
        EXPECT_FLOAT_EQ(value.X, 1.0f) << "a button action reads as a button";
    }

    // Held past the crossing is a hold, not a second press.
    harness.Accumulate();
    {
        const InputActionValue value = harness.ResolveTick();
        EXPECT_FALSE(value.WasPressed());
        EXPECT_TRUE(value.IsHeld());
    }

    PullTrigger(harness, 0.1f);
    harness.Accumulate();
    {
        const InputActionValue value = harness.ResolveTick();
        EXPECT_TRUE(value.WasReleased());
        EXPECT_FALSE(value.IsHeld());
    }
}

TEST(InputResolveThreshold, TheAuthoredThresholdDecidesWhereThePressIs)
{
    Harness harness = TriggerHarness(0.9f);

    PullTrigger(harness, 0.7f);
    harness.Accumulate();
    EXPECT_FALSE(harness.ResolveTick().IsHeld());

    PullTrigger(harness, 0.95f);
    harness.Accumulate();
    EXPECT_TRUE(harness.ResolveTick().IsHeld());
}

TEST(InputResolveThreshold, ACatchUpBurstDoesNotRepeatThePress)
{
    Harness harness = TriggerHarness();

    PullTrigger(harness, 0.8f);
    harness.Accumulate();

    int pressed = 0;
    for (int tick = 0; tick < 4; ++tick)
    {
        if (harness.ResolveTick().WasPressed())
            ++pressed;
    }
    EXPECT_EQ(pressed, 1);
    EXPECT_TRUE(harness.Values[0].IsHeld());
}

TEST(InputResolveThreshold, ActivatingAContextOverAPulledTriggerDoesNotFireAPress)
{
    Harness harness = TriggerHarness();
    harness.Active = { 0 };

    // Pulled while the context was off. Turning the context on must read as
    // held, exactly as an already-down key does -- otherwise opening a menu
    // fires whatever the player happens to be holding.
    PullTrigger(harness, 0.8f);
    harness.Accumulate();
    harness.ResolveTick();
    EXPECT_FALSE(harness.Values[0].IsHeld());

    harness.Active = { 1 };
    harness.Accumulate();
    const InputActionValue value = harness.ResolveTick();
    EXPECT_TRUE(value.IsHeld());
    EXPECT_FALSE(value.WasPressed());
}

TEST(InputResolveThreshold, TheTwoClocksCrossIndependently)
{
    Harness harness = TriggerHarness();

    PullTrigger(harness, 0.8f);
    harness.Accumulate();

    // The presentation clock reading the crossing must not consume it for the
    // simulation clock, which has not resolved yet.
    EXPECT_TRUE(harness.ResolveFrameClock().WasPressed());
    EXPECT_TRUE(harness.ResolveTick().WasPressed());
}

TEST(InputResolveThreshold, ATriggerStillDrivesAnAxisActionAsAnAxis)
{
    // Thresholding is what a button action asks for; an axis action still gets
    // the trigger's travel.
    Harness harness(SingleContext({ InputActionType::Axis1D },
                                  { DirectBinding(0, Trigger(GamepadTrigger::Right)) }));

    harness.Frame.SetGamepadAxis(GamepadAxis::RightTrigger, 0.4f);
    harness.Accumulate();
    EXPECT_FLOAT_EQ(harness.ResolveTick().X, 0.4f);
}

// ---------------------------------------------------------------------------
// Authored fire mode
//
// Which moment a digital action fires on is authored, not chosen at the call
// site. Fired is a mode-selected copy of one of the edges above, so these check
// that it inherits their guarantees rather than acquiring rules of its own.
// ---------------------------------------------------------------------------

namespace
{
Harness FireHarness(InputActionFireMode mode)
{
    return Harness(SingleContext({ InputActionType::Digital },
                                 { DirectBinding(0, Key(kKeySpace)) },
                                 { mode }));
}

enum class Step
{
    Press,
    Hold,
    Release,
    Idle,
};

// One frame per step, one tick per frame, collecting what fired.
std::vector<bool> RunFireScript(InputActionFireMode mode, const std::vector<Step>& steps)
{
    Harness harness = FireHarness(mode);
    std::vector<bool> fired;
    for (Step step : steps)
    {
        harness.BeginFrame();
        if (step == Step::Press)
            harness.PressKey(kKeySpace);
        else if (step == Step::Release)
            harness.ReleaseKey(kKeySpace);
        harness.Accumulate();
        fired.push_back(harness.ResolveTick().WasFired());
    }
    return fired;
}
}

TEST(InputResolveFireMode, EachModeFiresOnItsOwnMoment)
{
    // Press, keep holding, let go, stay idle.
    const std::vector<Step> script = { Step::Press, Step::Hold, Step::Release, Step::Idle };

    struct Case
    {
        InputActionFireMode Mode;
        std::vector<bool> Fired;
        const char* Why;
    };

    const Case cases[] = {
        { InputActionFireMode::Pressed, { true, false, false, false },
          "once as the control goes down" },
        { InputActionFireMode::Held, { true, true, false, false },
          "every pass the control is down" },
        { InputActionFireMode::Released, { false, false, true, false },
          "once as the control comes up" },
    };

    for (const Case& test : cases)
        EXPECT_EQ(RunFireScript(test.Mode, script), test.Fired) << test.Why;
}

TEST(InputResolveFireMode, FiredIsTheOnlyFlagTheModeDecides)
{
    // The raw edges stay what they were: gameplay built out of several phases
    // still reads them, whatever one moment the action authored.
    Harness harness = FireHarness(InputActionFireMode::Released);

    harness.PressKey(kKeySpace);
    harness.Accumulate();
    const InputActionValue pressed = harness.ResolveTick();
    EXPECT_TRUE(pressed.WasPressed()) << "a released-mode action still reports its press";
    EXPECT_TRUE(pressed.IsHeld());
    EXPECT_FALSE(pressed.WasFired());
}

TEST(InputResolveFireMode, AnAxisActionNeverFires)
{
    // An axis carries a magnitude rather than a moment. The compiler rejects
    // `fire` on one; a hand-built profile must not produce a moment either.
    Harness harness(SingleContext({ InputActionType::Axis2D }, { CardinalBinding(0) },
                                  { InputActionFireMode::Held }));

    harness.PressKey(kKeyW);
    harness.Accumulate();
    const InputActionValue value = harness.ResolveTick();
    EXPECT_FLOAT_EQ(value.Y, 1.0f);
    EXPECT_FALSE(value.WasFired());
}

TEST(InputResolveFireMode, ATapInsideOneFrameFiresPressAndReleaseModesOnce)
{
    // Both edges arrive on the same tick and the action never reads as held,
    // so the two edge modes each owe exactly one fire and hold owes none.
    for (InputActionFireMode mode : { InputActionFireMode::Pressed,
                                      InputActionFireMode::Released,
                                      InputActionFireMode::Held })
    {
        Harness harness = FireHarness(mode);
        harness.PressKey(kKeySpace);
        harness.ReleaseKey(kKeySpace);
        harness.Accumulate();

        const InputActionValue value = harness.ResolveTick();
        EXPECT_FALSE(value.IsHeld());
        EXPECT_EQ(value.WasFired(), mode != InputActionFireMode::Held);
    }
}

TEST(InputResolveFireMode, ACatchUpBurstFiresAnEdgeModeOnce)
{
    for (InputActionFireMode mode : { InputActionFireMode::Pressed, InputActionFireMode::Released })
    {
        Harness harness = FireHarness(mode);
        harness.PressKey(kKeySpace);
        if (mode == InputActionFireMode::Released)
        {
            harness.Accumulate();
            harness.ResolveTick();
            harness.BeginFrame();
            harness.ReleaseKey(kKeySpace);
        }
        harness.Accumulate();

        int fired = 0;
        for (int tick = 0; tick < 4; ++tick)
        {
            if (harness.ResolveTick().WasFired())
                ++fired;
        }
        EXPECT_EQ(fired, 1) << "an edge mode fires once per burst, not once per tick";
    }
}

TEST(InputResolveFireMode, AHeldModeFiresOnEveryTickOfACatchUpBurst)
{
    // The repetition a designer would come looking for. It is what the mode
    // means, so it is asserted rather than avoided.
    Harness harness = FireHarness(InputActionFireMode::Held);
    harness.PressKey(kKeySpace);
    harness.Accumulate();

    int fired = 0;
    for (int tick = 0; tick < 4; ++tick)
    {
        if (harness.ResolveTick().WasFired())
            ++fired;
    }
    EXPECT_EQ(fired, 4);
}

TEST(InputResolveFireMode, ActivatingAContextOverAHeldKeyFiresHoldButNotPress)
{
    for (InputActionFireMode mode : { InputActionFireMode::Pressed, InputActionFireMode::Held })
    {
        Harness harness = FireHarness(mode);
        harness.Active = { 0 };

        harness.PressKey(kKeySpace);
        harness.Accumulate();
        EXPECT_FALSE(harness.ResolveTick().WasFired());

        harness.Active = { 1 };
        harness.BeginFrame();
        harness.Accumulate();
        EXPECT_EQ(harness.ResolveTick().WasFired(), mode == InputActionFireMode::Held)
            << "opening a menu must not fire what the player was already holding";
    }
}

TEST(InputResolveFireMode, DeactivatingAContextFiresAReleaseMode)
{
    Harness harness = FireHarness(InputActionFireMode::Released);

    harness.PressKey(kKeySpace);
    harness.Accumulate();
    EXPECT_FALSE(harness.ResolveTick().WasFired());

    harness.Active = { 0 };
    harness.BeginFrame();
    harness.Accumulate();
    EXPECT_TRUE(harness.ResolveTick().WasFired())
        << "the binding going away is still the action coming up";
}

TEST(InputResolveFireMode, ASecondControlJoiningAHeldActionDoesNotRefirePress)
{
    Harness harness(SingleContext({ InputActionType::Digital },
                                  { DirectBinding(0, Key(kKeySpace)),
                                    DirectBinding(0, Key(kKeyEscape)) },
                                  { InputActionFireMode::Pressed }));

    harness.PressKey(kKeySpace);
    harness.Accumulate();
    EXPECT_TRUE(harness.ResolveTick().WasFired());

    harness.BeginFrame();
    harness.PressKey(kKeyEscape);
    harness.Accumulate();
    EXPECT_FALSE(harness.ResolveTick().WasFired())
        << "an edge-driven ability would fire twice";
}

TEST(InputResolveFireMode, LosingTheProfileFiresAReleaseModeExactlyOnce)
{
    Harness harness = FireHarness(InputActionFireMode::Released);

    harness.PressKey(kKeySpace);
    harness.Accumulate();
    ASSERT_TRUE(harness.ResolveTick().IsHeld());

    // No profile to consult here: the mode the release fires under is carried
    // by the clock, because the profile that declared it is what went away.
    ReleaseInputActions(harness.Simulation, harness.Values);
    EXPECT_TRUE(harness.Values[0].WasReleased());
    EXPECT_TRUE(harness.Values[0].WasFired());

    ReleaseInputActions(harness.Simulation, harness.Values);
    EXPECT_FALSE(harness.Values[0].WasReleased()) << "the release is owed once, not every pass";
    EXPECT_FALSE(harness.Values[0].WasFired());
}

TEST(InputResolveFireMode, LosingTheProfileDoesNotFireAPressMode)
{
    Harness harness = FireHarness(InputActionFireMode::Pressed);

    harness.PressKey(kKeySpace);
    harness.Accumulate();
    ASSERT_TRUE(harness.ResolveTick().IsHeld());

    ReleaseInputActions(harness.Simulation, harness.Values);
    EXPECT_TRUE(harness.Values[0].WasReleased());
    EXPECT_FALSE(harness.Values[0].WasFired())
        << "losing the bindings is not the player pressing anything";
}

TEST(InputResolveFireMode, ATriggerCrossingDrivesTheAuthoredMode)
{
    for (InputActionFireMode mode : { InputActionFireMode::Pressed, InputActionFireMode::Released })
    {
        InputBinding binding = DirectBinding(0, Trigger(GamepadTrigger::Right));
        binding.Threshold = 0.5f;
        Harness harness(SingleContext({ InputActionType::Digital }, { binding }, { mode }));

        PullTrigger(harness, 0.8f);
        harness.Accumulate();
        EXPECT_EQ(harness.ResolveTick().WasFired(), mode == InputActionFireMode::Pressed);

        PullTrigger(harness, 0.1f);
        harness.Accumulate();
        EXPECT_EQ(harness.ResolveTick().WasFired(), mode == InputActionFireMode::Released);
    }
}

TEST(InputResolveFireMode, TheTwoClocksFireIndependently)
{
    Harness harness = FireHarness(InputActionFireMode::Pressed);

    harness.PressKey(kKeySpace);
    harness.Accumulate();

    EXPECT_TRUE(harness.ResolveFrameClock().WasFired());
    EXPECT_TRUE(harness.ResolveTick().WasFired())
        << "the presentation clock must not consume the simulation's moment";
}
