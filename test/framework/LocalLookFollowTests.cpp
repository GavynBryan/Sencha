#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <participant/LocalControl.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>

#include "LocalLookFollow.h"

//=============================================================================
// The tag follows the subject. The engine says which entity this machine
// drives and nothing else; whether that entity takes this machine's look input
// is this game's rule, and this is the whole of it.
//=============================================================================
namespace
{
    struct TagWorld
    {
        WorldComponentSchema Schema;
        World Entities;

        TagWorld()
        {
            ComponentRegistrar components(&Schema, nullptr, nullptr);
            RegisterEngineComponents(components);
            Schema.Seal();
            Schema.Apply(Entities);
        }

        EntityId Body()
        {
            const EntityId body = Entities.CreateEntity();
            Entities.AddComponent<LookOrientation>(body, {});
            return body;
        }
    };
}

TEST(LocalLookFollow, TheTagMovesWithTheSubjectWithinOneCall)
{
    TagWorld world;
    const EntityId first = world.Body();
    const EntityId second = world.Body();
    EntityId tagged;

    (void)SetLocalControlSubject(world.Entities, first);
    EXPECT_TRUE(FollowLocalLookControl(world.Entities, tagged));
    EXPECT_TRUE(world.Entities.HasComponent<LocalLookControl>(first));
    EXPECT_EQ(tagged, first);

    (void)SetLocalControlSubject(world.Entities, second);
    EXPECT_TRUE(FollowLocalLookControl(world.Entities, tagged));
    EXPECT_FALSE(world.Entities.HasComponent<LocalLookControl>(first));
    EXPECT_TRUE(world.Entities.HasComponent<LocalLookControl>(second));
    EXPECT_EQ(tagged, second);
}

TEST(LocalLookFollow, NoSubjectMeansNoTagAnywhere)
{
    TagWorld world;
    const EntityId body = world.Body();
    EntityId tagged;

    (void)SetLocalControlSubject(world.Entities, body);
    (void)FollowLocalLookControl(world.Entities, tagged);
    (void)SetLocalControlSubject(world.Entities, EntityId{});
    EXPECT_TRUE(FollowLocalLookControl(world.Entities, tagged));

    EXPECT_FALSE(world.Entities.HasComponent<LocalLookControl>(body));
    EXPECT_FALSE(tagged.IsValid());
}

TEST(LocalLookFollow, AnUnchangedSubjectIsNotAnEdge)
{
    TagWorld world;
    const EntityId body = world.Body();
    EntityId tagged;

    (void)SetLocalControlSubject(world.Entities, body);
    ASSERT_TRUE(FollowLocalLookControl(world.Entities, tagged));
    EXPECT_FALSE(FollowLocalLookControl(world.Entities, tagged));
}

// The body was reaped before the follower ran. Removing from a dead entity is
// not attempted, and the tag lands on whatever the subject is now.
TEST(LocalLookFollow, ADeadPreviousBodyIsSkipped)
{
    TagWorld world;
    const EntityId first = world.Body();
    EntityId tagged;

    (void)SetLocalControlSubject(world.Entities, first);
    (void)FollowLocalLookControl(world.Entities, tagged);
    world.Entities.DestroyEntity(first);
    (void)SetLocalControlSubject(world.Entities, EntityId{});

    EXPECT_TRUE(FollowLocalLookControl(world.Entities, tagged));
    EXPECT_FALSE(tagged.IsValid());
}
