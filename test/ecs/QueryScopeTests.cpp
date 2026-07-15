#include <gtest/gtest.h>

#include <ecs/Ecs.h>

#include <cstdlib>

struct QueryScopeProbe
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(QueryScopeProbe, "test.query_scope_probe");

#ifndef NDEBUG
TEST(QueryScope, ThrowingCallbackReleasesStructuralMutationGuard)
{
    EXPECT_EXIT(
        {
            World world;
            world.RegisterComponent<QueryScopeProbe>();

            const EntityId entity = world.CreateEntity();
            world.AddComponent(entity, QueryScopeProbe{ 1 });

            Query<Read<QueryScopeProbe>> query(world);
            try
            {
                query.ForEachChunk([](auto&) { throw 7; });
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

TEST(QueryScope, ThrowingWriteCallbackStillPublishesColumnVersion)
{
    World world;
    world.RegisterComponent<QueryScopeProbe>();

    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, QueryScopeProbe{ 1 });
    world.AdvanceFrame();

    Query<Write<QueryScopeProbe>> writer(world);
    bool threw = false;
    try
    {
        writer.ForEachChunk([](auto& view) {
            auto values = view.template Write<QueryScopeProbe>();
            values[0].Value = 9;
            throw 7;
        });
    }
    catch (int)
    {
        threw = true;
    }

    EXPECT_TRUE(threw);
    ASSERT_NE(world.TryGet<QueryScopeProbe>(entity), nullptr);
    EXPECT_EQ(world.TryGet<QueryScopeProbe>(entity)->Value, 9);

    Query<Changed<QueryScopeProbe>> changed(world);
    int matchingChunks = 0;
    changed.ForEachChunk([&](auto&) { ++matchingChunks; }, 0);

    EXPECT_EQ(matchingChunks, 1);
}
