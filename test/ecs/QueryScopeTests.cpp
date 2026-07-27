// A query callback is game code, so it can throw. What the ECS owes in that
// case is that its own bookkeeping survives: the structural-mutation guard is
// released, and writes the callback completed stay visible to change detection.

#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <stdexcept>

struct QueryScopeProbe
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(QueryScopeProbe, "test.query_scope_probe");

namespace
{
class QueryScopeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        World_.RegisterComponent<QueryScopeProbe>();
        Entity_ = World_.CreateEntity();
        World_.AddComponent(Entity_, QueryScopeProbe{ 1 });
    }

    World    World_;
    EntityId Entity_;
};
} // namespace

TEST_F(QueryScopeTest, ThrowingCallbackReleasesTheScope)
{
    Query<Read<QueryScopeProbe>> query(World_);

    EXPECT_THROW(
        query.ForEachChunk([](auto&) { throw std::runtime_error("callback"); }),
        std::runtime_error);

    EXPECT_FALSE(World_.InQueryScope());
}

TEST_F(QueryScopeTest, StructuralMutationIsLegalAfterAThrowingCallback)
{
    Query<Read<QueryScopeProbe>> query(World_);

    EXPECT_THROW(
        query.ForEachChunk([](auto&) { throw std::runtime_error("callback"); }),
        std::runtime_error);

    const EntityId created = World_.CreateEntity();
    World_.AddComponent(created, QueryScopeProbe{ 2 });

    EXPECT_TRUE(World_.IsAlive(created));
    EXPECT_EQ(World_.EntityCount(), 2u);
}

TEST_F(QueryScopeTest, NestedQueriesReleaseOneScopeEach)
{
    Query<Read<QueryScopeProbe>> outer(World_);
    Query<Read<QueryScopeProbe>> inner(World_);

    outer.ForEachChunk([&](auto&) {
        EXPECT_TRUE(World_.InQueryScope());
        inner.ForEachChunk([&](auto&) { EXPECT_TRUE(World_.InQueryScope()); });
        EXPECT_TRUE(World_.InQueryScope());
    });

    EXPECT_FALSE(World_.InQueryScope());
}

TEST_F(QueryScopeTest, ThrowingCallbackPublishesTheWriteItCompleted)
{
    World_.AdvanceFrame();

    Query<Write<QueryScopeProbe>> writer(World_);
    EXPECT_THROW(
        writer.ForEachChunk([](auto& view) {
            view.template Write<QueryScopeProbe>()[0].Value = 42;
            throw std::runtime_error("callback");
        }),
        std::runtime_error);

    Query<Read<QueryScopeProbe>> reader(World_);
    reader.ForEachChunk([](auto& view) {
        EXPECT_EQ(view.template Read<QueryScopeProbe>()[0].Value, 42);
    });

    Query<Changed<QueryScopeProbe>> changed(World_);
    int matchingChunks = 0;
    changed.ForEachChunk(
        [&](auto&) { ++matchingChunks; },
        World_.CurrentFrame() - 1);

    EXPECT_EQ(matchingChunks, 1);
}
