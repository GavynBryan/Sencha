#include <gtest/gtest.h>

#include <ecs/Ecs.h>

#include <cstdlib>

struct WorldIterationProbe
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(WorldIterationProbe, "test.world_iteration_probe");

#ifndef NDEBUG
TEST(WorldIteration, MutableForEachRejectsStructuralMutation)
{
    World world;
    world.RegisterComponent<WorldIterationProbe>();

    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, WorldIterationProbe{ 1 });

    EXPECT_DEATH(
        world.ForEachComponent<WorldIterationProbe>(
            [&](EntityId, WorldIterationProbe&) { world.CreateEntity(); }),
        "active");
}

TEST(WorldIteration, ConstForEachRejectsStructuralMutation)
{
    World world;
    world.RegisterComponent<WorldIterationProbe>();

    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, WorldIterationProbe{ 1 });
    const World& readWorld = world;

    EXPECT_DEATH(
        readWorld.ForEachComponent<WorldIterationProbe>(
            [&](EntityId, const WorldIterationProbe&) { world.CreateEntity(); }),
        "active");
}

TEST(WorldIteration, ThrowingCallbackReleasesStructuralMutationGuard)
{
    EXPECT_EXIT(
        {
            World world;
            world.RegisterComponent<WorldIterationProbe>();

            const EntityId entity = world.CreateEntity();
            world.AddComponent(entity, WorldIterationProbe{ 1 });

            try
            {
                world.ForEachComponent<WorldIterationProbe>(
                    [](EntityId, WorldIterationProbe&) { throw 7; });
            }
            catch (int)
            {
            }

            world.CreateEntity();
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0),
        ".*");
}

TEST(WorldResources, DuplicateRegistrationAsserts)
{
    World world;
    world.AddResource<int>(1);

    EXPECT_DEATH(world.AddResource<int>(2), "Duplicate World resource registration");
}
#endif

TEST(WorldIteration, ThrowingMutableCallbackStillPublishesColumnVersion)
{
    World world;
    world.RegisterComponent<WorldIterationProbe>();

    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, WorldIterationProbe{ 1 });
    world.AdvanceFrame();

    bool threw = false;
    try
    {
        world.ForEachComponent<WorldIterationProbe>(
            [](EntityId, WorldIterationProbe& value) {
                value.Value = 9;
                throw 7;
            });
    }
    catch (int)
    {
        threw = true;
    }

    EXPECT_TRUE(threw);
    const World& readWorld = world;
    ASSERT_NE(readWorld.TryGet<WorldIterationProbe>(entity), nullptr);
    EXPECT_EQ(readWorld.TryGet<WorldIterationProbe>(entity)->Value, 9);

    Query<Changed<WorldIterationProbe>> changed(readWorld);
    int matchingChunks = 0;
    changed.ForEachChunk([&](auto&) { ++matchingChunks; }, 0);

    EXPECT_EQ(matchingChunks, 1);
}
