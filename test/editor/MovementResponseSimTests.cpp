#include <gtest/gtest.h>

#include "movement/MovementResponseSim.h"

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/MotionComposition.h>
#include <movement/MovementComponents.h>
#include <movement/JumpState.h>
#include <movement/MovementIntent.h>
#include <movement/MovementProfileData.h>
#include <movement/MovementProfileRuntime.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementTags.h>

#include <core/json/JsonParser.h>

#include <string>
#include <vector>

//=============================================================================
// The response simulation exists to tell an author how a profile feels, and it
// is only worth anything if it agrees with the game. It reimplements one thing
// the runtime expresses as system order: tuning resolves before jump execution,
// so the launch tick is still grounded, and the jump then replaces the up
// channel during composition.
//
// These tests drive the real systems over a real World and demand the same
// per-tick velocity. Reordering those systems or changing how a jump composes
// breaks this test rather than quietly making every preview lie.
//=============================================================================

namespace
{
    constexpr float kDt = 1.0f / 60.0f;
    const std::vector<std::string> kJumpTagNames = { "movement.jump.requested" };
    const std::vector<std::string> kNoTagNames = {};
    const Vec3d kGravity{ 0.0f, -9.81f, 0.0f };
    const Vec3d kUpAxis{ 0.0f, 1.0f, 0.0f };

    constexpr std::string_view kProfileJson = R"({
        "name": "identity",
        "layers": [
            { "name": "Base",
              "set": { "max_speed": 4.5, "acceleration": 10.0, "friction": 6.0,
                       "stop_speed": 1.0, "jump_speed": 6.9, "gravity_scale": 2.07 } },
            { "name": "Air control", "when": { "support": "none" },
              "set": { "friction": 0.0, "wish_speed_cap": 0.76 } },
            { "name": "Jump startup",
              "when": { "tags": { "all": ["movement.jump.requested"] } },
              "set": { "friction": 0.0 } }
        ],
        "modes": []
    })";

    // Mirrors the runtime pipeline for one character: the three fixed-tick
    // systems that turn tuning plus intent into a composed motion request.
    //
    // One GameplayTagRegistry serves both the world and the profile binding, so
    // the tag ids the jump system checks are the ids the profile matched on.
    struct RuntimeCharacter
    {
        World WorldState;
        GameplayTagRegistry* Tags = nullptr;
        LocomotionModeRegistry* Modes = nullptr;
        MovementTags Ids{};
        // Authored tuning keys on tag names, so a profile can react to a state
        // the engine does not mint itself. This one stands for a game granting
        // it; the engine's jump reads SupportState directly.
        GameplayTagId JumpStartup{};
        LocomotionModeId Free;
        EntityId Entity;

        FreeLocomotionSystem Locomotion{ kGravity, kUpAxis };
        JumpExecutionSystem Jump;
        MotionCompositionSystem Composition;

        RuntimeCharacter()
        {
            WorldState.RegisterComponent<GameplayTagContainer>();
            WorldState.RegisterComponent<KinematicState>();
            WorldState.RegisterComponent<SupportState>();
            WorldState.RegisterComponent<MovementIntent>();
            WorldState.RegisterComponent<ResolvedMovementTuning>();
            WorldState.RegisterComponent<LocomotionOutput>();
            WorldState.RegisterComponent<MotionAxisOverride>();
            WorldState.RegisterComponent<MotionImpulse>();
            WorldState.RegisterComponent<MotionRequest>();
            WorldState.RegisterComponent<CharacterMovement>();

            Tags = &WorldState.AddResource<GameplayTagRegistry>();
            Modes = &WorldState.AddResource<LocomotionModeRegistry>(*Tags);
            Free = Modes->RegisterFree();
            Ids = RegisterMovementTags(*Tags);
            JumpStartup = Tags->RegisterTag("movement.jump.requested")
                              .value_or(GameplayTagId{});
            WorldState.AddResource<MovementTags>(Ids);
            WorldState.RegisterComponent<JumpState>();

            Entity = WorldState.CreateEntity();
            WorldState.AddComponent<GameplayTagContainer>(Entity, {});
            WorldState.AddComponent<KinematicState>(Entity, {});
            WorldState.AddComponent<SupportState>(Entity, {});
            WorldState.AddComponent<MovementIntent>(Entity, {});
            WorldState.AddComponent<ResolvedMovementTuning>(Entity, {});
            WorldState.AddComponent<LocomotionOutput>(Entity, {});
            WorldState.AddComponent<MotionAxisOverride>(Entity, {});
            WorldState.AddComponent<MotionImpulse>(Entity, {});
            WorldState.AddComponent<MotionRequest>(Entity, {});
            WorldState.AddComponent<JumpState>(Entity, {});
            // Free locomotion only drives characters whose mode is the free one.
            WorldState.AddComponent<CharacterMovement>(Entity, CharacterMovement{ .Mode = Free });
        }

        // One fixed tick in the scheduled order, with tuning already resolved
        // (the resolution system's own output) for the facts of this tick.
        Vec3d Tick(const ResolvedMovementTuning& tuning,
                   SupportKind support,
                   const Vec3d& wishDir,
                   bool jumpRequested)
        {
            *WorldState.TryGet<ResolvedMovementTuning>(Entity) = tuning;
            WorldState.TryGet<MovementIntent>(Entity)->WishDir = wishDir;

            SupportState& supportState = *WorldState.TryGet<SupportState>(Entity);
            supportState = SupportState{};
            supportState.Kind = support;
            supportState.Normal = kUpAxis;

            GameplayTagContainer& tags = *WorldState.TryGet<GameplayTagContainer>(Entity);
            if (jumpRequested)
                tags.Grant(JumpStartup);
            WorldState.TryGet<MovementIntent>(Entity)->Jump = jumpRequested;

            Locomotion.Step(WorldState, kDt);
            Jump.Step(WorldState, kDt);
            Composition.Step(WorldState);

            const Vec3d velocity = WorldState.TryGet<MotionRequest>(Entity)->Velocity;
            WorldState.TryGet<KinematicState>(Entity)->Velocity = velocity;
            return velocity;
        }
    };

    std::shared_ptr<const CompiledMovementProfile> Compile(DataAssetTypeRegistry& types,
                                                          DataSchemaRegistry& schemas)
    {
        RegisterMovementProfileData(types, schemas);
        const auto parsed = JsonParse(kProfileJson);
        if (!parsed)
            return {};
        const DataAssetTypeRegistration* type = types.Find("movement.profile");
        if (type == nullptr)
            return {};
        DataAssetCompileResult result = type->Compile(*parsed);
        return std::static_pointer_cast<const CompiledMovementProfile>(result.Value);
    }
}

TEST(MovementResponseSim, JumpArcMatchesTheRuntimeSystemsTickForTick)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    const auto compiled = Compile(types, schemas);
    ASSERT_NE(compiled, nullptr);

    // The runtime world owns the registries; the profile binds against the same
    // ones, so both paths agree on what a tag or mode means.
    RuntimeCharacter runtime;
    const MovementProfileBindResult bound = BindMovementProfile(
        *compiled, *runtime.Tags,
        [&runtime](std::string_view name)
        {
            return name == "movement.mode.free" ? runtime.Free : LocomotionModeId{};
        });
    ASSERT_TRUE(bound.IsValid()) << bound.Error;

    const MovementSimContextFn makeContext =
        [&runtime](SupportKind support, std::span<const std::string> active,
                   GameplayTagContainer& storage)
        {
            storage = GameplayTagContainer{};
            for (const std::string& name : active)
            {
                if (const GameplayTagId id = runtime.Tags->FindTag(name); id.IsValid())
                    storage.Grant(id);
            }
            MovementResolveContext context;
            context.Support = support;
            context.Mode = runtime.Free;
            context.Tags = &storage;
            return context;
        };

    MovementSimParams params;
    params.Dt = kDt;
    params.MoveSpeed = 4.5f;
    params.Gravity = kGravity;
    params.UpAxis = kUpAxis;

    const MovementJumpArc arc = SimulateJumpArc(*bound.Profile, makeContext, params);
    ASSERT_TRUE(arc.Landed);
    ASSERT_GT(arc.Samples.size(), 3u);

    const Vec3d wishDir(0.0f, 0.0f, 1.0f);
    Vec3d position = Vec3d::Zero();

    for (std::size_t tick = 0; tick + 1 < arc.Samples.size(); ++tick)
    {
        const bool launching = tick == 0;
        const SupportKind support = launching ? SupportKind::Stable : SupportKind::None;

        GameplayTagContainer active;
        const MovementResolveContext context =
            makeContext(support,
                        launching ? std::span<const std::string>(kJumpTagNames)
                                  : std::span<const std::string>(kNoTagNames),
                        active);
        const MovementResolveResult resolved =
            ResolveMovementTuning(*bound.Profile, context, params.MoveSpeed, false);

        const Vec3d velocity = runtime.Tick(resolved.Tuning, support, wishDir, launching);
        position += velocity * kDt;

        const MovementSimSample& sample = arc.Samples[tick + 1];
        EXPECT_NEAR(position.Dot(kUpAxis), sample.Height, 1.0e-4f)
            << "height diverged at tick " << tick;
        EXPECT_NEAR(position.Dot(wishDir), sample.Distance, 1.0e-4f)
            << "distance diverged at tick " << tick;
    }
}

// The launch tick is the one the phase order actually decides: the character is
// still grounded, so the jump-startup rule matches while support is Stable.
TEST(MovementResponseSim, LaunchTickResolvesGroundedWithTheJumpTag)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    const auto compiled = Compile(types, schemas);
    ASSERT_NE(compiled, nullptr);

    GameplayTagRegistry tags;
    (void)RegisterMovementTags(tags);
    // The profile keys a layer on a tag the engine does not mint; a game
    // granting it would have registered it, and binding refuses names it
    // cannot resolve.
    (void)tags.RegisterTag("movement.jump.requested");
    const MovementProfileBindResult bound = BindMovementProfile(
        *compiled, tags, [](std::string_view) { return LocomotionModeId{ 1 }; });
    ASSERT_TRUE(bound.IsValid()) << bound.Error;

    GameplayTagContainer active;
    active.Grant(tags.RegisterTag("movement.jump.requested").value_or(GameplayTagId{}));
    MovementResolveContext context;
    context.Support = SupportKind::Stable;
    context.Mode = LocomotionModeId{ 1 };
    context.Tags = &active;

    const MovementResolveResult resolved =
        ResolveMovementTuning(*bound.Profile, context, 4.5f, true);

    ASSERT_EQ(resolved.Trace.size(), 3u);
    EXPECT_TRUE(resolved.Trace[0].Matched);
    EXPECT_FALSE(resolved.Trace[1].Matched) << "air control must not apply on the ground";
    EXPECT_TRUE(resolved.Trace[2].Matched) << "the jump startup rule runs while still grounded";
    EXPECT_FLOAT_EQ(resolved.Tuning.Friction, 0.0f);
    EXPECT_FLOAT_EQ(resolved.Tuning.JumpSpeed, 6.9f);
}

TEST(MovementResponseSim, SpeedResponseSettlesAtTheAuthoredTopSpeed)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    const auto compiled = Compile(types, schemas);
    ASSERT_NE(compiled, nullptr);

    GameplayTagRegistry tags;
    (void)RegisterMovementTags(tags);
    // The profile keys a layer on a tag the engine does not mint; a game
    // granting it would have registered it, and binding refuses names it
    // cannot resolve.
    (void)tags.RegisterTag("movement.jump.requested");
    const MovementProfileBindResult bound = BindMovementProfile(
        *compiled, tags, [](std::string_view) { return LocomotionModeId{ 1 }; });
    ASSERT_TRUE(bound.IsValid()) << bound.Error;

    const MovementSimContextFn makeContext =
        [&tags](SupportKind support, std::span<const std::string> active,
                GameplayTagContainer& storage)
        {
            storage = GameplayTagContainer{};
            for (const std::string& name : active)
            {
                if (const GameplayTagId id = tags.FindTag(name); id.IsValid())
                    storage.Grant(id);
            }
            MovementResolveContext context;
            context.Support = support;
            context.Mode = LocomotionModeId{ 1 };
            context.Tags = &storage;
            return context;
        };

    MovementSimParams params;
    params.Dt = kDt;
    params.MoveSpeed = 4.5f;
    params.Gravity = kGravity;
    params.UpAxis = kUpAxis;

    const MovementSpeedResponse response =
        SimulateSpeedResponse(*bound.Profile, makeContext, params);

    EXPECT_TRUE(response.ReachedTopSpeed);
    EXPECT_NEAR(response.TopSpeed, 4.5f, 0.05f);
    EXPECT_GT(response.TimeToTopSpeed, 0.0f);
    EXPECT_GT(response.StoppingDistance, 0.0f);
}
