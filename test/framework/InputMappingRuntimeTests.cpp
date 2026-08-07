#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/DataSchema.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputActionState.h>
#include <input/InputContextSet.h>
#include <input/InputProfileData.h>
#include <input/InputRegistration.h>
#include <runtime/RuntimeFrameLoop.h>

#include <SDL3/SDL.h>

#include <format>
#include <string>
#include <vector>

// The mapper driven the way the frame loop drives it: one PreSimulate per
// rendered frame, then zero, one, or several fixed ticks against that frame's
// input.

namespace
{
constexpr std::string_view kActionSetPath = "asset://data/input_actions.sdata";
constexpr std::string_view kProfilePath = "asset://data/input_default.sdata";

constexpr std::string_view kActionSetJson = R"({
    "actions": [
        { "name": "move", "type": "axis2" },
        { "name": "look", "type": "axis2" },
        { "name": "jump", "type": "digital" },
        { "name": "menu_back", "type": "digital", "scope": "presentation" }
    ]
})";

constexpr std::string_view kProfileJson = R"({
    "actions": "asset://data/input_actions.sdata",
    "contexts": [
        {
            "name": "gameplay",
            "priority": 100,
            "bindings": [
                { "action": "move", "composite": "cardinal",
                  "left": "key.a", "right": "key.d", "down": "key.s", "up": "key.w" },
                { "action": "look", "control": "mouse.delta", "scale": 0.5 },
                { "action": "jump", "control": "key.space" }
            ]
        },
        {
            "name": "menu",
            "priority": 200,
            "bindings": [
                { "action": "menu_back", "control": "key.escape" },
                { "action": "menu_back", "control": "key.space" }
            ]
        }
    ]
})";

// Records what the simulation saw on every tick, so a scenario can assert on
// the whole burst rather than only its last tick.
struct TickRecorder
{
    struct Sample
    {
        std::uint64_t Tick = 0;
        InputActionValue Jump;
        InputActionValue Move;
    };

    InputActionId Jump;
    InputActionId Move;
    std::vector<Sample> Samples;

    void FixedLogic(FixedLogicContext& ctx)
    {
        const auto* state = ctx.Entities.TryGetResource<InputActionState>();
        if (state == nullptr)
            return;
        const InputActionView view = state->Tick();
        Samples.push_back(Sample{ state->CurrentTick(), view.Get(Jump), view.Get(Move) });
    }
};

class InputRuntimeFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        RegisterInputProfileData(Types, Schemas);
        PublishProfile(kProfileJson);

        Partitions.Add(StoragePartitionId::Default());
        RegisterInputMapping(WorldState, Cache, Profile);
        RegisterInputSystems(Schedule, Cache, Logging);
        Recorder = &Schedule.Register<TickRecorder>();
        Schedule.After<TickRecorder, InputActionResolveSystem>();
        Schedule.Init();
    }

    void TearDown() override
    {
        WorldState.GetResource<InputBindingCache>().Clear();
        UnregisterInputProfileData(Types, Schemas);
    }

    std::shared_ptr<const void> CompileDocument(std::string_view typeName, std::string_view json)
    {
        const std::optional<JsonValue> parsed = JsonParse(json);
        EXPECT_TRUE(parsed.has_value());
        const DataAssetTypeRegistration* type = Types.Find(typeName);
        EXPECT_NE(type, nullptr);
        DataAssetCompileResult result = type->Compile(*parsed);
        EXPECT_TRUE(result.IsValid()) << result.Error;
        return result.Value;
    }

    void PublishProfile(std::string_view profileJson)
    {
        if (!Cache.Find(kActionSetPath).IsValid())
        {
            (void)Cache.Register(kActionSetPath, std::string(kInputActionSetTypeName),
                                 CompileDocument(kInputActionSetTypeName, kActionSetJson));
        }
        Profile = InputProfileHandle{ Cache.Register(
            kProfilePath, std::string(kInputProfileTypeName),
            CompileDocument(kInputProfileTypeName, profileJson)) };
    }

    // The author saves an edit to the action set while the game runs.
    void ReloadActionSet(std::string_view actionSetJson)
    {
        ASSERT_TRUE(Cache.ReloadInPlace(kActionSetPath, kInputActionSetTypeName,
                                        CompileDocument(kInputActionSetTypeName, actionSetJson)));
    }

    // A second profile asset, the way a game ships one per control scheme.
    void PublishAlternateProfile(std::string_view profileJson)
    {
        Profile = InputProfileHandle{ Cache.Register(
            "asset://data/input_alternate.sdata", std::string(kInputProfileTypeName),
            CompileDocument(kInputProfileTypeName, profileJson)) };
    }

    // Resolves action ids once the profile has bound, the way a game does in
    // its startup hook.
    void BindActionIds()
    {
        const InputActionRegistry* actions =
            WorldState.GetResource<InputBindingCache>().GetActions(Profile);
        ASSERT_NE(actions, nullptr);
        Recorder->Jump = actions->Find("jump");
        Recorder->Move = actions->Find("move");
        MenuBack = actions->Find("menu_back");
        Look = actions->Find("look");
    }

    void PressKey(std::uint16_t scancode)
    {
        Input.SetKeyHeld(scancode, true);
        Input.KeysPressed.push_back(scancode);
    }

    void ReleaseKey(std::uint16_t scancode)
    {
        Input.SetKeyHeld(scancode, false);
        Input.KeysReleased.push_back(scancode);
    }

    // One rendered frame: the frame-level pass, then `ticks` fixed ticks.
    void RunFrame(int ticks)
    {
        PreSimulateContext preSimulate{
            .Config = Config,
            .Runtime = Runtime,
            .Input = Input,
            .Entities = WorldState,
            .Partitions = Partitions,
        };
        Schedule.RunPreSimulate(preSimulate);

        for (int i = 0; i < ticks; ++i)
        {
            FixedLogicContext fixed{
                .Config = Config,
                .Runtime = Runtime,
                .Time = FixedSimTime{ 1.0 / 60.0, NextTick++ },
                .Entities = WorldState,
                .Partitions = Partitions,
            };
            Schedule.RunFixedLogic(fixed);
        }

        // What the platform layer does at the start of the next frame.
        Input.MouseDeltaX = 0.0f;
        Input.MouseDeltaY = 0.0f;
        Input.MouseWheelY = 0.0f;
    }

    [[nodiscard]] InputActionView FrameActions()
    {
        return WorldState.GetResource<InputActionState>().Frame();
    }

    DataAssetTypeRegistry Types;
    DataSchemaRegistry Schemas;
    DataAssetCache Cache;
    LoggingProvider Logging;
    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    InputFrame Input;
    World WorldState;
    StoragePartitionSet Partitions;
    EngineSchedule Schedule;
    InputProfileHandle Profile;
    TickRecorder* Recorder = nullptr;
    InputActionId MenuBack;
    InputActionId Look;
    std::uint64_t NextTick = 1;
};
}

TEST_F(InputRuntimeFixture, GameplayReadsActionsWithoutTouchingTheDevice)
{
    InputContextLease gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");
    RunFrame(1);
    BindActionIds();

    PressKey(SDL_SCANCODE_W);
    PressKey(SDL_SCANCODE_SPACE);
    RunFrame(1);

    ASSERT_FALSE(Recorder->Samples.empty());
    const auto& sample = Recorder->Samples.back();
    EXPECT_TRUE(sample.Jump.WasPressed());
    EXPECT_TRUE(sample.Jump.IsHeld());
    EXPECT_FLOAT_EQ(sample.Move.Y, 1.0f);
}

TEST_F(InputRuntimeFixture, AFrameWithNoTicksHandsItsEdgeToTheNextTick)
{
    InputContextLease gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");
    RunFrame(1);
    BindActionIds();
    Recorder->Samples.clear();

    PressKey(SDL_SCANCODE_SPACE);
    ReleaseKey(SDL_SCANCODE_SPACE);
    RunFrame(0);
    ASSERT_TRUE(Recorder->Samples.empty());

    RunFrame(1);

    // The whole tap happened while no tick ran. Both edges still arrive.
    ASSERT_EQ(Recorder->Samples.size(), 1u);
    EXPECT_TRUE(Recorder->Samples[0].Jump.WasPressed());
    EXPECT_TRUE(Recorder->Samples[0].Jump.WasReleased());
}

TEST_F(InputRuntimeFixture, ACatchUpBurstFiresThePressOnce)
{
    InputContextLease gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");
    RunFrame(1);
    BindActionIds();
    Recorder->Samples.clear();

    PressKey(SDL_SCANCODE_SPACE);
    RunFrame(4);

    ASSERT_EQ(Recorder->Samples.size(), 4u);
    int pressed = 0;
    for (const auto& sample : Recorder->Samples)
    {
        if (sample.Jump.WasPressed())
            ++pressed;
        EXPECT_TRUE(sample.Jump.IsHeld());
    }
    EXPECT_EQ(pressed, 1);
}

TEST_F(InputRuntimeFixture, MotionIsDistributedToTheFirstTickOfTheFrame)
{
    InputContextLease gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");
    RunFrame(1);
    BindActionIds();

    Input.MouseDeltaX = 10.0f;
    RunFrame(2);

    const auto& state = WorldState.GetResource<InputActionState>();
    // Two ticks, one frame of motion: the burst applies the displacement once.
    ASSERT_GE(state.HistoryCount(), 2u);
    EXPECT_FLOAT_EQ(state.History(1).View().Axis(Look), 5.0f);
    EXPECT_FLOAT_EQ(state.History(0).View().Axis(Look), 0.0f);

    // The presentation clock still saw the whole frame's motion.
    EXPECT_FLOAT_EQ(FrameActions().Axis(Look), 5.0f);
}

TEST_F(InputRuntimeFixture, ContextsActivateAndDeactivateThroughLeases)
{
    InputContextSet& contexts = WorldState.GetResource<InputContextSet>();
    RunFrame(1);
    BindActionIds();

    // Nothing is active yet, so no binding can fire.
    PressKey(SDL_SCANCODE_SPACE);
    RunFrame(1);
    EXPECT_FALSE(Recorder->Samples.back().Jump.IsHeld());

    {
        InputContextLease gameplay = contexts.Activate("gameplay");
        RunFrame(1);
        EXPECT_TRUE(contexts.IsActive("gameplay"));
        // The key was already down when the context arrived: held, not pressed.
        EXPECT_TRUE(Recorder->Samples.back().Jump.IsHeld());
        EXPECT_FALSE(Recorder->Samples.back().Jump.WasPressed());
    }

    // Dropping the lease is what turns the context off.
    RunFrame(1);
    EXPECT_FALSE(contexts.IsActive("gameplay"));
    EXPECT_FALSE(Recorder->Samples.back().Jump.IsHeld());
    EXPECT_TRUE(Recorder->Samples.back().Jump.WasReleased());
}

TEST_F(InputRuntimeFixture, LeasesAreCountedSoOneHolderCannotCancelAnother)
{
    InputContextSet& contexts = WorldState.GetResource<InputContextSet>();
    InputContextLease first = contexts.Activate("gameplay");
    {
        InputContextLease second = contexts.Activate("gameplay");
        RunFrame(1);
        EXPECT_TRUE(contexts.IsActive("gameplay"));
    }
    RunFrame(1);
    EXPECT_TRUE(contexts.IsActive("gameplay"));

    first.Reset();
    RunFrame(1);
    EXPECT_FALSE(contexts.IsActive("gameplay"));
}

TEST_F(InputRuntimeFixture, AHigherPriorityContextShadowsOneControlOnly)
{
    InputContextSet& contexts = WorldState.GetResource<InputContextSet>();
    InputContextLease gameplay = contexts.Activate("gameplay");
    InputContextLease menu = contexts.Activate("menu");
    RunFrame(1);
    BindActionIds();

    PressKey(SDL_SCANCODE_SPACE);
    PressKey(SDL_SCANCODE_W);
    RunFrame(1);

    // Menu takes Space for its own binding; gameplay's jump never sees it.
    EXPECT_FALSE(Recorder->Samples.back().Jump.IsHeld());
    EXPECT_TRUE(FrameActions().Held(MenuBack));
    // Movement is untouched: claiming is per control, not per context.
    EXPECT_FLOAT_EQ(Recorder->Samples.back().Move.Y, 1.0f);
}

TEST_F(InputRuntimeFixture, ContextChangesTakeEffectAtTheFrameBoundary)
{
    InputContextSet& contexts = WorldState.GetResource<InputContextSet>();
    RunFrame(1);
    BindActionIds();
    Recorder->Samples.clear();

    // Taken after PreSimulate settled the frame: every tick of this frame must
    // still resolve against the set the frame began with.
    PreSimulateContext preSimulate{
        .Config = Config,
        .Runtime = Runtime,
        .Input = Input,
        .Entities = WorldState,
        .Partitions = Partitions,
    };
    Schedule.RunPreSimulate(preSimulate);

    InputContextLease gameplay = contexts.Activate("gameplay");
    PressKey(SDL_SCANCODE_SPACE);

    for (int i = 0; i < 3; ++i)
    {
        FixedLogicContext fixed{
            .Config = Config,
            .Runtime = Runtime,
            .Time = FixedSimTime{ 1.0 / 60.0, NextTick++ },
            .Entities = WorldState,
            .Partitions = Partitions,
        };
        Schedule.RunFixedLogic(fixed);
    }

    ASSERT_EQ(Recorder->Samples.size(), 3u);
    for (const auto& sample : Recorder->Samples)
        EXPECT_FALSE(sample.Jump.IsHeld());

    RunFrame(1);
    EXPECT_TRUE(Recorder->Samples.back().Jump.IsHeld());
}

TEST_F(InputRuntimeFixture, FocusLossReleasesActionsRatherThanStickingThemDown)
{
    InputContextLease gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");
    RunFrame(1);
    BindActionIds();

    PressKey(SDL_SCANCODE_W);
    RunFrame(1);
    ASSERT_FLOAT_EQ(Recorder->Samples.back().Move.Y, 1.0f);

    // What the capture layer does when the window loses focus.
    Input.ReleaseAllHeld();
    RunFrame(1);

    EXPECT_FLOAT_EQ(Recorder->Samples.back().Move.Y, 0.0f);
    EXPECT_TRUE(Recorder->Samples.back().Move.WasReleased());
}

TEST_F(InputRuntimeFixture, SwappingProfilesKeepsContextsActiveByName)
{
    InputContextSet& contexts = WorldState.GetResource<InputContextSet>();
    InputContextLease gameplay = contexts.Activate("gameplay");
    RunFrame(1);
    BindActionIds();

    PressKey(SDL_SCANCODE_SPACE);
    RunFrame(1);
    ASSERT_TRUE(Recorder->Samples.back().Jump.IsHeld());

    // A different control scheme over the same actions.
    ReleaseKey(SDL_SCANCODE_SPACE);
    PublishAlternateProfile(R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "jump", "control": "key.j" } ] } ]
    })");
    RegisterInputMapping(WorldState, Cache, Profile);

    PressKey(SDL_SCANCODE_J);
    RunFrame(1);

    // The lease survives the swap because it names the context, and the new
    // binding is live.
    EXPECT_TRUE(contexts.IsActive("gameplay"));
    EXPECT_TRUE(Recorder->Samples.back().Jump.IsHeld());
}

TEST_F(InputRuntimeFixture, ABrokenProfileReportsItselfThroughTheActionState)
{
    InputContextLease gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");
    RunFrame(1);
    BindActionIds();

    PublishAlternateProfile(R"({
        "actions": "asset://data/input_actions.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "not_an_action", "control": "key.j" } ] } ]
    })");
    RegisterInputMapping(WorldState, Cache, Profile);
    RunFrame(1);

    // Visible and actionable, naming the binding and the action it could not
    // resolve, rather than a silently dead control scheme.
    const auto& state = WorldState.GetResource<InputActionState>();
    EXPECT_NE(state.Error().find("not_an_action"), std::string::npos) << state.Error();
    EXPECT_NE(state.Error().find("names no declared action"), std::string::npos)
        << state.Error();
}

TEST_F(InputRuntimeFixture, LosingTheProfileReleasesWhatItWasHolding)
{
    InputContextLease gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");
    RunFrame(1);
    BindActionIds();

    PressKey(SDL_SCANCODE_SPACE);
    RunFrame(1);
    ASSERT_TRUE(Recorder->Samples.back().Jump.IsHeld());

    // Pointed at a profile whose action set nothing published: no vocabulary,
    // so not one binding resolves and there are no previous tables under this
    // handle to fall back on.
    PublishAlternateProfile(R"({
        "actions": "asset://data/input_absent.sdata",
        "contexts": [ { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "jump", "control": "key.space" } ] } ]
    })");
    RegisterInputMapping(WorldState, Cache, Profile);
    RunFrame(1);

    // The key is still physically down. What gameplay reads is not: leaving the
    // last resolved values standing would have the simulation jumping for the
    // rest of the session.
    const auto& lost = Recorder->Samples.back();
    EXPECT_FALSE(lost.Jump.IsHeld());
    EXPECT_TRUE(lost.Jump.WasReleased());
    EXPECT_FALSE(FrameActions().Held(Recorder->Jump));

    // And it owes that release once, not on every tick from here on.
    RunFrame(1);
    EXPECT_FALSE(Recorder->Samples.back().Jump.WasReleased());
}

// ---------------------------------------------------------------------------
// The seam the networking ticket builds on
// ---------------------------------------------------------------------------

TEST_F(InputRuntimeFixture, TickRecordsProjectIntoAFlatPlayerCommand)
{
    InputContextLease gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");
    RunFrame(1);
    BindActionIds();

    PressKey(SDL_SCANCODE_W);
    PressKey(SDL_SCANCODE_SPACE);
    RunFrame(1);
    ReleaseKey(SDL_SCANCODE_SPACE);
    RunFrame(1);

    // What a command builder does: pick the actions this game replicates and
    // copy them out by index. No strings, no pointers, no platform types.
    struct PlayerCommand
    {
        std::uint64_t Tick = 0;
        float MoveX = 0.0f;
        float MoveY = 0.0f;
        std::uint8_t Buttons = 0;
    };
    static_assert(std::is_trivially_copyable_v<PlayerCommand>);

    const auto& state = WorldState.GetResource<InputActionState>();
    std::vector<PlayerCommand> commands;
    for (std::size_t i = 0; i < state.HistoryCount(); ++i)
    {
        const InputActionTickRecord record = state.History(i);
        const InputActionView view = record.View();
        const Vec2d move = view.Axis2(Recorder->Move);
        commands.push_back(PlayerCommand{
            record.Tick,
            move.X,
            move.Y,
            static_cast<std::uint8_t>(view.Held(Recorder->Jump) ? 1u : 0u),
        });
    }

    ASSERT_GE(commands.size(), 2u);
    // Newest first: jump released this tick, still held on the one before.
    EXPECT_EQ(commands[0].Buttons, 0u);
    EXPECT_EQ(commands[1].Buttons, 1u);
    EXPECT_FLOAT_EQ(commands[1].MoveY, 1.0f);
    EXPECT_GT(commands[0].Tick, commands[1].Tick);
}

TEST_F(InputRuntimeFixture, TheAuthoredFireModeDecidesWhatTheSimulationReads)
{
    InputContextLease gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");
    RunFrame(1);
    BindActionIds();

    const auto firedTicks = [this]
    {
        int count = 0;
        for (const auto& sample : Recorder->Samples)
        {
            if (sample.Jump.WasFired())
                ++count;
        }
        return count;
    };

    // Nothing authored: a button fires once per press however many ticks the
    // frame ran.
    Recorder->Samples.clear();
    PressKey(SDL_SCANCODE_SPACE);
    RunFrame(3);
    EXPECT_EQ(firedTicks(), 1);

    ReleaseKey(SDL_SCANCODE_SPACE);
    RunFrame(1);

    // The designer edits the action set and saves. The cached id still names
    // jump, and the same control now fires on every tick it is down -- an edit
    // to data, with no change to the system that reads it.
    ReloadActionSet(R"({
        "actions": [
            { "name": "move", "type": "axis2" },
            { "name": "look", "type": "axis2" },
            { "name": "jump", "type": "digital", "fire": "held" },
            { "name": "menu_back", "type": "digital", "scope": "presentation" }
        ]
    })");

    Recorder->Samples.clear();
    PressKey(SDL_SCANCODE_SPACE);
    RunFrame(3);
    EXPECT_EQ(firedTicks(), 3);
}

TEST_F(InputRuntimeFixture, HistoryRecordsCarryTheFiredMoment)
{
    InputContextLease gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");
    RunFrame(1);
    BindActionIds();

    PressKey(SDL_SCANCODE_SPACE);
    RunFrame(1);

    // A command builder reads records rather than the live view, so the moment
    // has to survive in the record it was resolved into.
    const InputActionState& state = WorldState.GetResource<InputActionState>();
    ASSERT_GT(state.HistoryCount(), 0u);
    EXPECT_TRUE(state.History(0).View().Fired(Recorder->Jump));
}

// The lease token crosses ILifetimeOwner's uint64_t slot, so the slot index
// survives an encode/decode round trip only if the full 32 bits come back.
// Every other test here holds a handful of contexts, which would pass even if
// the decode kept one byte.
TEST(InputContextSetTest, LeasesResolveBeyondTheFirstByteOfSlotIndices)
{
    InputContextSet contexts;
    std::vector<InputContextLease> leases;
    constexpr std::size_t kContexts = 300;

    for (std::size_t i = 0; i < kContexts; ++i)
        leases.push_back(contexts.Activate(std::format("context{}", i)));
    contexts.ApplyPending();

    for (std::size_t i = 0; i < kContexts; ++i)
        EXPECT_TRUE(contexts.IsActive(std::format("context{}", i))) << "context " << i;

    // Dropping one lease must reach that slot and no other.
    leases[kContexts - 1].Reset();
    contexts.ApplyPending();

    EXPECT_FALSE(contexts.IsActive(std::format("context{}", kContexts - 1)));
    for (std::size_t i = 0; i + 1 < kContexts; ++i)
        EXPECT_TRUE(contexts.IsActive(std::format("context{}", i))) << "context " << i;
}
