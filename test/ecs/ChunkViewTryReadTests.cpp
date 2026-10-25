#include <ecs/Ecs.h>

#include <gtest/gtest.h>

struct TryReadBase
{
    int Value = 0;
};
struct TryReadOptional
{
    int Extra = 0;
};
struct TryReadNeverRegistered
{
    int Unused = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(TryReadBase, "test.try_read_base");
SENCHA_DECLARE_COMPONENT_TYPE(TryReadOptional, "test.try_read_optional");
SENCHA_DECLARE_COMPONENT_TYPE(TryReadNeverRegistered, "test.try_read_never_registered");

TEST(ChunkViewTryRead, ReadsAColumnWhereTheChunkHasItAndNothingElsewhere)
{
    World world;
    world.RegisterComponent<TryReadBase>();
    world.RegisterComponent<TryReadOptional>();
    const EntityId plain = world.CreateEntity();
    world.AddComponent<TryReadBase>(plain, { 1 });
    const EntityId extended = world.CreateEntity();
    world.AddComponent<TryReadBase>(extended, { 2 });
    world.AddComponent<TryReadOptional>(extended, { 20 });

    Query<Read<TryReadBase>> query(world);
    int chunks = 0;
    int extras = 0;
    query.ForEachChunk([&](auto& view) {
        ++chunks;
        const auto base = view.template Read<TryReadBase>();
        const auto optional = view.template TryRead<TryReadOptional>();
        EXPECT_TRUE(view.template TryRead<TryReadNeverRegistered>().empty());
        if (optional.empty())
            return;
        ASSERT_EQ(optional.size(), view.Count());
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            EXPECT_EQ(base[i].Value, 2);
            extras += optional[i].Extra;
        }
    });
    EXPECT_EQ(chunks, 2) << "one chunk per archetype, the query filtering neither";
    EXPECT_EQ(extras, 20);
}
