#pragma once

#include "NavigationFixture.h"

#include <abilities/AbilityKit.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <assets/data/DataAssetCache.h>
#include <attributes/AttributeSet.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleRegistry.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <jobs/JobSystem.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementDefs.h>
#include <movement/MovementRegistration.h>
#include <movement/components/CharacterMovement.h>
#include <navigation/NavAgentRegistration.h>
#include <navigation/NavAgentRequests.h>
#include <navigation/NavigationSystem.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>

#include <memory>
#include <vector>

// A zone cooked from boxes the agents also collide with, run through the real
// fixed tick: FixedLogic (movement and the follower), physics, transform
// propagation, PostFixed (navigation's rebuild and link state).
struct NavAgentHarness
{
    static constexpr double kDt = 1.0 / 60.0;
    static constexpr float kCapsuleCentre = 0.95f;

    EngineConfig Config;
    RuntimeFrameLoop Loop;
    DataAssetCache DataAssets;
    ConsoleRegistry Console;
    WorldComponentSchema Schema = MakeSchema();
    RuntimeWorld Runtime{ Schema };
    std::unique_ptr<JobSystem> Jobs;
    EngineSchedule Schedule;
    NavZoneFixture Fixture;
    ZoneId Zone{ 0x5eed };
    StoragePartitionId Partition;
    std::uint64_t Tick = 0;

    explicit NavAgentHarness(const NavTestGeometry& geometry, std::vector<NavLinkRecord> links = {},
                             std::uint32_t workers = 0)
        : Jobs(std::make_unique<JobSystem>(workers))
    {
        World& world = Runtime.Entities();
        RegisterMovement(world);
        GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
        for (const char* name : { "navigation.profile.humanoid", "navigation.traversal.jump",
                                  "navigation.traversal.drop", "navigation.traversal.teleport" })
            (void)tags.RegisterTag(name);

        RegisterAbilityKitSystems(Schedule);
        RegisterMovementSystems(Schedule, DataAssets);
        RegisterPhysics(Schedule);
        Schedule.Register<NavigationSystem>(Runtime, Jobs.get(), &Console);
        RegisterNavigationAgents(Schedule, Runtime, Jobs.get(), &DataAssets, &Console);
        Schedule.Init();

        RuntimeZoneRecord& record = Runtime.BeginZoneImport(Zone);
        Partition = record.Partition;
        EXPECT_NE(AttachZoneNavigation(Runtime, record, Fixture.Cook(geometry, std::move(links))), nullptr);
        EXPECT_TRUE(Runtime.PublishZone(
            Zone, ZoneParticipation{ .Visible = true, .Physics = true, .Logic = true, .Audio = true }));
        ProcessResidency();

        PhysicsWorld& physics = Schedule.Get<PhysicsStepSystem>()->GetSimulation();
        for (const NavTestGeometry::Box& box : geometry.Boxes)
        {
            BodyDesc body;
            body.Shape = CollisionShape::MakeBox((box.Max - box.Min) * 0.5f);
            body.Position = (box.Min + box.Max) * 0.5f;
            body.Motion = BodyMotion::Static;
            body.Layer = CollisionLayer::Static;
            (void)physics.AddBody(body);
        }
    }

    static WorldComponentSchema MakeSchema()
    {
        WorldComponentSchema schema;
        RegisterEngineRuntimeComponents(schema);
        schema.Seal();
        return schema;
    }

    World& Entities() { return Runtime.Entities(); }
    NavAgentSystem& Agents() { return *Schedule.Get<NavAgentSystem>(); }
    NavigationSystem& Navigation() { return *Schedule.Get<NavigationSystem>(); }

    [[nodiscard]] GameplayTagId Tag(std::string_view name)
    {
        return std::as_const(Entities()).TryGetResource<GameplayTagRegistry>()->FindTag(name);
    }

    // A walking character standing on the floor at `feet`.
    EntityId SpawnAgent(Vec3d feet, std::initializer_list<std::string_view> tags = {})
    {
        World& world = Entities();
        const EntityId agent = world.CreateEntity(Partition);
        Transform3f pose = Transform3f::Identity();
        pose.Position = feet + Vec3d(0.0f, kCapsuleCentre, 0.0f);
        world.AddComponent<LocalTransform>(agent, LocalTransform{ pose });
        world.AddComponent<WorldTransform>(agent, WorldTransform{ pose });
        world.AddComponent<CharacterController>(agent, CharacterController{});
        world.AddComponent<CharacterMovement>(
            agent, CharacterMovement{ .Mode = world.GetResource<LocomotionModeRegistry>().FreeMode() });
        AttributeSet attributes;
        attributes.Add(world.GetResource<MovementDefs>().MoveSpeed, 6.0f);
        world.AddComponent<AttributeSet>(agent, attributes);
        GameplayTagContainer held;
        for (const std::string_view tag : tags)
            EXPECT_TRUE(held.Grant(Tag(tag)));
        world.AddComponent<GameplayTagContainer>(agent, held);
        NavAgent config;
        config.Profile = "navigation.profile.humanoid";
        world.AddComponent<NavAgent>(agent, config);
        return agent;
    }

    // Something to approach: a transform on the floor at `feet`.
    EntityId SpawnMarker(Vec3d feet)
    {
        World& world = Entities();
        const EntityId marker = world.CreateEntity(Partition);
        Transform3f pose = Transform3f::Identity();
        pose.Position = feet + Vec3d(0.0f, kCapsuleCentre, 0.0f);
        world.AddComponent<LocalTransform>(marker, LocalTransform{ pose });
        world.AddComponent<WorldTransform>(marker, WorldTransform{ pose });
        return marker;
    }

    void MoveMarker(EntityId marker, Vec3d feet)
    {
        Transform3f pose = Transform3f::Identity();
        pose.Position = feet + Vec3d(0.0f, kCapsuleCentre, 0.0f);
        Entities().TryGet<LocalTransform>(marker)->Value = pose;
    }

    void Step()
    {
        World& world = Entities();
        const FrameZoneView& zones = Runtime.BuildFrameView();
        const FixedSimTime time{ .DeltaSeconds = kDt, .TickIndex = Tick };
        FixedLogicContext logic{ .Config = Config, .Runtime = Loop, .Time = time, .Entities = world,
                                 .Partitions = zones.Logic };
        Schedule.RunFixedLogic(logic);
        PhysicsContext physics{ Config, Loop, time, world, zones.Physics };
        Schedule.RunPhysics(physics);
        PropagateTransforms(world, zones.Logic, TransformPropagationDomain::Simulation);
        PostFixedContext post{ Config, Loop, time, world, zones.Logic };
        Schedule.RunPostFixed(post);
        Runtime.EndFrameView();
        ++Tick;
    }

    // Steps until `done` holds or `ticks` pass; true if it held.
    template <typename Done>
    bool StepUntil(Done&& done, int ticks)
    {
        for (int i = 0; i < ticks; ++i)
        {
            Step();
            if (done())
                return true;
        }
        return false;
    }

    [[nodiscard]] const NavAgentProgress& Progress(EntityId agent)
    {
        return *std::as_const(Entities()).TryGet<NavAgentProgress>(agent);
    }

    [[nodiscard]] Vec3d Position(EntityId agent)
    {
        return std::as_const(Entities()).TryGet<WorldTransform>(agent)->Value.Position;
    }

    [[nodiscard]] Vec3d Intent(EntityId agent)
    {
        return std::as_const(Entities()).TryGet<MovementIntent>(agent)->WishDir;
    }

    NavRequestId GoTo(EntityId agent, Vec3d point)
    {
        return RequestNavigation(Entities(), agent, NavDestination::AtPoint(point));
    }

    void ProcessResidency()
    {
        ZoneResidencyContext ctx{ Config, Runtime.Entities(), Runtime.BeginResidencyProcessing() };
        Schedule.RunZoneResidency(ctx);
        Runtime.FinalizeResidencyProcessing();
    }

    void SetParticipation(ZoneParticipation participation)
    {
        EXPECT_TRUE(Runtime.RequestParticipation(Zone, participation));
        Runtime.FlushLifecycleRequests();
        ProcessResidency();
    }

    void Detach()
    {
        EXPECT_TRUE(Runtime.RequestDetach(Zone));
        Runtime.FlushLifecycleRequests();
        ProcessResidency();
    }
};
