// A ray finds where it enters one shape, and a strike resolves to the hit
// region of its target that the ray enters first.

#include <gtest/gtest.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <physics/HitRegionResolver.h>
#include <physics/PhysicsQueries.h>
#include <world/transform/TransformComponents.h>

#include <numbers>

namespace
{
    Transform3f At(const Vec3d& position, Quatf rotation = Quatf::Identity())
    {
        return Transform3f(position, rotation, Vec3d::One());
    }

    struct RegionWorld
    {
        World Entities;
        GameplayTagRegistry* Tags = nullptr;

        RegionWorld()
        {
            Entities.RegisterComponent<WorldTransform>();
            Entities.RegisterComponent<Parent>();
            Entities.RegisterComponent<HitRegion>();
            Tags = &Entities.AddResource<GameplayTagRegistry>();
        }

        EntityId Body(const Vec3d& position)
        {
            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<WorldTransform>(entity, WorldTransform{ At(position) });
            return entity;
        }

        EntityId Part(EntityId owner, std::string_view region, const CollisionShape& shape, const Vec3d& position,
                      std::int32_t priority = 0)
        {
            (void)Tags->RegisterTag(region);
            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<WorldTransform>(entity, WorldTransform{ At(position) });
            Entities.AddComponent<Parent>(entity, Parent{ owner });
            HitRegion part;
            part.Region = region;
            part.Shape = shape;
            part.Priority = priority;
            Entities.AddComponent<HitRegion>(entity, part);
            return entity;
        }
    };
}

TEST(RaycastShape, FindsWhereARayEntersEachPrimitive)
{
    const Vec3d along(1, 0, 0);
    EXPECT_NEAR(*RaycastShape(CollisionShape::MakeSphere(0.5f), At(Vec3d(5, 0, 0)), Vec3d::Zero(), along, 10.0f), 4.5f, 1e-3f);
    EXPECT_NEAR(*RaycastShape(CollisionShape::MakeCapsule(0.3f, 0.6f), At(Vec3d(3, 0, 0)), Vec3d::Zero(), along, 10.0f),
                2.7f, 1e-3f);
    const Quatf quarter = Quatf::FromAxisAngle(Vec3d::Up(), std::numbers::pi_v<float> / 2.0f);
    EXPECT_NEAR(*RaycastShape(CollisionShape::MakeBox(Vec3d(0.1f, 1, 2)), At(Vec3d(5, 0, 0), quarter), Vec3d::Zero(),
                              along, 10.0f),
                3.0f, 1e-3f)
        << "a box turned a quarter presents its long side";
    EXPECT_FALSE(RaycastShape(CollisionShape::MakeSphere(0.5f), At(Vec3d(5, 2, 0)), Vec3d::Zero(), along, 10.0f));
    EXPECT_FALSE(RaycastShape(CollisionShape::MakeSphere(0.5f), At(Vec3d(5, 0, 0)), Vec3d::Zero(), along, 4.0f));
    EXPECT_NEAR(*RaycastShape(CollisionShape::MakeSphere(1.0f), At(Vec3d::Zero()), Vec3d::Zero(), along, 10.0f), 0.0f, 1e-4f);
}

TEST(HitRegions, TheFirstPartTheRayEntersWins)
{
    RegionWorld world;
    const EntityId soldier = world.Body(Vec3d(10, 0, 0));
    (void)world.Part(soldier, "region.torso", CollisionShape::MakeBox(Vec3d(0.3f, 0.5f, 0.3f)), Vec3d(10, 0, 0));
    (void)world.Part(soldier, "region.head", CollisionShape::MakeSphere(0.2f), Vec3d(10, 0.75f, 0));
    const EntityId stranger = world.Body(Vec3d(20, 0, 0));
    (void)world.Part(stranger, "region.head", CollisionShape::MakeSphere(5.0f), Vec3d(9, 0, 0));

    HitRegionResolver resolver;
    const std::optional<HitRegionHit> torso = resolver.Resolve(world.Entities, soldier, Vec3d(0, 0, 0), Vec3d(1, 0, 0), 20.0f);
    ASSERT_TRUE(torso);
    EXPECT_EQ(torso->Region, world.Tags->FindTag("region.torso"));
    EXPECT_NEAR(torso->Distance, 9.7f, 1e-3f) << "another entity's part is not this one's";

    const std::optional<HitRegionHit> head = resolver.Resolve(world.Entities, soldier, Vec3d(0, 0.75f, 0), Vec3d(1, 0, 0), 20.0f);
    ASSERT_TRUE(head);
    EXPECT_EQ(head->Region, world.Tags->FindTag("region.head"));

    EXPECT_FALSE(resolver.Resolve(world.Entities, soldier, Vec3d(0, 3, 0), Vec3d(1, 0, 0), 20.0f));
}

TEST(HitRegions, OverlappingPartsAtOneDistanceGoToTheHigherPriority)
{
    RegionWorld world;
    const EntityId target = world.Body(Vec3d(5, 0, 0));
    (void)world.Part(target, "region.torso", CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)), Vec3d(5, 0, 0));
    (void)world.Part(target, "region.plate", CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)), Vec3d(5, 0, 0), 3);
    HitRegionResolver resolver;
    const std::optional<HitRegionHit> hit = resolver.Resolve(world.Entities, target, Vec3d::Zero(), Vec3d(1, 0, 0), 10.0f);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->Region, world.Tags->FindTag("region.plate"));
}

TEST(HitRegions, AGrazingRayCountsOnlyWhereItTouches)
{
    RegionWorld world;
    const EntityId target = world.Body(Vec3d(10, 0, 0));
    (void)world.Part(target, "region.head", CollisionShape::MakeSphere(0.2f), Vec3d(10, 0.75f, 0));
    HitRegionResolver resolver;
    const std::optional<HitRegionHit> grazed =
        resolver.Resolve(world.Entities, target, Vec3d(0, 0.94f, 0), Vec3d(1, 0, 0), 20.0f);
    ASSERT_TRUE(grazed) << "a ray just inside the head's edge still enters it";
    EXPECT_EQ(grazed->Region, world.Tags->FindTag("region.head"));
    EXPECT_GT(grazed->Distance, 9.8f);
    EXPECT_LT(grazed->Distance, 10.0f);
    EXPECT_FALSE(resolver.Resolve(world.Entities, target, Vec3d(0, 0.96f, 0), Vec3d(1, 0, 0), 20.0f))
        << "just outside it, the ray misses";
}

TEST(HitRegions, AStrikeOnATargetWithNoPartsLandsOnNone)
{
    RegionWorld world;
    const EntityId bare = world.Body(Vec3d(5, 0, 0));
    HitRegionResolver resolver;
    EXPECT_FALSE(resolver.Resolve(world.Entities, bare, Vec3d::Zero(), Vec3d(1, 0, 0), 10.0f));
    const EntityId inside = world.Body(Vec3d(0, 0, 0));
    (void)world.Part(inside, "region.torso", CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)), Vec3d::Zero());
    const std::optional<HitRegionHit> point = resolver.Resolve(world.Entities, inside, Vec3d::Zero(), Vec3d(1, 0, 0), 10.0f);
    ASSERT_TRUE(point) << "a strike that starts inside a part lands on it";
    EXPECT_NEAR(point->Distance, 0.0f, 1e-4f);
}
