// A scene instance as a unit: its single root, its root records, and the
// destruction of every member at once.

#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SceneInstanceGroup.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/scene/SmapFormat.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
    struct GroupWorld
    {
        GroupWorld()
            : Schema([] {
                WorldComponentSchema schema;
                schema.Add<LocalTransform>();
                schema.Add<WorldTransform>();
                schema.Add<Parent>();
                schema.Add<SceneInstance>();
                schema.Seal();
                return schema;
            }())
            , Runtime(Schema)
        {
        }

        EntityId Member(SceneInstanceId group, EntityId parent = {})
        {
            World& world = Runtime.Entities();
            const EntityId entity = world.CreateEntity();
            world.AddComponent<SceneInstance>(entity, SceneInstance{ {}, group });
            if (parent.IsValid())
                world.AddComponent<Parent>(entity, Parent{ parent });
            return entity;
        }

        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
    };

    constexpr SceneInstanceId kGroup{ SceneInstanceIdRuntimeBit | 1u };
    constexpr SceneInstanceId kOtherGroup{ SceneInstanceIdRuntimeBit | 2u };
}

TEST(SceneInstanceGroup, SingleRootIsTheOneLivingParentlessMember)
{
    GroupWorld g;
    World& world = g.Runtime.Entities();
    const EntityId root = g.Member(kGroup);
    const EntityId child = g.Member(kGroup, root);
    const EntityId second = g.Member(kGroup);
    const EntityId dead = g.Member(kGroup);
    world.DestroyEntity(dead);

    struct Case
    {
        const char* Name;
        std::vector<EntityId> Members;
        EntityId Expected;
    };
    const Case cases[] = {
        { "empty", {}, EntityId{} },
        { "only children", { child }, EntityId{} },
        { "one root", { root, child }, root },
        { "two roots", { root, child, second }, EntityId{} },
        { "dead members ignored", { dead, child, root }, root },
    };
    for (const Case& c : cases)
        EXPECT_EQ(SingleRoot(world, c.Members), c.Expected) << c.Name;
}

TEST(SceneInstanceGroup, CountRootRecordsCountsRecordsWithoutAParent)
{
    const auto contents = [](std::vector<std::uint32_t> parents) {
        SmapContents result;
        for (const std::uint32_t parent : parents)
        {
            SmapEntityRecord record;
            record.Parent = parent;
            result.Entities.push_back(std::move(record));
        }
        return result;
    };

    EXPECT_EQ(CountRootRecords(contents({})), 0u);
    EXPECT_EQ(CountRootRecords(contents({ UINT32_MAX, 0 })), 1u);
    EXPECT_EQ(CountRootRecords(contents({ UINT32_MAX, 0, UINT32_MAX })), 2u);
}

TEST(SceneInstanceGroup, DestroyingAnInstanceTakesEveryMemberAndNoOtherGroup)
{
    GroupWorld g;
    World& world = g.Runtime.Entities();
    const EntityId root = g.Member(kGroup);
    const EntityId child = g.Member(kGroup, root);
    const EntityId alreadyGone = g.Member(kGroup, root);
    world.DestroyEntity(alreadyGone);
    const EntityId bystander = g.Member(kOtherGroup);

    DestroySceneInstance(world, kGroup);

    EXPECT_FALSE(world.IsAlive(root));
    EXPECT_FALSE(world.IsAlive(child));
    EXPECT_TRUE(world.IsAlive(bystander));
    EXPECT_TRUE(world.GetResource<SceneInstanceIndex>().Entities(kGroup).empty());

    DestroySceneInstance(world, kGroup);
    EXPECT_TRUE(world.IsAlive(bystander));
}

// Every spawner mints from one sequence. Two counters that each started at one
// gave the first projectile the pawn's group, and despawning it took the pawn.
TEST(SceneInstanceGroup, RuntimeInstanceIdsAreNeverReissued)
{
    const SceneInstanceId first = MintRuntimeSceneInstanceId();
    const SceneInstanceId second = MintRuntimeSceneInstanceId();
    EXPECT_TRUE(first.IsValid());
    EXPECT_NE(first, second);
    EXPECT_NE(first.Value & SceneInstanceIdRuntimeBit, 0u);
    EXPECT_NE(second.Value & SceneInstanceIdRuntimeBit, 0u);
}
