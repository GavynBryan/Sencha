// A chunk's columns are laid out with each column aligned for its type, so how
// many rows fit has to count the padding between columns, not only their sizes.

#include <ecs/World.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{
struct OneByte
{
    char Value = 0;
};

struct WideA
{
    std::int64_t Value = 0;
};

struct SevenBytes
{
    char Value[7] = {};
};

struct WideB
{
    std::int64_t Value = 0;
};
}

SENCHA_DECLARE_COMPONENT_TYPE(OneByte, "test.layout.one_byte");
SENCHA_DECLARE_COMPONENT_TYPE(WideA, "test.layout.wide_a");
SENCHA_DECLARE_COMPONENT_TYPE(SevenBytes, "test.layout.seven_bytes");
SENCHA_DECLARE_COMPONENT_TYPE(WideB, "test.layout.wide_b");

// One byte, eight aligned, seven bytes, eight aligned: rows sized by bytes alone
// leave less slack than the two pads need.
TEST(ArchetypeLayout, PaddingBetweenColumnsIsCountedWhenSizingAChunk)
{
    World world;
    world.RegisterComponent<OneByte>();
    world.RegisterComponent<WideA>();
    world.RegisterComponent<SevenBytes>();
    world.RegisterComponent<WideB>();

    std::vector<EntityId> entities;
    for (int i = 0; i < 1200; ++i)
    {
        const EntityId entity = world.CreateEntity();
        world.AddComponent<OneByte>(entity, OneByte{ static_cast<char>(i) });
        world.AddComponent<WideA>(entity, WideA{ i * 3 });
        world.AddComponent<SevenBytes>(entity, SevenBytes{ { static_cast<char>(i), 0, 0, 0, 0, 0, static_cast<char>(i + 1) } });
        world.AddComponent<WideB>(entity, WideB{ -i });
        entities.push_back(entity);
    }
    for (int i = 0; i < 1200; ++i)
    {
        const EntityId entity = entities[static_cast<std::size_t>(i)];
        EXPECT_EQ(world.TryGet<OneByte>(entity)->Value, static_cast<char>(i));
        EXPECT_EQ(world.TryGet<WideA>(entity)->Value, i * 3);
        EXPECT_EQ(world.TryGet<SevenBytes>(entity)->Value[6], static_cast<char>(i + 1));
        EXPECT_EQ(world.TryGet<WideB>(entity)->Value, -i) << "no column overlaps the next";
    }
}
