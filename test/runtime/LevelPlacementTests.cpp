#include "AllocationCounter.h"

#include <abilities/AbilityGrants.h>
#include <abilities/AbilityKit.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/BaselineGameplayTags.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZoneStateLifetime.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr ZoneParticipation kPlayed{ .Visible = false, .Physics = true, .Logic = true, .Audio = false };
    constexpr ZoneId kRoom{ 0x61 };
    const LevelReference kLevel{ .Kind = LevelKind::World, .Name = "warehouse" };
    constexpr PersistentEntityId kFloor{ 0x100 };
    constexpr PersistentEntityId kCrate{ 0x101 };
    constexpr PersistentEntityId kLift{ 0x102 };
    constexpr PersistentEntityId kWall{ 0x103 };
    constexpr PersistentEntityId kStatue{ 0x104 };
    constexpr PersistentEntityId kDebris{ 0x105 };
    constexpr PersistentEntityId kGuard{ 0x106 };
    constexpr PersistentEntityId kSign{ 0x107 };
    constexpr float kDt = 1.0f / 60.0f;

    WorldComponentSchema EngineSchema(ComponentSerializerRegistry& serializers)
    {
        WorldComponentSchema schema;
        ComponentRegistrar registrar(&schema, &serializers, nullptr);
        RegisterEngineComponents(registrar);
        schema.Seal();
        return schema;
    }

    std::string Placed(float x, float y, float z)
    {
        return R"({"local": {"position": [)" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z)
            + R"(], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}})";
    }

    class LevelPlacement : public ::testing::Test
    {
    protected:
        explicit LevelPlacement(bool keepsLevelState = true)
            : Context(Logging)
            , Schema(EngineSchema(Serializers))
            , Runtime(Schema)
            , Bodies(Physics)
        {
            RegisterAbilityKit(Entities());
            if (keepsLevelState)
                Entities().SetResource(ZoneStateSerialization{ .Serializers = &Serializers, .Context = &Context });
        }

        World& Entities() { return Runtime.Entities(); }
        ZoneStateStore& Store() { return Entities().GetResource<ZoneStateStore>(); }
        ZoneStateScope Scope() { return Store().Scope(kLevel, kRoom); }

        static PackageEntityId Add(EntityBuildPackage& package, PersistentEntityId id, std::string_view transform)
        {
            const PackageEntityId entity = package.CreateEntity();
            EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(entity, { id }));
            EXPECT_TRUE(package.SetPersistentId(entity, id));
            Author(package, entity, ResolveComponentTypeId<LocalTransform>(), transform);
            return entity;
        }

        static void Author(EntityBuildPackage& package, PackageEntityId entity, ComponentTypeId type,
                           std::string_view record)
        {
            EXPECT_TRUE(package.SetSerializedComponent(entity, type, *JsonParse(record)));
        }

        static void Box(EntityBuildPackage& package, PackageEntityId entity, float half,
                        std::optional<std::string_view> motion = {})
        {
            Author(package, entity, ResolveComponentTypeId<Collider>(),
                   R"({"shape": {"type": "box", "half_extents": [)" + std::to_string(half) + ", 0.5, "
                       + std::to_string(half) + "]}}");
            if (motion.has_value())
                Author(package, entity, ResolveComponentTypeId<RigidBody>(),
                       R"({"motion": ")" + std::string(*motion) + R"("})");
        }

        static EntityBuildPackage Warehouse()
        {
            EntityBuildPackage package;
            Box(package, Add(package, kFloor, Placed(0.0f, -0.5f, 0.0f)), 20.0f);
            Box(package, Add(package, kCrate, Placed(0.0f, 0.5f, 0.0f)), 0.5f, "dynamic");
            Box(package, Add(package, kLift, Placed(5.0f, 0.5f, 0.0f)), 1.0f, "kinematic");
            Box(package, Add(package, kWall, Placed(-5.0f, 0.5f, 0.0f)), 1.0f);
            return package;
        }

        void FinishResidency()
        {
            Runtime.FlushLifecycleRequests();
            (void)Runtime.BeginResidencyProcessing();
            Runtime.FinalizeResidencyProcessing();
        }

        void Import(const EntityBuildPackage& package)
        {
            ZoneImportError error;
            ASSERT_TRUE(ImportZonePackage(Runtime, Schema, kRoom, package, Serializers, Context, kPlayed, &error,
                                          Scope()))
                << error.Message;
            FinishResidency();
        }

        void Detach()
        {
            Bodies.EvictPartition(Entities(), Runtime.FindZone(kRoom)->Partition);
            ASSERT_TRUE(Runtime.RequestDetach(kRoom));
            FinishResidency();
        }

        void Capture()
        {
            CaptureZoneState(Entities(), Scope(), IdentifiedEntitiesIn(Entities(), Runtime.FindZone(kRoom)->Partition),
                             ZoneCapture::Live);
        }

        void Tick(int steps)
        {
            StoragePartitionSet active;
            active.Add(Runtime.FindZone(kRoom)->Partition);
            for (int step = 0; step < steps; ++step)
            {
                Bodies.SyncToPhysics(Entities(), active);
                Physics.Step(kDt);
                Bodies.SyncFromPhysics(Entities(), active);
            }
        }

        EntityId Resolve(PersistentEntityId id) { return Entities().GetResource<PersistentEntityIndex>().TryResolve(id); }

        bool BaselineHolds(PersistentEntityId id, ComponentTypeId type)
        {
            const std::vector<AuthoredComponent>* baseline = Store().BaselineOf(Scope(), id);
            return baseline != nullptr
                && std::ranges::any_of(*baseline, [&](const AuthoredComponent& component) { return component.Type == type; });
        }

        bool BaselineHoldsTransform(PersistentEntityId id)
        {
            return BaselineHolds(id, ResolveComponentTypeId<LocalTransform>());
        }

        std::string PatchText(PersistentEntityId id)
        {
            const JsonValue* patch = Store().PatchOf(Scope(), id);
            return patch != nullptr ? JsonStringify(*patch) : std::string();
        }

        LoggingProvider Logging;
        SceneSerializationContext Context;
        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
        PhysicsWorld Physics;
        RigidBodyBinding Bodies;
    };

    class LevelPlacementWithoutLevelState : public LevelPlacement
    {
    protected:
        LevelPlacementWithoutLevelState()
            : LevelPlacement(false)
        {
        }
    };
}

TEST_F(LevelPlacement, ADynamicBodysTransformIsInItsBaselineAndAKinematicOnesAndAStaticEntitysAreNot)
{
    Import(Warehouse());
    EXPECT_TRUE(BaselineHoldsTransform(kCrate)) << "a dynamic body's place is not kept";
    EXPECT_FALSE(BaselineHoldsTransform(kLift)) << "a kinematic body's transform is derived, not kept";
    EXPECT_FALSE(BaselineHoldsTransform(kWall));
    EXPECT_FALSE(BaselineHoldsTransform(kFloor));
}

TEST_F(LevelPlacement, KeptAndAuthoredEachOverrideWhatTheComponentsSay)
{
    EntityBuildPackage package = Warehouse();
    const PackageEntityId statue = Add(package, kStatue, Placed(-8.0f, 0.5f, 3.0f));
    EXPECT_TRUE(package.AddComponent<ZoneStateLifetime>(
        statue, ZoneStateLifetime{ .Forgotten = ZoneStateForgetting::Never, .Placement = ZoneStatePlacement::Kept }));
    const PackageEntityId debris = Add(package, kDebris, Placed(8.0f, 0.5f, 3.0f));
    Box(package, debris, 0.25f, "dynamic");
    EXPECT_TRUE(package.AddComponent<ZoneStateLifetime>(
        debris, ZoneStateLifetime{ .Forgotten = ZoneStateForgetting::Never, .Placement = ZoneStatePlacement::Authored }));
    Import(package);
    EXPECT_TRUE(BaselineHoldsTransform(kStatue)) << "'kept' did not keep a transform nothing moves";
    EXPECT_FALSE(BaselineHoldsTransform(kDebris)) << "'authored' did not leave a dynamic body's place to content";

    Entities().TryGet<LocalTransform>(Resolve(kStatue))->Value.Position = Vec3d(-8.0f, 0.5f, 9.0f);
    Capture();
    Detach();
    Import(package);
    EXPECT_FLOAT_EQ(Entities().TryGet<LocalTransform>(Resolve(kStatue))->Value.Position.Z, 9.0f) << PatchText(kStatue);
}

TEST_F(LevelPlacement, AMovedCrateIsWhereItWasLeftAfterItsZonesRoundTripMovingAsItWas)
{
    Import(Warehouse());
    const EntityId crate = Resolve(kCrate);
    Entities().TryGet<LocalTransform>(crate)->Value.Position = Vec3d(2.0f, 3.0f, -1.0f);
    Entities().TryGet<RigidBody>(crate)->LinearVelocity = Vec3d(0.0f, 4.0f, 0.0f);
    Entities().TryGet<RigidBody>(crate)->Asleep = false;
    Detach();

    Import(Warehouse());
    const EntityId back = Resolve(kCrate);
    ASSERT_TRUE(back.IsValid());
    EXPECT_EQ(Entities().TryGet<LocalTransform>(back)->Value.Position, Vec3d(2.0f, 3.0f, -1.0f)) << PatchText(kCrate);
    EXPECT_EQ(Entities().TryGet<RigidBody>(back)->LinearVelocity, Vec3d(0.0f, 4.0f, 0.0f));
    Tick(1);
    EXPECT_GT(Entities().TryGet<LocalTransform>(back)->Value.Position.Y, 3.0f) << "the crate came back not rising";
}

TEST_F(LevelPlacement, ACodeAddedBodyAtItsDefaultsWritesNothing)
{
    EntityBuildPackage package = Warehouse();
    Add(package, kSign, Placed(3.0f, 2.0f, 3.0f));
    Import(package);
    Entities().AddComponent<RigidBody>(Resolve(kSign), RigidBody{});
    Capture();
    EXPECT_EQ(Store().PatchOf(Scope(), kSign), nullptr) << PatchText(kSign);
}

TEST_F(LevelPlacement, AnOwedSavedComponentAtItsDefaultIsInTheBaselineAndWritesNothing)
{
    EntityBuildPackage package = Warehouse();
    const PackageEntityId guard = Add(package, kGuard, Placed(3.0f, 0.5f, -3.0f));
    Author(package, guard, ResolveComponentTypeId<AbilityGrants>(), R"({"grants": []})");
    Import(package);
    ASSERT_TRUE(Entities().HasComponent<GameplayTagContainer>(Resolve(kGuard)));
    EXPECT_TRUE(BaselineHolds(kGuard, ResolveComponentTypeId<GameplayTagContainer>()))
        << "the importer did not put the owed component's default into the baseline";
    Capture();
    EXPECT_EQ(Store().PatchOf(Scope(), kGuard), nullptr) << PatchText(kGuard);

    const GameplayTagId alert = *Entities().GetResource<GameplayTagRegistry>().RegisterTag("guard.alert");
    ASSERT_EQ(GrantBaselineTag(Entities(), Resolve(kGuard), alert), GameplayTagWrite::Added);
    Detach();
    Import(package);
    const GameplayTagContainer* tags = Entities().TryGet<GameplayTagContainer>(Resolve(kGuard));
    ASSERT_NE(tags, nullptr);
    EXPECT_TRUE(tags->HasExact(alert)) << PatchText(kGuard);
}

TEST_F(LevelPlacement, AnEntityForgottenAtDetachHasNoBaselineAndIsNotDiffed)
{
    EntityBuildPackage package = Warehouse();
    const PackageEntityId debris = Add(package, kDebris, Placed(8.0f, 0.5f, 3.0f));
    Box(package, debris, 0.25f, "dynamic");
    EXPECT_TRUE(package.AddComponent<ZoneStateLifetime>(debris, ZoneStateLifetime{ .Forgotten = ZoneStateForgetting::AtDetach }));
    Import(package);
    EXPECT_EQ(Store().BaselineOf(Scope(), kDebris), nullptr) << "an entity forgotten at detach recorded a baseline";
    Entities().TryGet<LocalTransform>(Resolve(kDebris))->Value.Position = Vec3d(8.0f, 4.0f, 3.0f);
    Entities().TryGet<RigidBody>(Resolve(kDebris))->GravityScale = 0.5f;
    Capture();
    EXPECT_EQ(Store().PatchOf(Scope(), kDebris), nullptr) << PatchText(kDebris);
}

TEST_F(LevelPlacement, ADetachTheLevelChangeForgetsRecordsNothing)
{
    Import(Warehouse());
    Entities().DestroyEntity(Resolve(kWall));
    Runtime.FindZone(kRoom)->CapturedAtDetach = false;
    Detach();
    EXPECT_FALSE(Store().IsRecordedDestroyed(Scope(), kWall)) << "a detach of a level about to be forgotten was captured";

    Import(Warehouse());
    Entities().DestroyEntity(Resolve(kWall));
    Detach();
    EXPECT_TRUE(Store().IsRecordedDestroyed(Scope(), kWall));
}

TEST_F(LevelPlacementWithoutLevelState, AGameThatDoesNotKeepLevelStateRecordsNoBaselineAndAllocatesNothingForPlacement)
{
    EntityBuildPackage still;
    EntityBuildPackage moving;
    for (std::uint64_t index = 0; index < 64; ++index)
    {
        const float x = static_cast<float>(index) * 2.0f;
        Box(still, Add(still, PersistentEntityId{ 0x200 + index }, Placed(x, 0.5f, 0.0f)), 0.5f);
        Box(moving, Add(moving, PersistentEntityId{ 0x200 + index }, Placed(x, 0.5f, 0.0f)), 0.5f, "dynamic");
    }
    const auto captureAllocations = [&](const EntityBuildPackage& package) {
        Import(package);
        const std::vector<EntityId> identified = IdentifiedEntitiesIn(Entities(), Runtime.FindZone(kRoom)->Partition);
        const std::size_t before = AllocationCount();
        CaptureZoneState(Entities(), Scope(), identified, ZoneCapture::Live);
        const std::size_t made = AllocationCount() - before;
        for (std::uint64_t index = 0; index < 64; ++index)
        {
            const std::vector<AuthoredComponent>* baseline = Store().BaselineOf(Scope(), PersistentEntityId{ 0x200 + index });
            EXPECT_TRUE(baseline == nullptr || baseline->capacity() == 0) << "a game that keeps no level state kept a baseline";
        }
        Detach();
        return made;
    };
    const std::size_t forStill = captureAllocations(still);
    const std::size_t forMoving = captureAllocations(moving);
    EXPECT_EQ(forMoving, forStill) << "dynamic bodies cost a game that keeps no level state allocations at capture";
}
