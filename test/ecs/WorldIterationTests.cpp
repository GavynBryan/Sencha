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
    EntityStore world;
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
    EntityStore world;
    world.RegisterComponent<WorldIterationProbe>();

    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, WorldIterationProbe{ 1 });
    const EntityStore& readWorld = world;

    EXPECT_DEATH(
        readWorld.ForEachComponent<WorldIterationProbe>(
            [&](EntityId, const WorldIterationProbe&) { world.CreateEntity(); }),
        "active");
}

TEST(WorldIteration, ThrowingCallbackReleasesStructuralMutationGuard)
{
    EXPECT_EXIT(
        {
            EntityStore world;
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

#endif

TEST(WorldIteration, ThrowingMutableCallbackStillPublishesColumnVersion)
{
    EntityStore world;
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
    const EntityStore& readWorld = world;
    ASSERT_NE(readWorld.TryGet<WorldIterationProbe>(entity), nullptr);
    EXPECT_EQ(readWorld.TryGet<WorldIterationProbe>(entity)->Value, 9);

    Query<Changed<WorldIterationProbe>> changed(readWorld);
    int matchingChunks = 0;
    changed.ForEachChunk([&](auto&) { ++matchingChunks; }, 0);

    EXPECT_EQ(matchingChunks, 1);
}
