#include <ecs/CommandBuffer.h>
#include <ecs/FieldDelta.h>
#include <ecs/TransitionDispatch.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // Two int fields, so a field offset other than 0 is exercised.
    struct Pair
    {
        std::int32_t a;
        std::int32_t b;
    };

    struct SetRec
    {
        std::vector<std::string> events;
    };

    void OnSetCb(World&, EntityId, ComponentId, std::uint16_t offset,
                 const void* oldBytes, const void* newBytes, std::uint16_t size, void* ctx)
    {
        auto* r = static_cast<SetRec*>(ctx);
        std::int32_t oldV = 0, newV = 0;
        std::memcpy(&oldV, oldBytes, sizeof(std::int32_t));
        std::memcpy(&newV, newBytes, sizeof(std::int32_t));
        r->events.push_back("set@" + std::to_string(offset) + ":" + std::to_string(oldV)
                            + "->" + std::to_string(newV) + ":sz" + std::to_string(size));
    }

    ComponentId RegisterPair(World& w)
    {
        return w.RegisterComponentRaw("Pair", MakeComponentTypeId("test.fieldop_pair"),
                                      sizeof(Pair), alignof(Pair), false);
    }

    Pair ReadPair(World& w, EntityId e, ComponentId id)
    {
        Pair p{};
        std::memcpy(&p, w.GetComponentRaw(e, id), sizeof(Pair));
        return p;
    }
}

TEST(FieldOp, SetFieldWritesLeafAndFiresOnSetWithOldNew)
{
    World world;
    SetRec rec;
    const ComponentId id = RegisterPair(world);
    world.RegisterTransitionDispatch(id, TransitionDispatch{ nullptr, nullptr, &OnSetCb, &rec });

    const EntityId e = world.CreateEntity();
    const Pair init{ 10, 20 };
    world.AddComponentRaw(e, id, &init, sizeof(Pair), alignof(Pair), nullptr);

    CommandBuffer cmd(world);
    const std::int32_t newB = 99;
    cmd.SetFieldRaw(e, id, offsetof(Pair, b), &newB, sizeof(std::int32_t));
    cmd.Flush();

    EXPECT_EQ(ReadPair(world, e, id).a, 10); // untouched field
    EXPECT_EQ(ReadPair(world, e, id).b, 99);
    ASSERT_EQ(rec.events.size(), 1u);
    EXPECT_EQ(rec.events[0], "set@4:20->99:sz4");
}

TEST(FieldOp, SetToSameValueDoesNotFireOnSet)
{
    World world;
    SetRec rec;
    const ComponentId id = RegisterPair(world);
    world.RegisterTransitionDispatch(id, TransitionDispatch{ nullptr, nullptr, &OnSetCb, &rec });

    const EntityId e = world.CreateEntity();
    const Pair init{ 10, 20 };
    world.AddComponentRaw(e, id, &init, sizeof(Pair), alignof(Pair), nullptr);

    CommandBuffer cmd(world);
    const std::int32_t sameB = 20;
    cmd.SetFieldRaw(e, id, offsetof(Pair, b), &sameB, sizeof(std::int32_t));
    cmd.Flush();

    EXPECT_EQ(ReadPair(world, e, id).b, 20);
    EXPECT_TRUE(rec.events.empty()); // bytes unchanged: on_set suppressed
}

TEST(FieldOp, DeltaFieldWrapsIntegersAndFiresOnSet)
{
    World world;
    SetRec rec;
    const ComponentId id = RegisterPair(world);
    world.RegisterTransitionDispatch(id, TransitionDispatch{ nullptr, nullptr, &OnSetCb, &rec });

    const EntityId e = world.CreateEntity();
    const Pair init{ 5, 0 };
    world.AddComponentRaw(e, id, &init, sizeof(Pair), alignof(Pair), nullptr);

    CommandBuffer cmd(world);
    const std::int32_t d = 7;
    cmd.DeltaFieldRaw(e, id, offsetof(Pair, a), NumericFieldKind::Int32, &d, sizeof(std::int32_t));
    cmd.Flush();

    EXPECT_EQ(ReadPair(world, e, id).a, 12);
    ASSERT_EQ(rec.events.size(), 1u);
    EXPECT_EQ(rec.events[0], "set@0:5->12:sz4");
}

TEST(FieldOp, IntegerDeltaIsOrderIndependentAcrossSources)
{
    World world;
    const ComponentId id = RegisterPair(world);
    // No observer: pure arithmetic. Applying +A then +B, and +B then +A, on two
    // entities from a common base reaches the same wrapping-integer result.
    const Pair base{ 100, 0 };
    const EntityId e1 = world.CreateEntity();
    const EntityId e2 = world.CreateEntity();
    world.AddComponentRaw(e1, id, &base, sizeof(Pair), alignof(Pair), nullptr);
    world.AddComponentRaw(e2, id, &base, sizeof(Pair), alignof(Pair), nullptr);

    const std::int32_t da = 40, db = 7;
    {
        CommandBuffer cmd(world);
        cmd.DeltaFieldRaw(e1, id, offsetof(Pair, a), NumericFieldKind::Int32, &da, sizeof(da));
        cmd.DeltaFieldRaw(e1, id, offsetof(Pair, a), NumericFieldKind::Int32, &db, sizeof(db));
        cmd.Flush();
    }
    {
        CommandBuffer cmd(world);
        cmd.DeltaFieldRaw(e2, id, offsetof(Pair, a), NumericFieldKind::Int32, &db, sizeof(db));
        cmd.DeltaFieldRaw(e2, id, offsetof(Pair, a), NumericFieldKind::Int32, &da, sizeof(da));
        cmd.Flush();
    }
    EXPECT_EQ(ReadPair(world, e1, id).a, 147);
    EXPECT_EQ(ReadPair(world, e2, id).a, 147);
}

TEST(FieldOp, InertWhenNoOnSetRegistered)
{
    World world;
    const ComponentId id = RegisterPair(world);
    const EntityId e = world.CreateEntity();
    const Pair init{ 1, 2 };
    world.AddComponentRaw(e, id, &init, sizeof(Pair), alignof(Pair), nullptr);

    CommandBuffer cmd(world);
    const std::int32_t v = 42;
    cmd.SetFieldRaw(e, id, offsetof(Pair, a), &v, sizeof(std::int32_t));
    const std::int32_t d = 8;
    cmd.DeltaFieldRaw(e, id, offsetof(Pair, b), NumericFieldKind::Int32, &d, sizeof(std::int32_t));
    cmd.Flush();

    EXPECT_EQ(ReadPair(world, e, id).a, 42);
    EXPECT_EQ(ReadPair(world, e, id).b, 10);
}
