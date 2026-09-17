#include <gtest/gtest.h>

#include <app/BackRouter.h>
#include <app/EngineSchedule.h>
#include <app/PauseInputSystem.h>
#include <app/PauseState.h>
#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <controller/ControllerRegistration.h>
#include <controller/LookOrientation.h>
#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/DataSchema.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputActionState.h>
#include <input/InputBindingCache.h>
#include <input/InputContextSet.h>
#include <input/InputProfileData.h>
#include <input/InputRegistration.h>
#include <input/PointerCaptureSettle.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/transform/TransformComponents.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <runtime/FrameDiscontinuityBus.h>
#include <string>

// The reported bug, as a reproduction: with the menu up, move the mouse across
// the screen, then resume. Where the player was aiming must be where they are
// still aiming.
//
// The whole chain, not a model of it: the real mapper over a real profile, the
// real context set, the real look integration, and the aim it writes.

namespace
{
constexpr std::string_view kActionSetJson = R"({
    "actions": [
        { "name": "look", "type": "axis2" }
    ]
})";

constexpr std::string_view kProfileJson = R"({
    "actions": "asset://data/input_actions.sdata",
    "contexts": [
        { "name": "gameplay", "priority": 100, "bindings": [
            { "action": "look", "control": "mouse.delta", "scale": 0.01 }
        ]}
    ]
})";

class PauseLookFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        RegisterInputProfileData(Types, Schemas);

        const auto compile = [this](std::string_view type, std::string_view json) {
            const std::optional<JsonValue> parsed = JsonParse(json);
            EXPECT_TRUE(parsed.has_value());
            const DataAssetTypeRegistration* registration = Types.Find(type);
            EXPECT_NE(registration, nullptr);
            DataAssetCompileResult result = registration->Compile(*parsed);
            EXPECT_TRUE(result.IsValid()) << result.Error;
            return result.Value;
        };
        (void)Cache.Register("asset://data/input_actions.sdata",
                             std::string(kInputActionSetTypeName),
                             compile(kInputActionSetTypeName, kActionSetJson));
        Profile = InputProfileHandle{ Cache.Register(
            "asset://data/input_default.sdata", std::string(kInputProfileTypeName),
            compile(kInputProfileTypeName, kProfileJson)) };

        WorldState.RegisterComponent<LookOrientation>();
        WorldState.RegisterComponent<LocalLookControl>();
        WorldState.RegisterComponent<LocalTransform>();
        Partitions.Add(StoragePartitionId::Default());

        Pawn = WorldState.CreateEntity();
        WorldState.AddComponent<LookOrientation>(Pawn, {});
        WorldState.AddComponent<LocalLookControl>(Pawn, {});
        WorldState.AddComponent<LocalTransform>(Pawn, {});

        RegisterInputMapping(WorldState, Cache, Profile);
        RegisterInputSystems(Schedule, Cache, Logging, &Runtime.GetDiscontinuityBus());
        RegisterControllerSystems(Schedule);
        Schedule.Register<PauseInputSystem>(Pause, Router);
        Schedule.After<PauseInputSystem, InputActionResolveSystem>();
        Schedule.Init();

        Gameplay = WorldState.GetResource<InputContextSet>().Activate("gameplay");

        Fallback = Router.AddConsumer("test_shell", BackPriority::Fallback, [this] {
            Pause.Request(Pause.IsPaused() ? PausePhase::Playing : PausePhase::Paused);
            return true;
        });

        // The look action, resolved the way a game resolves it at startup.
        Runtime.GetWallClock().SetNowSource([this] { return Now; });
        RunFrame(0.0);
        const InputActionRegistry* actions =
            WorldState.GetResource<InputBindingCache>().GetActions(Profile);
        ASSERT_NE(actions, nullptr);
        WorldState.AddResource<LookInputBinding>().Look = actions->Find("look");
        ASSERT_TRUE(WorldState.GetResource<LookInputBinding>().Look.IsValid());
    }

    void TearDown() override
    {
        WorldState.GetResource<InputBindingCache>().Clear();
        UnregisterInputProfileData(Types, Schemas);
    }

    // One rendered frame in engine order, including the two things the frame
    // pump does around capture that the shell depends on.
    void RunFrame(double wallSeconds = 1.0 / 60.0, float mouseDx = 0.0f)
    {
        Now += std::chrono::duration_cast<TimeService::Clock::duration>(
            std::chrono::duration<double>(wallSeconds));

        Input.MouseDeltaX = mouseDx;
        Input.MouseDeltaY = 0.0f;

        // What the platform pump does around a capture change, in the order it
        // does it: the cursor is teleported when relative mode toggles, and
        // that jump is not the player's.
        if (Settle.ShouldDropPointerMotion())
            Input.DropPointerMotion();
        Settle.EndFrame();

        Runtime.BeginFrame();
        Runtime.ResolveLifecycleTransitions();
        Runtime.ScheduleFixedTicks();

        PreSimulateContext preSimulate{
            .Config = Config,
            .Runtime = Runtime,
            .Input = Input,
            .Entities = WorldState,
            .Partitions = Partitions,
        };
        Schedule.RunPreSimulate(preSimulate);

        while (Runtime.CanRunFixedTickThisFrame())
        {
            const FixedSimTime time = Runtime.BeginFixedTick();
            FixedLogicContext fixed{
                .Config = Config,
                .Runtime = Runtime,
                .Time = time,
                .Entities = WorldState,
                .Partitions = Partitions,
            };
            Schedule.RunFixedLogic(fixed);
            Runtime.EndFixedTick();
        }

        Runtime.BuildPresentationFrame();
        FrameUpdateContext frame{
            .Config = Config,
            .Runtime = Runtime,
            .Input = Input,
            .WallDeltaSeconds = wallSeconds,
            .Presentation = Runtime.GetCurrentFrame().Presentation,
            .Entities = WorldState,
            .Partitions = Partitions,
        };
        Schedule.RunFrameUpdate(frame);
        Runtime.EndFrame();

        Input.ClearEdges();
    }

    void PressBack()
    {
        Input.SetKeyHeld(SDL_SCANCODE_ESCAPE, true);
        Input.KeysPressed.push_back(SDL_SCANCODE_ESCAPE);
    }
    void ReleaseBack()
    {
        Input.SetKeyHeld(SDL_SCANCODE_ESCAPE, false);
        Input.KeysReleased.push_back(SDL_SCANCODE_ESCAPE);
    }

    [[nodiscard]] float Yaw() const
    {
        const LookOrientation* look = WorldState.TryGet<LookOrientation>(Pawn);
        return look != nullptr ? look->Yaw : 0.0f;
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
    PauseState Pause;
    BackRouter Router;
    BackConsumerLease Fallback;
    InputContextLease Gameplay;
    EntityId Pawn;
    PointerCaptureSettle Settle;
    TimeService::TimePoint Now{};
};
}

TEST_F(PauseLookFixture, MouseMovedWhileTheMenuIsUpNeverReachesTheAim)
{
    // Playing: the player turns, and the aim follows.
    RunFrame(1.0 / 60.0, 20.0f);
    RunFrame(1.0 / 60.0, 20.0f);
    const float aiming = Yaw();
    ASSERT_NE(aiming, 0.0f) << "precondition: looking around moves the aim";

    PressBack();
    RunFrame();
    ReleaseBack();
    ASSERT_TRUE(Pause.IsPaused());
    const float atPause = Yaw();

    // The menu is up and the cursor is free. The player moves it a long way
    // across the screen, over many frames, the way anyone reaching for a button
    // does.
    for (int frame = 0; frame < 60; ++frame)
        RunFrame(1.0 / 60.0, 30.0f);

    EXPECT_FLOAT_EQ(Yaw(), atPause) << "the aim moved while the game was paused";

    // Resume.
    PressBack();
    RunFrame();
    ReleaseBack();
    ASSERT_FALSE(Pause.IsPaused());

    // And the frames after it, with the mouse now still.
    for (int frame = 0; frame < 5; ++frame)
        RunFrame(1.0 / 60.0, 0.0f);

    EXPECT_NEAR(Yaw(), atPause, 1e-4f)
        << "resuming applied the mouse movement made while the menu was up";
}

TEST_F(PauseLookFixture, TheCursorsJumpBackToTheCentreDoesNotTurnTheView)
{
    // The other half. Resuming re-acquires the pointer, and entering relative
    // mode teleports the cursor from wherever it was over the menu. The platform
    // reports that jump the only way it reports movement, so the frame carrying
    // it looks exactly like a very fast flick.
    RunFrame(1.0 / 60.0, 10.0f);
    const float aiming = Yaw();

    PressBack();
    RunFrame();
    ReleaseBack();
    ASSERT_TRUE(Pause.IsPaused());

    for (int frame = 0; frame < 20; ++frame)
        RunFrame(1.0 / 60.0, 25.0f);

    PressBack();
    RunFrame();
    ReleaseBack();
    ASSERT_FALSE(Pause.IsPaused());

    // Capture comes back, and the pump is told the pointer was teleported.
    Settle.NotifyChanged();
    RunFrame(1.0 / 60.0, -1400.0f);   // the jump back to the centre
    RunFrame(1.0 / 60.0, 0.0f);

    EXPECT_NEAR(Yaw(), aiming, 1e-4f)
        << "the cursor being put back turned the view";

    // And the player is driving again straight after.
    RunFrame(1.0 / 60.0, 10.0f);
    RunFrame(1.0 / 60.0, 0.0f);
    EXPECT_LT(Yaw(), aiming - 0.05f) << "looking around stopped working after a resume";
}
