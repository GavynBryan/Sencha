#pragma once

#include <app/GameContexts.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleRegistry.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <math/Quat.h>
#include <perception/PerceptionFrame.h>
#include <perception/PerceptionSystem.h>
#include <perception/PerceptionVocabulary.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <runtime/RuntimeFrameLoop.h>
#include <spatial/sight/SightComponents.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <numbers>

// A World with sight components, a physics scene the test fills by hand, and
// one PerceptionSystem stepped a fixed tick at a time. Eyes and samples sit
// at their entity's origin unless a test says otherwise; forward is -Z.
struct PerceptionHarness
{
    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    World Entities;
    StoragePartitionSet Partitions;
    PhysicsWorld Physics;
    ConsoleRegistry Console;
    PerceptionSystem System{ Physics, &Console };
    std::uint64_t Tick = 0;

    PerceptionHarness()
    {
        Entities.RegisterComponent<WorldTransform>();
        Entities.RegisterComponent<SightSensor>();
        Entities.RegisterComponent<SightTarget>();
        Entities.RegisterComponent<GameplayTagContainer>();
        Partitions.Add(StoragePartitionId::Default());
    }

    static Quatf Yaw(float radians) { return Quatf::FromAxisAngle(Vec3d::Up(), radians); }

    EntityId Observer(const Vec3d& position, float range = 50.0f,
                      float halfAngle = std::numbers::pi_v<float> / 4.0f, Quatf rotation = Quatf::Identity())
    {
        const EntityId entity = Entities.CreateEntity();
        Entities.AddComponent<WorldTransform>(entity, WorldTransform{ Transform3f(position, rotation, Vec3d::One()) });
        Entities.AddComponent<SightSensor>(entity, SightSensor{ .Range = range,
                                                                .HalfAngle = halfAngle,
                                                                .EyeOffset = Vec3d::Zero() });
        return entity;
    }

    EntityId Target(const Vec3d& position)
    {
        const EntityId entity = Entities.CreateEntity();
        Entities.AddComponent<WorldTransform>(entity, WorldTransform{ Transform3f(position, Quatf::Identity(), Vec3d::One()) });
        Entities.AddComponent<SightTarget>(entity, SightTarget{ .SampleOffset = Vec3d::Zero() });
        return entity;
    }

    void Place(EntityId entity, const Vec3d& position, Quatf rotation = Quatf::Identity())
    {
        Entities.TryGet<WorldTransform>(entity)->Value = Transform3f(position, rotation, Vec3d::One());
    }

    PhysicsBodyId Box(const Vec3d& position, const Vec3d& halfExtents, EntityId owner = {}, bool trigger = false)
    {
        BodyDesc body;
        body.Shape = CollisionShape::MakeBox(halfExtents);
        body.Position = position;
        body.Motion = BodyMotion::Static;
        body.Layer = trigger ? CollisionLayer::Trigger : CollisionLayer::Static;
        body.IsTrigger = trigger;
        body.UserData = PackEntity(owner);
        return Physics.AddBody(body);
    }

    void SetCVar(std::string_view name, CVarValue value)
    {
        ASSERT_TRUE(Console.SetCVar(name, value, { "test" }, ConsolePhase::EngineReady).Succeeded());
    }

    void Step()
    {
        FixedLogicContext ctx{
            .Config = Config,
            .Runtime = Runtime,
            .Time = FixedSimTime{ .TickIndex = ++Tick },
            .Entities = Entities,
            .Partitions = Partitions,
        };
        System.FixedLogic(ctx);
    }

    [[nodiscard]] const PerceptionFrame& Frame() const { return *Entities.TryGetResource<PerceptionFrame>(); }
    [[nodiscard]] bool Sees(EntityId observer, EntityId subject) const
    {
        return Seen(observer, subject) != nullptr;
    }
    [[nodiscard]] const Observation* Seen(EntityId observer, EntityId subject) const
    {
        const PerceptionSenses* senses = Entities.TryGetResource<PerceptionSenses>();
        return senses != nullptr ? Frame().Find(observer, senses->Sight, subject) : nullptr;
    }
};
